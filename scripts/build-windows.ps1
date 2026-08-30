[CmdletBinding()]
param([switch]$Release, [switch]$DebugBuild, [switch]$SkipYtDlp)

$ErrorActionPreference = 'Stop'
if ($Release -and $DebugBuild) { throw 'Choose either -Release or -DebugBuild' }
$configuration = if ($DebugBuild) { 'debug' } else { 'release' }
$preset = "windows-x64-$configuration"

& (Join-Path $PSScriptRoot 'bootstrap-mpv.ps1')
& (Join-Path $PSScriptRoot 'bootstrap-anime4k.ps1')
& (Join-Path $PSScriptRoot 'bootstrap-qt.ps1')
if (-not $SkipYtDlp) { & (Join-Path $PSScriptRoot 'bootstrap-ytdlp.ps1') }

& cmake --preset $preset
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ctest --preset $preset
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
& (Join-Path $PSScriptRoot 'package-windows.ps1') -Configuration $configuration
