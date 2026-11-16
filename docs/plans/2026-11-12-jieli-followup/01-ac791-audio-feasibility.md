# 01 — AC791 音频子系统库可行性

**优先级：** P1。  
**依赖：** 00 基线确认。  
**后续：** 02 AC791 语音唤醒；之后才能排 04/05 等硬件任务。

## 目标与当前证据

判断获得的 TAL audio_subsys 库是否能在 TuyaOpen AC791/WL82 工具链上链接、初始化并处理 16 kHz/16-bit/mono PCM。先验证库 ABI 与最小链路，不直接把它接进正式应用。

任务书转入新 root 后，材料迁入 `D:\tuya_proj\TuyaOpen\.local-jieli-migration\2026-11-13\voice-inputs\`（`include_tal_audio_subsys.zip`、`jieli_lib_v2.zip`）；执行前确认文件存在及校验值。先前只读检查观察到 archive member 为 LLVM bitcode，符号含 C++ `std::__1`；target triple、PI32 LTO 输入兼容性、调用 ABI 和 runtime 仍未确认。没有独立模型是否存在的确证；“空气很好”只是头文件默认示例，不是产品目标词。本任务不预设可链或不可链，应先做工具链/最小链接探针；不执行包内二进制、不外传材料。

## 允许修改范围

- 在独立 `md/` worktree 中，只允许新增隔离的最小探针代码/构建描述，建议放 `apps/` 下临时独立 probe 目录；不得修改 `src/` 公共音频链、正式应用、板级配置或 `platform/JIELI` 现有 dirty 文件。
- 解压和检查仅限本地授权缓存目录/临时目录；不得上传库、头文件、反汇编、源码或密钥。

## 分步工作与验收

1. 核验 archive 来源、文件清单、大小/hash（只写报告），检查每个 archive member 的格式、LLVM bitcode target triple、符号、C++ 标准库/异常/RTTI/runtime 需求；不得据 `std::__1` 单独推断可/不可链接；允许执行隔离构建出的最小链接/init/PCM探针，不运行无关随包程序。
2. 对照 WL82 SDK 编译器、链接器、ABI、libc++ 版本和构建 flags；先生成最小链接探针，确认所需 symbols 与库依赖。链接失败需给出精确符号/格式原因；可寻找有证据的 SDK 替代候选，不改 SDK 解决绕过。
3. 若链接成功，用隔离 probe 验证 init/deinit、内存分配归属、输入 PCM API；再用真实 TKL 音频采集输入一段记录的 16 kHz/16-bit/mono PCM，记录样本格式、长度、耗时、heap/stack 变化和返回值。
4. 测试不应接入实际 KWS 词库或改变唤醒行为。若涉及实机，只在预约的 AC791 板上由单一持有人操作并交接。

**通过条件：** 得出“可用”或“当前不可用”的证据化结论。可用需确认 target triple/ABI/runtime、最小链接、init/deinit 与真实 PCM；不可用需给出不兼容的精确格式/symbol/ABI 证据并检查合理 SDK 候选。未能证明任一方向时标为未决并列缺口，不应据此无限阻塞 02 的独立 VAD/KWS 研究。未完成层次逐项标为未验证。

## 停止条件与交付

遇到缺失依赖来源不明、堆栈/内存超限或缺少隔离环境时停止，不重写库、不外传材料。交付归档 hash/符号摘要、工具链证据、probe 路径/diff、链接与 PCM 日志，以及实际观察到的分发限制。

## 共用规则

遵循 `README.md`：先完成 00；使用独立 `md/` worktree，逐一检查主仓与嵌套平台仓 dirty 并保持原样。硬件/串口只由预约的单一操作者访问，COM 号现场核实，不沿用 COM21。后续被派发不等于授权远端写；不构建正式应用、不声称硬件工作通过，除非对应步骤真的执行并记录。

