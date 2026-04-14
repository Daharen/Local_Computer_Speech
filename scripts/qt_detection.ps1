$ErrorActionPreference = 'Stop'

function Split-PathList {
    param(
        [string]$Value
    )

    if (-not $Value) {
        return @()
    }

    return $Value -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
}

function Resolve-ExistingPath {
    param(
        [string]$Path
    )

    if (-not $Path) {
        return $null
    }

    try {
        return (Resolve-Path -Path $Path -ErrorAction Stop).Path
    } catch {
        return $null
    }
}

function Resolve-QtRootFromQt6Dir {
    param(
        [string]$CandidateQt6Dir
    )

    if (-not $CandidateQt6Dir) {
        return $null
    }

    $resolvedQt6Dir = Resolve-ExistingPath -Path $CandidateQt6Dir
    if (-not $resolvedQt6Dir) {
        return $null
    }

    $qt6Config = Join-Path $resolvedQt6Dir 'Qt6Config.cmake'
    if (-not (Test-Path $qt6Config)) {
        return $null
    }

    $qtRoot = Split-Path -Path (Split-Path -Path (Split-Path -Path $resolvedQt6Dir -Parent) -Parent) -Parent
    if (-not $qtRoot) {
        return $null
    }

    $resolvedQtRoot = Resolve-ExistingPath -Path $qtRoot
    return $resolvedQtRoot
}

function Resolve-QtFromInputs {
    param(
        [string]$InputQtRoot,
        [string]$InputQt6Dir
    )

    $explicitRoot = $null
    if ($InputQtRoot) {
        $explicitRoot = Resolve-ExistingPath -Path $InputQtRoot
        if (-not $explicitRoot) {
            throw "QtRoot path not found: '$InputQtRoot'"
        }

        $expectedQt6Dir = Join-Path $explicitRoot 'lib\cmake\Qt6'
        if (-not (Test-Path (Join-Path $expectedQt6Dir 'Qt6Config.cmake'))) {
            throw "QtRoot '$explicitRoot' is invalid. Expected Qt6Config.cmake under '$expectedQt6Dir'."
        }

        if ($explicitRoot -notmatch 'msvc2022_64') {
            Write-Warning "QtRoot '$explicitRoot' does not look like an MSVC 2022 x64 desktop kit path (expected to include 'msvc2022_64')."
        }

        return [ordered]@{
            Source = 'parameter:QtRoot'
            QtRoot = $explicitRoot
            Qt6Dir = $expectedQt6Dir
            CMakeArg = "-DCMAKE_PREFIX_PATH=$explicitRoot"
        }
    }

    $explicitQt6Dir = $null
    if ($InputQt6Dir) {
        $explicitQt6Dir = Resolve-ExistingPath -Path $InputQt6Dir
        if (-not $explicitQt6Dir) {
            throw "Qt6Dir path not found: '$InputQt6Dir'"
        }

        if (-not (Test-Path (Join-Path $explicitQt6Dir 'Qt6Config.cmake'))) {
            throw "Qt6Dir '$explicitQt6Dir' is invalid. Expected Qt6Config.cmake in this directory."
        }

        $derivedRoot = Resolve-QtRootFromQt6Dir -CandidateQt6Dir $explicitQt6Dir
        if ($derivedRoot -and $derivedRoot -notmatch 'msvc2022_64') {
            Write-Warning "Qt6Dir '$explicitQt6Dir' resolved to root '$derivedRoot', which does not look like an MSVC 2022 x64 desktop kit path."
        }

        return [ordered]@{
            Source = 'parameter:Qt6Dir'
            QtRoot = $derivedRoot
            Qt6Dir = $explicitQt6Dir
            CMakeArg = "-DQt6_DIR=$explicitQt6Dir"
        }
    }

    $candidates = @()

    if ($env:Qt6_DIR) {
        $candidates += [ordered]@{ Kind = 'Qt6Dir'; Source = 'env:Qt6_DIR'; Value = $env:Qt6_DIR }
    }

    foreach ($entry in Split-PathList -Value $env:CMAKE_PREFIX_PATH) {
        if ($entry -match 'lib\\cmake\\Qt6$') {
            $candidates += [ordered]@{ Kind = 'Qt6Dir'; Source = 'env:CMAKE_PREFIX_PATH'; Value = $entry }
        } else {
            $candidates += [ordered]@{ Kind = 'QtRoot'; Source = 'env:CMAKE_PREFIX_PATH'; Value = $entry }
        }
    }

    $commonRoots = Get-ChildItem -Path 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'msvc2022_64' }
    foreach ($commonRoot in $commonRoots) {
        $candidates += [ordered]@{ Kind = 'QtRoot'; Source = 'auto:C:\Qt\*\msvc2022_64'; Value = $commonRoot }
    }

    foreach ($candidate in $candidates) {
        if ($candidate.Kind -eq 'Qt6Dir') {
            $resolved = Resolve-ExistingPath -Path $candidate.Value
            if (-not $resolved) { continue }
            if (-not (Test-Path (Join-Path $resolved 'Qt6Config.cmake'))) { continue }
            $root = Resolve-QtRootFromQt6Dir -CandidateQt6Dir $resolved

            return [ordered]@{
                Source = $candidate.Source
                QtRoot = $root
                Qt6Dir = $resolved
                CMakeArg = "-DQt6_DIR=$resolved"
            }
        }

        if ($candidate.Kind -eq 'QtRoot') {
            $resolved = Resolve-ExistingPath -Path $candidate.Value
            if (-not $resolved) { continue }
            $qt6Dir = Join-Path $resolved 'lib\cmake\Qt6'
            if (-not (Test-Path (Join-Path $qt6Dir 'Qt6Config.cmake'))) { continue }

            return [ordered]@{
                Source = $candidate.Source
                QtRoot = $resolved
                Qt6Dir = $qt6Dir
                CMakeArg = "-DCMAKE_PREFIX_PATH=$resolved"
            }
        }
    }

    throw @"
Qt6 was not found.
Required: Qt6 desktop development for MSVC 2022 x64.
Provide one of:
  - Qt root path (example): C:\Qt\6.8.0\msvc2022_64
  - Qt6_DIR path (example): C:\Qt\6.8.0\msvc2022_64\lib\cmake\Qt6
Examples:
  .\run.ps1 -BuildApp -QtRoot "C:\Qt\6.8.0\msvc2022_64"
  .\run.ps1 -BuildApp -Qt6Dir "C:\Qt\6.8.0\msvc2022_64\lib\cmake\Qt6"
"@
}
