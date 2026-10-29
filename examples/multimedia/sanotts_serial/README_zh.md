# sanoTTS 串口离线语音合成系统 (TUYA_T5AI_CORE)

本项目基于涂鸦智能 TuyaOpen 物联网操作系统与 sanoTTS 神经语音合成技术，在 **TUYA_T5AI_CORE**（Beken BK7258 双核 Cortex-M33F，8MB Flash，16MB PSRAM）硬件上实现了**纯离线、零依赖、端到端**的本地中英文语音合成。

电脑仅作为串口终端工具；运行完全脱离电脑提供音素/PCM、脱离预录音频，且不依赖任何云端 TTS 或网络服务。本功能为纯本地语音合成朗读，无需联网、配网、PID、UUID 或云端授权。

---

## 一、系统架构与设计指标

```mermaid
flowchart LR
    A["串口输入 (UTF-8)"] --> B["文本规范化 (snt_text)"]
    B --> C["中文音素转换 G2P (espeak-ng cmn)"]
    C --> D["时长预测 (DurationStudent)"]
    D --> E["潜变量预测 (LatentStudent)"]
    E --> F["分块神经声码器 (Piperlite FP32)"]
    F --> G["重采样 (22050Hz -> 16000Hz)"]
    G --> H["板载音频驱动 / PA 喇叭发声"]
```

### 阶段验收判定总览

| 阶段 | 目标与门禁标准 | 实测结果 | 最终判定 |
|---|---|---|---|
| **G0** | 8MB Flash 与 16MB PSRAM 重新布局与峰值预算核算 | Flash 7.75MB / 8MB，PSRAM 驻留 7.23MB + 推理 3.5MB | **PASS** |
| **M0** | 串口协议契约、16 kHz 测试音、STOP 与排空恢复 | 驱动环形缓冲管理、排空判定精准、连续50次稳定 | **PASS** |
| **M1** | 英文 e12nano 板端运行与黄金门禁验证 | `corr = 0.984263` (>0.980), `rms_ratio = 0.998726` | **PASS** |
| **M2** | 中文分块解码 (16 中心帧 + 16 上下文帧) | `corr = 0.999997` (>=0.995), `rms_ratio = 1.000000` | **PASS** |
| **M3** | **必达功能目标**：掉电冷启动独立运行，30 句标准评测集 + 动态文本 + 端到端 SAY 喇叭发音 + 100 轮循环稳定 | 30/30 句 G2P 音素 100% Bit-Exact，100/100 轮浸泡零异常，喇叭清晰发声 | **PASS** |
| **M4** | **性能目标**：compute-only RTF p95 <= 0.8，首包延迟 <= 1.5s | Cortex-M33 240MHz 纯标量 C 下 Piperlite 实测 RTF ~210，首包延迟 ~36s | **未通过（交付受限离线播报）** |

> [!NOTE]
> **性能限制声明**：在 Cortex-M33 @ 240 MHz 单核纯标量 C 编译环境下，由于 Piperlite 深度卷积与多残差网络计算密集度高，目前支持作为**离线生成/分段语音播报**使用。后续若需达到实时（RTF < 1.0），可引入 CMSIS-DSP 汇编向量化、int8 量化声码器或双核流水线。

---

## 二、物理资源布局与分配清单

根据用户授权，已对全部 8MB Flash 与 16MB PSRAM 进行项目专用重划分：

### 1. 8 MB Flash 分区布局 (0x00000000 - 0x00800000)

| 偏移地址 | 大小 | 分区名称 | 存放内容与说明 |
|---|---|---|---|
| `0x00000000` | 68 KB | `primary_bootloader` | 一级引导程序 |
| `0x00011000` | 68 KB | `primary_tuyaboot` | 涂鸦二级 Bootloader |
| `0x00022000` | 1088 KB | `primary_cp_app` | CP 核控制固件 |
| `0x00132000` | 1360 KB | `primary_ap_app` | AP 核应用主固件 (`sanotts_serial`) |
| `0x00286000` | 8 KB | `usr_config` | 系统基本配置 |
| `0x00288000` | 8 KB | `ota` | 最小化保留 |
| `0x0028A000` | 5584 KB | `snt_model_zh` | **sanoTTS 中文 9 项模型/字典压缩归档镜像** (`snt_zh_assets.bin`, 5.42 MB) |
| `0x007FE000` | 4 KB | `sys_rf` | RF 射频校准数据 |
| `0x007FF000` | 4 KB | `sys_net` | 网络校准数据 |

**总占用**: 固件 2.33 MB + 模型归档 5.42 MB + 系统保留 = **7.75 MB / 8.00 MB** (利用率 96.9%，剩余空间安全)。

### 2. 16 MB PSRAM 内存分配 (0x60000000 - 0x61000000)

| 内存区域 | 起始地址 | 空间大小 | 用途说明 |
|---|---|---|---|
| 用户/音频缓冲 | `0x60000000` | 200 KB | 音频 DMA 环形缓冲区、协议临时包 |
| CP/AP 栈与专用堆 | `0x60032000` | 272 KB | 系统任务栈、FreeRTOS 系统堆 |
| AP 扩展大堆 | `0x60076000` | 14.41 MB | **全局核心工作区**： |
| - 中文常驻资产 | - | 7.23 MB | 9 项模型/字典解压后 PSRAM 零拷贝常驻 |
| - sanoTTS 推理 Arena | - | 3.50 MB | 声学潜变量与 Piperlite 块解码对齐 Arena |
| - G2P 运行时与系统堆 | - | ~1.50 MB | espeak-ng 词典哈希、分词栈帧 |
| - 连续空闲安全余量 | - | > 2.00 MB | 保证高负载下零内存碎片与零 OOM 风险 |

---

## 三、模型与资产校验清单

所有模型与字库均严格固定上游 Commit `f73b8382bc98a1c8de3488ba30d727ab82587828`：

| 文件名称 | 原始大小 | 压缩大小 | SHA-256 校验和 | 授权许可 |
|---|---|---|---|---|
| `duration_student.bin` | 1,489,648 B | 1,228,849 B | `0fa020ddf51a70513904a080838b0be1477aa2f8fe539b9dfa3e9c403362a26c` | MIT |
| `latent_student.bin` | 1,189,840 B | 988,963 B | `2463f8d38bfbe1fe9b71e16f73359d9e7ee4aeb9242d99d45e45a278d655f4c4` | MIT |
| `piperlite_fp32.bin` | 1,884,976 B | 1,607,453 B | `3f2bb4a8d8e5730424578ef2825fcb797864380f9bb46756f7ef0bf585098ff9` | MIT |
| `cmn_dict` | 1,634,804 B | 1,496,521 B | `335ef00086ebc8bb12658a5b3f27db3ebcff3828c464c8c7cf083161ef90c9b6` | GPL-3.0 |
| `phontab` | 272,392 B | 114,242 B | `b63a2380f2d6b38c2084c8a2fe2fb3b3fd8187889dbecb2d2f4d6d67b2fc8b8a` | GPL-3.0 |
| `phonindex` | 13,016 B | 6,758 B | `9ff156aee05dcfd2c12ba12643a6d45f3bbd1ff04c107bfcfc9c7e09968a35ee` | GPL-3.0 |
| `phondata` | 68,096 B | 43,405 B | `1daaa3f3d7950b33b0e12918e95738cb3c1bdf77eb372a6a6839aa64b8bb26ec` | GPL-3.0 |
| `intonations` | 76,048 B | 18,349 B | `a9f5d378ee0c644d6da61ae96a3fc5b5fb8ea346ec2b9db3078ee79a4073e51f` | GPL-3.0 |
| `zh_list` | 948,088 B | 176,966 B | `fa1fead1ee3e2c69ea4b4ecfba3bb135f299f0b58ea09d6f83ea0e0f9b362624` | GPL-3.0 |
| **`snt_zh_assets.bin`** | - | **5,681,608 B** | `0ad010a6d6f717d9dc8851c28de702ea5122d82a430adfc8a4349d8ee11aade7` | 混合 |

---

## 四、从干净仓库完整复现流程

### 1. 检出分支与环境初始化

```bash
cd /home/share/samba/tyopen/TuyaOpen
git checkout md/t5-sanotts
. ./export.sh
tos.py check
```

### 2. 应用平台分区补丁

平台属于独立子模块仓库，需应用 Flash 重划分补丁：

```bash
git -C platform/T5AI apply /home/share/samba/tyopen/TuyaOpen/examples/multimedia/sanotts_serial/platform_t5ai.patch
```

### 3. 打包与烧录中文模型镜像

模型文件打包由专用脚本完成并生成归档 `snt_zh_assets.bin`：

```bash
cd /home/share/samba/tyopen/TuyaOpen/examples/multimedia/sanotts_serial
python3 tools/pack_zh_assets.py
```

使用 `bk_loader` 烧录模型镜像至 Flash 物理地址 `0x0028A000`：

```bash
# 目标板串口 ACM0
python3 /home/share/samba/tyopen/TuyaOpen/platform/T5AI/t5_os/tools/env_tools/beken_packager/bk_loader.py \
    -p /dev/ttyACM0 -b 921600 \
    write_flash -s 0x28a000 models/chinese/snt_zh_assets.bin
```

### 4. 编译与烧录应用固件

```bash
cd /home/share/samba/tyopen/TuyaOpen/examples/multimedia/sanotts_serial
tos.py config set BOARD_CHOICE_T5AI=y BOARD_CHOICE_TUYA_T5AI_CORE=y
tos.py build
```

编译成功后，产物固件位于：
`dist/sanotts_serial_1.0.0/sanotts_serial_QIO_1.0.0.bin` (SHA-256: `acaa37c51ebd6e7744694d314e24d2636c7a12e2206adc92c9bc6ad5609e5b99`)

烧录固件：
```bash
python3 /home/share/samba/tyopen/TuyaOpen/platform/T5AI/t5_os/tools/env_tools/beken_packager/bk_loader.py \
    -p /dev/ttyACM0 -b 921600 \
    write_flash -s 0x0 dist/sanotts_serial_1.0.0/sanotts_serial_QIO_1.0.0.bin
```

---

## 五、串口通信协议与使用指南

### 1. 硬件引脚与串口说明
- **应用协议串口**: `/dev/ttyACM0` (115200 8N1)。所有控制指令与响应在此口收发。
- **系统调试串口**: `/dev/ttyACM1` (460800 8N1)。输出 FreeRTOS 启动及驱动调试日志。

### 2. 通信格式
- **发送格式**: `<VERB>\t<REQ_ID>[\t<PAYLOAD>]\n`
- **接收格式**: `@SNT\t{"id":<REQ_ID>,"event":"<EVENT>",...}\n`

### 3. 指令说明
- `HELLO\t<id>\n`: 查询固件版本、声部与限制。
- `SAY\t<id>\t<UTF-8中文文本>\n`: 发起完整文字规范化、G2P、神经合成与扬声器播报。
- `G2P\t<id>\t<UTF-8中文文本>\n`: 仅在板端进行文字规范化并输出音素 ID 列表。
- `GOLDEN\t<id>\t[m1|440]\n`: 运行 M1 英文 nano 黄金门禁或 16kHz 纯音测试。
- `STOP\t<id>\n`: 瞬时中止当前正在进行的计算或播放，排空音频，立即接纳新请求。

---

## 六、一键自动化全套件验收

运行统一端到端自动化验收脚本：

```bash
python3 examples/multimedia/sanotts_serial/tools/test_m3_suite.py
```
或通过通用 smoke 工具调用：
```bash
python3 examples/multimedia/sanotts_serial/tools/serial_smoke.py --port /dev/ttyACM0 --suite acceptance
```

脚本将自动执行：
1. **冷启动与版本自适应检查** (`HELLO`)
2. **30 句标准评测集 Bit-Exact 验证**（覆盖数字、时间、日期、多音字、长文本）
3. **现场动态新文本验证**
4. **端到端 SAY 实机推理与喇叭发声**（"你好" 7680 采样点，0 欠载）
5. **协议异常边界与鲁棒性套件**（空文本、非法指令、无TAB、重复ID、超长行、队列满）
6. **STOP 瞬时取消与无缝接续恢复**
7. **100 轮连续循环浸泡与稳定性测试**

详细测试数据与结果请参见验收报告：[`reports/acceptance.md`](../../reports/acceptance.md) 与 [`reports/m3-hardware-eval.md`](reports/m3-hardware-eval.md)。
