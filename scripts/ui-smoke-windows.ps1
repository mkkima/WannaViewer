[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [Parameter(Mandatory=$true)][string]$Media,
    [switch]$SkipInteractions,
    [string]$CapturePath = ''
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
if (-not (Test-Path -LiteralPath $Media)) { throw "Media not found: $Media" }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WannaViewerQtPlaybackSmokeNative {
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
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@

function Find-PlayerWindow([int]$ProcessId) {
    $script:playbackSmokeWindow = [IntPtr]::Zero
    $callback = [WannaViewerQtPlaybackSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerQtPlaybackSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId -and
            [WannaViewerQtPlaybackSmokeNative]::IsWindowVisible($window) -and
            [WannaViewerQtPlaybackSmokeNative]::GetWindow($window, 4) -eq [IntPtr]::Zero) {
            $title = [Text.StringBuilder]::new(256)
            [void][WannaViewerQtPlaybackSmokeNative]::GetWindowText($window, $title, $title.Capacity)
            if ($title.ToString().StartsWith('WannaViewer')) {
                $script:playbackSmokeWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerQtPlaybackSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:playbackSmokeWindow
}

function Get-UiValue([IntPtr]$Window, [int]$State) {
    return [WannaViewerQtPlaybackSmokeNative]::SendMessage($Window, 0x8250, [IntPtr]$State, [IntPtr]::Zero).ToInt64()
}

function Invoke-UiAction([IntPtr]$Window, [int]$Action) {
    if ([WannaViewerQtPlaybackSmokeNative]::SendMessage($Window, 0x8251, [IntPtr]$Action, [IntPtr]::Zero) -eq [IntPtr]::Zero) {
        throw "Background UI action $Action failed"
    }
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add('--background-ui-test')
$start.ArgumentList.Add((Resolve-Path -LiteralPath $Media).Path)
$process = [Diagnostics.Process]::Start($start)
try {
    [void]$process.WaitForInputIdle(10000)
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 40
        $process.Refresh()
        $window = Find-PlayerWindow $process.Id
    } while (-not $process.HasExited -and $window -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $startupDeadline)
    if ($process.HasExited -or $window -eq [IntPtr]::Zero) { throw 'Qt player window did not start' }

    $playbackDeadline = [DateTime]::UtcNow.AddSeconds(35)
    do {
        if ((Get-UiValue $window 5) -eq 1) { break }
        Start-Sleep -Milliseconds 50
        $process.Refresh()
    } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $playbackDeadline)
    if ($process.HasExited) { throw "Qt player exited before playback started with code $($process.ExitCode)" }
    if ((Get-UiValue $window 5) -ne 1) {
        $title = [Text.StringBuilder]::new(256)
        [void][WannaViewerQtPlaybackSmokeNative]::GetWindowText($window, $title, $title.Capacity)
        throw "Playback did not produce a frame; last window title: $title"
    }
    if ((Get-UiValue $window 4) -ne 1) { throw 'Playback started without the loaded-media UI state' }

    if (-not $SkipInteractions) {
        Invoke-UiAction $window 3
        if ((Get-UiValue $window 6) -ne 1) { throw 'Playback controls did not appear' }
        if ((Get-UiValue $window 11) -ne 1000) { throw 'Playback controls appeared with incomplete opacity' }
        if ($CapturePath) {
            Add-Type -AssemblyName System.Drawing
            $rectangle = [WannaViewerQtPlaybackSmokeNative+RECT]::new()
            if (-not [WannaViewerQtPlaybackSmokeNative]::GetWindowRect($window, [ref]$rectangle)) {
                throw 'Unable to read the off-screen playback window rectangle'
            }
            $captureFile = [IO.Path]::GetFullPath($CapturePath)
            $captureDirectory = [IO.Path]::GetDirectoryName($captureFile)
            if ($captureDirectory) { [IO.Directory]::CreateDirectory($captureDirectory) | Out-Null }
            $bitmap = [Drawing.Bitmap]::new($rectangle.Right - $rectangle.Left, $rectangle.Bottom - $rectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $device = $graphics.GetHdc()
            try {
                if (-not [WannaViewerQtPlaybackSmokeNative]::PrintWindow($window, $device, 2)) {
                    throw 'PrintWindow could not capture the off-screen Qt playback UI'
                }
            } finally {
                $graphics.ReleaseHdc($device)
                $graphics.Dispose()
            }
            try { $bitmap.Save($captureFile, [Drawing.Imaging.ImageFormat]::Png) }
            finally { $bitmap.Dispose() }
        }
        Invoke-UiAction $window 4
        if ((Get-UiValue $window 6) -ne 0) { throw 'Playback controls did not hide cleanly' }
        if ((Get-UiValue $window 11) -ne 0) { throw 'Hidden playback controls retained opacity' }
        Invoke-UiAction $window 3
        if ((Get-UiValue $window 6) -ne 1) { throw 'Playback controls did not reappear cleanly' }
        if ((Get-UiValue $window 11) -ne 1000) { throw 'Playback controls reappeared with incomplete opacity' }

        Invoke-UiAction $window 6
        if ((Get-UiValue $window 7) -lt 4900 -or (Get-UiValue $window 7) -gt 5100) {
            throw "Timeline did not retain the exact selected position: $(Get-UiValue $window 7)"
        }
        Start-Sleep -Milliseconds 500
        if ((Get-UiValue $window 8) -le 0) { throw 'Timeline seek did not advance playback position' }

        foreach ($key in @(0x20,0x20,0x27,0x53,0x41,0x46,0x46,0x79)) {
            [void][WannaViewerQtPlaybackSmokeNative]::PostMessage($window, 0x0100, [IntPtr]$key, [IntPtr]::Zero)
            [void][WannaViewerQtPlaybackSmokeNative]::PostMessage($window, 0x0101, [IntPtr]$key, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 150
            $process.Refresh()
            if ($process.HasExited) { throw "Qt player crashed after virtual key $key" }
        }
    }

    Invoke-UiAction $window 5
    if (-not $process.WaitForExit(10000)) { throw 'Qt player did not close cleanly' }
    if ($process.ExitCode -ne 0) { throw "Qt player exited with code $($process.ExitCode)" }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

if ($SkipInteractions) {
    Write-Host 'Background Qt playback smoke passed: first frame and clean close.'
} else {
    Write-Host 'Background Qt playback smoke passed: first frame, controls, timeline, hotkeys, clean close.'
}
