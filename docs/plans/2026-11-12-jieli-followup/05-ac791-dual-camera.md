# 05 — AC791 双摄适配

**优先级：** P2。  
**依赖：** 00 基线；先核对实际板卡 revision、原理图和 SDK 摄像头输入能力。硬件队列建议排在语音和 OTA 后。

## 目标与当前证据

用户后续派发本任务时，完成 AC79_DevKitBoard（AC791/WL82）双摄路径：阶段 A 核实真实硬件拓扑；拓扑成立则阶段 B 实现双摄并分别、同时验收帧。若拓扑不成立，凭证据说明实板不支持目标并停止该实现，不虚构接口。

截至当前 TuyaOpen master/JIELI pin，AC79 DevKitBoard camera1 注册为 `-1`，无适配验收。SDK [WL82 README](../../../platform/JIELI/chip/wl82/AC79_AIoT_SDK/README.md) 只确认一路 DVP + 一路 SPI camera 可同步输出 YUV，不证明双 DVP。核对新 root 中的参考图与实板丝印：`platform/JIELI/chip/wl82/AC79_AIoT_SDK/doc/datasheet/AC791N规格书/schematic/AC79x Wifi Develop V1.0/AC79x Wifi Develop V1.0.pdf` 与 `.../JL_AC79_DevKitBoard/JL_AC79_DevKitBoard V1.0原理图.pdf`；尚未证明图纸与用户实板型号/revision一致，也没有双路有效帧记录。旧 worktree 不可续用。

## 允许修改范围

- 阶段 A 允许只读核对本地原理图、SDK headers/sample、board manifest 和板级测试文件。
- 阶段 B 若拓扑成立，允许在独立 worktree 修改 `platform/JIELI/tuyaos/tuyaos_adapter/`、`boards/JIELI/AC79_DevKitBoard/`、对应 camera TDD/API 与 `examples/` 最小验证应用，完成实现和验收，无需另开可行性后续任务。
- 不改电路/引脚、不接线、不新增未经确认的信号，也不把 SPI 摄像头宣称为第二路 DVP。

## 步骤与验收

1. 从本地现有资料确认板名、PCB revision 与原理图 title block/版本；将连接器、pin、sensor 模组和 DVP/SPI/CSI2 信号逐项对照。若有多份资料冲突，记录冲突而非选择性引用。
2. 对照 WL82 SDK camera driver、sensor driver/sample、时钟/帧同步限制和 DMA 缓冲区，确认两路是否能同时开，不仅是先后切换；注明所需带宽、内存、pinmux、IRQ 与传感器初始化方式。
3. 若拓扑不成立或实板只有单路接口，给出图纸/实板证据和硬件阻塞结论，不编码虚构的第二通道；不要要求用户重复提供本机已有资料。
4. 若拓扑成立，先分别用每一路采集有效帧，再同时采集；记录帧号/时间戳、丢帧、分辨率、帧率、buffer、内存和 30 分钟稳定性（建议起始值，按产品要求校准），并记录传感器 SKU 与线束。

**通过条件：** 阶段 A 对实板 revision、两路物理连接和 SDK 并发路径给出证据；阶段 B 若拓扑成立则分别和同时实测两路有效帧。若拓扑被证伪，提供可核验硬件阻塞证据即可作为 blocked outcome。只有 SDK API 或原理图库不能视为双摄通过。

## 停止条件与交付

图纸不匹配、无第二输入 pin、SDK 无并行支持、传感器型号/驱动不明时停止编码。交付拓扑图/来源、SDK API/source 证据、性能预算与继续/停止建议。

## 共用规则

遵守 `README.md`：先 00；每个仓库单独隔离并保护 dirty；硬件排队且单人持有。端口与连接现场核实，不套用 COM21。任务范围内允许隔离分支本地提交；推送、发布需明确授权，不进行硬件改线。

