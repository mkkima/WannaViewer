[CmdletBinding()]
param([string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe")

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WannaViewerEmptySmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] public static extern int GetWindowRgn(IntPtr window, IntPtr region);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr window, ref POINT point);
    [DllImport("user32.dll")] public static extern IntPtr ChildWindowFromPointEx(IntPtr parent, POINT point, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr value);
}
'@

function Find-ProcessWindow([int]$ProcessId, [string]$ClassName) {
    $script:foundWindow = [IntPtr]::Zero
    $callback = [WannaViewerEmptySmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerEmptySmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessId) {
            $class = [Text.StringBuilder]::new(128)
            [void][WannaViewerEmptySmokeNative]::GetClassName($window, $class, $class.Capacity)
            if ($class.ToString() -eq $ClassName) {
                $script:foundWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerEmptySmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:foundWindow
}

function Assert-NoBinaryRegion([IntPtr]$Window, [string]$Label) {
    $region = [WannaViewerEmptySmokeNative]::CreateRectRgn(0, 0, 1, 1)
    if ($region -eq [IntPtr]::Zero) { throw "Unable to allocate a test region for $Label" }
    try {
        # GetWindowRgn returns ERROR (0) when no 1-bit mask can create stair-stepped edges.
        $kind = [WannaViewerEmptySmokeNative]::GetWindowRgn($Window, $region)
        if ($kind -ne 0) { throw "$Label uses a binary window region that disables smooth edges (kind $kind)" }
    } finally {
        [void][WannaViewerEmptySmokeNative]::DeleteObject($region)
    }
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add('--background-ui-test')
$process = [Diagnostics.Process]::Start($start)
try {
    [void]$process.WaitForInputIdle(10000)
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $process.Refresh()
        $main = Find-ProcessWindow $process.Id 'WannaViewer.PlayerWindow'
    } while ($main -eq [IntPtr]::Zero -and -not $process.HasExited -and
             [DateTime]::UtcNow -lt $startupDeadline)
    if ($process.HasExited -or $main -eq [IntPtr]::Zero) { throw 'Player window did not start' }
    $mainRect = [WannaViewerEmptySmokeNative+RECT]::new()
    [void][WannaViewerEmptySmokeNative]::GetWindowRect($main, [ref]$mainRect)
    $virtualLeft = [WannaViewerEmptySmokeNative]::GetSystemMetrics(76)
    $virtualTop = [WannaViewerEmptySmokeNative]::GetSystemMetrics(77)
    $virtualRight = $virtualLeft + [WannaViewerEmptySmokeNative]::GetSystemMetrics(78)
    $virtualBottom = $virtualTop + [WannaViewerEmptySmokeNative]::GetSystemMetrics(79)
    if ($mainRect.Left -lt $virtualRight -and $mainRect.Right -gt $virtualLeft -and
        $mainRect.Top -lt $virtualBottom -and $mainRect.Bottom -gt $virtualTop) {
        throw 'Background UI smoke window intersects the visible virtual desktop'
    }
    if ([WannaViewerEmptySmokeNative]::GetForegroundWindow() -eq $main) {
        throw 'Background UI smoke window stole foreground focus'
    }
    $controls = @{}
    $childCallback = [WannaViewerEmptySmokeNative+EnumProcedure] {
        param($window, $data)
        $id = [WannaViewerEmptySmokeNative]::GetDlgCtrlID($window)
        if ($id -gt 0) { $controls[$id] = $window }
        return $true
    }
    [void][WannaViewerEmptySmokeNative]::EnumChildWindows($main, $childCallback, [IntPtr]::Zero)
    foreach ($id in @(100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,116,117,
                      150,151,152,153,154,155,156)) {
        if (-not $controls.ContainsKey($id)) { throw "Expected control $id is missing" }
    }
    foreach ($id in @(111,112,114)) {
        if (-not [WannaViewerEmptySmokeNative]::IsWindowVisible($controls[$id])) { throw "Expected empty-state control $id is hidden" }
        $rect = [WannaViewerEmptySmokeNative+RECT]::new()
        [void][WannaViewerEmptySmokeNative]::GetWindowRect($controls[$id], [ref]$rect)
        if ($rect.Left -lt $mainRect.Left -or $rect.Top -lt $mainRect.Top -or
            $rect.Right -gt $mainRect.Right -or $rect.Bottom -gt $mainRect.Bottom) {
            throw "Control $id extends outside the player window"
        }
    }
    foreach ($id in @(111,112)) {
        $buttonRect = [WannaViewerEmptySmokeNative+RECT]::new()
        [void][WannaViewerEmptySmokeNative]::GetWindowRect($controls[$id], [ref]$buttonRect)
        $buttonCenter = [WannaViewerEmptySmokeNative+POINT]::new()
        $buttonCenter.X = [int](($buttonRect.Left + $buttonRect.Right) / 2)
        $buttonCenter.Y = [int](($buttonRect.Top + $buttonRect.Bottom) / 2)
        [void][WannaViewerEmptySmokeNative]::ScreenToClient($main, [ref]$buttonCenter)
        $hit = [WannaViewerEmptySmokeNative]::ChildWindowFromPointEx($main, $buttonCenter, 0)
        if ($hit -ne $controls[$id]) {
            throw "Empty-state button $id is covered by another child window"
        }
    }
    foreach ($id in @(111,112,114)) { Assert-NoBinaryRegion $controls[$id] "Empty-state control $id" }
    foreach ($id in @(100,101,102,103,104,105,106,107,108,109,110,113,116,117,150,151,152,153,154,155,156)) {
        if ([WannaViewerEmptySmokeNative]::IsWindowVisible($controls[$id])) { throw "Control $id should be hidden on the empty state" }
    }

    # BN_CLICKED is zero, so the low word alone is the expected WM_COMMAND payload.
    [void][WannaViewerEmptySmokeNative]::PostMessage($main, 0x0111, [IntPtr]112, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
    } while (-not [WannaViewerEmptySmokeNative]::IsWindowVisible($controls[150]) -and [DateTime]::UtcNow -lt $deadline)
    if (-not [WannaViewerEmptySmokeNative]::IsWindowVisible($controls[150])) { throw 'Embedded URL overlay did not appear' }
    foreach ($id in @(150,151,152,153,155,156)) {
        if (-not [WannaViewerEmptySmokeNative]::IsWindowVisible($controls[$id])) { throw "URL overlay control $id is hidden" }
    }
    foreach ($id in @(150,153,155,156)) { Assert-NoBinaryRegion $controls[$id] "URL overlay control $id" }
    if ([WannaViewerEmptySmokeNative]::IsWindowVisible($controls[154])) { throw 'Choice list is visible in URL overlay' }
    if ((Find-ProcessWindow $process.Id 'WannaViewer.UrlDialog') -ne [IntPtr]::Zero -or
        (Find-ProcessWindow $process.Id '#32768') -ne [IntPtr]::Zero) {
        throw 'URL entry escaped into a separate native window'
    }
    [void][WannaViewerEmptySmokeNative]::PostMessage($main, 0x0111, [IntPtr]156, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    if ([WannaViewerEmptySmokeNative]::IsWindowVisible($controls[150])) { throw 'Embedded URL overlay did not close' }
    if (-not [WannaViewerEmptySmokeNative]::IsWindowVisible($controls[114])) { throw 'Empty state did not return after closing URL overlay' }
    [void][WannaViewerEmptySmokeNative]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Player did not close cleanly' }
    if ($process.ExitCode -ne 0) { throw "Player exited with code $($process.ExitCode)" }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

Write-Host 'Background empty-state UI smoke passed without stealing the screen, taskbar, cursor, or focus.'
