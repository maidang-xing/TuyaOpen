# 03 — JIELI OTA 可行性与最小 A/B 升级

**优先级：** P1。  
**依赖：** 00 基线；必要时可与 01/02 的非硬件准备并行。硬件使用排在 AC791 语音闭环后。

## 目标与当前证据

区分已有云端 OTA 服务、TAL/TKL 接口和 vendor 双备份引擎，并验证至少一个小型 TuyaOpen 应用在安全确认的 A/B 分区下完成真实云端升级。

历史 pin `a3687a42` 的 OTA 代码/容量结论不能代替当前平台。当前 canonical JIELI pin 是 `c967f4b174480ff1df966eeea80bee1b0edcb0f6`；实施前重新定位 `tuya_ota.c`、`tal_api.c`、`tkl_ota.c`、分区默认值与包格式。过往记录提及 dual-bank 默认关闭、整包上限 `0xF0000`、4096 B chunk 和 vendor package；须在最新源/构建配置/实测包逐项复核，不能假定仍成立。首次更改 layout、烧录双 bank 镜像或云端上传必须有该动作明确授权，并先给 flash-map diff 与恢复方案。

## 允许修改范围

- 仅在独立 worktree 中新增最小应用/构建配置或 OTA probe；修改已有 `app_default.config` 必须先隔离副本，不覆盖当前用户 dirty 配置。
- 双 bank 配置先在临时/独立配置中评估。首次切换真实板分区或烧录双 bank 镜像，必须明确包含在本次任务派发范围内；派单未包含时不得做。切换前输出 flash-map diff、备份/恢复步骤和配网/授权信息保留方案，不得默认擦除已有配网信息。
- 可按后续授权修改 JIELI TKL/主仓 OTA glue；不能改变默认分区或宣称 OTA 支持已普遍开启。

## 步骤与验收

1. 确认实际合并 SHA、board revision、vendor flasher/SDK 版本、单 bank 与双 bank `FLASH INFO`、所有 partition 边界、bootloader/VM/保留区。AC79 双 bank 数值必须从真实 flasher 输出核对；不要照抄 WL83 数字。首次更改分区/烧录仅在任务派单明确授权时执行，并先确认可保留授权与配网信息的刷写方式。
2. 从 vendor OTA 工具生成真实全量更新包，解析包大小是否不超过 `0xF0000`，确认 Jieli wrapper、目录和 CRC；记录包 SHA。对比裸 image、Tuya Cloud firmware upload 格式与 vendor engine 实际输入，确认中间没有格式转换缺口。
3. 选择小型最小应用，确认实际 image/package 适配目标 bank 和 TuyaOpen 数据区。做版本 A→B；验证下载、TAL/TKL start、逐块写、vendor verify、boot info 写入、重启、版本上报和 KV/授权/配网数据保留。
4. 单独注入不支持包、超长包、差分包、传输中断、写入失败、verify fail、重启前后断电等失败场景。只记录实测行为；不得把 vendor 文档推断成 rollback 已实现。
5. 分别在 WL82 和 WL83 验证；测试结论不能由单芯片推及另一芯片。

**通过条件：** 每块板实际 flash map 与工具输出一致；全量包格式与 size ceiling 明确；最小应用至少完成云端 A→B，并确认设备启动新版本；坏包/中断结果和用户数据安全边界有实测记录。否则把阻断项分类，不标“OTA 完整支持”。

## 停止条件与交付

无真实 vendor 包、目标 bank 未核实、双 bank 会覆盖未知数据、分区差异不明或恢复路径不可用时停止，不升级目标设备。交付 config 与 map、包 SHA/大小/格式、分阶段日志、失败注入结果、固件 SHA、数据保留与恢复步骤。烧录/云上传/发布必须在单独任务中明确授权。

## 共用规则

遵循 `README.md`。先 00，保护主仓与平台嵌套仓 dirty；分别使用隔离 worktree。硬件队列在 02 之后并单人独占，串口现查，不沿用 COM21。提交/推送/上传云固件/发布升级都属于外部状态写入，需单独授权；此任务书不自动授权。

