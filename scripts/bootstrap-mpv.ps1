[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$cacheRoot = Join-Path $dependencyRoot 'cache'
$target = Join-Path $dependencyRoot 'mpv'
$archiveName = 'mpv-dev-x86_64-20260814-git-7b8915bc1d.7z'
$archive = Join-Path $cacheRoot $archiveName
$uri = 'https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20260814/mpv-dev-x86_64-20260814-git-7b8915bc1d.7z'
$expectedSha256 = '0AF22B28E920620036D3AE08FD9283156DC9AF0420BF4DF84B0E02282094599C'

New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
if ($Force -or -not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $uri -OutFile $archive
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ne $expectedSha256) {
    throw "libmpv archive integrity check failed. Expected $expectedSha256, got $actual"
}
if ((Test-Path -LiteralPath (Join-Path $target 'libmpv-2.dll')) -and -not $Force) {
    return
}
$temporary = Join-Path $dependencyRoot ("mpv.extract." + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    Push-Location $temporary
    try { & cmake -E tar xf $archive --format=7zip } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $temporary 'libmpv-2.dll'))) {
        throw 'Unable to extract the libmpv archive'
    }
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Move-Item -LiteralPath $temporary -Destination $target
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
