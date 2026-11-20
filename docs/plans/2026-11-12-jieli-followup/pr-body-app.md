## 内容

AC791（WL82）开发板两路摄像头的驱动与分层改造，全部走 TuyaOpen 的 TDD/TDL 契约：

| 连接器 | 传感器 | 路径 | 状态 |
|---|---|---|---|
| J1（24-pin DVP） | GC2145 | video0 + DVP v2 拉模式 | ✅ 早期 320×240@20 实机 3,720 帧 / drops=0 目视满屏；**2026-11-19 传感器改由 TDD 拥有后重新实机：35.3 s / 571 帧 / drops=0 / ≈17.6 fps，零 `video_ioct` / `isc line err`，画面与颜色目视正常** |
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

- `JIELI_AC79_DevKitBoard_dvp_lcd`（GC2145）与 `JIELI_AC79_DevKitBoard_spi_lcd`（GC0310）两个 profile 均 BUILD SUCCESS。

## 2026-11-18 共享核心兼容性修复（`ab50873a`）

- `src/peripherals/camera/tdd_camera` 是**跨平台共享**代码：`CMakeLists.txt` 只要 `CONFIG_ENABLE_DVP=y` 就编译，而 `boards/T5AI/TKL_Kconfig` 默认 `ENABLE_DVP=y`。此前 v2 拉模式重写会让仍使用 v1 回调 API（`TUYA_DVP_CFG_T` + assign/post 回调）的 T5AI 编译失败。
- 改为**能力宏守卫**：平台头定义 `TKL_DVP_HAS_PULL_API` 时编译拉取实现，否则编译原有的回调实现（`#else` 分支与 master 逐字节一致）。`sync_polarity` 字段同样守卫（该类型在 v2 头叫 `TUYA_DVP_SYNC_MODE_E`，在 v1 头叫 `TUYA_DVP_SYNC_MODE`）。
- 还原 `clang-format` 噪声（`ab50873a` 只做到一部分，见下一节）：`tdl_camera_manage.c` 由 140/138 降到 **4/1**（唯一语义改动是 `raw_flow_task` 栈 8 KiB → 16 KiB），`tdd_camera_dvp.h` 由 19/12 降到 **12/0**，`tdd_camera_dvp.c` 变为纯新增（339/1）。
- 构建验证：JIELI `spi_tdd`、`dvp_lcd` BUILD SUCCESS；**T5AI `TUYA_T5AI_BOARD` 摄像头板 BUILD SUCCESS**（守卫前该组合在 `TUYA_DVP_SYNC_MODE_E` 处失败）。
- 实机回归（本头构建）：`spi_tdd` 干净构建烧录后连续 **175 s / 776 帧 / 4.43 fps / 480.0 行/帧 / `tag_err=0` / 零异常**，用户目视 SPI 画面正常（`serial-COM8-20261118-175026.log`）。

## 2026-11-20 DVP 统一：杰理改走 T5 的推模型（平台 `e475bdd4` / 应用 `1425132f`）

两个平台的 TDD 现在是同一份共享核心，杰理专有代码只剩两颗传感器：

| 文件 | 内容 |
|---|---|
| 平台 `tkl_dvp.h` | 推接口：`tkl_dvp_init(TUYA_DVP_CFG_T *)` + deinit/start/stop，与 T5AI 同形 |
| 平台 `tkl_dvp.c` | 删掉 `tkl_dvp_get_frame`、`TUYA_MEDIA_BUF_T` 环形池、信号量、`request_idr`；转换线程改为 `assign_cb` 取缓冲 → 1:1 中心裁剪并打包成 **UYVY** → `post_cb` 上交 |
| `tdd_camera_dvp.c` | **与 master 逐字节一致**（共享核心，两个平台同一份） |
| `tdd_camera_gc0310_spi.c` | 杰理独有的 SPI 模组驱动，注册走 `tdl_camera_dvp_device_register()` |
| `tdd_camera_dvp_gc2145.c` | **上游驱动直接用于杰理**（见下一小节） |
| `tdd_camera_dvp.h` / `tdd_camera_gc2145.h` / 其余传感器 | **与 master 逐字节一致** |

- `ENABLE_DVP_V1` 开关、`tdd_camera_dvp_v1.h`、每模组一份传输全部删除；CMake 只按 `CONFIG_BOARD_CHOICE_JIELI` 选传感器集合。
- 共享核心的 `dev_info` 补零初始化：原先 `yuv_order` 是未定义值，TDL 会拿到栈上的垃圾。
- 收益：应用仓净减 **667 行**，两平台同一套 TDD；代价：v2 的生命周期 `event_cb` 随接口一起移除，`get_frame` 的超时/背压语义改为 TDD 缓冲不足时丢帧计数。
- 配置侧同步（`396b819d`）：板级摄像头选项原本 `select ENABLE_DVP_V1` 以免手写配置漏项，该开关随本提交删除；`JIELI_AC79_DevKitBoard_spi_tdd` 更名为 `spi_lcd`，与 `dvp_lcd` 成对。

### 杰理不再需要专有 GC2145 驱动（应用 `7a8f846d` / 平台 `331f2c76`）

- 共享核心的 `__tdd_dvp_setup_callback()` 从空实现改成「重放 reset + init + set_ppi」，几何用 open 时记下的 ppi/fps；平台在 `video0.2` 打开后、SPI2 武装后调用 `setup_cb`，取代原先按传感器实现的 restart 钩子。
- 删除 `tdd_camera_dvp_gc2145_v1.c` / `.h`、`tdd_camera_gc0310_spi_restart()`、板级 `tkl_camera_sensor_restart()`。QVGA→VGA 回退一并删除：INIT 表本身就是 VGA，320×240 由 TKL 1:1 裁剪得到。
- 杰理编译集变为「共享核心 + 上游 `gc2145` + `gc0310_spi` + `i2c`」，DVP 层不再有任何平台专有代码。

构建验证（逐个核对实际编译的文件）：JIELI `dvp_lcd`、`spi_lcd` → 共享核心 + `gc2145` + `gc0310_spi` + `tdd_camera_dvp_i2c.c`；T5AI → 共享核心 + `gc0308` / `gc2145` / `ov2640` + `i2c`；三者 0 error。

实机（镜像 `92189ea1`，`dvpgeneric`，上游驱动 + setup 重放）：复位/写表/`set_ppi` 各出现两次（open 一次、`setup_cb` 重放一次）→ `DVP: TDD sensor restart ret=0` → `SET_FMT ok … 614400` → `first frame: 320x480 fmt=1 len=307200` → `first frame delivered`；218 行日志零异常，**用户目视画面正常**（`logs/serial-COM8-20261120-112409.log`）。

此前一轮实机（镜像 `fa59e342`，`dvppush`）：`gc2145 id init/restart: rt=0 2145` → `DVP: TDD sensor restart ret=0` → `SET_FMT ok … 614400` → `DVP capture started` → `first frame: 320x480 fmt=1 len=307200` → `first frame delivered`；213 行日志零异常、无 `fb_manage null` / `axi_rd_inv` / 断言；**用户目视画面与颜色正常**（`logs/serial-COM8-20261120-104942.log`）。

## 2026-11-19 预览几何：320×480 直出、1:1 居中裁剪（`77e3aab3` / `a3c89d5a`）

- 面板 320×480、传感器 640×480：铺满必然要缩放或丢弃。此前是"320×240 转换 + 每行写两遍"，画面被纵向拉伸、且丢掉一半行。
- 改为按面板尺寸请求 **320×480**，平台侧给 **1:1 居中裁剪**（`tkl_dvp` 取中间 320 列、全部 480 行，不缩放）。示例回到上游转换路径，`sg_conv_buf` / 逐行复制 / `tal_malloc` 全部删除，`example_camera.c` 现在相对 master 只剩 framebuffer 数一个 Kconfig 开关。
- 代价：横向视野约为原来的中间一半；收益：不变形、垂直 480 行全用、示例无几何特例。内存 +约 768 KB（TKL 输出缓冲/ch1 池、TDL raw 池按 320×480 重算，减掉不再需要的临时帧）。
- 实机（`dvpcrop` `2ccac400`）：用户目视**画面正常、无拉伸**。
- 示例零改动（`bd7b38b7`）：两个 framebuffer 够用，原先为 SPI 慢路径加的 `EXAMPLE_DISPLAY_FRAME_BUFF_NUM` Kconfig 与第 3 个缓冲已删除，`example_camera.c` 与 `output_display/Kconfig` 相对 master **零差异**；本 PR 的示例侧改动只剩两个新增的 board profile。
- 同提交带上 `a3c89d5a`（= PR maidang-xing/TuyaOpen#6 的 `83c7407a`）：`tdd_display_mcu8080.c` 的 `TUYA_GPIO_BASE_CFG_T gpio_cfg = {0}`。没有它 AC79 的 TE 引脚初始化返回 `-2`，display open 失败、每帧被丢（`fb_manage null`）。

## 2026-11-19 同步极性显式化 + 注释精简（`17b39cdb`）

- 传感器描述符显式写 `.sync_polarity = TUYA_DVP_SYNC_MODE_0`（GC2145 与 GC0310 SPI 各一处，v2 守卫内），不再依赖零初始化：AC791 的 DVP 两路 sync 都是高有效。
- 配套平台 `3a166adb` 会把不支持的极性判为 `OPRT_NOT_SUPPORTED`，两边取值必须一致；H-V 互换仍由板级 `JIELI_BOARD_CAMERA_HV_SYNC_SWAP` 表达，与极性无关。
- 本轮新增注释统一收成一行说明。
- `raw_flow_task` 的 8 KiB→16 KiB 已回退（`65796869`）：该改动没有对应的溢出记录（板上两次真溢出是厂商 demo 的 `spi_auto` 2 KB 与 `audio_encoder` 4 KB），且 T5AI 一直以 8 KiB 跑同一个 `tdl_camera_manage.c` 与同一个回调；`tdl_camera_manage.c` 现已与 master 零差异。
- 构建：T5AI、JIELI `dvp_lcd`、JIELI `spi_tdd` 均 BUILD SUCCESS。

## 2026-11-19 格式噪声清理（评审项）

上一节的"还原"实际上没落地：`ab50873a` 对 `tdl_camera_manage.c` 只有 +3/−4，树里仍留着约 139/138 的 clang-format 重排。本提交把受影响的文件从 master 原文重建、只重放语义改动：

| 文件 | 清理前 | 清理后 |
|---|---|---|
| `tdl_camera_manage.c` | +139/−138 | **0**（栈改动已回退，文件与 master 一致） |
| `tdd_camera_dvp.c` | +390/−53 | **+339/−1**（v2 拉取实现；`#else` 分支与 master 逐字节一致） |
| `tdd_camera_dvp_gc2145.c` | +116/−48 | **+73/−0** |
| `tdd_camera_dvp.h` | +19/−12 | **0**（字段已迁到 `tdd_camera_dvp_v1.h`） |
| `tdd_camera_gc2145.h` | +6/−1 | **0**（restart 声明已迁到 `tdd_camera_gc2145_v1.h`） |
| `example_camera.c` | +64/−21 | **+54/−4** |
| `board_com_api.c` | 含 1 行对齐改动 | 已还原 |

行为零变化：只去掉了空格/花括号/成员对齐，`git diff -w` 与普通 diff 的行数在这些文件上一致（`tdd_camera_dvp.c` 的 1 行删除是文件末尾无换行符）。T5AI、JIELI DVP、JIELI SPI 三个 profile 重新编译验证通过。

## 已知后续项

- 两平台已统一到推模型；杰理 SPI 模组（J4）本轮只做了构建验证，实机需断电换模组后复验。

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

## 2026-11-19 评审收敛：示例回到上游 + IIC 选择下沉到板级（`517857f0`）

评审意见两条：`example_camera.c` 不该有这么多改动（应完全兼容）；软件/硬件 IIC 与摄像头选择应在 board 文件里用一个宏决定。

- 示例回到 master + **两处功能性改动**（净 48/4，此前 93/24）：
  1. `EXAMPLE_DISPLAY_FRAME_BUFF_NUM`：SPI 路径 4.4 fps 下需要 3 个缓冲；
  2. 320×480 面板的 2× 垂直填充——`tdl_disp_convert_yuv422_to_fb` 不缩放（`tal_image_yuv422_to_rgb.c:146` 取 `min(in,out)`），不填就只有上半屏。
  其余 clang-format 噪声、`[CAM]` 启动标记、4 KiB→8 KiB 栈全部回退；**4 KiB 栈实机验证通过**（90 s、4.4 fps、无异常）。
- **IIC 归属**：板级 Kconfig 新增 `JIELI_BOARD_IIC1_SOFTWARE`（SPI 路径默认 y），平台只问"这条总线是不是软件 IIC"，`tkl_i2c.c` 里不再出现 `JIELI_CAMERA_*`。
- 引脚只命名一处：`board_com_api.c` 的 `.i2c.clk/sda` 改用 `JIELI_BOARD_IIC1_SCL/SDA_PIN`；iic0 在两路摄像头下都落在相机焊盘，由板级 Kconfig 表达。
- Kconfig 去掉过时的 `(video1)` 措辞。

实机（`b351a22b…`，spi_tdd）：`gc0310 chip id 0xa310` → `SPI capture started` → `first frame 640x480`，**4.4 fps / 479.8 行/帧 / `tag_err=0`，启动日志 0 条 E 级错误**；`dvp_lcd` BUILD SUCCESS。

> ⚠️ 依赖：AC791 上单独构建本分支时，8080 面板需要 **PR #6（`83c7407a`）** 的 TE 引脚修复，否则 `display init rt=-2`、帧全部丢弃（日志刷 `fb_manage null`）。

## 2026-11-19 结构对齐 T5：按"模组"而不是"路径"（`8eecd3a0`）

板级 Kconfig 原来选的是"摄像头路径"（DVP/SPI）——这描述的是厂商管线，不是板子。现在选**装的是哪个模组**，形状与 T5AI 的 `TUYA_T5AI_BOARD_CAMERA` 一致（两路共用引脚，所以仍是 choice）：

```
choice "Camera module"
  JIELI_BOARD_CAMERA_NONE
  JIELI_BOARD_CAMERA_GC2145   "GC2145 on J1 (24-pin DVP)"
  JIELI_BOARD_CAMERA_GC0310   "GC0310 on J4 (12-pin SPI)"
```

- `board_com_api.h` 新增 `BOARD_CAMERA_I2C_PORT/SCL/SDA`、`BOARD_CAMERA_RST_PIN`、`BOARD_CAMERA_POWER_PIN`、`BOARD_CAMERA_CLK`，每个模组一套，和 T5AI 板级头同形；
- `board_com_api.c` 用这些宏注册对应 TDD 驱动（DVP 模组仍由厂商 video0 管传感器，本轮只做结构对齐，不改行为）；
- 示例 profile 改成选模组。

平台侧（PR #47 `720bb6b0`）改为读板级 staging 的 `JIELI_BOARD_CAMERA_DVP/_SPI` 与 `JIELI_BOARD_CAMERA_WIDTH/HEIGHT/FPS`，适配层里不再出现任何"路径"字样。

验证：`spi_tdd`、`dvp_lcd` 均 BUILD SUCCESS；`spi_tdd` 实机启动链完整、**4.4 fps / 479.4 行/帧 / `tag_err=0`，60 s**。

## 2026-11-19 步骤 1/2：DVP 传感器也交给 TDD（`13af27fe`、`f183c80d`）

对齐 T5 之后，"谁拥有传感器"只剩一个判据：`TDD_DVP_SR_INTFS_T` 为空表示厂商拥有，非空表示 TDD 拥有。SPI 路径早已是 TDD 拥有，DVP 路径此前仍由厂商 `video0` 的 `gc2145.c` 探针并写寄存器表（TDD 只注册空接口表）。本次把 DVP 也翻过来，两路模组的行为彻底同形：

- **板级**：`board_com_api.c` 的 DVP 分支改调 `tdd_camera_dvp_gc2145_register()`，用 `BOARD_CAMERA_I2C_PORT/SCL/SDA`、`BOARD_CAMERA_RST_PIN`、`BOARD_CAMERA_CLK`——和 SPI 分支同一套宏形状，也是 T5AI 板级头的形状。
- **TDD**：GC2145 没有原生 QVGA 表（厂商 `GC2145_set_output_size()` 只是在 1616×1232 画布上开窗写 0x09–0x10，不是缩放），所以 `__dvp_gc2145_set_ppi()` 在查不到 QVGA 表时回退到 VGA 表。这与 SPI 路径是同一分工：**传感器跑板级几何，TKL 抽取到调用方要的尺寸**。
- **profile**：DVP 的板级几何就是传感器几何（640×480@20），示例请求 320×240@20，由 TKL 2:1 抽取交付（`f183c80d` 把示例 fps 从 25 对齐到板级 20，避免示例向 TDD 要一个与平台描述符不同的速率）。
- **平台侧配套**见 PR #47 `24003fe3`：`tkl_dvp.c` 只保留一个 ops 全空的时序描述符，厂商 ISP 仍能锁存并口时序与几何但不会重写 TDD 刚写的表；DVP 构建不再编译厂商 `gc2145_dvp/gc2145.c`；staging 的 `reset_gpio` 置 -1。

验证状态：

- `dvp_lcd`、`spi_tdd` 均 BUILD SUCCESS（T5AI 摄像头板回归同前，legacy 回调路径未动）。
- **无模组启动**（`logs/boot-dvp-tdd-noj1.log`）：`camera module: GC2145 on J1 (DVP)` → TDD iic0（clk 112 / sda 113）→ `gc2145 reset pin 115 lv:0` → `dvp initialized` → `sensor info: 640x480 fps=20` → `SET_FMT ok, static overlay buf=…/614400` → `DVP capture started` → `camera init success`。`sensor info` 出自我们的 `GC2145_TIMING` 描述符，说明厂商探针已被正确绕开。
- **帧率/画面/颜色未复验**：J1 模组当前未接（同一日志里只有 1 Hz 的 `[VIDEO]video_ioct: err = 11`，EAGAIN）。接上 J1 后需要补一轮 320×240 交付计数 + 目视，本 PR 才会回到"两路均实机验收"。

## 2026-11-19 步骤 1 实机：三处根因（`fb0b72b5`、`85174cf3`）

DVP 传感器交给 TDD 后第一次上板：先是 `video_ioct: err = 11` 一帧不出，修好后画面偏紫绿。逐个定位为三件事（平台侧两处配套见 PR #47 `e2413586`、`15f00ae5`）：

1. **传感器时钟**（`fb0b72b5`）：TDD 在 `tkl_dvp_start()` 之前复位并写表，而板上的 24 MHz XCLK（PH2）是 ISC 输出通道，要等厂商 `dev_open("video0.2")` 才使能——表全写在一颗没有时钟的传感器上。新增的 chip-id 读回把这件事直接摊开：`gc2145 id init: rt=0 ffff` →（video0 open）→ `gc2145 id init: rt=0 2145`。修法沿用 SPI 路径既有的钩子：板级 `tkl_camera_sensor_restart()` 在 GC2145 模组下转发到 `tdd_camera_dvp_gc2145_restart()`（复位 + 整表 + set_ppi 重放），平台在 `dev_open` 成功后调用。读回日志保留在 init 与 restart 两处，下次有人动时钟/管脚时一眼能看出来。
2. **H/V 同步**（平台 `15f00ae5`）：描述符漏了厂商在 `CONFIG_CAMERA_H_V_EXCHANGE` 下写的 `.sync_config`（WL82/AC791 是 SYNC0=VSYNC、SYNC1=HSYNC），表现为持续 `[VIDEO_ISC]isc line err`。
3. **YUV 序**（`85174cf3`）：`TDD_DVP_SR_CFG_T.yuv_order` 默认 UYVY 被 TDL 一路传到显示转换器，而 `tkl_dvp` 打包的是 YUYV——转换器按错误字节位置取亮/色度，画面偏紫绿。补 `.yuv_order = TUYA_YUV422_YUYV`，与 SPI 模组一致。

实机（AC791 DevKitBoard + J1 GC2145，镜像 `dvp-tdd-yuyv-cac7f91f.bin`）：启动链 `camera module: GC2145 on J1 (DVP)` → `gc2145 id init ffff` → `DVP: TDD sensor restart ret=0` → `gc2145 id ... 2145` → `SET_FMT ok … 614400` → `first frame: 320x240 fmt=1 len=153600`；**35.3 s / 571 帧 / drops=0 / ≈17.6 fps**，零 `video_ioct err`、零 `isc line err`（只剩既有的 `isc bandwith err` ≈2/s）；**用户目视确认画面与颜色正常**（`logs/serial-COM8-20261119-135349.log`）。去掉计数后的最终镜像 `dvp-tdd-final-96603c26.bin` 同样零错误，板子当前运行该镜像。

> 交付率说明：DVP profile 是"传感器 640×480、预览 320×240"，抽取在 `cam_proc` 里逐像素做，比早前 320×240 直采的 ≈28 fps 低（≈17.6 fps）。要提速可让 ISP 侧出小尺寸，或把转换改成对齐宽读。

## 2026-11-19 评审收敛：板级只声明引脚（`aada30d6`、`f5e3e981`）

- 删除 `JIELI_BOARD_IIC1_SOFTWARE`：IIC1 是软件还是硬件由平台按芯片与引脚决定（PH0/PH1 → 软件，因为摄像头模组在这两个焊盘上跑 2-wire 总线），板级只保留引脚声明，应用/板级都不再出现"软件/硬件"字样。
- `tdd_camera_dvp.h` 的注释改为"v2 极性类型是共享枚举的别名"，与平台 `9fdfb9f8` 一致。

- `b9607d33`：平台 v2 头统一成 stdint/void 后，共享 TDD 的事件回调也随之写 `void`（不再依赖 `VOID_T`）。
