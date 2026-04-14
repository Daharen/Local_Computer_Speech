# Local Computer Speech

Local Computer Speech is a **C++-first Qt desktop tray app** with a thin Python compatibility bridge for Qwen3-TTS inference.

## C++-First Doctrine (Binding)

- **Primary architecture is C++/Qt**: app lifecycle, tray behavior, UI, command dispatch, path policy, runtime state, diagnostics orchestration, and future queue/watch workflows belong in C++.
- **Python is intentionally narrow and replaceable**: only model-access compatibility routines (healthcheck, synth adapter command path, bootstrap helpers) live in Python.
- Future work should preserve or reduce Python surface area, not expand it.

## Disposable Repo vs Persistent Large-Data Doctrine

- Repo clone is disposable and may be wiped/recloned.
- Large assets must not live in repo.
- Persistent runtime assets are stored under:
  - `LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT`, or fallback:
  - `F:\My_Programs\Local_Computer_Speech_Large_Data`

The app and scripts consume wrapper-provided env vars:

- `LOCAL_COMPUTER_SPEECH_PROJECT_ROOT`
- `LOCAL_COMPUTER_SPEECH_TOOLS_ROOT`
- `LOCAL_COMPUTER_SPEECH_REPO_ROOT`
- `LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT`

## Initial Backend Model

- Tokenizer: `Qwen/Qwen3-TTS-Tokenizer-12Hz`
- Model: `Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice`

Installed locally under:

- `<LARGE_DATA_ROOT>\models\qwen\Qwen3-TTS-Tokenizer-12Hz`
- `<LARGE_DATA_ROOT>\models\qwen\Qwen3-TTS-12Hz-1.7B-CustomVoice`

Normal launch **does not auto-download** model weights. Installation is explicit.

## Setup Sequence

From repo root:

1. Setup backend env
   - `./run.ps1 -SetupBackend`
2. Install tokenizer+model explicitly
   - `./run.ps1 -InstallModel`
3. Backend healthcheck
   - `./run.ps1 -Healthcheck`
4. Build Qt app (example)
   - `cmake -S . -B build`
   - `cmake --build build --config Release`
5. Launch app
   - `./run.ps1 -LaunchApp`

Convenience setup path:

- `./run.ps1 -AllSetup`

## Launch Sequence

- Startup/lifecycle is controlled by C++ Qt app (`LocalComputerSpeech.exe`).
- Tray app exposes right-click actions:
  - Launch UI
  - Live Read (placeholder)
  - Text File To Audio (placeholder)
  - Open Output Folder
  - Exit

## Runtime Profile Assumptions (Initial)

- Prefer CUDA when available.
- CUDA dtype: `float16`.
- No hard requirement for `bf16`.
- No hard requirement for FlashAttention-2.
- Keep a clear seam for future attention backend optimization.

## Why a Python Backend Exists at All

The selected initial TTS stack is currently exposed through Python tooling. Therefore this scaffold uses a thin Python worker bridge for model runtime access while maintaining a C++-owned application architecture.
