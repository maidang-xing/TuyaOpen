# sanoTTS 交付快照：2026-11-03

这是 `md/t5-sanotts` 的源码、现有固件、模型、平台补丁和历史成果归档，不是一次重新通过全部验收的发布。

源码快照：`7dd9329e`。完整 SHA、源码补丁基线、platform 精确基线及模型校验见 [manifest.json](manifest.json)。源码和模型原文件已随本分支跟踪，不需要从本机临时目录获取。

## 文件说明

| 文件 | 内容 |
|---|---|
| `firmware/sanotts_serial_QIO_1.0.0.bin` | 现有完整烧录镜像，物理地址 `0x0` |
| `firmware/sanotts_serial_UA_1.0.0.bin` | 原 dist 中的应用产物，供追溯；不是完整启动镜像 |
| `firmware/sanotts_serial_UG_1.0.0.bin` | 原 dist 中的升级产物，仅归档；本项目 OTA 分区不足，不能据此进行 OTA |
| `firmware/tuya_kconfig.h`、`app_default.config` | 本地生成配置与示例配置，保留原有编译标识 |
| `firmware-debug.tar.gz` | 原 dist/debug 全量输出，包括各核 ELF、MAP、配置和 Flash/PSRAM 布局 |
| `platform_t5ai.patch` | platform/T5AI 相对精确 HEAD 的全部已跟踪改动，3 个文件 |
| `tuyaopen-source.patch.gz` | 主仓库从分叉基线到源码快照提交的完整 binary diff，包含源码、模型和报告，不递归包含本次归档 |
| `historical-reports.txt` | 主仓库和示例目录中已有成果报告的索引 |
| `verification/` | 本次主机测试、资源校验及补丁应用检查的原始结果 |
| `SHA256SUMS` | 本目录全部交付文件及模型镜像、资源清单的校验值 |

中文模型镜像位于 [snt_zh_assets.bin](../../models/chinese/snt_zh_assets.bin)，物理地址 `0x28A000`。原始权重和中文前端数据在同一 `models/chinese/` 目录；英文模型、fixture 和第三方许可证均在示例目录中。

## 固件来源限制

本次保存的是已有 `dist/sanotts_serial_1.0.0/` 固件，没有重编译、重新烧录或复跑真机测试。生成头文件中的 `OPEN_COMMIT` 为 `1664a43b...`，与源码快照 HEAD 不同，因此不能单靠该标识宣称固件已对应所有后续提交。二进制的 SHA-256 是本次归档的准确身份；需要证明源码与固件一致时，应在固定平台基线上应用补丁后重新构建并复测。

历史报告包含已报告的 M0–M3 结果和“先合成、后连续播放”改进；历史 M4 未通过，中文推理等待时间可能达到分钟级。本次不会将这些历史报告改写成本轮实测。

## 本次验证结果

- 模型 manifest 的文件大小和 SHA-256 校验通过。
- 压缩模型镜像中 9 项资产逐项解压、反洗牌、CRC 和原文件对比通过；模型不超过所分配分区，QIO 文件不覆盖模型起始地址。
- platform 补丁在临时目录中的精确基线通过 `git apply --check`，应用后 3 个文件与当前 platform 工作树逐字节一致。
- 主仓库完整源码补丁对独立临时 Git index 的基线通过 `git apply --cached --check`。
- `test_assets.py`：13 项通过；`test_resampler.py`：6 项通过。
- `test_chunk.py`：host harness 编译失败，缺少 `tuya_cloud_types.h`，且 `PR_ERR` 未声明。
- `test_g2p.py`：11 项中 1 项取消测试失败。
- `test_protocol.py`：1024 字节边界测试触发 glibc 堆断言中止。

因此，本快照的完整主机测试套件没有通过。异常细节见 [host-tests.json](verification/host-tests.json) 和各测试日志，归档未修改固件实现来隐藏失败。

## 在干净环境恢复

先检出本分支，初始化 SDK/平台。platform 必须是 manifest 中的精确提交 `69793c66bebf58ef4f886282626c2ea96a28f9a2`；已有本地修改的 platform 不要重复应用补丁。以下命令从仓库根运行：

```bash
git -C platform/T5AI rev-parse HEAD
git -C platform/T5AI apply --check "$PWD/examples/multimedia/sanotts_serial/artifacts/2026-11-03/platform_t5ai.patch"
git -C platform/T5AI apply "$PWD/examples/multimedia/sanotts_serial/artifacts/2026-11-03/platform_t5ai.patch"
cd examples/multimedia/sanotts_serial/artifacts/2026-11-03
sha256sum -c SHA256SUMS
```

需要在不检出本分支的情况下恢复源码时，在 manifest 的 `source_patch_base` 上解压 `tuyaopen-source.patch.gz`，先 `git apply --check` 再应用。该补丁只恢复打包前的源码快照，不包含此归档目录。

从项目重新构建沿用 SDK 的 `export.sh`、Core 配置和 `tos.py build`。若直接烧录归档，先确认串口对应目标板：QIO 写 `0x0`，模型镜像写 `0x28A000`，二者都需要；不要把 UG 当作全量固件。此步骤会使用本项目专用 Flash 布局。

平台 `create_ug_file.py` 补丁跳过缩小 OTA 分区后的容量错误，只为保留现有构建产物；它不代表 UG 可以安全写入当前 OTA 分区。
