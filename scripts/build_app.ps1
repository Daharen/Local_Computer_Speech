[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$Qt6Dir,
    [switch]$DetectOnly
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'qt_detection.ps1')

function Resolve-AppExecutable {
    param(
        [string]$BuildDir
    )

    $candidates = @(
        (Join-Path $BuildDir 'Release\LocalComputerSpeech.exe'),
        (Join-Path $BuildDir 'LocalComputerSpeech.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $repoRoot 'build'

$qt = Resolve-QtFromInputs -InputQtRoot $QtRoot -InputQt6Dir $Qt6Dir

Write-Host "Qt detection source : $($qt.Source)"
if ($qt.QtRoot) {
    Write-Host "Qt root            : $($qt.QtRoot)"
}
Write-Host "Qt6_DIR            : $($qt.Qt6Dir)"
Write-Host "CMake argument      : $($qt.CMakeArg)"

if ($DetectOnly) {
    Write-Host 'Detection-only mode complete. No configure/build was run.'
    exit 0
}

Write-Host "Configuring CMake in '$buildDir'..."
& cmake -S $repoRoot -B $buildDir $qt.CMakeArg
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configure failed.'
}

Write-Host 'Building Release configuration...'
& cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw 'CMake build failed.'
}

$exe = Resolve-AppExecutable -BuildDir $buildDir
if (-not $exe) {
    throw "Build completed, but LocalComputerSpeech.exe was not found in '$buildDir\\Release' or '$buildDir'."
}

Write-Host "App build complete: $exe"
