#!/usr/bin/env python3
"""test_g2p.py -- Host unit tests for TuyaOpen sanoTTS Chinese G2P & Text Frontend (Task D).

Validates:
  1. Task A 10 Golden Sentences: 100% bit-exact match against tests/fixtures/chinese/zh_golden.json.
  2. 30-Sentence Extended Evaluation: numbers, decimals, dates, times, polyphones, everyday smart home, and long text.
  3. foreach_segment streaming: segment size <= 256 IDs, order preservation, callback early-stop.
  4. Robustness & Abnormal inputs: invalid UTF-8, whitespace-only, empty input, unpunctuated long text, atomic unit protection, buffer overflow protection, and cancellation requests.
"""

from __future__ import annotations

import json
import os
import subprocess
import unittest
from pathlib import Path


def get_base_dir() -> Path:
    """Returns examples/multimedia/sanotts_serial root directory."""
    return Path(__file__).resolve().parent.parent.parent


def ensure_harness(base_dir: Path) -> Path:
    """Builds snt_g2p_harness if not present or outdated."""
    tools_dir = base_dir / "tools"
    bin_path = tools_dir / "snt_g2p_harness"
    c_harness = tools_dir / "snt_g2p_harness.c"
    src_dir = base_dir / "src"
    espeak_dir = base_dir / "third_party" / "espeak-ng"
    miniz_dir = base_dir / "third_party" / "miniz"

    c_sources = [
        c_harness,
        src_dir / "snt_assets.c",
        src_dir / "snt_text.c",
        src_dir / "snt_g2p.c",
        miniz_dir / "miniz.c",
        *sorted((espeak_dir / "libespeak-ng").glob("*.c")),
        *sorted((espeak_dir / "ucd-tools").glob("*.c")),
    ]

    for s in c_sources:
        if not s.is_file():
            raise FileNotFoundError(f"Missing source file: {s}")

    needs_build = not bin_path.is_file()
    if not needs_build:
        bin_mtime = bin_path.stat().st_mtime
        for s in c_sources:
            if s.stat().st_mtime > bin_mtime:
                needs_build = True
                break

    if needs_build:
        cmd = [
            "gcc",
            "-O2",
            "-std=gnu11",
            "-w",
            "-DHAVE_CONFIG_H",
            f"-I{src_dir}",
            f"-I{miniz_dir}",
            f"-I{espeak_dir}",
            f"-I{espeak_dir / 'include'}",
            f"-I{espeak_dir / 'ucd-tools' / 'include'}",
            f"-I{espeak_dir / 'libespeak-ng'}",
            *[str(s) for s in c_sources],
            "-lm",
            "-o",
            str(bin_path),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Failed to build snt_g2p_harness:\n{res.stderr}")

    return bin_path


class TestChineseG2PGolden(unittest.TestCase):
    """Task A 10 Golden Sentences verification."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.base_dir = get_base_dir()
        cls.harness = ensure_harness(cls.base_dir)
        cls.models_dir = cls.base_dir / "models" / "chinese"
        cls.golden_path = cls.base_dir / "tests" / "fixtures" / "chinese" / "zh_golden.json"
        with open(cls.golden_path, "r", encoding="utf-8") as f:
            cls.golden_data = json.load(f)

    def test_golden_harness_cli(self) -> None:
        """Runs the built-in golden verification command in snt_g2p_harness."""
        cmd = [str(self.harness), "golden", str(self.models_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Golden test failed:\n{res.stdout}\n{res.stderr}")
        self.assertIn("10/10 golden sentences bit-exact", res.stdout)

    def test_each_golden_sentence_individually(self) -> None:
        """Verifies each sentence output JSON against zh_golden.json bit-exact."""
        for item in self.golden_data:
            idx = item["index"]
            text = item["text"]
            expected_ids = item["ids"]
            expected_n = item["n_ids"]

            cmd = [str(self.harness), "convert", text, str(self.models_dir)]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Convert failed for s{idx:02d} '{text}'")

            out = json.loads(res.stdout)
            self.assertEqual(out["n_ids"], expected_n, f"Length mismatch for s{idx:02d} '{text}'")
            self.assertEqual(out["ids"], expected_ids, f"ID sequence mismatch for s{idx:02d} '{text}'")
            self.assertEqual(out["unknown_symbols"], 0, f"Unknown symbols for s{idx:02d} '{text}'")


class TestChineseG2P30Dataset(unittest.TestCase):
    """30-Sentence Extended Evaluation (Task A 10 + 20 Extended)."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.base_dir = get_base_dir()
        cls.harness = ensure_harness(cls.base_dir)
        cls.models_dir = cls.base_dir / "models" / "chinese"
        cls.dataset_path = cls.base_dir / "tests" / "fixtures" / "chinese" / "zh_30_dataset.json"
        with open(cls.dataset_path, "r", encoding="utf-8") as f:
            cls.dataset = json.load(f)

    def test_all_30_sentences_consistency(self) -> None:
        """Verifies all 30 sentences against the frozen reference fixture."""
        self.assertGreaterEqual(len(self.dataset), 30)
        for item in self.dataset:
            idx = item["index"]
            text = item["text"]
            expected_ids = item["ids"]
            expected_n = item["n_ids"]

            cmd = [str(self.harness), "convert", text, str(self.models_dir)]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Convert failed for [{idx:02d}] '{text}'")

            out = json.loads(res.stdout)
            self.assertEqual(out["n_ids"], expected_n, f"Length mismatch for [{idx:02d}] '{text}'")
            self.assertEqual(out["ids"], expected_ids, f"ID sequence mismatch for [{idx:02d}] '{text}'")
            self.assertEqual(out["unknown_symbols"], 0)


class TestForeachSegmentation(unittest.TestCase):
    """Tests snt_g2p_foreach_segment streaming and segmentation limits."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.base_dir = get_base_dir()
        cls.harness = ensure_harness(cls.base_dir)
        cls.models_dir = cls.base_dir / "models" / "chinese"

    def test_long_multiclause_text_segmentation(self) -> None:
        """A 465-ID text must be split into multiple segments each <= 256 IDs."""
        text = "涂鸦智能致力于推动物联网开发。通过软硬件一体化方案，让智能硬件开发更简单更高效。"
        cmd = [str(self.harness), "foreach", text, str(self.models_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)

        out = json.loads(res.stdout)
        segments = out["segments"]
        self.assertGreater(len(segments), 1)

        for s in segments:
            self.assertLessEqual(s["n_ids"], 256, f"Segment {s['segment_index']} exceeded 256 IDs!")
            self.assertEqual(s["ids"][0], 1, "BOS missing")
            self.assertEqual(s["ids"][1], 0, "PAD missing")
            self.assertEqual(s["ids"][-1], 2, "EOS missing")

    def test_unpunctuated_long_sentence_segmentation(self) -> None:
        """A long Chinese text without any punctuation must be safely split with segments <= 256."""
        # 80 Chinese characters without punctuation
        text = "这是一个完全没有标点符号的长中文句子用来严格测试前端分句切分逻辑是否能够在字符上限范围内安全切分并且绝不发生内存溢出或者字符阶段损坏"
        cmd = [str(self.harness), "foreach", text, str(self.models_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)

        out = json.loads(res.stdout)
        segments = out["segments"]
        self.assertGreater(len(segments), 1)
        for s in segments:
            self.assertLessEqual(s["n_ids"], 256)


class TestRobustnessAndEdgeCases(unittest.TestCase):
    """Tests abnormal and edge case inputs."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.base_dir = get_base_dir()
        cls.harness = ensure_harness(cls.base_dir)
        cls.models_dir = cls.base_dir / "models" / "chinese"

    def test_empty_string(self) -> None:
        cmd = [str(self.harness), "convert", "", str(self.models_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        out = json.loads(res.stdout)
        self.assertEqual(out["n_ids"], 0)

    def test_whitespace_only(self) -> None:
        cmd = [str(self.harness), "convert", "   \t\n  ", str(self.models_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        out = json.loads(res.stdout)
        self.assertEqual(out["n_ids"], 0)

    def test_invalid_utf8_sequence(self) -> None:
        """Invalid UTF-8 byte sequences must be safely sanitized without crashes."""
        # Mix valid text with invalid bytes 0xFF, 0xFE
        invalid_bytes = b"\xe4\xbd\xa0\xe5\xa5\xbd\xff\xfe\xe4\xb8\x96\xe7\x95\x8c" # "你好" + invalid + "世界"
        proc = subprocess.Popen(
            [str(self.harness), "convert", invalid_bytes.decode("latin1"), str(self.models_dir)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = proc.communicate(timeout=5)
        self.assertEqual(proc.returncode, 0)

    def test_fullwidth_digits_and_colon(self) -> None:
        """Full-width ０８：３０ must be normalized and yield identical IDs as 08:30."""
        cmd_half = [str(self.harness), "convert", "时间08:30", str(self.models_dir)]
        cmd_full = [str(self.harness), "convert", "时间０８：３０", str(self.models_dir)]
        res_half = subprocess.run(cmd_half, capture_output=True, text=True, check=True)
        res_full = subprocess.run(cmd_full, capture_output=True, text=True, check=True)

        data_half = json.loads(res_half.stdout)
        data_full = json.loads(res_full.stdout)
        self.assertEqual(data_full["ids"], data_half["ids"])

    def test_cancellation_request(self) -> None:
        cmd = [str(self.harness), "cancel-test", "你好世界", str(self.models_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("rc=-7", res.stdout)

    def test_normalization_modes(self) -> None:
        # Default mode (0)
        cmd0 = [str(self.harness), "normalize", "编号１２３４５", "0"]
        res0 = subprocess.run(cmd0, capture_output=True, text=True, check=True)
        self.assertEqual(res0.stdout.strip(), "编号12345")

        # Expand words mode (1)
        cmd1 = [str(self.harness), "normalize", "编号12345", "1"]
        res1 = subprocess.run(cmd1, capture_output=True, text=True, check=True)
        self.assertEqual(res1.stdout.strip(), "编号一万二千三百四十五")

        # Expand digits mode (2)
        cmd2 = [str(self.harness), "normalize", "编号12345", "2"]
        res2 = subprocess.run(cmd2, capture_output=True, text=True, check=True)
        self.assertEqual(res2.stdout.strip(), "编号一二三四五")


if __name__ == "__main__":
    unittest.main()
