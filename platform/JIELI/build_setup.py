#!/usr/bin/env python3
"""Validate the host inputs needed by the AC7916A build bridge."""

from __future__ import annotations

import shutil
import sys

from jieli_build import BuildError, resolve_sdk_root, resolve_tool_dir


def main() -> int:
    try:
        sdk_root = resolve_sdk_root()
        tool_dir = resolve_tool_dir(sdk_root)
    except BuildError as exc:
        print(f"[JIELI] build setup failed: {exc}", file=sys.stderr)
        return 1

    make = shutil.which("make")
    if not make:
        print("[JIELI] build setup failed: make not found", file=sys.stderr)
        return 1

    postbuild = sdk_root / "cpu/wl82/tools/download.sh"
    if not postbuild.is_file():
        print(f"[JIELI] build setup failed: postbuild script not found: {postbuild}", file=sys.stderr)
        return 1

    print(f"[JIELI] SDK: {sdk_root}")
    print(f"[JIELI] toolchain: {tool_dir}")
    print(f"[JIELI] make: {make}")
    print(f"[JIELI] postbuild: {postbuild}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
