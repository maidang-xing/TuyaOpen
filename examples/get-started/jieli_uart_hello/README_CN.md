# Jieli AC7916A UART Hello

这是 TuyaOpen 接入杰理 7916x 的最小验收示例，目标板为 AC7916A/wl82，
日志 UART 默认使用 115200 波特率。

```sh
cd examples/get-started/jieli_uart_hello
export JIELI_SDK_ROOT=/path/to/AC79_AIoT_SDK
export JIELI_TOOL_DIR=/path/to/pi32v2/bin
tos.py config set CONFIG_BOARD_CHOICE=AC7916A
tos.py build
```

构建成功后，固件位于 `dist/jieli_uart_hello_1.0.0/`。Linux 主机若没有
杰理 `host-client`，当前适配会生成原始 `app.bin`；接入实际板卡时需要由
`JIELI_FLASH_CMD` 指定对应的杰理烧录命令：

```sh
export JIELI_FLASH_CMD='my-jieli-uploader --file "{binfile}" --port "{port}" --baud "{baud}"'
tos.py flash -p /dev/ttyUSB0 -b 115200
tos.py monitor -p /dev/ttyUSB0 -b 115200
```
