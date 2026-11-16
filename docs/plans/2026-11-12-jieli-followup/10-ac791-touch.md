# 10 — AC791 触摸输入适配

**优先级：** P2。
**依赖：** 00 基线；与语音、OTA、摄像头实现独立。硬件排队建议在 AC791 语音之后、OTA 之前。
**状态：** 未来被派发时实施；本轮仅计划。

**公共边界：** 本轮只新增/更新任务书，未实现、构建、烧录或实测。未来任务被明确派发后，可在文中范围内修改、验证和本地提交；推送/PR/发布需另有明确授权。先检查主仓与 `platform/JIELI` 各自的分支、dirty 和未跟踪文件，使用隔离 worktree，保护已有修改；硬件/串口单人独占，现场核实板卡版次与端口并按 README 预约、交接，不擦除已有配网/授权。

## 目标与当前证据

为 AC79_DevKitBoard（AC791/WL82）接入板载触摸，使 TuyaOpen 当前使用的 LVGL v9 显示应用能正确接收触摸。显示能工作不代表触摸已适配。

板图：`platform/JIELI/chip/wl82/AC79_AIoT_SDK/doc/datasheet/AC791N规格书/schematic/JL_AC79_DevKitBoard/JL_AC79_DevKitBoard V1.0原理图.pdf` 与参考 `.../schematic/AC79x Wifi Develop V1.0/AC79x Wifi Develop V1.0.pdf`；PDF 文本可见 `TP_SDA`、`TP_SCL`、`TP_INT`、`TP_RESET` 等网络名，但不能据此确认用户实板版次、触控 IC/地址或信号 GPIO。先核实实板丝印/版次并对照图纸、原理图位号图及 SDK board 数据；若不一致先记录差异，不猜型号/引脚。PB0 与 LCD/touch 共用 reset 目前仅为待核线索，不当作既定事实。

SDK 有候选驱动 `platform/JIELI/chip/wl82/AC79_AIoT_SDK/apps/common/ui/touch_panel/gt911.c`、`ft6236.c`；SDK 示例含 `apps/common/example/ui/ui_touch_demo.c`。TuyaOpen 主仓已有 LVGL v9 port `src/liblvgl/v9/port/lv_port_indev.c`，chat-bot显示入口在 `apps/tuya.ai/your_chat_bot/src/display2/`；截至 master 055042a8/JIELI pin c967f4b1，平台 Kconfig 未启用 touch 且未见 `tkl_touch`；派发时重查。控制器与接入层须由执行者依据实板/board 配置确认。

## 允许修改范围

- 在独立 worktree 内，按核实后的板级归属修改 `platform/JIELI/tuyaos/tuyaos_adapter/` 和 `boards/JIELI/AC79_DevKitBoard/` 中实际存在/适用的触摸接口与板级配置；如当前没有接口，先沿现有 adapter 结构实现最小必要入口。
- 主仓限于 `src/liblvgl/v9/port/` 与 `apps/tuya.ai/your_chat_bot/src/display2/` 的实际触摸接入和最小诊断 UI；只有核实构建注册必需时才增改对应清单。不得改无关功能或供应商 SDK 源码。
- 可运行触及文件的相关构建和静态检查；设备测试须预约独占硬件。

## 步骤与验收

1. 确认目标板版次、触控器件丝印、I2C/SPI 接法、IRQ/reset/电源脚与 LCD 共用/复用关系；与 SDK board 配置交叉核验并记录证据。确认 reset 共享时明确所有权/时序，避免 LCD 与触摸互相复位。
2. 依据确认的器件选择现有 SDK 驱动或实现适配；映射 press/move/release、坐标范围与旋转方向，接入确认后的 TKL/TDL 或 LVGL v9 pointer indev。资源申请/释放、重复初始化、错误恢复及生命周期需完整。
3. 完成相关目标构建；实机验证四角与中心触点、拖动轨迹、长按和抬起无粘滞、显示方向与触摸坐标同步；重启及重复 open/close 后仍可用；与显示刷新、音频、网络并发无回归，并记录 I2C/IRQ 错误恢复。
4. 坐标误差阈值和循环次数先作为建议起始值，由实测分辨率与硬件噪声校准；不得将建议数值写成既有硬件规格。

**通过条件：** 有板图/board 配置依据、可复现构建结果及实机交互记录；硬件未知或暂不可用时分层标记阻塞，不声称触摸验收通过。

## 禁止项与交付

不改 PCB、无证据不定控制器或引脚、不借用他人串口租约，不覆盖 dirty，不默认擦除配网/授权。交付板卡/版次、控制器与信号证据、代码文件/diff、构建命令结果、交互与生命周期记录、未验证项及硬件释放确认。
