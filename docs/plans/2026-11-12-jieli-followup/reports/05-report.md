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
