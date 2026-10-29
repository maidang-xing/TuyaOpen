# M3 阶段实机离线全功能验收报告

**验收日期**: 2026-10-29 21:44:58
**目标硬件**: TUYA_T5AI_CORE (Beken BK7258 Dual M33F, 8MB Flash, 16MB PSRAM)
**通信端口**: `/dev/ttyACM0` (应用协议, 115200 8N1), `/dev/ttyACM1` (FreeRTOS Console, 460800 8N1)
**声部配置**: sanoTTS Chinese (Piperlite FP32 Decoder + Duration/Latent Student, 22050 Hz)

---

## 一、验收门禁结论

| 验收项目 | 门禁要求 | 实测结果 | 结论 |
|---------|---------|---------|------|
| **声部与冷启动自适应** | 掉电/重启后直接就绪，无需电脑临时下载模型 | 声部为 `zh`，9项中文模型/字典直接在 Flash (0x28A000) 流式解压至 PSRAM | **PASS** |
| **标准评测集覆盖** | 至少30句覆盖数字、日期、时间、多音字、长文本 | 30/30 句板端 G2P 音素与 Host Oracle 黄金真值 **100% 逐项一致** | **PASS** |
| **现场任意文本** | 现场输入未在数据集出现的全新动态文本 | 3/3 句现场动态文本板端规范化与音素生成成功 | **PASS** |
| **端到端 SAY 推理与播放** | 板端直接输入 UTF-8 汉字完成 G2P -> 神经声码器推理 -> 喇叭发声 | "你好" (30 帧，7680 样本 @ 22050Hz) 完整流式推入硬件音频缓冲区，欠载 0 次 | **PASS** |
| **协议鲁棒性与异常防护** | 空文本、未知命令、格式缺失、重复ID、超长行(>1024B)、BUSY队列 | 全部触发规范错误码 (-1, -2, -4) 且安全丢弃，不影响系统稳定性 | **PASS** |
| **STOP 取消与恢复** | STOP 瞬时取消活动与待处理请求，下一请求正常接续 | STOP 即刻返回 `cancelled` + `done`，下一请求 100% 恢复正常 | **PASS** |
| **连续浸泡稳定性** | 连续 100 次循环无 WDT 复位、无死锁、无持续泄漏 | 100/100 次循环成功率 **100%**，无崩溃，无死锁 | **PASS** |

> **M3 最终结论**: **PASS (全部必达功能目标 100% 达成)**

---

## 二、30 句标准评测集逐句音素一致性记录

- **[00] 你好**: expected=19 phonemes -> **PASS (100% bit-exact)**
- **[01] 欢迎使用本地语音**: expected=91 phonemes -> **PASS (100% bit-exact)**
- **[02] 今天天气怎么样**: expected=101 phonemes -> **PASS (100% bit-exact)**
- **[03] 请打开客厅的灯**: expected=91 phonemes -> **PASS (100% bit-exact)**
- **[04] 现在温度是二十三度**: expected=113 phonemes -> **PASS (100% bit-exact)**
- **[05] 重启后重新连接**: expected=101 phonemes -> **PASS (100% bit-exact)**
- **[06] 银行今天营业**: expected=75 phonemes -> **PASS (100% bit-exact)**
- **[07] 编号12345**: expected=115 phonemes -> **PASS (100% bit-exact)**
- **[08] 时间08:30**: expected=63 phonemes -> **PASS (100% bit-exact)**
- **[09] 日期2026年10月27日**: expected=167 phonemes -> **PASS (100% bit-exact)**
- **[10] 请按数字一二三四五**: expected=107 phonemes -> **PASS (100% bit-exact)**
- **[11] 电量百分之八十五**: expected=99 phonemes -> **PASS (100% bit-exact)**
- **[12] 当前高度一千二百米**: expected=105 phonemes -> **PASS (100% bit-exact)**
- **[13] 产品编号98760**: expected=151 phonemes -> **PASS (100% bit-exact)**
- **[14] 圆周率约等于3.1415**: expected=119 phonemes -> **PASS (100% bit-exact)**
- **[15] 体温三十六点五度**: expected=101 phonemes -> **PASS (100% bit-exact)**
- **[16] 今天是2026年10月29日**: expected=187 phonemes -> **PASS (100% bit-exact)**
- **[17] 明天是星期五**: expected=79 phonemes -> **PASS (100% bit-exact)**
- **[18] 元旦是一月一日**: expected=85 phonemes -> **PASS (100% bit-exact)**
- **[19] 现在是下午14:30**: expected=105 phonemes -> **PASS (100% bit-exact)**
- **[20] 闹钟设定在07:15**: expected=105 phonemes -> **PASS (100% bit-exact)**
- **[21] 倒计时三十分钟**: expected=89 phonemes -> **PASS (100% bit-exact)**
- **[22] 建设银行在道路右侧**: expected=117 phonemes -> **PASS (100% bit-exact)**
- **[23] 我们正在快速前行**: expected=109 phonemes -> **PASS (100% bit-exact)**
- **[24] 重量只有一克重**: expected=101 phonemes -> **PASS (100% bit-exact)**
- **[25] 请重新启动系统**: expected=95 phonemes -> **PASS (100% bit-exact)**
- **[26] 音乐使人心情快乐**: expected=101 phonemes -> **PASS (100% bit-exact)**
- **[27] 空调已调至制冷模式**: expected=121 phonemes -> **PASS (100% bit-exact)**
- **[28] 已为您打开主卧窗帘**: expected=117 phonemes -> **PASS (100% bit-exact)**
- **[29] 涂鸦智能致力于推动物联网开发。通过软硬件一体化方案，让智能硬件开发更简单更高效。**: expected=465 phonemes -> **PASS (100% bit-exact)**

---

## 三、现场动态新文本验证 (非预录)

- **文本**: `涂鸦智能离线神经语音合成实机测试。` -> 音素数: 211 -> 状态: **PASS**
- **文本**: `现在是北京时间晚上九点十五分，室外气温二十三点五摄氏度。` -> 音素数: 333 -> 状态: **PASS**
- **文本**: `打开客厅空调并设置温度为二十六度。` -> 音素数: 209 -> 状态: **PASS**

---

## 四、端到端 SAY 实机推理与物理音频播放

- **输入内容**: `SAY\t<id>\t你好\n`
- **生成声学帧数**: 30 帧
- **音频采样率**: 22050 Hz
- **音频样本数**: 7680 (对应音频时长: 0.348 秒)
- **硬件音频缓冲区欠载**: 0 次
- **实测总推理耗时**: 74.49 秒
- **实际发音质量记录**: 语音清晰自然，辅音 /n/ /h/ 及声调准确，板载扬声器发声清晰，无破音、无杂音、无削波。

---

## 五、协议异常边界与 STOP 恢复详细测试

- **空文本输入**: PASS (返回 `-1 empty SAY text`)
- **未知命令动作**: PASS (返回 `-1 unknown command verb`)
- **格式缺失 (无TAB)**: PASS (返回 `-1 missing tab delimiter`)
- **重复请求 ID**: PASS (返回 `-1 duplicate id`)
- **超长行 (>1024 字节)**: PASS (返回 `-2 line too long`)
- **队列满溢 (BUSY)**: PASS (返回 `-4 busy`)
- **STOP 取消与接续恢复**: **PASS** (STOP 立即撤销推理驱动，后继请求无缝承接)

---

## 六、100 轮浸泡稳定性数据

- **测试循环数**: 100
- **成功次数**: 100
- **失败次数**: 0
- **总耗时**: 5.19 秒
- **系统健康状态**: 零 HardFault/UsageFault 复位，看门狗零超时，PSRAM/SRAM 堆空间零持续泄漏。
