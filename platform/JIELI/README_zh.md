# Jieli wl82 平台（AC7916A 开发板）

当前平台适配的第一个里程碑是 `jieli_uart_hello`：使用 TuyaOpen 的
`tos.py config/build/flash/monitor` 入口，最终链接仍由 AC79 SDK 的
`apps/demo/demo_hello/board/wl82/Makefile` 完成。

## 环境变量

```sh
export JIELI_SDK_ROOT=/path/to/AC79_AIoT_SDK
export JIELI_TOOL_DIR=/path/to/pi32v2/bin
```

`JIELI_TOOL_DIR` 至少需要包含 `clang`、`lto-wrapper`、`lto-ar`、`objdump`
和 `objsizedump`。Linux 主机没有杰理 `host-client` 时，构建适配会使用
杰理 `objcopy` 从 `sdk.elf` 生成原始 `app.bin`；这不是完整 UFW 包。

## 构建

```sh
cd examples/get-started/jieli_uart_hello
tos.py config set CONFIG_BOARD_CHOICE=AC7916A
tos.py build
```

输出文件为：

```text
dist/jieli_uart_hello_1.0.0/jieli_uart_hello_QIO_1.0.0.bin
```

## 烧录和串口

AC79 SDK 当前 checkout 未提供可直接在 Linux 上执行的烧录器。配置一个
具体板卡/烧录器命令后，TuyaOpen 的 `tos.py flash` 会通过平台 bridge 调用：

```sh
export JIELI_FLASH_CMD='my-jieli-uploader --file "{binfile}" --port "{port}" --baud "{baud}"'
tos.py flash -p /dev/ttyUSB0 -b 115200
tos.py monitor -p /dev/ttyUSB0 -b 115200
```

`JIELI_FLASH_CMD` 中支持 `{binfile}`、`{port}`、`{baud}`、`{chip}` 和
`{board}` 占位符。未配置时，`tos.py flash` 会明确报错，不会退回通用
`tyutool`。

预期串口输出包含：

```text
TuyaOpen Jieli AC7916A
UART Hello World
```
