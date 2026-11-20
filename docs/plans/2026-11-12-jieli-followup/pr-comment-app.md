实机验收更新（2026-11-17）：

- 3 FB 配置镜像 `spi-upstream3fb-8c39c3b8.bin` 已烧录（`flash-791-spiupstream3fb-20261117-111924.log`，Device online → 擦除/下载/reboot）。
- 60 s 日志 `serial-COM8-20261117-112024.log`：4.44 fps、约 480 行/帧、`tag_err=0`、无复位。
- 用户目视：2 FB（`spi-upstream-99a2db6e.bin`）与 3 FB（`spi-upstream3fb-8c39c3b8.bin`）两版画面均正常、无撕裂。
- 结论：本 PR 的 SPI 预览路径达到实机通过（数据面 + 目视 + 长稳）；未做云端端到端验证。
