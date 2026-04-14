$ErrorActionPreference = 'Stop'

function Get-LargeDataRoot {
    param()

    if ($env:LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT -and $env:LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT.Trim().Length -gt 0) {
        return $env:LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT
    }

    return 'F:\My_Programs\Local_Computer_Speech_Large_Data'
}

function Get-PythonExe {
    param(
        [string]$LargeDataRoot
    )

    $venvPy = Join-Path $LargeDataRoot 'python_env\Scripts\python.exe'
    if (Test-Path $venvPy) {
        return $venvPy
    }

    $candidates = @('py', 'python')
    foreach ($candidate in $candidates) {
        try {
            & $candidate --version *> $null
            if ($LASTEXITCODE -eq 0) {
                return $candidate
            }
        } catch {
            # Try next candidate.
        }
    }

    throw 'No Python launcher found. Install Python 3.10+ and re-run setup.'
}

function Ensure-RootSubdirs {
    param(
        [string]$LargeDataRoot
    )

    $dirs = @(
        'models\qwen\Qwen3-TTS-Tokenizer-12Hz',
        'models\qwen\Qwen3-TTS-12Hz-1.7B-CustomVoice',
        'python_env',
        'logs',
        'cache',
        'runtime',
        'output',
        'temp'
    )

    foreach ($dir in $dirs) {
        New-Item -ItemType Directory -Path (Join-Path $LargeDataRoot $dir) -Force | Out-Null
    }
}
