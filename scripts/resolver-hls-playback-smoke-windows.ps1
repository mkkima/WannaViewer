[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceEndpoint,
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe"
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
$endpoint = [Uri]$SourceEndpoint
if ($endpoint.Scheme -ne 'https' -or $endpoint.Host -ne 'plapi.cdnvideohub.com') {
    throw 'SourceEndpoint must be an HTTPS CVH source API URL'
}
$response = Invoke-RestMethod -Method Get -Uri $endpoint -Headers @{Accept='application/json'} -TimeoutSec 20
$hls = [string]$response.sources.hlsUrl
if (-not $hls.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CVH did not return an HTTPS HLS URL'
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Player).Path)
$start.UseShellExecute = $false
$start.ArgumentList.Add($hls)
$process = [Diagnostics.Process]::Start($start)
try {
    [void]$process.WaitForInputIdle(10000)
    # The title changes to "playing" only after libmpv reports time-pos > 0;
    # FileLoaded/buffering alone is intentionally insufficient.
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    } while (-not $process.HasExited -and $process.MainWindowTitle -notlike '*playing*' -and
             [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited) { throw "Player exited early with code $($process.ExitCode)" }
    if ($process.MainWindowTitle -notlike '*playing*') { throw 'Player did not decode video and advance playback time for CVH HLS' }
    Write-Host 'Packaged mpv decoded CVH video and advanced playback time for a fresh HLS manifest.'
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(5000)) { throw 'Player did not close cleanly' }
    if ($process.ExitCode -ne 0) { throw "Player exited with code $($process.ExitCode)" }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
