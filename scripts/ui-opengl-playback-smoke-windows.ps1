[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [Parameter(Mandatory=$true)][string]$Media,
    [switch]$VerifyAnime4KModes
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
if (-not (Test-Path -LiteralPath $Media)) { throw "Media not found: $Media" }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WannaViewerOpenGlSmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@

function Find-PlayerWindow([int]$ProcessId) {
    $script:openGlSmokeWindow = [IntPtr]::Zero
    $callback = [WannaViewerOpenGlSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerOpenGlSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId -and [WannaViewerOpenGlSmokeNative]::IsWindowVisible($window)) {
            $title = [Text.StringBuilder]::new(256)
            [void][WannaViewerOpenGlSmokeNative]::GetWindowText($window, $title, $title.Capacity)
            if ($title.ToString().StartsWith('WannaViewer')) {
                $script:openGlSmokeWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerOpenGlSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:openGlSmokeWindow
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add('--background-render-test')
$start.ArgumentList.Add((Resolve-Path -LiteralPath $Media).Path)
$process = [Diagnostics.Process]::Start($start)
try {
    [void]$process.WaitForInputIdle(10000)
    $window = [IntPtr]::Zero
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 40
        $process.Refresh()
        $window = Find-PlayerWindow $process.Id
    } while (-not $process.HasExited -and $window -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $startupDeadline)
    if ($process.HasExited -or $window -eq [IntPtr]::Zero) {
        $detail = if ($process.HasExited) { "process exit code $($process.ExitCode)" } else { 'startup timed out' }
        throw "Background Qt OpenGL test window did not start: $detail"
    }

    # Cold-loading the 114 MB libmpv DLL can take several seconds while
    # Windows Defender scans a freshly packaged binary. The application's own
    # frame watchdog starts at MPV_EVENT_START_FILE, not at process creation.
    $playbackDeadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        $clockAdvanced = [WannaViewerOpenGlSmokeNative]::SendMessage(
            $window, 0x8250, [IntPtr]5, [IntPtr]::Zero) -ne [IntPtr]::Zero
        $frameRendered = [WannaViewerOpenGlSmokeNative]::SendMessage(
            $window, 0x8250, [IntPtr]10, [IntPtr]::Zero) -ne [IntPtr]::Zero
        if ($clockAdvanced -and $frameRendered) { break }
        Start-Sleep -Milliseconds 50
        $process.Refresh()
    } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $playbackDeadline)
    if ($process.HasExited) { throw "Qt OpenGL player exited early with code $($process.ExitCode)" }
    $started = [WannaViewerOpenGlSmokeNative]::SendMessage(
        $window, 0x8250, [IntPtr]5, [IntPtr]::Zero) -ne [IntPtr]::Zero
    $rendered = [WannaViewerOpenGlSmokeNative]::SendMessage(
        $window, 0x8250, [IntPtr]10, [IntPtr]::Zero) -ne [IntPtr]::Zero

    if ($VerifyAnime4KModes) {
        for ($shaderIndex = 1; $shaderIndex -le 6; ++$shaderIndex) {
            [void][WannaViewerOpenGlSmokeNative]::SendMessage(
                $window, 0x8251, [IntPtr]39, [IntPtr]::Zero)
            $applied = [WannaViewerOpenGlSmokeNative]::SendMessage(
                $window, 0x8251, [IntPtr]40, [IntPtr]$shaderIndex) -ne [IntPtr]::Zero
            if (-not $applied) { throw "Unable to apply Anime4K mode index $shaderIndex" }

            $shaderDeadline = [DateTime]::UtcNow.AddSeconds(8)
            do {
                Start-Sleep -Milliseconds 40
                $process.Refresh()
                $frameAfterShader = [WannaViewerOpenGlSmokeNative]::SendMessage(
                    $window, 0x8250, [IntPtr]10, [IntPtr]::Zero) -ne [IntPtr]::Zero
            } while (-not $process.HasExited -and -not $frameAfterShader -and
                     [DateTime]::UtcNow -lt $shaderDeadline)
            if ($process.HasExited) { throw "Qt OpenGL player exited while applying Anime4K mode index $shaderIndex" }
            if (-not $frameAfterShader) { throw "Anime4K mode index $shaderIndex did not produce a rendered frame" }
            $activeIndex = [WannaViewerOpenGlSmokeNative]::SendMessage(
                $window, 0x8250, [IntPtr]27, [IntPtr]::Zero).ToInt64()
            if ($activeIndex -ne $shaderIndex) {
                throw "Anime4K mode index $shaderIndex was not retained; active index is $activeIndex"
            }
            $shaderErrorVisible = [WannaViewerOpenGlSmokeNative]::SendMessage(
                $window, 0x8250, [IntPtr]31, [IntPtr]::Zero) -ne [IntPtr]::Zero
            if ($shaderErrorVisible) { throw "Anime4K mode index $shaderIndex opened an error dialog" }
        }
    }

    [void][WannaViewerOpenGlSmokeNative]::SendMessage(
        $window, 0x8251, [IntPtr]5, [IntPtr]::Zero)
    if (-not $process.WaitForExit(30000)) { throw 'Qt OpenGL player did not close cleanly within 30 seconds' }
    if ($process.ExitCode -ne 0) { throw "Qt OpenGL player exited with code $($process.ExitCode)" }
    if (-not $started) { throw 'Qt/libmpv OpenGL playback clock did not advance' }
    if (-not $rendered) { throw 'Qt/libmpv OpenGL framebuffer stayed black' }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

$detail = if ($VerifyAnime4KModes) { ' and all Anime4K modes rendered' } else { '' }
Write-Host "Background Qt/libmpv OpenGL smoke passed: real video output advanced$detail and closed cleanly."
