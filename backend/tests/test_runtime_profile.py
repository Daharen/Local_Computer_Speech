from __future__ import annotations

import types
import unittest
from unittest.mock import patch

from local_computer_speech_backend import runtime_profile


class TestRuntimeProfileForceDevice(unittest.TestCase):
    def test_force_cpu_overrides_cuda_detection(self) -> None:
        fake_torch = types.SimpleNamespace(
            __version__="2.0",
            version=types.SimpleNamespace(cuda="12.1"),
            cuda=types.SimpleNamespace(
                is_available=lambda: True,
                device_count=lambda: 1,
                get_device_name=lambda _idx: "Fake GPU",
            ),
        )
        with patch.dict("os.environ", {"LCS_FORCE_DEVICE": "cpu"}, clear=False), patch.dict(
            "sys.modules", {"torch": fake_torch}
        ):
            profile = runtime_profile.choose_runtime_profile()

        self.assertEqual(profile["device"], "cpu")
        self.assertEqual(profile["dtype"], "float32")
        self.assertEqual(profile["force_device_reason"], "LCS_FORCE_DEVICE=cpu")

    def test_force_cuda_records_ignored_reason_when_unavailable(self) -> None:
        fake_torch = types.SimpleNamespace(
            __version__="2.0",
            version=types.SimpleNamespace(cuda=None),
            cuda=types.SimpleNamespace(
                is_available=lambda: False,
                device_count=lambda: 0,
                get_device_name=lambda _idx: "",
            ),
        )
        with patch.dict("os.environ", {"LCS_FORCE_DEVICE": "cuda"}, clear=False), patch.dict(
            "sys.modules", {"torch": fake_torch}
        ):
            profile = runtime_profile.choose_runtime_profile()

        self.assertEqual(profile["device"], "cpu")
        self.assertIn("ignored", str(profile["force_device_reason"]))


if __name__ == "__main__":
    unittest.main()
