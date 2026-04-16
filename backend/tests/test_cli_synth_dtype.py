from __future__ import annotations

import io
import json
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

from local_computer_speech_backend import cli


class _FakeModel:
    def generate_custom_voice(self, **_: object):
        return [0.1, -0.1], 24000


class _FakeQwenModelClass:
    calls = []
    errors_by_call_index = {}

    @classmethod
    def reset(cls) -> None:
        cls.calls = []
        cls.errors_by_call_index = {}

    @classmethod
    def from_pretrained(cls, model_path: str, **kwargs: object):
        cls.calls.append((model_path, kwargs))
        call_index = len(cls.calls)
        error = cls.errors_by_call_index.get(call_index)
        if error is not None:
            raise error
        return _FakeModel()


class TestCliSynthDtypeCompatibility(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        root = Path(self.tempdir.name)
        self.paths = types.SimpleNamespace(
            model_dir=root / "models" / "qwen-model",
            tokenizer_dir=root / "models" / "qwen-tokenizer",
            output=root / "output",
            large_data_root=root,
        )
        self.paths.model_dir.mkdir(parents=True, exist_ok=True)
        self.paths.tokenizer_dir.mkdir(parents=True, exist_ok=True)
        self.paths.output.mkdir(parents=True, exist_ok=True)

        self.request_path = root / "request.json"
        self.request_payload = {
            "text": "hello",
            "output_path": "voice.wav",
            "speaker": "Ryan",
            "language": "English",
            "instruct": "",
        }
        self.request_path.write_text(json.dumps(self.request_payload), encoding="utf-8")

        self.fake_torch = types.SimpleNamespace(
            float16="float16",
            float32="float32",
            cuda=types.SimpleNamespace(is_available=lambda: False),
        )
        self.sf_writes = []
        self.fake_sf = types.SimpleNamespace(
            write=lambda path, audio, sample_rate: self.sf_writes.append((path, audio, sample_rate))
        )

        _FakeQwenModelClass.reset()

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def _run_synth(self):
        with patch("local_computer_speech_backend.cli.ensure_runtime_dirs", return_value=self.paths), patch(
            "local_computer_speech_backend.cli.choose_runtime_profile",
            return_value={"device": "cpu", "dtype": "float32"},
        ), patch("local_computer_speech_backend.cli._import_qwen_model_class", return_value=_FakeQwenModelClass), patch(
            "local_computer_speech_backend.cli._normalize_generation_output",
            return_value=([0.1, -0.1], 24000),
        ), patch.dict("sys.modules", {"torch": self.fake_torch, "soundfile": self.fake_sf}):
            output = io.StringIO()
            with redirect_stdout(output):
                rc = cli.cmd_synth_request(str(self.request_path))
        payload = json.loads(output.getvalue().strip())
        return rc, payload

    def test_from_pretrained_uses_dtype_in_default_path(self) -> None:
        rc, payload = self._run_synth()

        self.assertEqual(rc, 0)
        self.assertTrue(payload["ok"])
        self.assertEqual(len(_FakeQwenModelClass.calls), 1)

        _, kwargs = _FakeQwenModelClass.calls[0]
        self.assertIn("dtype", kwargs)
        self.assertNotIn("torch_dtype", kwargs)
        self.assertEqual(kwargs["dtype"], "float32")
        self.assertTrue(kwargs["local_files_only"])
        self.assertTrue(kwargs["trust_remote_code"])

    def test_retry_with_torch_dtype_on_dtype_kwarg_compatibility_error(self) -> None:
        _FakeQwenModelClass.errors_by_call_index = {
            1: TypeError("got an unexpected keyword argument 'dtype'"),
        }

        rc, payload = self._run_synth()

        self.assertEqual(rc, 0)
        self.assertTrue(payload["ok"])
        self.assertEqual(len(_FakeQwenModelClass.calls), 2)

        _, first_kwargs = _FakeQwenModelClass.calls[0]
        _, second_kwargs = _FakeQwenModelClass.calls[1]
        self.assertIn("dtype", first_kwargs)
        self.assertNotIn("torch_dtype", first_kwargs)
        self.assertIn("torch_dtype", second_kwargs)
        self.assertNotIn("dtype", second_kwargs)

    def test_non_argument_errors_surface_as_synthesis_failure(self) -> None:
        _FakeQwenModelClass.errors_by_call_index = {
            1: RuntimeError("GPU out of memory"),
        }

        rc, payload = self._run_synth()

        self.assertEqual(rc, 1)
        self.assertFalse(payload["ok"])
        self.assertIn("Synthesis failed", payload["error"])
        self.assertIn("GPU out of memory", payload["error"])
        self.assertEqual(len(_FakeQwenModelClass.calls), 1)

    def test_tokenizer_path_signature_error_retries_without_tokenizer_path(self) -> None:
        _FakeQwenModelClass.errors_by_call_index = {
            1: TypeError("from_pretrained() got an unexpected keyword argument 'tokenizer_path'"),
        }

        rc, payload = self._run_synth()

        self.assertEqual(rc, 0)
        self.assertTrue(payload["ok"])
        self.assertEqual(len(_FakeQwenModelClass.calls), 2)

        _, first_kwargs = _FakeQwenModelClass.calls[0]
        _, second_kwargs = _FakeQwenModelClass.calls[1]
        self.assertIn("tokenizer_path", first_kwargs)
        self.assertNotIn("tokenizer_path", second_kwargs)
        self.assertIn("dtype", second_kwargs)
        self.assertNotIn("torch_dtype", second_kwargs)

    def test_tokenizer_retry_only_on_signature_style_errors(self) -> None:
        _FakeQwenModelClass.errors_by_call_index = {
            1: RuntimeError("tokenizer_path file missing on disk"),
        }

        rc, payload = self._run_synth()

        self.assertEqual(rc, 1)
        self.assertFalse(payload["ok"])
        self.assertIn("tokenizer_path file missing on disk", payload["error"])
        self.assertEqual(len(_FakeQwenModelClass.calls), 1)

    def test_bridge_json_contract_keys_remain_unchanged(self) -> None:
        rc, payload = self._run_synth()

        self.assertEqual(rc, 0)
        self.assertEqual(
            set(payload.keys()),
            {"ok", "output_path", "sample_rate", "speaker", "language", "elapsed_ms", "device", "error"},
        )


if __name__ == "__main__":
    unittest.main()
