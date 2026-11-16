# 07 — AC792N/WL83 语音与唤醒

**优先级：** P3。  
**依赖：** 00 基线；语音实现依赖 02 的 AC791 唤醒闭环完成。可先只读准备 WL83 资料/ABI，但 WL83 必须单独核实，不得推定与 WL82 同 ABI。产品目标唤醒词需明确。

## 目标与当前证据

确认 AC792N/WL83 的音频采集、KWS/VAD、chat-bot 会话/TTS 路径，以及必要库/模型是否在该平台可用；不把 WL82 的链接或硬件结果推及 WL83。

截至当前 master/pin，16k/16bit/mono 采集已接入 RMS VAD，JIELI `tkl_kws.c` 仍为 NOT_SUPPORTED stub。WL83 SDK 自带 KWS 库与 `jl_kws` 参考模块只是候选，不代表 Tuya 接入、目标模型或唤醒词已验证。已有 chat-bot/TTS 记录不能证明 KWS、恢复监听或回声行为通过；“空气很好”是示例词，不是产品目标词。执行前重查 ABI、模型、audio callback 和 board revision。

## 允许修改范围

- 主仓允许范围：`apps/tuya.ai/your_chat_bot/config/`、`apps/tuya.ai/your_chat_bot/src/`、`src/ai_components/ai_main/` 与 `src/ai_components/ai_audio/` 中直接相关的配置/调用点。
- 平台仓允许范围：`platform/JIELI/tuyaos/tuyaos_adapter/src/driver/tkl_kws.c`、`tkl_vad.c`、`tkl_audio.c` 及必要头文件/构建列表；先在隔离 probe 中核对 WL83 ABI 和 PCM，再决定是否集成。
- 不得无证据更换目标词；不得覆盖平台 dirty。用户提供材料的本地评估不以额外许可审批为前置；若发现实际分发限制，记录其边界。
- 实机须在 AC792N 指定 revision 上独占测试。

## 步骤与验收

1. 确认实际 AC792N PCB revision、mic/audio route、ADC 使用通道、采样率/通道数、扬声器路径；对照当前源码与板级配置。
2. 对 WL83 单独检查候选库格式、target triple、C/C++ ABI/runtime、必要模型、许可证、ROM/RAM/PSRAM/stack；先做最小 init 和 PCM probe。
3. 单独测 KWS、VAD、静音/噪声和目标词正负样本；建议起始验收值（需用真实样本校准）：目标词 20 次至少命中 18 次；安静环境静音 30 分钟 0 次误触发；100 次启停/重置均能恢复。记录真实噪声误触发率。所有阈值标为建议起始值。
4. 再接 chat-bot 测多轮会话、错误恢复监听、TTS 同时麦克风策略、回声误触发和长时间资源曲线。记录固件 SHA、样本来源、环境和指标。

**通过条件：** 02 已完成；WL83 自身库 ABI/模型已确认；目标词明确；真实 PCM/KWS 正负样本及建议指标实测值、端到端交互、恢复监听及 TTS 自激行为可复核。只证明 app/TTS 启动不代表 wakeword 已通过。

## 停止条件与交付

库技术不兼容、模型缺失或板级音频拓扑不符时停止接入，交付原因、依赖和可继续的最小方案。提交路径/diff、模型与库指纹、测量结果、运行 SHA、未验项。

## 共用规则

遵守 `README.md`：00 为前置；保护两仓 dirty；独立 `md/` worktree。硬件和串口单一操作者、明确时间窗/交接、现场核实端口（不能假定 COM21）。不进行远端写或模型外传，除非后续明确授权。

