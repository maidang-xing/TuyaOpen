"""Host-side helpers for the TuyaOpen AC7916A/wl82 build bridge."""

from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Mapping, Optional


MODULE_ROOT = Path(__file__).resolve().parent
BOARD_BUILD_RELATIVE = Path("apps/demo/demo_hello/board/wl82")
TOOLS_RELATIVE = Path("cpu/wl82/tools")


class BuildError(RuntimeError):
    """Raised when a required Jieli build input is unavailable."""


def resolve_sdk_root(
    environ: Optional[Mapping[str, str]] = None,
    module_root: Path = MODULE_ROOT,
) -> Path:
    env = os.environ if environ is None else environ
    configured = env.get("JIELI_SDK_ROOT", "").strip()
    candidates = []
    if configured:
        candidates.append(Path(configured).expanduser())
    candidates.append((module_root / "../../../AC79_AIoT_SDK").resolve())

    for candidate in candidates:
        if (candidate / "apps/demo/demo_hello/board/wl82/Makefile").is_file():
            return candidate

    searched = ", ".join(str(path) for path in candidates)
    raise BuildError(
        "AC79 SDK not found; set JIELI_SDK_ROOT to a fw-AC79_AIoT_SDK checkout. "
        f"Searched: {searched}"
    )


def resolve_tool_dir(
    sdk_root: Path,
    environ: Optional[Mapping[str, str]] = None,
    module_root: Path = MODULE_ROOT,
) -> Path:
    env = os.environ if environ is None else environ
    configured = env.get("JIELI_TOOL_DIR", "").strip()
    candidates = []
    if configured:
        candidates.append(Path(configured).expanduser())
    candidates.extend(
        [
            (sdk_root.parent / "ipc_ac7916a/toolchain/jieli-linux-toolchains/pi32v2/bin").resolve(),
            Path("/opt/jieli/pi32v2/bin"),
            (module_root.parents[2] / "ipc_ac7916a/toolchain/jieli-linux-toolchains/pi32v2/bin").resolve(),
            (module_root.parents[3] / "ipc_ac7916a/toolchain/jieli-linux-toolchains/pi32v2/bin").resolve(),
        ]
    )

    required = ("clang", "lto-wrapper", "lto-ar", "objdump", "objsizedump")
    for candidate in candidates:
        if candidate.is_dir() and all((candidate / name).exists() for name in required):
            return candidate

    searched = ", ".join(str(path) for path in candidates)
    raise BuildError(
        "Jieli pi32v2 toolchain not found; set JIELI_TOOL_DIR to pi32v2/bin. "
        f"Required tools: {', '.join(required)}. Searched: {searched}"
    )


def build_make_command(sdk_root: Path, tool_dir: Path, jobs: int = 1) -> list[str]:
    if jobs < 1:
        raise ValueError("jobs must be at least 1")
    board_dir = sdk_root / BOARD_BUILD_RELATIVE
    return [
        "make",
        "-C",
        str(board_dir),
        f"TOOL_DIR={tool_dir}",
        f"-j{jobs}",
        "pre_build",
        "../../../../../cpu/wl82/tools/sdk.elf",
    ]


def create_staging_tree(
    sdk_root: Path,
    staging_root: Path,
    tuyaopen_root: Path,
    header_dir: Optional[Path] = None,
) -> Path:
    """Create a small overlay tree without modifying the vendor checkout."""
    if staging_root.exists():
        shutil.rmtree(staging_root)

    build_root = staging_root / "build"
    build_root.mkdir(parents=True)
    for name in ("cpu", "include_lib", "lib"):
        (build_root / name).symlink_to(sdk_root / name, target_is_directory=True)

    apps_root = build_root / "apps"
    apps_root.mkdir()
    (apps_root / "common").symlink_to(sdk_root / "apps/common", target_is_directory=True)
    shutil.copytree(
        sdk_root / "apps/demo/demo_hello",
        apps_root / "demo/demo_hello",
    )

    platform_root = tuyaopen_root / "platform/JIELI"
    shutil.copy2(platform_root / "tuyaos_app_main.c", build_root / "tuyaos_app_main.c")
    shutil.copy2(
        tuyaopen_root / "examples/get-started/jieli_uart_hello/src/example_jieli_uart_hello.c",
        build_root / "tuyaopen_uart_hello.c",
    )
    shutil.copytree(
        platform_root / "tuyaos/tuyaos_adapter",
        build_root / "tuyaos_adapter",
    )

    makefile = apps_root / "demo/demo_hello/board/wl82/Makefile"
    content = makefile.read_text(encoding="utf-8")
    vendor_main = "../../../../../apps/demo/demo_hello/app_main.c"
    if vendor_main not in content:
        raise BuildError(f"AC79 demo Makefile has no app_main source: {makefile}")
    content = content.replace(vendor_main, "../../../../../tuyaos_app_main.c")
    extra_sources = (
        "c_SRC_FILES += \\\n"
        "    ../../../../../tuyaopen_uart_hello.c \\\n"
        "    ../../../../../tuyaos_adapter/src/tkl_output.c \\\n"
        "    ../../../../../tuyaos_adapter/src/tkl_system.c \\\n"
        "    ../../../../../tuyaos_adapter/src/tkl_uart.c\n"
    )
    marker = "c_OBJS    :="
    if marker not in content:
        raise BuildError(f"AC79 demo Makefile has no object list marker: {makefile}")
    content = content.replace(marker, extra_sources + "\n" + marker, 1)
    content += "\n"
    content += "INCLUDES += \\\n"
    content += f"    -I{tuyaopen_root}/tools/porting/adapter/system \\\n"
    content += f"    -I{tuyaopen_root}/tools/porting/adapter/uart \\\n"
    content += f"    -I{tuyaopen_root}/tools/porting/adapter/init/include \\\n"
    content += f"    -I{tuyaopen_root}/tools/porting/adapter/utilities/include \\\n"
    content += f"    -I{tuyaopen_root}/src/common/include\n"
    content += "INCLUDES += \\\n"
    content += "    -I../../../../../tuyaos_adapter/include \\\n"
    content += "    -I../../../../../include_lib/driver/device \\\n"
    content += "    -I../../../../../include_lib/driver/cpu/wl82\n"
    if header_dir is not None:
        content += f"INCLUDES += -I{header_dir}\n"
    makefile.write_text(content, encoding="utf-8")
    return build_root


def find_qio_artifact(tools_dir: Path) -> Path:
    for name in ("jl_isd.ufw", "jl_isd.fw", "app.bin"):
        candidate = tools_dir / name
        if candidate.is_file() and candidate.stat().st_size > 0:
            return candidate
    raise BuildError(
        f"Jieli postbuild produced no flash package in {tools_dir}; "
        "expected jl_isd.ufw, jl_isd.fw, or app.bin"
    )


def tuya_qio_name(params: Mapping[str, str]) -> str:
    project = params.get("CONFIG_PROJECT_NAME", "jieli_uart_hello")
    version = params.get("CONFIG_PROJECT_VERSION", "1.0.0")
    return f"{project}_QIO_{version}.bin"
