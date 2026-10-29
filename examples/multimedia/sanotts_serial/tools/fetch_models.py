#!/usr/bin/env python3
"""fetch_models.py -- Download and verify frozen sanoTTS models and assets.

Validates model manifests and fixtures for TuyaOpen T5AI-Core sanoTTS against
upstream commit hashes. Supports strict --verify-only checking and atomic staging downloads.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
from pathlib import Path


def get_default_base_dir() -> Path:
    """Finds the examples/multimedia/sanotts_serial root directory."""
    script_dir = Path(__file__).resolve().parent
    return script_dir.parent


def sha256_file(filepath: Path) -> str:
    """Computes the SHA-256 hex digest of a file in streaming chunks."""
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def verify_manifest(
    manifest_path: Path,
    base_dir: Path | None = None,
    verbose: bool = True
) -> tuple[bool, list[str]]:
    """Strict verification of all assets declared in manifest.json.

    Returns:
        (is_valid, list_of_error_messages)
    """
    errors: list[str] = []
    if not manifest_path.is_file():
        errors.append(f"Manifest file not found: {manifest_path}")
        return False, errors

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        errors.append(f"Invalid JSON in manifest {manifest_path}: {e}")
        return False, errors

    if base_dir is None:
        base_dir = manifest_path.parent.parent

    voices = data.get("voices", {})
    if not voices:
        errors.append("Manifest contains no 'voices' entries")

    total_files = 0
    checked_files = 0

    for voice_id, voice_info in voices.items():
        v_sr = voice_info.get("sample_rate")
        if not v_sr or not isinstance(v_sr, int) or v_sr <= 0:
            errors.append(f"Voice {voice_id} has invalid sample_rate: {v_sr}")

        license_file = voice_info.get("license_file")
        if license_file:
            lic_path = base_dir / license_file
            if not lic_path.is_file():
                errors.append(f"Voice {voice_id} license file missing: {lic_path}")

        files = voice_info.get("files", [])
        if not files:
            errors.append(f"Voice {voice_id} has no files declared")

        for f_entry in files:
            total_files += 1
            rel_path = f_entry.get("rel_path")
            expected_sha = f_entry.get("sha256")
            expected_bytes = f_entry.get("bytes")
            f_sr = f_entry.get("sample_rate")

            if not rel_path:
                errors.append(f"Voice {voice_id} file entry missing rel_path")
                continue

            full_path = base_dir / rel_path

            if not full_path.is_file():
                errors.append(f"Missing file: {rel_path} ({full_path})")
                continue

            # Check sample rate consistency
            if f_sr is not None and v_sr is not None and f_sr != v_sr:
                errors.append(
                    f"Sample rate mismatch for {rel_path}: file specifies {f_sr} Hz, voice expects {v_sr} Hz"
                )

            # Check byte size
            actual_bytes = full_path.stat().st_size
            if expected_bytes is not None and actual_bytes != expected_bytes:
                errors.append(
                    f"Size mismatch for {rel_path}: got {actual_bytes} bytes, expected {expected_bytes} bytes"
                )

            # Check SHA-256
            if not expected_sha:
                errors.append(f"Missing expected SHA-256 in manifest for {rel_path}")
                continue

            actual_sha = sha256_file(full_path)
            if actual_sha.lower() != expected_sha.lower():
                errors.append(
                    f"Hash mismatch for {rel_path}:\n"
                    f"  got:      {actual_sha}\n"
                    f"  expected: {expected_sha}"
                )
            else:
                checked_files += 1
                if verbose:
                    print(f"[OK] {rel_path:<48} ({actual_bytes:>7} B) SHA-256 verified")

    is_valid = len(errors) == 0
    return is_valid, errors


def download_file_atomic(
    url: str,
    target_path: Path,
    expected_sha256: str,
    expected_bytes: int | None = None
) -> None:
    """Downloads a file to a staging path and atomically renames on success."""
    target_path.parent.mkdir(parents=True, exist_ok=True)
    staging_path = target_path.with_name(f"{target_path.name}.staging.{os.getpid()}")

    try:
        print(f"Fetching {url} -> {staging_path.name}...")
        # Check if local file or cache URL
        if url.startswith("local:"):
            raise ValueError(f"Cannot download local generated artifact from network: {url}")

        req = urllib.request.Request(url, headers={"User-Agent": "TuyaOpen-SanoTTS-Fetcher/1.0"})
        with urllib.request.urlopen(req) as resp, open(staging_path, "wb") as out_f:
            shutil.copyfileobj(resp, out_f)

        actual_bytes = staging_path.stat().st_size
        if expected_bytes is not None and actual_bytes != expected_bytes:
            raise ValueError(
                f"Downloaded size mismatch: expected {expected_bytes}, got {actual_bytes}"
            )

        actual_sha = sha256_file(staging_path)
        if actual_sha.lower() != expected_sha.lower():
            raise ValueError(
                f"SHA-256 mismatch for downloaded file:\n"
                f"  got:      {actual_sha}\n"
                f"  expected: {expected_sha}"
            )

        # Atomic replacement
        staging_path.replace(target_path)
        print(f"[FETCHED] {target_path} ({actual_bytes} bytes)")
    finally:
        if staging_path.exists():
            staging_path.unlink()


def fetch_all_missing(
    manifest_path: Path,
    base_dir: Path,
    cache_dir: Path | None = None
) -> bool:
    """Fetches missing files from cache or source URLs."""
    with open(manifest_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    all_success = True
    for voice_id, voice_info in data.get("voices", {}).items():
        for f_entry in voice_info.get("files", []):
            rel_path = f_entry["rel_path"]
            target_path = base_dir / rel_path
            expected_sha = f_entry["sha256"]
            expected_bytes = f_entry.get("bytes")
            url = f_entry.get("source_url", "")

            if target_path.is_file():
                if sha256_file(target_path).lower() == expected_sha.lower():
                    continue

            # Try to copy from local cache first if available
            copied_from_cache = False
            if cache_dir and cache_dir.is_dir():
                # Check candidate paths in cache
                filename = target_path.name
                for candidate in cache_dir.rglob(filename):
                    if candidate.is_file() and sha256_file(candidate).lower() == expected_sha.lower():
                        target_path.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(candidate, target_path)
                        print(f"[COPIED FROM CACHE] {candidate} -> {target_path}")
                        copied_from_cache = True
                        break

            if not copied_from_cache:
                if not url or url.startswith("local:"):
                    print(f"ERROR: Cannot fetch {rel_path} without valid URL or cache", file=sys.stderr)
                    all_success = False
                    continue
                try:
                    download_file_atomic(url, target_path, expected_sha, expected_bytes)
                except Exception as e:
                    print(f"ERROR: Failed to download {rel_path}: {e}", file=sys.stderr)
                    all_success = False

    return all_success


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify or fetch sanoTTS frozen models and assets"
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Path to manifest.json"
    )
    parser.add_argument(
        "--base-dir",
        type=Path,
        default=None,
        help="Base directory for relative paths (defaults to examples/multimedia/sanotts_serial)"
    )
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="Verify existing assets only; exit non-zero on any error or missing asset"
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=None,
        help="Optional local cache directory containing sanoTTS upstream"
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress per-file OK messages"
    )

    args = parser.parse_args()

    base_dir = args.base_dir or get_default_base_dir()
    manifest_path = args.manifest or (base_dir / "models" / "manifest.json")

    print(f"TuyaOpen sanoTTS Asset Manager")
    print(f"Base dir: {base_dir}")
    print(f"Manifest: {manifest_path}")

    if args.verify_only:
        valid, errors = verify_manifest(manifest_path, base_dir=base_dir, verbose=not args.quiet)
        if not valid:
            print("\nVERIFICATION FAILED with errors:", file=sys.stderr)
            for err in errors:
                print(f"  * {err}", file=sys.stderr)
            return 1
        print("\nAll assets verified successfully against manifest (SHA-256 and size match).")
        return 0

    # Fetch mode
    cache_dir = args.cache_dir
    if cache_dir is None:
        # Check standard cache location
        candidate_cache = base_dir.parents[2] / ".cache" / "sanoTTS"
        if candidate_cache.is_dir():
            cache_dir = candidate_cache

    success = fetch_all_missing(manifest_path, base_dir, cache_dir=cache_dir)
    valid, errors = verify_manifest(manifest_path, base_dir=base_dir, verbose=not args.quiet)

    if not success or not valid:
        print("\nAsset fetch or verification failed:", file=sys.stderr)
        for err in errors:
            print(f"  * {err}", file=sys.stderr)
        return 1

    print("\nAll assets ready and verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
