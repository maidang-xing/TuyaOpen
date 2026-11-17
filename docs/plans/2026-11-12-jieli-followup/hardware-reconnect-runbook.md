# 791 硬件复接回场清单（2026-11-17 编写）

板卡当前断开（用户要求先完成软件适配）。本清单把“板子回来后的第一场”固定成可重复的顺序，避免每次重新推导镜像、端口和判据。**未执行前，以下各项一律记为待验，不计入验收通过。**

## 0. 开工前置

- 现场确认板卡 revision、USB/串口枚举（**不沿用历史 COM8**），单人独占登记到 `hardware-ledger.md`。
- 镜像从 `images/` 取，烧录后核对 SHA；`tools/flash-791.ps1` 已内置各 preset 的 SHA。
- 当前分支基线：应用 `camera/tdd-refactor`（= PR #2，最新提交含 GC0310 表修正 `e9a592c4`/`665b25af`）、平台 `camera/ac791-clean`（= PR #47，`6c36e54f`）。

## 1. SPI 摄像头（05，J4，重点：紫/粉偏色复验）

- 只接 J4 模组。烧 `spitddd`：`spi-tdd-bda963a3.bin`，SHA `bda963a3…`。
- 串口判据：`[CAM] app_main entry` → `first frame 640x480 len=614400`；持续 ≥60 s，行/帧 ≈480，`tag_err` 不累积，无复位。
- **画面判据（本次核心）**：颜色与厂商 demo 一致，不得偏紫/偏粉；无撕裂、无错位行。表已改为“仅厂商实际执行的 304 条写”，属紫/粉问题的直接修复，需目视重新确认。
- 观察 tag 错误恢复：人为扰动/长时间运行后出现 `SPI: tag error N, resyncing` 时应自动恢复出帧（`6c36e54f`）。
- 若仍偏紫/粉：离线已排除“传输重排字节/转换实现错误”（见状态记录 2026-11-17 晚 SPI 偏色取证），剩下两条按序二分——① 顺序：临时把 TDD `yuv_order` 改为 `TUYA_YUV422_UYVY` 重编对比同场景画面；② AWB：对照 `gc0310.c` 的 0x8c/0x8d 两组增益块。每种都留屏摄证据。
- 证据：串口日志 + 屏幕照片/录屏，路径写入矩阵 05 行。

## 2. DVP 摄像头（05，J1）

- **只接 J1 模组并断开 J4**：两模组同时连接时共用 PA2/PA3/PA4、PH0–PH3 且同 I2C 地址 0x42，vendor video0 探测会读到 GC0310 的 `0xa310` 并误判，随后 `video.c:558` 断言复位——这是 11-17 “一直重启”的已证原因，不是代码回归。
- 烧 `dvphead`：`dvp-head-68a633ea.bin`，411,433 B，SHA `68a633ea…`（当前 PR 头重建，含错误路径修复；旧的 `dvptdd`/`dvp-4fe36c41` 仅作对照基线）。
- 判据：sensor 名探测成功、有效帧、帧率与配置一致、无复位；画面颜色/几何正常。
- 证据同上；若仍复位，先记录探测到的 sensor 名再判断。

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

- 镜像：应用分支 `task/06-ac792-camera-adapt`（`69137bf9`）+ 平台 `camera/wl83-dvp`（`6bdb310a`）；示例 `examples/peripherals/camera/output_display` 选 `JIELI_AC792N_Develop_Board_dvp.config`（J49 DVP0，JPEG 输出）。对应构建产物 558,220 B / `77dedf11…`（构建级，非本场烧录记录）。
- 只接 J49（DVP0）；J47（CSI-2）与 DVP0 共用 PD11–PD14，不能同时接。**不要同时打开触摸**：camera 上电用 PE6，而 PE6 是触摸 RST。
- 串口期望：`[camera] DVP0 started (640x480)` → `[camera] first frame: 640x480 jpeg len=...`；屏上应出现解码后的画面（示例用 `tal_image_jpeg_decode_rgb565`，JPEG 路径不做旋转）。
- 若无帧：先查 sensor 供电/复位时序（PE6 LDO、PD8 XCLK、PE9 RESET）与 IIC1（PE7/PE8）地址 0x21 探测；`server_open video1.1` 失败或 `VIDEO_REQ_REC` 非 0 会直接打印。
- CSI-2 单独构建：同一示例改临时配置（`CONFIG_ENABLE_CSI2=y`、`CONFIG_CAMERA_NAME="camera_csi2"`、关 DVP、保留 `CONFIG_EXAMPLE_CAMERA_JPEG=y`），构建级已过（556,396 B / `71d82aa7…`）；实机未验。
- 判据：有效帧 + 屏显至少 60 s 无复位；只到构建或只到“串口 first frame”不构成出图验收。

## 交接

结束时记录：板卡 revision、COM、操作者、所烧镜像 SHA、日志路径，并释放占用。任何未实际执行的步骤写“未运行”。
