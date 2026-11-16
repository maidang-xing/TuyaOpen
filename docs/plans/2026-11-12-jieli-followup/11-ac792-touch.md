# 11 — AC792N 触摸输入适配

**优先级：** P2。
**依赖：** 00 基线；与摄像头、OTA、语音实现独立。
**状态：** 未来被派发时实施；本轮仅计划。

**公共边界：** 本轮只新增/更新任务书，未实现、构建、烧录或实测。未来任务被明确派发后，可在文中范围内修改、验证和本地提交；推送/PR/发布需另有明确授权。先检查主仓与 `platform/JIELI` 各自的分支、dirty 和未跟踪文件，使用隔离 worktree，保护已有修改；硬件/串口单人独占，现场核实板卡版次与端口并按 README 预约、交接，不擦除已有配网/授权。

## 目标与当前证据

为 AC792N Develop（AC792/WL83）接入板载触摸并映射到 TuyaOpen LVGL v9 使用的输入设备。LCD/MIPI-DSI 显示输出与触摸输入是独立路径；显示可用不能作为触摸通过证据。

板图 `platform/JIELI/chip/wl83/AC792_SDK/doc/硬件资料/原理图/开发板/AC792N开发板原理图位号图/AC792N Develop V1.21 原理图.pdf` 的可抽取文字包含 `TP_SDA`、`TP_SCL`、`TP_INT`、`TP_RESET`；这不足以确认用户实板版次、器件料号/地址和 GPIO 复用。先核对实板丝印与位号图、板级配置及 SDK，不猜控制器或引脚。

WL83 SDK 有触摸公共框架 `platform/JIELI/chip/wl83/AC792_SDK/sdk/apps/common/lcd/touch_driver/tp_driver.c`、驱动候选 `gt911.c`、`ft6236.c`、`gt9271.c`、`gt1151.c`、`cst816d.c`、`cst3240.c`，及 `lvgl_v9/examples/porting/lv_port_indev.c`。这些证明 SDK 有可评估候选实现，不证明当前 TuyaOpen adapter/AC792 board 已启用或已适配。TuyaOpen 主仓有 `src/liblvgl/v9/port/lv_port_indev.c` 与 chat-bot `apps/tuya.ai/your_chat_bot/src/display2/`；截至 master 055042a8/JIELI pin c967f4b1，平台 Kconfig 未启用 touch 且未见 `tkl_touch`；派发时重查，并依实板/board 配置确定接入点。

## 允许修改范围

- 在独立 worktree 内，按核实后的板级归属修改 `platform/JIELI/tuyaos/tuyaos_adapter/`、`boards/JIELI/AC792N_Develop_Board/` 与 `boards/JIELI/config/AC792N_Develop_Board.config` 中实际适用的输入接口与配置；若基线核对发现当前板名/版次不同，先记录并按实际路径限定变更。
- 主仓仅修改 `src/liblvgl/v9/port/`、`apps/tuya.ai/your_chat_bot/src/display2/` 的适配及必要的最小验证 UI/构建注册。不得改无关应用或供应商 SDK 源码。
- 相关构建和静态检查可执行；实机测试须单独预约并由一人持有设备/串口。

## 步骤与验收

1. 以实板版次为准核对触控器件、I2C/SPI 总线、地址、IRQ/reset/电源信号、复用及 LCD 共用资源；交叉对照原理图、位号图、SDK board 配置，记录不一致项。
2. 复用经确认的 SDK driver/framework 或实现最小适配；正确处理 press/move/release、坐标边界及显示旋转，注册 LVGL v9 pointer indev 或已确认的项目输入入口。处理 bus/IRQ/reset 初始化顺序、重复 open/close、资源回收和失败恢复。
3. 完成相关目标构建；实机验四角与中心、拖动、长按/抬起无粘滞、触摸方向与显示方向一致；重启/重复 open-close；与显示、音频、网络同时运行无回归；记录错误和恢复结果。阈值/循环次数仅列为建议起始值并按实测校正。

**通过条件：** 具备可核验板卡/器件配置、构建证据和实机分层验收记录。没有硬件证据时明确未验证，不以 SDK 存在驱动或 LCD 点亮替代触摸通过。

## 禁止项与交付

不改 PCB，不臆造型号/引脚，不覆盖主仓或平台仓 dirty，不默认擦除配网/授权，不与其他执行者并行占用同板/端口。交付器件和引脚证据、变更清单/diff、构建与交互日志、未验证/阻塞项及硬件释放确认。
