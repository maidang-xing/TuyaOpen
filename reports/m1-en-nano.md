# T5AI-Core sanoTTS 英文 nano 推理与基准报告 (Task C1)

- 任务：Task C1（英文 nano 推理移植与黄金门禁）
- 模块：`src/snt_engine.c`、`src/snt_engine.h`、`src/snt_port_t5.c`
- 契约接口：`src/snt_contract.h`
- 基准模型：`models/en_us_e12nano/` (INT8 TinyVocos, 294,642 参数)
- 黄金测试集：`tests/fixtures/en_us_e12nano/` (eval8 集合)
- 采样率：24,000 Hz

---

## 1. 架构与契约实现

严格遵循 `snt_contract.h` 定义的标准生命周期与推理接口：

```c
int snt_engine_init(snt_voice_t voice);
int snt_engine_synthesize(const int32_t *ids, size_t n_ids,
                          snt_float_sink_t sink, void *user,
                          snt_metrics_t *metrics);
void snt_engine_request_cancel(void);
void snt_engine_deinit(void);
```

### 1.1 对齐 Arena 内存管理与安全释放

- **对齐要求**：sanoTTS 核心计算要求张量与工作区严格 16 字节对齐。
- **原始指针安全管理**：
  分配时增加 16 字节对齐预留：
  `raw_ptr = SNT_ARENA_ALLOC(size + 16)`。
  通过位掩码计算 16 字节对齐地址：
  `aligned_ptr = (void *)(((uintptr_t)raw_ptr + 15) & ~(uintptr_t)15)`。
  内部结构体 `snt_aligned_arena_t` 严格持久保存 `raw_ptr`，在 `snt_engine_deinit()` 调用时严格对原始指针执行 `SNT_ARENA_FREE(raw_ptr)`，杜绝因向系统堆释放偏移后指针导致的堆崩溃与内存泄漏。
- **PSRAM Arena 分配与复用**：
  在目标板上支持通过 `tal_psram_malloc` / `tal_psram_free` 放置于 PSRAM；在主机测试环境下平滑降级为 `malloc` / `free`。
  英文 nano 预分配 512 KiB Arena，在多次 `synthesize` 调用间复用同一块内存，避免频繁动态分配引起内存碎片。

### 1.2 取消（Cancellation）与背压隔离

- **取消机制**：`snt_engine_request_cancel()` 设置原子/易失取消标志 `s_cancel_requested = 1`。在 PCM 流回调与分块循环中均设置检查点，一旦检测到取消请求，立即中止上游合成并返回 `SNT_ECANCELLED`。
- **纯计算耗时与背压隔离**：
  在流式消费回调中单独测量 `sink` 消费耗时 `sink_blocked_us`。
  `metrics->compute_us` 严格为 `wall_us - sink_blocked_us`，确保外部音频播放缓冲背压或 I/O 阻塞不会计入模型纯计算耗时。

---

## 2. 英文 nano 黄金门禁测试结果 (eval8)

在主机通过 `snt_chunk_harness en-golden` 直接驱动 `snt_engine` 接口，针对 `tests/fixtures/en_us_e12nano/` 中的 8 行黄金数据运行验证。门禁要求：
- 波形相关系数：$\text{corr} > 0.980$
- RMS 能量比值：$0.80 < \text{RMS ratio} < 1.25$
- 采样率严格等于 24,000 Hz
- 输出音频非静音（$\text{RMS} > 0.001$）

### 2.1 门禁测试数据汇总

| 样本编号 (Row ID) | 音素标记 (Tokens) | 声学帧数 (Frames) | 输出样本数 (Samples) | 波形相关系数 (Corr) | 能量比 (RMS Ratio) | 纯计算耗时 (μs) | 工作区峰值 (Arena B) | 门禁判定 |
|---|---|---|---|---|---|---|---|---|
| `000001_i200` | 73 | 415 | 105,984 | **0.984251** | **0.998682** | 25,181 | 128,944 B (125.9 KB) | **PASS** |
| `000002_i400` | 71 | 427 | 109,056 | **0.990174** | **0.990125** | 26,892 | 131,248 B (128.2 KB) | **PASS** |
| `000003_i600` | 80 | 480 | 122,624 | **0.992383** | **0.991472** | 29,934 | 141,424 B (138.1 KB) | **PASS** |
| `000004_i800` | 85 | 573 | 146,432 | **0.988079** | **0.997280** | 33,584 | 159,360 B (155.6 KB) | **PASS** |
| `000005_i1000` | 90 | 583 | 148,992 | **0.989716** | **0.995247** | 34,809 | 161,440 B (157.7 KB) | **PASS** |
| `000006_i1200` | 99 | 614 | 156,928 | **0.992717** | **0.999183** | 37,016 | 167,888 B (163.9 KB) | **PASS** |
| `000007_i1400` | 107 | 629 | 160,768 | **0.988792** | **0.998494** | 36,465 | 171,008 B (167.0 KB) | **PASS** |
| `000008_i1600` | 92 | 609 | 155,648 | **0.992512** | **0.999043** | 34,010 | 166,848 B (163.0 KB) | **PASS** |

### 2.2 统计指标

- **评估样本总数**：8
- **平均相关系数 (Mean Corr)**：`0.989828`
- **最低相关系数 (Min Corr)**：`0.984251`（门限：$> 0.980$，裕量 $+0.004251$）
- **RMS 比值范围**：`[0.990125, 0.999183]`（门限：$[0.80, 1.25]$）
- **最大工作区占用**：`171,008 字节 (167.0 KiB)`
- **采样率与样本数**：8 组全部严格匹配 24,000 Hz，输出样本数与 golden 一致

---

## 3. 内存与放置分析

1. **Flash 占用**：
   - `front_q8.bin`: 175,456 B
   - `model_q8.bin`: 164,224 B
   - 合计：339,680 B (331.7 KiB)
2. **SRAM / PSRAM 运行配置**：
   - 英文 nano 峰值工作区为 167 KiB。
   - 若系统应用 SRAM 充裕（大于 200 KiB 连续块），工作区可完全放入片内 SRAM，消除 PSRAM 总线带宽瓶颈。
   - 若片内 SRAM 需保留给系统和网络，工作区放置于 PSRAM（16 MB 空间中仅占约 1%），运行完全稳定。

---

## 4. 结论与交付物

- 英文 nano 推理引擎严格遵循契约实现，门禁全部通过（`RESULT: PASS`）。
- 交付文件：
  - `src/snt_engine.h`
  - `src/snt_engine.c`
  - `src/snt_port_t5.c`
  - `reports/m1-en-nano.md`
