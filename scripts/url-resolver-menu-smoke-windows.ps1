[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [switch]$VerifyYummyPlayback,
    [switch]$Interactive,
    [string]$CapturePath,
    [string[]]$Urls = @(
        'https://ru.yummyani.me/catalog/item/angel-po-sosedstvu-2',
        'https://animego.me/anime/dlya-tebya-bessmertnyy-3-2855'
    )
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
if ($Interactive) { Write-Warning '-Interactive is ignored: UI tests always stay off-screen.' }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WannaViewerQtResolverSmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr target, uint flags);
}
'@

function Find-PlayerWindow([int]$ProcessId) {
    $script:resolverSmokeWindow = [IntPtr]::Zero
    $callback = [WannaViewerQtResolverSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerQtResolverSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId -and
            [WannaViewerQtResolverSmokeNative]::IsWindowVisible($window) -and
            [WannaViewerQtResolverSmokeNative]::GetWindow($window, 4) -eq [IntPtr]::Zero) {
            $title = [Text.StringBuilder]::new(256)
            [void][WannaViewerQtResolverSmokeNative]::GetWindowText($window, $title, $title.Capacity)
            if ($title.ToString().StartsWith('WannaViewer')) {
                $script:resolverSmokeWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerQtResolverSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:resolverSmokeWindow
}

function Get-UiValue([IntPtr]$Window, [int]$State) {
    return [WannaViewerQtResolverSmokeNative]::SendMessage($Window, 0x8250, [IntPtr]$State, [IntPtr]::Zero).ToInt64()
}

function Invoke-UiAction([IntPtr]$Window, [int]$Action) {
    if ([WannaViewerQtResolverSmokeNative]::SendMessage($Window, 0x8251, [IntPtr]$Action, [IntPtr]::Zero) -eq [IntPtr]::Zero) {
        throw "Background UI action $Action failed"
    }
}

$playerPath = (Resolve-Path -LiteralPath $Player).Path
foreach ($url in $Urls) {
    $start = [Diagnostics.ProcessStartInfo]::new($playerPath)
    $start.UseShellExecute = $false
    $start.ArgumentList.Add('--background-ui-test')
    $start.ArgumentList.Add($url)
    $process = [Diagnostics.Process]::Start($start)
    try {
        [void]$process.WaitForInputIdle(10000)
        $windowDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 50
            $process.Refresh()
            $window = Find-PlayerWindow $process.Id
        } while ($window -eq [IntPtr]::Zero -and -not $process.HasExited -and
                 [DateTime]::UtcNow -lt $windowDeadline)
        if ($process.HasExited -or $window -eq [IntPtr]::Zero) { throw "Qt player did not start for $url" }

        $selectorDeadline = [DateTime]::UtcNow.AddSeconds(35)
        do {
            if ((Get-UiValue $window 3) -eq 1) { break }
            Start-Sleep -Milliseconds 100
            $process.Refresh()
        } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $selectorDeadline)
        if ($process.HasExited) { throw "Qt player exited while resolving $url" }
        if ((Get-UiValue $window 3) -ne 1) { throw "Embedded Qt source selector did not appear for $url" }

        $providers = Get-UiValue $window 9
        $hostName = ([Uri]$url).Host
        if ($providers -eq 0 -and $hostName -eq 'animego.me') {
            # The catalog endpoint intentionally lists episodes first. Open the
            # selected episode to fetch its current CVH/AniBoom/Kodik choices.
            Invoke-UiAction $window 8
            $providerDeadline = [DateTime]::UtcNow.AddSeconds(35)
            do {
                Start-Sleep -Milliseconds 100
                $process.Refresh()
                if (-not $process.HasExited -and (Get-UiValue $window 3) -eq 1) {
                    $providers = Get-UiValue $window 9
                }
            } while ($providers -eq 0 -and -not $process.HasExited -and
                     [DateTime]::UtcNow -lt $providerDeadline)
        }
        if ($hostName -eq 'ru.yummyani.me' -and ($providers -band 7) -ne 7) {
            throw "YummyAnime selector is missing CVH, Kodik, or Alloha (provider mask $providers)"
        }
        if ($providers -eq 0) { throw "Source selector contains no recognized provider for $url" }
        Write-Host "Embedded Qt selector resolved provider mask $providers for $url"

        if ($CapturePath) {
            Add-Type -AssemblyName System.Drawing
            $rectangle = [WannaViewerQtResolverSmokeNative+RECT]::new()
            if (-not [WannaViewerQtResolverSmokeNative]::GetWindowRect($window, [ref]$rectangle)) {
                throw 'Unable to read the off-screen source selector rectangle'
            }
            $captureFile = [IO.Path]::GetFullPath($CapturePath)
            $captureDirectory = [IO.Path]::GetDirectoryName($captureFile)
            if ($captureDirectory) { [IO.Directory]::CreateDirectory($captureDirectory) | Out-Null }
            $bitmap = [Drawing.Bitmap]::new($rectangle.Right - $rectangle.Left, $rectangle.Bottom - $rectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $device = $graphics.GetHdc()
            try {
                if (-not [WannaViewerQtResolverSmokeNative]::PrintWindow($window, $device, 2)) {
                    throw 'PrintWindow could not capture the off-screen Qt selector'
                }
            } finally {
                $graphics.ReleaseHdc($device)
                $graphics.Dispose()
            }
            try { $bitmap.Save($captureFile, [Drawing.Imaging.ImageFormat]::Png) }
            finally { $bitmap.Dispose() }
        }

        if ($VerifyYummyPlayback -and $hostName -eq 'ru.yummyani.me') {
            Invoke-UiAction $window 7
            $playbackDeadline = [DateTime]::UtcNow.AddSeconds(45)
            do {
                if ((Get-UiValue $window 5) -eq 1) { break }
                Start-Sleep -Milliseconds 100
                $process.Refresh()
            } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $playbackDeadline)
            if ($process.HasExited -or (Get-UiValue $window 5) -ne 1) {
                throw 'CVH was selected but playback did not produce a frame'
            }
            Write-Host 'CVH selection produced a playback frame.'
        }

        Invoke-UiAction $window 5
        if (-not $process.WaitForExit(10000)) { throw 'Qt player did not close cleanly' }
        if ($process.ExitCode -ne 0) { throw "Qt player exited with code $($process.ExitCode)" }
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

Write-Host 'Background Qt resolver selector smoke passed.'
