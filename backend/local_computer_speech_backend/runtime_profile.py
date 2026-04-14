from __future__ import annotations


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
    try:
        import torch

        has_cuda = torch.cuda.is_available()
    except Exception:
        has_cuda = False

    if has_cuda:
        return {
            "device": "cuda",
            "dtype": "float16",
            "attention_backend": "standard",
            "future_attention_optimization": "seam_reserved",
        }

    return {
        "device": "cpu",
        "dtype": "float32",
        "attention_backend": "standard",
        "future_attention_optimization": "seam_reserved",
    }
