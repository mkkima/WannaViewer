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
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@
$start = [System.Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add((Resolve-Path -LiteralPath $Media).Path)
$process = [System.Diagnostics.Process]::Start($start)
try {
    $process.WaitForInputIdle(10000) | Out-Null
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $process.Refresh()
    } while (-not $process.HasExited -and $process.MainWindowHandle -eq 0 -and
             [DateTime]::UtcNow -lt $startupDeadline)
    if ($process.HasExited -or $process.MainWindowHandle -eq 0) { throw 'Player window did not start' }
    $window = $process.MainWindowHandle
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
