## 内容

AC791（WL82）开发板两路摄像头的驱动与分层改造，全部走 TuyaOpen 的 TDD/TDL 契约：

| 连接器 | 传感器 | 路径 | 状态 |
|---|---|---|---|
| J1（24-pin DVP） | GC2145 | video0 + DVP v2 拉模式 | ✅ 实机 320×240@20，129.8 s / 3,720 帧 / drops=0，目视满屏 |
| J4（12-pin SPI） | GC0310 | video1 + SPI2 逐行中断/DMA | ✅ 实机 640×480 ≈4.4 fps、481.0 行/帧、`tag_err=0`，**目视颜色正常** |

主要提交：
- `b1efdfcc` 新增 AC791 DVP + SPI 摄像头预览（TDD 注册、TDL 管理、板级配置）
- `7fa64dc9` 保持 `example_camera.c` 通用，板级细节下沉到 TDD/TKL（`TDD_DVP_SR_CFG_T.yuv_order`、packed YUYV 交付、raw 线程栈 16 KiB）
- `0a74cd24` GC0310 改由 TDD 经 IIC1 配置（`JIELI_CAMERA_TDD_SENSOR`）
- `e9a592c4` GC0310 初始化表按厂商**实际执行**序列重新生成（24 MHz 分支 304 条，修正此前误收块注释备选写入导致的偏紫/偏粉）
- `0f15617d` 投递队列满时归还帧、`tkl_dvp_init()` 失败路径回收
- `53ad1852` DVP 改走标准 `tkl_dvp` v2 拉模式（ch1 / YUV422 / pull 任务投递 TDL）
- `845048d2` 320×240 预览垂直 2× 铺满 320×480 面板
- `8304d183` 几何保护：RGB565 临时帧只在几何匹配时分配，修掉 SPI profile 下向 307,200 B 面板 FB memcpy 614,400 B 的越界

## 实机证据（AC791 DevKitBoard）

### DVP / J1（GC2145）

- `dvpfill`（`ed1b448e…`）启动链 `display init rt=0` → `camera init rt=0` → `sensor: GC2145` → `320x240 fps=20` → `SET_FMT ok, static overlay buf=…/153600` → `DVP capture started` → `first frame delivered`，**用户目视确认画面铺满 320×480 屏**。
- 计数镜像 `dvpharddiag`（`dvp-v2-harden-diag-3b1b82c9.bin`）：**129.8 s / 3,720 帧 / drops=0 / 零异常**（≈28.7 fps，130 条非致命 `isc bandwith err`）。
- 平台头 `52b1322` 复验：**133.4 s / 3,820 帧 / drops=0 / 零异常**（≈28.6 fps），结束时 USB 仍枚举。

### SPI / J4（GC0310）— 2026-11-18 验收

- 镜像 `spi-base3-556e7662.bin`（`spibase`，430,692 B，SHA256 `556e7662…`）：平台 `52b1322` + 本 PR `8304d183` + 显示 TE 修复（fork #6）。
- 启动链：`gc0310 chip id: 0xa310 (expected 0xa310)` → `gc0310 spi init done` → `SPI: spi2 ok` → `line-IRQ mode, 8 line buffers` → `SPI capture started (640x480, line IRQ, first 1292 B / line 1288 B)` → `TDD sensor restart ret=0` → `first frame: 640x480 fmt=1 len=614400`。
- 运行：**≈4.47 fps**（83 帧 / 15 s）、**481.0 行/帧**、`tag_err=0`、无异常、无缺行累积。
- **用户目视：颜色正常，偏紫/偏粉问题已消除**（`e9a592c4` 重建初始化表是根因修复）。
- 日志：`serial-COM8-20261118-163917.log`、`serial-COM8-20261118-163541.log`。

## 已知限制（重要）

- J1 与 J4 共用 PA2/PA3/PA4 与 PH0–PH3，且两模组同为 I2C 地址 0x42，**双路同时采集硬件不可行**；验收口径为“单路可用 + 硬件互斥”。
- SPI 链路为 2 线 + 字节碎片，VGA 上限约 4–5 fps（厂商参考同为 5 fps）；传感器 `GC0310_set_output_size()` 为空实现、固定 VGA，320×240 由 TKL 主机侧 2:1 抽取交付。
- 板上音频功放空闲时会把摄像头 SPI 总线的耦合噪声放大成“叮叮”声。这是板级 PA 常开（为兼容 chatbot 示例）的既有行为，非摄像头缺陷，本轮不处理。
- KWS/VAD 音频链路不在本 PR 内（见 `audio/tal-engine` 分支）。

## 构建

- `JIELI_AC79_DevKitBoard_spi_lcd`、`JIELI_AC79_DevKitBoard_dvp_lcd`、`JIELI_AC79_DevKitBoard_spi_tdd` 三个 profile 均 BUILD SUCCESS。

## 2026-11-18 共享核心兼容性修复（`ab50873a`）

- `src/peripherals/camera/tdd_camera` 是**跨平台共享**代码：`CMakeLists.txt` 只要 `CONFIG_ENABLE_DVP=y` 就编译，而 `boards/T5AI/TKL_Kconfig` 默认 `ENABLE_DVP=y`。此前 v2 拉模式重写会让仍使用 v1 回调 API（`TUYA_DVP_CFG_T` + assign/post 回调）的 T5AI 编译失败。
- 改为**能力宏守卫**：平台头定义 `TKL_DVP_HAS_PULL_API` 时编译拉取实现，否则编译原有的回调实现（`#else` 分支与 master 逐字节一致）。`sync_polarity` 字段同样守卫（该类型在 v2 头叫 `TUYA_DVP_SYNC_MODE_E`，在 v1 头叫 `TUYA_DVP_SYNC_MODE`）。
- 还原 `clang-format` 噪声：`tdl_camera_manage.c` 由 140/138 降到 **4/1**（唯一语义改动是 `raw_flow_task` 栈 8 KiB → 16 KiB），`tdd_camera_dvp.h` 由 19/12 降到 **12/0**，`tdd_camera_dvp.c` 变为纯新增（339/1）。
- 构建验证：JIELI `spi_tdd`、`dvp_lcd` BUILD SUCCESS；**T5AI `TUYA_T5AI_BOARD` 摄像头板 BUILD SUCCESS**（守卫前该组合在 `TUYA_DVP_SYNC_MODE_E` 处失败）。
- 实机回归（本头构建）：`spi_tdd` 干净构建烧录后连续 **175 s / 776 帧 / 4.43 fps / 480.0 行/帧 / `tag_err=0` / 零异常**，用户目视 SPI 画面正常（`serial-COM8-20261118-175026.log`）。

## 已知后续项

- T5AI TKL DVP 仍是 v1 零拷贝回调模型。要统一成 v2 拉模式需要平台仓独立 PR + T5 硬件 A/B（ch1 会多一次拷贝），完成后再删除共享核心里的 legacy 分支。

## 2026-11-17 更新

- `9faeca2a`：`example_camera.c` 增加原始启动标记（`app_main`/`user_main`/各 init 步骤）并把应用线程栈提到 8 KiB；解决“静默板”无法区分线程未启动与流程挂死。
- 平台侧同步修复 `a6b181bb`（WL82 软件 IIC 的 400 kHz 门限），TDD 归属 GC0310 的 SPI 路径实机通过：≈4.84 fps、481 行/帧、`tag_err=0`，85 s 无复位。

## 2026-11-17 评审修复（应用侧）

- `e9a592c4`：GC0310 初始化表按厂商**实际执行**序列重新生成（块注释中的 gamma/AWB/CC/HB 备选写入此前被误收录，传感器落在与厂商 demo 不同的 AWB/CC 状态，是偏紫/偏粉的最可能根因）；24 MHz 分支 304 条写入，逐条与厂商源码一致。
- `0f15617d`：投递队列满时把帧归还 TDL 池（否则两次溢出永久耗尽帧池）；`tkl_dvp_init()` 之后若 sensor init/set_ppi 失败，先 `tkl_dvp_deinit()` 再返回，避免重试泄漏上一组缓冲。

## 2026-11-18 更新：DVP v2（标准 tkl_dvp 拉模式）与满屏预览

- `53ad1852` TDD 改为标准 v2 契约：`TUYA_DVP_BASE_CFG_T`（ch1 YUV 拉模式）+ 新增 pull 任务把帧投递到 TDL；`supported_fmts` 收敛为 `TDL_CAMERA_FMT_YUV422`；`TDD_DVP_SR_CFG_T` 增加 `sync_polarity`。
- `845048d2` 示例铺满：320×240 帧先转 RGB565 临时帧；面板高度为两倍时每行写两遍，铺满 320×480；其它尺寸回退 1:1。
- 配套：平台 PR #47（静态 BSS overlay 缓冲 + 采集/转换分任务 + volatile 转换 + SPI DMA 帧缓存失效）、显示 TE 修复 PR maidang-xing/TuyaOpen#6。
- `8304d183` 几何保护：RGB565 临时帧只在“面板宽=相机宽且高≥2×相机高”时分配和使用，其它几何直接转进面板 FB；DVP 几何行为不变。

## 2026-11-18 评审清理：SPI 只留一条路径（`ba696866`）

评审的两个问题是"都走 TDD/TKL 了为什么还改厂商 `gc0310.c`"和"为什么堆了这么多 `JIELI_*` 宏"。根因是 SPI 路径一度并存两套实现——厂商 `video1.0` 驱动与 TDD 传感器驱动——靠 Kconfig 的 `JIELI_CAMERA_TDD_SENSOR` 二选一。TDD 路径验收通过后另一条已无 profile 使用，本次把开关和它带出来的东西一起删掉：

- 删 `JIELI_CAMERA_TDD_SENSOR`：SPI 路径固定由 TDD 配置传感器（IIC1 在 PH0/PH1）。`board_com_api.c` 三处守卫与 `tkl_camera_sensor_restart()` 钩子改用 `JIELI_CAMERA_PATH_SPI`。
- 删 `JIELI_CAMERA_SPI_EDGE_{SMPH,SMPL,H_SMPH}` 与 `JIELI_CAMERA_I2C_SCAN`：全仓无消费者（平台 staging 固定写板级 demo 的 SMPH），都是 bring-up 期实验开关。
- 删 `JIELI_AC79_DevKitBoard_spi_lcd(.qvga)` 两个 profile：走厂商 video1 路径，随开关一起退役；`spi_tdd` 保留。
- 保留 `CONFIG_EXAMPLE_CAMERA_WIDTH/HEIGHT/FPS`：`example_camera.c` 用它们填 `cfg.width/height/fps`，不是死配置（删掉后示例按 Kconfig 默认 480×480@20 打开并挂死——已实测）。

配套平台侧清理在平台 PR #47（`tkl_dvp.c` 删厂商 video1 线程与握手、厂商 `gc0310.c` 还原原样）。**两侧必须同落**：`tkl_dvp.c` 的 SPI 分支读同一个 Kconfig 宏，只改应用侧会让 TKL 退回厂商 video1 路径、板级却注册 TDD 传感器，开机卡死在相机初始化（已实测）。

验证（清理后的树）：

- `spi_tdd` 干净构建 → 烧录 → **4.43 fps / 480.0 行/帧 / `tag_err=0`，175 s 连续 776 帧、零异常**（`serial-COM8-20261118-190828.log`）。
- `dvp_lcd` profile BUILD SUCCESS；**T5AI 摄像头板 BUILD SUCCESS**（legacy 回调路径不受影响）。

## 2026-11-18 IIC 总线（配合平台 PR #47 `fc558435`）

板级 `TDD_DVP_SR_USR_CFG_T.i2c = {port = TUYA_I2C_NUM_1, clk = 112, sda = 113}` 会由共享 TDD 通过 `tkl_io_pinmux_config()` 落到 PH0/PH1，`iic1` 的引脚也改由板级 Kconfig 提供（此前在平台 `tkl_i2c.c` 里硬编码）。平台侧做了硬件 IIC1 与软件 IIC 的 A/B：硬件节点能让 ID 探测和寄存器表通过，但 SPI 一帧都收不到（`tag_err` ≈2700/s），因为 PH0/PH1 与传感器 2-wire SPI 复用；软件 IIC 维持 4.44 fps / `tag_err=0`。详见平台 PR #47。
