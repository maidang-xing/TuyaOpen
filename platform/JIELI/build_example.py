#!/usr/bin/env python3
"""Build and package a TuyaOpen example with the Jieli AC79 SDK."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from jieli_build import (
    BuildError,
    TOOLS_RELATIVE,
    build_make_command,
    find_qio_artifact,
    resolve_sdk_root,
    resolve_tool_dir,
    tuya_qio_name,
)


def parse_build_params(path: Path) -> dict[str, str]:
    params: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        params[key.strip()] = value.strip().strip('"')
    return params


def _run(command: list[str], cwd: Path, env: dict[str, str]) -> None:
    print(f"[JIELI] run: {shlex.join(command)}")
    result = subprocess.run(command, cwd=cwd, env=env, check=False)
    if result.returncode != 0:
        raise BuildError(f"command failed with exit code {result.returncode}: {command[0]}")


def _run_postbuild(sdk_root: Path, tool_dir: Path, env: dict[str, str]) -> None:
    tools_dir = sdk_root / TOOLS_RELATIVE
    command_text = env.get("JIELI_POSTBUILD_CMD", "").strip()
    if command_text:
        _run(shlex.split(command_text), tools_dir, env)
        return

    script = tools_dir / "download.sh"
    if not script.is_file():
        raise BuildError(f"Jieli postbuild script not found: {script}")
    _run(["bash", str(script), "sdk"], tools_dir, env)


def build(params: dict[str, str]) -> Path:
    sdk_root = resolve_sdk_root()
    tool_dir = resolve_tool_dir(sdk_root)
    jobs = max(1, int(os.environ.get("JIELI_BUILD_JOBS", "1")))
    env = os.environ.copy()
    env["PATH"] = f"{tool_dir}:{env.get('PATH', '')}"
    env["OBJDUMP"] = str(tool_dir / "objdump")
    env["OBJSIZEDUMP"] = str(tool_dir / "objsizedump")

    command = build_make_command(sdk_root, tool_dir, jobs)
    _run(command, sdk_root, env)

    tools_dir = sdk_root / TOOLS_RELATIVE
    elf = tools_dir / "sdk.elf"
    if not elf.is_file() or elf.stat().st_size == 0:
        raise BuildError(f"Jieli linker did not produce {elf}")

    _run_postbuild(sdk_root, tool_dir, env)
    package = find_qio_artifact(tools_dir)

    output_dir = Path(params.get("BIN_OUTPUT_DIR", ""))
    if not output_dir:
        raise BuildError("BIN_OUTPUT_DIR is missing from build parameters")
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / tuya_qio_name(params)
    shutil.copy2(package, output)
    print(f"[JIELI] artifact: {output}")
    return output


def clean() -> None:
    sdk_root = resolve_sdk_root()
    tool_dir = resolve_tool_dir(sdk_root)
    env = os.environ.copy()
    env["PATH"] = f"{tool_dir}:{env.get('PATH', '')}"
    _run(
        ["make", "-C", str(sdk_root / "apps/demo/demo_hello/board/wl82"),
         f"TOOL_DIR={tool_dir}", "clean"],
        sdk_root,
        env,
    )


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[2] not in ("build", "clean"):
        print(f"usage: {argv[0]} <build-param-dir> <build|clean>", file=sys.stderr)
        return 2
    try:
        param_dir = Path(argv[1])
        param_file = param_dir / "build_param.config"
        if argv[2] == "clean":
            clean()
        else:
            build(parse_build_params(param_file))
    except (BuildError, OSError, ValueError) as exc:
        print(f"[JIELI] build failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
