[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [string]$FixtureRoot = "$PSScriptRoot\..\.deps\cache",
    [int]$Port = 18765,
    [switch]$ExistingServer
)

$ErrorActionPreference = 'Stop'
$server = $null
if (-not $ExistingServer) {
    $serverStart = [System.Diagnostics.ProcessStartInfo]::new((Get-Command python).Source)
    $serverStart.UseShellExecute = $false
    $serverStart.CreateNoWindow = $true
    foreach ($argument in @('-m','http.server',[string]$Port,'--bind','127.0.0.1','--directory',(Resolve-Path -LiteralPath $FixtureRoot).Path)) {
        $serverStart.ArgumentList.Add($argument)
    }
    $server = [System.Diagnostics.Process]::Start($serverStart)
}
try {
    Start-Sleep -Seconds 1
    if ($server -and $server.HasExited) { throw 'Local HTTP fixture server could not start' }
    Invoke-WebRequest -Uri "http://127.0.0.1:$Port/hls/master.m3u8" -TimeoutSec 5 | Out-Null
    $results = @()
    foreach ($path in @('hls/master.m3u8','dash/manifest.mpd')) {
        $url = "http://127.0.0.1:$Port/$path"
        $start = [System.Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
        $start.UseShellExecute = $false
        $start.ArgumentList.Add('--benchmark')
        $start.ArgumentList.Add($url)
        $process = [System.Diagnostics.Process]::Start($start)
        if (-not $process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Network benchmark timed out: $url"
        }
        $reportPath = Join-Path (Split-Path -Parent $Player) 'benchmark.json'
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        $results += [pscustomobject]@{ URL=$url; ExitCode=$process.ExitCode; Codec=$report.codec; FPS=$report.fps; Decoder=$report.decoder; Dropped=$report.dropped_frames; Delayed=$report.delayed_frames }
    }
    $results | Format-Table -AutoSize
    if ($results.ExitCode -contains 1) { throw 'A network smoke test failed' }
} finally {
    if ($server -and -not $server.HasExited) { $server.Kill(); $server.WaitForExit() }
}
