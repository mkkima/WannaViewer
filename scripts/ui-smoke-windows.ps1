[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [Parameter(Mandatory=$true)][string]$Media,
    [switch]$SkipInteractions,
    [ValidateSet('None', 'Continue', 'Restart')][string]$ResumeChoice = 'None',
    [double]$MinimumInitialPositionSeconds = 0,
    [switch]$WaitForResumeEntryRemoval,
    [switch]$VerifySettingPersistence,
    [switch]$VerifyExtendedSettings,
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

$configPath = Join-Path (Split-Path -Parent (Resolve-Path -LiteralPath $Player).Path) 'config\player.conf'
$configExisted = Test-Path -LiteralPath $configPath
$originalConfigBytes = if ($configExisted) { [IO.File]::ReadAllBytes($configPath) } else { $null }
$settingsPersistenceVerified = $false
$extendedSettingsVerified = $false
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
        if ((Get-UiValue $window 5) -eq 1 -or (Get-UiValue $window 22) -eq 1) { break }
        Start-Sleep -Milliseconds 50
        $process.Refresh()
    } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $playbackDeadline)
    if ($process.HasExited) { throw "Qt player exited before playback started with code $($process.ExitCode)" }
    $resumePromptVisible = (Get-UiValue $window 22) -eq 1
    if ($resumePromptVisible) {
        if ($ResumeChoice -eq 'None') { throw 'A resume prompt appeared without an expected test choice' }
        Invoke-UiAction $window $(if ($ResumeChoice -eq 'Continue') { 20 } else { 21 })
        if ($ResumeChoice -eq 'Restart' -and (Get-UiValue $window 21) -ne 0) {
            throw 'Starting from the beginning retained the old resume entry'
        }
        $playbackDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            if ((Get-UiValue $window 5) -eq 1) { break }
            Start-Sleep -Milliseconds 40
            $process.Refresh()
        } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $playbackDeadline)
    } elseif ($ResumeChoice -ne 'None') {
        throw "Expected a resume prompt for choice '$ResumeChoice', but none appeared"
    }
    if ((Get-UiValue $window 5) -ne 1) {
        $title = [Text.StringBuilder]::new(256)
        [void][WannaViewerQtPlaybackSmokeNative]::GetWindowText($window, $title, $title.Capacity)
        throw "Playback did not produce a frame; last window title: $title"
    }
    if ((Get-UiValue $window 4) -ne 1) { throw 'Playback started without the loaded-media UI state' }
    if ($MinimumInitialPositionSeconds -gt 0) {
        $positionDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $positionCentiseconds = Get-UiValue $window 8
            if ($positionCentiseconds -ge [Math]::Round($MinimumInitialPositionSeconds * 100)) { break }
            Start-Sleep -Milliseconds 40
            $process.Refresh()
        } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $positionDeadline)
        if ($positionCentiseconds -lt [Math]::Round($MinimumInitialPositionSeconds * 100)) {
            throw "Playback did not resume at or beyond $MinimumInitialPositionSeconds seconds; position was $($positionCentiseconds / 100.0)"
        }
    }
    if ($WaitForResumeEntryRemoval) {
        if ((Get-UiValue $window 21) -ne 1) { throw 'No saved resume entry was loaded for completion testing' }
        $completionDeadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            Start-Sleep -Milliseconds 100
            $process.Refresh()
        } while (-not $process.HasExited -and (Get-UiValue $window 21) -eq 1 -and
                 [DateTime]::UtcNow -lt $completionDeadline)
        if ((Get-UiValue $window 21) -ne 0) {
            throw 'Completed playback retained its saved resume entry'
        }
    }

    if (-not $SkipInteractions) {
        Invoke-UiAction $window 3
        if ((Get-UiValue $window 6) -ne 1) { throw 'Playback controls did not appear' }
        if ((Get-UiValue $window 11) -ne 1000) { throw 'Playback controls appeared with incomplete opacity' }
        Invoke-UiAction $window 4
        if ((Get-UiValue $window 6) -ne 0) { throw 'Playback controls did not hide cleanly' }
        if ((Get-UiValue $window 11) -ne 0) { throw 'Hidden playback controls retained opacity' }
        Invoke-UiAction $window 3
        if ((Get-UiValue $window 6) -ne 1) { throw 'Playback controls did not reappear cleanly' }
        if ((Get-UiValue $window 11) -ne 1000) { throw 'Playback controls reappeared with incomplete opacity' }

        Invoke-UiAction $window 32
        $controlsHideDeadline = [DateTime]::UtcNow.AddSeconds(4)
        do {
            Start-Sleep -Milliseconds 40
            $process.Refresh()
        } while (-not $process.HasExited -and
                 ((Get-UiValue $window 6) -ne 0 -or (Get-UiValue $window 11) -ne 0) -and
                 [DateTime]::UtcNow -lt $controlsHideDeadline)
        if ($process.HasExited) { throw "Qt player exited while waiting for controls to auto-hide with code $($process.ExitCode)" }
        if ((Get-UiValue $window 6) -ne 0 -or (Get-UiValue $window 11) -ne 0) {
            throw 'Playback controls did not auto-hide after pointer inactivity'
        }
        Invoke-UiAction $window 3

        Invoke-UiAction $window 6
        if ((Get-UiValue $window 7) -lt 4900 -or (Get-UiValue $window 7) -gt 5100) {
            throw "Timeline did not retain the exact selected position: $(Get-UiValue $window 7)"
        }
        if ((Get-UiValue $window 16) -ne 1 -or (Get-UiValue $window 17) -ne 1000) {
            throw 'Timeline seek did not show settled playback feedback'
        }
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
        Start-Sleep -Milliseconds 500
        if ((Get-UiValue $window 8) -le 0) { throw 'Timeline seek did not advance playback position' }

        Invoke-UiAction $window 17
        if (((Get-UiValue $window 19) -band 8) -eq 0) {
            throw 'Statistics button did not expose its active state'
        }
        Invoke-UiAction $window 17
        if (((Get-UiValue $window 19) -band 8) -ne 0) {
            throw 'Statistics button retained its active state after being disabled'
        }

        Invoke-UiAction $window 18
        $stateDeadline = [DateTime]::UtcNow.AddSeconds(2)
        do { Start-Sleep -Milliseconds 20 }
        while (((Get-UiValue $window 19) -band 1) -eq 0 -and [DateTime]::UtcNow -lt $stateDeadline)
        if (((Get-UiValue $window 19) -band 1) -eq 0) {
            throw 'Mute button did not expose its active state'
        }
        Invoke-UiAction $window 18
        $stateDeadline = [DateTime]::UtcNow.AddSeconds(2)
        do { Start-Sleep -Milliseconds 20 }
        while (((Get-UiValue $window 19) -band 1) -ne 0 -and [DateTime]::UtcNow -lt $stateDeadline)
        if (((Get-UiValue $window 19) -band 1) -ne 0) {
            throw 'Mute button retained its active state after sound was restored'
        }

        if ($VerifySettingPersistence) {
            Invoke-UiAction $window 19
            if ((Get-UiValue $window 20) -ne 37) {
                throw 'Volume control did not accept the persistence test value'
            }
        }

        if ($VerifyExtendedSettings) {
            Invoke-UiAction $window 22
            if ((Get-UiValue $window 23) -ne 1) { throw 'Settings overlay did not open' }
            $settingsListState = Get-UiValue $window 24
            if ($settingsListState -ne ((12 -shl 8) -bor 5)) {
                throw "Settings overlay state was $settingsListState instead of twelve choices with five active values"
            }
            Invoke-UiAction $window 23
            if ((Get-UiValue $window 25) -ne 0) { throw 'Resume setting did not turn off' }
            Invoke-UiAction $window 24
            if ((Get-UiValue $window 25) -ne 1) { throw 'Resume setting did not turn back on' }
            Invoke-UiAction $window 25
            if ((Get-UiValue $window 26) -ne 0) { throw 'Animation setting did not turn off' }
            Invoke-UiAction $window 26
            if ((Get-UiValue $window 26) -ne 1) { throw 'Animation setting did not turn back on' }
            Invoke-UiAction $window 38
            if ((Get-UiValue $window 30) -ne 8) {
                throw 'Shader menu did not expose Off, six Anime4K processing modes, and Custom GLSL'
            }
            Invoke-UiAction $window 2
            $shaderActions = @(28, 33, 34, 35, 36, 37)
            for ($shaderIndex = 1; $shaderIndex -le 6; ++$shaderIndex) {
                Invoke-UiAction $window $shaderActions[$shaderIndex - 1]
                Start-Sleep -Milliseconds 250
                $process.Refresh()
                if ($process.HasExited) { throw "Qt player crashed while applying Anime4K mode index $shaderIndex" }
                if ((Get-UiValue $window 27) -ne $shaderIndex) {
                    throw "Anime4K mode at index $shaderIndex did not become active"
                }
            }
            Invoke-UiAction $window 28
            Invoke-UiAction $window 27
            if ((Get-UiValue $window 21) -ne 0) { throw 'Clear history retained the current resume entry' }
        }

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
    if ($VerifySettingPersistence -and (Test-Path -LiteralPath $configPath)) {
        $settingsPersistenceVerified = Select-String -LiteralPath $configPath -Pattern '^playback\.volume=37$' -Quiet
    }
    if ($VerifyExtendedSettings -and (Test-Path -LiteralPath $configPath)) {
        $resumeSaved = Select-String -LiteralPath $configPath -Pattern '^playback\.resume=true$' -Quiet
        $animationsSaved = Select-String -LiteralPath $configPath -Pattern '^ui\.animations=true$' -Quiet
        $shaderSaved = Select-String -LiteralPath $configPath -Pattern '^shader\.preset=anime4k-a$' -Quiet
        $extendedSettingsVerified = $resumeSaved -and $animationsSaved -and $shaderSaved
    }
    if ($configExisted) {
        [IO.File]::WriteAllBytes($configPath, $originalConfigBytes)
    } elseif (Test-Path -LiteralPath $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
}

if ($VerifySettingPersistence -and -not $settingsPersistenceVerified) {
    throw 'Volume setting was not persisted to config/player.conf'
}
if ($VerifyExtendedSettings -and -not $extendedSettingsVerified) {
    throw 'Extended playback settings were not persisted to config/player.conf'
}

if ($SkipInteractions) {
    Write-Host 'Background Qt playback smoke passed: first frame and clean close.'
} else {
    Write-Host 'Background Qt playback smoke passed: first frame, controls, feedback, active states, timeline, hotkeys, clean close.'
}
