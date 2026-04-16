[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

function Get-TorchCudaProbe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonExe
    )

    $probeJson = & $PythonExe -c "import json, torch; data={'torch_version': torch.__version__, 'cuda_available': bool(torch.cuda.is_available()), 'torch_cuda_build': torch.version.cuda, 'device_count': int(torch.cuda.device_count() if torch.cuda.is_available() else 0)}; print(json.dumps(data))"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to probe torch CUDA runtime (exit $LASTEXITCODE)."
    }

    return ($probeJson | ConvertFrom-Json)
}

function Install-TorchDeterministic {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonExe
    )

    $torchPackages = @('torch', 'torchvision', 'torchaudio')
    $allowCpuTorch = $env:LOCAL_COMPUTER_SPEECH_ALLOW_CPU_TORCH -eq '1'

    if (-not $IsWindows) {
        Write-Host 'Non-Windows host detected; installing torch packages with default index policy.'
        & $PythonExe -m pip install --upgrade @torchPackages
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install torch packages on non-Windows host (exit $LASTEXITCODE)."
        }
        return (Get-TorchCudaProbe -PythonExe $PythonExe)
    }

    $customIndex = $env:LOCAL_COMPUTER_SPEECH_TORCH_INDEX_URL
    if ([string]::IsNullOrWhiteSpace($customIndex)) {
        $torchIndices = @(
            'https://download.pytorch.org/whl/cu128',
            'https://download.pytorch.org/whl/cu126',
            'https://download.pytorch.org/whl/cu124'
        )
    }
    else {
        $torchIndices = @($customIndex)
    }

    foreach ($indexUrl in $torchIndices) {
        Write-Host "Attempting CUDA torch install from index: $indexUrl"
        & $PythonExe -m pip install --upgrade --index-url $indexUrl @torchPackages
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Torch install failed from $indexUrl (exit $LASTEXITCODE). Trying next index."
            continue
        }

        try {
            $probe = Get-TorchCudaProbe -PythonExe $PythonExe
        }
        catch {
            Write-Warning "Torch probe failed after install from $indexUrl: $($_.Exception.Message)"
            continue
        }

        Write-Host (
            "Torch probe: version={0}, cuda_available={1}, torch_cuda_build={2}, device_count={3}" -f
            $probe.torch_version,
            $probe.cuda_available,
            $probe.torch_cuda_build,
            $probe.device_count
        )

        if ($probe.cuda_available -and -not [string]::IsNullOrWhiteSpace($probe.torch_cuda_build)) {
            Write-Host "CUDA-capable torch installation verified via $indexUrl."
            return $probe
        }

        Write-Warning "Torch from $indexUrl did not expose CUDA at runtime. Trying next index."
    }

    if ($allowCpuTorch) {
        Write-Warning 'LOCAL_COMPUTER_SPEECH_ALLOW_CPU_TORCH=1 set; allowing CPU-only torch fallback.'
        & $PythonExe -m pip install --upgrade @torchPackages
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install CPU fallback torch packages (exit $LASTEXITCODE)."
        }
        return (Get-TorchCudaProbe -PythonExe $PythonExe)
    }

    throw @"
CUDA-capable torch provisioning failed on Windows.
CPU-only torch is not accepted by default for this project's intended Windows/NVIDIA target.
Set LOCAL_COMPUTER_SPEECH_ALLOW_CPU_TORCH=1 only if you explicitly want to proceed without CUDA.
"@
}

function Install-OptionalFlashAttention {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonExe,
        [Parameter(Mandatory = $true)]
        [pscustomobject]$TorchProbe
    )

    if ($env:LOCAL_COMPUTER_SPEECH_SKIP_FLASH_ATTN -eq '1') {
        Write-Host 'Skipping flash-attn install due to LOCAL_COMPUTER_SPEECH_SKIP_FLASH_ATTN=1.'
        return
    }

    if (-not $TorchProbe.cuda_available) {
        Write-Host 'Skipping flash-attn install because CUDA is not available in torch runtime.'
        return
    }

    Write-Host 'Attempting optional flash-attn installation (best effort).'
    & $PythonExe -m pip install --no-build-isolation flash-attn
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Optional flash-attn install failed (exit $LASTEXITCODE). Continuing setup."
        return
    }

    $flashAttnProbe = & $PythonExe -c "import importlib.util; print('present' if importlib.util.find_spec('flash_attn') else 'missing')"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "flash-attn probe failed after installation (exit $LASTEXITCODE). Continuing setup."
        return
    }

    if ($flashAttnProbe -eq 'present') {
        Write-Host 'Optional flash-attn install succeeded and module import is detectable.'
        return
    }

    Write-Warning 'flash-attn install completed but module was not detected; continuing setup.'
}

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
    throw "Failed to install non-torch backend requirements (exit $LASTEXITCODE)."
}

$torchProbe = Install-TorchDeterministic -PythonExe $venvPython
Install-OptionalFlashAttention -PythonExe $venvPython -TorchProbe $torchProbe

Write-Host 'Backend environment setup complete.'
Write-Host "Python: $venvPython"
Write-Host (
    "Torch diagnostics: version={0}, cuda_available={1}, torch_cuda_build={2}, device_count={3}" -f
    $torchProbe.torch_version,
    $torchProbe.cuda_available,
    $torchProbe.torch_cuda_build,
    $torchProbe.device_count
)
