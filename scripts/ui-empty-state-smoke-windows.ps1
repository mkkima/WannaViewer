[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [string]$CapturePath = ''
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

function Invoke-UiAction([IntPtr]$Window, [int]$Action) {
    if ([WannaViewerQtSmokeNative]::SendMessage($Window, 0x8251, [IntPtr]$Action, [IntPtr]::Zero) -eq [IntPtr]::Zero) {
        throw "Background UI action $Action failed"
    }
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add('--background-ui-test')
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

    Invoke-UiAction $main 1
    if (-not (Query-UiState $main 2)) { throw 'Embedded Qt URL overlay did not appear' }
    if (Query-UiState $main 1) { throw 'Empty state remained visible under the URL overlay' }
    if ([WannaViewerQtSmokeNative]::GetForegroundWindow() -eq $main) {
        throw 'Opening the embedded URL overlay stole foreground focus'
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

    Invoke-UiAction $main 2
    if (Query-UiState $main 2) { throw 'Embedded Qt URL overlay did not close' }
    if (-not (Query-UiState $main 1)) { throw 'Empty state did not return after closing the URL overlay' }
    Invoke-UiAction $main 5
    if (-not $process.WaitForExit(10000)) { throw 'Qt player did not close cleanly' }
    if ($process.ExitCode -ne 0) { throw "Qt player exited with code $($process.ExitCode)" }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

Write-Host 'Background Qt empty-state smoke passed without using the foreground desktop.'
