[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$repoRoot = Split-Path $PSScriptRoot -Parent
$largeDataRoot = Get-LargeDataRoot
Ensure-RootSubdirs -LargeDataRoot $largeDataRoot

$venvPython = Join-Path $largeDataRoot 'python_env\Scripts\python.exe'
if (-not (Test-Path $venvPython)) {
    throw "Backend python env missing at $venvPython. Run .\run.ps1 -SetupBackend first."
}

$backendRoot = Join-Path $repoRoot 'backend'
$targetQwenRoot = Join-Path $largeDataRoot 'models\qwen'

$script = @'
from huggingface_hub import snapshot_download
from pathlib import Path

repos = [
    "Qwen/Qwen3-TTS-Tokenizer-12Hz",
    "Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice",
    "Qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice",
]
base = Path(r"__TARGET_ROOT__")
base.mkdir(parents=True, exist_ok=True)

for repo_id in repos:
    local_dir = base / repo_id.split("/", 1)[1]
    print(f"Installing {repo_id} -> {local_dir}")
    snapshot_download(
        repo_id=repo_id,
        local_dir=str(local_dir),
        local_dir_use_symlinks=False,
        resume_download=True,
    )
print("Model install complete.")
'@

$script = $script.Replace('__TARGET_ROOT__', $targetQwenRoot.Replace('\\', '\\\\'))

$tempScriptPath = Join-Path $largeDataRoot 'temp\install_qwen3_tts_model.py'
$script | Set-Content -Path $tempScriptPath -Encoding UTF8

& $venvPython $tempScriptPath
if ($LASTEXITCODE -ne 0) {
    throw "Model install failed with exit code $LASTEXITCODE while running $tempScriptPath."
}

Write-Host "Installed tokenizer+model under $targetQwenRoot"
