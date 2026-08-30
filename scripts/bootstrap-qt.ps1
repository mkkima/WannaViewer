[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = '6.8.3'
$toolchain = 'msvc2022_64'
$archiveName = '6.8.3-0-202503201308qtbase-Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-X86_64.7z'
$archiveHash = '41688269fac0565db956c66d9eecae777d16197e0c02cd81b88640c1f5d73d3f'
$archiveUrl = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/$archiveName"
$cacheDirectory = Join-Path $projectRoot '.deps-cache'
$archive = Join-Path $cacheDirectory $archiveName
$qtRoot = Join-Path $projectRoot ".deps\qt\$version\$toolchain"
$qtConfig = Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'

if (Test-Path -LiteralPath $qtConfig) {
    Write-Host "Qt $version is already available at $qtRoot"
    return
}

New-Item -ItemType Directory -Force -Path $cacheDirectory | Out-Null
if (-not (Test-Path -LiteralPath $archive) -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $archiveHash) {
    Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $archiveHash) {
    throw "Qt archive digest mismatch: $archive"
}

New-Item -ItemType Directory -Force -Path $qtRoot | Out-Null
& 7z x $archive "-o$qtRoot" -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Unable to extract the pinned Qt SDK' }
if (-not (Test-Path -LiteralPath $qtConfig)) {
    throw "Qt SDK layout is incomplete after extraction: $qtConfig"
}

$qtConf = Join-Path $qtRoot 'bin\qt.conf'
if (-not (Test-Path -LiteralPath $qtConf)) {
    Set-Content -LiteralPath $qtConf -Encoding ascii -Value "[Paths]`nPrefix=..`n"
}
Write-Host "Qt $version SDK: $qtRoot"
