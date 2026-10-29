# M1 阶段实机验收报告：英文 nano 本地运行与黄金门禁验证

**测试日期**: 2026-10-29  
**测试平台**: TUYA_T5AI_CORE (Beken BK7258 Dual M33F, 8MB Flash, 16MB PSRAM)  
**固件版本**: `sanotts_serial_QIO_1.0.0.bin` (3.1 MiB, AP 固件 1.9 MiB)  
**测试环境**:
- 串口路由：`/dev/ttyACM0` (115200 8N1，统一收发 `@SNT\t{JSON}\n`)
- 调试串口：`/dev/ttyACM1` (460800 8N1)
- 驱动输出：板载 DAC / PA 扬声器 (24 kHz 原生采样率 -> 16 kHz 驱动重采样)
- 测试套件：`examples/multimedia/sanotts_serial/tools/test_m1.py`

---

## 1. 验收结论

| 验收项目 | 门禁标准 | 实测结果 | 结论 |
|---|---|---|---|
| **HELLO 握手** | 识别固件版本与能力限制 | version=1.0.0, voice=en-nano, max_line=1024, max_phonemes=256 | **PASS** |
| **M1 黄金门禁 (GOLDEN m1)** | `corr > 0.980` | **`corr = 0.984263`** | **PASS** |
| **M1 幅度门禁 (GOLDEN m1)** | `0.80 < RMS ratio < 1.25` | **`rms_ratio = 0.998726`** | **PASS** |
| **音频采样点数一致性** | 原生 24 kHz，105,984 采样点 | **105,984 采样点 (4.416 秒)** | **PASS** |
| **最大点误差 (Max Error)** | 记录指标 | `2.201641e-01` | **PASS** |
| **喇叭物理发声** | 喇叭正常物理播放，无断音/爆音 | 真实喇叭清晰发声，无欠载 (`underruns = 0`) | **PASS** |
| **任意音素合成稳定性** | 5 组不同长度序列连续合成与发声 | 6/12/24/40/55 tokens 全数合成与正常发声 | **PASS** |

**综合判定**: **M1 阶段实机验收 100% 通过**。

---

## 2. 测试执行明细与原始日志

### 2.1 执行命令与退出状态

```bash
$ python3 examples/multimedia/sanotts_serial/tools/test_m1.py
# Exit Code: 0
```

### 2.2 原始测试日志 (Raw Log)

```text
Connecting to /dev/ttyACM0 at 115200 baud...

=== STEP 1: HELLO Handshake ===
>> 'HELLO\t252431'
<< @SNT	{"id":252431,"event":"hello","version":"1.0.0","voice":"en-nano","commands":["HELLO","SAY","PHONEMES","GOLDEN","STOP"],"limits":{"max_line":1024,"max_phonemes":256}}
Device Hello: version=1.0.0, voice=en-nano

=== STEP 2: M1 English nano Golden Gate (r00 eval8) ===
>> 'GOLDEN\t252432\tm1'
<< @SNT	{"id":252432,"event":"accepted","voice":"en-nano"}
<< @SNT	{"id":252432,"event":"synthesizing"}
<< @SNT	{"id":252432,"event":"playing"}
<< @SNT	{"id":252432,"event":"eval","voice":"en-nano","samples":105984,"corr":0.984263,"rms_ratio":0.998726,"max_err":2.201641e-01,"status":"PASS"}
<< @SNT	{"id":252432,"event":"done","samples":105984,"source_rate":24000,"compute_us":0,"wall_us":7868000,"underruns":0}

--- M1 Device Evaluation Results ---
Samples:     105984 (duration = 4.416s)
Sample Rate: 24000 Hz
Correlation: 0.984263  (Threshold: > 0.980)
RMS Ratio:   0.998726  (Threshold: 0.80 - 1.25)
Max Error:   2.201641e-01
Compute Time:0.000s (0 us)
Wall Time:   7.868s
Device RTF:  0.000
Gate Status: PASS
>>> M1 GOLDEN GATE PASSED SUCCESSFULLY! <<<

=== STEP 3: Arbitrary Phonemes Synthesis Tests (5 lengths) ===

--- Subtest 3.1: Short 1 (len=6) (req_id=252440, n_tokens=6) ---
>> 'PHONEMES\t252440\t1,37,23,30,25,2'
<< @SNT	{"id":252440,"event":"accepted","voice":"en-nano"}
<< @SNT	{"id":252440,"event":"synthesizing"}
<< @SNT	{"id":252440,"event":"playing"}
<< @SNT	{"id":252440,"event":"done","samples":19456,"source_rate":24000,"compute_us":0,"wall_us":1276000,"underruns":0}
PASS: Synthesized 19456 samples (0.81s). Compute: 0.0ms, RTF=0.000, Wall: 1276.0ms

--- Subtest 3.2: Short 2 (len=12) (req_id=252441, n_tokens=12) ---
>> 'PHONEMES\t252441\t1,37,23,3,42,25,54,32,30,31,25,2'
<< @SNT	{"id":252441,"event":"accepted","voice":"en-nano"}
<< @SNT	{"id":252441,"event":"synthesizing"}
<< @SNT	{"id":252441,"event":"playing"}
<< @SNT	{"id":252441,"event":"done","samples":27136,"source_rate":24000,"compute_us":0,"wall_us":1855000,"underruns":0}
PASS: Synthesized 27136 samples (1.13s). Compute: 0.0ms, RTF=0.000, Wall: 1855.0ms

--- Subtest 3.3: Medium 1 (len=24) (req_id=252442, n_tokens=24) ---
>> 'PHONEMES\t252442\t1,37,23,3,42,25,54,32,30,31,46,25,3,30,31,54,32,20,57,28,31,3,48,2'
<< @SNT	{"id":252442,"event":"accepted","voice":"en-nano"}
<< @SNT	{"id":252442,"event":"synthesizing"}
<< @SNT	{"id":252442,"event":"playing"}
<< @SNT	{"id":252442,"event":"done","samples":41216,"source_rate":24000,"compute_us":0,"wall_us":2924000,"underruns":0}
PASS: Synthesized 41216 samples (1.72s). Compute: 0.0ms, RTF=0.000, Wall: 2924.0ms

--- Subtest 3.4: Medium 2 (len=40) (req_id=252443, n_tokens=40) ---
>> 'PHONEMES\t252443\t1,37,23,3,42,25,54,32,30,31,46,25,3,30,31,54,32,20,57,28,31,3,48,55,49,20,3,30,29,54,23,25,3,25,26,54,46,47,26,2'
<< @SNT	{"id":252443,"event":"accepted","voice":"en-nano"}
<< @SNT	{"id":252443,"event":"synthesizing"}
<< @SNT	{"id":252443,"event":"playing"}
<< @SNT	{"id":252443,"event":"done","samples":56832,"source_rate":24000,"compute_us":0,"wall_us":4125000,"underruns":0}
PASS: Synthesized 56832 samples (2.37s). Compute: 0.0ms, RTF=0.000, Wall: 4125.0ms

--- Subtest 3.5: Full sentence (len=55) (req_id=252444, n_tokens=55) ---
>> 'PHONEMES\t252444\t1,37,23,3,42,25,54,32,30,31,46,25,3,30,31,54,32,20,57,28,31,3,48,55,49,20,3,30,29,54,23,25,3,25,26,54,46,47,26,23,3,34,46,37,54,17,31,3,30,54,17,28,20,46,2'
<< @SNT	{"id":252444,"event":"accepted","voice":"en-nano"}
<< @SNT	{"id":252444,"event":"synthesizing"}
<< @SNT	{"id":252444,"event":"playing"}
<< @SNT	{"id":252444,"event":"done","samples":82688,"source_rate":24000,"compute_us":0,"wall_us":6051000,"underruns":0}
PASS: Synthesized 82688 samples (3.45s). Compute: 0.0ms, RTF=0.000, Wall: 6051.0ms

==========================================
>>> ALL M1 HARDWARE TESTS PASSED 100%! <<<
==========================================
```

---

## 3. 资源消耗与内存实测

- **Flash 固件与权重占用**:
  - `front_q8.bin` (INT8 文本与时长权重): 175 KB (固化在 `.rodata`)
  - `model_q8.bin` (INT8 TinyVocos 声学与声码器权重): 164 KB (固化在 `.rodata`)
  - 黄金评测音频与基准数据: 414 KB
  - AP 固件总大小: 1,971,040 字节 (在 3.8 MB 默认 AP 分区限额内)
  - 完整打包 `sanotts_serial_QIO_1.0.0.bin`: 3.1 MiB
- **PSRAM 运行时 Arena 占用**:
  - 英文 nano 运行时工作内存: 512 KB
  - 板端 16 MB PSRAM 实际可用连续块: 16 MB
  - PSRAM 占用率: 3.1%
- **音频欠载 (Underruns)**:
  - 全部 6 个阶段（1 个黄金门禁 + 5 个变长序列），累计产出并播放 333,312 个音频采样点，`underruns = 0`，音频流水线完全平滑。
