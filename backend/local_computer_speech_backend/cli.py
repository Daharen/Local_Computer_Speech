from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from .path_resolver import ensure_runtime_dirs
from .runtime_profile import choose_runtime_profile


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
    error: str,
) -> int:
    payload = {
        "ok": ok,
        "output_path": output_path,
        "sample_rate": sample_rate,
        "speaker": speaker,
        "language": language,
        "elapsed_ms": elapsed_ms,
        "device": device,
        "error": error,
    }
    print(json.dumps(payload, ensure_ascii=False))
    return 0 if ok else 1


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


def cmd_synth_request(request_json: str) -> int:
    start = time.perf_counter()
    paths = ensure_runtime_dirs()

    request_path = Path(request_json)
    if not request_path.exists():
        return _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker="Ryan",
            language="English",
            elapsed_ms=0,
            device="",
            error=f"Request JSON file does not exist: {request_path}",
        )

    try:
        request = json.loads(request_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker="Ryan",
            language="English",
            elapsed_ms=0,
            device="",
            error=f"Failed to read request JSON: {exc}",
        )

    text = str(request.get("text", "")).strip()
    output_path_raw = str(request.get("output_path", "")).strip()
    language = str(request.get("language", "English") or "English")
    speaker = str(request.get("speaker", "Ryan") or "Ryan")
    instruct = str(request.get("instruct", "") or "")

    if not text:
        return _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=0,
            device="",
            error="Input text is empty after trimming.",
        )

    if not output_path_raw:
        return _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=0,
            device="",
            error="output_path is required in request JSON.",
        )

    output_path = Path(output_path_raw)
    if not output_path.is_absolute():
        output_path = paths.output / output_path

    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not paths.model_dir.exists() or not paths.tokenizer_dir.exists():
        return _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=0,
            device="",
            error=(
                "Model assets are missing. Run run.ps1 -InstallModel to install tokenizer and model into "
                "LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT."
            ),
        )

    runtime_profile = choose_runtime_profile()

    try:
        import torch
        import soundfile as sf

        Qwen3TTSModel = _import_qwen_model_class()

        device = runtime_profile.get("device", "cpu")
        dtype_name = runtime_profile.get("dtype", "float32")
        torch_dtype = torch.float16 if dtype_name == "float16" else torch.float32

        load_kwargs = {
            "dtype": torch_dtype,
            "trust_remote_code": True,
            "local_files_only": True,
        }

        if device == "cuda" and torch.cuda.is_available():
            load_kwargs["device_map"] = "auto"
            resolved_device = "cuda:0"
        else:
            load_kwargs["device_map"] = None
            resolved_device = "cpu"

        model_path = str(paths.model_dir)
        tokenizer_path = str(paths.tokenizer_dir)

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

        try:
            model = _load_with_tokenizer_retry(**load_kwargs)
        except Exception as exc:
            if not _is_dtype_compatibility_error(exc):
                raise

            fallback_kwargs = dict(load_kwargs)
            fallback_kwargs["torch_dtype"] = fallback_kwargs.pop("dtype")
            model = _load_with_tokenizer_retry(**fallback_kwargs)

        generation = model.generate_custom_voice(
            text=text,
            speaker=speaker,
            language=language,
            instruct=instruct,
        )

        audio, sample_rate = _normalize_generation_output(generation)
        sf.write(str(output_path), audio, sample_rate)

        elapsed_ms = int((time.perf_counter() - start) * 1000)
        return _emit_synth_response(
            ok=True,
            output_path=str(output_path),
            sample_rate=int(sample_rate),
            speaker=speaker,
            language=language,
            elapsed_ms=elapsed_ms,
            device=resolved_device,
            error="",
        )
    except Exception as exc:
        elapsed_ms = int((time.perf_counter() - start) * 1000)
        return _emit_synth_response(
            ok=False,
            output_path="",
            sample_rate=0,
            speaker=speaker,
            language=language,
            elapsed_ms=elapsed_ms,
            device="",
            error=f"Synthesis failed: {exc}",
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local Computer Speech backend bridge CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("healthcheck", help="Validate backend bridge and model path visibility")

    synth = sub.add_parser("synth", help="Generate a real WAV using a JSON request file")
    synth.add_argument("--request-json", required=True, help="Path to synthesis request JSON")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.command == "healthcheck":
        return cmd_healthcheck()

    if args.command == "synth":
        return cmd_synth_request(request_json=args.request_json)

    parser.error("Unhandled command")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
