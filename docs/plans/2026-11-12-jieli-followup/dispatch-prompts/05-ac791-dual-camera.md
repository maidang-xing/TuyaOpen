# 05 — AC791 双摄拓扑与适配

适用：00 完成。可并行准备资料；硬件按排期预约。

```text
请执行任务 05。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\05 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。按 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\05-ac791-dual-camera.md 先确认 AC791 实板拓扑，再决定实现。核对实际 PCB revision、原理图、接口/传感器和 WL82 SDK 并发能力；DVP+SPI 只是两种接口候选，不能推断为双路 DVP。拓扑不成立时以证据报告阻塞，不虚构接口；成立后先分别、再并发采集有效帧并记录帧率、丢帧、内存、时长。使用独立主仓及平台 worktree、canonical pin，保护 dirty；明确 camera API/平台共享文件 owner，不与任务 06 同时改公共 camera 接口。确认你可访问本机资料和目标硬件；若缺失交回差异/补丁。实机须唯一租约、现场确认板/传感器/端口，避免改线。构建不代表双摄通过。交付拓扑证据、源码变更、构建/帧日志、firmware SHA 和未验项；任务范围内可本地提交，不推送或发 PR。
```

本文件只保存派发提示词；尚未执行任务 05。
