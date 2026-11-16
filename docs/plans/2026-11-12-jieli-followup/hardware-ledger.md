# JIELI 硬件与串口占用登记（2026-11-13）

规则：单板/单串口同一时间只有一个持有人；交接必须在此登记；端口每次现场核实，不沿用历史 COM 号。

## 当前连接

| 项目 | 值 | 证据 |
|---|---|---|
| 端口 | `COM8`（USB-Enhanced-SERIAL CH343，VID:PID 1A86:55D3，SER 5AB9072473） | `serial.tools.list_ports`、`Get-CimInstance Win32_SerialPort`，2026-11-13 |
| 板卡 | AC79_DevKitBoard / AC791 / WL82 | 用户在 14:1x 更换为 791 板；重烧后启动日志 `WL82(AC791N) CHIP_ID: 0x6f01`、`Platform chip: wl82`、`Platform board: AC79_DevKitBoard`、`Platform commit-id: c967f4b1…`（`postflash-earlyboot.log`，2026-11-13） |
| Flash | 8 MiB（历史 flasher 输出 `Flash Capacity: 8M`） | `.local-jieli-migration/.../chatbot-ac79-flash.log` |
| 当前固件 | `your_chat_bot`（`app_chat_bot.c:66` 周期打印堆信息） | COM8 日志，2026-11-13 14:04–14:12 |
| 配网/授权 | 未见已配网（Wi-Fi 扫描无 BSS）；未见授权日志 | COM8 日志；未做激活/云端操作 |

### 14:10 前的旧记录（已物理更换，仅作历史）

- 同一 COM8 在 14:04–14:10 运行 WL83（AC792N）固件：`Free psram heap size:14362472`（16 MiB SDRAM）。
- 历史串口证据（2026-11-10/11）中 COM8 亦曾为 AC792N（`WL83(AC792N) setup_arch`、`Platform board: AC792N_Develop_Board`）。
- 结论：COM8 的板卡身份必须每次现场核实，不能按历史记录推断。

## 占用与交接

| 时间窗 | 任务 | 持有人 | 动作 | 释放/交接 |
|---|---|---|---|---|
| 2026-11-13 14:04–14:12 | 00 基线/硬件盘点 | 总管 | 只读监听、DTR/RTS 尝试（未复位）、CLI 探测（无响应） | 已释放，移交任务 02 |
| 2026-11-13 14:15 起 | 02 AC791 语音 | 任务 02 agent（唯一持有人） | 构建/烧录/串口/离线与实机语音验证 | 已释放（2026-11-13 12:5x，02 完成，冻结不再烧录） |
| 2026-11-13 12:5x 起 | 10 AC791 触摸 | 任务 10 agent → 总管代理（13:35） | 烧录触摸验证/坐标调试镜像、I2C/坐标捕获 | **实机成功**：13:38/13:43/13:52 三版镜像；INT 修复版 `pressed=1`，坐标 x=36..319、y=40..479，分辨率 320x480、方向常规。COM8 已释放（13:56）。 |
| 2026-11-13 13:58 起 | 03/04 OTA（Layout C 全刷） | 任务 03 agent（COM8 唯一持有人） | 分区重设计、dual-bank 全刷、A/B 验证 | 用户授权原文：**“当前没有配网，可以全刷，重新设计flash分区”**（2026-11-13）。实际**未烧录**：容量矩阵证明 8 MiB WL82 双 bank 需 ≈9.97 MiB（firmware 9324K + reserved 884K）、工具判定 16M，Layout C 被否决。COM8 已释放（2026-11-13 14:2x），板卡保持触摸产品镜像，未改变分区/未上云。证据：`.worktrees/03-ota/.layoutc-capacity/summary.json`、`.worktrees/03-ota/docs/plans/2026-11-12-jieli-followup/reports/03-report.md` 第 10 节。 |
| 2026-11-13 14:4x 起 | 02 AC791 语音（“你好涂鸦”实机） | 任务 02 agent（COM8 唯一持有人） | 已选定 `keyword_tflite` 路径（用户提供的 `audio_subsys.a` 为 pi32v2 LLVM bitcode，含 nhty 模型），WAKEUP 镜像 BUILD SUCCESS 2,648,112 B（`0e27a41e…`）；15:05 烧录 2.94 MB 探针镜像 `4fd38c41…` | **启动挂起**：无应用日志、`isd_download` 无法再进下载模式；用户已于 15:2x 断电重上电。COM8 已释放（15:1x）。token/唤醒未取得。 |
| 2026-11-16 17:xx | 05 AC791 SPI 摄像头（稳定版） | 总管（COM8 单人持有） | 4 fps/整帧门控实验冻结后回退（平台 `e2cd4918`）；重建烧录 `spipadstable`（`31d4021f…`） | 约 2.5 分钟后缺行恶化、画面冻结；已回烧 `spirobust`（`071f1ee8…`，`good=114 bad=7577` / 28 s）；**占用未释放** |
| 2026-11-16 17:xx | 05 AC791 SPI 摄像头（VGA 4fps 整理版） | 总管（COM8 单人持有） | 代码整理并推送（平台 `38994765`/`0ce93a38`，应用 `14290897`，文档 `33db32ad`）；重建烧录 `spivga4fpstidy`（`49759776…`） | 640×480 交付、`frames/badrows/skipped` 统计、~3 fps 干净帧；**占用未释放** |
| 2026-11-16 17:xx | 05 AC791 SPI 摄像头（320×240） | 总管（COM8 单人持有） | 探针镜像 `spiqvgadbg`（`b9956762…`）与 `spiqvgaout`（`db4dd238…`）实测 row 恒 1288 → sensor 端裁剪不可达；恢复厂商寄存器表，改为 TKL 主机侧 2:1 抽取交付 320×240，镜像 `spiqvgadec`（`0979fd35…`）烧录启动成功 | `first frame: 320x240 len=153600`、`good=90 bad=23`、`conv_only=19ms`；画面待目视复验；**占用未释放** |
| 2026-11-16 16:xx | 05 AC791 SPI 摄像头（画质收尾） | 总管（COM8 单人持有） | TE(PC0) 同步、3 缓冲、帧拼接超时、错位行复制；显示埋点 `te=11ms tx=27ms` | 当前板载 `spitimeout`（`b595629b…`）；声音/紫绿线已确认解决，移动接缝判定为 8 位 8080 带宽限制；**占用未释放** |
| 2026-11-16 13:xx–14:1x | 05 AC791 SPI 摄像头 | 总管 | SPI 数据面镜像、预览镜像烧录与串口抓取 | **实机**：`frames=106 fps=5 bad=0`、屏上预览 fps=4（conversion+flush 65–68 ms），用户目视“有画面、少量紫/绿线条闪烁”。随后 PA 静音诊断镜像使应用启动异常（USB 不枚举），已撤回代码并重建；待烧 `spi-preview-8ab7fca0…`（40 MHz + 行尾校验）。**板卡自 14:2x 起 USB 不枚举，持有权未释放，等待用户断电重上电** |
| 2026-11-13 20:xx 起 | 05 AC791 双摄（分别跑通） | 任务 05 agent（COM8 唯一持有人，用户要求暂停，未释放） | 恢复下载模式 → 触摸回滚 `54a61aa5…` 烧录并启动成功 → 页码镜像 `e821333b…` 烧录（`Download completed`） | **暂停快照**：板载 `e821333b…`（YUV422P 页 11–16），目视未验。回滚镜像 `54a61aa5…` 已验证可启动（COM8 日志 `Free heap size:389380`）。下步：用户报告页 11–16 → SPI I2C 全扫描 `1517260b…` → 语音探针 → 回滚。 |

### 烧录中断事件（2026-11-13 14:33）

- 总管看到 `isd_download.exe`（PID 18200）在 `flash-02-baseline.log` 打印 FLASH INFO 后数分钟无新输出，误判为等待设备，将其 `Stop-Process`。
- 事后日志显示它已进入 `Start downloading data to flash` 并写出 `Write sector: 557 … 550`，随后父进程报 uploader exit `4294967295`。**板卡可能处于部分烧写状态。**
- 已要求任务 02 持有人立即完整重烧并在报告中如实记录；后续不再由总管直接干预该串口/进程。
- 14:34 重烧 `flash-02-baseline-retry.log`：`Device online` → `Erasing reserved area VM [0x22e000-0x300000]` → `Download completed` → `Platform flash success`。注意：擦除范围只到 `0x300000`，未包含 `TUYAOPEN 0x300000–0x3d1000`（与 FLASH INFO 的 `ERASE MODE: NONE` 一致）。**启动验证以任务 02 的串口日志为准。**

## 仪器

- 示波器/逻辑分析仪：未见连接记录；任务 09 的 PWM/UART1 RX 物理验收仍待仪器与单独时间窗。

## 排队（2026-11-13 起）

1. ~~10 AC791 触摸~~：已于 13:38–13:52 完成实机验证，COM8 已释放。
2. ~~03/04 OTA~~：03 已完成容量取证，8 MiB 双 bank 被否决（未烧录）；04 的同址 A/B 同样阻断。
3. 09 外围：AC791 UART1 RX 需独立 USB-TTL（不得占用 COM8）；AC792 PWM 需 AC792N 与示波器/LA。
4. 02 语音端到端：用户选择暂缓配网；恢复时需重新占用 COM8 并重烧对应镜像。
5. 05/06/07/11 AC792：AC792N 当前未连接；接入后按 camera/voice/touch 分别预约。
