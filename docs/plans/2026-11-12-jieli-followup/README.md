# JIELI 后续工作与派发索引

本目录把 PR #799 合并后的剩余固件、硬件和文档工作拆成 12 个可独立派发任务。每个提示词含一段完整 `text` 代码块，可直接复制给本次用户指定的 agent 工具；不限定模型或 MCP。任务执行者须读各任务书和适用 `AGENTS.md`，用户明确派发后在范围内直接实施，不只返回计划。

## 当前基线（2026-11-13）

- 唯一后续任务基线：`D:\tuya_proj\TuyaOpen`，upstream/master `055042a8119b3a21910761bc13f171da19cf35fc`（#800）。2026-11-16 复核：`origin/master` 已到 `530aeafd`（仅 4 个 CI 提交，未拉取）。2026-11-17 复核：`origin/master` 已到 `2022c929`（`530aeafd..2022c929` 仅 2 个 T9 AEC 提交，未触及 JIELI/camera/ota/audio 路径）；平台 `master` 仍为 pin `c967f4b1`。PR #799 已合并；旧 HEAD、旧平台 pin 和旧目录只作历史，不阻塞工作。
- `platform/JIELI` 是独立 clone，clean、无 alternates；当前 checkout `c967f4b174480ff1df966eeea80bee1b0edcb0f6`，与 `platform/platform_config.yaml` pin 一致（#45 Windows 工具链修复）。JIELI Windows PI32 工具链已在该 pin 的 tracked 路径 `platform/JIELI/tools/toolchains/windows/pi32`；Linux 缓存可重建。
- 00 的迁移基线核对已完成；本轮未做构建、烧录、串口或实机测试。每个任务派发时仍要核查基线漂移和 dirty。
- 主仓、平台仓和文档仓是独立 Git 仓库。先确认 `D:\tuya_proj\TuyaOpen\.worktrees` 已由本地 exclude 忽略；否则仅向 `.git/info/exclude` 添加本地规则（不改 tracked ignore）。编码任务在 `D:\tuya_proj\TuyaOpen\.worktrees\<任务ID>` 建主仓 worktree；平台另行 checkout 当前 YAML pin。任务 08 的 worktree 必须从 TuyaOpen.io 自己的 master 创建，可放在上述 `.worktrees\docs-08`，不得把 TuyaOpen SDK master 当文档仓基线。

## 任务与提示词

| ID | 任务书 | 可复制提示词 | 依赖 / 并行说明 |
|---|---|---|---|
| 00 | [基线复核](00-baseline.md) | [00-baseline](dispatch-prompts/00-baseline.md) | P0；迁移核对已完成，后续开工前刷新；不合并 PR。 |
| 01 | [AC791 音频库可行性](01-ac791-audio-feasibility.md) | [01-ac791-audio-feasibility](dispatch-prompts/01-ac791-audio-feasibility.md) | P1；先做 TAL bitcode/PI32 最小探针，可与非硬件资料准备并行。 |
| 02 | [AC791 语音唤醒](02-ac791-voice-wakeup.md) | [02-ac791-voice-wakeup](dispatch-prompts/02-ac791-voice-wakeup.md) | P1；接续 01；语音适配单一代码 owner。 |
| 03 | [OTA 可行性与最小升级](03-ota-feasibility-and-minimal-upgrade.md) | [03-ota-feasibility-and-minimal-upgrade](dispatch-prompts/03-ota-feasibility-and-minimal-upgrade.md) | P1；非硬件评估可与 01 并行。 |
| 04 | [chat-bot OTA 与恢复](04-ota-chatbot-and-recovery.md) | [04-ota-chatbot-and-recovery](dispatch-prompts/04-ota-chatbot-and-recovery.md) | P1；依赖同芯片 03 通过相关阶段。 |
| 05 | [AC791 双摄](05-ac791-dual-camera.md) | [05-ac791-dual-camera](dispatch-prompts/05-ac791-dual-camera.md) | P2；先验证实板拓扑；与 06 共享 camera owner。 |
| 06 | [AC792 摄像头集成](06-ac792-camera-integration.md) | [06-ac792-camera-integration](dispatch-prompts/06-ac792-camera-integration.md) | P2；重查 #760/#23；与 05 共享 camera owner。 |
| 07 | [AC792 语音](07-ac792-voice.md) | [07-ac792-voice](dispatch-prompts/07-ac792-voice.md) | P3；WL83 可先只读准备；实现等 02 代码交接。 |
| 08 | [TuyaOpen.io 文档](08-docs.md) | [08-docs](dispatch-prompts/08-docs.md) | P2；可立即盘点/写草稿；文档仓需用自身基线。 |
| 09 | [外围硬件回归](09-peripheral-hardware-regression.md) | [09-peripheral-hardware-regression](dispatch-prompts/09-peripheral-hardware-regression.md) | P2；AC792 PWM 与 AC791 UART 分时预约。 |
| 10 | [AC791 触摸](10-ac791-touch.md) | [10-ac791-touch](dispatch-prompts/10-ac791-touch.md) | P2；与 11 共享 touch/LVGL owner；AC791 语音后排硬件。 |
| 11 | [AC792 触摸](11-ac792-touch.md) | [11-ac792-touch](dispatch-prompts/11-ac792-touch.md) | P2；与 10 协调 owner；AC792 需单独确认设备。 |

## 状态摘要

详见 [验收矩阵](acceptance-matrix.md) 与 [2026-11-13 状态记录](2026-11-13-status.md)（含 2026-11-16/17 各段追加）。截至 2026-11-18：05 的 SPI 由 TDD 配置 GC0310（`0xa310`）、≈4.84 fps、`tag_err=0` 长稳，SPI 镜像可从 PR 头逐字节重建；SPI 紫/粉偏色待目视复验；DVP 单模组 v2 实机已定位崩溃发生在厂商 ISP 中断路径（`isc_sen_isr0` / `wl80_video1_ioctl`，自动化地址符号化），根因指向 overlay 缓冲混合模式与转换时序，根因已确认并**实机打通**：overlay 目标缓冲必须是厂商式 32 字节对齐静态数组（BSS），堆缓冲会触发 ISP 中断 AXI 异常；`dvpstatic` 连续 91.6 s/2700 帧/0 丢帧/0 异常，`dvpfill`（`ed1b448e…`）已由用户目视确认满屏预览；最终代码已推送（平台 `660f2f59` + 评审加固 `6562c410` → PR #47；应用 `845048d2` + 几何保护 `8304d183` → fork PR #2），显示 TE 修复 `83c7407a` 单独成 PR maidang-xing/TuyaOpen#6；SPI 偏色的单次烧录二分镜像 `spi-yuvbi3-ac4f2c39.bin`（`spiyuv` 预设，交替 YUYV/UYVY、page1 AWB/page0 PLL 回读）与生产基线 `spi-base2-1444db37.bin`（`spibase`）已备好，待只接 J4 模组后实机判定。02 的 VAD 9 分 08 秒实机通过，KWS 引擎运行通过；产品词缺 token 序列（nhty 模型 + 40-token 探针已备 `taltokens` preset，**未上板**）。03/04 离线容量矩阵完成：8 MiB 双 bank 需裁 `audlogo`+`ui_res` 或改 16 MiB，**未烧录**；04 另有两处本地 OTA 修复（平台 `f288868a` 写失败判定、应用 `7022af27` 数据失败上报）：两处均已整镜像构建链接通过（AC79 your_chat_bot；应用版 2,267,592 B / `ae288e19…`，含平台修复版 2,267,656 B / `e302082b…`），已推送 PR #50/#810；并在「03 平台 + 打开 `CONFIG_JIELI_BOARD_DUAL_BANK`」的组合下也整镜像构建通过（2,267,752 B / `c4ad96c5…`，staged 双 bank 宏生效），A/B 行为仍未实机验证；未烧录/未上云。06 仅构建级：WL83 DVP 后端在 `camera/wl83-dvp`，示例按 JPEG 打开并解码显示；评审后修正了 AC792N IIC 引脚（原为 PA6/PA7、PA2/PA3，sensor 会走错脚）、TDD 帧池回收、截断 JPEG、CSI-2 UART0 RX 冲突等 8 项；AC792N DVP（558,572 B）、CSI-2（556,716 B）与 WL82 SPI 回归（420,540 B，同基线尺寸）均构建通过；已推送 PR #52（末版 DVP 558,572 B / `af91aefb…`，CSI-2 556,716 B / `84fa41b6…`）；应用侧 #809 已于 2026-11-18 关闭且未发现替代 PR，06 仍只有构建级证据；实机未验；PR #23 仍 CONFLICTING（新实现不再依赖它）。07/09/11 缺硬件或仪器；11 的两处本地修复 `ec706983` 已按 `tp_test` 配置重建整镜像通过（2,596,056 B / `cc822e5b…`），仍无实机证据；07 产品词路线 B 的 WL83 链接探针已通过（WAKEUP 2,967,456 B / `6b08e377…`，路由决策：用户已决定 2026-11-18 起先不做 AC792 语音内容，07 暂停、探针只留档）；08 本地构建通过未发布。PR：平台 #47/#48/#49/#50/#51/#52、应用 #2/#4/#5/#810 已推（#809 于 2026-11-18 关闭，无替代）；2026-11-18 新增应用 #6（显示 TE 引脚修复）并更新 #47/#2 正文为 DVP v2 实机验收结果（2026-11-18 复核：新推 PR 中 #47/#2/#4/#5/#6/#810 均 OPEN + MERGEABLE，#809 已关闭；#51 叠在 #47 分支上，#52 再叠在 #51 上）。checks：平台 #47–#49 仅 Git Repo Sync=SUCCESS，#46 无 checks，应用侧 review/Git Repo Sync 均为 SKIPPED——两仓库都没有构建型 CI，不能以“CI 绿”代替构建/实机证据；#23 仍 CONFLICTING（新的 WL83 实现走 #52/#809，不再依赖 #23）。硬件复接顺序见 [hardware-reconnect-runbook.md](hardware-reconnect-runbook.md)。

## 并行、共享代码与硬件

- 00 为唯一共享基线检查；01/03 可做互不冲突的只读/隔离工作。02 与 07 共用 `tkl_audio.c`、`tkl_vad.c`、`tkl_kws.c` 及构建清单，必须串行或先指定唯一代码 owner；AC791 完成后再将接口交接给 WL83。
- 05/06 共用 camera API 和平台 adapter，资料工作可并行，修改公共代码需单一 owner；10/11 共用 touch/LVGL 入口，同样先定 owner。04 依赖 03；08/09 可分别准备，但文档只据验收证据更新结论。
- 建议 791 硬件队列：语音 → 触摸 → 最小 OTA → chat-bot OTA/恢复 → 双摄；09 如插队须显式交接。792 的触摸、camera、voice、PWM 分别预约，不能假定板已连接。开始前现场核实板卡 revision、端口和操作者，单板/串口单人独占；结束后记录 SHA 并释放。无硬件证据就标未验证。
- 每次派发都保护已有 dirty、stash 和未跟踪用户文件；不覆盖、不 reset/clean。确认执行 agent 能访问同一机器/项目，不能访问则返回补丁和缺口。任务范围内普通本地修改、相关检查和本地提交可直接执行；首次改变 flash layout、擦除配网/授权等高影响动作按对应任务边界先落实数据保全/恢复；推送、PR、合并、云上传或发布需该次派发明确授权。
- 本目录任务书和提示词是待派发计划，不是实现或验收记录。静态审查、构建、烧录、真实输入/输出、云端闭环和断电恢复分层汇报；未实际做的步骤写“未运行/未验证”。

## 迁移资料

任务书与提示词已迁入本目录；两份语音输入材料迁入 `D:\tuya_proj\TuyaOpen\.local-jieli-migration\2026-11-13\voice-inputs\`，后续任务 01 使用新路径。旧仓的 refs/stash 与其它迁移备份在 `.local-jieli-migration\2026-11-13\`；旧目录/缓存清理仍在进行，README 不宣称已清完。不要从旧 `D:\tuya_proj\TuyaOpen-JieLi\TuyaOpen` 开 worktree。