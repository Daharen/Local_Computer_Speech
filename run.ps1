[CmdletBinding()]
param(
    [switch]$SetupBackend,
    [switch]$InstallModel,
    [switch]$Healthcheck,
    [switch]$BuildApp,
    [switch]$LaunchApp,
    [switch]$DetectQt,
    [switch]$BackendSynthSmoke,
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

if (-not ($SetupBackend -or $InstallModel -or $Healthcheck -or $BuildApp -or $LaunchApp -or $DetectQt -or $BackendSynthSmoke -or $AllSetup)) {
    Write-Host 'No mode selected. Use one or more of:'
    Write-Host '  -SetupBackend  -InstallModel  -Healthcheck  -BuildApp  -LaunchApp  -DetectQt  -BackendSynthSmoke  -AllSetup'
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


if ($BackendSynthSmoke) {
    $largeDataRoot = if ($env:LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT -and $env:LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT.Trim().Length -gt 0) {
        $env:LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT
    } else {
        'F:\My_Programs\Local_Computer_Speech_Large_Data'
    }

    $venvPython = Join-Path $largeDataRoot 'python_env\Scripts\python.exe'
    if (-not (Test-Path $venvPython)) {
        throw "Backend python env missing at $venvPython. Run .\run.ps1 -SetupBackend first."
    }

    $runtimeRoot = Join-Path $largeDataRoot 'runtime\requests'
    $outputRoot = Join-Path $largeDataRoot 'output'
    New-Item -ItemType Directory -Force -Path $runtimeRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $requestJson = Join-Path $runtimeRoot ("smoke_request_{0}.json" -f $stamp)
    $outputWav = Join-Path $outputRoot ("tts_smoke_{0}.wav" -f $stamp)

    $payload = @{
        text = 'Backend smoke test for Local Computer Speech.'
        output_path = $outputWav
        language = 'English'
        speaker = 'Ryan'
        instruct = ''
    }
    $payload | ConvertTo-Json | Set-Content -Path $requestJson -Encoding UTF8

    $env:PYTHONPATH = Join-Path $PSScriptRoot 'backend'
    & $venvPython -m local_computer_speech_backend.cli synth --request-json $requestJson
    exit $LASTEXITCODE
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
