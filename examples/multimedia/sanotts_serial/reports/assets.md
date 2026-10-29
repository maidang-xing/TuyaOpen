# T5AI-Core sanoTTS 资源冻结与主机参考报告 (Task A)

- 日期：2026-10-29
- 任务：Task A（资源冻结与主机 oracle 建设）
- 分支：`md/t5-sanotts-task-a`
- 上游仓库：`https://github.com/Ampixa/sanoTTS.git`
- 上游固定提交：`f73b8382bc98a1c8de3488ba30d727ab82587828`
- 交付规范：[t5_core_sanotts_design.md](../../../../docs/t5_core_sanotts_design.md)、[t5_core_sanotts_plan.md](../../../../docs/t5_core_sanotts_plan.md)

---

## 1. 资源冻结与许可证清单

### 1.1 源码与许可证范围

| 目录/文件 | 来源 | 许可证 | 作用 |
|---|---|---|---|
| `third_party/sanotts/include/` | upstream `mcu/include/` | MIT | C99 推理核心头文件（`snt_nano.h`, `snt_piperlite.h`, `snt_front_f32.h`, `snt_port.h` 等） |
| `third_party/sanotts/src/` | upstream `mcu/src/` | MIT | C99 推理实现（标量 reference 内核、nano 解码、piperlite 解码、front 特征扩展） |
| `third_party/sanotts/ports/host/` | upstream `mcu/ports/host/` | MIT | 主机 POSIX 适配层（单核串行 `snt_par_run`, `snt_now_us`） |
| `third_party/sanotts/models/en_us_e12nano/` | upstream `mcu/models/` | MIT | 英文 nano INT8 模型权重偏移头文件 `nano_q8_meta.h` |
| `models/chinese/cmn_dict` 等字典文件 | upstream `mcu/ports/wasm/espeak-data-multi/` | GPL-3.0 | eSpeak-ng 中文 `cmn` 前端字典与音素映射数据 |

> [!NOTE] 许可证分割与合规性
> sanoTTS 的推理引擎本身为纯 C99 实现，属于 MIT 许可证（见 `third_party/sanotts/LICENSE.MIT`）。
> eSpeak-ng 前端字典与 G2P 模块属于 GPL-3.0 许可证（见 `third_party/sanotts/LICENSE`）。
> 所有引入文件完整保留了原始文件头、版权声明与许可证说明。

---

## 2. Flash 与 PSRAM 物理资源预算分析

T5AI-Core 硬件规格：**8 MB Flash**、**16 MB PSRAM**、**640 KB SRAM**。

### 2.1 Flash 占用核算

| 语言与模型 | 资源项 | 字节数 (Bytes) | 折合容量 |
|---|---|---|---|
| **英文 (en_us_e12nano)** | `front_q8.bin` | 175,456 | 171.3 KiB |
| | `model_q8.bin` | 164,224 | 160.4 KiB |
| | **英文模型小计** | **339,680** | **331.7 KiB (0.32 MiB)** |
| **中文 (chinese web/voices)** | `front_f32.bin` | 3,438,220 | 3,357.6 KiB (3.28 MiB) |
| | `dec_f32.bin` | 2,560,816 | 2,500.8 KiB (2.44 MiB) |
| | `meta.json` | 1,230 | 1.2 KiB |
| | `cmn_dict` (eSpeak 字典) | 1,566,347 | 1,529.6 KiB (1.49 MiB) |
| | `phontab` | 59,520 | 58.1 KiB |
| | `phonindex` | 44,904 | 43.9 KiB |
| | `phondata` | 4,096 | 4.0 KiB |
| | `intonations` | 2,312 | 2.3 KiB |
| | `cmn` (voice 描述) | 686 | 0.7 KiB |
| | **中文权重与字典小计** | **7,678,131** | **7,498.2 KiB (7.32 MiB)** |

> [!IMPORTANT] 8 MB Flash 门禁核算结论
> 1. 英文 e12nano 模型仅占 **0.32 MiB**，与固件结合后总占用约 0.8–1.0 MiB，可在任何常规分区中宽松部署。
> 2. 中文 FP32 初始模型与 eSpeak 字典合计达 **7.32 MiB**。
> 3. T5AI-Core 全部 Flash 空间为 8.00 MiB (8,388,608 Bytes)，剩余预算仅为 **710,477 Bytes (约 693.8 KiB)**。
> 4. 因此，按设计规划，**必须使用用户授权的项目专用重划布局**：彻底取消 OTA 分区、清空音视频显示预留区和多余授权/云端分区，启动固件体积必须控制在 650 KiB 以内，方可将完整未压缩中文 FP32 模型和完整 eSpeak 字典装入 8 MB Flash。

### 2.2 PSRAM 运行工作区 (Working Arena) 预算

| 模型 | 推理方式 | 样本长度 / 帧数 | 运行时工作区峰值 (Arena Peak) | 可行性判定 |
|---|---|---|---|---|
| **en_us_e12nano** | INT8 全句合成 | 629 frames (~6.7s) | **171,008 B (167.0 KiB)** | SRAM/PSRAM 均可无缝容纳 |
| **chinese FP32** | 整句未分块 ("你好") | 30 frames (0.35s) | **2,211,840 B (2.11 MiB)** | PSRAM 可容纳 |
| **chinese FP32** | 整句未分块 ("欢迎使用本地语音") | 118 frames (1.37s) | **8,699,904 B (8.30 MiB)** | PSRAM 占用过半 |
| **chinese FP32** | 整句未分块 ("日期2026年10月27日") | 202 frames (2.35s) | **14,893,056 B (14.20 MiB)** | 逼近 16 MB PSRAM 物理极限 |
| **chinese FP32** | 整句未分块 (> 2.5s 语句) | > 215 frames | **> 16,000,000 B (> 16 MiB)** | **物理溢出，必崩** |
| **chinese FP32** | **Task C2 方案：分块解码** | 中心块 16 帧 + 上下文 | **预计约 2.0–3.5 MiB** | **在 16 MB PSRAM 内稳定安全** |

> [!CAUTION] PSRAM 内存墙警告
> 实测表明，中文 FP32 piperlite 解码器的工作区与帧数呈严格线性增长，每增加 1 秒音频（约 86 帧），工作区增加约 **6.35 MB**！
> 2.35 秒的日常短句已需 14.89 MB PSRAM。若直接进行全句解码，稍微稍长句子必导致内存溢出。
> **Task C2 规划的 latent frame 分块解码是中文落地的绝对必选方案**，绝不可作为可选优化对待。

---

## 3. 英文 nano 黄金门禁基准 (en_us_e12nano)

使用 C99 标量参考内核 `snt_host_harness en-nano-golden` 对上游 eval8 集合进行逐项校验，结果如下：

- 随机种子派生：`sha256("000001_i200")[:8] = 2236265385529901705` (bit-exact)
- MT19937 均匀流校验：64 个数值与 `torch.rand` **完全一致 (0 差错，BIT-EXACT)**
- Box-Muller 高斯噪声校验：1660 个数值，最大误差 **1.91e-06**（门限 tol 1e-05，OK）
- Sincos 近似校验：在 $[-32.0, 32.0]$ 区间最大误差 **5.45e-06**（门限 tol 1e-04，OK）

### 3.1 英文 8 行测试集参考指标

| 样本编号 | 标记 (Tokens) | 帧数 (Frames) | 样本数 (Samples) | 波形相关系数 (Corr) | RMS 比值 (RMS Ratio) | 主机耗时 (μs) | 工作区峰值 (Arena B) | 门禁判定 |
|---|---|---|---|---|---|---|---|---|
| `000001_i200` | 73 | 415 | 105,984 | **0.984251** | **0.998682** | 18,790 | 128,944 | PASS |
| `000002_i400` | 71 | 427 | 109,056 | **0.990174** | **0.990125** | 19,036 | 131,248 | PASS |
| `000003_i600` | 80 | 480 | 122,624 | **0.992383** | **0.991472** | 23,143 | 141,424 | PASS |
| `000004_i800` | 85 | 573 | 146,432 | **0.988079** | **0.997280** | 28,540 | 159,360 | PASS |
| `000005_i1000` | 90 | 583 | 148,992 | **0.989716** | **0.995247** | 27,526 | 161,440 | PASS |
| `000006_i1200` | 99 | 614 | 156,928 | **0.992717** | **0.999183** | 36,959 | 167,888 | PASS |
| `000007_i1400` | 107 | 629 | 160,768 | **0.988792** | **0.998494** | 28,789 | 171,008 | PASS |
| `000008_i1600` | 92 | 609 | 155,648 | **0.992512** | **0.999043** | 28,803 | 166,848 | PASS |

- **平均相关系数 (Mean Corr):** `0.989828`
- **最低相关系数 (Min Corr):** `0.984251`（大于 0.980 门槛）
- **PSRAM/SRAM 最高水位:** `171,008 Bytes (167.0 KiB)`
- **采样率:** 24,000 Hz

---

## 4. 中文 10 条黄金参考基准 (chinese web/voices)

依据 `docs/t5_core_sanotts_plan.md` 规定，使用 eSpeak `cmn` 音素表映射并经 C99 主机参考合成器生成的 10 条中文黄金基准。
采样率固定为 **22,050 Hz**。
音素格式遵从 Piper 协议规范：以 `1, 0` 开头（BOS, PAD），以 `2` 结尾（EOS），中间音素交替插入 `0` (PAD)。

### 4.1 10 条黄金句子指标汇总

| 序号 | 语句文本 | 音素数 | 预测帧数 | 样本数 | 音频时长 | 有效 RMS | 峰值幅度 | 完整解码工作区 |
|---|---|---|---|---|---|---|---|---|
| 0 | `你好` | 19 | 30 | 7,680 | 0.35 s | 0.0276 | 0.1773 | 2,211,840 B (2.11 MB) |
| 1 | `欢迎使用本地语音` | 91 | 118 | 30,208 | 1.37 s | 0.0470 | 0.3067 | 8,699,904 B (8.30 MB) |
| 2 | `今天天气怎么样` | 101 | 111 | 28,416 | 1.29 s | 0.0506 | 0.2915 | 8,183,808 B (7.80 MB) |
| 3 | `请打开客厅的灯` | 91 | 108 | 27,648 | 1.25 s | 0.0546 | 0.3231 | 7,962,624 B (7.59 MB) |
| 4 | `现在温度是二十三度` | 113 | 126 | 32,256 | 1.46 s | 0.0555 | 0.3613 | 9,289,728 B (8.86 MB) |
| 5 | `重启后重新连接` | 101 | 112 | 28,672 | 1.30 s | 0.0552 | 0.3065 | 8,257,536 B (7.87 MB) |
| 6 | `银行今天营业` | 75 | 85 | 21,760 | 0.99 s | 0.0467 | 0.2276 | 6,266,880 B (5.98 MB) |
| 7 | `编号12345` | 115 | 147 | 37,632 | 1.71 s | 0.0560 | 0.4025 | 10,838,016 B (10.34 MB) |
| 8 | `时间08:30` | 63 | 72 | 18,432 | 0.84 s | 0.0485 | 0.3460 | 5,308,416 B (5.06 MB) |
| 9 | `日期2026年10月27日` | 167 | 202 | 51,712 | 2.35 s | 0.0618 | 0.4865 | 14,893,056 B (14.20 MB) |

### 4.2 10 条黄金句子详细音素 ID 清单

#### 句子 0: `你好` (n_ids = 19)
```json
[1, 0, 26, 0, 21, 0, 132, 0, 127, 0, 120, 0, 51, 0, 33, 0, 132, 0, 2]
```

#### 句子 1: `欢迎使用本地语音` (n_ids = 91)
```json
[1, 0, 127, 0, 33, 0, 14, 0, 135, 0, 26, 0, 22, 0, 120, 0, 21, 0, 62, 0, 44, 0, 3, 0, 31, 0, 10, 0, 120, 0, 21, 0, 10, 0, 132, 0, 3, 0, 22, 0, 120, 0, 27, 0, 26, 0, 66, 0, 135, 0, 3, 0, 28, 0, 59, 0, 132, 0, 26, 0, 32, 0, 120, 0, 21, 0, 135, 0, 3, 0, 22, 0, 120, 0, 37, 0, 132, 0, 3, 0, 22, 0, 120, 0, 21, 0, 135, 0, 26, 0, 2]
```

#### 句子 2: `今天天气怎么样` (n_ids = 101)
```json
[1, 0, 32, 0, 55, 0, 120, 0, 21, 0, 135, 0, 26, 0, 3, 0, 32, 0, 20, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 32, 0, 20, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 135, 0, 3, 0, 32, 0, 31, 0, 120, 0, 59, 0, 132, 0, 26, 0, 3, 0, 25, 0, 120, 0, 27, 0, 9, 0, 131, 0, 3, 0, 22, 0, 120, 0, 21, 0, 51, 0, 135, 0, 44, 0, 2]
```

#### 句子 3: `请打开客厅的灯` (n_ids = 91)
```json
[1, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 132, 0, 44, 0, 3, 0, 32, 0, 120, 0, 51, 0, 132, 0, 3, 0, 23, 0, 20, 0, 120, 0, 14, 0, 21, 0, 135, 0, 3, 0, 23, 0, 20, 0, 120, 0, 27, 0, 9, 0, 135, 0, 3, 0, 32, 0, 20, 0, 120, 0, 21, 0, 135, 0, 44, 0, 3, 0, 32, 0, 120, 0, 59, 0, 131, 0, 3, 0, 32, 0, 120, 0, 59, 0, 135, 0, 44, 0, 2]
```

#### 句子 4: `现在温度是二十三度` (n_ids = 113)
```json
[1, 0, 55, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 32, 0, 31, 0, 120, 0, 14, 0, 21, 0, 135, 0, 3, 0, 35, 0, 120, 0, 33, 0, 59, 0, 135, 0, 26, 0, 3, 0, 32, 0, 120, 0, 33, 0, 135, 0, 3, 0, 31, 0, 10, 0, 120, 0, 21, 0, 10, 0, 135, 0, 3, 0, 120, 0, 59, 0, 30, 0, 135, 0, 3, 0, 31, 0, 10, 0, 120, 0, 21, 0, 10, 0, 62, 0, 3, 0, 31, 0, 120, 0, 14, 0, 135, 0, 26, 0, 3, 0, 32, 0, 120, 0, 33, 0, 135, 0, 2]
```

#### 句子 5: `重启后重新连接` (n_ids = 101)
```json
[1, 0, 32, 0, 31, 0, 10, 0, 120, 0, 27, 0, 26, 0, 66, 0, 135, 0, 3, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 132, 0, 3, 0, 127, 0, 120, 0, 27, 0, 33, 0, 135, 0, 3, 0, 32, 0, 31, 0, 10, 0, 20, 0, 27, 0, 26, 0, 66, 0, 62, 0, 55, 0, 120, 0, 21, 0, 135, 0, 26, 0, 3, 0, 24, 0, 120, 0, 21, 0, 61, 0, 62, 0, 26, 0, 3, 0, 32, 0, 55, 0, 120, 0, 21, 0, 61, 0, 135, 0, 2]
```

#### 句子 6: `银行今天营业` (n_ids = 75)
```json
[1, 0, 22, 0, 21, 0, 62, 0, 26, 0, 127, 0, 120, 0, 51, 0, 62, 0, 44, 0, 3, 0, 32, 0, 55, 0, 120, 0, 21, 0, 135, 0, 26, 0, 3, 0, 32, 0, 20, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 22, 0, 120, 0, 21, 0, 62, 0, 44, 0, 3, 0, 22, 0, 120, 0, 21, 0, 61, 0, 135, 0, 2]
```

#### 句子 7: `编号12345` (n_ids = 115)
```json
[1, 0, 28, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 127, 0, 120, 0, 51, 0, 33, 0, 135, 0, 3, 0, 31, 0, 10, 0, 121, 0, 21, 0, 10, 0, 62, 0, 59, 0, 30, 0, 135, 0, 3, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 31, 0, 121, 0, 14, 0, 135, 0, 26, 0, 28, 0, 14, 0, 21, 0, 132, 0, 3, 0, 31, 0, 120, 0, 21, 0, 142, 0, 135, 0, 31, 0, 10, 0, 21, 0, 10, 0, 62, 0, 35, 0, 121, 0, 33, 0, 132, 0, 2]
```

#### 句子 8: `时间08:30` (n_ids = 63)
```json
[1, 0, 31, 0, 10, 0, 120, 0, 21, 0, 10, 0, 62, 0, 3, 0, 32, 0, 55, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 28, 0, 120, 0, 51, 0, 135, 0, 3, 0, 31, 0, 120, 0, 14, 0, 135, 0, 26, 0, 31, 0, 10, 0, 21, 0, 10, 0, 62, 0, 2]
```

#### 句子 9: `日期2026年10月27日` (n_ids = 167)
```json
[1, 0, 106, 0, 120, 0, 21, 0, 10, 0, 135, 0, 3, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 135, 0, 3, 0, 59, 0, 30, 0, 135, 0, 3, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 61, 0, 135, 0, 26, 0, 3, 0, 59, 0, 30, 0, 135, 0, 31, 0, 10, 0, 121, 0, 21, 0, 10, 0, 62, 0, 24, 0, 21, 0, 27, 0, 33, 0, 135, 0, 3, 0, 26, 0, 120, 0, 21, 0, 61, 0, 62, 0, 26, 0, 3, 0, 31, 0, 10, 0, 120, 0, 21, 0, 10, 0, 62, 0, 3, 0, 22, 0, 120, 0, 37, 0, 61, 0, 135, 0, 3, 0, 121, 0, 59, 0, 30, 0, 135, 0, 31, 0, 10, 0, 21, 0, 10, 0, 62, 0, 32, 0, 55, 0, 20, 0, 120, 0, 21, 0, 135, 0, 3, 0, 106, 0, 120, 0, 21, 0, 10, 0, 135, 0, 2]
```

---

## 5. 验收命令与复现记录

在本工作区（分支 `md/t5-sanotts-task-a`）下执行以下四条门禁验收命令，确保退出码均为 0：

```bash
# 1. 单元测试（校验损坏或缺失 manifest 时能准确报错）
python -m unittest discover -s examples/multimedia/sanotts_serial/tests/host -p 'test_assets.py'

# 2. 资源哈希与尺寸校验（非零退出保证）
python examples/multimedia/sanotts_serial/tools/fetch_models.py --verify-only

# 3. 英文 nano INT8 黄金门禁基准
python examples/multimedia/sanotts_serial/tools/host_reference.py --voice en-nano --suite golden

# 4. 中文 web/voices smoke 门禁基准
python examples/multimedia/sanotts_serial/tools/host_reference.py --voice zh --suite smoke
```

### 实际运行终端输出记录

```text
$ python -m unittest discover -s examples/multimedia/sanotts_serial/tests/host -p 'test_assets.py'
.............
----------------------------------------------------------------------
Ran 13 tests in 0.015s

OK

$ python examples/multimedia/sanotts_serial/tools/fetch_models.py --verify-only
TuyaOpen sanoTTS Asset Manager
Base dir: /home/share/samba/tyopen/worktrees/md-t5-sanotts-task-a/examples/multimedia/sanotts_serial
Manifest: /home/share/samba/tyopen/worktrees/md-t5-sanotts-task-a/examples/multimedia/sanotts_serial/models/manifest.json
[OK] models/en_us_e12nano/front_q8.bin                ( 175456 B) SHA-256 verified
[OK] models/en_us_e12nano/model_q8.bin                ( 164224 B) SHA-256 verified
[OK] third_party/sanotts/models/en_us_e12nano/nano_q8_meta.h (   6317 B) SHA-256 verified
[OK] tests/fixtures/en_us_e12nano/r00_ids.bin         (    292 B) SHA-256 verified
[OK] tests/fixtures/en_us_e12nano/r00_durs.bin        (    292 B) SHA-256 verified
[OK] tests/fixtures/en_us_e12nano/r00_audio.bin       ( 423936 B) SHA-256 verified
[OK] tests/fixtures/en_us_e12nano/e2e_noise.bin       (   6640 B) SHA-256 verified
[OK] tests/fixtures/en_us_e12nano/e2e_uniform.bin     (    256 B) SHA-256 verified
[OK] tests/fixtures/en_us_e12nano/rows.txt            (    377 B) SHA-256 verified
[OK] models/chinese/front_f32.bin                     (3438220 B) SHA-256 verified
[OK] models/chinese/dec_f32.bin                       (2560816 B) SHA-256 verified
[OK] models/chinese/meta.json                         (   1230 B) SHA-256 verified
[OK] models/chinese/cmn_dict                          (1566347 B) SHA-256 verified
[OK] models/chinese/phondata                          (   4096 B) SHA-256 verified
[OK] models/chinese/phonindex                         (  44904 B) SHA-256 verified
[OK] models/chinese/phontab                           (  59520 B) SHA-256 verified
[OK] models/chinese/intonations                       (   2312 B) SHA-256 verified
[OK] models/chinese/cmn                               (    686 B) SHA-256 verified
[OK] tests/fixtures/chinese/zh_golden.json            (   5897 B) SHA-256 verified

All assets verified successfully against manifest (SHA-256 and size match).

$ python examples/multimedia/sanotts_serial/tools/host_reference.py --voice en-nano --suite golden
Running en-nano (golden): .../tools/snt_host_harness en-nano-golden .../tests/fixtures/en_us_e12nano 8
=== sanoTTS en_us_e12nano Host Reference ===
seed    : sha256("000001_i200")[:8] = 2236265385529901705, fixture says 2236265385529901705 OK
uniform : 64 values, 0 differ -- BIT-EXACT vs torch.rand
noise   : 1660 values, 446 differ, max |delta| 1.91e-06 (tol 1e-05) OK
sincos  : max |error| 5.45e-06 over |phi| <= 32.00 (worst at -29.3267, tol 1e-04) OK

row            frames  samples       corr  rms_ratio        us      arena
000001_i200       415   105984   0.984251   0.998682     18790     128944
000002_i400       427   109056   0.990174   0.990125     19036     131248
000003_i600       480   122624   0.992383   0.991472     23143     141424
000004_i800       573   146432   0.988079   0.997280     28540     159360
000005_i1000      583   148992   0.989716   0.995247     27526     161440
000006_i1200      614   156928   0.992717   0.999183     36959     167888
000007_i1400      629   160768   0.988792   0.998494     28789     171008
000008_i1600      609   155648   0.992512   0.999043     28803     166848

Rows evaluated: 8, mean corr: 0.989828, MIN corr: 0.984251, arena peak: 171008 bytes
RESULT: PASS

$ python examples/multimedia/sanotts_serial/tools/host_reference.py --voice zh --suite smoke
Running zh (smoke): .../tools/snt_host_harness zh-suite .../models/chinese .../tests/fixtures/chinese 2
=== sanoTTS Chinese web/voices Host Reference ===
Models dir: .../models/chinese
Fixtures dir: .../tests/fixtures/chinese

text                   frames  samples        rms       peak         ms    arena_B
你好                     30     7680     0.0276     0.1773       97.2    2211840
欢迎使用本地语音    118    30208     0.0470     0.3067      387.2    8699904
RESULT: PASS
```

---

## 6. 给后续任务（Task B / C / D / E）的集成指引与契约交接

1. **给 Task C (英文与中文引擎):**
   - 英文模型权重：`models/en_us_e12nano/{front_q8.bin, model_q8.bin}`。
   - 英文头文件：`third_party/sanotts/models/en_us_e12nano/nano_q8_meta.h`。
   - 英文参考输入与输出：`tests/fixtures/en_us_e12nano/r00_*`（必须以 `dur_override` 进行无漂移相关系数校验，门限 corr > 0.98）。
   - 中文模型权重：`models/chinese/{front_f32.bin, dec_f32.bin}`。
   - 中文分块解码必须以本报告第 2.2 节的实测工作区为依据，将中心帧长度控制在合理范围（如 16 帧），并与 `tests/fixtures/chinese/s00`..`s09` 逐条进行数值比对。
2. **给 Task D (板端中文 G2P):**
   - 字典与映射文件：`models/chinese/{cmn_dict, phontab, phonindex, phondata, intonations, cmn}`。
   - 10 条黄金中文输入原文与预期音素 ID 数组已完全冻结于本报告第 4.2 节及 `tests/fixtures/chinese/zh_golden.json`。板端与主机输出音素 IDs 必须逐项 100% 一致。
3. **给 Task B / E (音频与系统集成):**
   - 采样率契约：英文原生 24,000 Hz，中文原生 22,050 Hz。
   - Core 喇叭固定为 16,000 Hz 单声道，必须由 Task B 的重采样模块转换；模型门禁在原生采样率下执行，重采样与音频排空另外验收。
