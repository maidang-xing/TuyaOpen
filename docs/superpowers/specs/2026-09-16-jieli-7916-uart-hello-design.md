# Jieli AC7916A UART Hello World Design

## Goal

将 Jieli AC7916A（`wl82`）接入 TuyaOpen，先完成不依赖 Wi-Fi、蓝牙或云服务的 UART Hello World 纵向闭环：TuyaOpen CLI 识别平台、调用 Jieli 构建链生成固件、调用平台烧录 bridge 下载固件，并通过 `tos.py monitor` 看到启动日志。

## Scope

本阶段包含：

- TuyaOpen 平台名 `JIELI` 和板名 `AC7916A`。
- Jieli AC79 SDK 的 `wl82` Makefile 构建封装。
- 当前 TuyaOpen 的最小系统/日志/UART 接口入口。
- `examples/get-started/jieli_uart_hello` 示例。
- TuyaOpen `tos.py build`、`tos.py flash`、`tos.py monitor` 的接入。
- 主机侧构建脚本的单元测试、静态配置检查和无硬件 build 验证。

本阶段不包含 Wi-Fi、蓝牙、Tuya Cloud、KV、OTA、GPIO 继电器和 switch DP。

## Architecture

TuyaOpen 负责项目配置、Kconfig、组件编译和 CLI 调度；Jieli SDK 继续负责最终链接脚本、厂商静态库和 section/movable region。平台 `build_example.py` 接收 TuyaOpen 生成的 `build_param.config`，将 TuyaOpen 示例源码和头文件传入 Jieli 工程。检测到杰理 `host-client` 时生成完整厂商包；Linux 主机缺少该工具时使用 Jieli `objcopy` 生成原始 `app.bin`，并统一复制为 TuyaOpen 的 `.build/bin/` 标准命名文件。

`platform_flash_bridge.py` 实现 `platform_flash(using_data, binfile, port, baud, boards_root, logger)`，将 TuyaOpen 的 `tos.py flash` 参数转换成用户配置的 `JIELI_FLASH_CMD` 调用。缺少烧录命令、license 或串口时，bridge 必须返回清晰错误，而不能回退到不匹配的 `tyutool`。

## Data flow

```text
app_default.config
  -> tos.py config/build
  -> .build/cache/using.config + build_param.config
  -> platform/JIELI/build_example.py
  -> AC79_AIoT_SDK/apps/.../board/wl82/Makefile
  -> sdk.elf + Jieli packaged firmware
  -> .build/bin/<project>_QIO_<version>.bin
  -> tos.py flash
  -> platform/JIELI/platform_flash_bridge.py
  -> Jieli USB downloader
```

## Acceptance criteria

1. `tos.py config set` 能选择 `JIELI`、`AC7916A` 和 `jieli_uart_hello`。
2. 在已安装匹配 Jieli 工具链的 Linux 环境中，`tos.py build` 返回 0，并生成标准命名的原始应用 artifact；具备厂商 postbuild 工具时生成完整 QIO/UFW artifact。
3. `tos.py flash -p <port>` 只调用 Jieli platform bridge，工具缺失时返回非零并报告缺失项。
4. 在真实 AC7916A 开发板上烧录后，UART 以 115200 波特率输出 `TuyaOpen Jieli AC7916A` 和 `UART Hello World`。
5. 不修改用户已有的 TuyaOpen 主工作区变更，不依赖旧 `ipc_ac7916a` 仓库的 legacy Tuya OS 初始化。

## Known constraints

- 当前已验证的旧 Jieli 工具链能完成 AC79 编译/链接，但 checkout 中的 791x postbuild/烧录工具为 Windows 版本；Linux 构建因此降级输出原始 `app.bin`，实际烧录仍需配置 `JIELI_FLASH_CMD`。
- Jieli SDK 的 Makefile 使用相对路径和专用 linker plugin，因此不能用通用 CMake linker 替换。
- 当前没有真实开发板、串口和授权文件，硬件验收只能在资源可用后完成。
