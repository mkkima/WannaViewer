[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cacheRoot = Join-Path $projectRoot '.deps\cache'
$target = Join-Path $projectRoot 'shaders\Anime4K'
$archive = Join-Path $cacheRoot 'Anime4K_v4.0.zip'
$uri = 'https://github.com/bloc97/Anime4K/releases/download/v4.0.1/Anime4K_v4.0.zip'
$expectedSha256 = '139CD282086457C5ADC79CAF7B75B8B825091D71C9B54958C18745FEA62D7ED7'

New-Item -ItemType Directory -Force -Path $cacheRoot,$target | Out-Null
if ($Force -or -not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $uri -OutFile $archive }
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ne $expectedSha256) { throw "Anime4K archive integrity check failed. Expected $expectedSha256, got $actual" }
if ((Test-Path -LiteralPath (Join-Path $target 'Anime4K_Clamp_Highlights.glsl')) -and -not $Force) { return }
$temporary = Join-Path $cacheRoot ("anime4k.extract." + [Guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $archive -DestinationPath $temporary -Force
    Get-ChildItem -LiteralPath $temporary -Filter '*.glsl' -File | Copy-Item -Destination $target -Force
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
