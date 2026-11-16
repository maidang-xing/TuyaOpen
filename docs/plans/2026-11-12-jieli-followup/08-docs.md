# 08 — TuyaOpen.io JIELI 文档收尾

**优先级：** P2。  
**依赖：** 00 基线；硬件、OTA事实须来自已核实的对应任务，不能先行写成已验证。

## 目标与当前证据

整理并发布准确、互相一致的 JIELI 板级文档，包括外设映射、OTA 默认配置/包格式/容量边界、触摸控制器/接口/方向映射及明确未验证项。文档仓：`D:\tuya_proj\TuyaOpen.io`；主要页面在 `docs/hardware/jieli/` 和 `i18n/zh/docusaurus-plugin-content-docs/current/hardware/jieli/`。

PR #276 `https://github.com/Tuya-Community/TuyaOpen.io/pull/276` 已合并，包含两板英文/中文外设映射。PR #279 `https://github.com/Tuya-Community/TuyaOpen.io/pull/279` 已 closed 且未合并（任务书基准时 open、mergeable=false），目的为移除索引中 JIELI 入口，映射页不变；先核查当前状态和维护者意图，不擅自恢复或移除链接。本地 `md/jieli-peripheral-mapping` 分支包含 OTA 分区及 UART1 更新；当时本地 HEAD `fb99df5`，fork remote 分支 `dd39b88`，没有发现这些更新对应的 open PR。版本可能漂移，实施前必须复查。

## 允许修改范围

- 文档仓须使用独立 worktree；只允许修改本任务明确的 JIELI 英文/中文页面和 sidebar/index 引用。
- 不覆盖当前主仓/文档仓 dirty；不移动/还原 PR #279 中索引变更，不修改 manifest submodule、全站文档、日志或无关 SEO 内容。
- 可运行文档构建和链接校验；不得推送、创建/更新/合并 PR 或发布网站，除非后续另外明确授权。

## 步骤与验收

1. 查询 PR #276/#279 当前状态、mergeability、diff、评论；核对文档 branch 的 base/head 与 origin master，再决定 cherry-pick/rebase 方案，不直接把旧分支当作新 PR base。
2. 从 03/04/09/10/11 的验收证据同步 OTA/分区/外设/触摸状态；触摸显示控制器、手势/坐标旋转和实测状态须与任务证据一致，不能由屏幕显示能力推断触摸已通过；用“已实现”“已在板验证”“未验证”分层表达。说明 dual bank 默认关闭、layout 变更需重烧录、vendor OTA 包格式、`0xF0000` 为整包上限而非 4 KiB chunk、AC79 map 的不确定性。未实测 rollback/power loss 不写成具有自动回退。
3. 双语对照板名/芯片名/引脚/范围/单位/默认值/命令；只改对应页面和获准导航链接。确认删除索引链接的用户意图后再协调 #279。
4. 运行最小 Markdown/链接检查或文档构建，报告准确命令和输出路径；只写成功实际生成的页面，不推断上线。

**通过条件：** #279 意图得到处理；主仓实现事实与页面一致；英/中文关键表格/链接一致；文档构建与本地链接检查通过；没有把静态/构建证据冒充实机 OTA。远端 PR/发布是独立动作，不属本任务默认通过条件。

## 停止条件与交付

PR #279 意图冲突、板级证据与当前代码不一致、OTA 事实未验证、文件已被他人修改无法安全合并时暂停修改。交付文档 diff、构建日志摘要/产物路径、PR 状态及未解决问题。

## 共用规则

遵守 `README.md`：主仓、平台仓、文档仓都重新检查 dirty，各自独立隔离；已有 index、manifest submodule 和日志保持原样。硬件事实需注明测试设备/版次。构建不等于发布；未经单独授权不推送、创建/评论/合并 PR 或发布网站。

