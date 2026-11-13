# 05 — AC791 双摄拓扑与双 DVP 可行性（阶段 A，只读研究）

- **任务**：JIELI 后续任务 05，AC79_DevKitBoard（AC791/WL82）双摄路径的阶段 A 拓扑核实。
- **日期**：2026-11-13。
- **主仓基线**：`D:\tuya_proj\TuyaOpen\.worktrees\05-camera`，分支 `task/05-ac791-camera`，`055042a8119b3a21910761bc13f171da19cf35fc`。
- **平台 pin**：阶段 A 时 `platform/JIELI` = `c967f4b174480ff1df966eeea80bee1b0edcb0f6`，只读。阶段 B 在隔离 worktree `.platform-worktrees/05-camera` 上以当时本地 tip `77657dfa`（含 03 OTA/KWS 平台提交）为基，分支 `task/05-ac791-camera-platform`，提交 `630eb574` + `9307f388` + `7659c3c9`，未推送。
- **本轮边界**：只读研究，未改代码、未构建、未烧录、未接触 COM8（任务 02 独占）、未推送。
- **结论级别**：原理图 + SDK 源码 + 板级配置静态证据；**没有任何实板 probe、有效帧、双路并发或稳定性数据**。

---

## 0. 阶段 B 增补（2026-11-13，实机前）

用户确认**两路摄像头都已插上，两路分别单独实测**，不要求并发。阶段 A 的“不能并发”结论不变；本节把“分别跑通”落成可切换的实现与构建证据。

### 0.1 实现与开关

| 项 | DVP 路 | SPI 路 |
|---|---|---|
| 连接器 | J1 24-pin | J4 12-pin |
| Kconfig | `CONFIG_JIELI_CAMERA_PATH_DVP=y` | `CONFIG_JIELI_CAMERA_PATH_SPI=y` |
| 厂商设备 | `video0.2`（camera0，回退 `video0`） | `video1.0`（sensor）+ `spi2`（数据） |
| 摄像头 I2C | iic0（软件 IIC）PH0/PH1 | iic1（硬件 IIC）PH0/PH1 |
| XCLK/RESET | PH2 / PH3 | PH2 / PH3 |
| 数据引脚 | PA0-PA7 + PA8/9/10 | PA2/PA3/PA4 |
| 输出格式 | YUV422（`VIDIOC_*OVERLAY`） | YUV422（BT656 行包去头尾） |
| 默认分辨率 | 640x480@20 | 320x240@12 |

开关是 Kconfig `choice`（三选一：none / DVP / SPI），**不是运行时并发**；两条路径共用 PA2/PA3/PA4 与 PH0-PH3，因此一个镜像只注册一路。`ENABLE_DVP` 是新平台能力开关，默认 n，只有板级 choice 选中时才编译 TKL adapter。

主要改动：

- 主仓：`boards/JIELI/TKL_Kconfig`（`ENABLE_DVP`）、`boards/JIELI/AC79_DevKitBoard/Kconfig`（camera choice、宽高/帧率、IIC 引脚默认值、`depends on !JIELI_BOARD_UART1_ENABLE`）、`boards/JIELI/AC79_DevKitBoard/board_com_api.c`（注册 `tdl_camera_dvp_device_register("camera", ...)`，sensor 接口表留空由 vendor 驱动 probe）、`examples/peripherals/camera/jieli_probe/`（无屏无 SD 的验证应用 + `probe.ps1`）。
- 平台仓（本地分支 `task/05-ac791-camera-platform`）：`include/media/tkl_dvp.h`、`tuya_cloud_types.h`（`TUYA_DVP_TKL_CFG_T` + `inter_cfg`）、`src/driver/tkl_dvp.c`、`adapter_sources.txt`、`src/system/tkl_system.c`（补 `tkl_system_enter/exit_critical`，此前只有声明）、`tools/jieli_build/feature_map.py`（CAMERA feature、`video_rec_server.a`、源去重）、`board_config.py`/`sdk_overlay.py`（app_config、板级 camera0/camera1/video0/video1/spi2 注册与 staging）。

### 0.2 构建证据（两条路径分别构建）

| 路径 | 配置 | 结果 | SHA256 | 大小 |
|---|---|---|---|---|
| DVP | `JIELI_AC79_DevKitBoard_dvp.config` | BUILD SUCCESS | `efe0e44fde00ed2e952b30fa3b039254b13d385ebfd46b05ba4959ae6272cdc1` | 408,468 B |
| SPI | `JIELI_AC79_DevKitBoard_spi.config` | BUILD SUCCESS | `8f60aad678b9e9bf03a9e5c59fd59c50e782629d6f44f5e03ccba0bf6fcba11d` | 413,428 B |

产物已固化到 `.worktrees/05-camera/.artifacts/jieli-camera/`（同名 `.sha256`；SPI 包大小按 (line+8)*height+8 修正后重建），对应 `dist/jieli_probe_1.0.0/jieli_probe_QIO_1.0.0.bin`。两次构建使用同一份已提交源码；该 vendor 工具链的镜像字节含构建时间戳，重复构建 SHA 会变，因此表中的 SHA 是**已归档产物的指纹**，不是可复现的源码摘要。

提交：主仓 `b845f3a6` + `51ba3b0c` + `90358e9a`（`task/05-ac791-camera`），平台仓 `630eb574` + `9307f388` + `7659c3c9`（`task/05-ac791-camera-platform`，基于 `77657dfa`；本地提交，未推送）。

staging 交叉核对（DVP/SPI 各自）：

- `app_config.h`：DVP 仅 `CONFIG_VIDEO_ENABLE` + 640x480；SPI 另加 `CONFIG_VIDEO1_ENABLE`、`CONFIG_SPI_VIDEO_ENABLE`、`CONFIG_SPI_VIDEO_USE_ISC1_RECV=0`、`CONFIG_SPI_ONE_LINE_ENABLE=0`、`TCFG_SPI_ATTR` 与 `TCFG_VIDEO1_*`/`TCFG_IIC1_*`。
- `board.c`：DVP 注册 `{ "video0.*", &video_dev_ops, ... }`；SPI 注册 `{ "video1.*", ... }` 与 `{ "spi2", &spi_dev_ops, ... }`。
- `tuya_board_periph_config.h`：DVP `IIC0=PH0/PH1 (112/113)`；SPI `IIC1=PH0/PH1 (112/113)`。
- Makefile：`-DJIELI_CAMERA_PATH_{DVP,SPI}=1 -DJIELI_CAMERA_WIDTH/HEIGHT/FPS=...`，并链接 `video_rec_server.a`。

### 0.3 共享引脚 init/release 顺序（代码核对）

| 阶段 | DVP | SPI |
|---|---|---|
| 设备表 | 只挂 `video0.*` | 只挂 `video1.*` + `spi2` |
| TKL init | 只分配 YUV422 缓冲 | 只分配 YUV422 缓冲 |
| 采集线程启动 | `dev_open("video0.2")`（失败回退 `video0`）；vendor 驱动按 `camera0_data` 拉 XCLK PH2、复位 PH3，并通过 iic0 探测/配置 sensor；随后 `VIDIOC_SET_FMT/OVERLAY` | 先 `gpio_output_channle(PH2, 24M)` → `dev_open("video1.0")`（iic1 + 复位 PH3 + sensor 配置）→ 最后 `dev_open("spi2")` 占用 PA2/PA3/PA4；再 `IOCTL_SPI_SET_USER_INFO` |
| 采集停止 | `VIDIOC_CLEAR_OVERLAY` → `dev_close(video0)` | `dev_close(spi2)`（释放 PA2-PA4）→ `dev_close(video1)`（释放 iic1/PH3）→ `gpio_clear_output_channle(PH2)` |
| 跨路径切换 | 通过重新编译 + 重启实现；同一镜像内不会同时初始化两套 pinmux | 同左 |

`tkl_dvp_stop()` 现在等待采集线程完成上述 teardown（最多 1 s）后才释放线程，避免切换时把仍持有 `spi2`/`video1`/`video0` 的任务直接 kill。确认过的冲突项：PH2/PH3 同时是 UART1(TX/RX) 与 PWM0 默认引脚，Kconfig 已让 camera choice `depends on !JIELI_BOARD_UART1_ENABLE`；PWM0 未被本验证应用打开，产品若启用 PWM0 需另做互斥。

### 0.4 每路验收表（实机待 COM8 放行）

| 字段 | DVP（J1/video0） | SPI（J4/video1+spi2） |
|---|---|---|
| 烧录镜像 SHA256 | `b689631c…`（首次烧录尝试，未写入） | `8f60aad6…`（尚未重建/烧录） |
| sensor 型号 | 待 vendor probe / `CAMERA_GET_SENSOR_TYPE` 日志；J1 丝印复核 | 待 vendor probe / `CAMERA_GET_SENSOR_TYPE` 日志；J4 丝印复核 |
| sensor ID | 待 `CAMERA_GET_SENSOR_INFO` + 驱动 `*_ID_check` 日志 | 待同左 |
| 首帧时间 | 待测（`FIRST FRAME` 行 + 上电到首帧 ms） | 待测 |
| 分辨率 | 期望 640x480 | 期望 320x240（以 probe 实际为准） |
| 像素格式 | YUV422 | YUV422（BT656 行包剥离后） |
| 帧率 | 期望 20 fps，记录 1 s 窗口实测 | 期望 12 fps，记录 1 s 窗口实测 |
| 失败码/现象 | 待记录：`video0 open failed`、`VIDIOC_SET_FMT`/`SET_OVERLAY` 返回值、无帧 | 待记录：`video1.0 open failed`、`spi2 open failed`、`bad packet head=...`、`short line copy`、无帧 |
| 结论 | 未测 | 未测 |

### 0.5 烧录纪律与回滚

- COM8 归语音任务期间未打开。15:1x 总管放行后做过一次 DVP 烧录尝试（见 0.7），因板卡未进入下载模式而失败，镜像未写入。后续仍按 **DVP → SPI** 顺序，每次烧录前重新枚举端口。
- 回滚镜像：`D:\tuya_proj\TuyaOpen\.worktrees\10-touch\apps\tuya.ai\your_chat_bot\dist\your_chat_bot_1.0.1\your_chat_bot_QIO_1.0.1.bin`，SHA256 `54a61aa539f11be81cd960017da29e7681cd66a056d219706ffa1b970ab629ad`。
- 不改分区、不上云、不推送。验证脚本：`examples/peripherals/camera/jieli_probe/probe.ps1`（`build` / `archive` / `flash` / `capture`）。

### 0.6 本轮阶段 B 明确未做

- 除 0.7 记录的一次未写入的烧录尝试外，未取得任何帧数据，因此 **没有 sensor ID、首帧、分辨率、帧率或有效帧实机数据**。
- 未验证 vendor `video0`/`video1` 在本板的具体 probe 顺序与实际 SKU；表中型号一栏保持“待测”，不按原理图猜写。
- 未验证 SPI 双线采样相位（`TCFG_SPI_ATTR` 取自 vendor DevKitBoard 配置）是否匹配实际模组；若首包持续 `bad packet head`，下一步只调该宏。

### 0.7 实机尝试记录 1（2026-11-13 15:16–15:26，阻塞在下载模式）

用户放行 COM8 后，按 DVP 优先执行；**烧录未成功，镜像没有写入**。

- 现场核实：COM8 = `USB-Enhanced-SERIAL CH343 (VID 1A86 PID 55D3)`，可正常打开/关闭；无 `monitor` 残留。但系统未出现 JieLi 下载设备（VID 4C4A）。
- DVP 重新构建：`b689631c95c529a3292895a9f1ed4a1c82a82d63b748d07923310f556a559d4e`，408,468 B（1484… 之前的归档不用于本次烧录）。
- 厂商 `isd_download.exe`（平台默认命令）：打印 FLASH INFO 后 `-wait 300` 等待设备，无 `Device online`，最终超时/被停止。
- 平台 Python 串口下载器（`JIELI_USE_PY_FLASHER=1`，`-m serial -p COM8 -b 1000000`）：9600 握手后报 `Target is in Mask ROM mode`，上传 `wl82loader.bin` 时 `Timeout waiting for serial ACK`。
- 判断：板上当前是语音 2.94 MB 探针镜像，启动挂起后不再提供运行时下载入口；需要用户按住 UPDATE/下载键上电（或 UPDATE+RST、重插 USB 下载口）让芯片停在下载模式。
- 证据：`.artifacts/jieli-camera/logs/flash-dvp-legacy.log`、`.artifacts/jieli-camera/logs/flash-dvp-serial.log`。COM8 已释放，未连续重试。

### 0.8 恢复优先顺序与下载器复核（2026-11-13，待 ROM 模式）

**进入下载模式后的顺序（不得跳步）：**

1. **先烧已知可启动的触摸镜像** `54a61aa539f11be81cd960017da29e7681cd66a056d219706ffa1b970ab629ad`（2.2 MB，`your_chat_bot_QIO_1.0.1.bin`），确认板子恢复：
   - 在 `D:\tuya_proj\TuyaOpen\.worktrees\10-touch\apps\tuya.ai\your_chat_bot` 执行 `tos.py flash -p COM8`。该目录 `.build/jieli-staging/.../app.bin` 的 SHA256 已核实**就是** `54a61aa5…`，不是近似重建。
   - 预期日志：`Device online` → `Erasing…` → `Start downloading data to flash` → `Download completed` → `Platform flash success`；随后串口出现 `WL82(AC791N) CHIP_ID`、`Platform board: AC79_DevKitBoard`，无挂起。
   - 注意：烧录器会擦 VM 区（与 02 报告一致）；该镜像本来就是回滚基线。
2. 板子确认可启动后，再烧 DVP probe（当前 `.build` 的 `app.bin` 对应 `b689631c…`；烧前再次核对 staging `app.bin` 的 SHA）。
3. DVP 成功后切 SPI 配置构建、烧录、抓帧；结束再烧回 `54a61aa5…`。

**下载器复核结论：**

- 平台 Python 串口下载器（`JIELI_USE_PY_FLASHER=1`）：
  - `-b/--baud` 被 CLI 接收，但 `JLSerialTransport.sync_and_negotiate_baudrate()` **从未被调用**；`connect()` 只以 9600 打开，之后所有命令仍在 9600。因此“115200/1M 降速重试”当前没有实现，`-b` 实际无效。
  - `prepare_device()` 对串口 transport 调不到 `inquiry()`，`inq_product` 为空，于是无条件打印 `Target is in Mask ROM mode` 并上传 RAM loader。上一次日志里的 “Mask ROM mode” 是**误报**，不能作为“已进入 ROM 模式”的证据；真正的失败是随后 loader 写入超时。
  - loader 选择顺序与 pin 不一致：`flasher.py::_locate_loader()` 先取 `cpu/wl82/tools/wl82loader.bin`（30.5 KiB，SHA256 `9920e666…`），而随 `isd_download.exe` 一起提供的是 `cpu/wl82/tools/loader_tools/wl82loader.bin`（29.5 KiB，SHA256 `e6388091…`）。若要重试 Python 路径，应先把 `loader_tools/wl82loader.bin` 作为首选（或临时放入 `tools/jieli_flasher/loaders/`），不要直接覆盖厂商文件。
  - 串口命令（仅供放行后使用）：
    ```powershell
    $env:OPEN_SDK_ROOT="D:\tuya_proj\TuyaOpen\.worktrees\05-camera"
    $env:OPEN_SDK_PYTHON="D:\tuya_proj\TuyaOpen\.venv\Scripts\python.exe"
    $env:JIELI_USE_PY_FLASHER=1
    python D:\tuya_proj\TuyaOpen\.worktrees\05-camera\tos.py flash -p COM8 -b 9600
    ```
    预期：`Opening serial port COM8 at 9600 baud` → 同步 ACK → 上传 loader → `Device online`；若仍在 loader 阶段 `Timeout waiting for serial ACK`，说明设备并未停在 ROM 模式或 loader 不匹配，应立即停止。
- `loader_tools/isd_download.exe` 与 `tools/isd_download.exe` 是同一厂商 2.2 MB 工具，仍走 USB 自动探测（VID 4C4A），没有串口参数；换用它不会改变“必须先进入下载模式”的前提。
- 首选仍是厂商 `isd_download.exe`（`tos.py flash -p COM8`，不加 `JIELI_USE_PY_FLASHER`）。串口 Python 路径只在厂商工具探测不到 USB 设备时作为一次备选，且只重试一次。

### 0.9 实机恢复尝试（2026-11-13 15:30，串口 ROM 假设）

按“只此一次”的授权，用正确 loader 做了一次软件恢复（目标先是触摸回滚镜像 `54a61aa5…`）：

- COM8 打开前核实空闲；烧录用 `.worktrees/10-touch` 的 staging，烧前 `app.bin` SHA256 再次确认就是 `54a61aa539f11be81cd960017da29e7681cd66a056d219706ffa1b970ab629ad`。
- 把 `cpu/wl82/tools/loader_tools/wl82loader.bin`（29.5 KiB，`e6388091…`）临时放入 `tools/jieli_flasher/loaders/`（不覆盖厂商文件；尝试后已恢复仓库跟踪版本 `9920e666…`）。
- 命令：`JIELI_USE_PY_FLASHER=1 tos.py flash -p COM8 -b 9600`。
- 结果：`Opening serial port COM8 at 9600 baud` → `Uploading wl82loader.bin (30208 bytes) to RAM 0x01C02000` → 4 秒后 `Timeout waiting for serial ACK`。**没有写入任何数据**；COM8 随后空闲。
- 关键判断：日志中的 “Mask ROM mode” 仍是误报（`connect()` 不做同步握手，`sync_and_negotiate_baudrate()` 未被调用；第一次真实协议交互就是 loader 的 CDB 帧）。因此“9600 握手有响应”这一假设**没有日志支持**；换用 loader_tools 版本后仍超时，说明设备没有停在串口 ROM 同步状态，而不是 loader 版本问题。
- 证据：`.artifacts/jieli-camera/logs/recovery-touch-serial.log`、`recovery-touch-app-sha.txt`。
- 停止条件已触发：不再重试；等待用户按住 KEY/UPDATE 上电，使设备以 USB 下载模式（VID 4C4A）出现。出现后再按 0.8 的恢复优先顺序执行。

### 0.10 SPI 首轮实机与修复（2026-11-13 16:24–16:31）

板子恢复后，DVP 先烧通（见 0.11）；随后 SPI：

**烧录与启动**

- 镜像 `9b540a55ceb62da7b701575b667a2e31d3b55fb548b7786cb2cf458609d89918`（320x240@12，413,428 B）；用户复位后 `Device online` → VM 擦除 → `Download completed`。
- 烧录器 `-reboot 500` 之后串口**完全没有输出**；不是固件挂起——用户普通断电重上电后正常启动：
  `WL82(AC791N) CHIP_ID: 0x6f01` → `camera path: SPI (J4/video1+spi2)` → `camera info: max 320x240@12fps fmts=0x301` → `tdl_camera_dev_open(320x240@12): 0`。
  结论：以后每次厂商烧录后需普通断电重上电才能看到启动日志，不能把 `-reboot` 后的静默当成挂起。

**传感器探测失败**

```
iic write err!!! line : 274 / :87
BYD3901 Sensor ID : 0xa5a5
GC0310  Sensor ID : 0x0
GC6133  Sensor ID : 0xff
[VIDEO_CAMERA]check digital sensor err
[VIDEO]camera1 open err
[camera] video1.0 open failed (no SPI sensor?)
```

- 三颗候选全部无有效 ID，I2C 写从地址阶段就 NAK。
- 根因（静态证据）：厂商 `apps/wifi_camera/board/wl82/board_develop.c:630` 在 `CONFIG_VIDEO1_ENABLE` 下把 **iic1 注册为软件 IIC**（`sw_iic1_data`，PH0/PH1，delay 50），硬件 IIC 只作 iic2；GC0310/GC6133 驱动固定打开 `iic1`。TuyaOpen 适配器原来把 iic1 注册为硬件 IIC，因此 SPI 路径的 sensor 探测必然失败。
- 证据日志：`.artifacts/jieli-camera/logs/spi-boot-after-powercycle.log`（含完整 probe 顺序）。

**修复（已构建，未烧）**

1. `tkl_i2c.c`：`JIELI_CAMERA_PATH_SPI` 下把 iic1 注册为软件 IIC（PH0/PH1，`sw_iic_delay=50`），其余构建保持硬件 IIC。
2. `tkl_dvp.c`：SPI DMA 缓冲从 311 KiB 静态 `.sram` 改为启动时按需对齐分配（单包），避免开机即占用内部 RAM。map 对比：
   - 修复前：`.ram0_bss = 0x4FAE0 (326,368 B)`，其中 `.sram = 0x4C67C (313,980 B)`；
   - 修复后：`.ram0_bss = 0x3BE0 (15,328 B)`，`.sram = 0x75C (1,884 B)`；
   - `ram0` 区总量 `0x7FD4C (523,596 B)`，修复前静态占用本身未越界，但内部堆只剩约 152 KiB；修复后恢复约 464 KiB。
3. 启动探针：`user_main` 第一条 `[SPI_PROBE] boot, path=...`；SPI staging 在 `devices_init()` 前后、`video1.0`/`spi2`/`SET_USER_INFO` 每步打印。

**新镜像**：`20f806d0d6c81a48e19c1865183c5bbaa59f6235c2659b3b947efbb8f6fb52da`，414,004 B；平台提交 `8bb0545f`。待硬件窗口烧录验证 sensor ID/帧。

### 0.11a 修复版 SPI 复测（2026-11-13 16:32–16:35，软件 IIC）

- 镜像 `20f806d0d6c81a48e19c1865183c5bbaa59f6235c2659b3b947efbb8f6fb52da`（414,004 B）烧录成功；用户**普通断电重上电**后捕获到完整启动。
- 启动链正常：
  ```
  [SPI_PROBE] devices_init begin
  [SPI_PROBE] devices_init done
  [SPI_PROBE] boot, path=SPI
  camera path: SPI (J4/video1+spi2)
  camera info: max 320x240@12fps fmts=0x301
  [camera] SPI buffers: dma=1c0f680 (155528 B) frame=1c35624 (153600 B)
  [camera] SPI: output XCLK PH2
  [camera] opening video1.0
  ```
  `CODE+CONST 363968`、`RAM_SIZE 523596 / REMAIN 465804`，动态 DMA 缓冲分配成功（内部堆约 464 KiB），静态 `.sram` 已降至 `.ram0_bss 15328 B` 量级。
- **传感器探测仍全部失败**（软件 iic1 已确认编入镜像：`sw_iic_delay`/`__initcall_link_s_iic1_pd` 在 map/object 中）：
  ```
  BYD3901 reset / iic write err!!! line : 274 ×2 / Sensor ID : 0x0 / not BYD3901
  GC0310  reset / iic write err!!! line : 87  ×2 / Sensor ID : 0x0 / not GC0310
  GC6133  reset / iic write err!!! line : 220 ×2 / Sensor ID : 0x0 / not GC6133
  [VIDEO_CAMERA]check digital sensor err
  [VIDEO]camera1 open err
  [camera] video1.0 open failed (no SPI sensor?)
  ```
  与硬件 IIC 版本的区别只在读回值（旧 0xa5a5/0x0/0xff → 新 0x0/0x0/0x0）；**从地址阶段 NAK 不变**，说明不是控制器类型问题，而是总线上没有任何器件应答。
- 引脚/配置与厂商完全一致（`board_dev_kit_cfg.h`）：IIC1 SCL/SDA = PH0/PH1（112/113）、XCLK = PH2、RESET = PH3、PWDN = -1；`camera1_data.interface = SEN_INTERFACE0`，与 `board_develop.c` 相同。SPI 路径厂商只探测 BYD3901/GC0310/GC6133 三款。
- **阻塞点需要用户确认硬件**：J4（12-pin FPC）是否插了 SPI 摄像头模组、模组型号/丝印是哪一款。若未插或型号不在上述三款，属“缺件/型号不符”，不是固件问题；若已插且型号匹配，则需查 J4 供电与 PH0/PH1 上拉。
- 日志：`.artifacts/jieli-camera/logs/spi2-boot-capture.log`、`spi2-flash-vendor.log`。

### 0.11b 厂商烧录参数与串口注意事项（2026-11-13，协调方核对官方文档）

- **`-reboot 2000`**：官方建议下载完成后等待 2000 ms 再复位；此前用 `-reboot 500`，出现“烧完串口静默、必须人工断电”的现象。后续烧录命令统一改为 `-reboot 2000`，仍以普通断电重上电抓启动日志为准。
- **下载前拔 CH343**：官方排障说明 PC 串口/UART 适配器可能阻止进入下载模式。若 `Device Offline`，先拔 CH343（只留 USB1.1 下载口）再烧，烧完插回抓日志。
- **强制下载**：官方为“长按 update 键 + 按 reset，先松 reset 再松 update”；工具目录 `SM01-DFU.exe` 可能对应杰理强制升级工具；板上 K1–K8 是 AD 按键，具体哪个是 update/reset 需用户按丝印确认。
- 免按键下载依赖固件使能 USB 从机模式；TuyaOpen/触摸镜像具备，探针镜像在 USB 初始化前挂住时才需要强制入口。

### 0.11c `output_display`（DVP + ILI9488）实机：首帧后异常重启（2026-11-13 16:53–17:2x）

- 构建：新增 `examples/peripherals/camera/output_display/config/JIELI_AC79_DevKitBoard_dvp_lcd.config`（DVP 640x480@20 + `ENABLE_LIBLVGL`/`ENABLE_DISPLAY`/`ENABLE_MCU8080`）。首版镜像 `3883c1251684139e1f9145a4185c8ec296acc8adf9c650756610239b5d408053`，410.8 KiB。
- 烧录：`-reboot 2000` 后**自动重启成功**（`system reset reason: SOFT`），无需人工断电；`Device online` → VM 擦除 → `Download completed`。
- 启动与显示初始化正常：
  ```
  tdd_disp_mcu8080_ili9488_register: display
  [EMI]emi data io : IO_PORTC_01 - IO_PORTC_08 / emi clk = 20000 KHZ / emi open success
  [camera] sensor: GC2145
  [camera] sensor info: 640x480 fps=20 real_fps=0
  [camera] first frame: 640x480 fmt=1 len=614400
  ```
- **新事实：DVP 路 sensor = GC2145（此前 DVP 只抓到帧，未抓到型号）**，640x480 YUV422，首帧 614400 B。
- 首帧后约 15 ms 即异常重启（两次复现）：
  ```
  CPU0 run addr = 0x1e00000
  CPU0 20348c0 --> 20348b8 --> 20349fc --> 20348c0
  rets: 0x1c07c70   reti: 0x1c07c0e
  exception reason : axi_rd_inv
  ```
  符号化（该示例 `sdk.elf` + `llvm-symbolizer`）：`0x20348c0` 位于 `tdl_disp_convert_yuv422_to_fb` 内层 YUV 读取（`r3 = b[r0+1]`），返回地址 `0x1c07c70` 在 `__get_camera_raw_frame_cb`。**不是 flush 回调崩溃**。
- 调试版（`19a9ece9…`）打印：
  ```
  [disp-dbg] info 320x480 fmt=0 swap=1
  [disp-dbg] fb add 320x480 fmt=0 num=2
  [disp-dbg] frame data=4188700 wh=640x480 len=614400 fb=400f380 fbwh=320x480 fblen=307200 in_order=0
  ```
  `fmt=0` 是 RGB565，`is_swap=1`，输入/输出尺寸与长度都正确；`convert done` 从未打印，确认卡在转换函数内读取 `frame->data`。
- 隔离实验：构建纯色填充版 `deb27baf2adf05bb41561b4ed18cf640ddddf9f75f7419e1d3ad83b1ac3e9acb`（跳过转换、直接 `memset` fb 再 flush），烧录成功。用户断电重上电后日志走到
  ```
  [disp-dbg] frame data=4188700 ... fb=400f380 fbwh=320x480 fblen=307200
  ```
  之后**再无任何输出**（没有 `solid fill done`、没有 exception、串口一直静默）。纯色版在该打印之后的第一步就是 `memset(fb, 0xF8, 307200)`，因此**挂起点在写显示帧缓冲或其后的打印**，而不是 `tdl_disp_convert_yuv422_to_fb` 读相机帧。与转换版的 `axi_rd_inv` 合在一起看，问题集中在 `0x400f380` 这块 fb 内存路径。
- 纯显示自检镜像 `aaa86a680e62852f2eff5087cd8262bf63260637ca0e16e33d8497a82cc4d45c`：不打开 camera，在 `user_main` 里取 fb → 写首字节 → memset 全帧 → flush，三步各有打印。用于区分 fb 内存写和 EMI flush；待用户断电重上电后抓取。
- 增强自检镜像 `6d7729a04ff1dc0ab4b9716070d09fabaa5f36299b26c5f5770618ee2b6f9f03`：在 `user_main` 里再分配一块 640x480 合成 YUV422 缓冲，调用同一个 `tdl_disp_convert_yuv422_to_fb` 并 flush，把“转换函数本身”和“相机回调线程上下文”分开验证；不打开 camera。
- **自检结果（用户强制下载模式后烧入，17:30）**：全部通过：
  ```
  [disp-dbg] selftest fb=400f340 len=307200
  [disp-dbg] selftest first byte written
  [disp-dbg] selftest fill done
  [disp-dbg] selftest flush returned
  [disp-dbg] selftest convert start fb=400f340 len=307200
  [disp-dbg] selftest convert done
  [disp-dbg] selftest convert flush returned
  [disp-dbg] display-only build, camera not opened
  ```
  说明 **fb 内存写、EMI flush、`tdl_disp_convert_yuv422_to_fb` 在 Tuya 线程上都正常**；LCD 应已出现纯色/合成彩条。
- 旁证：纯色版在相机回调里的 `solid fill done` 这次在 **258 s 后**才补打出来（同一句打印），说明相机回调线程被长时间阻塞/饿死；非纯色版则在同线程的转换里 `axi_rd_inv`。根因收敛为**厂商 `raw_flow_task`（8 KB 栈）上下文**，不是显示驱动或转换算法。
- **修复**：`output_display` 增加 `EXAMPLE_DISPLAY_WORKER`：相机回调只 `memcpy` 一帧到 SDRAM 缓冲并 `tal_semaphore_post`；独立 worker（8 KB 栈，`THREAD_PRIO_3`）取 fb → `tdl_disp_convert_yuv422_to_fb` → `tdl_disp_dev_flush`。worker 忙时丢帧。镜像 `7a8e51ee4e421325cd54d541eee4a31cbe02cc5a4893fdb7ad13333ad5796d45`，已烧录成功（`Download completed`）。烧后板子无输出，待用户断电重上电后验证。
- **双烧撞车**：协调方与我几乎同时向同一台设备发起 `isd_download`，其日志在写完数据后报 `ERROR: Failed to query data crc, address = 0x0`；我的同一时刻日志为 `Download completed`（EXIT=0），板子随后启动的 `setup_arch 17:25:54` 与我的镜像一致，故判断 CRC 错误由并发访问导致，镜像本身已正常启动并跑完自检。后续烧录改为单人操作。
- 厂商路径对照（静态）：`lcd_ili9488.c:59-63` 的 `LCD_DrawToDev`/`ILI9488_lvgl_Fill` 最终都走 `WriteDAT_DMA` → `lcd_send_map` → `dev_write(emi, buf, len)`，传入的也是 `malloc` 得到的缓冲；`10-touch` 的 `tdd_display_mcu8080.c` 与 05 逐字节一致，且 `app_display_camera_flush` 用的就是同一个 `tdl_disp_convert_yuv422_to_fb`（宏封装）。因此“厂商用了特殊显示缓冲、SDRAM 帧不能写”这一假设被排除；剩余重点是**相机回调线程（厂商 `raw_flow_task`，8 KB 栈）上下文**或 TDD 输入帧的内存属性。
- 显示分辨率：`tdl_disp_convert_yuv422_to_fb` 的软件转换只做裁剪（`width=min(in,out)`），不做缩放；640x480 → 320x480 会取左侧 320 列、保留全部 480 行。若后续需要整幅画面，必须另加缩放，不能指望该函数。
- 日志：`.artifacts/jieli-camera/logs/output-display-flash-vendor.log`、`output-display-boot.log`、`output-display-dbg-boot.log`、`output-display-solid-boot.log`、`output-display-solid-powercycle.log`。

### 0.11d SPI 静态逐行对照与根因分类（2026-11-13，未烧）

**厂商 probe 顺序（DevKitBoard 的 video1 路径）**

1. `camera_platform_data.xclk_gpio = PH2`、`reset_gpio = PH3`、`pwdn_gpio = -1`（`board_dev_kit_cfg.h:38-40`）。
2. 应用层先输出 XCLK：DevKitBoard 示例 `apps/common/example/Devkitboard_test/spi_camera.c:255` 对 PH2 用 `CH1_PLL_24M`；`wifi_camera/spi_video.c` 对 PH8 才用 `CH3_PLL_24M`。
3. `dev_open("video1.0")` → 厂商 camera1 驱动按 BYD3901 → GC0310 → GC6133 顺序探测（SPI 路径仅这三颗），每颗先拉 PH3 复位、再经 **iic1** 读 ID。
4. 探测通过后 `dev_open("spi2")`，SPI2 平台数据带采样极性属性；再 `IOCTL_SPI_SET_USER_INFO`、循环 `IOCTL_SPI_READ_DATA`。

**我们的实现与差异**

| 步骤 | 我们 | 厂商 | 结论 |
|---|---|---|---|
| XCLK | `gpio_output_channle(PH2, CH3_PLL_24M)` | PH2 用 `CH1_PLL_24M` | **已改 CH1**（`tkl_dvp.c`） |
| SPI2 属性 | `SPI_SCLK_L_UPL_SMPH` | demo_DevKitBoard 硬编码 SMPH；wifi_camera 的 GC0310 配置为 SMPL；BYD3901 为 H/SMPH | 新增 Kconfig 三选一，SPI 镜像选 SMPL（GC0310），默认 SMPH |
| 复位 | 厂商驱动用 camera1_data.reset_gpio=PH3 自动做 | 相同 | 一致 |
| PWDN | 未操作（`-1`） | `-1`；板上 R03 10K 下拉 | 一致 |
| I2C | 软件 iic1，PH0/PH1 | 软件 iic1，PH0/PH1 | 一致 |
| sensor 列表 | BYD3901/GC0310/GC6133 | 同 | 一致（无 GC0308-SPI/BF20A6/SC030IOT 驱动） |

**供电核对（AC79_DevKitBoard V1.2 原理图）**

- J4 pin1 `SEN_VDD`：R44 **0R 已贴**接 `AVDD28`（U2 RT9193-2.8V，EN 接 VIN，常开）；R45 **NC**（备选 3.3V_MCU）。不是 GPIO/load-switch 控制。
- J4 pin9 `PWDN`：R03 **10K 下拉到 GND**（R04 NC 到 SEN_VDD）→ 模组默认使能。
- J4 SCL/SDA：R41/R42 为 **0R 串阻，板上无上拉**；上拉必须在模组内。
- `+3.3V` 外部扩展电源原理图标注“常开”。U2 的 2.8V 供 DVP 已验证工作，说明该轨正常。

**根因分类**

1. **软件电源步骤：已排除**。板上无受控电源轨/PWDN GPIO，厂商同样不操作。
2. **I2C bus/地址：模组侧无 ACK 是当前直接原因**。三颗 sensor 在 address 阶段即 NAK（0xdc/0x42/0x80），与上拉/模组是否插好/接触有关；不是 iic0/iic1 选错（三者都开 iic1），也不是从地址写错。
3. **XCLK/SPI 极性：静态不一致，已修正并构建**（`74e70368…`），但 I2C 探测发生在 SPI 数据之前，修正不会让当前的无 ACK 变好；需模组先应答。
4. **模组：待用户重插 J4 并确认型号**。型号未知时无法确定 SMPH/SMPL/H_SMPH，镜像里已做成 Kconfig 可选。

**厂商 demo 对照镜像可行性**

`tools/jieli_build` 固定 staging `apps/demo/demo_hello` 并按它的 Makefile 构建，不能直接切到 `apps/wifi_camera` 或 `demo_DevKitBoard`。但 video1 的 sensor 探测/初始化在预编译 `video_rec_server.a` 里，两个 demo 与我们链接的是同一份库；差别只在板级注册（iic1 软件节点、`video1.*`、`spi2` 平台数据）和 SPI 采样属性，这些已经逐项对齐（XCLK 通道与属性另见上表）。因此另建厂商镜像主要用于验证模组本身，不能替代当前对 I2C 无 ACK 的判断；暂不为此新增第二套 staging。

### 0.11 DVP 实机结果（2026-11-13 16:18–16:22）

- 烧录：`b689631c95c529a3292895a9f1ed4a1c82a82d63b748d07923310f556a559d4e`（408,468 B），无需用户复位：`Device online` → VM 擦除 `[0x69000-0x300000]` → `Download completed`。
- 帧证据（COM8 115200，28 s 窗口，接在启动后约 10 s）：
  ```
  frames=216 fps=21 bad=0 last_len=614400 t=10590ms
  ...
  frames=762 fps=21 bad=0 last_len=614400 t=37520ms
  ```
  `614400 = 640×480×2`，即 YUV422；`bad=0` 表示所有回调整帧非零。实测约 21 fps（配置 20）。
- 缺口：接上串口时已过启动阶段，未捕获 `WL82` 横幅、sensor 型号/ID 与 `FIRST FRAME latency`；帧证据本身有效。日志：`.artifacts/jieli-camera/logs/dvp-flash-vendor.log`、`dvp-boot-capture.log`。

---

## 1. 结论（先给结论）

**AC79_DevKitBoard 不支持双路 DVP。阶段 B 不应实现“第二路 DVP”。**

三条独立证据：

1. **板上只有一路 DVP 摄像头连接器**：24-pin `FPC24-0.5MM`（V1.0/V1.2 均为 `J1`）。另一路是 12-pin `FPC_12P_0.5MM`（V1.2 `J4`），信号为 SPI/BT656 摄像头（`SEN_SPI_CLK`、`SEN_SPI_D0/D1`、`SEN_XCLK`、`SEN_SDA/SCL`、`SEN_RESET/PWDN`），**不是 DVP 数据总线**。
2. **12-pin SPI 摄像头与 24-pin DVP 摄像头共用引脚**：`PA2/PA3/PA4`（DVP 的 D5/D4/D3）以及 `PH0/PH1/PH2/PH3`（DVP 的 I2C/XCLK/RESET）。因此在这块板上，**DVP + SPI 也不能同时工作**；更不可能双 DVP。
3. **vendor 板级数据明确把第二路注册为 `-1`**：`apps/demo/demo_DevKitBoard/board/wl82/DevKitBoard.c:493-507` 的 `camera1_data` 为 `interface = -1`、`group_port = -1`、所有 `data_gpio = -1`。SDK 的 `video1` 在其它板上被注释为“SPI 摄像头”（`apps/wifi_camera/board/wl82/board_develop.c:651`），`spi_video.c` 直接 `dev_open("video1.0")`，不是第二路 DVP。

**SDK README 的“一路 DVP + 一路 SPI 摄像头同时输出 YUV”是芯片/SDK 能力声明，不是 AC79_DevKitBoard 的板级事实。** 该能力需要把 DVP 与 SPI 布到不重叠的 pin group；本板没有这样做。

**与任务 06 的结论要区分**：AC792N 的 DVP0/CSI-2 是 **PD11-PD14 pad 在两种接口间复用**；AC791 本板是 **一路 DVP + 一路 SPI 连接器共用 PA2-PA4/PH0-PH3**。两者都导致“不能并发”，但复用对象、证据和可行修复不同，不能照搬 06 的结论。

---

## 2. 板级 revision 与资料冲突

本地资料同时存在：

| 资料 | 路径 | 页数/版本 |
|---|---|---|
| DevKitBoard V1.0 原理图 | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/doc/datasheet/AC791N规格书/schematic/JL_AC79_DevKitBoard/JL_AC79_DevKitBoard V1.0原理图.pdf` | **1 页，p.1**；title block `JL_AC79_DevKitBoard V1.0` |
| DevKitBoard V1.2 原理图 | `.../JL_AC79_DevKitBoard/JL_AC79_DevKitBoard V1.2.pdf` | **1 页，p.1**；title block `JL_AC79_DevKitBoard V1.2` |
| DevKitBoard V1.0 位号图 | `.../JL_AC79_DevKitBoard V1.0位号图.pdf` | 1 页 |
| AC79x Wifi Develop V1.0 | `.../schematic/AC79x Wifi Develop V1.0/AC79x Wifi Develop V1.0.pdf` | 1 页；是模块/开发板参考，不是 TuyaOpen 的 AC79_DevKitBoard |
| WL82 IO 功能列表 | `.../JL_AC79_DevKitBoard/JL_AC79_WIFI V1.0 IO功能列表.pdf` | **4 页；相机/IO 复用表在 p.3-4** |

**冲突点**：任务书指向 V1.0 原理图，但 TuyaOpen 板级文件 `boards/JIELI/AC79_DevKitBoard/audio_config.h:12` 明确写 **“AC79_DevKitBoard V1.2: MIC1 is differential on the board schematic.”** 说明当前板级配置是按 V1.2 写的。V1.0 与 V1.2 的相机拓扑在本报告关心的点上一致（都是一路 24-pin DVP + 一路 12-pin SPI），所以**结论对两个 revision 都成立**；但实板丝印/revision 仍必须现场确认，不能从仓库文件名推断。

---

## 3. 24-pin DVP 连接器（J1 / FPC24-0.5MM）

### 3.1 连接器引脚（原理图 V1.2 p.1；V1.0 p.1 同构）

| J1 引脚 | 信号 | 说明 |
|---|---|---|
| 1 | NC | |
| 2 | AGND | |
| 3 | SDA | SEN0_SDA |
| 4 | AVDD30 | 3.0V |
| 5 | SCL | SEN0_SCL |
| 6 | RESET | SEN0_RESET |
| 7 | VSYNC | SEN0_VSYN |
| 8 | PWDN | SEN0_PWDN |
| 9 | HSYNC | SEN0_HSYN |
| 10 | DVDD18 | 1.8V |
| 11 | DOVDD | 由板级电源选择 |
| 12 | D9 | 文本层可见 → SEN0_D7 |
| 13 | MCLK | SEN0_XCLK |
| 14 | D8 | 文本层可见 → SEN0_D6 |
| 15 | DGND | |
| 16 | D7 | 文本层可见 → SEN0_D5 |
| 17 | PCLK | SEN0_PCLK |
| 18 | D6 | 文本层可见 → SEN0_D4 |
| 19 | D2 | 文本层可见 → SEN0_D0 |
| 20 | D5 | 文本层可见 → SEN0_D3 |
| 21 | D3 | 文本层可见 → SEN0_D1 |
| 22 | D4 | 文本层可见 → SEN0_D2 |
| 23 | D1 | 文本层未读出对应 SEN0 网络；需位号图/实板通断确认 |
| 24 | D0 | 文本层未读出对应 SEN0 网络；需位号图/实板通断确认 |
| 25/26 | GND | 屏蔽/地 |

> 注：右列 `D0-D9` 是**模组侧**的引脚名，不是 SoC 的 `SEN0_Dn` 位序。上表只记录原理图文本层能直接读出的对应关系；`SEN0_D0..D7` 的 SoC 侧 8-bit 总线是 **PA0..PA7**（vendor `camera0_data`），但连接器 `Dn` 到 `SEN0_Dn` 的逐位映射必须以位号图或实板通断为准。不要按 `D0→D0` 的顺序假设。

### 3.2 SoC 引脚（V1.2 p.1；V1.0 p.1 同）

| 信号 | SoC | 说明 |
|---|---|---|
| SEN0_D0..D7 | **PA0..PA7** | DVP 8-bit 数据 |
| SEN0_PCLK | **PA8** | DVP PCLK |
| SEN0_VSYN | **PA9** | VSYNC |
| SEN0_HSYN | **PA10** | HSYNC |
| SEN0_SCL | **PH0** | 摄像头 I2C SCL |
| SEN0_SDA | **PH1** | 摄像头 I2C SDA |
| SEN0_XCLK | **PH2** | MCLK/XCLK |
| SEN0_RESET | **PH3** | RESET |
| SEN0_PWDN | 网络 `SEN0_PWDN` | 原理图未显示直接接 SoC；按 vendor 板级数据 `pwdn_gpio = -1` |

**vendor 板级数据交叉验证**：`apps/demo/demo_DevKitBoard/board/wl82/DevKitBoard.c:460-490` 的 `camera0_data` 正是：
- `xclk_gpio = IO_PORTH_02`
- `reset_gpio = IO_PORTH_03`
- `pwdn_gpio = -1`
- `interface = SEN_INTERFACE0`
- `pclk_gpio = IO_PORTA_08`
- `hsync_gpio = IO_PORTA_09`
- `vsync_gpio = IO_PORTA_10`
- `data_gpio = {PA7, PA6, PA5, PA4, PA3, PA2, PA1, PA0, -1, -1}`
- `group_port = ISC_GROUPA`

---

## 4. 12-pin SPI/BT656 摄像头连接器（J4 / FPC_12P_0.5MM）

### 4.1 连接器引脚（V1.0 p.1；V1.2 p.1 同构）

| J4 引脚 | V1.0 网络 | V1.2 网络 | 说明 |
|---|---|---|---|
| 1 | VDD | SEN_VDD | 模块电源 |
| 2 | SCL | SEN_SCL | I2C SCL |
| 3 | SDA | SEN_SDA | I2C SDA |
| 4 | PCLK | SEN_SPI_CLK | SPI/BT656 时钟 |
| 5 | MCLK | SEN_XCLK | 摄像头 XCLK |
| 6 | DATA0 | SEN_SPI_D0 | SPI 数据 0 |
| 7 | DATA1 | SEN_SPI_D1 | SPI 数据 1 |
| 8 | RST | SEN_RESET | 复位 |
| 9 | PWDN | SEN_PWDN | 掉电 |
| 10 | NC | — | |
| 11 | NC | — | |
| 12 | GND | — | |

### 4.2 SoC 引脚（V1.0 p.1 与 V1.2 p.1 均可见）

| 信号 | SoC |
|---|---|
| SEN_SPI_CLK | **PA3** |
| SEN_SPI_D0 | **PA4** |
| SEN_SPI_D1 | **PA2** |
| SEN_SCL | **PH0** |
| SEN_SDA | **PH1** |
| SEN_XCLK | **PH2**（经 0R/R73 与 DVP0 XCLK 共用） |
| SEN_RESET | **PH3**（与 DVP0 RESET 共用） |
| SEN_PWDN | 网络 `SEN_PWDN`（与 DVP0 的 PWDN 网络同名/共用） |

**vendor 数据交叉验证**：`apps/demo/demo_DevKitBoard/include/spi_video.h:43` 指定 `JL_SPI2`，`:47` 指定 `CAM_XCLK_PORT = IO_PORTH_02`；SPI 摄像头示例 `apps/common/example/video/camera/spi/main.c` 用 `dev_open("video1.0")` 接收。SDK 的 `REGISTER_CAMERA1` 被 `gc0310_spi`/`gc6133_spi` 使用（`apps/common/camera/gc0310_spi/gc0310.c:780`、`apps/common/camera/gc6133_spi/gc6133.c:368`），说明 `camera1_dev`/`ISP_DEV_1` 是 **SPI 摄像头路径**，不是第二路 DVP。

---

## 5. 为什么双 DVP 不可行

### 5.1 板上没有第二路 DVP 连接器

- 24-pin DVP FPC 只有 `J1` 一个。
- 12-pin FPC `J4` 只有 `DATA0/DATA1 + PCLK + MCLK`，是 1/2-bit SPI/BT656，不是 8-bit DVP；它没有 D0-D7、VSYNC/HSYNC 的完整 DVP 总线。
- 原理图中没有第二个 `SEN1_*`/`DVP1` 连接器；`SEN1` 只出现在 WL82 的 IO 复用表里（芯片能力），不是本板连接器。

### 5.2 即使 DVP + SPI 也共用引脚

| 功能 | DVP0 (J1) | SPI CAM (J4) | 冲突 |
|---|---|---|---|
| 数据 | PA0-PA7 | PA2 (D1), PA3 (CLK), PA4 (D0) | **PA2/PA3/PA4 重叠** |
| I2C | PH0 (SCL), PH1 (SDA) | PH0 (SCL), PH1 (SDA) | **完全重叠** |
| XCLK | PH2 | PH2（经 0R） | **重叠** |
| RESET | PH3 | PH3 | **重叠** |
| PWDN | SEN0_PWDN | SEN_PWDN | 同名/共用网络 |

原理图还有两处直接证据：

- `DVP0 Camera 电源选择`（V1.0 p.1 x≈1439,y≈351；V1.2 p.1 x≈682,y≈635）与 `DVP-CAM` / `SPI-CAM` 标签，配合 R44-R50 一组 0R 选焊，说明两路是**二选一**的装配/供电选择，而不是同时供电。
- V1.2 p.1 扩展 IO 表明确写 **“扩展使用时需拔掉摄像头”**（x≈486,y≈358）。扩展排针 J18 暴露 PA5-PA10，与 DVP0 的 PCLK/VSYNC/HSYNC 重叠；这也证明相机与扩展/第二功能是互斥的。

### 5.3 SDK 的 dual-ISP API 不能替代板级连接

- `include_lib/driver/cpu/wl82/asm/isp_dev.h:203-218` 有 `camera1_dev` 和 `REGISTER_CAMERA1`，并声明 `isp1_mount/isp1_unmount/isp1_power`。
- `include_lib/driver/device/video/camera.h:95-96` 有 `DVP_SENSOR0(sel)` / `DVP_SENSOR1(sel)`：
  - `DVP_SENSOR0`: sel 0 = PC3-PC13，sel 1 = PA6-PA15
  - `DVP_SENSOR1`: sel 0 = PH1-PH11，sel 1 = PD0-PD10
- 这说明 **芯片/SDK 有两路 DVP 的 pin group 能力**，但 AC79_DevKitBoard 只把 Sensor0 的 PA 组引到 J1，没有把 Sensor1 的 PD 组引到第二个 DVP 连接器。`camera1_dev` 在本板上实际由 SPI 摄像头驱动使用，不是第二路 DVP。

> **资料冲突记录**：`camera.h:95` 的 `DVP_SENSOR0(sel=1)` 注释写 `PA6~PA15`，而 vendor `DevKitBoard.c:460-490` 的 `camera0_data` 实际使用 `PA0~PA10`。两者不完全一致；本报告以 **vendor 板级数据和原理图** 为准，`camera.h` 宏注释只作为 pin group 能力参考。若未来要在其它板卡上复用该宏，必须重新核对宏定义与实际 group。

### 5.4 显示 MCU8080 的复用关系

板载 ILI9488 320x480 面板走 MCU8080/EMI 总线：

| LCD 信号 | SoC |
|---|---|
| EMI_WR / LCD_HSYNC | PC10 |
| RS / LCD_VSYNC | PC9 |
| EMI_D7 / LCD_DCLK | PC8 |
| EMI_D6..EMI_D0 / LCD_D0..D6 | PC7..PC1 |
| TE / LCD_D7 | PC0 |
| LCD_BL / LCD_DE | PB8 |

（V1.2 p.1 IO 表；`boards/JIELI/AC79_DevKitBoard/board_com_api.c:49-52,93-114` 注册 320x480 ILI9488 MCU8080 面板。）

**结论**：
- DVP0 使用 PA0-PA10 + PH0-PH3，与 LCD 的 PC0-PC10 + PB8 **不重叠**，所以 **显示 MCU8080 不阻塞 DVP0**。
- 但 `DVP_SENSOR0(sel=0)` 的 PC3-PC13 组会与 LCD 的 PC 总线冲突；本板用的是 PA 组（`ISC_GROUPA`），所以避开了 LCD。若有人试图把 DVP0 改到 PC 组，会直接撞显示。
- 扩展排针 J18 暴露 PA5-PA10，与 DVP0 的 PCLK/VSYNC/HSYNC 重叠；使用扩展时需拔掉摄像头（V1.2 p.1 明确标注）。
- PH2/PH3 同时是 TKL UART1（SDK uart2）和 PWM0 的默认引脚（`boards/JIELI/AC79_DevKitBoard/Kconfig:91-113`：UART1 TX/RX=114/115，PWM0=114，即 PH2/PH3）。因此启用 DVP0 会与 UART1/PWM0 互斥；当前 board 默认 UART1 off，但这是必须在集成时显式处理的冲突。
- 摄像头 I2C 在 PH0/PH1；当前 TuyaOpen board Kconfig 的 IIC0 默认是 0/1（PA0/PA1）、IIC1 默认是 7/8（PA7/PB0），与 vendor 板级的 PH0/PH1 不一致。若未来做单 DVP 集成，必须把 IIC0（或实际使用的 IIC）改到 PH0/PH1（flat 112/113），否则 sensor probe 不会成功。

---

## 6. 当前 TuyaOpen 侧的 camera 状态

- `boards/JIELI/AC79_DevKitBoard/board_com_api.c` **没有任何 camera 注册**（没有 camera0/1、DVP、SPI camera）。
- 平台 `platform/JIELI/tuyaos/tuyaos_adapter/` 下**没有** `tkl_dvp.c`/`tkl_csi2.c`/camera 适配；主仓 `src/peripherals/camera` 有 DVP TDD，但 JIELI 平台没有对应的 TKL DVP 实现。
- 因此“camera1 注册为 -1”描述的是 **vendor DevKitBoard.c**（`camera1_data`），不是 TuyaOpen 板级文件。TuyaOpen 侧连 camera0 都未注册。
- 任务 05 的阶段 A 结论是 **blocked**；在拓扑被证伪的情况下，不应为了“补上 camera1”而虚构第二路 DVP，也不应把 SPI 摄像头当作第二路 DVP 验收。

---

## 7. 若要支持双 DVP，需要什么（不是本板）

要满足“两路 DVP 同时采集”，至少需要：

1. 一块有**两个独立 DVP 连接器**的板卡，分别接到 `DVP_SENSOR0` 和 `DVP_SENSOR1` 的非重叠 pin group（例如 Sensor0 用 PA/PC 组，Sensor1 用 PD/PH 组），并保证两组不同时占用 LCD/扩展排针。
2. 两个 sensor 的 I2C 独立（或至少可分别寻址/分时），XCLK/RESET/PWDN 独立；不能像本板这样共用 PH0-PH3。
3. 两路 ISP/DMA/帧缓冲带宽与内存预算：本板 8 MiB SDRAM（`hardware-ledger.md` 记录 `Free psram heap size:7407688`），双 VGA YUV422 + ISP + 编码的预算需重新核算。
4. 实板证据：两块 sensor 的 SKU/线束、两路 probe、分别有效帧、同时有效帧、丢帧/帧率/内存/30 分钟稳定性。
5. 显示与相机 pin group 的最终复用表（本板 LCD 占 PC0-PC10；不要把 DVP 放到 PC 组）。

在这些条件满足前，**不要接线、不要改板、不要把扩展排针上的 PD 组当作已验证的第二 DVP**。

---

## 8. 所需实板验证（阶段 A 收尾清单）

1. 实板丝印/revision：V1.0 还是 V1.2；与 `audio_config.h:12` 的 V1.2 说明是否一致。
2. 板上相机连接器实物：是否只有 `J1`（24-pin）和 `J4`（12-pin）；是否有未装配的第二 DVP 位。
3. `J1` 插入的 sensor 模组型号（GC0308/GC2145/其它）、I2C 地址、供电；`J4` 是否插 SPI 模组（GC0310/GC6133）。
4. 万用表/通断确认 `J1`/`J4` 到 PA0-PA10、PH0-PH3 的对应关系，特别是 PA2/PA3/PA4 与 PH0-PH3 的共用。
5. R44-R50 的 0R 装配：DVP-CAM 还是 SPI-CAM；两路是否真的二选一。
6. 扩展排针 J18 使用相机时是否必须拔掉（V1.2 p.1 已标注“扩展使用时需拔掉摄像头”）。
7. UART1/PWM0 是否被产品占用（PH2/PH3）；若占用，DVP0 不可用。
8. 若坚持双摄：提供第二块板/第二连接器的原理图与 pin group，重新做阶段 A；不要在本板上继续写第二路 DVP 代码。

---

## 9. 与任务 06 的边界

| 项 | 05 AC791 | 06 AC792N |
|---|---|---|
| 复用对象 | 一路 DVP + 一路 SPI 连接器共用 PA2-PA4/PH0-PH3 | DVP0 与 CSI-2 共用 PD11-PD14 |
| 结论 | 双 DVP 不可行；DVP+SPI 在本板也不可并发 | DVP0/CSI-2 不能并发；DVP0 优先、CSI-2 独立验收 |
| 修复方向 | 换板/第二 DVP 连接器 + 非重叠 pin group | 二选一 Kconfig；DVP0 优先；CSI-2 独立验收 |
| 共享 owner | camera API/平台 adapter 与 06 共用，但本任务阶段 A 不改代码 | 已产出集成方案 |

**不要把 06 的“DVP0/CSI-2 独立验收”直接套到 05**：05 的问题不是“两路接口二选一”，而是“板上根本没有第二路 DVP 连接器”。

---

## 10. 本轮明确未做

- 未修改主仓或平台仓代码；未新增 camera 适配。
- 未构建、未烧录、未上电、未接串口、未碰 COM8。
- 未做 sensor probe、有效帧、双路并发、丢帧、帧率、内存或 30 分钟稳定性测试。
- 未确认实板 revision、连接器实物、sensor SKU 或 0R 装配。
- 未推送、未发 PR。

## 11. 关键证据索引

| 证据 | 路径:行号 / 页码 |
|---|---|
| 24-pin DVP FPC `J1` 引脚与 SoC 映射 | `.../JL_AC79_DevKitBoard V1.2.pdf` p.1；`.../JL_AC79_DevKitBoard V1.0原理图.pdf` p.1 |
| 12-pin SPI FPC `J4` 引脚与 SoC 映射 | 同上 p.1 |
| DVP0 `camera0_data` / `camera1_data=-1` | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/apps/demo/demo_DevKitBoard/board/wl82/DevKitBoard.c:460-511` |
| `video1` 为 SPI 摄像头 | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/apps/wifi_camera/board/wl82/board_develop.c:651`；`apps/wifi_camera/spi_video.c:415` |
| `REGISTER_CAMERA1` 用于 SPI sensor | `apps/common/camera/gc0310_spi/gc0310.c:780`；`apps/common/camera/gc6133_spi/gc6133.c:368` |
| dual-ISP / camera1_dev 能力 | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/include_lib/driver/cpu/wl82/asm/isp_dev.h:203-218` |
| DVP_SENSOR0/1 pin group | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/include_lib/driver/device/video/camera.h:95-96` |
| SPI camera 使用 SPI2 / PH2 XCLK | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/apps/demo/demo_DevKitBoard/include/spi_video.h:43,47` |
| 摄像头 I2C 在 PH0/PH1 | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/apps/demo/demo_DevKitBoard/board/wl82/DevKitBoard.c:163-173` |
| 显示 MCU8080 引脚 PC0-PC10/PB8 | `.../JL_AC79_DevKitBoard V1.2.pdf` p.1 IO 表；`boards/JIELI/AC79_DevKitBoard/board_com_api.c:49-52,93-114` |
| UART1/PWM0 与 PH2/PH3 冲突 | `boards/JIELI/AC79_DevKitBoard/Kconfig:91-113` |
| 板级 V1.2 说明 | `boards/JIELI/AC79_DevKitBoard/audio_config.h:12` |
| SDK README 的 DVP+SPI 声明 | `platform/JIELI/chip/wl82/AC79_AIoT_SDK/README.md:117` |
| 扩展使用需拔摄像头 | `.../JL_AC79_DevKitBoard V1.2.pdf` p.1（x≈486,y≈358） |
| IO 复用表 | `.../JL_AC79_WIFI V1.0 IO功能列表.pdf` p.3-4 |
