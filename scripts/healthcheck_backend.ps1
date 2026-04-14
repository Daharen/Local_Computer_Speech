[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$repoRoot = Split-Path $PSScriptRoot -Parent
$largeDataRoot = Get-LargeDataRoot

$venvPython = Join-Path $largeDataRoot 'python_env\Scripts\python.exe'
if (-not (Test-Path $venvPython)) {
    throw "Backend python env missing at $venvPython. Run .\run.ps1 -SetupBackend first."
}

$env:PYTHONPATH = Join-Path $repoRoot 'backend'
& $venvPython -m local_computer_speech_backend.cli healthcheck
exit $LASTEXITCODE
