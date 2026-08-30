[CmdletBinding()]
param([ValidateSet('release','debug')][string]$Configuration = 'release')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot "build\windows-x64-$Configuration\bin"
$destination = Join-Path $projectRoot 'dist\windows-x64'
foreach ($required in @('player.exe','libmpv-2.dll','WebView2Loader.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required))) { throw "Missing package input: $required" }
}
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($file in @('player.exe','libmpv-2.dll','WebView2Loader.dll')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file) -Force
}
$qtRoot = Join-Path $projectRoot '.deps\qt\6.8.3\msvc2022_64'
$deployQt = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployQt)) { throw "Pinned Qt deployment tool is missing: $deployQt" }
$qtDeployMode = if ($Configuration -eq 'debug') { '--debug' } else { '--release' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Visual Studio locator is missing: $vswhere" }
$visualStudioRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioRoot) { throw 'A Visual Studio C++ toolchain installation was not found' }
$previousVcInstallDirectory = $env:VCINSTALLDIR
try {
    $env:VCINSTALLDIR = Join-Path $visualStudioRoot 'VC'
    & $deployQt $qtDeployMode --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime `
        --skip-plugin-types generic,imageformats,networkinformation,tls `
        --dir $destination (Join-Path $destination 'player.exe')
} finally {
    $env:VCINSTALLDIR = $previousVcInstallDirectory
}
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed' }
$redistRoot = Join-Path $visualStudioRoot 'VC\Redist\MSVC'
$redistVersion = Get-ChildItem -LiteralPath $redistRoot -Directory -ErrorAction Stop |
    Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if (-not $redistVersion) { throw "MSVC redistributable files are missing under $redistRoot" }
$crtDirectory = Get-ChildItem -LiteralPath (Join-Path $redistVersion.FullName 'x64') -Directory -ErrorAction Stop |
    Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } |
    Select-Object -First 1
if (-not $crtDirectory) { throw "The x64 MSVC CRT directory is missing under $($redistVersion.FullName)" }
Get-ChildItem -LiteralPath $crtDirectory.FullName -File -Filter '*.dll' |
    Copy-Item -Destination $destination -Force
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
