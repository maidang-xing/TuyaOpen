# 02 — AC791 语音唤醒闭环

适用：00 完成；01 已给出候选路线或可并行推进不依赖它的工作。02 独占音频 adapter 修改。

```text
请执行任务 02。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\02 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。依据 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\02-ac791-voice-wakeup.md，在 AC791/WL82 完成采集→KWS/VAD→chat-bot→TTS→恢复监听。先复核当前 pin 和目标词；目标词未确认时不得擅换模型/词库，但继续完成可独立的 ABI、PCM、生命周期和错误恢复工作并标清阻塞。现有 16k/16bit/mono capture 已接 RMS VAD；tkl_kws.c 在目标 pin 仍为 NOT_SUPPORTED，WL82 SDK KWS 库是待验证候选。使用独立主仓及 platform/JIELI worktree，后者按 canonical pin，保护 dirty；不要与任务 07 并行改 tkl_audio.c/tkl_vad.c/tkl_kws.c 或共享构建清单，先登记代码 owner。分开报告静态检查、构建、离线音频和实机证据。硬件需唯一租约/现场确认端口，不擦配网授权。建议起始值：目标词20次命中≥18、安静静音30分钟0误唤醒、启停100次可恢复；注明待产品校准。任务范围内可本地提交；不得推送/发PR/上传。交付 diff、SHA、样本/条件、日志、未验项。
```

本文件只保存派发提示词；尚未执行任务 02。
