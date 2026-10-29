#!/usr/bin/env python3
"""report_metrics.py -- Structured metrics aggregation and reporting tool for sanoTTS.

Parses JSON/JSONL metrics logs (from host reference, diagnostics, or serial smoke tests)
and produces formatted Markdown reports with statistical aggregates (median, p95, min, max, mean).
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def percentile(values: list[float], p: float) -> float:
    """Computes percentile p (0..100) using linear interpolation."""
    if not values:
        return 0.0
    s = sorted(values)
    k = (len(s) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return s[int(k)]
    d0 = s[int(f)] * (c - k)
    d1 = s[int(c)] * (k - f)
    return d0 + d1


def load_records(input_path: Path) -> list[dict]:
    """Loads records from JSON array or JSONL file."""
    if not input_path.is_file():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    text = input_path.read_text(encoding="utf-8").strip()
    if not text:
        return []

    if text.startswith("["):
        try:
            return json.loads(text)
        except json.JSONDecodeError as e:
            raise ValueError(f"Failed to parse JSON array in {input_path}: {e}")

    # Parse as JSONL
    records = []
    for line_idx, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as e:
            raise ValueError(f"Failed to parse JSONL line {line_idx} in {input_path}: {e}")

    return records


def generate_markdown_report(records: list[dict], title: str = "sanoTTS Performance & Quality Report") -> str:
    """Generates markdown report from metrics records."""
    if not records:
        return f"# {title}\n\n*No records provided.*\n"

    lines = [f"# {title}", "", f"Total Utterances Evaluated: **{len(records)}**", ""]

    # Table of individual records
    lines.append("## Utterance Evaluation Details")
    lines.append("")
    lines.append("| Index | Text / ID | Samples | Rate (Hz) | Duration (s) | Corr | RMS Ratio | RTF | Arena (B) | Status |")
    lines.append("|-------|-----------|---------|-----------|--------------|------|-----------|-----|-----------|--------|")

    corrs = []
    rms_ratios = []
    rtfs = []
    durations = []
    arenas = []

    for idx, r in enumerate(records):
        name = r.get("text") or r.get("row_id") or r.get("id") or f"row_{idx}"
        samples = r.get("samples", 0)
        rate = r.get("sample_rate") or r.get("source_rate", 22050)
        dur_s = r.get("duration_s") or (samples / rate if rate > 0 else 0.0)
        corr = r.get("corr")
        rms_ratio = r.get("rms_ratio")
        arena = r.get("arena_peak") or r.get("pl_arena_bytes") or 0
        compute_us = r.get("compute_us") or (r.get("ms", 0) * 1000.0)

        durations.append(dur_s)
        if arena > 0:
            arenas.append(arena)

        if corr is not None:
            corrs.append(corr)
            corr_str = f"{corr:.4f}"
        else:
            corr_str = "N/A"

        if rms_ratio is not None:
            rms_ratios.append(rms_ratio)
            rms_str = f"{rms_ratio:.4f}"
        else:
            rms_str = "N/A"

        if dur_s > 0 and compute_us > 0:
            rtf = (compute_us / 1000000.0) / dur_s
            rtfs.append(rtf)
            rtf_str = f"{rtf:.3f}"
        else:
            rtf_str = "N/A"

        status = r.get("status", "PASS")

        lines.append(
            f"| {idx} | {name} | {samples} | {rate} | {dur_s:.3f} | {corr_str} | {rms_str} | {rtf_str} | {arena:,} | {status} |"
        )

    lines.append("")
    lines.append("## Aggregate Summary")
    lines.append("")
    lines.append("| Metric | Min | Median (p50) | p95 | Max | Mean | Gate Threshold |")
    lines.append("|--------|-----|--------------|-----|-----|------|----------------|")

    if corrs:
        lines.append(
            f"| Correlation | {min(corrs):.4f} | {percentile(corrs, 50):.4f} | {percentile(corrs, 95):.4f} | {max(corrs):.4f} | {sum(corrs)/len(corrs):.4f} | ≥ 0.9800 |"
        )
    if rms_ratios:
        lines.append(
            f"| RMS Ratio | {min(rms_ratios):.4f} | {percentile(rms_ratios, 50):.4f} | {percentile(rms_ratios, 95):.4f} | {max(rms_ratios):.4f} | {sum(rms_ratios)/len(rms_ratios):.4f} | [0.80, 1.25] |"
        )
    if rtfs:
        lines.append(
            f"| Compute RTF | {min(rtfs):.4f} | {percentile(rtfs, 50):.4f} | {percentile(rtfs, 95):.4f} | {max(rtfs):.4f} | {sum(rtfs)/len(rtfs):.4f} | ≤ 0.8000 (M4) |"
        )
    if arenas:
        lines.append(
            f"| Working Arena (B) | {min(arenas):,} | {int(percentile(arenas, 50)):,} | {int(percentile(arenas, 95)):,} | {max(arenas):,} | {int(sum(arenas)/len(arenas)):,} | ≤ Available PSRAM |"
        )

    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Structured metrics aggregator and markdown report generator"
    )
    parser.add_argument("--input", type=Path, required=True, help="Input JSON or JSONL file")
    parser.add_argument("--output", type=Path, default=None, help="Output Markdown report file")
    parser.add_argument("--title", type=str, default="sanoTTS Evaluation Report", help="Report title")

    args = parser.parse_args()

    try:
        records = load_records(args.input)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    report_md = generate_markdown_report(records, title=args.title)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report_md, encoding="utf-8")
        print(f"Report written to: {args.output}")
    else:
        print(report_md)

    return 0


if __name__ == "__main__":
    sys.exit(main())
