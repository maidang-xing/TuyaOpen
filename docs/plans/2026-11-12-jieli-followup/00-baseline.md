# 00 — 新 root 与平台基线复核

**优先级：** P0；后续实现任务开工前复核。  
**状态：** 2026-11-13 迁移后基线核对已完成；本书用于未来刷新，不要求等待 PR #799 合并。  
**限制：** 只读核对，不合并/推送 PR，不构建、不操作硬件。

## 当前基线记录

- TuyaOpen 保留根：`D:\tuya_proj\TuyaOpen`，upstream/master 当前确认 `055042a8119b3a21910761bc13f171da19cf35fc`（#800）；PR #799 已合并。若 HEAD 变化，记录新 SHA 和来源，不因旧 open/head 记录阻塞。
- 平台仓 `platform/JIELI` 为独立 clone，当前 checkout/pin 为 `c967f4b174480ff1df966eeea80bee1b0edcb0f6`，与 `platform/platform_config.yaml` 一致，clean、无 alternates；含 #45 Windows PI32 工具发现修复。
- 主仓/平台历史 dirty、refs/stash 已由迁移流程保全于 `.local-jieli-migration/2026-11-13/`；详见 [状态与迁移记录](2026-11-13-status.md)。该目录只作本地备份，不当作代码 worktree。

## 未来刷新步骤

1. 在新 root 确认 upstream、当前 master SHA、`git status`、worktree 和本地备份；不进入旧 `D:\tuya_proj\TuyaOpen-JieLi\TuyaOpen`。
2. 读取 YAML platform pin；单独核验 `platform/JIELI` checkout、remote、HEAD、dirty、alternates。平台源码任务另在独立 worktree checkout 精确 pin。
3. 针对本次所派任务复核相关源码/配置入口及最近改动，更新本目录对应任务书的事实和依赖；记录所有已有 dirty/untracked 并保护。
4. 返回 master/pin SHA、证据文件/行号、dirty 清单、漂移和可启动任务。PR 状态仅在任务依赖它时复查，不自行合并。

## 验收与交付

基线通过只表示仓库来源、SHA、平台 pin 和文件入口已核实；不表示相关代码已构建、烧录或在实机验证。任何漂移都记录新证据，不使用历史 SHA 覆盖当前观察。

## 共用规则

遵循 README 公共约束：各仓库独立管理；保护 dirty、stash、refs 和用户资料；实现任务在新 root 的 `.worktrees/<任务ID>` 隔离；本任务不触碰硬件或远端状态。