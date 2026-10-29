"""test_chunk.py -- Host unit tests for Task C (snt_engine and snt_chinese_chunk).

Validates:
  1. Task C1: English nano INT8 golden gate (8 rows, corr > 0.98, 0.80 < RMS ratio < 1.25, 24kHz)
  2. Task C2: Chinese FP32 chunked decoding matrix (blocks 8/16/32, context 8/11/16)
  3. Tail non-integer chunk handling
  4. Short sentence handling (<= 30 frames)
  5. Boundary smoothness and continuity
  6. Engine cancellation and lifecycle management
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent.parent


def get_harness_path() -> Path:
    tools_dir = BASE_DIR / "tools"
    bin_path = tools_dir / "snt_chunk_harness"
    c_src = tools_dir / "snt_chunk_harness.c"

    src_dir = BASE_DIR / "src"
    tp_dir = BASE_DIR / "third_party" / "sanotts"
    tp_src = tp_dir / "src"
    tp_inc = tp_dir / "include"
    nano_model_inc = tp_dir / "models" / "en_us_e12nano"

    sources = [
        c_src,
        src_dir / "snt_engine.c",
        src_dir / "snt_chinese_chunk.c",
        src_dir / "snt_port_t5.c",
        tp_src / "snt_nano.c",
        tp_src / "snt_kernels_ref.c",
        tp_src / "snt_front_f32.c",
        tp_src / "snt_piperlite.c",
    ]

    for s in sources:
        if not s.is_file():
            raise FileNotFoundError(f"Missing required C source: {s}")

    needs_build = not bin_path.is_file()
    if not needs_build:
        bin_mtime = bin_path.stat().st_mtime
        for s in sources:
            if s.stat().st_mtime > bin_mtime:
                needs_build = True
                break

    if needs_build:
        cmd = [
            "gcc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c99",
            f"-I{src_dir}",
            f"-I{tp_inc}",
            f"-I{nano_model_inc}",
            *[str(s) for s in sources],
            "-lm",
            "-o",
            str(bin_path),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Harness compilation failed:\n{res.stderr}")

    return bin_path


class TestTaskCEngineAndChunking(unittest.TestCase):
    """Test suite for sanoTTS Task C implementation."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.harness = get_harness_path()
        cls.fixtures_en = BASE_DIR / "tests" / "fixtures" / "en_us_e12nano"
        cls.fixtures_zh = BASE_DIR / "tests" / "fixtures" / "chinese"
        cls.models_zh = BASE_DIR / "models" / "chinese"

        cls.assertTrue(cls.harness.is_file(), "Harness binary not built")
        cls.assertTrue(cls.fixtures_en.is_dir(), "English fixtures missing")
        cls.assertTrue(cls.fixtures_zh.is_dir(), "Chinese fixtures missing")
        cls.assertTrue(cls.models_zh.is_dir(), "Chinese models missing")

    def test_c1_en_nano_golden_gate(self) -> None:
        """Verify Task C1 English nano 8-row golden gate through snt_engine."""
        cmd = [str(self.harness), "en-golden", str(self.fixtures_en), "8"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"en-golden failed:\n{res.stderr}\n{res.stdout}")
        self.assertIn("RESULT: PASS", res.stdout)

        # Parse rows and assert metrics
        for line in res.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 8 and parts[0].startswith("00000"):
                row_id = parts[0]
                corr = float(parts[3])
                rms_ratio = float(parts[4])
                self.assertGreater(corr, 0.98, f"{row_id} corr={corr} <= 0.98")
                self.assertGreater(rms_ratio, 0.80, f"{row_id} rms_ratio={rms_ratio} <= 0.80")
                self.assertLess(rms_ratio, 1.25, f"{row_id} rms_ratio={rms_ratio} >= 1.25")
                self.assertEqual(parts[7], "PASS", f"{row_id} did not pass gate")

    def test_c2_zh_chunking_matrix_8_16_32(self) -> None:
        """Verify Task C2 Chinese chunked decoding across block sizes 8, 16, 32."""
        cmd = [str(self.harness), "zh-matrix", str(self.models_zh), str(self.fixtures_zh), "10"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"zh-matrix failed:\n{res.stderr}\n{res.stdout}")
        self.assertIn("RESULT: PASS", res.stdout)

        # Verify all 50 matrix lines pass thresholds: corr >= 0.995, 0.98 <= rms_ratio <= 1.02
        passed_rows = 0
        for line in res.stdout.splitlines():
            if "PASS" in line and not line.startswith("text"):
                parts = line.split()
                # line format: text blk ctx frames corr rms_ratio max_err bnd_jump PASS
                if len(parts) >= 9:
                    blk = int(parts[1])
                    ctx = int(parts[2])
                    corr = float(parts[4])
                    rms_ratio = float(parts[5])
                    max_err = float(parts[6])
                    bnd_jump = float(parts[7])

                    self.assertIn(blk, (8, 16, 32))
                    self.assertIn(ctx, (8, 11, 16))
                    self.assertGreaterEqual(corr, 0.995, f"Low corr {corr} for {parts[0]}")
                    self.assertGreaterEqual(rms_ratio, 0.98, f"Low rms_ratio {rms_ratio} for {parts[0]}")
                    self.assertLessEqual(rms_ratio, 1.02, f"High rms_ratio {rms_ratio} for {parts[0]}")

                    # If context >= 11 (within derived RF), output must be bit-exact (max_err == 0.0)
                    if ctx >= 11:
                        self.assertEqual(max_err, 0.0, f"Expected bit-exact for ctx={ctx}, got max_err={max_err}")
                        self.assertEqual(bnd_jump, 0.0, f"Expected zero bnd_jump for ctx={ctx}, got {bnd_jump}")
                    else:
                        self.assertLess(bnd_jump, 0.001, f"Boundary discontinuity {bnd_jump} too large")

                    passed_rows += 1

        self.assertEqual(passed_rows, 50, f"Expected 50 matrix test rows, got {passed_rows}")

    def test_c2_short_sentence_handling(self) -> None:
        """Verify short sentence (<= 30 frames) like '你好' decodes correctly."""
        cmd = [str(self.harness), "zh-matrix", str(self.models_zh), str(self.fixtures_zh), "1"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("你好", res.stdout)
        self.assertIn("RESULT: PASS", res.stdout)

    def test_c2_tail_non_integer_chunks(self) -> None:
        """Verify sentences where total_frames is not a multiple of center_frames."""
        # e.g. s01 "欢迎使用本地语音" has 118 frames: 118 % 16 = 6 != 0
        cmd = [str(self.harness), "zh-matrix", str(self.models_zh), str(self.fixtures_zh), "2"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("欢迎使用本地语音", res.stdout)
        self.assertIn("RESULT: PASS", res.stdout)

    def test_c2_boundary_smoothness(self) -> None:
        """Verify chunk boundary continuity: no jump discontinuity between chunks."""
        cmd = [str(self.harness), "zh-matrix", str(self.models_zh), str(self.fixtures_zh), "3"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        for line in res.stdout.splitlines():
            if "PASS" in line and not line.startswith("text"):
                parts = line.split()
                if len(parts) >= 9:
                    bnd_jump = float(parts[7])
                    self.assertLess(bnd_jump, 0.001)

    def test_engine_cancellation_and_lifecycle(self) -> None:
        """Verify snt_engine cancellation and lifecycle deinit without crash."""
        cmd = [str(self.harness), "cancel-test", str(self.models_zh)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"cancel-test failed:\n{res.stderr}\n{res.stdout}")
        self.assertIn("Cancellation test completed.", res.stdout)


if __name__ == "__main__":
    unittest.main()
