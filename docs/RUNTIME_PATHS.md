# Runtime Paths and Persistence Policy

## Environment Variables Consumed

- `LOCAL_COMPUTER_SPEECH_PROJECT_ROOT`
- `LOCAL_COMPUTER_SPEECH_TOOLS_ROOT`
- `LOCAL_COMPUTER_SPEECH_REPO_ROOT`
- `LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT`

If `LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT` is missing, fallback is:

- `F:\My_Programs\Local_Computer_Speech_Large_Data`

## Persistent Directory Layout

All mutable or large runtime assets live under `<LARGE_DATA_ROOT>`:

- `models\qwen\Qwen3-TTS-Tokenizer-12Hz`
- `models\qwen\Qwen3-TTS-12Hz-1.7B-CustomVoice`
- `python_env`
- `logs`
- `cache`
- `runtime`
- `output`
- `temp`

## Prohibited Placement

Do not place model files, venv, cache, or generated runtime artifacts under the disposable repo clone.

## Deterministic Bootstrap

- Backend env creation is explicit: `run.ps1 -SetupBackend`
- Model install is explicit: `run.ps1 -InstallModel`
- Launch path never performs implicit model download.
