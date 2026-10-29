#!/usr/bin/env python3
"""host_reference.py -- Host C reference runner for sanoTTS.

Builds and runs the portable C reference harness for:
  - English nano INT8 golden gate:
      python host_reference.py --voice en-nano --suite golden
  - Chinese FP32 piperlite smoke/golden suite:
      python host_reference.py --voice zh --suite smoke
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def get_base_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def ensure_harness(base_dir: Path, force_rebuild: bool = False) -> Path:
    """Compiles the C host reference harness if not already built or outdated."""
    tools_dir = base_dir / "tools"
    bin_path = tools_dir / "snt_host_harness"
    c_harness = tools_dir / "snt_host_harness.c"

    src_dir = base_dir / "third_party" / "sanotts" / "src"
    inc_dir = base_dir / "third_party" / "sanotts" / "include"
    models_inc = base_dir / "third_party" / "sanotts" / "models" / "en_us_e12nano"
    port_c = base_dir / "third_party" / "sanotts" / "ports" / "host" / "snt_port_host.c"

    c_sources = [
        c_harness,
        src_dir / "snt_nano.c",
        src_dir / "snt_kernels_ref.c",
        port_c,
        src_dir / "snt_front_f32.c",
        src_dir / "snt_piperlite.c",
    ]

    for src in c_sources:
        if not src.is_file():
            raise FileNotFoundError(f"Required C source not found: {src}")

    needs_build = force_rebuild or (not bin_path.is_file())
    if not needs_build:
        bin_mtime = bin_path.stat().st_mtime
        for src in c_sources:
            if src.stat().st_mtime > bin_mtime:
                needs_build = True
                break

    if needs_build:
        cmd = [
            "gcc",
            "-O2",
            "-std=c99",
            "-Wall",
            f"-I{inc_dir}",
            f"-I{models_inc}",
            *[str(s) for s in c_sources],
            "-lm",
            "-o",
            str(bin_path),
        ]
        print(f"Building host reference harness: {' '.join(cmd)}")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Compilation failed:\n{res.stderr}", file=sys.stderr)
            raise RuntimeError("Failed to build host reference harness")

    return bin_path


def run_en_nano(harness: Path, base_dir: Path, suite: str) -> int:
    fixtures_dir = base_dir / "tests" / "fixtures" / "en_us_e12nano"
    if not fixtures_dir.is_dir():
        print(f"ERROR: Fixtures directory missing: {fixtures_dir}", file=sys.stderr)
        return 1

    max_rows = "1" if suite == "smoke" else "8"
    cmd = [str(harness), "en-nano-golden", str(fixtures_dir), max_rows]
    print(f"Running en-nano ({suite}): {' '.join(cmd)}")
    res = subprocess.run(cmd)
    return res.returncode


def run_zh(harness: Path, base_dir: Path, suite: str) -> int:
    models_dir = base_dir / "models" / "chinese"
    fixtures_dir = base_dir / "tests" / "fixtures" / "chinese"

    if not models_dir.is_dir():
        print(f"ERROR: Models directory missing: {models_dir}", file=sys.stderr)
        return 1

    count = "2" if suite == "smoke" else "10"
    f_arg = str(fixtures_dir) if fixtures_dir.is_dir() else ""
    cmd = [str(harness), "zh-suite", str(models_dir), f_arg, count]
    print(f"Running zh ({suite}): {' '.join(cmd)}")
    res = subprocess.run(cmd)
    return res.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Host C reference runner for sanoTTS"
    )
    parser.add_argument(
        "--voice",
        choices=["en-nano", "en_us_e12nano", "zh", "chinese"],
        required=True,
        help="Voice lineage to test"
    )
    parser.add_argument(
        "--suite",
        choices=["golden", "smoke", "all"],
        default="golden",
        help="Test suite level"
    )
    parser.add_argument(
        "--base-dir",
        type=Path,
        default=None,
        help="Base directory (examples/multimedia/sanotts_serial)"
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="Force recompilation of C reference harness"
    )

    args = parser.parse_args()
    base_dir = args.base_dir or get_base_dir()

    try:
        harness = ensure_harness(base_dir, force_rebuild=args.rebuild)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    voice = "en-nano" if args.voice in ("en-nano", "en_us_e12nano") else "zh"

    if voice == "en-nano":
        return run_en_nano(harness, base_dir, args.suite)
    else:
        return run_zh(harness, base_dir, args.suite)


if __name__ == "__main__":
    sys.exit(main())
