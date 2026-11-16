# 10 — AC791 触摸适配

适用：00 完成；AC791 实测等待语音执行者释放硬件。

```text
请执行任务 10。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\10 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。依据 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\10-ac791-touch.md 为 AC79_DevKitBoard 接入触摸。先按实板 revision、丝印、原理图、SDK board 配置核实 IC、总线、地址、IRQ/reset、供电及与 LCD 的共享关系；TP_SDA/SCL/INT/RESET 网络名不证明芯片型号或 GPIO。当前 TuyaOpen 有 LVGL v9 indev port，但尚无启用的 JIELI TKL touch API；SDK 多个候选驱动不可猜选。若证据缺失，继续做可独立的框架/代码入口评估并记录阻塞，不写死硬件值。主仓和平台用独立 worktree，保护 dirty；与任务 11 先协调共享 LVGL/touch 代码 owner。构建后若取得租约，实测中心/四角/拖动/释放、旋转映射、重复启停和错误恢复；屏幕点亮不是触摸验收。实机等待语音 owner 显式释放，现场确认端口，不擦配网授权。交付图纸/板卡证据、diff、构建/交互日志、固件 SHA、未验证项；本地提交，不推送。
```

本文件只保存派发提示词；尚未执行任务 10。
