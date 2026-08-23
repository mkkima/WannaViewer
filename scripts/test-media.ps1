[CmdletBinding()]
param(
    [string]$Player = "$PSScriptRoot\..\dist\windows-x64\player.exe",
    [string]$MediaDirectory = "$PSScriptRoot\..\tests\media",
    [ValidateSet('software','hardware','hardware-shader')][string]$Mode = 'hardware'
)

$ErrorActionPreference = 'Stop'
$expected = @(
    '1080p-h264-8bit.*', '1080p-hevc-10bit.*', '4k-hevc-10bit.*', '4k60-hevc-10bit.*',
    '4k-av1-10bit.*', '4k60-av1-10bit.*', 'hdr10-hevc.*', 'hdr10-av1.*',
    'ass-subtitles.*', 'high-bitrate-mkv.*', 'hls.url', 'dash.url'
)
if (-not (Test-Path -LiteralPath $Player)) { throw "Player not found: $Player" }
New-Item -ItemType Directory -Force -Path $MediaDirectory | Out-Null
$missing = @()
foreach ($pattern in $expected) {
    if (-not (Get-ChildItem -LiteralPath $MediaDirectory -Filter $pattern -File -ErrorAction SilentlyContinue)) { $missing += $pattern }
}
if ($missing.Count) {
    Write-Host 'Missing user-provided/open test samples:'
    $missing | ForEach-Object { Write-Host "  $_" }
}
$results = @()
foreach ($sample in Get-ChildItem -LiteralPath $MediaDirectory -File) {
    $input = if ($sample.Extension -eq '.url') { (Get-Content -LiteralPath $sample.FullName -Raw).Trim() } else { $sample.FullName }
    $process = Start-Process -FilePath $Player -ArgumentList @('--benchmark',"--benchmark-mode=$Mode",$input) -PassThru -Wait
    $report = Join-Path (Split-Path -Parent $Player) 'benchmark.json'
    $results += [pscustomobject]@{ Sample=$sample.Name; ExitCode=$process.ExitCode; Report=if(Test-Path $report){Get-Content $report -Raw | ConvertFrom-Json}else{$null} }
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $MediaDirectory 'results.json') -Encoding utf8
if ($results.ExitCode -contains 1) { throw 'At least one media benchmark failed' }
