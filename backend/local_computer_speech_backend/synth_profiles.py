from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .path_resolver import BackendPaths


@dataclass(frozen=True)
class SynthProfileConfig:
    profile_name: str
    model_id: str
    model_path: Path
    tokenizer_path: Path
    preferred_dtype: str
    attention_mode: str | None
    speaker: str
    instruction: str
    max_chunk_chars: int
    use_case_label: str


def _profile_catalog(paths: BackendPaths) -> dict[str, SynthProfileConfig]:
    qwen_root = paths.models_root / "qwen"
    tokenizer = qwen_root / "Qwen3-TTS-Tokenizer-12Hz"
    return {
        "hq_qwen_1_7b_customvoice": SynthProfileConfig(
            profile_name="hq_qwen_1_7b_customvoice",
            model_id="Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice",
            model_path=qwen_root / "Qwen3-TTS-12Hz-1.7B-CustomVoice",
            tokenizer_path=tokenizer,
            preferred_dtype="float16",
            attention_mode=None,
            speaker="Ryan",
            instruction="",
            max_chunk_chars=520,
            use_case_label="high_quality_narration",
        ),
        "fast_qwen_0_6b_customvoice": SynthProfileConfig(
            profile_name="fast_qwen_0_6b_customvoice",
            model_id="Qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice",
            model_path=qwen_root / "Qwen3-TTS-12Hz-0.6B-CustomVoice",
            tokenizer_path=tokenizer,
            preferred_dtype="float16",
            attention_mode=None,
            speaker="Ryan",
            instruction="",
            max_chunk_chars=260,
            use_case_label="fast_assistant_readout",
        ),
    }


def resolve_profile(paths: BackendPaths, profile_name: str) -> SynthProfileConfig:
    catalog = _profile_catalog(paths)
    if profile_name in catalog:
        return catalog[profile_name]
    return catalog["hq_qwen_1_7b_customvoice"]
