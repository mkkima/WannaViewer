[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [string]$CapturePath = '',
    [switch]$VerifyMotion
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WannaViewerQtSmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr target, uint flags);
}
'@

function Find-PlayerWindow([int]$ProcessId) {
    $script:foundQtWindow = [IntPtr]::Zero
    $callback = [WannaViewerQtSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerQtSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId -and
            [WannaViewerQtSmokeNative]::IsWindowVisible($window) -and
            [WannaViewerQtSmokeNative]::GetWindow($window, 4) -eq [IntPtr]::Zero) {
            $title = [Text.StringBuilder]::new(256)
            [void][WannaViewerQtSmokeNative]::GetWindowText($window, $title, $title.Capacity)
            if ($title.ToString().StartsWith('WannaViewer')) {
                $script:foundQtWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerQtSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:foundQtWindow
}

function Query-UiState([IntPtr]$Window, [int]$State) {
    return [WannaViewerQtSmokeNative]::SendMessage($Window, 0x8250, [IntPtr]$State, [IntPtr]::Zero) -ne [IntPtr]::Zero
}

function Get-UiNumber([IntPtr]$Window, [int]$State) {
    return [WannaViewerQtSmokeNative]::SendMessage($Window, 0x8250, [IntPtr]$State, [IntPtr]::Zero).ToInt64()
}

function Invoke-UiAction([IntPtr]$Window, [int]$Action) {
    if ([WannaViewerQtSmokeNative]::SendMessage($Window, 0x8251, [IntPtr]$Action, [IntPtr]::Zero) -eq [IntPtr]::Zero) {
        throw "Background UI action $Action failed"
    }
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add($(if ($VerifyMotion) { '--background-motion-test' } else { '--background-ui-test' }))
$process = [Diagnostics.Process]::Start($start)
try {
    [void]$process.WaitForInputIdle(10000)
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 40
        $process.Refresh()
        $main = Find-PlayerWindow $process.Id
    } while ($main -eq [IntPtr]::Zero -and -not $process.HasExited -and
             [DateTime]::UtcNow -lt $startupDeadline)
    if ($process.HasExited -or $main -eq [IntPtr]::Zero) { throw 'Qt player window did not start' }

    $mainRect = [WannaViewerQtSmokeNative+RECT]::new()
    if (-not [WannaViewerQtSmokeNative]::GetWindowRect($main, [ref]$mainRect)) {
        throw 'Unable to read the Qt player window rectangle'
    }
    $virtualLeft = [WannaViewerQtSmokeNative]::GetSystemMetrics(76)
    $virtualTop = [WannaViewerQtSmokeNative]::GetSystemMetrics(77)
    $virtualRight = $virtualLeft + [WannaViewerQtSmokeNative]::GetSystemMetrics(78)
    $virtualBottom = $virtualTop + [WannaViewerQtSmokeNative]::GetSystemMetrics(79)
    if ($mainRect.Left -lt $virtualRight -and $mainRect.Right -gt $virtualLeft -and
        $mainRect.Top -lt $virtualBottom -and $mainRect.Bottom -gt $virtualTop) {
        throw 'Background Qt smoke window intersects the visible virtual desktop'
    }
    if ([WannaViewerQtSmokeNative]::GetForegroundWindow() -eq $main) {
        throw 'Background Qt smoke window stole foreground focus'
    }
    if (-not (Query-UiState $main 1)) { throw 'Empty state or its open buttons are not visible' }
    if (Query-UiState $main 2) { throw 'URL overlay is unexpectedly visible on startup' }
    if (Query-UiState $main 6) { throw 'Playback controls are unexpectedly visible without media' }
    if ($VerifyMotion -and (Get-UiNumber $main 13) -ne 1000) {
        throw 'Empty state did not start fully opaque before the motion test'
    }

    Invoke-UiAction $main $(if ($VerifyMotion) { 9 } else { 1 })
    if (-not (Query-UiState $main 2)) { throw 'Embedded Qt URL overlay did not appear' }
    if (-not $VerifyMotion -and (Query-UiState $main 1)) {
        throw 'Empty state remained visible under the URL overlay'
    }
    if ([WannaViewerQtSmokeNative]::GetForegroundWindow() -eq $main) {
        throw 'Opening the embedded URL overlay stole foreground focus'
    }
    if ($VerifyMotion) {
        $overlayOpacity = Get-UiNumber $main 12
        $emptyOpacity = Get-UiNumber $main 13
        $scrimOpacity = Get-UiNumber $main 14
        $overlayOffset = Get-UiNumber $main 15
        if ($overlayOpacity -le 0 -or $overlayOpacity -ge 1000 -or
            $emptyOpacity -le 0 -or $emptyOpacity -ge 1000 -or
            $scrimOpacity -le 0 -or $scrimOpacity -ge 1000 -or
            $overlayOffset -le 0 -or $overlayOffset -ge 16) {
            throw "Modal entrance was not paused at an intermediate frame: overlay=$overlayOpacity empty=$emptyOpacity scrim=$scrimOpacity offset=$overlayOffset"
        }
        Invoke-UiAction $main 10
        $motionDeadline = [DateTime]::UtcNow.AddSeconds(2)
        do {
            Start-Sleep -Milliseconds 20
        } while (((Get-UiNumber $main 12) -ne 1000 -or (Get-UiNumber $main 13) -ne 0 -or
                  (Get-UiNumber $main 14) -ne 1000 -or (Get-UiNumber $main 15) -ne 0) -and
                 [DateTime]::UtcNow -lt $motionDeadline)
        if ((Get-UiNumber $main 12) -ne 1000 -or (Get-UiNumber $main 13) -ne 0 -or
            (Get-UiNumber $main 14) -ne 1000 -or (Get-UiNumber $main 15) -ne 0) {
            throw 'URL overlay entrance animation did not settle at its exact target state'
        }
    }

    if ($CapturePath) {
        Add-Type -AssemblyName System.Drawing
        $captureFile = [IO.Path]::GetFullPath($CapturePath)
        $captureDirectory = [IO.Path]::GetDirectoryName($captureFile)
        if ($captureDirectory) { [IO.Directory]::CreateDirectory($captureDirectory) | Out-Null }
        $width = $mainRect.Right - $mainRect.Left
        $height = $mainRect.Bottom - $mainRect.Top
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $device = $graphics.GetHdc()
        try {
            if (-not [WannaViewerQtSmokeNative]::PrintWindow($main, $device, 2)) {
                throw 'PrintWindow could not capture the off-screen Qt window'
            }
        } finally {
            $graphics.ReleaseHdc($device)
            $graphics.Dispose()
        }
        try { $bitmap.Save($captureFile, [Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    }

    Invoke-UiAction $main $(if ($VerifyMotion) { 11 } else { 2 })
    if (Query-UiState $main 2) { throw 'Embedded Qt URL overlay did not close' }
    if (-not (Query-UiState $main 1)) { throw 'Empty state did not return after closing the URL overlay' }
    if ($VerifyMotion) {
        $overlayOpacity = Get-UiNumber $main 12
        $emptyOpacity = Get-UiNumber $main 13
        $scrimOpacity = Get-UiNumber $main 14
        if ($overlayOpacity -le 0 -or $overlayOpacity -ge 1000 -or
            $emptyOpacity -le 0 -or $emptyOpacity -ge 1000 -or
            $scrimOpacity -le 0 -or $scrimOpacity -ge 1000) {
            throw "Modal exit was not paused at an intermediate frame: overlay=$overlayOpacity empty=$emptyOpacity scrim=$scrimOpacity"
        }
        Invoke-UiAction $main 12
        $motionDeadline = [DateTime]::UtcNow.AddSeconds(2)
        do {
            Start-Sleep -Milliseconds 20
        } while (((Get-UiNumber $main 12) -ne 0 -or (Get-UiNumber $main 13) -ne 1000 -or
                  (Get-UiNumber $main 14) -ne 0) -and [DateTime]::UtcNow -lt $motionDeadline)
        if ((Get-UiNumber $main 12) -ne 0 -or (Get-UiNumber $main 13) -ne 1000 -or
            (Get-UiNumber $main 14) -ne 0) {
            throw 'Modal exit animation did not settle at its exact target state'
        }
    }
    Invoke-UiAction $main 5
    if (-not $process.WaitForExit(10000)) { throw 'Qt player did not close cleanly' }
    if ($process.ExitCode -ne 0) { throw "Qt player exited with code $($process.ExitCode)" }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

if ($VerifyMotion) {
    Write-Host 'Background Qt motion smoke passed: fade, slide, scrim, settled states, clean close.'
} else {
    Write-Host 'Background Qt empty-state smoke passed without using the foreground desktop.'
}
