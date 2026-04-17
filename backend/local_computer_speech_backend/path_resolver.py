from __future__ import annotations

import os
from pathlib import Path
from dataclasses import dataclass

DEFAULT_LARGE_DATA_ROOT = Path(r"F:\My_Programs\Local_Computer_Speech_Large_Data")


@dataclass(frozen=True)
class BackendPaths:
    large_data_root: Path
    python_env: Path
    logs: Path
    cache: Path
    runtime: Path
    output: Path
    temp: Path
    tokenizer_dir: Path
    model_dir: Path


def resolve_paths() -> BackendPaths:
    large_data_root = Path(os.getenv("LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT", str(DEFAULT_LARGE_DATA_ROOT)))

    models_root = large_data_root / "models"
    return BackendPaths(
        large_data_root=large_data_root,
        python_env=large_data_root / "python_env",
        logs=large_data_root / "logs",
        cache=large_data_root / "cache",
        runtime=large_data_root / "runtime",
        output=large_data_root / "output",
        temp=large_data_root / "temp",
        tokenizer_dir=models_root / "qwen" / "Qwen3-TTS-Tokenizer-12Hz",
        model_dir=models_root / "qwen" / "Qwen3-TTS-12Hz-1.7B-CustomVoice",
    )


def ensure_runtime_dirs() -> BackendPaths:
    paths = resolve_paths()
    for p in [
        paths.large_data_root,
        paths.python_env,
        paths.logs,
        paths.cache,
        paths.runtime,
        paths.output,
        paths.temp,
        paths.tokenizer_dir.parent,
    ]:
        p.mkdir(parents=True, exist_ok=True)
    return paths
