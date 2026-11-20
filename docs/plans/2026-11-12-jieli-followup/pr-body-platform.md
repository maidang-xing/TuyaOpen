## 内容

AC791（WL82）开发板两路摄像头的平台侧实现：DVP（GC2145，J1/video0）与 SPI（GC0310，J4/video1）在 TKL 层统一交付 packed UYVY，SPI 接收采用原厂的逐行中断 + DMA 行拷贝。

- 新增 WL82 DVP/SPI 摄像头 TKL 与 staging（`tkl_dvp.h` 契约、板级配置、feature map）
- DVP 走与 T5AI 相同的推模型：`TUYA_DVP_CFG_T.inter_cfg` 的 `assign_cb` / `post_cb` 交付帧，TKL 不持帧池（2026-11-20 起；此前为 v2 拉模式，见下方历史小节）
- DVP 平面 YUV422P 在 TKL 交错成 packed UYVY，并在 CPU 读取前 `flushinv_dcache`；overlay 缓冲使用 32 字节对齐静态 BSS 数组（厂商 storage class）
- SPI 逐行 IRQ 接收（`SPI: line-IRQ mode, 8 line buffers`），每行一次中断重装 1288 B + ISR 内 `dma_copy_async`，字节滑移容忍、丢行补齐、标签失配重同步
- SPI 帧缓冲在 CPU 读取前 `flushinv_dcache`（行中断与整包两条路径）
- TDD 模式下由 TDD 负责 SPI 传感器上电/复位/寄存器表（`JIELI_CAMERA_TDD_SENSOR`）

## 2026-11-20 接口统一：改回 T5 的推模型（`e475bdd4`）

为了让两个平台跑同一份 TDD 核心，DVP 的交付方式从 v2 拉模式改成 T5AI 的推模型：

- `tkl_dvp.h` 只剩 `tkl_dvp_init(TUYA_DVP_CFG_T *)` / `deinit` / `start` / `stop`；`tkl_dvp_get_frame`、`tkl_dvp_request_idr`、`TKL_DVP_HAS_PULL_API` 全部删除。
- `tkl_dvp.c` 删掉 TKL 自己的 `TUYA_MEDIA_BUF_T` 环形池、就绪信号量与 free_cb；转换线程改为 `assign_cb` 向 TDD 取缓冲 → 1:1 中心裁剪 + UYVY 打包 → `post_cb` 上交。分配不到缓冲时计丢弃，不回收已交付的帧。
- v2 的生命周期 `event_cb`（INITIALIZED/STARTED/STOPPED/DEINITIALIZED）随接口移除；`sync_polarity` 的校验和 1:1 裁剪保持不变。
- 收益：应用仓 `1425132f` 净减 667 行，杰理侧不再有传输代码；两平台共用 `tdd_camera_dvp.c`。

构建：JIELI `dvp_lcd`、`spi_lcd` 与 T5AI 三个 profile 均 0 error，逐项核对编译文件（杰理 = 共享核心 + 两颗传感器 + i2c；T5AI = 共享核心 + gc0308/gc2145/ov2640 + i2c）。

实机（镜像 `fa59e342`）：`gc2145 id init/restart: rt=0 2145` → `DVP: TDD sensor restart ret=0` → `SET_FMT ok … 614400` → `DVP capture started` → `first frame: 320x480 fmt=1 len=307200` → `first frame delivered`；213 行日志零异常，**用户目视画面与颜色正常**（`serial-COM8-20261120-104942.log`）。SPI 模组本轮仅构建验证，实机需断电换模组后复验。

## 2026-11-20 setup_cb 取代板级 restart 钩子（`331f2c76`）

- `video0.2` 打开（使能 XCLK）后、SPI2 武装后，TKL 改为调用 `TUYA_DVP_CFG_T.inter_cfg.setup_cb`；共享核心在该回调里重放 reset + init + set_ppi（几何取 open 时的 ppi/fps），startup 时序与原先一致。
- 板级 `tkl_camera_sensor_restart()` 与其按传感器分叉的两个实现删除，平台不再需要知道传感器类型。
- 实机（镜像 `92189ea1`）：`DVP: TDD sensor restart ret=0` → `SET_FMT ok … 614400` → `first frame: 320x480` → `first frame delivered`，218 行零异常，画面正常。SPI 路径同一个 setup_cb，待换模组实机复验。

## 实机证据（AC791 DevKitBoard）

| 路径 | 结果 |
|---|---|
| DVP / J1（GC2145，320×240@20） | 129.8 s / 3,720 帧 / drops=0 / 零异常（≈28.7 fps）；`52b1322` 复验 133.4 s / 3,820 帧 / drops=0（≈28.6 fps）；**目视满屏**。2026-11-19 传感器改由 TDD 拥有后重新实机：**35.3 s / 571 帧 / drops=0 / ≈17.6 fps，零 `video_ioct` / `isc line err`，画面与颜色目视正常** |
| SPI / J4（GC0310，640×480） | ≈4.47 fps、481.0 行/帧、`tag_err=0`、零异常；**目视颜色正常** |

## 2026-11-18 SPI/J4 实机验收（平台 head `52b1322`）

- 镜像：`spi-base3-556e7662.bin`（`spibase`，430,692 B，SHA256 `556e7662…`）= 本 PR head `52b1322` + 应用 `8304d183` + 显示 TE 修复（fork #6）。
- 启动链：`gc0310 chip id: 0xa310 (expected 0xa310)` → `gc0310 spi init done` → `SPI: spi2 ok` → `line-IRQ mode, 8 line buffers` → `SPI capture started (640x480, line IRQ, first 1292 B / line 1288 B)` → `TDD sensor restart ret=0` → `first frame: 640x480 fmt=1 len=614400`。
- 运行：**≈4.47 fps**（83 帧 / 15 s）、**481.0 行/帧**、`tag_err=0`、无异常、无缺行累积。
- **偏紫/偏粉已解决**：根因是应用侧 `e9a592c4` 重建的 GC0310 初始化表（此前的表误收了厂商块注释里的备选 gamma/AWB/CC/HB 写入）；修正后用户目视确认颜色正常。本 PR head 的 SPI DMA 帧缓存失效（`52b1322`）在此轮一并实机通过。
- 日志：`serial-COM8-20261118-163917.log`、`serial-COM8-20261118-163541.log`。
- 说明：板上音频功放空闲时会把摄像头 SPI 总线耦合噪声放大成“叮叮”声（PA 为兼容 chatbot 示例保持常开），属既有板级行为，非摄像头缺陷，本轮不处理。

## 已知限制

- DVP 与 SPI 共用 PA2/PA3/PA4 与 PH0–PH3，且两模组同为 I2C 地址 0x42，**两路同时采集硬件不可行**；同时插会误探针并触发 `video.c:558` 断言复位。
- SPI 链路 2 线 + 字节碎片，VGA 上限约 4–5 fps（厂商参考 5 fps）；传感器固定 VGA，320×240 由主机侧抽取。
- `stop` join 超时、`cam_proc` 创建失败等错误分支未注入故障，为代码级验证。

## 2026-11-19 帧尺寸：1:1 居中裁剪（`1a3b4809`）

- `tkl_dvp` 原来按整数比例抽样（step_x/step_y），把 640×480 抽成 320×240 之类；面板是 320×480 时既会拉伸也会丢行。
- 改为 `yuv422p_crop_to_packed_yuyv()`：取传感器中间 `out_w × out_h` 的区域，**像素 1:1 拷贝，不缩放**，超出部分不交付。
- `tkl_dvp_init()` 的校验随之从"必须是整数比例"改成"只要不超过传感器尺寸"（宽度仍需偶数，色度对才对齐）。
- 应用侧配套：`dvp_lcd` 预览请求 320×480（应用 PR #2 `77e3aab3`）。实机 `dvpcrop` `2ccac400` 目视画面正常、无拉伸。

## 2026-11-19 同步极性：编不出来就拒绝（`3a166adb`）

- `tkl_dvp_init()` 此前只校验 `sync_polarity` 的范围（0..3）然后丢弃。本平台注册的传感器描述符固定为 HSYNC/VSYNC 都高有效，且厂商描述符是静态的、注册后改不了，所以其它取值**永远达不到**——静默忽略违反接口自身"不支持必须拒绝"的约定。
- 改为：越界仍 `OPRT_INVALID_PARM`，合法但不支持的极性返回 `OPRT_NOT_SUPPORTED`。
- 语义澄清并写进 `docs/jieli-tkl.md`：`sync_polarity` 只表达**极性**；WL82 的 **H-V SYNC 互换**是线路映射，由板级 `JIELI_BOARD_CAMERA_HV_SYNC_SWAP` 在编译期写入描述符 `.sync_config`，不经过该字段。
- 构建：`dvp_lcd`、`spi_tdd` BUILD SUCCESS（应用仓同 head `17b39cdb`）。

## 构建

`spi_lcd`、`dvp_lcd`、`spi_tdd` 三个 profile 在本分支 head 上均 BUILD SUCCESS。

## 2026-11-18 共享 TDD 兼容（`dbe48765`）

- `tkl_dvp.h` 增加能力宏 `TKL_DVP_HAS_PULL_API`：共享的 `src/peripherals/camera/tdd_camera` 据此选择拉取实现。仍使用 v1 回调 API 的平台（T5AI、ESP32）不定义该宏，继续编译原有实现，互不影响。
- 验证组合：JIELI `spi_tdd` / `dvp_lcd` BUILD SUCCESS；T5AI `TUYA_T5AI_BOARD` 摄像头板在与应用 PR #2 的组合下 BUILD SUCCESS。
- 实机回归（本头构建）：`spi_tdd` 干净构建烧录后连续 **175 s / 776 帧 / 4.43 fps / 480.0 行/帧 / `tag_err=0` / 零异常**，用户目视 SPI 画面正常（`serial-COM8-20261118-175026.log`）。
- 后续项：T5AI TKL DVP 迁移到 v2（平台仓独立 PR + T5 硬件验证）后，可删除共享核心中的 legacy 分支。

## 2026-11-17 更新：TDD 归属 sensor 的 SPI 路径实机通过

- 新增 `a6b181bb`：WL82 的 400 kHz 门限只约束硬件 iic1；SPI 摄像机构建把 iic1 注册为 PH0/PH1 软件 IIC 后，TDD 以 100 kHz 初始化不再被 `OPRT_NOT_SUPPORTED` 拒绝（此前 camera open 直接 `-4` 退出）。
- 实机（COM8，`serial-COM8-20261117-184501.log`）：GC0310 chip id `0xa310` → 整表 + page3 SPI 块 → `first frame 640x480 len=614400` → `TDD sensor restart ret=0`；85 s 长稳 frames 382→793（≈4.84 fps）、`lines/frame≈481`、`tag_err=0`、零复位。
- DVP/J1 的“反复重启”已定位为**共享总线干扰**：J1 与 J4 共用 PA2/PA3/PA4 与 PH0–PH3，两模组同时连接时 vendor video0 探测读到 GC0310 的 `0xa310` 并误判，随后 `video.c:558` 断言复位。

## 2026-11-17 评审修复（平台侧）

- `db957bf0`：`tkl_i2c_reset()` 不再对软件口应用硬件 IIC1 的 pinmux，否则 SPI 构建复位 iic1 会把 PH0/PH1 抢回硬件控制器。
- `5bdc5c60`：SPI 行缓冲分配失败路径补全回收（预览缓冲失败时此前会泄漏行缓冲与三帧，约 1.85 MB）。
- `6c36e54f`：行标签失配后按厂商 `camera_reinit` 语义重同步（帧边界重新武装 + 传感器重启，200 ms 冷却），并停止在预览尺寸非整除因子时交付上一帧。

## 2026-11-18 更新：标准 tkl_dvp v2 接口 + AC791 实机出帧打通

- **接口**：`a4aab851` 按 `rtos-kernel/dvp` 标准头重写 WL82 DVP：ch1（YUV 处理通道）`GET` 拉模式 + 2 块 64 字节对齐帧池（`TUYA_MEDIA_BUF_T` / `free_cb` / 背压丢帧）、ch2/ch3 与 `request_idr` 返回 `OPRT_NOT_SUPPORTED`、生命周期事件、init/start/stop/deinit 状态机。
- **实机根因与修复（`660f2f59`）**：ISP 的 YUV422 overlay 缓冲必须是**厂商式的 32 字节对齐静态数组（SDRAM .bss）**。用 `tkl_system_malloc()` 的堆缓冲时，任何对已完成帧的读取（memcpy / 平面→打包转换）都会让下一次 `GET_OVERLAY` 或 ISP 帧中断报 `axi_rd_inv`/`c1_if_bus_inv`/`c0_wr_bus_inv`；`sdk.elf` 符号化显示故障 PC 落在 `isc_sen_isr0` 与 `wl80_video1_ioctl`。改为静态缓冲后消除。
- **处理解耦**：采集任务只做 `GET → flushinv → memcpy 到 2 槽 staging → CLEAR`（厂商缓冲在 1 ms 内归还），独立的 `cam_proc` 任务做平面 YUV422P→packed YUYV 转换（volatile 字节读 + 对齐 32 位写，规避 pi32v2 的非对齐宽访问）并投递 TDL。
- **实机证据**：`dvpstatic`（`e07ebdb7…`）连续 **91.6 s / 2700 帧、drops=0、零异常**（约 29.5 fps）；`dvpfill`（`ed1b448e…`）**用户目视确认画面铺满 320×480 屏**。日志：`serial-COM8-20261118-143831.log`、`serial-COM8-20261118-144832.log`。
- 配套应用侧：示例铺满改动 + 几何保护（maidang fork PR #2，`845048d2`、`8304d183`）

## 2026-11-18 评审加固（`6562c41` / `52b1322`）

- DVP-only 静态量（含 153,600 B overlay 数组）移入 `JIELI_CAMERA_PATH_DVP` 门内：此前 SPI-only 构建会无条件保留 614,400 B SDRAM `.bss`，直接压缩 SPI 帧池可用堆。
- 启动/停止生命周期：`cam_proc` 创建失败时先等采集任务观察到 `s_running=0` 再释放其句柄；采集任务自行退出后的重启先 join 旧 `cam_proc`（失败则拒绝启动）；`stop` 检查转换任务 join 超时，失败时保留整个会话与缓冲。
- 拒绝奇数宽度（打包转换按偶数宽做对齐 32 位写）。
- `GET_OVERLAY` 前恢复 `memset(&yuv)`，且 `flushinv_dcache` 只覆盖实际拷贝的 `len`。
- `52b1322`：SPI 接收路径在 CPU 读取前对 DMA 帧缓冲执行 `flushinv_dcache`（行中断与整包两条路径），避免复用帧池条目时读到上一轮的缓存行——**已随 2026-11-18 SPI 实机验收通过**。
- 实机复验（DVP）：`dvphard`（`dvp-v2-harden-1461b9eb.bin`）启动链全绿；`dvpharddiag` 连续 **129.8 s / 3,720 帧 / drops=0 / 零 exception**；`52b1322` 头复验 **133.4 s / 3,820 帧 / drops=0**。
- 配套：显示 TE 引脚未初始化修复（PR maidang-xing/TuyaOpen#6），未合并时单独构建需带上，否则 `display init rt=-2`。

## 2026-11-18 评审清理：`tkl_dvp` 只保留 TDD 传感器路径（`4918f5c2`）

SPI 路径原先还挂着一套厂商 `video1.0` 传感器开流逻辑（`spi_sensor_open_thread` + `s_sensor_ready/s_sensor_done` 握手），由 `JIELI_CAMERA_TDD_SENSOR` 与 TDD 路径二选一。TDD 路径验收通过后删除该分支：提前开 PH2 XCLK、SPI2 armed 后重启传感器、teardown 在 SPI 路径上全部变为无条件行为（**-140 行**）。

同时把厂商 `gc0310.c` 还原为原厂版本：此前的 9 行补丁（ID 页切换 + 复位后延时）只服务那条已删除的厂商路径；TuyaOpen 侧寄存器表由 TDD 传感器驱动负责，vendor 文件保持原样。

> ⚠️ 必须与应用仓 `camera/tdd-refactor` 的 `ba696866`（PR #2）同落：本文件的 SPI 分支读同一个 Kconfig 宏 `JIELI_CAMERA_PATH_SPI`。

验证：AC791 `spi_tdd` 用本树构建、烧录 → 640×480 4.43 fps、480.0 行/帧、`tag_err=0`、175 s 零异常；DVP profile 构建通过。

## 2026-11-18 IIC 总线 A/B：硬件 IIC1 vs 软件 IIC（`fc558435`）

评审的问题是"为什么不用 TKL pinmux / 硬件 IIC 上 PH0/PH1"。现状澄清：**IIC 引脚本来就是 pinmux + 板级配置**——共享 TDD 在 `tdd_dvp_i2c_init()` 里对 `cfg->clk/sda`（板级传 112/113 = PH0/PH1）调用 `tkl_io_pinmux_config()`；`tkl_i2c.c` 只决定 vendor `iic1` 设备节点注册成硬件还是软件控制器。本提交把该节点里硬编码的 `IO_PORTH_00/01` 改成读 `TUYA_BOARD_IIC1_SCL/SDA_PIN`（板级 Kconfig 在 SPI 路径下默认就是 PH0/PH1），引脚名只留一处。

A/B（同一棵树，只改节点类型；`spi_tdd` profile，AC791 + J4 GC0310）：

| 变体 | iic1 节点 | 结果 |
|---|---|---|
| A | 硬件 IIC，PH0/PH1 | ID 探测与整张寄存器表**都通过**，但 SPI 收到 **0 帧**、`tag_err` ≈2700/s、画面全无 |
| B | 软件 IIC（bit-bang），PH0/PH1 | **4.44 fps / 479.8 行/帧 / `tag_err=0`** |

结论：硬件控制器在这两个焊盘上不可用——它们与传感器 2-wire SPI 的时钟/数据复用，硬件 IIC 占用引脚后 SPI 收不到有效行。软件 IIC 保留。日志 `logs/boot-ab-hwiic.log`（A）、`logs/serial-COM8-20261118-192756.log`（B）。

> 更正早前记录：此前"硬件 IIC 会让开机静默"的结论**作废**——那次是在清理未完成的中间树上测的，A/B 不可信；本次在最终树上重做，变体 A 能正常启动并完成 I2C 初始化全程。

## 2026-11-19 评审收敛：IIC 选择下沉到板级（`1cbd64c1`）

- `tkl_i2c.c` 不再读摄像头宏：板级 staging 一个 `JIELI_BOARD_IIC1_SOFTWARE`，适配层只判断"软件/硬件"；iic0 的 SPI 专用分支一并删除，引脚全部来自板级 Kconfig（`TUYA_BOARD_IIC0/1_*`）。
- `board_config.py`：删掉板级已不存在的 SPI 采样沿分支（staged 值本来就是厂商 demo 的 SMPH），去掉 `video1` 旧措辞。

构建/实机：`spi_tdd`、`dvp_lcd` 均 BUILD SUCCESS；`spi_tdd` 实机 4.4 fps、`tag_err=0`、启动日志零 E 级错误。

## 2026-11-19 结构对齐：平台只认板级 flag（`720bb6b0`）

`tkl_dvp.c` 与 staging 工具改读 `JIELI_BOARD_CAMERA_DVP/_SPI` 和 `JIELI_BOARD_CAMERA_WIDTH/HEIGHT/FPS`——这些是板级从"Camera module"选择里 staged 出来的。适配层里不再出现传感器名或"路径"字样；没 staged 时保留 640×480 兜底。

验证：`spi_tdd`、`dvp_lcd` BUILD SUCCESS；`spi_tdd` 实机 4.4 fps / `tag_err=0` / 60 s。

## 2026-11-19 步骤 1：DVP 的传感器交给 TDD，TKL 只描述时序（`24003fe3`）

应用侧 PR #2 `13af27fe` 把 DVP 传感器翻给 TDD（与 SPI 路径同形）后，`tkl_dvp.c` 不能再扮演"厂商探针"：它注册的 `REGISTER_CAMERA` 会被厂商 ISP 的 `sensor_check` 选中并写寄存器表，与 TDD 刚写的表互相覆盖。本次改为**只描述时序的空壳描述符**：

- `REGISTER_CAMERA(GC2145_TIMING)`：logo/总线/并口极性/几何/fps 保持真实值，`ops.sensor_check/init/set_size_fps` 全部返回 0、不写任何寄存器。厂商 ISP 仍能锁存 DVP 并口时序与几何，但不再碰传感器。
- 几何来自板级 staging 的 `JIELI_BOARD_CAMERA_WIDTH/HEIGHT/FPS`（`DVP_SENSOR_WIDTH/HEIGHT`），`VIDIOC_SET_FMT` 也按传感器几何下，与 TDD 写进传感器的分辨率一致。
- 抽取交付：`yuv422p_to_packed_yuyv(src, dst, in_w, in_h, out_w, out_h)` 按 `step_x/step_y` 从传感器几何抽到调用方几何；`dvp_prepare()` 的暂存缓冲按传感器尺寸，交付缓冲按 `s_width×s_height×2`；`init` 的几何校验从"必须相等"放宽为"整数倍"。DVP profile 因此是"传感器 640×480 / 预览 320×240"。
- `board_config.py`：staged `tuya_camera0_data.reset_gpio` 置 -1（复位归 TDD，与 SPI 分支同形）。
- `sdk_overlay.py`：DVP 构建剔除厂商 `camera/gc2145_dvp/gc2145.c`，探针不可能再选中厂商驱动；SPI 构建的剔除逻辑不变。

验证：

- `spi_tdd`、`dvp_lcd` BUILD SUCCESS；staged Makefile 对应 `-DJIELI_BOARD_CAMERA_DVP=1`（640×480@20）与 `-DJIELI_BOARD_CAMERA_SPI=1`。
- 无模组启动：平台选中 `GC2145_TIMING`（日志 `sensor info: 640x480 fps=20` → `SET_FMT ok` → `DVP capture started`），说明厂商探针已被绕开且时序锁存正常。
- **帧率/画面未复验**：J1 模组当前未接，`video_ioct: err = 11`（EAGAIN）每秒一条，与"传感器不在位"一致。

## 2026-11-19 步骤 1 实机：两处配套修复（`e2413586`、`15f00ae5`）

应用 PR #2 的 `13af27fe` 让 TDD 拥有 DVP 传感器后，上板一帧不出。平台侧两处修复：

- **`e2413586` 时序**：`video0` 的 open 才使能传感器时钟（PH2 是 ISC 输出通道）并把 ISP 拉起来，而 TDD 的表写在 `tkl_dvp_start()` 之前，等于写给了没有时钟的传感器（读回 0xffff）。在 `dev_open("video0.2")` 成功后调用板级的 `tkl_camera_sensor_restart()`——和 SPI 路径 arm 完 SPI2 后用的是同一个钩子，本板上它重放复位 + 寄存器表。
- **`15f00ae5` H/V 同步**：厂商 GC2145 描述符把 `.sync_config` 包在 `#if CONFIG_CAMERA_H_V_EXCHANGE` 里（WL82/AC791 为 SYNC0=VSYNC、SYNC1=HSYNC）。我们的时序描述符没写这一项，表现是持续 `[VIDEO_ISC]isc line err`、零帧。该宏只在厂商 `app_config.h`，适配层看不到，所以 staging 对 DVP 构建另发 `-DJIELI_BOARD_CAMERA_HV_SYNC_SWAP=1`，`tkl_dvp.c` 据此决定是否写 `.sync_config`。

实机（AC791 + J1 GC2145）：修好后 35.3 s / 571 帧 / drops=0 / ≈17.6 fps，零 `video_ioct` / `isc line err`，画面与颜色用户目视正常（`logs/serial-COM8-20261119-135349.log`）。YUV 序的第三处修复在应用侧（`85174cf3`，`.yuv_order = YUYV`）。

## 2026-11-19 评审收敛：接口不重复定义、IIC 端口下沉、统一整数类型

- **`9fdfb9f8` 极性枚举**：`tuya_cloud_types.h` 里本来就有 `TUYA_DVP_SYNC_MODE`（公共内容），`tkl_dvp.h` 不再自带一套：改为 `typedef TUYA_DVP_SYNC_MODE TUYA_DVP_SYNC_MODE_E;`，值不变，旧枚举保留原名（撤销此前的 `_V1_` 改名）。`TUYA_FRAME_FMT_H264_I` 未添加——只有 `request_idr()` 之后才会产生该格式，本移植的 ch3 返回 `OPRT_NOT_SUPPORTED`。
- **`508658f4` IIC 端口**：删除板级 `JIELI_BOARD_IIC1_SOFTWARE`。应用只按端口号 `tkl_i2c_init()`；端口是硬件还是软件由适配层按芯片与板级引脚在编译期决定（`JIELI_IIC1_IS_SOFTWARE`）：软件 IIC 支持任意 GPIO，硬件 IIC 只支持控制器映射内的引脚（PA7/PA8、PC1/PC2、USB DPA/DMA），其余自动回退软件；PH0/PH1 是摄像头 2-wire 总线，恒走软件。`docs/jieli-tkl.md` 增加端口表、引脚规则与默认推荐引脚（PA0/PA1、PA7/PA8）。
- **`5261adf0` 类型**：`tkl_dvp.c` 的 `u8/u16/u32/s32` 等 135 处换成 TuyaOpen 的 `UINT8_T/UINT16_T/UINT32_T/INT32_T`；厂商 `sensor_ops` 的函数指针签名底层类型一致，不受影响。

验证：`dvp_lcd`、`spi_tdd` BUILD SUCCESS；DVP 镜像与改前逐字节比对只差 3 字节（构建戳），其余完全一致。

### 类型拼写最终收敛（`ee416a7c`）

核对后发现 TuyaOpen 的约定是 **stdint**：应用仓 `src/` 里 `uint32_t` 有 40,864 处而 `UINT32_T` 只有 13 处（且都在自带别名的 `tuya_ai_service` 里），平台其它 TKL 头（gpio/pwm/spi/uart/i2c）也全是 `uint*_t`；唯一的例外是 registry 的 v2 `tkl_dvp.h`。因此**不往 `tuya_cloud_types.h` 补 `UINT*_T/VOID_T`**，而是把 vendored 的契约头与 `tkl_dvp.c` 改成 `uint8_t/uint16_t/uint32_t/uint64_t/void`。DVP 镜像与改前仅差 5 字节构建戳。
