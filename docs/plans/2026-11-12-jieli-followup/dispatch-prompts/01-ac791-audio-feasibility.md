# 01 — AC791 音频库可行性

适用：00 核实完成后。可与其他任务的非硬件资料准备并行。

```text
请执行任务 01。使用本次指定的 agent 工具直接实施，不要求 GPT-Luna 或特定 MCP；先读适用 AGENTS 和该目录 README 公共规则。主仓从 D:\tuya_proj\TuyaOpen 仓库的 master 分支 在 D:\tuya_proj\TuyaOpen\.worktrees\01 建隔离 worktree；platform/JIELI 独立 checkout 当前 YAML pin。按 D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\01-ac791-audio-feasibility.md 做 AC791/WL82 隔离探针，不接正式应用。新 root 的材料在 D:\tuya_proj\TuyaOpen\.local-jieli-migration\2026-11-13\voice-inputs\（include_tal_audio_subsys.zip、jieli_lib_v2.zip）。核实 archive member、LLVM bitcode triple/data layout、PI32 工具链和 C++ runtime；不凭 std::__1 或 magic 单独判断可/不可链接。优先用现成工具静态读取；最小 link/init/PCM probe 放独立 worktree 的临时目录，记录依赖、符号、内存与实际错误，不安装工具、不运行与任务无关的随包程序；允许构建并执行隔离的最小链接/init/PCM探针，不上传材料。把外部 TAL 路线与 WL82 SDK 自带 KWS/VAD 库路线分开。当前 platform/JIELI 采集为 16k/16bit/mono，capture callback 已喂 RMS VAD。库是否可链仍未证；不得把静态兼容说成跑通。若需要实机，先获唯一硬件租约并登记板版次/串口，测试完释放。返回探针路径、命令证据、结论边界和下一步，不改公共音频源码。
```

本文件只保存派发提示词；尚未执行任务 01。
