# 07 — WL83（AC792N）产品唤醒词路线备忘

日期：2026-11-17。来源：只读分析 agent + 总管抽查。板卡断开，全部为静态证据，未烧录、未改代码、未做 git 写操作。

## 本轮复核状态

- 已由总管独立复验：
  - `07-voice` 应用 `ai_mode_wakeup.c` 的唤醒回调**不检查 `wakeup_word`**（任何回调都进入 LISTEN）；
  - `examples/multimedia/audio_kws/src/example_kws.c` 调用了 `tkl_kws_wakeup_word_config` / `tkl_kws_get_process_uint_size` / `tkl_kws_recognize_wakeup_word`，而 07 平台 `tuyaos_adapter/include/kws/tkl_kws.h` **没有这些声明**（该示例在 07 平台不可链接；产品 `your_chat_bot` 不构建它，不影响当前镜像）；
  - WL83 `include_lib/newlib/pi32v2-lib/repack_libc.sh` 确实删除 `lib_a-memcmp.o`/`strcmp`/`strlen`/`strncmp` 等对象（对应 TAL 移植的缺符号机制）；实测 WL83 随包 `libc.a` 只剩 `lib_a-wmemcmp.o`，四个窄字符串函数均无定义；
  - TAL 归档位置：`.platform-worktrees/02-voice/chip/wl82/AC79_AIoT_SDK/cpu/wl82/liba/audio_subsys.a`（6,624,528 B）；`llvm-nm -u` 实测它引用 `fflush/fprintf/fseeko/ftello/memcmp/strcmp/strlen/strncmp` 八个符号，而平台工具链 `tools/toolchains/windows/pi32/lib/libc.a` 定义了这四个窄字符串函数。
- 依赖 agent 静态扫描、未逐条复算：WL83 各归档符号导出情况、`jlkws` 8 词与模型常量大小。

## 结论速览

| 问题 | 结论 |
|---|---|
| WL83 8 词能否不改 SDK 源码替换 | 不能通过宏/外部 bin/资源打包替换；词表与声学模型是厂商归档内的链接期常量，替换单位是整个归档 |
| WL83 是否有其他可配置唤醒词接口 | 有 `jl_kws_model_*`/`smart_voice`、yes/no KWS、Wanson ASR、AISpeech ASR 等候选，但没有已证实的自定义唤醒词外部模型加载路径，多数默认关闭 |
| WL82 TAL 归档移植 WL83 | 构建/链接层面可行：同为 pi32v2 + clang 4.0.1 LLVM bitcode；需改 WL83 构建接线并补 4 个 libc 符号；产品词仍需训练侧决定 |
| 训练/移植最小工作集 | int8 TFLite FSMN 模型 + 40 类 token 表 + `keyword_tflite_add_keyword` 的 token ID；02 的 40-token 探针方法可复用（探针本身未上板且仅 AC791） |
| 现状代码改动面 | 换厂商归档约 10 行；接 TAL（feed 模式）平台侧约 200 行 + 19 行 TKL 头 + 构建接线 |

## 1. WL83 词表/模型组织

- 词表只以注释和返回码映射形式出现在源码：`chip/wl83/AC792_SDK/sdk/apps/common/asr/jlkws/jlkws.c:60-72`（`confidence[8]` 列出 小杰小杰、小杰同学、播放音乐、暂停播放、增大音量、减小音量、上一首、下一首；ret 2/3 为前两个唤醒词）。
- 引擎固定 `model = 0`：`jlkws.c:79,85-97,133`；API 只有 `int model` 索引，无模型文件路径（`jlsp_far_keyword.h`）。
- 归档符号：`libJLspKwsFarKeyword.a` 的 `jlsp_far_keyword_param.c.o` 定义 `kws_far_keyword`（181,100 B）、`kws_param_far_keyword`（792 B）、`kws_word_data_far_keyword`（128 B）、`word_topo_param_far_keyword`（20 B）（见 `reports/07-report.md:102-103`）。
- 链接段：`.jlsp_far_keyword_const`/`_code`、`.jlsp_far_front_const`/`_code`（`sdk/include_lib/media/media_lib_text.ld:142-147`）；WL83 默认 `AUDIO_KWS_CHINESE_FAR_AT_RAM=0`（`sdk/audio/cpu/wl83/audio_config_def.h:91`），模型留 flash。
- 无 `.kws_model` 段、无独立模型资源文件；TuyaOpen 工具只负责选归档（`tools/jieli_build/feature_map.py:368-381`）与加链接行（`sdk_overlay.py:1183`）。
- **替换方式**：整体替换 `libJLspKwsFarKeyword.a`（需 JieLi 重新训练/编译），并同步 ret 映射。

## 2. 其它候选接口（均非“外部自定义词”）

- `jl_kws_model_*` / `smart_voice`：`include_lib/media/tech_lib/jlsp_kws.h`、`include_lib/media/asr/jl_kws.h:18` `audio_kws_open(mode, file_name)`；`audio/smart_voice/jl_kws_platform.c:167` 实际传 `NULL`，`kws_model_files[]` 被注释；`TCFG_SMART_VOICE_ENABLE 0`（`apps/wifi_camera/board/wl83/sdk_config.h:310`）。`libJLspKws.a` 内是编译进归档的多词/电话命令模型。
- 仅头文件、WL83 无实现（全 `.a` 无导出）：`JLSP_command_wake_word_*`、`JLSP_phone_call_wake_word_*`、`JLSP_wake_word_*`（Siri 风格）。只有 `JLSP_vad_*` 在 `lib_dvad.a` 导出，是 VAD。
- yes/no KWS（`TCFG_KWS_VOICE_RECOGNITION_ENABLE 0`）、Wanson 命令词 ASR（`TCFG_AUDIO_ASR_DEVELOP=0`）、`user_asr` 空模板、AISpeech ASR（ASR 非唤醒）。
- 未发现 `voice_recognize` / `sound_event` 模块。

## 3. TAL 移植 WL83 可行性

- 应用仓 `audio/tal-engine` 分支没有 `tkl_kws.c`/`tkl_vad.c` 实现，只有 TKL 头与示例；实现只在平台仓。
- 平台 `audio/tal-engine` 的 TAL 分支无芯片守卫（`tkl_kws.c:501` 起），只依赖 `tal_audio_engine_compat.h`、TKL 线程/内存与 libc；另有 WL82 的厂商分支。
- ISA/ABI 一致：TAL 归档 README 标注 pi32v2 LLVM bitcode（clang 4.0.1，SDK LTO 链接）；02 报告 262 个成员均为 `BC C0 DE`、triple `pi32v2`；WL83 归档同样为 clang 4.0.1/pi32v2；平台工具链两芯片同 `-target pi32v2 -mcpu=r3 -flto`。
- 链接缺口：TAL 与 WL83 库对比后有 8 个未解析符号——`fflush`、`fprintf`（WL83 `apps/common/system/init_expand.c:56-70` 已提供）；`fseeko`、`ftello`（`apps/common/c++/cxx_runtime.cpp:52-62` 已提供）；`memcmp`、`strcmp`、`strlen`、`strncmp`（WL83 的 `libc.a` 被 `repack_libc.sh` 主动删除这些对象，工具链 `tools/toolchains/windows/pi32/lib/libc.a` 中有）。
- 构建接线缺口（平台 `audio/tal-engine` `sdk_overlay.py`）：`:858` 读 TAL 开关；`:1055-1059` 加 C++ shim；`:1207-1212` 写死 `liba/audio_subsys.a` + 顶层 `include_lib/c++/libstdc++/libc++.a`/`libc++abi.a`（WL82 布局）。WL83 的 C++ 库在 `ucos/`（vendor `demo_hello` Makefile:242,291-295 为准），需改路径并把 `audio_subsys.a` 放到 WL83 `liba/` 或改路径。
- TAL 分支 `tkl_kws.c` 是 pull 模式（`keyword_tflite_forward_pcm` 按 160 sample/320 B 喂入；`tkl_jieli_kws_feed_capture` 空实现），而产品应用走采集回调 feed（`tkl_audio.c:157`），需要 ring+task 包装或改 pull。

## 4. 训练/导出最小工作集

1. int8 TFLite FSMN 模型（TFL3，16 kHz/16-bit，输出 40 类逐帧 token）；
2. 40 类 token/lexicon 表（ID ↔ 字符/音素）——归档内没有，必须由训练侧提供；
3. 模型选择名（`keyword_tflite_create`）或强符号替换（已验证的做法：`tal_nhty_model.c` 把默认模型换成 `g_fsmn_epoch17_nhty_hty_int8_model_data`，136,392 B）；
4. 导出工具链（TFLite converter + wekws FSMN/CTC），本仓库只有消费侧 API。

约束：`keyword_tflite_add_keyword` 需要 `int *token_ids`；TAL 分支当前传占位 0（`平台 audio/tal-engine tkl_kws.c:562-584`）。TAL arena 44 KiB 是 `KeywordSpottingTflite` 构造器 48 KiB 上限导致（`tkl_kws.c:530-535`）。

40-token 探针可复用点：同 API、同 160-sample 单元、同 76 B 结果结构；但它未上板、仅 AC791 工作区，更适合验证候选序列而非无约束盲搜。

## 5. 现状代码接入点

- 平台 07 分支 `tkl_kws.c`：8 词映射（`:114-117`）、回调 ret 2/3 映射为 `UNKNOWN`（`:206-228`）、采集喂流（`:481-489`）、非 KWS 芯片 stub（`:491-527`）；`tkl_vad.c` 目前是 RMS VAD。
- 应用 07 分支：`ai_chat_main.c:421` `tkl_kws_init()`；`ai_mode_wakeup.c:169-170` 注册+enable；回调 `:61-71` 不检查词；`ai_mode_free.c:174-175` 同样。
- 关键风险：当前回调不区分唤醒词，厂商 8 词（含“小杰小杰”）任一命中都会进入 LISTEN；这是现有 07 镜像的实际行为。

## 6. 需要用户决定（最小下一步）

1. **产品词路线（最关键）**
   - A：让 JieLi 重训/重编 `libJLspKwsFarKeyword.a`，把“你好涂鸦”做进厂商固定模型（路径已实机验证，但依赖厂商训练/授权）；
   - B：走 TAL nhty 路径（模型数据在手，但需 token 表/模型选择 + WL83 构建接线）；
   - C：暂用厂商 8 词（注意当前回调不检查词）。
2. **若选 B，先做“只构建、不烧录”的 WL83 TAL 链接探针**（当前即可做）：`audio_subsys.a` 放入 WL83 `liba/`、libc++ 路径改 `ucos/`、补 `memcmp/strcmp/strlen/strncmp`，目标是在 map 中看到 `keyword_tflite_*`/`g_fsmn_*`/`rnn_vad_*`。
3. **是否授权 AC791 硬件窗口跑 40-token 探针**（`taltokens` preset，624,176 B，从未上板）以反推 token 序列。
4. **license/训练合同**：厂商 `jlkws` 仍使用临时 license（02 报告定位第 119,999 帧后截断），量产需正式授权——这会影响选 A 还是 B。
