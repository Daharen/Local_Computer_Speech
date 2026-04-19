from __future__ import annotations

import importlib.util
import os


def _flash_attention_available() -> bool:
    return importlib.util.find_spec("flash_attn") is not None


def choose_runtime_profile() -> dict:
    """
    C++-first app owns orchestration; Python exposes narrow model-runtime compatibility info.

    Initial profile doctrine:
      - prefer CUDA when available
      - float16 on CUDA path
      - do not hardcode bf16
      - do not hard-require FlashAttention-2
      - reserve optimization seam for future attention backends
    """
    forced_device_raw = os.environ.get("LCS_FORCE_DEVICE", "").strip().lower()
    forced_device = forced_device_raw if forced_device_raw in {"cpu", "cuda"} else None

    device = "cpu"
    dtype = "float32"
    attention_backend = "standard"

    torch_version = None
    cuda_available = False
    torch_cuda_build = None
    device_count = 0
    device_name_0 = None

    try:
        import torch

        torch_version = getattr(torch, "__version__", None)
        cuda_available = bool(torch.cuda.is_available())
        torch_cuda_build = getattr(torch.version, "cuda", None)
        device_count = int(torch.cuda.device_count() if cuda_available else 0)

        if device_count > 0:
            try:
                device_name_0 = str(torch.cuda.get_device_name(0))
            except Exception:
                device_name_0 = None

        if cuda_available:
            device = "cuda"
            dtype = "float16"
            if _flash_attention_available():
                attention_backend = "flash-attn"
    except Exception:
        pass

    force_reason = None
    if forced_device == "cpu":
        device = "cpu"
        dtype = "float32"
        attention_backend = "standard"
        force_reason = "LCS_FORCE_DEVICE=cpu"
    elif forced_device == "cuda":
        if cuda_available:
            device = "cuda"
            dtype = "float16"
            if _flash_attention_available():
                attention_backend = "flash-attn"
            else:
                attention_backend = "standard"
            force_reason = "LCS_FORCE_DEVICE=cuda"
        else:
            force_reason = "LCS_FORCE_DEVICE=cuda (ignored: cuda unavailable)"

    return {
        "device": device,
        "dtype": dtype,
        "attention_backend": attention_backend,
        "future_attention_optimization": "seam_reserved",
        "torch_version": torch_version,
        "cuda_available": cuda_available,
        "torch_cuda_build": torch_cuda_build,
        "device_count": device_count,
        "device_name_0": device_name_0,
        "force_device": forced_device,
        "force_device_reason": force_reason,
    }
