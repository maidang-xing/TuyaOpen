# 07 — AC792 语音与唤醒

适用：WL83 可先只读准备；代码实现等待 02 语音 owner 完成交接。

```text
请执行任务 07。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\07 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。按 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\07-ac792-voice.md 在 AC792/WL83 独立验证采集、KWS/VAD、chat-bot、TTS 和恢复监听。先核对最新 pin、实板版次、mic/ADC route、格式和目标词；不把 AC791 ABI、模型或结果外推到 WL83。现有采集为 16k/16bit/mono 并接 RMS VAD，目标 pin 的 tkl_kws.c 仍是 stub；WL83 SDK 自带 KWS 库/参考模块是独立候选，须核 target ABI、模型、内存和 PCM API。可提前做只读/独立 probe；未完成 02 时不得并改共享 tkl_audio.c/tkl_vad.c/tkl_kws.c/构建清单，先交接唯一代码 owner。独立 worktree、保护 dirty；产品目标词未确认不得替换。实机需 AC792 单人租约并现场确认端口。报告静态、构建、PCM/模型和端到端各层，不将 app/TTS 启动说成唤醒通过。可用建议起始阈值 20次命中≥18、静音30分钟0误触发、启停100次恢复，需标待校准。仅本地提交，不外传库/模型、不做远端写。
```

本文件只保存派发提示词；尚未执行任务 07。
