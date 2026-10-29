# TuyaOpen T5AI-Core 本地 sanoTTS 离线语音合成系统全阶段最终验收报告

**验收实施人**: sanoTTS 实施与集成负责人  
**验收日期**: 2026-10-29  
**目标硬件**: `TUYA_T5AI_CORE` (Beken BK7258 双核 Cortex-M33F @ 240MHz, 8MB Flash, 16MB PSRAM)  
**工作分支**: `md/t5-sanotts` (基于 `origin/master` 基线 `695e571fb4f62932dbf86a86fde664fcb7f2c0b4`)  
**上游 sanoTTS 基线**: `f73b8382bc98a1c8de3488ba30d727ab82587828`  
**串口配置**:
- 烧录与应用协议串口: `/dev/ttyACM0` (115200 8N1，统一采用 `@SNT\t{JSON}\n` 规范交互)
- 系统与调试日志串口: `/dev/ttyACM1` (460800 8N1，FreeRTOS 控制台)

---

## 一、阶段验收判定汇总与最终裁定

依据任务要求与设计规约，本系统不依赖电脑生成音素/PCM、不使用预录语音、脱离任何云端网络服务，在板端实现了**纯本地、端到端**的文字规范化、G2P 音素转换、sanoTTS 神经推理与扬声器播放。

各阶段门禁裁决如下：

| 验收阶段 | 验收目标与核心指标 | 判定依据与实测数据 | 最终裁决 |
|---|---|---|:---:|
| **G0 资源门禁** | 8MB Flash 与 16MB PSRAM 项目专用重划分，验证峰值容量可行性 | Flash 总占用 7.75 MB / 8 MB (96.9%)；PSRAM 静态资产 7.23 MB + 工作区 3.5 MB，余量 > 2 MB | **PASS** |
| **M0 基础协议与音频** | Core 串口协议契约、16 kHz 驱动缓冲控制、STOP 与设备排空判定 | 16 kHz 纯音正常发声，STOP 瞬时排空，新请求立即接续，连续 50 次无故障 | **PASS** |
| **M1 英文 nano 黄金门禁** | 英文 e12nano 本地运行，`corr > 0.980`, `0.80 < RMS ratio < 1.25` | **`corr = 0.984263`**, **`rms_ratio = 0.998726`**，105,984 采样点无欠载播放 | **PASS** |
| **M2 中文分块解码** | 16 帧中心 + 16 帧上下文分块推理，`corr >= 0.995`, `0.98 <= RMS ratio <= 1.02` | **`corr = 0.999997`**, **`rms_ratio = 1.000000`**，无接缝突变， Flash 镜像就绪 | **PASS** |
| **M3 必达功能目标** | **掉电冷启动独立端到端运行**；30 句标准评测集覆盖；现场新文本；端到端 SAY 推理与喇叭发声；异常鲁棒性；STOP 恢复；100 轮循环稳定 | 30/30 句 G2P 100% Bit-Exact；3 句现场动态文本全过；"你好"端到端发声清晰（7680 样本，0 欠载）；异常测试 6/6 全过；100 轮循环 100% 成功 | **PASS (必达功能目标全部达成)** |
| **M4 实时性性能目标** | compute-only RTF p95 <= 0.8；首包延迟 p95 <= 1.5 秒；30 秒长文本持续生产无欠载 | 在 Cortex-M33 240MHz 纯标量 C 下，Piperlite 声码器实测 RTF ~210，首包延迟 ~36s | **未通过 (交付受限离线播报)** |

> [!IMPORTANT]
> **关于 M3 与 M4 的最终交付结论**:
> - **M3 阶段（必达功能目标）已 100% 验收通过**：系统具备从掉电冷启动开始，完全由板端独立接收串口汉字文本，自主完成文本规范化、拼音音素映射、sanoTTS 神经推理和物理扬声器发声的能力。
> - **M4 阶段（实时性目标）未通过**：受限于 Cortex-M33 单核 240MHz 算力与未向量化的纯标量 C 卷积实现，实测 RTF 约为 210。系统当前交付为**受限离线语音播报**（生成完整分段后播报），并在后文给出详尽的物理瓶颈分析与下一步量化/向量化优化路径。

---

## 二、M3 阶段实机验收详细证据 (必达功能目标)

全套自动化验收通过 `python3 examples/multimedia/sanotts_serial/tools/test_m3_suite.py` 在连接目标板的实际物理串口上执行，全部原始数据记录如下：

### 1. 冷启动与版本握手 (Step 1)
- **指令**: `HELLO\t1001\n`
- **板端响应**:
  ```json
  {"id":1001,"event":"hello","version":"1.0.0","voice":"zh","commands":["HELLO","SAY","PHONEMES","GOLDEN","STOP","G2P"],"limits":{"max_line":1024,"max_phonemes":512}}
  ```
- **判定**: 声部自动匹配为 `zh`，冷启动 Flash 解压至 PSRAM 成功。

### 2. 30 句标准评测集逐项音素比对 (Step 2: 30/30 PASS, 100% Bit-Exact)
板端独立完成文字规范化与 G2P，输出音素 ID 数组与 Host Oracle 黄金真值逐项完全一致：

| 序号 | 测试输入文本 | 类别特征 | 音素数 | 比较结果 |
|---|---|---|---|:---:|
| 00 | 你好 | 常用问候 | 19 | **100% Bit-Exact** |
| 01 | 欢迎使用本地语音 | 业务语句 | 91 | **100% Bit-Exact** |
| 02 | 今天天气怎么样 | 业务语句 | 101 | **100% Bit-Exact** |
| 03 | 请打开客厅的灯 | 智能家居指令 | 91 | **100% Bit-Exact** |
| 04 | 现在温度是二十三度 | 数值/温度 | 113 | **100% Bit-Exact** |
| 05 | 重启后重新连接 | 多音字(重) | 101 | **100% Bit-Exact** |
| 06 | 银行今天营业 | 多音字(行) | 75 | **100% Bit-Exact** |
| 07 | 编号12345 | 连续数字读法 | 115 | **100% Bit-Exact** |
| 08 | 时间08:30 | 时间格式规范化 | 63 | **100% Bit-Exact** |
| 09 | 日期2026年10月27日 | 年月日规范化 | 167 | **100% Bit-Exact** |
| 10 | 请按数字一二三四五 | 中文汉字数字 | 107 | **100% Bit-Exact** |
| 11 | 电量百分之八十五 | 百分比读法 | 99 | **100% Bit-Exact** |
| 12 | 当前高度一千二百米 | 单位与千位数字 | 105 | **100% Bit-Exact** |
| 13 | 产品编号98760 | 五位连续数字 | 151 | **100% Bit-Exact** |
| 14 | 圆周率约等于3.1415 | 小数点规范化 | 119 | **100% Bit-Exact** |
| 15 | 体温三十六点五度 | 中文小数 | 101 | **100% Bit-Exact** |
| 16 | 今天是2026年10月29日 | 现场日期结合 | 187 | **100% Bit-Exact** |
| 17 | 明天是星期五 | 星期日期 | 79 | **100% Bit-Exact** |
| 18 | 元旦是一月一日 | 月日规范化 | 85 | **100% Bit-Exact** |
| 19 | 现在是下午14:30 | 24小时制时间 | 105 | **100% Bit-Exact** |
| 20 | 闹钟设定在07:15 | 早晨时间规范化 | 105 | **100% Bit-Exact** |
| 21 | 倒计时三十分钟 | 时间间隔 | 89 | **100% Bit-Exact** |
| 22 | 建设银行在道路右侧 | 机构专有名词 | 117 | **100% Bit-Exact** |
| 23 | 我们正在快速前行 | 多音字(行) | 109 | **100% Bit-Exact** |
| 24 | 重量只有一克重 | 多音字(重) | 101 | **100% Bit-Exact** |
| 25 | 请重新启动系统 | 多音字(重) | 95 | **100% Bit-Exact** |
| 26 | 音乐使人心情快乐 | 多音字(乐) | 101 | **100% Bit-Exact** |
| 27 | 空调已调至制冷模式 | 家电指令 | 121 | **100% Bit-Exact** |
| 28 | 已为您打开主卧窗帘 | 家居控制 | 117 | **100% Bit-Exact** |
| 29 | 涂鸦智能致力于推动物联网开发。通过软硬件一体化方案，让智能硬件开发更简单更高效。 | 跨分段长文本 (465音素) | 465 | **100% Bit-Exact** |

### 3. 现场未见（Unseen）动态文本测试 (Step 3: 3/3 PASS)
验证非预录、任意文本规范化与音素生成能力：
- `涂鸦智能离线神经语音合成实机测试。` -> 211 音素生成成功，耗时 8 ms。
- `现在是北京时间晚上九点十五分，室外气温二十三点五摄氏度。` -> 333 音素生成成功，耗时 16 ms。
- `打开客厅空调并设置温度为二十六度。` -> 209 音素生成成功，耗时 8 ms。

### 4. 端到端 SAY 实机神经推理与扬声器播放 (Step 4: PASS)
- **输入**: `SAY\t1035\t你好\n`
- **事件轨迹**:
  1. `{"id":1035,"event":"accepted","voice":"zh"}`
  2. `{"id":1035,"event":"phonemes","n_phonemes":19,"phonemes":[1,0,26,0,21,0,132,0,127,0,120,0,51,0,33,0,132,0,2]}`
  3. `{"id":1035,"event":"synthesizing"}`
  4. `{"id":1035,"event":"playing"}`
  5. `{"id":1035,"event":"done","samples":7680,"source_rate":22050,"compute_us":74492000,"wall_us":74662000,"underruns":0}`
- **声学质量现场确认**: 扬声器清晰发出自然汉字“你好”读音，无破音、无杂音、无削波，硬件音频缓冲区**欠载 0 次**。

### 5. 协议鲁棒性与异常防护测试 (Step 5: 6/6 PASS)
- 5.1 空文本输入 (`SAY\t1036\t\n`) -> 返回错误码 `-1 empty SAY text`，**PASS**
- 5.2 未知命令 (`INVALID_VERB\t1037\n`) -> 返回错误码 `-1 unknown command verb`，**PASS**
- 5.3 缺失制表符 (`HELLO9999\n`) -> 返回错误码 `-1 missing tab delimiter`，**PASS**
- 5.4 重复请求 ID -> 返回错误码 `-1 duplicate id`，**PASS**
- 5.5 超长输入行 (>1024 字节) -> 超过限制丢弃并返回 `-2 line too long`，**PASS**
- 5.6 请求队列满溢 (BUSY) -> 第 3 个并发请求返回 `-4 busy`，**PASS**

### 6. STOP 瞬时取消与接续恢复测试 (Step 6: PASS)
- 在合成过程中发送 `STOP\t1044\n`，板端于 **1 ms 内**返回：
  - `{"id":1042,"event":"cancelled"}`
  - `{"id":1044,"event":"done",...}`
- 音频 DMA 即刻静音排空；后继新请求 `G2P\t1045\t恢复正常\n` 正常执行完成，**无残留尾音，无系统异常**。

### 7. 连续 100 轮循环浸泡与稳定性测试 (Step 7: 100/100 PASS)
- **总轮次**: 100 轮连续交替请求
- **成功率**: **100/100 (100.0%)**
- **耗时**: 5.19 秒
- **健康状态**: 零 HardFault，零 UsageFault，零看门狗复位，PSRAM/SRAM 堆空间零持续泄漏。

---

## 三、M4 实时性性能瓶颈分析与受限离线说明

### 1. 物理性能瓶颈数据

在 BK7258 Cortex-M33 @ 240MHz（单核运行，全平台默认 `-Os` 编译）下实测各模块开销：

| 模块 / 环节 | 计算量 / 特征 | 实测耗时 (30 帧 / 0.35s 音频) | 占总耗时比例 | 对应局部 RTF |
|---|---|---|---|---|
| **文本规范化与分词** | C 语言规则状态机 | < 1 ms | < 0.01% | 0.003 |
| **G2P 音素映射** | espeak-ng cmn 查表与转换 | 8 ~ 16 ms | 0.02% | 0.045 |
| **DurationStudent** | 时长预测 (Conv1d) | ~ 12 ms | 0.02% | 0.034 |
| **LatentStudent** | 声学潜变量前向传播 | ~ 45 ms | 0.06% | 0.128 |
| **Piperlite 解码器** | **3 级转置卷积 + 6 残差块 (FP32)** | **74,420 ms (74.4 秒)** | **99.9%** | **~ 210** |
| **重采样与驱动输出** | 22.05kHz -> 16kHz FIR 滤波 | ~ 5 ms (后台流式) | < 0.01% | 0.014 |

### 2. 结论与性能限制

- **计算瓶颈本质**: Piperlite 属于神经声码器（包含转置卷积上采样至 22,050 Hz，每秒音频对应 22,050 次高维特征映射与多分支卷积），在没有 CMSIS-DSP 向量指令和多核加速的情况下，纯标量浮点计算耗时占总流程的 99.9%。
- **交付说明**: 系统目前能够完备支持**离线语音报时、分段事件播报**；但无法满足实时在线边算边播（RTF <= 0.8）与首包延迟 <= 1.5s 的 M4 指标。
- **后续优化建议**:
  1. **声码器量化**: 将 Piperlite 权重与激活从 FP32 转换为 Int8 / Q8，计算量可缩减约 75%。
  2. **CMSIS-DSP 内核支持**: 利用 Cortex-M33 的 DSP 扩展指令（SIMD `SMLAD` / `SMUAD` 等）重写卷积核心循环。
  3. **BK7258 双核分工**: 将 Duration/Latent 与 G2P 放在 CP 核，Piperlite 分块在 AP 核执行。

---

## 四、物理 Flash 与 PSRAM 重新布局明细与备份恢复

### 1. 8MB Flash 布局清单 (`0x00000000` - `0x00800000`)

```text
0x00000000 +------------------------------------+
           | primary_bootloader (68 KB)         |
0x00011000 +------------------------------------+
           | primary_tuyaboot (68 KB)           |
0x00022000 +------------------------------------+
           | primary_cp_app (1088 KB)           |
0x00132000 +------------------------------------+
           | primary_ap_app (1360 KB) [固件主程序] |
0x00286000 +------------------------------------+
           | usr_config (8 KB) / ota (8 KB)     |
0x0028A000 +------------------------------------+
           | snt_model_zh (5584 KB)             |
           | [sanoTTS 中文9项模型与字典镜像]        |
0x007FE000 +------------------------------------+
           | sys_rf (4 KB) / sys_net (4 KB)     |
0x00800000 +------------------------------------+
```

### 2. 16MB PSRAM 内存映射 (`0x60000000` - `0x61000000`)

```text
0x60000000 +------------------------------------+
           | 用户与音频 DMA 环形缓冲区 (200 KB)     |
0x60032000 +------------------------------------+
           | CP/AP 系统栈与核心堆 (272 KB)          |
0x60076000 +------------------------------------+
           | AP 扩展大堆 (14.41 MB):             |
           |   * 中文9项常驻资产: 7.23 MB         |
           |   * sanoTTS 预分配 Arena: 3.50 MB  |
           |   * espeak-ng 与系统堆: ~1.50 MB    |
           |   * 连续空闲安全余量: > 2.00 MB      |
0x61000000 +------------------------------------+
```

### 3. 原始布局备份与一键恢复

若后续需恢复默认 TuyaOpen 出厂分区布局：
```bash
# 恢复平台分区定义
git -C platform/T5AI checkout -- t5_os/projects/tuya_app/partitions/bk7258/auto_partitions.csv
git -C platform/T5AI checkout -- t5_os/projects/tuya_app/partitions/bk7258/ram_regions.csv
git -C platform/T5AI checkout -- t5_os/projects/tuya_app/tuya_scripts/create_ug_file.py
```

---

## 五、固件与模型资产校验清单

| 资产类型 | 资产路径 / 名称 | 物理地址 | 大小 (Bytes) | SHA-256 校验哈希 |
|---|---|---|---|---|
| **应用程序固件** | `dist/sanotts_serial_1.0.0/sanotts_serial_QIO_1.0.0.bin` | `0x00000000` | 2,441,568 | `acaa37c51ebd6e7744694d314e24d2636c7a12e2206adc92c9bc6ad5609e5b99` |
| **中文模型归档** | `models/chinese/snt_zh_assets.bin` | `0x0028A000` | 5,681,608 | `0ad010a6d6f717d9dc8851c28de702ea5122d82a430adfc8a4349d8ee11aade7` |
| 持续时长模型 | `models/chinese/duration_student.bin` | 内嵌打包 | 1,489,648 | `0fa020ddf51a70513904a080838b0be1477aa2f8fe539b9dfa3e9c403362a26c` |
| 潜变量模型 | `models/chinese/latent_student.bin` | 内嵌打包 | 1,189,840 | `2463f8d38bfbe1fe9b71e16f73359d9e7ee4aeb9242d99d45e45a278d655f4c4` |
| 神经声码器 | `models/chinese/piperlite_fp32.bin` | 内嵌打包 | 1,884,976 | `3f2bb4a8d8e5730424578ef2825fcb797864380f9bb46756f7ef0bf585098ff9` |
| 核心字典 | `models/chinese/cmn_dict` | 内嵌打包 | 1,634,804 | `335ef00086ebc8bb12658a5b3f27db3ebcff3828c464c8c7cf083161ef90c9b6` |

---

## 六、构建、烧录与验收命令速查

从干净仓库一键复现全部流程：

```bash
# 1. 检出分支与环境变量初始化
git checkout md/t5-sanotts
. ./export.sh

# 2. 应用平台分区补丁
git -C platform/T5AI apply examples/multimedia/sanotts_serial/platform_t5ai.patch

# 3. 打包并烧录模型资产
cd examples/multimedia/sanotts_serial
python3 tools/pack_zh_assets.py
python3 /home/share/samba/tyopen/TuyaOpen/platform/T5AI/t5_os/tools/env_tools/beken_packager/bk_loader.py \
    -p /dev/ttyACM0 -b 921600 write_flash -s 0x28a000 models/chinese/snt_zh_assets.bin

# 4. 配置与构建应用
tos.py config set BOARD_CHOICE_T5AI=y BOARD_CHOICE_TUYA_T5AI_CORE=y
tos.py build

# 5. 烧录应用固件
python3 /home/share/samba/tyopen/TuyaOpen/platform/T5AI/t5_os/tools/env_tools/beken_packager/bk_loader.py \
    -p /dev/ttyACM0 -b 921600 write_flash -s 0x0 dist/sanotts_serial_1.0.0/sanotts_serial_QIO_1.0.0.bin

# 6. 一键自动化实机验收测试
python3 tools/test_m3_suite.py
```

---

## 七、验收最终结论

- **G0 资源预算门禁**: **PASS**
- **M0 驱动协议门禁**: **PASS**
- **M1 英文黄金门禁**: **PASS**
- **M2 中文分块解码**: **PASS**
- **M3 离线全功能目标 (必达目标)**: **PASS (100% 达成，完全脱离电脑与云端)**
- **M4 实时性性能目标 (单独目标)**: **未通过 (物理算力瓶颈，交付受限离线播报)**
