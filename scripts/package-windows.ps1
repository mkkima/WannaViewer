[CmdletBinding()]
param([ValidateSet('release','debug')][string]$Configuration = 'release')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot "build\windows-x64-$Configuration\bin"
$destination = Join-Path $projectRoot 'dist\windows-x64'
foreach ($required in @('player.exe','libmpv-2.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required))) { throw "Missing package input: $required" }
}
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($file in @('player.exe','libmpv-2.dll')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file) -Force
}
foreach ($asset in @('config','presets','resolvers','shaders')) {
    Copy-Item -LiteralPath (Join-Path $source $asset) -Destination (Join-Path $destination $asset) -Recurse -Force
}
$ytDlp = Join-Path $projectRoot 'tools\yt-dlp.exe'
if (Test-Path -LiteralPath $ytDlp) {
    New-Item -ItemType Directory -Force -Path (Join-Path $destination 'tools') | Out-Null
    Copy-Item -LiteralPath $ytDlp -Destination (Join-Path $destination 'tools\yt-dlp.exe') -Force
}
foreach ($directory in @('cache','logs')) { New-Item -ItemType Directory -Force -Path (Join-Path $destination $directory) | Out-Null }
foreach ($file in @('README.md','BUILDING.md','SECURITY.md','DEPENDENCIES.md','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $destination $file) -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_LICENSES') -Destination (Join-Path $destination 'THIRD_PARTY_LICENSES') -Recurse -Force
Write-Host "Portable package: $destination"
