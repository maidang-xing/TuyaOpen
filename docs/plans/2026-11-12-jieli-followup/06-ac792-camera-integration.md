# 06 — AC792N 摄像头集成与既有 PR 复审

**优先级：** P2。
**依赖：** 00 基线；先对既有 TuyaOpen 与平台 PR 做只读状态/差异复审。硬件验证在目标 camera sensor 和板卡可用后单独预约。

## 目标与当前证据

恢复并集成既有 AC792N 双摄变更，以 DVP 为优先交付：主仓 TuyaOpen PR #760（https://github.com/tuya/TuyaOpen/pull/760）与 JieLi 平台 PR #23（https://github.com/tuya/TuyaOpen-JieLi/pull/23）（#760 曾 closed 未合并，#23 曾 open 未合并）需配套复审并在隔离本地工作区集成。历史 head 分别为 `849ede8d221736f9273f53c70248e39e033e26c1` 与 `b93ceb0e8ded5cedcadf8f917f5b8f9f4c48313d`；派发时重查状态/commit。canonical pin 为 `c967f4b174480ff1df966eeea80bee1b0edcb0f6`；该 pin 静态审计未见 `tkl_dvp`/`tkl_csi2`，派发时应以实际 checkout 复核。AC792N Develop V1.21 原理图：`platform/JIELI/chip/wl83/AC792_SDK/doc/硬件资料/原理图/开发板/AC792N开发板原理图位号图/AC792N Develop V1.21 原理图.pdf`；摄像头接口资料：`platform/JIELI/chip/wl83/AC792_SDK/doc/硬件资料/AC792N系列摄像头接口说明-V1.0-20240725.pdf`。用于核对 J49 CAM2 DVP0、J47 CAM1 CSI2，并对照实板版次。

用户本项优先目标是 AC792 开发板的 DVP 摄像头：J49 DVP0 必须恢复并验收。J47 CSI-2 是独立路径，需单独核对 PR #23 的平台实现、验证 sensor/link 后分别验收。MIPI-DSI 显示链路不等于 CSI-2 摄像头链路，不得用显示 TDD 作为 camera 已集成证据。

## 允许修改范围

- 本任务范围包含复审两个 PR、在主仓和平台仓各自独立 worktree 本地恢复/集成配套变更；允许本地 cherry-pick/commit，不允许远端写。
- 可改 `src/peripherals/camera/`、必要的 `boards/JIELI/AC792N_Develop_Board/`、最小 camera 示例，以及 `platform/JIELI/tuyaos/tuyaos_adapter/` 和对应 WL83 vendor staging/driver；DVP 优先，再单独处理 CSI-2。
- 不删改无关 vendor SDK 文件；两个 worktree 不得共用或覆盖 dirty checkout。

## 步骤与验收

1. 更新 PR #760/#23 状态、head/base、评论、changed files 和依赖；记录变更是否可安全恢复。
2. 在两个隔离 worktree 本地恢复/集成主仓和平台变更，先补齐 J49 DVP board routing、sensor driver 与应用调用，记录本地 commit SHA。
3. 核实实板 revision、DVP sensor、供电/复位、sensor I2C、DMA/分辨率；另核实 J47 CSI-2 lanes/clock 与对应平台 driver。资料冲突时暂停该路径。
4. 分别验收 DVP 与 CSI-2：sensor probe、stream、有效帧、分辨率/帧率/错误统计；若产品目标要求并发，再验证同步采集、丢帧、内存与 30 分钟稳定运行（建议起始值）。构建不能代替帧验证。

**通过条件：** 两仓配套变更在本地隔离工作区集成；J49 DVP 优先路径实机输出有效帧；J47 CSI-2 分别给出构建和有效帧结果，不能以 DVP 结果代替。没有实机帧时仅可标 API/构建部分验证。

## 停止条件与交付

PR 无法访问、HEAD 变化造成对应关系不明、硬件 revision 不符、没有 sensor/原理图依据或平台 checkout dirty 无法隔离时停止修改。交付 PR 状态/链接、sha/依赖表、缺口、worktree/分仓计划、测试证据与未验证项。

## 共用规则

遵循 `README.md`：先核实 00；主仓和 `platform/JIELI` 各自管理 dirty 和独立 worktree，不能让多 agent 共改一份嵌套仓库。任务范围内允许本地 cherry-pick/commit；推送/PR 更新/合并仍需明确授权。硬件/串口独占并交接，端口现场核实。

