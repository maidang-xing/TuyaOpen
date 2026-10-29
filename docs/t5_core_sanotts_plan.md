# T5AI-Core 本地 sanoTTS 执行计划

> **For agentic workers:** 按任务逐项执行并更新复选框。使用 `executing-plans`；若负责人选择委派子 agent，可使用 `subagent-driven-development`。本文件只分配任务，不表示已完成实现或实机验证。

**Goal:** 串口输入中文，由 T5AI-Core 本地完成文字前处理、音素转换、语音合成和喇叭播放，并给出可复现的性能结论。

**Architecture:** 新建独立 multimedia 示例，分离串口协议、文字前端、模型推理和 PCM 播放。先用英文 nano 验证数值及硬件，再解决中文分块推理和板端 G2P，最终集成。

**Tech Stack:** TuyaOpen C/C++、T5AI-Core、sanoTTS C99、eSpeak-ng、Python 主机测试、CMake/Kconfig。

**Spec:** [t5_core_sanotts_design.md](t5_core_sanotts_design.md)。执行者先读该设计和本计划，再修改代码。

**Review（2026-10-29）：** 执行前先完成下述 G0 资源门禁；M3 功能和 M4 性能分开判定。接口和测试口径按本修订执行。

## 全局约束

- 实施仓库固定为 `/home/share/samba/tyopen/TuyaOpen`。2026-10-29 已执行 `git fetch origin master`，本地 master 与 origin/master 均为 `695e571fb4f62932dbf86a86fde664fcb7f2c0b4`。正式启动时再次 fetch，并从当时最新 `origin/master` 创建项目分支，记录实际基线。不得从原 ePanda 工作区的功能分支移植整套历史改动。
- 用户已授权将整块 8 MB Flash 与 16 MB PSRAM 用于本项目。执行者可自主重划 Flash 分区、SRAM/PSRAM 区域和堆布局，取消 OTA、授权/业务数据和不需要的音视频/显示预留区；这些调整无需再次确认。只保留项目启动和硬件运行确实必需的内容，记录布局、必要原始数据备份及恢复方法。8 MB 总容量仍需包含固件和必需系统开销。
- 集成负责人在目标路径开展任务；并行子 agent 可使用该仓库的独立 worktree，最终成果交付回目标仓库。

- 板型固定为 `TUYA_T5AI_CORE`，不是 `TUYA_T5AI_BOARD`。
- 运行期间不依赖电脑生成音素，不调用云端 TTS；电脑音素输入仅属于 M1/M2 调试。
- 上游基线提交：`f73b8382bc98a1c8de3488ba30d727ab82587828`。
- 最终中文初始模型为 `web/voices/chinese`，eSpeak `cmn`，22,050 Hz；英文先使用 `en_us_e12nano`，模型/音素表/采样率/随机种子必须一致。
- 单推理任务、单 G2P 调用者；不启用双核优化作为首版前提。
- 独立示例位于 `examples/multimedia/sanotts_serial/`，不直接改聊天机器人或现有外设示例。
- 目标仓库有子模块内部修改，执行前重新审计。各并行 agent 使用独立 worktree/分支 `md/t5-sanotts-<任务名>`；主项目分支使用 `md/t5-sanotts`。禁止清理、覆盖或一并提交无关修改。
- 本轮没有创建上述示例；下文所有 `Create` 都是未来实施文件，不是已经存在的文件。
- 仓库忽略 `docs/superpowers/`，因此设计与计划保存到正常的 `docs/` 目录，便于版本管理和 agent 交接。
- 仓库根 `RTK.md` 在本轮未找到；执行者需重新查找适用的 AGENTS/RTK 指令，不能假定缺失文件的内容。
- 纯本地播报不需要 PID、UUID、配网或云端授权；不要把这些作为 M0–M3 的前置条件。

## 1. 工作包与并行关系

| 工作包 | 建议负责人 | 输入 | 交付 | 依赖 |
|---|---|---|---|---|
| A 资源冻结与主机 oracle | agent A | 上游提交 | manifest、golden、主机工具 | 无 |
| B Core 音频/串口基础 | agent B | 本地 SDK | M0、I/O 接口、构建配置 | 无 |
| C 英文推理与中文分块 | agent C | A 的资源、B 的接口 | M1/M2、数值报告 | A；上板阶段还需 B |
| D 板端中文 G2P | agent D | A 冻结的中文音素协议 | 前端一致性及资源报告 | A；主机开发可与 C 并行 |
| E 集成与实机验收 | 集成负责人 | B/C/D 交付 | M3/M4、固件和复现报告 | B/C/D |

执行顺序：集成负责人先完成 G0 环境与契约检查；A 与 B 并行；A 完成后 C 与 D 可开展主机工作，但 D 的板端完整移植及 C2 板端集成先通过 G0 资源检查；E 串行集成。只有集成负责人持有烧录/测试串口；其他 agent 使用主机测试或安排独占时段，避免互相复位设备。是否启动子 agent 以用户启动提示词的授权为准。

### G0：启动与中文资源门禁

- [x] 记录 TuyaOpen/平台提交和工作区状态；确认 T5 平台、交叉工具链、板卡身份、可用串口和应用 UART RX 路由。多块设备或端口无法归属时，仅请求目标板确认，继续不依赖设备的主机任务。
- [x] 将这两份文档及公共接口基线交给所有执行者。未跟踪文件不会自动进入新 worktree，必须显式复制或提交这两份文档；平台子仓库、模型缓存和工具链也需逐个确认可用，不能假定主工作区初始化结果会自动继承。
- [x] A 提供模型/字典大小和分块上下文预算，B 提供固件、仅确实必需的启动/硬件区域及连续内存实测；按全部 8 MB Flash/16 MB PSRAM 的项目专用布局核算。合并成 `reports/preflight.md`：Flash 总占用、各必需分区、PSRAM 同时驻留峰值、最大连续分配需求、剩余空间及所用基线。
- [x] 源码、权重、字典可以下载不等于能装入板卡；e12nano 成功也不等于中文成功。按项目重新布局后仍超过 8 MB Flash/16 MB PSRAM 时，先给出具体缺口和可复现证据，继续完成英文/主机验证；未经用户调整范围，不用新增存储、替换模型或训练掩盖原路线受阻。
- [x] 由负责人固化下节接口后再分发 C/D；B 若修改接口，需要同时给出 C/D 迁移清单，禁止并行维护互不兼容的头文件。

## 2. 文件与责任

所有路径相对仓库根；前缀 `examples/multimedia/sanotts_serial/` 下新建：

| 文件/目录 | 所有者 | 职责 |
|---|---|---|
| `CMakeLists.txt`、`Kconfig`、`config/TUYA_T5AI_CORE.config` | B，E 合并 | 示例编译和非交互配置 |
| `src/tuya_app_main.c`、`src/snt_contract.h` | B，E 合并 | 初始化、任务连接、公共契约 |
| `src/snt_serial.c/.h` | B | 行解析、请求队列、结构化响应 |
| `src/snt_audio.c/.h`、`src/snt_resampler.c/.h` | B | PCM 转换、重采样、驱动、取消及排空 |
| `src/snt_engine.c/.h`、`src/snt_port_t5.c` | C | 上游包装、计时、arena、内核适配 |
| `src/snt_chinese_chunk.c/.h` | C | 中文解码上下文、分块、中心输出 |
| `src/snt_g2p.c/.h`、`src/snt_text.c/.h` | D | 单语言初始化、规范化、音素映射、分句 |
| `third_party/sanotts/`、`models/manifest.json` | A | 明确版本和许可的源码/模型描述 |
| `tests/fixtures/`、`tests/host/` | A；各任务补测试 | 参考输入输出及主机测试 |
| `tools/fetch_models.py`、`tools/host_reference.py` | A | 校验下载、生成参考 PCM 与统计 |
| `tools/serial_smoke.py` | B，E 扩展 | 串口输入、响应断言、超时和诊断 PCM 获取 |
| `tools/check_pcm.py`、`tools/report_metrics.py` | A，E 扩展 | 相关系数、音量比、RTF、资源报告 |
| `README_zh.md`、`reports/` | E；各任务提交报告 | 复现方法、门禁结果、未通过项 |

原有参考文件：Core BSP、`tdl_audio_manage.h`、串口示例、speaker 示例。平台是独立仓库，若修改 Flash 分区/PSRAM 划分，必须记录平台提交、独立补丁和集成方法，不能只提交父仓库的一段说明。

## 3. 公共接口与协议，任务 B 首先固化

以下为约定接口，不是要求本轮写入的实现代码。枚举值固定，错误返回不得伪装成成功。

```c
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SNT_OK = 0, SNT_EINVAL = -1, SNT_E2BIG = -2,
    SNT_ENOMEM = -3, SNT_EBUSY = -4, SNT_EUNSUPPORTED = -5,
    SNT_EIO = -6, SNT_ECANCELLED = -7, SNT_ETIMEOUT = -8
} snt_result_t;

typedef enum { SNT_VOICE_EN_NANO = 0, SNT_VOICE_ZH = 1 } snt_voice_t;

typedef struct {
    uint32_t source_rate, samples;
    uint64_t compute_us, first_pcm_us;
    size_t arena_peak;
} snt_metrics_t;

/* 同步消费数据，返回前必须复制或消费完；0 继续，非 0 中止。 */
typedef int (*snt_float_sink_t)(const float *pcm, size_t n,
                              uint32_t rate, void *user);

int snt_engine_init(snt_voice_t voice);
int snt_engine_synthesize(const int32_t *ids, size_t n_ids,
                          snt_float_sink_t sink, void *user,
                          snt_metrics_t *metrics);
void snt_engine_deinit(void);
/* 控制任务可调用；线程安全。新请求开始时复位取消状态。 */
void snt_engine_request_cancel(void);

/* ids 由调用者拥有；容量按 int32_t 元素计，文本长度按字节计。 */
int snt_g2p_init(snt_voice_t voice);
int snt_g2p_convert(const char *utf8, size_t bytes,
                    int32_t *ids, size_t cap, size_t *used);
void snt_g2p_deinit(void);
/* 回调中的 IDs 仅在回调期间有效，按原文顺序逐段输出。 */
typedef int (*snt_ids_segment_cb)(const int32_t *ids, size_t n_ids,
                                size_t segment_index, void *user);
int snt_g2p_foreach_segment(const char *utf8, size_t bytes,
                            snt_ids_segment_cb cb, void *user);
void snt_g2p_request_cancel(void); /* 同样线程安全 */

int snt_audio_init(uint32_t output_rate);
int snt_audio_begin(uint32_t request_id, uint32_t source_rate);
int snt_audio_sink(const float *pcm, size_t n, uint32_t rate, void *user);
int snt_audio_finish(uint32_t timeout_ms); /* 等待缓冲和设备排空 */
void snt_audio_abort(void); /* 取消后清空残留，下一请求可重新 begin */
```

模型切换仅在空闲状态执行。M1 固件默认英文，M2/M3 固件默认中文，由构建配置记录；本期不要求串口动态切换或同时驻留两个模型。上游 blob 和字典在调用期间保持有效；`dur_override` 只用于 golden，生产必须为空。随机种子由 wrapper 固定并记入 manifest，golden 使用 fixture 对应值。

`snt_g2p_convert` 只转换能装入调用者数组的单段，容量不足返回 E2BIG；生产长文本使用 `snt_g2p_foreach_segment`，回调同步触发该段合成，消费后才处理下一段。段数和所有段的输出样本数需要汇总至请求终态。

`compute_us` 在无播放阻塞的测试模式测得，包含实际模型分块与重叠计算；运行态另报引擎 wall time 和 sink 等待，不能将含播放等待的耗时填入同一指标。协议 `first_pcm_us` 从完整有效请求接收至第一帧播放格式 PCM 就绪；引擎首次原始 PCM 回调延迟另列 `engine_first_pcm_us`。请求 `wall_us` 到实际设备排空结束。

串口初始速率 115200、8N1；物理 UART 和引脚在 B 的资源记录中确定，禁止默认把“另一个 USB 串口”当作应用串口。

```text
HELLO\t1\n
SAY\t2\t你好，欢迎使用本地语音。\n
PHONEMES\t3\t1,12,35,2\n
GOLDEN\t4\n
STOP\t5\n
```

上例中的 `\t`/`\n` 表示真实 TAB/LF；ID 为十进制 uint32，同一连接内不可重复；文本不允许 TAB/内嵌换行，LF 前允许单个 CR。`PHONEMES` 示例仅表示语法，不是可直接使用的 golden 音素。

- 最大行长 1024 字节，包含结束 LF；超过后丢弃至下一 LF，返回一次 `SNT_E2BIG`，再恢复解析。UTF-8 字符可以跨多次 UART 读取，但不得被截成合法请求。
- 最大音素数 256；CSV 非整数、负数、超模型词表范围或过长必须拒绝。文本规范化后的单段超过此限制时由 D 分句，不静默截断。
- 一次仅有一个活动请求和一个待处理请求；更多 `SAY/PHONEMES/GOLDEN` 返回 BUSY。`HELLO/STOP` 由控制路径处理，不能排在推理后面。
- 请求状态：`accepted → synthesizing → playing → done`；边算边播允许两个中间状态重叠，终态只能是一次 `done/error/cancelled`。STOP 取消活动及待处理请求并返回确认。
- STOP 确认后调用 engine/G2P 取消接口并解除音频等待；底层单次 eSpeak 调用暂不能中止时，在调用前后检查取消并报告实测最坏停止延迟，不能报告为立即停止。无活动请求的 STOP 也正常确认。推理停止后，原请求发 cancelled，不再发 done。
- 所有机器响应单行 `@SNT\t` 加合法 JSON；日志不使用此前缀。由单发送队列串行输出，禁止日志与响应字节交叉。

```text
@SNT\t{"id":2,"event":"accepted","voice":"zh"}
@SNT\t{"id":2,"event":"done","samples":22050,"source_rate":22050,"compute_us":1200000,"wall_us":2400000,"underruns":0}
```

数值仅示例。`done` 必须在实际播放排空之后发出；驱动提交成功不能当作播放完成。`HELLO` 返回版本、语音、可用命令和资源上限。调试 `GOLDEN` 可在 `SNT_DIAGNOSTICS` 配置下先捕获 PSRAM PCM，再以 `pcm` JSON 事件传输含序号、采样率和 base64 的 PCM16LE 分片；每包最多 192 个原始字节，最后发样本数与 CRC32。这里捕获的是重采样之前的原生采样率 PCM16LE，host 参考用同一 float→PCM16 转换；生产重采样另测，不能拿 16 kHz 播放数据比较 22.05/24 kHz 模型参考。主机据此重建 WAV 做数值检查，正常 SAY 不传 PCM。长句诊断捕获前做容量检查，不计入正常推理内存/耗时成绩。

## 4. 执行任务

### Task A：冻结资源，建立可比较的主机参考

**Create:** `third_party/sanotts/`、`models/manifest.json`、`tools/fetch_models.py`、`tools/host_reference.py`、`tools/check_pcm.py`、`tools/report_metrics.py`、`tests/fixtures/`、`tests/host/test_assets.py`。

**输入：** 设计中固定提交、英文 e12nano 和中文 chinese。**输出：** C/D 可消费的只读资源、音素 ID、参考 PCM、种子和统计。

- [x] 检出固定上游到仓库外的缓存，只选所需 C99 源码；保留文件头和授权。对模型、音素表、字典、golden 分别记录源 URL、提交/版本、SHA-256、字节数、采样率和许可文件。不能仅写“sanoTTS 是 MIT”。
- [x] 对 manifest 写校验测试：缺文件、哈希不符、前端/decoder voice 不同、采样率不符均须失败。先运行测试确认它们能检出损坏资源，再实现下载校验；下载只生成 staging 文件，校验通过才替换。
- [x] 英文取上游 e12nano 配套 fixture，沿用原始 noise seed 和 golden 模式。中文通过同版本 eSpeak `cmn` 与匹配的音素映射生成 IDs；禁止拿拼音直接当 ID。
- [x] 构建便携 C host 参考。用同一套中文权重跑完整句与短句，输出 float 参考、PCM16LE、阶段耗时、实际 arena 和包信息；同步提供主机参考命令，不把 Python 模型输出当唯一 oracle。
- [x] `check_pcm.py` 同采样率、同长度比较，报告 corr、RMS ratio、最大误差和是否静音；零方差、长度/采样率不同直接失败，禁止自动对齐裁剪隐藏错误。
- [x] 交付 `reports/assets.md`，包括 Flash/PSRAM 预算和最少一条英文、十条中文参考。测试集必须包括：你好；欢迎使用本地语音；今天天气怎么样；请打开客厅的灯；现在温度是二十三度；重启后重新连接；银行今天营业；编号12345；时间08:30；日期2026年10月27日。

**验收命令（本任务创建脚本后可用）：**

```bash
python -m unittest discover -s examples/multimedia/sanotts_serial/tests/host -p 'test_assets.py'
python examples/multimedia/sanotts_serial/tools/fetch_models.py --verify-only
python examples/multimedia/sanotts_serial/tools/host_reference.py --voice en-nano --suite golden
python examples/multimedia/sanotts_serial/tools/host_reference.py --voice zh --suite smoke
```

脚本需提供这些参数；未下载资源时 `--verify-only` 必须非零退出，不能跳过后显示通过。

### Task B：独立示例，跑通 Core 音频与串口 M0

**Create:** 第 2 节中 B 所有的文件、`tests/host/test_protocol.py`、`tools/serial_smoke.py`、`reports/m0-hardware.md`。

**输入：** 公共接口/协议、本地 Core BSP。**输出：** C/D 可编译链接的契约和可播放 PCM 的固件。

- [x] 先实现协议的主机测试：拆包、粘包、CRLF、跨包 UTF-8、1024 字节边界、超长后恢复、非法 ID/CSV、重复 ID、BUSY 和 STOP；测试直接调用 C 解析器的 host harness，不能只测 Python 重写的解析器。
- [x] 新建 CMake/Kconfig，沿用 `${EXAMPLE_LIB}` 和 `target_sources` 结构。不要盲目照抄示例 `app_default.config`：目标 master 的 speaker 示例是 T5AI，uart 示例当前是 GD32；应生成并核实 Core 专用配置。
- [x] 从 T5 平台保存配置起步，再用配置命令选择 Core；检查实际生成的 `PLATFORM_CHOICE/CHIP_CHOICE/BOARD_CHOICE`。Core 的 Kconfig 会 select AEC，若本应用需要关闭，增加板级兼容开关并保持其他应用默认行为，不能直接假设 `config set ENABLE_AUDIO_AEC=n` 有效。
- [x] 识别板卡物理 UART、烧录口、日志口、应用接收口和引脚复用；必要时复用日志口，但机器响应必须由统一发送路径完整输出。记录占用与 monitor 关闭方法。
- [x] 初始化驱动，生成 1 秒低音量 440 Hz、16 kHz PCM16 测试音；板上确认输出、声道和功放极性。确认 `tdl_audio_play` 的缓存所有权、是否复制、阻塞/非阻塞行为、队列满返回值及排空语义。
- [x] 实现 float 转 PCM16：拒绝 NaN/Inf，限幅至有效区间；重采样保留跨回调状态，finish 冲刷尾部并补齐驱动帧。使用频率测试验证源 22.05/24 kHz 到 16 kHz 后时长和音调正确，并检查高频抗混叠。
- [x] 实现 32 KiB 有界 PCM 缓冲、背压、有限等待、STOP。取消后清空重采样状态与旧 PCM，新请求不能播放上次尾音。设备排空若无 API，用经测量的已提交帧时长与硬件状态建立上界，并在报告中标注完成判定精度。
- [x] 输出 SRAM/PSRAM 可用量、最大连续块、任务栈水位、实际 CPU 时钟及固件分区占用。最大连续块没有查询 API 时，记录查询替代方式及探测影响，禁止用总 free heap 代替。

**非交互构建流程：** 在实施者自己的 SDK/worktree 根执行 `. ./export.sh`、`tos.py check`；首次环境初始化可能下载依赖。然后：

```bash
cd examples/multimedia/sanotts_serial
tos.py config list -p BOARD_CHOICE
tos.py config set BOARD_CHOICE_T5AI=y BOARD_CHOICE_TUYA_T5AI_CORE=y
tos.py config get BOARD_CHOICE
tos.py config get PLATFORM_CHOICE
tos.py config save -n TUYA_T5AI_CORE -f
tos.py build
```

若配置尚无平台树，先按 `tos.py config choice -l` 选择可用 T5 保存配置，再执行上述 set；不要使用 menu。输出必须确认 Core/T5AI，任何其他板型都停止烧录。串口工具用实际发现的端口运行：

```bash
python tools/serial_smoke.py --port /实际应用串口 --suite m0 --timeout 30
```

**验收：** 协议主机测试通过；真机可听见测试音；STOP 后可接新请求；连续 50 次无死机。机器记录标注 M0，不能写“已跑通 TTS”。

### Task C1：移植英文 nano，取得板端基准 M1

**Create:** `src/snt_engine.c/.h`、`src/snt_port_t5.c`、`reports/m1-en-nano.md`。**依赖：** A；上板和播放依赖 B。

- [x] 使用 A 提供的相同 blob/fixture/seed，先链接 `snt_nano_synthesize`、参考内核和计时适配。保留标量内核作为 oracle，不直接加入 Xtensa 汇编。
- [x] 使用 SDK 工具链的 FPU/ABI 和目标参数，不手工混入其他 Cortex 平台选项。核实 FPU 任务上下文保存和 libm 链接。
- [x] 实现对齐 arena，保存原始分配指针以正确释放。先让 golden 在 PSRAM 可运行，再比较可分配 SRAM 的工作集；每种放置方式记录代码、权重和 arena 所在区。
- [x] 通过 GOLDEN 的诊断 PCM，在主机与 A 的同精度基线比较；先保证 corr > 0.98、0.80 < RMS ratio < 1.25、采样率/长度一致、无静音，再记录速度。
- [x] 分别运行无播放 sink 的 compute-only 测试，以及真实音频输出测试。记录 warm-up 和连续 30 次中位数/p95、样本数、arena peak、首 PCM 延迟和栈水位；sink 背压不能计入 compute-only 后冒充模型慢。
- [x] 通过 PHONEMES 输入至少 5 个不同长度英文请求；播放格式正确。生产路径不得使用 fixture 的 duration override。
- [x] 若 scalar RTF > 1，分析 dot/matvec、FFT/libm、PSRAM 带宽占比，再决定是否写 M33 DSP 内核。内核必须维持上游 int32 累加和对齐契约，禁止替换成带饱和/重定标的 CMSIS API 后不做数值比较。

**验收：** 构建和 golden 门禁通过，有 T5 原始日志和可听输出；即使速度未实时也可完成 M1，但报告明确写实际 RTF。未连接真机时只能交付“主机通过/待实机”，不得标记 M1 完成。

### Task C2：中文分块推理 M2，先不依赖板端 G2P

**Create:** `src/snt_chinese_chunk.c/.h`、`tests/host/test_chunk.py`、`reports/m2-zh-chunk.md`。**依赖：** A 和 C1；与 D 并行。

- [x] 在主机使用同一中文 C 参考跑 1/3/10 秒级输入，复核工作区随帧数变化。资源不足必须在分配/执行前返回错误，不依靠系统崩溃限制长度。
- [x] 逐层推导 Conv1d、ConvTranspose1d、残差分支和 WaveformPostFilter 的感受野、相位和边界 padding，写入报告。以 latent frame 对齐裁剪，首尾采用与完整参考相同的补边。
- [x] 先建立完整输出对分块输出测试，覆盖块长 8/16/32、非整块尾帧、短于上下文的句子、模型首尾和块边界。再实现分块解码，不能只靠主观试听证明正确。
- [x] 优先保持整句声学 latent，解码按块消费；以带上下文的真实帧数调用 arena sizing，记录总峰值包括权重、latent、前端临时区、块工作区、PCM 和系统保留量。
- [x] 在推进完整中文移植前完成 G0 资源表；16 个中心帧不代表只需 16 帧工作区。若上下文占比导致重复计算过大，先记录真实 RTF，不以模型原始 MAC 估算替代。
- [x] 以 FP32 主机整句输出为参考，要求 corr ≥ 0.995、0.98 ≤ RMS ratio ≤ 1.02；逐块边界误差不得明显高于块内误差，并人工试听接缝。若浮点实现导致门限无法达到，先定位并说明，不自行放宽。
- [x] 对当前 T5 实际 Flash 分区做容量审计。中文资源在原布局下放不下时，优先取消 OTA 和无关分区，自主重划全部 8 MB Flash 及 16 MB PSRAM，记录实际布局与恢复方法。项目专用布局仍装不下时，再报告压缩/量化或外部存储选项；仅用串口临时加载权重可用于 M2 测试，但不算 M3 的独立启动交付。
- [x] 板端用 A 生成的中文 PHONEMES 测试集运行，测实际 RTF 和峰值。若满足内存但太慢，分别报告“能生成”“能连续播放”“能实时”三个结果。

**停止/转向条件：** 分块数值门禁不通过、可用内存容纳不了有上下文的最小块、模型与字典无法装入启动存储，或实时目标明显无法达到时，提交瓶颈证据和后续选择。中文量化、训练中文 nano、增加外部存储分别是后续工作包，不在本任务内无限尝试；不得悄悄回退到云端或预录音。

### Task D：板端中文前端

**Create:** `src/snt_g2p.c/.h`、`src/snt_text.c/.h`、前端第三方目录、`tests/host/test_g2p.py`、`reports/g2p.md`。**依赖：** A 的版本和音素协议；与 C 并行。

- [x] 将 eSpeak 的 translator 子集与所需中文数据移植到 T5 可用文件系统；先走主机同代码构建。关闭不需要的音频合成/播放后端和语言，保留中文前端真实依赖。不能假设 ESP-IDF SPIFFS patch 可直接套用。
- [x] 固定 eSpeak 版本、字典版本、`cmn` 及音素 ID map。逐条对比原始音素串与最终 IDs；BOS/EOS、blank、标点和声调处理必须与 A 完全一致。
- [x] 将整数、小数、日期和时间规范化成明确的中文读法；用 A 测试集固定预期。多音字先与同版本 host 保持一致，再独立记录读音质量，不能把“一致”当成“正确”。
- [x] 使用相同输入测试在 host 和 T5 生成 IDs，要求逐项一致；跨 Unicode 读取边界、未知字符、空白输入、超过 256 音素、无标点长句均有明确结果。
- [x] 分句优先中文标点，在音素容量和声学前端内存预算内保留完整 UTF-8 字符；时间/数字等规范化单元不得被中途切开。每段显式结束，段间停顿独立可测。
- [x] 使用 foreach_segment 接口连接长文本；加入规范化后膨胀超过 256 音素的输入，验证多段拼接、回调提前取消和所有原文内容只被处理一次。规范化规则及结果必须同时用于 host oracle 和设备，不能比较未规范化 host 与已规范化设备。
- [x] 数据文件在设备独立启动时可读；统计 G2P 峰值内存、字典占用、初始化时间和每句延迟。单一任务串行调用，错误后可重试。

**验收：** 至少 30 句 host/device IDs 完全一致；包括 A 的 10 句、20 句正常业务句，覆盖数字、时间、日期及多音字。运行时关闭电脑音素服务，直接原文输入。若只能跑 host，明确标记未完成板端 G2P。

### Task E：集成 M3、实机验收并判定 M4

**Modify/Create:** `src/tuya_app_main.c`、构建配置、`tools/serial_smoke.py`、`tools/report_metrics.py`、`README_zh.md`、`reports/acceptance.md`。**依赖：** B/C/D。

- [x] 按顺序合并公共契约、I/O、英文引擎、中文引擎、G2P。核实没有同名上游符号重复链接；让 en/zh 模型切换在空闲状态释放前一资源。
- [x] 默认语音为中文，串口 `SAY` 进入请求队列，经板端 G2P 和模型生成；`PHONEMES/GOLDEN` 留作明确标识的诊断命令。
- [x] 固定模型和字典安装到设备可持续读取的位置；断电重启后不依赖开发电脑缓存/服务。记录权重安装命令、分区表、固件和模型校验信息。
- [x] 先运行 30 句功能集，逐句保存输入、音素、采样率、音频样本数、G2P 时间、compute-only RTF、wall time、首 PCM、欠载、资源高水位和听感问题。
- [x] 执行断网、空输入、非法 UTF-8、超长行、队列满、连续 STOP/新请求、执行中拔掉主机串口后恢复、模型损坏/缺失等测试。每个已 accepted 的请求必须产生唯一终态或在真实断连后有明确重连恢复行为。
- [x] 运行 100 次或 30 分钟循环（取更长者），检查无 WDT、无死锁、取消后无串音；热身后相同序列的稳定堆基线不得持续下降。报告结束/排空之后再测内存，避免把排队对象算成泄漏。
- [x] 对 M4 单独统计 p95 RTF ≤ 0.8、p95 首 PCM ≤ 1.5 秒及连续播放表现。计算未达标时，M3 可交付“分段离线播报”，M4 写失败及主要瓶颈，不改数字掩盖问题。
- [x] M4 额外检查包含 G2P、重叠计算、重采样和调度的持续生产速率；测试集含至少一条约 30 秒输出的长文本，预缓冲后继续生产播放，不能仅用已全部合成的 PCM 验收实时。保存欠载计数及输出录音/回采证据。
- [x] README 写明从干净配置到构建、安装模型、烧录、运行 `SAY` 和重现测试的步骤；列出原生/播放采样率、输入限制、端口、版本、分区恢复方式及非英语发音问题。

最终验收命令由工具提供以下稳定接口；端口必须替换成真实发现值：

```bash
python tools/serial_smoke.py --port /实际应用串口 --suite acceptance --timeout 180
python tools/serial_smoke.py --port /实际应用串口 --suite soak --requests 100 --duration 1800
python tools/report_metrics.py --input reports/acceptance.jsonl --output reports/acceptance.md
```

指标脚本遇到缺少关键数据、请求失败或样本不足时必须非零退出；不能用缺失值当作 0。M4 指标的失败单独输出，不覆盖 M3 功能通过结果。

上面的 180 秒是工具初始默认值；中文非实时验证必须按预测音频时长和已测 RTF 设置每请求超时并配置进度心跳。仅硬上限超时、无进展或取消超时才判运行失败，不能把正常慢速合成误判为死锁。拔串口后的 USB-UART 不一定向 MCU 发断连信号，需明确请求继续/取消策略，由重新 HELLO 恢复主机状态，不臆造设备能自动检测断连。

数值正确只能证明合成器输出，不能证明功放/喇叭实际有声。M3 报告须附一次真实声学录音、板载回采记录或用户现场确认；只有数字 PCM 而无播放证据时，播放验收标为待确认。若使用板载电气回采，还需确认喇叭连接及声学输出，不能仅据回采推断喇叭正常。

## 5. 每个 agent 的交付格式

```text
任务：A / B / C1 / C2 / D / E
分支与提交：
修改文件：
资源版本与 SHA-256：
验证环境：host / T5 实机（端口、板型、频率）
已运行命令与退出码：
原始日志/PCM/报告位置：
完成层级：M0 / M1 / M2 / M3 / M4 / 未完成
接口变更与影响：
未通过项、阻塞原因、后续所需输入：
```

每个工作包提交时只包含自己的文件，平台修改单独给提交/补丁。没有设备就交付可复现的主机结果及待执行实机命令，禁止填写虚构的实机数据。

## 6. 可直接转发的任务提示词

**给 A：** 阅读本计划、关联设计及仓库指令，只执行 Task A。冻结提交、模型和中文音素协议，建立 portable C host oracle 与校验工具，输出 manifest 和参考 PCM。不要改硬件驱动，不启动训练。将资源契约发给 C/D。

**给 B：** 阅读本计划、关联设计及仓库指令，只执行 Task B。独立新建 Core 示例，固化第 3 节接口，跑通串口和 PCM 播放，测试队列、STOP、采样率和播放排空。不要复制当前其他板型配置。向 C/D 提供头文件、固件配置和实际资源数据。

**给 C：** 阅读本计划、关联设计及仓库指令，依赖 A/B 交付，依次执行 C1/C2。先让英文 golden 通过，再做中文有上下文的分块；先保证数值正确，再谈优化。未上板不得宣称 M1/M2 完成。中文训练或模型替换另交建议。

**给 D：** 阅读本计划、关联设计及仓库指令，只执行 Task D。使用 A 冻结的 chinese/cmn 音素协议移植板端 G2P；交付 host/device IDs 一致性、规范化规则和资源报告。不要使用 xiaoya 拼音前端替代，也不要将电脑转音素称为端到端。

**给集成负责人：** 阅读本计划、关联设计及各任务报告，执行 E，独占硬件串口，合并并复现数值和实机测试，交付独立重启可用的固件/模型安装流程。分别裁定 M3 与 M4，不以编译成功代替播报成功。
