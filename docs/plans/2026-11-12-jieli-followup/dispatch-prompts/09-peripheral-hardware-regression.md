# 09 — JIELI 外围硬件回归

适用：00 完成并确认 AC791/AC792 硬件预约；PWM 与 UART 可分时执行。

```text
请执行任务 09。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\09 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。依据 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\09-peripheral-hardware-regression.md 验证 AC792 PWM CH0/2/3 与 AC791 UART1 RX。先重新核查实际 pin/config、板版次和日志串口冲突；历史“PWM CH1/UART TX已过、其他待验”需按最新记录确认。独立 worktree 保护 dirty；仅在确认复现后改对应 adapter/board 路径。实机前登记唯一板卡/串口租约、现场枚举端口、线束、仪器和恢复方法；一块板/串口同一时刻只允许一个操作者，不与语音/OTA/camera并占。PWM用示波器记录周期/占空比/引脚；UART做外部收发/loopback并记录字节、丢失和错误。构建/发送成功不代表波形或接收通过。未经明确授权不擦配置或改变分区。交付固件 SHA、仪器截图/CSV、串口原始结果、变更、未验证项及端口释放交接；仅本地提交，不做远端写。
```

本文件只保存派发提示词；尚未执行任务 09。
