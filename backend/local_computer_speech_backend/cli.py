from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from .path_resolver import ensure_runtime_dirs
from .runtime_profile import choose_runtime_profile
from .synth_profiles import SynthProfileConfig, resolve_profile

_MODEL_CACHE: dict[str, object] = {}


def _log_event(event: str, **fields: object) -> None:
    payload = {"event": event, **fields}
    print(json.dumps(payload, ensure_ascii=False), file=sys.stderr, flush=True)


def cmd_healthcheck() -> int:
    paths = ensure_runtime_dirs()
    runtime_profile = choose_runtime_profile()

    has_model = paths.model_dir.exists()
    has_tokenizer = paths.tokenizer_dir.exists()

    payload = {
        "status": "ok" if (has_model and has_tokenizer) else "degraded",
        "message": (
            "Backend reachable and local model assets found."
            if (has_model and has_tokenizer)
            else "Model/tokenizer missing. Run run.ps1 -InstallModel explicitly before launch."
        ),
        "paths": {
            "large_data_root": str(paths.large_data_root),
            "model": str(paths.model_dir),
            "tokenizer": str(paths.tokenizer_dir),
        },
        "runtime_profile": runtime_profile,
    }
    print(json.dumps(payload, indent=2))
    return 0 if payload["status"] == "ok" else 2


def _emit_synth_response(
    *,
    ok: bool,
    output_path: str,
    sample_rate: int,
    speaker: str,
    language: str,
    elapsed_ms: int,
    device: str,
    attention_backend: str,
    torch_version: str,
    error: str,
    profile: str,
) -> dict:
    payload = {
        "ok": ok,
        "output_path": output_path,
        "sample_rate": sample_rate,
        "speaker": speaker,
        "language": language,
        "elapsed_ms": elapsed_ms,
        "device": device,
        "attention_backend": attention_backend,
        "torch_version": torch_version,
        "error": error,
        "profile": profile,
    }
    return payload


def _import_qwen_model_class():
    try:
        from qwen_tts import Qwen3TTSModel  # type: ignore

        return Qwen3TTSModel
    except Exception:
        from qwen_tts.model import Qwen3TTSModel  # type: ignore

        return Qwen3TTSModel


def _normalize_generation_output(generation):
    sample_rate = 24000
    audio = generation

    if isinstance(generation, dict):
        audio = generation.get("audio") or generation.get("wav") or generation.get("waveform")
        sample_rate = int(generation.get("sample_rate", sample_rate))
    elif isinstance(generation, (tuple, list)) and len(generation) >= 2:
        audio = generation[0]
        sample_rate = int(generation[1])

    if audio is None:
        raise ValueError("Model returned no audio waveform data.")

    try:
        import numpy as np

        audio = np.asarray(audio).squeeze()
    except Exception as exc:  # pragma: no cover - defensive path
        raise RuntimeError(f"Failed to normalize generated audio: {exc}") from exc

    if audio.size == 0:
        raise ValueError("Generated audio waveform was empty.")

    return audio, sample_rate


def _is_dtype_compatibility_error(exc: Exception) -> bool:
    message = str(exc).lower()
    if "dtype" not in message:
        return False

    keyword_or_deprecation_markers = (
        "deprecated",
        "unexpected keyword",
        "keyword argument",
    )
    return any(marker in message for marker in keyword_or_deprecation_markers)


def _is_tokenizer_path_compatibility_error(exc: Exception) -> bool:
    message = str(exc).lower()
    if "tokenizer_path" not in message:
        return False

    keyword_or_deprecation_markers = (
        "deprecated",
        "unexpected keyword",
        "keyword argument",
        "positional argument",
        "signature",
    )
    return any(marker in message for marker in keyword_or_deprecation_markers)


def _is_attention_implementation_compatibility_error(exc: Exception) -> bool:
    message = str(exc).lower()
    if "attn_implementation" not in message and "attention" not in message:
        return False

    compatibility_markers = (
        "unsupported",
        "not support",
        "not supported",
        "unexpected keyword",
        "keyword argument",
        "signature",
    )
    return any(marker in message for marker in compatibility_markers)


def _resolve_attention_mode(configured_mode: str | None, runtime_profile: dict) -> str:
    requested = configured_mode or "eager"
    normalized = {
        "standard": "eager",
        "flash-attn": "flash_attention_2",
        "eager": "eager",
        "sdpa": "sdpa",
        "flash_attention_2": "flash_attention_2",
        "flash_attention_3": "flash_attention_3",
    }.get(requested, requested)

    if normalized in ("flash_attention_2", "flash_attention_3") and runtime_profile.get("attention_backend") != "flash-attn":
        _log_event("attention_fallback", requested=requested, resolved="eager")
        return "eager"
    return normalized


def _split_sentences(text: str) -> list[str]:
    segments = [seg.strip() for seg in re.split(r"(?<=[.!?;])\s+", text) if seg.strip()]
    if segments:
        return segments
    stripped = text.strip()
    return [stripped] if stripped else []


def chunk_text(text: str, *, soft_target: int, hard_max: int) -> list[str]:
    paragraphs = [p.strip() for p in text.replace("\r\n", "\n").split("\n\n") if p.strip()]
    chunks: list[str] = []

    for paragraph in paragraphs or [text.strip()]:
        current = ""
        for sentence in _split_sentences(paragraph):
            candidate = sentence if not current else f"{current} {sentence}".strip()
            if len(candidate) <= soft_target:
                current = candidate
                continue

            if current:
                chunks.append(current)
                current = ""

            if len(sentence) <= hard_max:
                current = sentence
                continue

            words = sentence.split()
            segment = ""
            for word in words:
                next_segment = word if not segment else f"{segment} {word}"
                if len(next_segment) <= hard_max:
                    segment = next_segment
                else:
                    if segment:
                        chunks.append(segment)
                    segment = word
            if segment:
                current = segment

        if current:
            chunks.append(current)

    return [c for c in chunks if c.strip()]


@dataclass
class ModelRuntime:
    model: object
    resolved_device: str
    resolved_attention_mode: str


def _load_model_for_profile(
    profile: SynthProfileConfig,
    runtime_profile: dict,
):
    cached = _MODEL_CACHE.get(profile.profile_name)
    if cached is not None:
        _log_event("model_cache_hit", profile=profile.profile_name)
        return cached

    _log_event("model_load_start", profile=profile.profile_name, model_path=str(profile.model_path))
    import torch

    Qwen3TTSModel = _import_qwen_model_class()

    resolved_attention_mode = _resolve_attention_mode(profile.attention_mode, runtime_profile)
    resolved_device = "cpu"
    if runtime_profile.get("device") == "cuda" and torch.cuda.is_available():
        resolved_device = "cuda:0"

    dtype_name = profile.preferred_dtype or runtime_profile.get("dtype", "float32")
    if dtype_name == "float16":
        torch_dtype = torch.float16
    else:
        torch_dtype = torch.float32

    load_kwargs = {
        "dtype": torch_dtype,
        "trust_remote_code": True,
        "local_files_only": True,
        "device_map": "auto" if resolved_device.startswith("cuda") else None,
    }

    if resolved_attention_mode:
        load_kwargs["attn_implementation"] = resolved_attention_mode

    model_path = str(profile.model_path)
    tokenizer_path = str(profile.tokenizer_path)

    def _load_with_tokenizer_retry(**kwargs):
        try:
            return Qwen3TTSModel.from_pretrained(
                model_path,
                tokenizer_path=tokenizer_path,
                **kwargs,
            )
        except Exception as exc:
            if not _is_tokenizer_path_compatibility_error(exc):
                raise

            return Qwen3TTSModel.from_pretrained(
                model_path,
                **kwargs,
            )

    def _load_with_dtype_retry(load_attempt_kwargs: dict):
        try:
            return _load_with_tokenizer_retry(**load_attempt_kwargs)
        except Exception as exc:
            if not _is_dtype_compatibility_error(exc):
                raise

            fallback_kwargs = dict(load_attempt_kwargs)
            fallback_kwargs["torch_dtype"] = fallback_kwargs.pop("dtype")
            return _load_with_tokenizer_retry(**fallback_kwargs)

    try:
        model = _load_with_dtype_retry(load_kwargs)
    except Exception as exc:
        if "attn_implementation" not in load_kwargs or not _is_attention_implementation_compatibility_error(exc):
            raise

        _log_event(
            "attention_override_retry",
            profile=profile.profile_name,
            requested=resolved_attention_mode,
            resolved="model_default",
        )
        fallback_kwargs = dict(load_kwargs)
        fallback_kwargs.pop("attn_implementation", None)
        model = _load_with_dtype_retry(fallback_kwargs)
        resolved_attention_mode = "model_default"

    runtime = ModelRuntime(
        model=model,
        resolved_device=resolved_device,
        resolved_attention_mode=resolved_attention_mode,
    )
    _MODEL_CACHE[profile.profile_name] = runtime
    _log_event(
        "model_load_end",
        profile=profile.profile_name,
        device=resolved_device,
        attention_mode=resolved_attention_mode,
    )
    return runtime


def _build_synth_payload(request_json: str) -> tuple[int, dict]:
    start = time.perf_counter()
    paths = ensure_runtime_dirs()

    request_path = Path(request_json)
    if not request_path.exists():
        return 1, _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker="Ryan",
            language="English",
            elapsed_ms=0,
            device="",
            attention_backend="eager",
            torch_version="",
            error=f"Request JSON file does not exist: {request_path}",
            profile="hq_qwen_1_7b_customvoice",
        )

    try:
        request = json.loads(request_path.read_text(encoding="utf-8-sig"))
    except Exception as exc:
        return 1, _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker="Ryan",
            language="English",
            elapsed_ms=0,
            device="",
            attention_backend="eager",
            torch_version="",
            error=f"Failed to read request JSON: {exc}",
            profile="hq_qwen_1_7b_customvoice",
        )

    profile_name = str(request.get("profile", "hq_qwen_1_7b_customvoice") or "hq_qwen_1_7b_customvoice")
    profile = resolve_profile(paths, profile_name)
    runtime_profile = choose_runtime_profile()

    _log_event("request_accepted", profile=profile.profile_name)

    text = str(request.get("text", "")).strip()
    output_path_raw = str(request.get("output_path", "")).strip()
    language = str(request.get("language", "English") or "English")
    speaker = str(request.get("speaker", profile.speaker) or profile.speaker)
    instruct = str(request.get("instruct", profile.instruction) or profile.instruction)

    if not text:
        return 1, _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=0,
            device="",
            attention_backend="eager",
            torch_version="",
            error="Input text is empty after trimming.",
            profile=profile.profile_name,
        )

    _log_event("text_normalized", profile=profile.profile_name, char_count=len(text))

    if not output_path_raw:
        return 1, _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=0,
            device="",
            attention_backend="eager",
            torch_version="",
            error="output_path is required in request JSON.",
            profile=profile.profile_name,
        )

    output_path = Path(output_path_raw)
    if not output_path.is_absolute():
        output_path = paths.output / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not profile.model_path.exists() or not profile.tokenizer_path.exists():
        return 1, _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=0,
            device="",
            attention_backend="eager",
            torch_version="",
            error=(
                f"Model assets are missing for profile {profile.profile_name}. "
                "Run run.ps1 -InstallModel to install tokenizer and model into LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT."
            ),
            profile=profile.profile_name,
        )

    soft_target = int(request.get("soft_chunk_chars", profile.max_chunk_chars))
    hard_max = int(request.get("hard_chunk_chars", max(profile.max_chunk_chars + 120, 320)))
    chunks = chunk_text(text, soft_target=soft_target, hard_max=hard_max)
    _log_event("chunking_complete", profile=profile.profile_name, chunks=len(chunks), soft_target=soft_target, hard_max=hard_max)

    try:
        import numpy as np
        import soundfile as sf

        _log_event("total_synthesis_start", profile=profile.profile_name)
        model_runtime = _load_model_for_profile(profile, runtime_profile)

        audio_parts = []
        sample_rate = 24000
        first_chunk_start = None
        first_chunk_end = None

        for index, chunk in enumerate(chunks):
            chunk_start = time.perf_counter()
            _log_event(
                "chunk_generation_start",
                profile=profile.profile_name,
                chunk_index=index + 1,
                chunk_total=len(chunks),
            )
            if index == 0:
                first_chunk_start = time.perf_counter()
                _log_event("first_chunk_generation_start", profile=profile.profile_name)
            generation = model_runtime.model.generate_custom_voice(
                text=chunk,
                speaker=speaker,
                language=language,
                instruct=instruct,
            )
            audio, sample_rate = _normalize_generation_output(generation)
            audio_parts.append(audio)
            _log_event(
                "chunk_generation_end",
                profile=profile.profile_name,
                chunk_index=index + 1,
                elapsed_sec=round(time.perf_counter() - chunk_start, 3),
            )
            if index == 0:
                first_chunk_end = time.perf_counter()
                _log_event("first_chunk_generation_end", profile=profile.profile_name)

        merged_audio = np.concatenate(audio_parts)
        sf.write(str(output_path), merged_audio, sample_rate)
        _log_event("wav_merge_write_complete", profile=profile.profile_name, output_path=str(output_path))

        elapsed_seconds = time.perf_counter() - start
        elapsed_ms = int(elapsed_seconds * 1000)
        wav_duration_sec = float(len(merged_audio)) / float(sample_rate)
        word_count_estimate = len(text.split())
        rtf = elapsed_seconds / wav_duration_sec if wav_duration_sec > 0 else 0.0
        seconds_per_minute_wall = (wav_duration_sec / elapsed_seconds) * 60.0 if elapsed_seconds > 0 else 0.0

        _log_event(
            "synthesis_benchmark",
            profile=profile.profile_name,
            char_count=len(text),
            word_count_estimate=word_count_estimate,
            output_wav_duration_sec=round(wav_duration_sec, 3),
            wall_clock_synthesis_sec=round(elapsed_seconds, 3),
            real_time_factor=round(rtf, 3),
            sec_audio_per_min_wall=round(seconds_per_minute_wall, 3),
            first_chunk_latency_sec=round((first_chunk_end - first_chunk_start), 3)
            if (first_chunk_start and first_chunk_end)
            else None,
        )
        _log_event("total_synthesis_end", profile=profile.profile_name)

        return 0, _emit_synth_response(
            ok=True,
            output_path=str(output_path),
            sample_rate=int(sample_rate),
            speaker=speaker,
            language=language,
            elapsed_ms=elapsed_ms,
            device=model_runtime.resolved_device,
            attention_backend=model_runtime.resolved_attention_mode,
            torch_version=str(runtime_profile.get("torch_version") or ""),
            error="",
            profile=profile.profile_name,
        )
    except Exception as exc:
        elapsed_ms = int((time.perf_counter() - start) * 1000)
        return 1, _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=elapsed_ms,
            device="",
            attention_backend=_resolve_attention_mode(profile.attention_mode, runtime_profile),
            torch_version=str(runtime_profile.get("torch_version") or ""),
            error=f"Synthesis failed: {exc}",
            profile=profile.profile_name,
        )


def cmd_synth_request(request_json: str) -> int:
    rc, payload = _build_synth_payload(request_json)
    print(json.dumps(payload, ensure_ascii=False))
    return rc


def cmd_serve() -> int:
    print("READY", flush=True)
    for line in sys.stdin:
        request_json = line.strip()
        if not request_json:
            continue
        rc, payload = _build_synth_payload(request_json)
        print(json.dumps(payload, ensure_ascii=False), flush=True)
        if payload.get("ok") is False and "Input text is empty" in str(payload.get("error")):
            pass
    return 0


def cmd_probe_profile(profile_name: str) -> int:
    paths = ensure_runtime_dirs()
    profile = resolve_profile(paths, profile_name)

    payload = {
        "profile": profile.profile_name,
        "supported_speakers": [profile.speaker],
        "supported_languages": ["English"],
    }
    _log_event(
        "profile_probe",
        profile=profile.profile_name,
        supported_speakers=payload["supported_speakers"],
        supported_languages=payload["supported_languages"],
    )
    print(json.dumps(payload, ensure_ascii=False))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local Computer Speech backend bridge CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("healthcheck", help="Validate backend bridge and model path visibility")

    synth = sub.add_parser("synth", help="Generate a real WAV using a JSON request file")
    synth.add_argument("--request-json", required=True, help="Path to synthesis request JSON")

    sub.add_parser("serve", help="Start persistent backend worker over stdin/stdout")
    probe = sub.add_parser("probe-profile", help="Show supported speakers/languages for a profile")
    probe.add_argument("--profile", default="hq_qwen_1_7b_customvoice", help="Profile name to probe")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.command == "healthcheck":
        return cmd_healthcheck()

    if args.command == "synth":
        return cmd_synth_request(request_json=args.request_json)

    if args.command == "serve":
        return cmd_serve()

    if args.command == "probe-profile":
        return cmd_probe_profile(profile_name=args.profile)

    parser.error("Unhandled command")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
