[CmdletBinding()]
param(
    [switch]$SetupBackend,
    [switch]$InstallModel,
    [switch]$Healthcheck,
    [switch]$LaunchApp,
    [switch]$AllSetup
)

$ErrorActionPreference = 'Stop'

if (-not ($SetupBackend -or $InstallModel -or $Healthcheck -or $LaunchApp -or $AllSetup)) {
    Write-Host 'No mode selected. Use one or more of:'
    Write-Host '  -SetupBackend  -InstallModel  -Healthcheck  -LaunchApp  -AllSetup'
    Write-Host 'Example full setup:'
    Write-Host '  .\run.ps1 -AllSetup'
    exit 0
}

if ($AllSetup) {
    & "$PSScriptRoot\scripts\setup_backend_env.ps1"
    & "$PSScriptRoot\scripts\install_qwen3_tts_model.ps1"
    & "$PSScriptRoot\scripts\healthcheck_backend.ps1"
}

if ($SetupBackend) {
    & "$PSScriptRoot\scripts\setup_backend_env.ps1"
}

if ($InstallModel) {
    & "$PSScriptRoot\scripts\install_qwen3_tts_model.ps1"
}

if ($Healthcheck) {
    & "$PSScriptRoot\scripts\healthcheck_backend.ps1"
}

if ($LaunchApp) {
    Write-Host 'Launching Qt app skeleton (build must already exist).'
    $exe = Join-Path $PSScriptRoot 'build\LocalComputerSpeech.exe'
    if (-not (Test-Path $exe)) {
        throw "Executable not found at '$exe'. Configure+build first (CMake + Qt6)."
    }
    & $exe
}
