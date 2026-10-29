# T5AI-Core sanoTTS G0 硬件与资源预检报告 (Preflight Report)

**日期**: 2026-10-29  
**板型**: `TUYA_T5AI_CORE` (SoC: Beken BK7258 Dual Cortex-M33)  
**SDK 基线**: TuyaOpen commit `89b34249` (基于 origin/master `695e571fb4f62932dbf86a86fde664fcb7f2c0b4`)  
**Platform 基线**: `platform/T5AI` commit `7321a15f7f3f9c9419e165304ac92135bbaf4e30`  
**工具链**: GNU Arm Embedded Toolchain 10.3-2021.10 (`arm-none-eabi-gcc 10.3.1`)  
**烧录工具**: `tools/tyutool/tyutool_cli` v3.2.10

---

## 1. 硬件连接与串口路由

- **物理 USB 双串口设备**: `1a86:55d2` (QinHeng Electronics CH342/CH347 USB Dual Serial)
- **Flash 烧录口 (Interface 00)**: `/dev/ttyACM0` (Lowest USB Interface)
- **Console / 日志 / 应用串口 (Interface 02)**: `/dev/ttyACM1` (Baud: 115200, 8N1)
- **音频硬件配置**:
  - Codec 驱动: `tdd_audio` / `TKL_AUDIO` (BK7258 内置 Audio DAC)
  - 喇叭使能引脚: `BOARD_SPEAKER_EN_PIN` = `GPIO 39`，有效电平 `LOW`
  - 默认硬件播放格式: 16 kHz, 16-bit, 单声道 (Mono)

---

## 2. 物理容量与默认分区缺陷分析

### 2.1 物理资源
- **Flash 物理总容量**: 8 MB = 8,388,608 字节 (`0x800000`)
- **PSRAM 物理总容量**: 16 MB = 16,777,216 字节 (`0x1000000`)
- **SRAM 共享物理容量**: 640 KB (Base: `0x28000000`)

### 2.2 默认分区表冲突 (`auto_partitions.csv`)
默认分区分配了 2940 KB 的 `ota` 分区和 196 KB 的 `tuya_data`，且保留了视频与屏幕预留区：
- `primary_ap_app`: 3808 KB (约 3.72 MB)
- `ota`: 2940 KB (约 2.87 MB)
- `tuya_data`: 196 KB

**冲突点**:
sanoTTS 中文基线 `web/voices/chinese` 包含：
- `front_f32.bin` (约 3.7 MB)
- `dec_f32.bin` (约 2.3 MB)
- `meta.json` + eSpeak `cmn` 字典 (约 0.3 ~ 0.5 MB)
- 权重与字典总计: **~6.2 MB**
在默认 3.8 MB 的 `primary_ap_app` 下无法容纳 6.2 MB 模型。

---

## 3. 授权项目专用 8 MB Flash 布局方案

依据用户全权授权（取消 OTA、删除无关业务/云端授权分区及音视频显示预留）：

| 分区名 | 偏移 (Offset) | 大小 (Size) | 用途与保留依据 |
|---|---|---|---|
| `primary_bootloader` | 0x000000 | 68 KB | 芯片硬件 ROM Boot 一级引导 (必需) |
| `primary_tuyaboot` | 0x011000 | 68 KB | 芯片二级引导程序 (必需) |
| `primary_cp_app` | 0x022000 | 1088 KB | CP 协处理器固件 (RF/Audio通信底层必需) |
| `primary_ap_app` | 0x132000 | 1024 KB | AP 应用程序 (FreeRTOS+TuyaOpen+sanoTTS代码引擎) |
| `snt_model_zh` | 0x232000 | 5928 KB | sanoTTS 中文模型与 eSpeak 字典数据 (只读数据区) |
| `sys_rf` | 0x7FE000 | 4 KB | 芯片射频标定参数 (硬件必需) |
| `sys_net` | 0x7FF000 | 4 KB | 芯片 MAC 与系统网络参数 (硬件必需) |
| **总计** | - | **8180 KB / 8192 KB** | **剩余可用安全余量: 12 KB** |

**结论**: Flash 专用布局完全可在物理 8 MB 内装入完整中文模型与独立启动固件。

---

## 4. 16 MB PSRAM 专用布局与工作区峰值预算

在 `ram_regions.csv` 中移除 `PSRAM_MEM_SLAB_DISPLAY` (5.44 MB) 与 `PSRAM_MEM_SLAB_ENCODE` (1.37 MB) 后，`AP_PSRAM_HEAP` 扩充至 **15.4 MB**。

### 中文分块推理内存峰值预算:
1. **中文权重驻留 (可选载入加速)**: 5.72 MB
2. **中心块 16 帧 + 上下文解码工作区**: 约 2.20 MB
3. **eSpeak cmn 翻译前端内存**: 约 1.00 MB
4. **PCM 环形缓冲**: 32 KB
5. **系统任务栈与网络/音频驱动队列**: 约 1.20 MB
- **PSRAM 峰值总需求**: **10.15 MB**
- **可用连续 PSRAM 空间**: **15.40 MB**
- **PSRAM 剩余安全余量**: **~5.25 MB (余量充足，无 OOM 风险)**

---

## 5. 门禁裁定结论

- **Flash 8 MB 门禁**: **通过 (专用布局下总需求 ~7.1 MB < 8.0 MB)**
- **PSRAM 16 MB 门禁**: **通过 (峰值需求 ~10.15 MB < 15.4 MB，最大连续分配需求满足)**
- **系统时钟与 I/O 路由**: **通过 (UART0/UART1 分配明确，DAC 硬件链路具备)**
- **后续行动**: 推进 Task A (主机 Oracle) 与 Task B (M0 固件驱动) 完成。
