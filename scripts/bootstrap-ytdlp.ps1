[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$targetRoot = Join-Path $projectRoot 'tools'
$target = Join-Path $targetRoot 'yt-dlp.exe'
$uri = 'https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp.exe'
$expectedSha256 = '66674953FE251B89F4D08C5F0E35E0728679BD67AB3D7D05C0562AF101DD3E7A'

New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null
if ($Force -or -not (Test-Path -LiteralPath $target)) { Invoke-WebRequest -Uri $uri -OutFile $target }
$actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($actual -ne $expectedSha256) {
    Remove-Item -LiteralPath $target -Force
    throw "yt-dlp integrity check failed. Expected $expectedSha256, got $actual"
}
