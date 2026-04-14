[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$repoRoot = Split-Path $PSScriptRoot -Parent
$largeDataRoot = Get-LargeDataRoot
Ensure-RootSubdirs -LargeDataRoot $largeDataRoot

$bootstrapPython = Get-PythonExe -LargeDataRoot $largeDataRoot
$venvRoot = Join-Path $largeDataRoot 'python_env'

if (-not (Test-Path (Join-Path $venvRoot 'Scripts\python.exe'))) {
    Write-Host "Creating Python venv at $venvRoot"
    & $bootstrapPython -m venv $venvRoot
}

$venvPython = Join-Path $venvRoot 'Scripts\python.exe'
$requirements = Join-Path $repoRoot 'backend\requirements.txt'

& $venvPython -m pip install --upgrade pip wheel setuptools
& $venvPython -m pip install -r $requirements

Write-Host 'Backend environment setup complete.'
Write-Host "Python: $venvPython"
