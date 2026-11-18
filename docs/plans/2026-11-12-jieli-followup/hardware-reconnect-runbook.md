# 791 硬件复接回场清单（2026-11-17 编写）

板卡当前断开（用户要求先完成软件适配）。本清单把“板子回来后的第一场”固定成可重复的顺序，避免每次重新推导镜像、端口和判据。**未执行前，以下各项一律记为待验，不计入验收通过。**

## 0. 开工前置

- 现场确认板卡 revision、USB/串口枚举（**不沿用历史 COM8**），单人独占登记到 `hardware-ledger.md`。
- 镜像从 `images/` 取，烧录后核对 SHA；`tools/flash-791.ps1` 已内置各 preset 的 SHA。
- 当前分支基线：应用 `camera/tdd-refactor`（= PR #2，`8304d183`：DVP 满屏 + SPI 几何保护）、平台 `camera/ac791-clean`（= PR #47，`6562c41`，含 660f2f59 的实机修复与 6562c41 的生命周期/内存加固），显示 TE 修复另见 fork PR #6（`83c7407a`）。

## 1. SPI 摄像头（05，J4，重点：紫/粉偏色复验）

- 只接 J4 模组。**先烧 `spibase`**（`spi-base2-1444db37.bin`，430,692 B，SHA `1444db37…`）：当前基线的干净重建镜像（GC0310 表已对齐厂商 304 条、几何保护、显示 TE 修复、平台 `6562c41` 的 SPI 内存修复、默认 YUYV）。它取代 `spitddd`（`bda963a3…`）作为本轮首烧。
- 串口判据：`[CAM] app_main entry` → `first frame 640x480 len=614400`；持续 ≥60 s，行/帧 ≈480，`tag_err` 不累积，无复位。
- **画面判据（本次核心）**：颜色与厂商 demo 一致，不得偏紫/偏粉；无撕裂、无错位行。表已改为“仅厂商实际执行的 304 条写”，属紫/粉问题的直接修复，需目视重新确认。
- 观察 tag 错误恢复：人为扰动/长时间运行后出现 `SPI: tag error N, resyncing` 时应自动恢复出帧（`6c36e54f`）。
- 若 `spibase` 仍偏紫/粉：再烧 `spiyuv`（`spi-yuvbi3-ac4f2c39.bin`，431,364 B，SHA `ac4f2c39…`）——单次烧录完成顺序二分：同一路画面每 10 帧（≈2.3 s）在 `YUYV`/`UYVY` 间切换，串口同步打印 `[CAM] yuv_order=<order> frame=N`。
  - 寄存器回读（本次关键证据）：`[CAM] init page1 awb 8b..90 = 00 00 00 00 00 c9`（AWB 块在 **page 1**；`0x90=0xc9` 是稳定 AWB）与 `[CAM] init page0 pll f7..fa = 88 00 4f 32`、`page0 8f..90 = aa 8c`（证明页选与实际落写）；TDD 在 SPI 武装后会重启传感器，故还有一组 `restart` 前缀的同格式回读，应以 restart 组为准。
  - 目视判据：哪种顺序下颜色自然即取哪种；两种都偏紫/粉则排除字节序。此时若白色物体仍偏色，说明不是 U/V 互换（白色 U=V=128 与顺序无关），按顺序看：① AWB 收敛（厂商丢弃前 4 帧；等 10–30 s 看是否褪色并观察 `tag_err` 是否累积）；② 复位/XCLK 顺序（TDD 在 XCLK 关闭时先复位，厂商先开 XCLK 且两次复位/ID；可改 XCLK 提前与复位释放延时再验）；③ YVYU 变体（现有枚举只有 YUYV/UYVY；若前两步都不成立，再做字节 1/3 交换的临时变体）。
  - 该镜像用临时诊断源码构建（已从分支还原，副本在 `.local-jieli-migration/2026-11-18/`），不要与 PR 内容混淆。
- 证据：串口日志 + 屏幕照片/录屏，路径写入矩阵 05 行。

## 2. DVP 摄像头（05，J1）

- **只接 J1 模组并断开 J4**：两模组同时连接时共用 PA2/PA3/PA4、PH0–PH3 且同 I2C 地址 0x42，vendor video0 探测会读到 GC0310 的 `0xa310` 并误判，随后 `video.c:558` 断言复位——这是 11-17 “一直重启”的已证原因，不是代码回归。
- **先起抓取再烧**：`tools/capture-791.ps1 -Seconds 150 -Port COM8 -Baud 115200`，然后 `tools/flash-791.ps1 -Image <preset>`（板子须为 `WL82 UDISK`/`WL82 UBOOT1.00`；崩溃循环后 USB 不枚举时只能 `UPDATE+RESET`）。
- **按顺序烧 DVP v2 候选**（均为本地构建、已归档；板卡需在 UBOOT）：
  1. `dvpdemo` = `dvp-v2-demo-ed732288.bin`（423,500 B）——厂商 `get_yuv_data.c` 外部缓冲模式（`SET_FMT` 只设 type+format、`SET_OVERLAY(&yuv)`、64 字节对齐）+ `volatile` 字节读/对齐 32 位写转换 + 暂存拷贝；镜像内含 P1–P5 分段打印与每 10 帧 `P5 delivered frame N`。
  2. 若失败，`dvpprobe` = `dvp-v2-probe-f9040c98.bin`（旧混合模式分段：P2 整缓冲校验和、P3 memcpy、P4 staging 转换，各带 ms）；若 P2/P4 暴露出"抓取线程停留越久越容易崩"，改烧 `dvpdecouple` = `dvp-v2-decouple-a4b06822.bin`（423,344 B，厂商外部缓冲模式 + 采集任务只做 GET/拷贝/CLEAR、独立 `cam_proc` 任务做转换与投递）。
  3. 若整机卡在 `[EMI]emi open success`（显示初始化，不属于相机代码），改烧 `dvpnodisp` = `dvp-v2-nodisp-41664aaa.bin`（415,120 B，跳过 `__display_init`，只验证相机链路；预期 `[CAM] display init skipped (diag)` → `camera init rt=0` → `[camera] delivered 20 frames…`）。
  4. 若读走整帧后下一次 GET 就崩（`dvpprobe` 的 P1 后即复位），先烧 `dvpstatic` = `dvp-v2-static-e07ebdb7.bin`（423,408 B，overlay 缓冲改为 153,600 字节 32 对齐 BSS 静态数组，匹配厂商 demo 的存储属性）。
  5. 再失败，`dvpstage` = `dvp-v2-stage-a4e97b15.bin`（仅暂存拷贝）、`dvpclear`/`dvpfix` 作对照；`dvphead`（`dvp-head-68a633ea.bin`）是 v1 基线，实机已知 `first frame` 后 `isc bandwith err`→`axi_rd_inv`。
- 判据：`display init rt=0`；`sensor: GC2145`、`320x240 fps=20`；P1–P4 逐段通过且无 `exception`；出现持续 `P5 delivered frame N`；屏上出现画面（示例按 YUYV→RGB565 刷屏）。
- 已知线索：`dvpfix`/`dvpclear` 实机的异常 PC 已符号化到厂商 `isc_sen_isr0`（ISP 传感器中断）与 `wl80_video1_ioctl`，属 ISP 缓冲/时序问题，不是单纯转换代码错误。
- 证据同上；若仍复位，先记录探测到的 sensor 名与最后一条 `P?` 标记再判断。

## 3. VAD（02）

- 烧 `audiovad`：`audio-vad-ef600bb8.bin`，SHA `ef600bb8…`。
- 判据：≥9 分钟连续运行，单位计数按 20 ms 递增，无异常/复位。

## 4. KWS（02）

- 烧 `audiokws`：`audio-kws-de71c3cc.bin`，SHA `de71c3cc…`。
- 判据：启动无 constructor CHECK / `VIR_EXIT`（TAL 引擎 arena 44 KiB）；单位计数递增。
- **产品词「你好涂鸦」token 反推（本场必须做，做完产品词才有下一步）**：烧厂商 40-token 探针 `taltokens`：`tal-tokens-f0a4675a.bin`，624,176 B，SHA `f0a4675a…`（源自平台 02 工作区 `apps/demo/tal_probe_tokens`：nhty 模型强符号替换 + SAPI「你好涂鸦」PCM + 40 类 × 每批 15）。串口收集 `[TAL_TOKENS] HIT ... idx= start= end= score=`，按 start/end 顺序得到 token 序列；把序列写进 `tkl_kws` 的 `keyword_tflite_add_keyword`，再复测真人发音。
- 在 token 序列和真人命中都拿到前，KWS 只能标“引擎运行正常”，不标“唤醒成功”。

## 5. OTA A/B（03/04）

- 仅在以下两选一确定并**单独授权首次改分区**后执行：① 16 MiB 免裁；② 8 MiB 裁掉 `audlogo`+`ui_res`（离线矩阵已证可通过）。
- 执行前：整片备份 + 记录 FLASH INFO；流程与失败注入按 `2026-11-13-status.md` 的 03/04 报告执行；禁止上云/发布。

## 6. 触摸（10）与外围（09）

- 触摸按 README 硬件队列（语音之后）单独预约。
- 09 PWM/UART 波形需仪器，另行约时；无仪器不做结论。

## 7. AC792N 摄像头（06，仅构建级已验，实机为新增项）

- 镜像：应用分支 `camera/ac792-camera-adapt`（`cb7027ee`，PR #809）+ 平台 `camera/wl83-dvp`（`b5e75d22`，PR #52，叠 #51/#47）；示例 `examples/peripherals/camera/output_display` 选 `JIELI_AC792N_Develop_Board_dvp.config`（J49 DVP0，JPEG 输出）。最新构建产物 558,572 B / `f0c4c5b8…`（构建级，非本场烧录记录）。
- IIC 引脚已是板级默认（IIC1=PE8/PE7、IIC2=PE14/PE13），无需在示例配置里覆盖；TKL 只接受 JPEG/JPEG_YUV422_BOTH，请求 YUV422 会在 open 时直接报错而不是无帧。
- 只接 J49（DVP0）；J47（CSI-2）与 DVP0 共用 PD11–PD14，不能同时接。**不要同时打开触摸**：camera 上电用 PE6，而 PE6 是触摸 RST。
- 串口期望：`[camera] DVP0 started (640x480)` → `[camera] first frame: 640x480 jpeg len=...`；屏上应出现解码后的画面（示例用 `tal_image_jpeg_decode_rgb565`，JPEG 路径不做旋转）。若出现 `jpeg N B exceeds the frame pool`，说明编码帧超出 TDL 池，属被丢弃的坏帧。
- 若无帧：先查 sensor 供电/复位时序（PE6 LDO、PD8 XCLK、PE9 RESET）与 IIC1（PE7/PE8）地址 0x21 探测；`server_open video1.1` 失败或 `VIDEO_REQ_REC` 非 0 会直接打印。
- CSI-2 单独构建：同一示例改临时配置（`CONFIG_ENABLE_CSI2=y`、`CONFIG_ENABLE_BOARD_CAMERA_CSI2=y`、`CONFIG_CAMERA_NAME="camera_csi2"`、关 DVP、保留 `CONFIG_EXAMPLE_CAMERA_JPEG=y`），在推送末版上重建通过（556,716 B / `9fc070ff…`）；staging 会把 UART0 RX 停到 -1 给 PE11（PWDN）让路，并把 `"video0.*"`/IIC2=PE13/PE14 写入板级。实机未验。
- 判据：有效帧 + 屏显至少 60 s 无复位；只到构建或只到“串口 first frame”不构成出图验收。

## 交接

结束时记录：板卡 revision、COM、操作者、所烧镜像 SHA、日志路径，并释放占用。任何未实际执行的步骤写“未运行”。
