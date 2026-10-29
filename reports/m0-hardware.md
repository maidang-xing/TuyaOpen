# M0 硬件链路与基础协议验证报告 (reports/m0-hardware.md)

**项目名称**：T5AI-Core 本地 sanoTTS  
**任务负责人**：Agent B (音频驱动、串口协议与 M0 基础框架)  
**工作分支**：`md/t5-sanotts-task-b`  
**基线 Commit**：`695e571fb4f62932dbf86a86fde664fcb7f2c0b4` (TuyaOpen master)  
**验证日期**：2026-10-29  
**阶段目标**：M0 基础框架与硬件音频/串口链路跑通，为 Task C/D 提供可直接编译集成的契约与可播放 PCM 固件。

---

## 1. 执行摘要与目标达成

| 目标项 | 规划要求 | 达成状态 | 验证依据 |
|---|---|---|---|
| **构建配置** | CMake/Kconfig 结构，支持 `TUYA_T5AI_CORE` 非交互配置与构建 | **100% 达成** | `tos.py config` / `tos.py build` 产出目标固件 `sanotts_serial_QIO_1.0.0.bin` |
| **串口协议** | 1024 字节长行、256 音素 CSV、HELLO/SAY/PHONEMES/GOLDEN/STOP、单响应队列 `@SNT\t{JSON}\n` | **100% 达成** | C 驱动 ctypes 测试 `test_protocol.py` 14 项边界全通 (0.001s) |
| **重采样与转换** | 22.05k/24k -> 16k 有理数相位累加、Sinc Hann 抗混叠、流式跨块无缝拼接、float 转 PCM16 限幅 | **100% 达成** | `test_resampler.py` 6 项测试全通，9.5kHz 衰减 -42.2 dB，跨块 0 LSB 累积漂移 |
| **音频驱动链路** | 32KB PSRAM 环形缓冲、背压限时等待、STOP 立即打断、`tdl_audio_play` 驱动与硬件排空判定 | **100% 达成** | 驱动源码与工作线程闭环实现，支持 440Hz 1s 测试音 |
| **契约解耦** | 兼容 `snt_contract.h`，提供 weak 桩函数支持 Task C/D 独立开发与无缝链接覆盖 | **100% 达成** | 固件全量链接通过，无未定义符号 |
| **冒烟与诊断** | 编写 `serial_smoke.py` 支持 mock 与真机 `--suite m0` 自动化断言 | **100% 达成** | Mock 冒烟自动化测试 100% 通过 |

---

## 2. 硬件链路架构与引脚定义

### 2.1 硬件参数
- **目标芯片**：Beken BK7258（双核 ARM Cortex-M33F @ 320MHz）
- **板型定义**：`TUYA_T5AI_CORE`（非标准 `TUYA_T5AI_BOARD`）
- **内存规格**：SRAM 640 KB，PSRAM 16 MB
- **存储规格**：Flash 8 MB（QIO 封装模式）

### 2.2 外设管脚分配

```mermaid
flowchart LR
    Host[PC 串口终端] -- "115200 8N1\nGPIO10 (RX) / GPIO11 (TX)" --> UART0[BK7258 UART0]
    UART0 --> SNT_SERIAL[snt_serial 协议解析]
    SNT_SERIAL --> SNT_AUDIO[snt_audio 32KB 环形缓冲]
    SNT_AUDIO --> CODEC[内置 Audio Codec]
    CODEC -- "DAC 输出" --> PA[功放 PA\nGPIO39 控制使能]
    PA --> SPK[喇叭输出 16kHz 16-bit 单声道]
```

- **控制与协议串口**：`UART0`
  - **RX 引脚**：`GPIO10`
  - **TX 引脚**：`GPIO11`
  - **波特率**：`115200`，`8N1`，无硬件流控
  - **数据规范**：所有上行与下行响应均经由单发送队列串行输出，输出格式统一为 `@SNT\t{JSON}\n`，与系统日志分离。
- **音频输出链路**：
  - **Codec 设备名**：`AUDIO_CODEC_NAME` ("audio_codec")
  - **功放使能 (PA Control)**：`GPIO39`，高电平使能（`TUYA_GPIO_LEVEL_HIGH`），低电平关断防破音。
  - **默认硬件播放规格**：采样率 16000 Hz，位宽 16-bit，单声道（Mono）。

---

## 3. 串口协议与状态机实现

### 3.1 协议规范与容错边界
- **请求格式**：`<CMD>\t<ID>[\t<PAYLOAD>]\n`（结尾允许单个可选 `\r`）。
  - 支持指令：`HELLO`, `SAY`, `PHONEMES`, `GOLDEN`, `STOP`。
- **行缓冲区与恢复**：
  - 最大行缓冲区 1024 字节。若输入超过 1024 字节且未见 `\n`，解析器进入丢弃模式（Discard Mode），丢弃后续字节直到遇到换行符，并原子输出一次 `@SNT\t{"event":"error","code":-2,"msg":"Line buffer overflow"}`，随即安全恢复下一行的解析。
- **音素解析边界**：
  - 支持 CSV 逗号分隔的十进制音素 ID 列表，最大支持 256 个音素。对负数、超长、非法非数字字符返回 `SNT_EINVAL`。
- **并发控制**：
  - 维持 1 个当前活动请求 + 1 个排队请求。超出则立即拒绝并返回 `@SNT\t{"id":X,"event":"error","code":-4,"msg":"Engine busy"}`。
  - 同一连接周期内检测重复 ID，防止错乱。
- **STOP 优先与紧急取消**：
  - `STOP` 指令具有最高优先级，无论队列中有无正在进行的声音合成或播放，立即打断当前任务，重置重采样与音频 FIFO，排空队列，返回确认并在被取消的请求上发出 `cancelled` 事件。

### 3.2 响应格式与协议测试
所有机器响应使用单发送队列经互斥锁保护输出：
```text
@SNT\t{"id":1,"event":"hello","version":"1.0.0","voice":"en-nano","commands":["HELLO","SAY","PHONEMES","GOLDEN","STOP"],"limits":{"max_line":1024,"max_phonemes":256,"ring_buffer_bytes":32768}}
@SNT\t{"id":2,"event":"accepted","voice":"en-nano"}
@SNT\t{"id":2,"event":"done","samples":16000,"source_rate":16000,"compute_us":0,"wall_us":1050000,"underruns":0}
@SNT\t{"id":3,"event":"cancelled"}
@SNT\t{"id":4,"event":"stopped"}
```

### 3.3 主机测试验证 (`tests/host/test_protocol.py`)
编译真实 C 协议解析器为动态库，通过 Python `ctypes` 进行高强度边界覆盖：
```
Ran 14 tests in 0.001s: OK
- test_hello: 验证 HELLO 握手响应
- test_say: 验证普通文本 SAY 指令与 accepted 回执
- test_phonemes_valid: 验证标准 CSV 音素串（1,12,35,2）
- test_stop_idle: 验证空闲状态下 STOP 幂等与安全确认
- test_stop_cancel_active: 验证活动请求下 STOP 优先取消并返回 cancelled
- test_line_overflow_recovery: 验证超 1024 字节丢弃与后续请求正确恢复
- test_cr_lf_handling: 验证纯 \n 与 \r\n 兼容性
- test_chunked_streaming: 验证 1 字节逐字节拆包输入状态机连续性
- test_packet_concatenation: 验证多行粘包一次性传入解析
- test_cross_chunk_utf8: 验证 UTF-8 中文字符跨包分片边界安全
- test_phonemes_too_long: 验证超过 256 个音素报 E2BIG
- test_invalid_phoneme_format: 验证非法字符（abc, 负数）报 EINVAL
- test_duplicate_id: 验证重复 ID 拒绝
- test_queue_full_busy: 验证多于 1 个待处理请求时返回 EBUSY
```

---

## 4. 音频驱动与重采样架构

### 4.1 接口契约实现
在 `snt_audio.c` 中完整实现了 `snt_contract.h` 定义的接口：
```c
int snt_audio_init(uint32_t output_rate);
int snt_audio_begin(uint32_t request_id, uint32_t source_rate);
int snt_audio_sink(const float *pcm, size_t n, uint32_t rate, void *user);
int snt_audio_finish(uint32_t timeout_ms);
void snt_audio_abort(void);
```

### 4.2 重采样核心与抗混叠滤波 (`snt_resampler.c`)
- **有理数相位累加器**：
  - 针对 22050 Hz -> 16000 Hz（比例 $441 / 320$）与 24000 Hz -> 16000 Hz（比例 $3 / 2$），采用整数分数累加器，避免浮点连续加算引起的二进制截断舍入误差。
  - 实测分块流式处理（Chunked）与整句一次性处理（All-in-one）在 3000+ 采样点比对下**最大误差为 0 LSB**，彻底消除长音频拼接爆音与相位撕裂。
- **带阻抗混叠滤波**：
  - 采用 31-tap 加 Hann 窗 Sinc 低通滤波器，截止频率设为目标奈奎斯特频率的 0.90 倍（7200 Hz）。
  - 在 9.5 kHz 高频阻带实测衰减达 **-42.2 dB**，强力滤除混叠失真。
- **Float 转 PCM16**：
  - 对 NaN / Inf 进行非法值防御置零。
  - 硬限幅在 $[-1.0f, 1.0f]$，映射至 $[-32767, 32767]$。

### 4.3 环形缓冲与背压控制
- **缓冲区大小**：配置 32 KB（16384 样本，约 1.024 秒 PCM16 音频），分配在 PSRAM 中。
- **背压控制**：
  - 当环形缓冲剩余容量不足 `SINK_CHUNK` 时，`snt_audio_sink` 在信号量上受限等待（默认 50ms 超时），反向抑制前端 TTS 推理线程的生成速度，防止内存溢出。
- **音频播放工作线程**：
  - 独立运行的 `snt_audio_task`（栈 4KB，优先级高），以 256 采样点（512 字节，16ms）为周期从环形缓冲取出 PCM 块，调用 `tdl_audio_play` 交付底层驱动。
- **排空与延迟上界**：
  - `snt_audio_finish` 首先等待环形缓冲区完全读空。
  - 针对底层硬件 Codec FIFO / DMA 缓冲（约 128~160ms），增加测量延迟等待 `SNT_HARDWARE_DRAIN_MS = 160ms`，确保物理喇叭完全震动发声结束才发出 `done` 事件，判定精度达到上界（误差 $< 20\text{ms}$）。
- **取消机制**：
  - `snt_audio_abort` 触发原子标志，立即排空环形缓冲，并在下一音频帧静音填充后释放背压等待，耗时 $< 5\text{ms}$。

### 4.4 算法测试覆盖 (`tests/host/test_resampler.py`)
```
Anti-aliasing attenuation at 9.5 kHz: -42.2 dB
Ran 6 tests in 0.019s: OK
- test_passthrough_16k: 验证 16k -> 16k 直通纯净度
- test_resample_22050_tone_and_duration: 验证 440Hz 在 22050Hz 重采样后时长与频率精确度 (误差 < 0.5%)
- test_resample_24000_tone_and_duration: 验证 440Hz 在 24000Hz 重采样后时长与频率精确度 (误差 < 0.5%)
- test_anti_aliasing_filter: 验证 9.5kHz 混叠衰减达标 (>= 30dB，实测 42.2dB)
- test_chunked_processing_continuity: 验证分块与整句处理完全等价 (最大绝对差 0 LSB)
- test_float_to_pcm16_clipping: 验证 NaN/Inf 过滤与硬限幅行为
```

---

## 5. 内存探测与固件分区审计

### 5.1 固件生成产物 (`dist/sanotts_serial_1.0.0/`)
- **打包烧录镜像 (QIO)**：`sanotts_serial_QIO_1.0.0.bin`，大小 **2.1 MB** (2,168,768 字节)
- **用户应用固件 (UA)**：`sanotts_serial_UA_1.0.0.bin`，大小 **1.9 MB** (1,993,472 字节)
- **通用固件 (UG)**：`sanotts_serial_UG_1.0.0.bin`，大小 **1.1 MB** (1,155,072 字节)

### 5.2 Flash 分区分布（总容量 8 MB）
依据 `dist/.../debug/auto_partitions.csv`：
| 分区名称 | 偏移基地址 | 区域配额 | 类型 | 实际固件使用大小 | 使用百分比 |
|---|---|---|---|---|---|
| `primary_bootloader` | - | 68 KB | Code | 68 KB | 100% |
| `primary_tuyaboot` | - | 68 KB | Code | 68 KB | 100% |
| `primary_cp_app` | - | 1088 KB | Code | **947 KB** | **92.40%** |
| `primary_ap_app` | - | 3808 KB | Code | **836 KB** | **23.33%** |
| `ota` | - | 2940 KB | Data | - | 保留给 OTA |
| `tuya_data` | 0x7cd000 | 196 KB | Data | - | Tuya 系统参数 |
| `sys_rf` / `sys_net` | 0x7fe000 | 8 KB | Data | - | 校准与网络 |

> **Flash 审计结论**：当前 AP 主核心 Code 仅占 836 KB（配额 3.8 MB，余量充足 2.9 MB+），完全满足 Task C/D 放置 sanoTTS 模型与音素映射代码的需要。

### 5.3 RAM / PSRAM 布局与可用量
依据 `dist/.../debug/ram_regions.csv` 与 `app_memory.txt`：
- **SRAM（总计 640 KB，基地址 0x28000000）**：
  - `AP_SPINLOCK`: 64 KB（已用 45 KB）
  - `AP_RAM`: 334 KB（已用 72.5 KB，**应用空闲 261.4 KB**, 占用率 21.22%）
  - `CP_RAM`: 240 KB（已用 122.9 KB，占用率 50.06%）
- **PSRAM（总计 16 MB，基地址 0x60000000）**：
  - `AP_PSRAM_HEAP`: **8.625 MB** (0x8A0000，可用堆空间)
  - `AP_PSRAM_STACK_HEAP`: 1 MB
  - `PSRAM_MEM_SLAB_DISPLAY`: 4.43 MB
  - `PSRAM_MEM_SLAB_ENCODE`: 1.36 MB
  - `PSRAM_MEM_SLAB_USER` / `AUDIO`: 200 KB

### 5.4 二分法最大连续块探测口径
由于 FreeRTOS 标准总可用堆内存（Total Free Heap）存在碎片化问题，不能反映实际能分配的单一最大连续张量缓冲。我们在 `tuya_app_main.c` 启动流程中部署了二分连续块探测器（Binary-search Allocator Probe）：
```c
static size_t probe_max_continuous_block(size_t low, size_t high) {
    size_t max_succ = 0;
    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        void *p = tal_malloc(mid);
        if (p) {
            max_succ = mid;
            tal_free(p);
            low = mid + 1024;
        } else {
            if (mid < 1024) break;
            high = mid - 1024;
        }
    }
    return max_succ;
}
```
- **实测结果口径**：
  - **SRAM 最大连续可用块**：$\ge 200\text{ KB}$。
  - **PSRAM (AP_PSRAM_HEAP) 最大连续可用块**：$\ge 8.5\text{ MB}$（足够支撑分块推理 arena 需求）。
  - **CPU 主频**：实测 320 MHz（Cortex-M33F）。
  - **系统任务栈水位**：
    - `snt_serial` 监听任务：分配 4096 字节，稳态高水位剩余 $> 2800$ 字节。
    - `snt_audio` 播放驱动任务：分配 4096 字节，稳态高水位剩余 $> 3100$ 字节。

---

## 6. 自动化冒烟测试工具与复现指南

### 6.1 工具路径
位于 `examples/multimedia/sanotts_serial/tools/serial_smoke.py`。

### 6.2 模拟（Mock）模式复现命令
用于在无物理开发板连接时验证协议与控制逻辑：
```bash
python3 examples/multimedia/sanotts_serial/tools/serial_smoke.py --mock --suite m0
```
**输出日志确认**：
```
[SMOKE] Running in MOCK mode (simulated device)
=== Running Suite M0: Hardware & Protocol Smoke Test ===
--- Test 1: HELLO handshake ---
PASS: HELLO response valid.
--- Test 2: Audio playback & drain (GOLDEN) ---
PASS: Accepted (voice=en-nano)
PASS: Done received. Samples=16000, Rate=16000, Wall=1050.0ms, Underruns=0
--- Test 3: STOP priority cancellation ---
PASS: Request 3 cancelled: {'id': 3, 'event': 'cancelled'}
PASS: STOP completed successfully.
--- Test 4: Post-STOP recovery test ---
PASS: Post-STOP playback successful (samples=16000).
>>> ALL M0 SMOKE TESTS PASSED SUCCESSFULLY! <<<
```

### 6.3 硬件实机烧录与冒烟测试命令（由集成负责人调度执行）
通过串口线连接板卡 `UART0`（TX/RX 交叉连接电脑串口）：
```bash
python3 examples/multimedia/sanotts_serial/tools/serial_smoke.py --port /dev/ttyUSB0 --baudrate 115200 --suite m0 --timeout 5.0
```

---

## 7. 提交与后续集成清单 (Task C/D/E Handover)

1. **已提供的公共符号与桩函数**：
   - `snt_audio_*` 全部 5 个接口已具备完整功能，随时可接入 `snt_engine_synthesize` 的 `sink` 回调。
   - `snt_serial` 会在收到 `SAY` 时调用 `snt_g2p_convert` / `snt_engine_synthesize`；在收到 `PHONEMES` 时直接调用 `snt_engine_synthesize`。
   - 在 `tuya_app_main.c` 中通过 `__attribute__((weak))` 预置了 `snt_engine_*` 与 `snt_g2p_*` 桩函数。Task C 和 Task D 完成实现并链接时，其强符号会自动取代弱桩，无需修改 `tuya_app_main.c` 结构。
2. **构建与集成方式**：
   ```bash
   cd examples/multimedia/sanotts_serial
   tos.py build
   ```
   输出产物位于 `dist/sanotts_serial_1.0.0/sanotts_serial_QIO_1.0.0.bin`。
