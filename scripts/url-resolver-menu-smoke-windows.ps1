[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [switch]$VerifyYummyPlayback,
    [string[]]$Urls = @(
        'https://ru.yummyani.me/catalog/item/angel-po-sosedstvu-2',
        'https://animego.me/anime/dlya-tebya-bessmertnyy-3-2855'
    )
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;
using System.Runtime.InteropServices;
public static class WannaViewerUrlSmokeNative {
    public delegate bool EnumProcedure(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProcedure callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll")] public static extern int GetWindowRgn(IntPtr window, IntPtr region);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")]
    public static extern IntPtr SendMessageText(IntPtr window, uint message, IntPtr wParam, StringBuilder text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr value);

    public static string[] ListItems(IntPtr list) {
        const uint LB_GETCOUNT = 0x018B;
        const uint LB_GETTEXT = 0x0189;
        const uint LB_GETTEXTLEN = 0x018A;
        int count = SendMessage(list, LB_GETCOUNT, IntPtr.Zero, IntPtr.Zero).ToInt32();
        var items = new List<string>();
        for (int index = 0; index < count; ++index) {
            int length = SendMessage(list, LB_GETTEXTLEN, (IntPtr)index, IntPtr.Zero).ToInt32();
            if (length < 0) continue;
            var text = new StringBuilder(length + 1);
            SendMessageText(list, LB_GETTEXT, (IntPtr)index, text);
            items.Add(text.ToString());
        }
        return items.ToArray();
    }
}
'@

function Find-ProcessWindow([int]$ProcessIdentifier, [string]$ClassName) {
    $script:foundUrlSmokeWindow = [IntPtr]::Zero
    $callback = [WannaViewerUrlSmokeNative+EnumProcedure] {
        param($window, $data)
        $ownerProcess = [uint32]0
        [void][WannaViewerUrlSmokeNative]::GetWindowThreadProcessId($window, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProcessIdentifier -and [WannaViewerUrlSmokeNative]::IsWindowVisible($window)) {
            $class = [Text.StringBuilder]::new(128)
            [void][WannaViewerUrlSmokeNative]::GetClassName($window, $class, $class.Capacity)
            if ($class.ToString() -eq $ClassName) {
                $script:foundUrlSmokeWindow = $window
                return $false
            }
        }
        return $true
    }
    [void][WannaViewerUrlSmokeNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:foundUrlSmokeWindow
}

function Get-Control([IntPtr]$Window, [int]$Id) {
    $control = [WannaViewerUrlSmokeNative]::GetDlgItem($Window, $Id)
    if ($control -eq [IntPtr]::Zero) { throw "Embedded source selector control $Id was not created" }
    return $control
}

function Assert-NoBinaryRegion([IntPtr]$Control, [string]$Label) {
    $region = [WannaViewerUrlSmokeNative]::CreateRectRgn(0, 0, 1, 1)
    if ($region -eq [IntPtr]::Zero) { throw "Unable to allocate a test region for $Label" }
    try {
        $kind = [WannaViewerUrlSmokeNative]::GetWindowRgn($Control, $region)
        if ($kind -ne 0) { throw "$Label uses a binary window region that disables smooth edges (kind $kind)" }
    } finally {
        [void][WannaViewerUrlSmokeNative]::DeleteObject($region)
    }
}

function Select-ListItem([IntPtr]$Window, [IntPtr]$List, [int]$Id, [int]$Index) {
    $LB_SETCURSEL = 0x0186
    $WM_COMMAND = 0x0111
    $LBN_SELCHANGE = 1
    [void][WannaViewerUrlSmokeNative]::SendMessage($List, $LB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
    $command = $Id -bor ($LBN_SELCHANGE -shl 16)
    [void][WannaViewerUrlSmokeNative]::SendMessage($Window, $WM_COMMAND, [IntPtr]$command, $List)
}

function Get-AllSourceLabels([IntPtr]$Window) {
    $seasonList = Get-Control $Window 137
    $voiceList = Get-Control $Window 138
    $episodeList = Get-Control $Window 139
    $sourceList = Get-Control $Window 140
    $labels = [Collections.Generic.List[string]]::new()
    $seasonCount = [WannaViewerUrlSmokeNative]::ListItems($seasonList).Count
    for ($season = 0; $season -lt $seasonCount; ++$season) {
        Select-ListItem $Window $seasonList 137 $season
        $voiceCount = [WannaViewerUrlSmokeNative]::ListItems($voiceList).Count
        for ($voice = 0; $voice -lt $voiceCount; ++$voice) {
            Select-ListItem $Window $voiceList 138 $voice
            $episodeCount = [WannaViewerUrlSmokeNative]::ListItems($episodeList).Count
            for ($episode = 0; $episode -lt $episodeCount; ++$episode) {
                Select-ListItem $Window $episodeList 139 $episode
                foreach ($label in [WannaViewerUrlSmokeNative]::ListItems($sourceList)) { $labels.Add($label) }
            }
        }
    }
    return $labels.ToArray()
}

$playerPath = (Resolve-Path -LiteralPath $Player).Path
foreach ($url in $Urls) {
    $cursorBefore = [WannaViewerUrlSmokeNative+POINT]::new()
    [void][WannaViewerUrlSmokeNative]::GetCursorPos([ref]$cursorBefore)
    $start = [Diagnostics.ProcessStartInfo]::new($playerPath)
    $start.UseShellExecute = $false
    $start.ArgumentList.Add($url)
    $process = [Diagnostics.Process]::Start($start)
    try {
        $selectorClosedByPlayback = $false
        [void]$process.WaitForInputIdle(10000)
        $deadline = [DateTime]::UtcNow.AddSeconds(25)
        $panel = [IntPtr]::Zero
        $window = [IntPtr]::Zero
        do {
            Start-Sleep -Milliseconds 100
            $process.Refresh()
            $window = $process.MainWindowHandle
            if ((Find-ProcessWindow $process.Id '#32770') -ne [IntPtr]::Zero) {
                throw "Resolver showed an error dialog for $url"
            }
            if ((Find-ProcessWindow $process.Id '#32768') -ne [IntPtr]::Zero) {
                throw "Resolver created a native popup menu instead of the embedded selector for $url"
            }
            if ($window -ne [IntPtr]::Zero) { $panel = [WannaViewerUrlSmokeNative]::GetDlgItem($window, 130) }
        } while (($panel -eq [IntPtr]::Zero -or -not [WannaViewerUrlSmokeNative]::IsWindowVisible($panel)) -and
                 [DateTime]::UtcNow -lt $deadline)
        if ($panel -eq [IntPtr]::Zero -or -not [WannaViewerUrlSmokeNative]::IsWindowVisible($panel)) {
            throw "Embedded resolver selector did not appear for $url"
        }
        Assert-NoBinaryRegion $panel 'Source selector panel'

        foreach ($id in 133..140) {
            $control = Get-Control $window $id
            if (-not [WannaViewerUrlSmokeNative]::IsWindowVisible($control)) {
                throw "Embedded source selector control $id is not visible for $url"
            }
        }
        foreach ($id in @(137,138,139,140,142,143)) {
            Assert-NoBinaryRegion (Get-Control $window $id) "Source selector control $id"
        }
        $titleText = [Text.StringBuilder]::new(128)
        [void][WannaViewerUrlSmokeNative]::GetWindowText((Get-Control $window 131), $titleText, $titleText.Capacity)
        if ($titleText.ToString() -ne 'Choose a playback source') { throw 'Embedded selector title is missing' }

        $parsedUrl = [Uri]$url
        if ($parsedUrl.Host -eq 'ru.yummyani.me') {
            $labels = Get-AllSourceLabels $window
            foreach ($provider in @('Alloha','Kodik','CVH')) {
                if (-not ($labels | Where-Object { $_ -like "*$provider*" })) {
                    throw "Embedded source selector does not expose $provider for $url"
                }
            }
            if ($VerifyYummyPlayback) {
                $seasonList = Get-Control $window 137
                $voiceList = Get-Control $window 138
                $episodeList = Get-Control $window 139
                $sourceList = Get-Control $window 140
                Select-ListItem $window $seasonList 137 0
                Select-ListItem $window $voiceList 138 0
                Select-ListItem $window $episodeList 139 0
                $sourceItems = [WannaViewerUrlSmokeNative]::ListItems($sourceList)
                $cvhIndex = -1
                for ($index = 0; $index -lt $sourceItems.Count; ++$index) {
                    if ($sourceItems[$index] -like '*CVH*') { $cvhIndex = $index; break }
                }
                if ($cvhIndex -lt 0) { throw 'CVH is unavailable for the first Yummy episode' }
                Select-ListItem $window $sourceList 140 $cvhIndex
                [void][WannaViewerUrlSmokeNative]::SendMessage($window, 0x0111, [IntPtr]142, [IntPtr]::Zero)
                $playbackDeadline = [DateTime]::UtcNow.AddSeconds(45)
                do {
                    Start-Sleep -Milliseconds 100
                    $process.Refresh()
                } while (-not $process.HasExited -and $process.MainWindowTitle -notlike '*playing*' -and
                         [DateTime]::UtcNow -lt $playbackDeadline)
                if ($process.HasExited) { throw "Player exited while opening CVH with code $($process.ExitCode)" }
                if ($process.MainWindowTitle -notlike '*playing*') {
                    throw 'Open stream did not resolve CVH and advance playback time'
                }
                $selectorClosedByPlayback = $true
                Write-Host 'Embedded selector opened CVH and playback time advanced.'
                [void][WannaViewerUrlSmokeNative]::SendMessage($window, 0x0111, [IntPtr]107, [IntPtr]::Zero)
                $settingsDeadline = [DateTime]::UtcNow.AddSeconds(5)
                $settingsPanel = Get-Control $window 150
                do {
                    Start-Sleep -Milliseconds 50
                } while (-not [WannaViewerUrlSmokeNative]::IsWindowVisible($settingsPanel) -and
                         [DateTime]::UtcNow -lt $settingsDeadline)
                if (-not [WannaViewerUrlSmokeNative]::IsWindowVisible($settingsPanel) -or
                    -not [WannaViewerUrlSmokeNative]::IsWindowVisible((Get-Control $window 154))) {
                    throw 'Playback settings did not open in the embedded choice overlay'
                }
                foreach ($id in @(150,154,155,156)) {
                    Assert-NoBinaryRegion (Get-Control $window $id) "Playback settings control $id"
                }
                if ((Find-ProcessWindow $process.Id '#32768') -ne [IntPtr]::Zero) {
                    throw 'Playback settings created a native popup menu'
                }
                [void][WannaViewerUrlSmokeNative]::SendMessage($window, 0x0111, [IntPtr]156, [IntPtr]::Zero)
                if ([WannaViewerUrlSmokeNative]::IsWindowVisible($settingsPanel)) {
                    throw 'Embedded playback settings did not close'
                }
                Write-Host 'Playback settings stayed inside the player.'

                $controlsBar = Get-Control $window 113
                $barRectangle = [WannaViewerUrlSmokeNative+RECT]::new()
                if (-not [WannaViewerUrlSmokeNative]::GetWindowRect($controlsBar, [ref]$barRectangle)) {
                    throw 'Unable to read the playback controls rectangle'
                }
                [void][WannaViewerUrlSmokeNative]::SetCursorPos(
                    [int](($barRectangle.Left + $barRectangle.Right) / 2),
                    [int](($barRectangle.Top + $barRectangle.Bottom) / 2))
                Start-Sleep -Milliseconds 2200
                if (-not [WannaViewerUrlSmokeNative]::IsWindowVisible($controlsBar)) {
                    throw 'Playback controls hid while the cursor was inside the controls overlay'
                }

                $windowRectangle = [WannaViewerUrlSmokeNative+RECT]::new()
                if (-not [WannaViewerUrlSmokeNative]::GetWindowRect($window, [ref]$windowRectangle)) {
                    throw 'Unable to read the player window rectangle'
                }
                [void][WannaViewerUrlSmokeNative]::SetCursorPos(
                    [int](($windowRectangle.Left + $windowRectangle.Right) / 2),
                    $windowRectangle.Top + 80)
                # Ensure the inactivity interval starts after leaving the overlay even on
                # headless/remote desktops that occasionally coalesce cursor messages.
                [void][WannaViewerUrlSmokeNative]::SendMessage(
                    $window, 0x0200, [IntPtr]::Zero, [IntPtr]::Zero)
                $controlsHideDeadline = [DateTime]::UtcNow.AddSeconds(3)
                do {
                    Start-Sleep -Milliseconds 50
                } while ([WannaViewerUrlSmokeNative]::IsWindowVisible($controlsBar) -and
                         [DateTime]::UtcNow -lt $controlsHideDeadline)
                if ([WannaViewerUrlSmokeNative]::IsWindowVisible($controlsBar)) {
                    $cursorNow = [WannaViewerUrlSmokeNative+POINT]::new()
                    [void][WannaViewerUrlSmokeNative]::GetCursorPos([ref]$cursorNow)
                    throw "Playback controls did not hide promptly after the cursor left the overlay; cursor=$($cursorNow.X),$($cursorNow.Y) bar=$($barRectangle.Left),$($barRectangle.Top),$($barRectangle.Right),$($barRectangle.Bottom)"
                }

                # Hiding changes the child window under the cursor. Windows may emit a
                # synthetic WM_MOUSEMOVE for that transition even though the cursor did
                # not move; the controls must not reappear and start blinking.
                $stableHiddenDeadline = [DateTime]::UtcNow.AddSeconds(2)
                do {
                    Start-Sleep -Milliseconds 25
                    if ([WannaViewerUrlSmokeNative]::IsWindowVisible($controlsBar)) {
                        throw 'Playback controls reappeared without actual cursor movement'
                    }
                } while ([DateTime]::UtcNow -lt $stableHiddenDeadline)

                [void][WannaViewerUrlSmokeNative]::SetCursorPos(
                    [int](($windowRectangle.Left + $windowRectangle.Right) / 2) + 40,
                    $windowRectangle.Top + 80)
                [void][WannaViewerUrlSmokeNative]::SendMessage(
                    $window, 0x0200, [IntPtr]::Zero, [IntPtr]::Zero)
                $controlsShowDeadline = [DateTime]::UtcNow.AddSeconds(1)
                do {
                    Start-Sleep -Milliseconds 25
                } while (-not [WannaViewerUrlSmokeNative]::IsWindowVisible($controlsBar) -and
                         [DateTime]::UtcNow -lt $controlsShowDeadline)
                if (-not [WannaViewerUrlSmokeNative]::IsWindowVisible($controlsBar)) {
                    throw 'Playback controls did not reappear after actual cursor movement'
                }
                Write-Host 'Playback controls stay hidden without movement and reappear after real movement.'
            }
        }
        if ((Find-ProcessWindow $process.Id '#32768') -ne [IntPtr]::Zero) {
            throw "A native popup menu appeared while using the embedded selector for $url"
        }
        Write-Host "Embedded resolver selector appeared: $url"

        if (-not $selectorClosedByPlayback) {
            $WM_COMMAND = 0x0111
            [void][WannaViewerUrlSmokeNative]::SendMessage($window, $WM_COMMAND, [IntPtr]143, [IntPtr]::Zero)
            if ([WannaViewerUrlSmokeNative]::IsWindowVisible($panel)) { throw 'Embedded resolver selector did not close' }
        }
        [void][WannaViewerUrlSmokeNative]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        if (-not $process.WaitForExit(10000)) { throw 'Player did not close cleanly' }
        if ($process.ExitCode -ne 0) { throw "Player exited with code $($process.ExitCode)" }
    } finally {
        [void][WannaViewerUrlSmokeNative]::SetCursorPos($cursorBefore.X, $cursorBefore.Y)
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

Write-Host 'Embedded URL resolver selector smoke passed.'
