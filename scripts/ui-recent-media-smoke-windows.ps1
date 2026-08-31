[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [Parameter(Mandatory=$true)][string]$Media,
    [string]$CapturePath = ''
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
if (-not (Test-Path -LiteralPath $Media)) { throw "Media not found: $Media" }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WannaViewerRecentSmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr target, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@

function Find-PlayerWindow([int]$ProcessId) {
    $script:recentSmokeWindow = [IntPtr]::Zero
    $callback = [WannaViewerRecentSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerRecentSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId -and
            [WannaViewerRecentSmokeNative]::IsWindowVisible($window) -and
            [WannaViewerRecentSmokeNative]::GetWindow($window, 4) -eq [IntPtr]::Zero) {
            $title = [Text.StringBuilder]::new(256)
            [void][WannaViewerRecentSmokeNative]::GetWindowText($window, $title, $title.Capacity)
            if ($title.ToString().StartsWith('WannaViewer')) {
                $script:recentSmokeWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerRecentSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:recentSmokeWindow
}

function Get-UiValue([IntPtr]$Window, [int]$State) {
    return [WannaViewerRecentSmokeNative]::SendMessage($Window, 0x8250, [IntPtr]$State, [IntPtr]::Zero).ToInt64()
}

function Invoke-UiAction([IntPtr]$Window, [int]$Action) {
    if ([WannaViewerRecentSmokeNative]::SendMessage($Window, 0x8251, [IntPtr]$Action, [IntPtr]::Zero) -eq [IntPtr]::Zero) {
        throw "Background UI action $Action failed"
    }
}

function Start-TestPlayer([string]$InputMedia = '') {
    $start = [Diagnostics.ProcessStartInfo]::new($script:playerPath)
    $start.UseShellExecute = $false
    $start.ArgumentList.Add('--background-ui-test')
    if ($InputMedia) { $start.ArgumentList.Add($InputMedia) }
    $process = [Diagnostics.Process]::Start($start)
    [void]$process.WaitForInputIdle(10000)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 40
        $process.Refresh()
        $window = Find-PlayerWindow $process.Id
    } while (-not $process.HasExited -and $window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or $window -eq [IntPtr]::Zero) { throw 'Qt player window did not start' }
    return [pscustomobject]@{ Process = $process; Window = $window }
}

function Wait-ForPlayback($Session, [string]$Stage) {
    $deadline = [DateTime]::UtcNow.AddSeconds(35)
    do {
        if ((Get-UiValue $Session.Window 5) -eq 1) { return }
        Start-Sleep -Milliseconds 50
        $Session.Process.Refresh()
    } while (-not $Session.Process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    $title = [Text.StringBuilder]::new(256)
    [void][WannaViewerRecentSmokeNative]::GetWindowText($Session.Window, $title, $title.Capacity)
    throw "Playback did not start during $Stage (loaded=$(Get-UiValue $Session.Window 4), title=$title)"
}

function Close-TestPlayer($Session) {
    Invoke-UiAction $Session.Window 5
    if (-not $Session.Process.WaitForExit(10000)) { throw 'Qt player did not close cleanly' }
    if ($Session.Process.ExitCode -ne 0) { throw "Qt player exited with code $($Session.Process.ExitCode)" }
}

$playerPath = (Resolve-Path -LiteralPath $Player).Path
$sourceMediaPath = (Resolve-Path -LiteralPath $Media).Path
$temporaryMediaName = 'wannaviewer-recent-тест-' + [guid]::NewGuid().ToString('N') + [IO.Path]::GetExtension($sourceMediaPath)
$mediaPath = Join-Path ([IO.Path]::GetTempPath()) $temporaryMediaName
Copy-Item -LiteralPath $sourceMediaPath -Destination $mediaPath
$configDirectory = Join-Path (Split-Path -Parent $playerPath) 'config'
$recentPath = Join-Path $configDirectory 'recent-media.json'
$playbackPath = Join-Path $configDirectory 'playback-state.json'
$backups = @{}
foreach ($path in @($recentPath, $playbackPath)) {
    $backups[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
}
$session = $null
try {
    foreach ($path in @($recentPath, $playbackPath)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }

    $session = Start-TestPlayer $mediaPath
    Wait-ForPlayback $session 'initial open'
    Close-TestPlayer $session
    $session = $null
    if (-not (Test-Path -LiteralPath $recentPath)) { throw 'Successful playback did not create recent-media.json' }
    $saved = Get-Content -LiteralPath $recentPath -Raw | ConvertFrom-Json
    if ($saved.schema_version -ne 1 -or $saved.entries.Count -ne 1) {
        throw 'Successful playback did not create exactly one valid recent entry'
    }
    if ([IO.Path]::GetFullPath($saved.entries[0].open_value) -ne $mediaPath) {
        throw 'Recent entry did not preserve the absolute local media path'
    }

    $session = Start-TestPlayer
    if ((Get-UiValue $session.Window 1) -ne 1 -or (Get-UiValue $session.Window 28) -ne 1 -or
        (Get-UiValue $session.Window 29) -ne 1) {
        throw 'Recent media was not available on the empty start screen'
    }
    if ($CapturePath) {
        Add-Type -AssemblyName System.Drawing
        $rectangle = [WannaViewerRecentSmokeNative+RECT]::new()
        if (-not [WannaViewerRecentSmokeNative]::GetWindowRect($session.Window, [ref]$rectangle)) {
            throw 'Unable to read the recent-media window rectangle'
        }
        $captureFile = [IO.Path]::GetFullPath($CapturePath)
        $captureDirectory = [IO.Path]::GetDirectoryName($captureFile)
        if ($captureDirectory) { [IO.Directory]::CreateDirectory($captureDirectory) | Out-Null }
        $bitmap = [Drawing.Bitmap]::new($rectangle.Right - $rectangle.Left, $rectangle.Bottom - $rectangle.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $device = $graphics.GetHdc()
        try {
            if (-not [WannaViewerRecentSmokeNative]::PrintWindow($session.Window, $device, 2)) {
                throw 'PrintWindow could not capture the recent-media start screen'
            }
        } finally {
            $graphics.ReleaseHdc($device)
            $graphics.Dispose()
        }
        try { $bitmap.Save($captureFile, [Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    }
    Invoke-UiAction $session.Window 29
    Wait-ForPlayback $session 'one-click recent reopen'
    Close-TestPlayer $session
    $session = $null

    $session = Start-TestPlayer
    if ((Get-UiValue $session.Window 28) -ne 1) { throw 'Reopened media was not promoted in recent history' }
    Invoke-UiAction $session.Window 31
    if ((Get-UiValue $session.Window 28) -ne 0) { throw 'Clear recent retained entries in the start screen' }
    Close-TestPlayer $session
    $session = $null

    $session = Start-TestPlayer $mediaPath
    Wait-ForPlayback $session 'missing-file setup'
    Close-TestPlayer $session
    $session = $null
    Remove-Item -LiteralPath $mediaPath -Force

    $session = Start-TestPlayer
    if ((Get-UiValue $session.Window 28) -ne 1 -or (Get-UiValue $session.Window 29) -ne 0) {
        throw 'A missing local file was not retained as an unavailable recent entry'
    }
    Invoke-UiAction $session.Window 30
    if ((Get-UiValue $session.Window 28) -ne 0) { throw 'Remove retained the selected missing-file entry' }
    Close-TestPlayer $session
    $session = $null
} finally {
    if ($session -and -not $session.Process.HasExited) {
        $session.Process.Kill()
        $session.Process.WaitForExit()
    }
    foreach ($path in @($recentPath, $playbackPath)) {
        $bytes = $backups[$path]
        if ($null -ne $bytes) {
            [IO.File]::WriteAllBytes($path, $bytes)
        } elseif (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    if (Test-Path -LiteralPath $mediaPath) { Remove-Item -LiteralPath $mediaPath -Force }
}

Write-Host 'Background Qt recent-media smoke passed: persist, one-click reopen, clear, missing-file state, and remove.'
