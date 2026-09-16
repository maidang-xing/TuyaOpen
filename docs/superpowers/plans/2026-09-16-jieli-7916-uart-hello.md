# Jieli AC7916A UART Hello World Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 接入 AC7916A/wl82 平台，使 TuyaOpen 能通过 `tos.py build/flash/monitor` 运行 UART Hello World。

**Architecture:** TuyaOpen 保持 Kconfig、组件和 CLI 调度；`platform/JIELI` 封装 Jieli host build/flash；Jieli SDK 继续掌握最终链接、section 和固件打包。第一阶段只实现最小系统、日志和 UART 路径。

**Tech Stack:** TuyaOpen Python CLI、CMake/Ninja、Kconfig、Jieli Makefile、Jieli pi32v2 clang/lto-wrapper、UART 115200。

**Spec:** `docs/superpowers/specs/2026-09-16-jieli-7916-uart-hello-design.md`

## Global Constraints

- 目标芯片固定为 AC7916A，Jieli 平台固定为 `wl82`。
- 第一阶段不启用 Wi-Fi、蓝牙、Tuya Cloud、KV、OTA、GPIO 继电器和 switch DP。
- Jieli 最终链接必须继续使用其 `sdk.ld`、厂商 `.a` 和 movable-region 参数。
- 主工作区已有改动不得进入本分支提交。
- 缺少匹配工具链、license 或串口时必须失败并报告原因；缺少 Linux postbuild 工具时允许明确标注的原始 `app.bin` 降级产物。

### Task 1: Add the platform and board configuration

**Files:**
- Create: `platform/JIELI/Kconfig`
- Create: `platform/JIELI/default.config`
- Create: `platform/JIELI/platform_config.cmake`
- Create: `platform/JIELI/toolchain_file.cmake`
- Create: `boards/JIELI/Kconfig`
- Create: `boards/JIELI/AC7916A/Kconfig`
- Create: `boards/JIELI/AC7916A/CMakeLists.txt`
- Modify: `boards/Kconfig`
- Modify: `platform/platform_config.yaml`

**Interfaces:**
- Produces `CONFIG_PLATFORM_CHOICE=JIELI`, `CONFIG_CHIP_CHOICE=AC7916A`, `CONFIG_BOARD_CHOICE=AC7916A` and `PLATFORM_PATH` metadata consumed by later build tasks.

- [ ] Write a Python configuration test that parses the platform YAML and Kconfig source files and asserts the JIELI/AC7916A symbols exist.
- [ ] Run `python -m unittest tests/platform/test_jieli_config.py -v` and verify it fails because the symbols are absent.
- [ ] Add the platform and board Kconfig files with RTOS defaults, UART enablement, 115200 log baudrate, and no network feature selection.
- [ ] Add the JIELI entry to `platform/platform_config.yaml` and the board choice to `boards/Kconfig`.
- [x] Run the configuration test and inspect the generated Kconfig selection.
- [x] Commit with `git commit -m "feat: add Jieli AC7916A platform configuration"`.

### Task 2: Add the host-side Jieli build adapter

**Files:**
- Create: `platform/JIELI/jieli_build.py`
- Create: `platform/JIELI/build_setup.py`
- Create: `platform/JIELI/build_example.py`
- Create: `tests/platform/test_jieli_build.py`
- Modify: `platform/JIELI/default.config`

**Interfaces:**
- `jieli_build.py` provides `resolve_sdk_root()`, `resolve_tool_dir()`, `build_make_command()`, and `find_qio_artifact()`.
- `build_example.py` accepts `build_param_dir` and `build|clean`, returns a nonzero exit code for missing prerequisites, and copies the packaged artifact into `BIN_OUTPUT_DIR` using `<name>_QIO_<version>.bin`.

- [ ] Write tests for absolute SDK/toolchain resolution, command construction, missing SDK failure, and deterministic artifact naming.
- [ ] Run `python -m unittest tests/platform/test_jieli_build.py -v` and verify the new adapter tests fail because the module does not exist.
- [ ] Implement environment variables `JIELI_SDK_ROOT` and `JIELI_TOOL_DIR`, defaulting to the checked-out SDK and the existing old toolchain only when explicitly discoverable; never silently use `/opt/jieli` if it is absent.
- [ ] Implement `build_setup.py` as a non-interactive prerequisite check for `clang`, `lto-wrapper`, `lto-ar`, `make`, and Jieli SDK files.
- [ ] Implement `build_example.py` to invoke `make -C <sdk>/apps/demo/demo_hello/board/wl82 TOOL_DIR=<tool-dir>`, validate `cpu/wl82/tools/sdk.elf`, run the Jieli postbuild command, and copy the resulting package to `BIN_OUTPUT_DIR`.
- [x] Run the unit tests and verify they pass; run the adapter through `tos.py build`.
- [x] Commit with `git commit -m "feat: add Jieli host build adapter"`.

### Task 3: Add the minimal UART application and platform output path

**Files:**
- Create: `examples/get-started/jieli_uart_hello/CMakeLists.txt`
- Create: `examples/get-started/jieli_uart_hello/app_default.config`
- Create: `examples/get-started/jieli_uart_hello/src/example_jieli_uart_hello.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/include/jieli_tkl_uart.h`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_output.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_uart.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_system.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/CMakeLists.txt`
- Modify: `platform/JIELI/platform_config.cmake`

**Interfaces:**
- The example exports `example_jieli_uart_hello(void)` and emits the two acceptance strings through the TuyaOpen log/UART path.
- The adapter maps TuyaOpen output/UART calls to Jieli `printf`/UART APIs without requiring network or cloud initialization.

- [ ] Add a source-level test that checks the example contains the exact acceptance strings and uses the TuyaOpen logging entry point.
- [ ] Run the source-level test and verify it fails before the example exists.
- [ ] Implement the smallest application entry compatible with the Jieli demo application startup and call `example_jieli_uart_hello()` once during startup.
- [ ] Implement output and UART adapter functions using only headers present in the selected Jieli SDK; return `OPRT_NOT_SUPPORTED` for operations not required by the hello demo.
- [ ] Add adapter sources/includes to `platform_config.cmake` and ensure the platform linker receives them.
- [x] Run the source-level test and compile the example with the Jieli compiler; verify the minimal graph pulls no network component.
- [x] Integrate the example into the Jieli final link.

### Task 4: Integrate TuyaOpen build artifacts and flash bridge

**Files:**
- Create: `platform/JIELI/platform_flash_bridge.py`
- Create: `platform/JIELI/README_zh.md`
- Modify: `platform/JIELI/build_example.py`
- Modify: `tests/platform/test_jieli_build.py`

**Interfaces:**
- `platform_flash_bridge.py` exports `platform_flash(using_data, binfile, port, baud, boards_root, logger)` and returns `{success: bool, message: str}`.
- `tos.py flash` discovers the bridge by platform name and does not invoke generic `tyutool` for JIELI.

- [x] Add bridge coverage for missing binary and missing uploader.
- [x] Implement subprocess execution with `port`, optional `baud`, and the generated artifact; never fall back to generic `tyutool`.
- [x] Document Linux host prerequisites, `JIELI_SDK_ROOT`, `JIELI_TOOL_DIR`, `JIELI_FLASH_CMD`, UART port and 115200 monitor settings.
- [x] Run the complete platform unit-test set and verify it passes.

### Task 5: Run the TuyaOpen CLI acceptance checks

**Files:**
- Modify: `examples/get-started/jieli_uart_hello/README_CN.md`
- Create: `tests/platform/test_jieli_acceptance.py`

- [x] Add the example README with the config/build/flash/monitor sequence.
- [x] Run `python -m unittest discover -s tests/platform -p 'test_jieli_*.py'` and verify all host-side tests pass.
- [x] Run `tos.py build` from clean and generate the standard artifact.
- [x] Run `tos.py flash` with a virtual port and verify it routes to the Jieli bridge.
- [ ] On a real board with an authorized downloader and serial port, run flash and monitor; hardware validation remains blocked until those resources are available.
- [x] Run `git diff --check` and inspect `git status` before publishing.
