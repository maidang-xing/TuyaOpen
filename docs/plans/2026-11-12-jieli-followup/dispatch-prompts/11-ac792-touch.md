# 11 — AC792 触摸适配

适用：00 完成；触摸和 camera 可做资料准备，实机分别预约。

```text
请执行任务 11。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\11 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。按 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\11-ac792-touch.md 接入 AC792N Develop 触摸输入。先核实实际 PCB revision、器件丝印/位号、I2C/SPI、地址、IRQ/reset、电源和 pinmux；图纸 TP_* 网络与 SDK 的多候选驱动均不足以断定 IC/GPIO。核对最新主仓 LVGL v9入口、当前 canonical JIELI pin 和 board config；不能沿用旧 platform pin。以独立主仓/platform worktree 实施最小适配，保护 dirty；先和任务 10 确认共享 touch/LVGL 文件的唯一代码 owner，不能同时改。设备可用时单独租约实测触点、拖动、方向、重复 init/close 和错误恢复；DSI/LCD显示不代表输入通过。若硬件未连接/型号不明，完成不依赖硬件的工作并留下明确缺口。记录实际构建命令、板版次、固件 SHA、交互日志和未验项；不擦授权/配网，不推送/发 PR。
```

本文件只保存派发提示词；尚未执行任务 11。
