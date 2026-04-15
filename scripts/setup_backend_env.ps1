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

& $venvPython -m pip install --upgrade pip wheel
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install/upgrade pip tooling in backend venv (exit $LASTEXITCODE)."
}

& $venvPython -m pip install 'setuptools<82'
if ($LASTEXITCODE -ne 0) {
    throw "Failed to pin setuptools<82 for torch compatibility (exit $LASTEXITCODE)."
}

& $venvPython -m pip install -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install backend requirements (exit $LASTEXITCODE)."
}

Write-Host 'Backend environment setup complete.'
Write-Host "Python: $venvPython"
