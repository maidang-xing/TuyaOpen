# Claude cli Buddy by T5AI-Pocket



## 项目内容



claude code cli：window



本项目在 T5AI-Pocket 开发板上，通过 BLE 连接到电脑终端 claude cli。claude cli 启动后使用蓝牙扫描连接到设备。



同时本项目也兼容 claude desktop版本（同样的蓝牙协议）



claude cli 将能够获取到的状态、信息、数据、审批功能都通过蓝牙提供给 T5AI 设备，然后设备端显示这些状态、数据以及通过按键控制审批等等。



参考项目：apps/tuya_t5_pocket/claude-desktop-buddy/src/buddies，github ：https://github.com/anthropics/claude-desktop-buddy.git



参考项目：https://github.com/op7418/m5-paper-buddy.git



## 项目模块



### UI 设计



屏幕大小：宽*高：384 * 168 墨水屏（黑白两色）

动画渲染引擎使用 LVGL



UI 的角色主要分为两种

1、一种是代码中自带的Ascll 角色，具体参考 apps/tuya_t5_pocket/claude-desktop-buddy/src/buddies 项目中的内容。

要求这十八种角色完全保留。

2、另一种是用户自定义的gif 角色。

编写脚本，对gif 预处理，通过两种方式：BLE 和 USB/Serial 刷写到设备的flash 中。（**用户自定义角色UI部分 的代码展示保留，不进行开发**）



除了 UI 中的角色之外，还有各种数据、信息、状态、会话等内容需要展示。设备中存在 遥感（可映射为上下左右和向下的按键五个操作方式）以及一个独立的enter按键和esc 按键（用来退出当前页面）。根据这些按键和显示屏信息设计UI（还有 LED的控制） ，方便信息的展示以及用户交互。

LED 的使用方法：src/peripherals/led/tdl_led/include/tdl_led_manage.h

按键 和遥感都已经映射到了 LVGL 中



### 插件

工程中需要增加一个插件的文件夹，里面提供claude cli 可使用的 plugin（使用到claude 的hook）。

插件主要提供 claude 进行一些处理，用户可以一键安装插件在claude 上

具体内容可以参考：[m5-paper-buddy/plugin at main · op7418/m5-paper-buddy](https://github.com/op7418/m5-paper-buddy/tree/main/plugin)

本部分需要单独生成一份README.md 在代码路径下面，讲解插件作用以及如何安装。



### 蓝牙连接

当前只在window 平台的 claude cli 进行蓝牙连接，后续扩展到macos/linux。

设备上需要实时显示蓝牙的连接状态，并且只有claude cli连接成功之后才会进入下一个页面展示claude 的信息。

之后每次启动claude cli之后都会自动启动 BLE 连接设备。



### 通信协议

生成文档专门介绍一下整个交互过程的协议