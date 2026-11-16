# 04 — chat-bot OTA 与恢复

适用：00 完成且同芯片任务 03 已通过相应 OTA 阶段。

```text
请执行任务 04。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\04 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。遵循 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\04-ota-chatbot-and-recovery.md，并先确认任务 03 对同芯片的升级包、分区和启动路径已有可复核结果；没有则完成可独立的容量/恢复准备并报告依赖，不跳过验证。重新测实际 chat-bot bin/package、bank、资源分区和下载限制；旧 2.46 MiB 与约 1.42 MiB 仅为历史参考。仅在独立主仓/platform worktree 中改最小应用与 OTA 相关代码，保护 dirty；不得默认改分区、擦授权/配网、首次切 dualbank、断电注入或云上传。首次 layout 变更、云写/发布需具体明确授权。按层记录构建、包校验、下载、启动新版本、配置保留、坏包/中断/断电恢复；每一步未实测就标未验证。硬件需唯一租约、现场确认端口和恢复镜像，且等待同板任务显式释放。交付尺寸与容量证据、包/固件 SHA、错误路径日志和安全边界；仅本地提交，不做远端写。
```

本文件只保存派发提示词；尚未执行任务 04。
