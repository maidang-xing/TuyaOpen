## 内容

AC791（WL82）开发板两路摄像头的平台侧实现：DVP（GC2145，J1/video0）与 SPI（GC0310，J4/video1）在 TKL 层统一交付 packed YUYV，SPI 接收采用原厂的逐行中断 + DMA 行拷贝。

- 新增 WL82 DVP/SPI 摄像头 TKL 与 staging（`tkl_dvp.h` 契约、板级配置、feature map）
- DVP 按标准 `tkl_dvp` v2 拉模式实现：ch1（YUV 处理通道）`GET` 拉取 + 帧池背压、生命周期事件、init/start/stop/deinit 状态机
- DVP 平面 YUV422P 在 TKL 交错成 packed YUYV，并在 CPU 读取前 `flushinv_dcache`；overlay 缓冲使用 32 字节对齐静态 BSS 数组（厂商 storage class）
- SPI 逐行 IRQ 接收（`SPI: line-IRQ mode, 8 line buffers`），每行一次中断重装 1288 B + ISR 内 `dma_copy_async`，字节滑移容忍、丢行补齐、标签失配重同步
- SPI 帧缓冲在 CPU 读取前 `flushinv_dcache`（行中断与整包两条路径）
- TDD 模式下由 TDD 负责 SPI 传感器上电/复位/寄存器表（`JIELI_CAMERA_TDD_SENSOR`）

## 实机证据（AC791 DevKitBoard）

| 路径 | 结果 |
|---|---|
| DVP / J1（GC2145，320×240@20） | 129.8 s / 3,720 帧 / drops=0 / 零异常（≈28.7 fps）；`52b1322` 复验 133.4 s / 3,820 帧 / drops=0（≈28.6 fps）；**目视满屏** |
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
