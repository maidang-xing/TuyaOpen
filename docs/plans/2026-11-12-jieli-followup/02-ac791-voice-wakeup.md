# 02 — AC791 语音唤醒与会话恢复

**优先级：** P1。  
**依赖：** 00 基线；01 的音频库至少已确定可否链接/替代实现；产品目标唤醒词由需求方确认。  
**硬件顺序：** AC791 音频验证单独排期；语音闭环完成前不把板交给 OTA 或摄像头任务。

## 目标与当前证据

在 AC791/WL82 上建立可重复的“采集 → 唤醒判断 → chat-bot 对话 → TTS 播放 → 恢复监听”闭环，并确认 VAD/KWS 与扬声器回声对话行为。

截至当前 TuyaOpen master / JIELI pin（见 README），音频采集 contract 为 16 kHz、16-bit、mono；`tkl_audio.c` capture callback 已把 PCM 分别送入上层回调与 `tkl_jieli_vad_feed_capture`，后者聚帧后调用 RMS VAD；`tkl_kws.c` 仍为 NOT_SUPPORTED stub。WL82 SDK 有厂商 KWS 库及调用示例，但未有在 Tuya/TKL 链路运行或产品词模型确证。TAL archive 为 LLVM bitcode，target/ABI 未决；不要把示例词“空气很好”当目标词。每次执行重查实际 pin 和源文件，不沿用其他分支观察。

## 允许修改范围

- 主仓允许范围：`apps/tuya.ai/your_chat_bot/config/`、`apps/tuya.ai/your_chat_bot/src/`、`src/ai_components/ai_main/`、`src/ai_components/ai_audio/` 中直接相关的配置与调用点。
- 平台仓允许范围：`platform/JIELI/tuyaos/tuyaos_adapter/src/driver/tkl_kws.c`、`tkl_vad.c`、`tkl_audio.c` 及直接所需头文件/构建列表。核实 contract 与候选实现后再替换 stub；不得静默改变目标词。
- 不得改公共 KWS/VAD API contract，除非本任务证明有必要并在 diff 中列明兼容范围。
- 板卡仅使用预约的 AC791 目标 revision；不得同时做 OTA 或摄像头实验。

## 步骤与验收

1. 明确唤醒词、语言、准确率/误唤醒指标与静音/噪声测试条件；没有指标先把候选词和阈值记为待确认。
2. 核验候选 KWS API、模型来源、target ABI、所需内存与栈，并记录实际观察到的分发限制；优先复用 SDK 自带可验证的实现。不要预设要重编 KWS 库。
3. 以离线音频/PCM probe 分开测 KWS 与 VAD，记录正例、负例、噪声、静音输入、阈值、耗时、heap/stack。建议起始验收值（按目标样本校准）：目标词 20 次至少命中 18 次；安静环境静音 30 分钟 0 次误触发；100 次启停/重置均能恢复。记录真实噪声环境误触发率；这些数值是起始值，不代替产品定义。
4. 接入聊天应用后验证唤醒触发、按键行为、ASR/云端错误后的恢复监听、TTS 播放期间麦克风策略、扬声器回声/自激和重复触发。分别记录现场录音条件与固件 SHA。

**通过条件：** 产品目标词已确认；KWS/VAD 正负样本测试可复现并报告上述建议指标实测值；真实板完成唤醒—会话—TTS—恢复监听，失败路径可恢复。未完成的准确率/长时间运行/噪声条件要标出。

## 停止条件与交付

若目标词未确认、候选库技术上不兼容、模型缺失、没有安全音频测试环境或内存预算不清，停止在可行性报告。交付变更、测试向量来源、真实采样条件、指标结果、固件 SHA 和回退路径；不得把离线 probe 写成实机通过。

## 共用规则

遵守 `README.md`：先确认 00；保持所有 dirty 和敏感配置；主仓/平台分开 worktree。实机由一名持有人独占，排队、交接并现场核对 COM 号（不能套用 COM21）。远端写需单独明确授权；此任务书本身不授权实现以外的发布/推送操作。

