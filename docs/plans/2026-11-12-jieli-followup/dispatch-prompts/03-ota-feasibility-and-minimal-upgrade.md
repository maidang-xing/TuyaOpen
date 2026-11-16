# 03 — OTA 可行性与最小升级

适用：00 完成。非硬件包/源码评估可并行。

```text
请执行任务 03。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\03 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。按 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\03-ota-feasibility-and-minimal-upgrade.md 验证 AC791/AC792 OTA 能力并准备最小安全升级。重查当前 canonical pin、板版次、flash map、包格式和镜像尺寸；旧记录的 dualbank 默认值、0xF0000 整包限制和历史 chatbot/bank 尺寸都须在目标平台重新确认。不得默认打开 dualbank、改 layout、擦授权/配网或烧录布局变更镜像；首次改 layout、云端上传/发布必须先有该动作的明确授权。仅在独立 worktree 修改任务范围内文件，保护 dirty；工具/设备不可访问时给可复现补丁或缺口。分层验证配置、构建、包生成、烧录、云下载和设备升级，未实测的层明确未验证。实机先申请唯一硬件租约，现场核实板卡/端口并留恢复方案；不与同板任务并行。交付 map diff、包/版本/固件 SHA、日志、恢复及授权边界；不推送、不创建或更新远端 PR。
```

本文件只保存派发提示词；尚未执行任务 03。
