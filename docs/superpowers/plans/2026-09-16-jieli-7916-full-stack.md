# Jieli AC7916A TuyaOpen Full-Stack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 通过 TAL driver → TKL adapter 分层，将 AC7916A/wl82 的 System、UART、Wi‑Fi、BLE 接入 TuyaOpen，并让 `apps/tuya_cloud/switch_demo` 通过 `tos.py build/flash` 进入可验证链路。

**Architecture:** Jieli 原生 FreeRTOS、Wi‑Fi、BLE、lwIP 和 vendor libraries 保持底层实现；平台私有 TAL driver 归一化 Jieli API；TuyaOpen TKL 和 `switch_demo` 只依赖 TuyaOpen 接口。CMake 生成 TuyaOpen archives，Jieli staging Makefile 继续完成最终链接。

**Tech Stack:** TuyaOpen Kconfig/CMake/Ninja/tos.py、Jieli AC79 SDK wl82、pi32v2 clang/lto-wrapper、FreeRTOS、Jieli Wi‑Fi/BT Stack、lwIP、Python unittest。

**Spec:** `docs/superpowers/specs/2026-09-16-jieli-7916-full-stack-design.md`

## Global Constraints

- 目标芯片固定为 AC7916A，Jieli 平台固定为 `wl82`。
- 平台私有 TAL driver 不包含 TuyaOpen 业务头文件。
- BLE 使用 Jieli vendor BT Stack，不编译 TuyaOpen NimBLE。
- Jieli 最终链接继续使用 vendor `sdk.ld`、厂商 `.a` 和 movable-region 参数。
- 不修改 `/home/share/samba/tyopen/TuyaOpen` 主工作区已有用户改动。
- 设备未连接；本轮只验收主机编译、产物和 flash 路由，不声称真实烧录成功。

---

### Task 1: Freeze SDK contracts and add supplemental platform metadata

**Files:**
- Create: `platform/JIELI/sdk_config.yaml`
- Modify: `platform/JIELI/platform_config.cmake`
- Modify: `platform/JIELI/toolchain_file.cmake`
- Modify: `boards/JIELI/Kconfig`
- Modify: `boards/JIELI/AC7916A/Kconfig`
- Test: `tests/platform/test_jieli_sdk_config.py`

**Interfaces:**
- YAML keys: `sdk_root_env`, `toolchain_env`, `vendor_board`, `include_dirs`, `library_files`, `defines`, `stack_libraries`。
- `platform_config.cmake` consumes the same metadata and exposes `JIELI_SDK_ROOT`、`JIELI_TOOL_DIR`、`JIELI_VENDOR_LIBS`。

- [ ] Write a test that loads the YAML and asserts the AC79 SDK marker, wl82 board, required tools, FreeRTOS define, Wi‑Fi libraries, and BT Stack libraries are listed.
- [ ] Run the new test and verify it fails because the supplemental YAML is absent.
- [ ] Add YAML metadata with paths relative to `JIELI_SDK_ROOT`, never hard-code the developer checkout path.
- [ ] Add CMake parsing/validation for each required path and emit one actionable missing-item error.
- [ ] Run the metadata test and Python syntax checks.
- [ ] Commit `feat: describe Jieli AC7916A SDK inputs`。

### Task 2: Extract Jieli TAL System and UART layer

**Files:**
- Create: `platform/JIELI/tuyaos/tal_driver/include/jieli_tal_system.h`
- Create: `platform/JIELI/tuyaos/tal_driver/include/jieli_tal_uart.h`
- Create: `platform/JIELI/tuyaos/tal_driver/src/jieli_tal_system.c`
- Create: `platform/JIELI/tuyaos/tal_driver/src/jieli_tal_uart.c`
- Modify: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_system.c`
- Modify: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_uart.c`
- Modify: `platform/JIELI/tuyaos/tuyaos_adapter/CMakeLists.txt`
- Test: `tests/platform/test_jieli_tal_contracts.py`

**Interfaces:**
- `jieli_tal_system_*` uses Jieli `os_*` APIs and returns 0 or a negative platform error.
- `jieli_tal_uart_*` owns the Jieli device handle and maps logical port 0 to `uart2`.
- TKL functions remain the public TuyaOpen entry points and convert return codes.

- [ ] Add contract tests that forbid `tuya_cloud_types.h` in TAL driver files and require the timeout/port constants.
- [ ] Implement TAL system wrappers for thread, mutex, semaphore, queue, sleep, time, random, reset and heap query.
- [ ] Implement TAL UART open/config/read/write/close and explicit unsupported returns.
- [ ] Change TKL system/UART files to call TAL driver functions only.
- [ ] Build the existing `jieli_uart_hello` example and verify the Jieli final link still succeeds.
- [ ] Commit `feat: add Jieli TAL system and UART layer`。

### Task 3: Implement TAL Network and Wi‑Fi adapter

**Files:**
- Create: `platform/JIELI/tuyaos/tal_driver/include/jieli_tal_network.h`
- Create: `platform/JIELI/tuyaos/tal_driver/include/jieli_tal_wifi.h`
- Create: `platform/JIELI/tuyaos/tal_driver/src/jieli_tal_network.c`
- Create: `platform/JIELI/tuyaos/tal_driver/src/jieli_tal_wifi.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_network.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_wifi.c`
- Modify: `platform/JIELI/tuyaos/tuyaos_adapter/CMakeLists.txt`
- Test: `tests/platform/test_jieli_wifi_contracts.py`

**Interfaces:**
- `jieli_tal_wifi_*` exposes scan/STA/AP/status/IP/MAC/event callbacks without Tuya types.
- `tkl_wifi_*` implements `tools/porting/adapter/wifi/tkl_wifi.h`.
- `tkl_network_*` implements `tools/porting/adapter/network/tkl_network.h` using Jieli/lwIP sockets.

- [ ] Inventory exact Jieli Wi‑Fi and lwIP symbols from the selected SDK headers and vendor archives; record the mapping in the test fixture.
- [ ] Add failing contract tests for STA connect/disconnect, scan release, status, IP, MAC and event conversion.
- [ ] Implement the TAL Wi‑Fi state machine and copy callback data before crossing task contexts.
- [ ] Implement TKL Wi‑Fi conversion and required network socket/DNS/select functions.
- [ ] Add Jieli Wi‑Fi/lwIP libraries to the supplemental link group.
- [ ] Compile a Wi‑Fi-only TuyaOpen example and inspect the link map for unresolved Wi‑Fi/network symbols.
- [ ] Commit `feat: add Jieli TAL Wi-Fi and network adapter`。

### Task 4: Implement Jieli TAL BLE and TuyaOpen Bluetooth adapter

**Files:**
- Create: `platform/JIELI/tuyaos/tal_driver/include/jieli_tal_ble.h`
- Create: `platform/JIELI/tuyaos/tal_driver/src/jieli_tal_ble.c`
- Create: `platform/JIELI/tuyaos/tuyaos_adapter/src/tkl_bluetooth.c`
- Modify: `platform/JIELI/tuyaos/tuyaos_adapter/CMakeLists.txt`
- Test: `tests/platform/test_jieli_ble_contracts.py`

**Interfaces:**
- `jieli_tal_ble_*` owns Jieli BLE Stack lifecycle and translates GAP/GATT events into platform-neutral events.
- `tkl_bluetooth.c` implements the public functions in `tkl_bluetooth.h`.

- [ ] Inventory the Jieli BLE role, GAP, GATT and callback APIs and assert the required vendor libraries are in the SDK YAML.
- [ ] Add failing tests for peripheral init, advertising data, service/characteristic registration, notify and disconnect.
- [ ] Implement the TAL BLE event bridge with bounded buffers and explicit MTU/length validation.
- [ ] Implement TKL BLE functions needed by Tuya BLE provisioning.
- [ ] Ensure CMake does not add `src/tal_bluetooth/nimble` for JIELI and does add the Jieli BT Stack libraries.
- [ ] Compile a BLE peripheral configuration and inspect the link map for NimBLE objects or unresolved callbacks.
- [ ] Commit `feat: add Jieli TAL BLE adapter`。

### Task 5: Enable the complete TuyaOpen component graph for Jieli

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `platform/JIELI/platform_config.cmake`
- Modify: `boards/JIELI/AC7916A/Kconfig`
- Modify: `platform/JIELI/jieli_build.py`
- Modify: `platform/JIELI/build_example.py`
- Test: `tests/platform/test_jieli_link_inputs.py`

**Interfaces:**
- `CONFIG_JIELI_MINIMAL_HELLO=y` keeps the earlier UART smoke build.
- `CONFIG_JIELI_MINIMAL_HELLO=n` enables the full TuyaOpen component graph.
- The bridge passes `OPEN_LIBS_DIR` archives into the Jieli linker group before vendor system libraries close the group.

- [ ] Add a test that verifies minimal and full configurations select different component graphs.
- [ ] Make the root CMake skip default components only when `CONFIG_JIELI_MINIMAL_HELLO=y`.
- [ ] Add all implemented adapter and TAL sources to the Jieli platform target.
- [ ] Update staging Makefile generation so `libtuyaapp.a` and `libtuyaos.a` are linked before the vendor `--end-group`.
- [ ] Produce a link map and fail on unresolved `tkl_`, `tal_`, Wi‑Fi, BLE or socket symbols.
- [ ] Commit `feat: link full TuyaOpen components on Jieli`。

### Task 6: Port and build `apps/tuya_cloud/switch_demo`

**Files:**
- Create: `apps/tuya_cloud/switch_demo/config/AC7916A.config`
- Modify: `apps/tuya_cloud/switch_demo/app_default.config`
- Modify: `platform/JIELI/tuyaos_app_main.c`
- Modify: `platform/JIELI/default.config`
- Test: `tests/platform/test_jieli_switch_demo.py`

**Interfaces:**
- `tuya_app_main()` is called once from the Jieli startup entry after TAL/TKL initialization.
- `switch_demo` keeps its existing Tuya product/license configuration interface.

- [ ] Add a test checking the AC7916A config enables Wi‑Fi, Bluetooth, UART, lwIP, KV, timer, work queue and required security options.
- [ ] Add the AC7916A switch config and select it through the normal TuyaOpen Kconfig path.
- [ ] Adapt the Jieli app entry to invoke `tuya_app_main()` and preserve vendor startup ordering.
- [ ] Run `tos.py config set CONFIG_BOARD_CHOICE=AC7916A` in `apps/tuya_cloud/switch_demo`.
- [ ] Run a clean `tos.py build -v`; fix compile errors and unresolved link symbols until the build returns 0.
- [ ] Verify the generated artifact contains `tuya_iot_init`, network and BLE adapter symbols.
- [ ] Commit `feat: build switch_demo for Jieli AC7916A`。

### Task 7: Complete flash route and host-side acceptance

**Files:**
- Modify: `platform/JIELI/platform_flash_bridge.py`
- Modify: `platform/JIELI/README_zh.md`
- Create: `tests/platform/test_jieli_acceptance.py`

- [ ] Add tests for artifact validation, command placeholder expansion, nonzero uploader return and success return.
- [ ] Verify `tos.py flash -p /dev/ttyJIELI_TEST` enters the Jieli bridge and fails only because no uploader command is configured.
- [ ] Document `JIELI_SDK_ROOT`, `JIELI_TOOL_DIR`, `JIELI_FLASH_CMD`, build output and 115200 monitor settings.
- [ ] Run all Jieli platform tests, `git diff --check`, Python syntax checks and a clean `switch_demo` build.
- [ ] Commit `feat: validate Jieli switch_demo host flow`。

### Task 8: Independent review and iteration

**Files:**
- Review all commits and changed files on `feat/jieli-7916-uart`.
- Modify only files required by review findings.

- [ ] Dispatch a subagent to review architecture, API coverage, link ordering, Kconfig/YAML, and unresolved-symbol risks.
- [ ] Reproduce every review finding with a focused test or build command.
- [ ] Implement required corrections and rerun the complete host acceptance suite.
- [ ] Record hardware flash/run as blocked until a board, port and Jieli uploader are connected.
- [ ] Push the final branch to the personal remote after review iteration.
