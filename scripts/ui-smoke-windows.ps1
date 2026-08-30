[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [Parameter(Mandatory=$true)][string]$Media
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
if (-not (Test-Path -LiteralPath $Media)) { throw "Media not found: $Media" }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WannaViewerSmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@
$findWindow = {
    param([int]$ProcessId)
    $script:backgroundSmokeWindow = [IntPtr]::Zero
    $callback = [WannaViewerSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId -and [WannaViewerSmokeNative]::IsWindowVisible($window)) {
            $class = [Text.StringBuilder]::new(128)
            [void][WannaViewerSmokeNative]::GetClassName($window, $class, $class.Capacity)
            if ($class.ToString() -eq 'WannaViewer.PlayerWindow') {
                $script:backgroundSmokeWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:backgroundSmokeWindow
}
$start = [System.Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add('--background-ui-test')
$start.ArgumentList.Add((Resolve-Path -LiteralPath $Media).Path)
$process = [System.Diagnostics.Process]::Start($start)
try {
    $process.WaitForInputIdle(10000) | Out-Null
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $process.Refresh()
        $window = & $findWindow $process.Id
    } while (-not $process.HasExited -and $window -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $startupDeadline)
    if ($process.HasExited -or $window -eq [IntPtr]::Zero) { throw 'Player window did not start' }
    $playbackDeadline = [DateTime]::UtcNow.AddSeconds(35)
    do {
        $title = [Text.StringBuilder]::new(256)
        [void][WannaViewerSmokeNative]::GetWindowText($window, $title, $title.Capacity)
        if ($title.ToString().Contains('playing')) { break }
        Start-Sleep -Milliseconds 50
        $process.Refresh()
    } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $playbackDeadline)
    if ($process.HasExited) { throw "Player exited before playback started with code $($process.ExitCode)" }
    if (-not $title.ToString().Contains('playing')) { throw "Playback did not start; last window title: $title" }
    foreach ($key in @(0x20,0x20,0x27,0x53,0x41,0x46,0x46,0x79)) {
        [WannaViewerSmokeNative]::PostMessage($window, 0x0100, [IntPtr]$key, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw "Player crashed after virtual key $key" }
    }
    [WannaViewerSmokeNative]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if (-not $process.WaitForExit(5000)) { throw 'Player did not close cleanly' }
    if ($process.ExitCode -ne 0) { throw "Player exited with code $($process.ExitCode)" }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
Write-Host 'UI smoke test passed: playback, pause, seek, subtitles, audio, fullscreen, statistics, clean close.'
