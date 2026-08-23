[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$env:WANNAVIEWER_LIVE_RESOLVER_TESTS = '1'
$projectRoot = Split-Path -Parent $PSScriptRoot
ctest --test-dir (Join-Path $projectRoot 'build\windows-x64-release') --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Live resolver tests failed' }
