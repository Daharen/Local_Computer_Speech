[CmdletBinding()]
param(
    [switch]$SetupBackend,
    [switch]$InstallModel,
    [switch]$Healthcheck,
    [switch]$BuildApp,
    [switch]$LaunchApp,
    [switch]$DetectQt,
    [switch]$AllSetup,
    [string]$QtRoot,
    [string]$Qt6Dir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scripts\qt_detection.ps1')

function Resolve-AppExecutable {
    param(
        [string]$RepoRoot
    )

    $buildDir = Join-Path $RepoRoot 'build'
    $candidates = @(
        (Join-Path $buildDir 'Release\LocalComputerSpeech.exe'),
        (Join-Path $buildDir 'LocalComputerSpeech.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

if (-not ($SetupBackend -or $InstallModel -or $Healthcheck -or $BuildApp -or $LaunchApp -or $DetectQt -or $AllSetup)) {
    Write-Host 'No mode selected. Use one or more of:'
    Write-Host '  -SetupBackend  -InstallModel  -Healthcheck  -BuildApp  -LaunchApp  -DetectQt  -AllSetup'
    Write-Host 'Example full setup:'
    Write-Host '  .\run.ps1 -AllSetup'
    Write-Host 'Build app explicitly:'
    Write-Host '  .\run.ps1 -BuildApp -QtRoot "C:\Qt\6.8.0\msvc2022_64"'
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

if ($DetectQt) {
    & "$PSScriptRoot\scripts\build_app.ps1" -QtRoot $QtRoot -Qt6Dir $Qt6Dir -DetectOnly
}

if ($BuildApp) {
    & "$PSScriptRoot\scripts\build_app.ps1" -QtRoot $QtRoot -Qt6Dir $Qt6Dir
}

if ($LaunchApp) {
    Write-Host 'Launching Qt app (build must already exist).'
    $exe = Resolve-AppExecutable -RepoRoot $PSScriptRoot
    if (-not $exe) {
        throw "Executable not found. Expected one of: '$PSScriptRoot\build\Release\LocalComputerSpeech.exe' or '$PSScriptRoot\build\LocalComputerSpeech.exe'. Build first with '.\run.ps1 -BuildApp'."
    }

    $qt = Resolve-QtFromInputs -InputQtRoot $QtRoot -InputQt6Dir $Qt6Dir
    if (-not $qt.QtRoot) {
        throw "Detected Qt6_DIR '$($qt.Qt6Dir)' but could not derive a Qt root/bin path. Provide -QtRoot explicitly (example: C:\Qt\6.8.0\msvc2022_64)."
    }
    $qtBin = Join-Path $qt.QtRoot 'bin'
    if (-not (Test-Path $qtBin)) {
        throw "Qt bin directory was not found at '$qtBin'."
    }

    Write-Host "Qt detection source : $($qt.Source)"
    Write-Host "Qt runtime bin      : $qtBin"
    $env:PATH = "$qtBin;$env:PATH"

    Write-Host "Starting: $exe"
    & $exe
}
