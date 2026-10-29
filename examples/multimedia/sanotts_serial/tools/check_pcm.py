#!/usr/bin/env python3
"""check_pcm.py -- Strict waveform audio comparison tool for sanoTTS.

Compares reference and candidate waveforms at identical sample rates and lengths.
Reports:
  - Pearson correlation (corr)
  - RMS ratio (RMS(cand) / RMS(ref))
  - Max absolute sample error
  - Silence / zero-variance detection

Zero-variance, differing length, or mismatched sample rates fail immediately.
Does NOT perform automatic alignment or trimming.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import wave
from pathlib import Path


def load_audio(
    path: Path,
    raw_rate: int | None = None,
    raw_fmt: str = "float32"
) -> tuple[list[float], int]:
    """Loads audio from WAV or raw binary float32/int16 file.

    Returns:
        (samples_as_floats, sample_rate)
    """
    if not path.is_file():
        raise FileNotFoundError(f"Audio file not found: {path}")

    # Check if WAV by magic header
    with open(path, "rb") as f:
        head = f.read(12)

    if len(head) >= 12 and head[:4] == b"RIFF" and head[8:12] == b"WAVE":
        with wave.open(str(path), "rb") as wf:
            n_ch = wf.getnchannels()
            sampwidth = wf.getsampwidth()
            rate = wf.getframerate()
            n_frames = wf.getnframes()
            frames = wf.readframes(n_frames)

            if n_ch != 1:
                raise ValueError(f"Only mono audio supported, got {n_ch} channels in {path}")

            if sampwidth == 2:  # 16-bit PCM
                count = len(frames) // 2
                ints = struct.unpack(f"<{count}h", frames)
                samples = [s / 32768.0 for s in ints]
            elif sampwidth == 4:  # 32-bit float or int32
                count = len(frames) // 4
                try:
                    samples = list(struct.unpack(f"<{count}f", frames))
                except struct.error:
                    ints = struct.unpack(f"<{count}i", frames)
                    samples = [s / 2147483648.0 for s in ints]
            else:
                raise ValueError(f"Unsupported sample width: {sampwidth} bytes in {path}")
            return samples, rate

    # Raw binary file
    if raw_rate is None:
        raise ValueError(f"Raw binary file {path} requires --rate parameter")

    raw_data = path.read_bytes()
    if raw_fmt.lower() in ("float32", "f32"):
        n = len(raw_data) // 4
        samples = list(struct.unpack(f"<{n}f", raw_data[: n * 4]))
    elif raw_fmt.lower() in ("int16", "s16", "pcm16"):
        n = len(raw_data) // 2
        ints = struct.unpack(f"<{n}h", raw_data[: n * 2])
        samples = [s / 32768.0 for s in ints]
    else:
        raise ValueError(f"Unsupported raw format: {raw_fmt}")

    return samples, raw_rate


def compute_metrics(
    ref: list[float],
    cand: list[float],
    rate_ref: int,
    rate_cand: int,
    silence_threshold: float = 1e-6
) -> dict:
    """Computes comparison metrics between reference and candidate."""
    if rate_ref != rate_cand:
        raise ValueError(
            f"Sample rate mismatch: reference has {rate_ref} Hz, candidate has {rate_cand} Hz"
        )

    if len(ref) != len(cand):
        raise ValueError(
            f"Length mismatch: reference has {len(ref)} samples, candidate has {len(cand)} samples"
        )

    n = len(ref)
    if n == 0:
        raise ValueError("Audio files are empty (0 samples)")

    sa = 0.0
    sb = 0.0
    saa = 0.0
    sbb = 0.0
    sab = 0.0
    max_err = 0.0
    peak_ref = 0.0
    peak_cand = 0.0

    for i in range(n):
        a = ref[i]
        b = cand[i]

        if math.isnan(a) or math.isinf(a):
            raise ValueError(f"Reference audio contains NaN/Inf at sample {i}")
        if math.isnan(b) or math.isinf(b):
            raise ValueError(f"Candidate audio contains NaN/Inf at sample {i}")

        err = abs(b - a)
        if err > max_err:
            max_err = err
        if abs(a) > peak_ref:
            peak_ref = abs(a)
        if abs(b) > peak_cand:
            peak_cand = abs(b)

        sa += a
        sb += b
        saa += a * a
        sbb += b * b
        sab += a * b

    # Variances
    var_a = (saa - (sa * sa) / n) / n
    var_b = (sbb - (sb * sb) / n) / n

    rms_ref = math.sqrt(saa / n)
    rms_cand = math.sqrt(sbb / n)

    is_silent_ref = var_a <= silence_threshold or peak_ref <= silence_threshold
    is_silent_cand = var_b <= silence_threshold or peak_cand <= silence_threshold

    if is_silent_ref:
        raise ValueError(f"Reference audio is silent or zero-variance (var={var_a:.2e}, peak={peak_ref:.2e})")
    if is_silent_cand:
        raise ValueError(f"Candidate audio is silent or zero-variance (var={var_b:.2e}, peak={peak_cand:.2e})")

    denom = math.sqrt(var_a * var_b)
    if denom <= 0.0:
        corr = 0.0
    else:
        cov = (sab - (sa * sb) / n) / n
        corr = cov / denom

    rms_ratio = (rms_cand / rms_ref) if rms_ref > 0 else 0.0

    return {
        "samples": n,
        "sample_rate": rate_ref,
        "duration_s": round(n / rate_ref, 4),
        "corr": corr,
        "rms_ref": rms_ref,
        "rms_cand": rms_cand,
        "rms_ratio": rms_ratio,
        "max_err": max_err,
        "peak_ref": peak_ref,
        "peak_cand": peak_cand,
        "is_silent_ref": is_silent_ref,
        "is_silent_cand": is_silent_cand,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Strictly compare reference and candidate audio PCM/WAV"
    )
    parser.add_argument("reference", type=Path, help="Reference audio (WAV or raw)")
    parser.add_argument("candidate", type=Path, help="Candidate audio (WAV or raw)")
    parser.add_argument("--rate", type=int, default=None, help="Sample rate for raw PCM")
    parser.add_argument("--format", choices=["float32", "int16"], default="float32", help="Raw format")
    parser.add_argument("--corr-min", type=float, default=None, help="Minimum correlation threshold")
    parser.add_argument("--rms-ratio-min", type=float, default=None, help="Minimum RMS ratio threshold")
    parser.add_argument("--rms-ratio-max", type=float, default=None, help="Maximum RMS ratio threshold")
    parser.add_argument("--max-err", type=float, default=None, help="Maximum allowed sample error")
    parser.add_argument("--json", action="store_true", help="Output JSON results")

    args = parser.parse_args()

    try:
        ref_samples, ref_rate = load_audio(args.reference, raw_rate=args.rate, raw_fmt=args.format)
        cand_samples, cand_rate = load_audio(args.candidate, raw_rate=args.rate, raw_fmt=args.format)
        metrics = compute_metrics(ref_samples, cand_samples, ref_rate, cand_rate)
    except Exception as e:
        if args.json:
            print(json.dumps({"status": "ERROR", "error": str(e)}, indent=2))
        else:
            print(f"FAILED: {e}", file=sys.stderr)
        return 1

    # Check thresholds if provided
    passed = True
    reasons = []

    if args.corr_min is not None and metrics["corr"] < args.corr_min:
        passed = False
        reasons.append(f"corr {metrics['corr']:.6f} < threshold {args.corr_min:.6f}")

    if args.rms_ratio_min is not None and metrics["rms_ratio"] < args.rms_ratio_min:
        passed = False
        reasons.append(f"rms_ratio {metrics['rms_ratio']:.6f} < min {args.rms_ratio_min:.6f}")

    if args.rms_ratio_max is not None and metrics["rms_ratio"] > args.rms_ratio_max:
        passed = False
        reasons.append(f"rms_ratio {metrics['rms_ratio']:.6f} > max {args.rms_ratio_max:.6f}")

    if args.max_err is not None and metrics["max_err"] > args.max_err:
        passed = False
        reasons.append(f"max_err {metrics['max_err']:.6f} > max {args.max_err:.6f}")

    metrics["status"] = "PASS" if passed else "FAIL"
    metrics["failures"] = reasons

    if args.json:
        print(json.dumps(metrics, indent=2))
    else:
        print("=== sanoTTS PCM Audio Comparison ===")
        print(f"Reference:   {args.reference}")
        print(f"Candidate:   {args.candidate}")
        print(f"Rate:        {metrics['sample_rate']} Hz")
        print(f"Samples:     {metrics['samples']} ({metrics['duration_s']} s)")
        print(f"Correlation: {metrics['corr']:.6f}")
        print(f"RMS Ratio:   {metrics['rms_ratio']:.6f} (ref={metrics['rms_ref']:.4f}, cand={metrics['rms_cand']:.4f})")
        print(f"Max Error:   {metrics['max_err']:.6e}")
        print(f"Peak Level:  ref={metrics['peak_ref']:.4f}, cand={metrics['peak_cand']:.4f}")
        print(f"Status:      {metrics['status']}")
        if not passed:
            for r in reasons:
                print(f"  * {r}", file=sys.stderr)

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
