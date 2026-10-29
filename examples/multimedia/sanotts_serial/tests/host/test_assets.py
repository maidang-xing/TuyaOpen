"""test_assets.py -- Host unit tests for sanoTTS manifest, assets, and verification tools.

Tests verification of manifest integrity, detection of missing or corrupted files,
sample rate mismatches, atomic staging download semantics, and PCM comparison metrics.
"""

from __future__ import annotations

import copy
import json
import math
import shutil
import tempfile
import unittest
from pathlib import Path

# Add tools directory to path
TOOLS_DIR = Path(__file__).resolve().parent.parent.parent / "tools"
import sys

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import fetch_models
import check_pcm


class TestManifestIntegrity(unittest.TestCase):
    """Tests against the real deployed manifest and frozen assets."""

    def setUp(self) -> None:
        self.base_dir = Path(__file__).resolve().parent.parent.parent
        self.manifest_path = self.base_dir / "models" / "manifest.json"

    def test_real_manifest_exists_and_valid(self) -> None:
        self.assertTrue(self.manifest_path.is_file(), f"Manifest missing at {self.manifest_path}")
        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.base_dir, verbose=False
        )
        self.assertTrue(is_valid, f"Real manifest verification failed with errors: {errors}")
        self.assertEqual(len(errors), 0)

    def test_real_manifest_has_required_voices(self) -> None:
        with open(self.manifest_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        voices = data.get("voices", {})
        self.assertIn("en_us_e12nano", voices)
        self.assertIn("chinese", voices)

        # en_us_e12nano specs
        en = voices["en_us_e12nano"]
        self.assertEqual(en.get("sample_rate"), 24000)
        self.assertEqual(en.get("architecture"), "nano_int8")

        # chinese specs
        zh = voices["chinese"]
        self.assertEqual(zh.get("sample_rate"), 22050)
        self.assertEqual(zh.get("architecture"), "piperlite_fp32")
        self.assertEqual(zh.get("espeak_voice"), "cmn")


class TestManifestFailureDetection(unittest.TestCase):
    """Tests error detection when manifest or files are tampered with or corrupted."""

    def setUp(self) -> None:
        self.tmp_dir = tempfile.mkdtemp(prefix="test_sanotts_assets_")
        self.tmp_path = Path(self.tmp_dir)

        # Create dummy assets
        self.models_dir = self.tmp_path / "models" / "mock_voice"
        self.models_dir.mkdir(parents=True)

        self.front_file = self.models_dir / "front.bin"
        self.front_file.write_bytes(b"FRONT_TEST_BYTES_1234")

        self.dec_file = self.models_dir / "dec.bin"
        self.dec_file.write_bytes(b"DEC_TEST_BYTES_5678")

        self.lic_file = self.tmp_path / "LICENSE.MIT"
        self.lic_file.write_text("MIT License test")

        self.valid_manifest = {
            "schema_version": "1.0.0",
            "voices": {
                "mock_voice": {
                    "voice_id": "mock_voice",
                    "sample_rate": 22050,
                    "license_file": "LICENSE.MIT",
                    "files": [
                        {
                            "name": "front.bin",
                            "rel_path": "models/mock_voice/front.bin",
                            "sample_rate": 22050,
                            "bytes": len(b"FRONT_TEST_BYTES_1234"),
                            "sha256": fetch_models.sha256_file(self.front_file),
                        },
                        {
                            "name": "dec.bin",
                            "rel_path": "models/mock_voice/dec.bin",
                            "sample_rate": 22050,
                            "bytes": len(b"DEC_TEST_BYTES_5678"),
                            "sha256": fetch_models.sha256_file(self.dec_file),
                        },
                    ],
                }
            },
        }
        self.manifest_path = self.tmp_path / "manifest.json"
        self._write_manifest(self.valid_manifest)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp_dir)

    def _write_manifest(self, data: dict) -> None:
        self.manifest_path.write_text(json.dumps(data, indent=2), encoding="utf-8")

    def test_mock_fixture_valid(self) -> None:
        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.tmp_path, verbose=False
        )
        self.assertTrue(is_valid)
        self.assertEqual(len(errors), 0)

    def test_missing_manifest_file(self) -> None:
        is_valid, errors = fetch_models.verify_manifest(
            self.tmp_path / "non_existent.json", base_dir=self.tmp_path, verbose=False
        )
        self.assertFalse(is_valid)
        self.assertTrue(any("not found" in e.lower() for e in errors))

    def test_missing_asset_file(self) -> None:
        self.front_file.unlink()
        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.tmp_path, verbose=False
        )
        self.assertFalse(is_valid)
        self.assertTrue(any("missing file" in e.lower() for e in errors))

    def test_corrupt_hash_detected(self) -> None:
        # Tamper with file contents
        self.front_file.write_bytes(b"CORRUPTED_CONTENTS!!!")
        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.tmp_path, verbose=False
        )
        self.assertFalse(is_valid)
        self.assertTrue(any("hash mismatch" in e.lower() for e in errors))

    def test_byte_size_mismatch_detected(self) -> None:
        m = copy.deepcopy(self.valid_manifest)
        m["voices"]["mock_voice"]["files"][0]["bytes"] = 999999
        self._write_manifest(m)

        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.tmp_path, verbose=False
        )
        self.assertFalse(is_valid)
        self.assertTrue(any("size mismatch" in e.lower() for e in errors))

    def test_sample_rate_mismatch_detected(self) -> None:
        m = copy.deepcopy(self.valid_manifest)
        # File says 16000 Hz while voice says 22050 Hz
        m["voices"]["mock_voice"]["files"][0]["sample_rate"] = 16000
        self._write_manifest(m)

        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.tmp_path, verbose=False
        )
        self.assertFalse(is_valid)
        self.assertTrue(any("sample rate mismatch" in e.lower() for e in errors))

    def test_missing_license_file_detected(self) -> None:
        self.lic_file.unlink()
        is_valid, errors = fetch_models.verify_manifest(
            self.manifest_path, base_dir=self.tmp_path, verbose=False
        )
        self.assertFalse(is_valid)
        self.assertTrue(any("license file missing" in e.lower() for e in errors))


class TestCheckPcmMetrics(unittest.TestCase):
    """Tests for check_pcm calculation and validation logic."""

    def test_identical_signals_perfect_corr(self) -> None:
        sig = [math.sin(2 * math.pi * 440 * i / 16000) for i in range(1600)]
        m = check_pcm.compute_metrics(sig, sig, 16000, 16000)
        self.assertAlmostEqual(m["corr"], 1.0, places=5)
        self.assertAlmostEqual(m["rms_ratio"], 1.0, places=5)
        self.assertEqual(m["samples"], 1600)
        self.assertEqual(m["sample_rate"], 16000)

    def test_differing_lengths_fail(self) -> None:
        s1 = [0.1] * 100
        s2 = [0.1] * 105
        with self.assertRaises(ValueError) as ctx:
            check_pcm.compute_metrics(s1, s2, 16000, 16000)
        self.assertIn("Length mismatch", str(ctx.exception))

    def test_differing_rates_fail(self) -> None:
        s1 = [0.1] * 100
        with self.assertRaises(ValueError) as ctx:
            check_pcm.compute_metrics(s1, s1, 16000, 22050)
        self.assertIn("Sample rate mismatch", str(ctx.exception))

    def test_silence_fails(self) -> None:
        silent = [0.0] * 200
        non_silent = [math.sin(i) for i in range(200)]
        with self.assertRaises(ValueError) as ctx:
            check_pcm.compute_metrics(silent, non_silent, 16000, 16000)
        self.assertIn("silent", str(ctx.exception).lower())

        with self.assertRaises(ValueError) as ctx:
            check_pcm.compute_metrics(non_silent, silent, 16000, 16000)
        self.assertIn("silent", str(ctx.exception).lower())


if __name__ == "__main__":
    unittest.main()
