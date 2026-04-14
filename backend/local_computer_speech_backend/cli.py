from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .path_resolver import ensure_runtime_dirs, resolve_paths
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


def cmd_synth_placeholder(text: str, output_file: str) -> int:
    paths = ensure_runtime_dirs()
    if not paths.model_dir.exists() or not paths.tokenizer_dir.exists():
        print(
            "ERROR: Model assets are missing. Run run.ps1 -InstallModel to install tokenizer and model "
            "into LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT.",
            file=sys.stderr,
        )
        return 2

    out = Path(output_file)
    if not out.is_absolute():
        out = paths.output / out

    out.parent.mkdir(parents=True, exist_ok=True)

    print("SYNTH_PLACEHOLDER_ONLY")
    print(f"Input text length: {len(text)}")
    print(f"Would synthesize to: {out}")
    print("No fake audio file is generated in this scaffold.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local Computer Speech backend bridge CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("healthcheck", help="Validate backend bridge and model path visibility")

    synth = sub.add_parser("synth-placeholder", help="Validate synth command path (no audio generation yet)")
    synth.add_argument("--text", required=True, help="Input text")
    synth.add_argument("--output", required=True, help="Output path (relative paths are under large-data output)")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.command == "healthcheck":
        return cmd_healthcheck()

    if args.command == "synth-placeholder":
        return cmd_synth_placeholder(text=args.text, output_file=args.output)

    parser.error("Unhandled command")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
