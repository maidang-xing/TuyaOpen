# 06 — AC792 摄像头 PR 集成

适用：00 完成；先只读重查 PR 状态和依赖。公共 camera 代码需与 05 协调 owner。

```text
请执行任务 06。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\06 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。依据 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\06-ac792-camera-integration.md 复核并本地集成 AC792 摄像头配套变更。重新查 TuyaOpen PR #760、JieLi PR #23 当前 state/head/base/diff；既有记录是 #760 closed未合并、#23 open，不能按旧 pin a368 开工或假定可直接 cherry-pick。当前平台 pin 是 c967f4b…；此前核对该 pin 未含 tkl_dvp/csi2。优先评估并接通 J49 DVP0，J47 CSI-2 单独验证；显示 MIPI-DSI 不算 CSI camera。主仓和平台各用独立 worktree，保护 dirty；与 05 声明单一 camera API 代码 owner，避免并改共享文件。先核对板版次、sensor、时钟/复位、I2C、lane/DVP route，再构建并分别记录 sensor probe/有效帧；没有硬件帧就标未验证。硬件只在单人租约后测，不接线猜 pin。允许范围内本地集成/提交；不推送、改 PR 或合并远端。交付 PR 状态证据、SHA、diff、构建与帧结果。
```

本文件只保存派发提示词；尚未执行任务 06。
