# Jieli AC7916A TuyaOpen Full-Stack Design

## Goal

在 AC7916A/wl82 上将 Jieli 原生底层能力接入 TuyaOpen TAL/TKL 分层，覆盖
TAL System、UART、Wi-Fi、BLE，并让 `apps/tuya_cloud/switch_demo` 能通过
TuyaOpen 的 `tos.py build` 完成主机侧编译和固件产物生成。设备未连接，真实
烧录和运行日志不作为本轮通过条件。

## Scope

本轮包含：

- JIELI 平台补充 SDK YAML、AC7916A board Kconfig/CMake 和构建参数。
- Jieli FreeRTOS、UART、定时器、队列、线程、互斥锁、信号量、内存、时间、
  重启等 TAL System 依赖。
- Jieli Wi-Fi STA/AP/扫描/连接状态/IP 事件到 TuyaOpen Wi-Fi/TAL Network 的
  适配。
- Jieli BLE 外设角色、广播、连接、GATT 收发和 Tuya 配网所需事件的适配。
- TuyaOpen `switch_demo` 的 AC7916A 配置、完整组件构建和 Jieli 最终链接。
- `tos.py build`、`tos.py flash` 的非交互入口；flash bridge 在没有真实烧录器
  时返回清晰错误。

本轮不包含：AC792x 实现、真实设备烧录成功、云端产品创建、授权码申请、
继电器 GPIO 的具体板卡电气确认和 OTA。

## Architecture

采用方案 B，Jieli 私有 API 不直接进入 TuyaOpen 业务层：

```text
switch_demo / tuya_iot / netmgr
              ↓
       TuyaOpen TKL adapter
              ↓
       Jieli TAL driver layer
              ↓
 Jieli FreeRTOS / Wi-Fi / BLE / lwIP API
              ↓
       AC79 vendor static libraries
```

平台私有 TAL driver 放在 `platform/JIELI/tuyaos/tal_driver`，只暴露稳定的
Jieli 平台中间接口和自有类型；`platform/JIELI/tuyaos/tuyaos_adapter` 中的
TKL 实现负责把 TuyaOpen 类型转换为 TAL driver 类型。TAL driver 不包含
`tuya_cloud_types.h`、`tal_api.h` 或业务代码，便于后续 AC792x 复用接口。

TuyaOpen CMake 负责编译完整 TuyaOpen 组件和 adapter 静态库，Jieli Makefile
继续负责最终启动对象、厂商库、`sdk.ld`、movable-region 和 section 布局。
构建 bridge 将 `libtuyaapp.a`、`libtuyaos.a` 放入 Jieli linker group，避免
修改 vendor SDK 原始 checkout。

## Layer contracts

### Jieli TAL System

提供以下平台中间接口：线程创建/删除、互斥锁、信号量、队列、毫秒延时、
单调时钟、堆统计、随机数、临界区和系统重启。所有阻塞超时统一使用毫秒，
`UINT32_MAX` 表示永久等待；TKL 层负责把 TuyaOpen 超时值转换为该约定。

### Jieli TAL UART

使用 Jieli device API 操作已注册的 `uart2` 调试口；提供 open、close、读、
写、波特率和阻塞模式。TuyaOpen UART 0 映射 `uart2`，不支持的中断/流控命令
返回 `OPRT_NOT_SUPPORTED`，不得静默成功。

### Jieli TAL Wi-Fi

提供初始化、反初始化、扫描、STA 连接/断开、AP 启停、当前状态、MAC/IP、
事件回调和管理帧入口。Jieli 事件回调不得直接调用 TuyaOpen 回调，必须通过
工作队列或系统任务转换到 TuyaOpen 上下文，避免在厂商 Wi-Fi 线程中执行云端
或复杂业务逻辑。

### Jieli TAL BLE

提供 stack init/deinit、地址、广播/扫描、连接/断开、GATT service/characteristic
注册、读写/notify/indicate 和 GAP/GATT 事件回调。Jieli BLE Stack 由 vendor
库提供，不编译 TuyaOpen NimBLE 实现，避免两套 controller/host 同时链接。

### TAL Network

以 Jieli SDK 的 lwIP/socket 实现为底层，补齐 TuyaOpen 所需的 socket、DNS、
select、超时、setsockopt 和接口状态；Wi-Fi link 状态通过统一事件回调传入
`netmgr`。网络 adapter 不重复实现 TCP/IP 协议栈。

## Build and flash flow

```text
apps/tuya_cloud/switch_demo/app_default.config
  → tos.py config/build
  → Kconfig + CMake full component graph
  → TuyaOpen Jieli clang archives
  → Jieli staging Makefile + vendor libraries
  → sdk.elf
  → vendor package or Linux raw app.bin
  → .build/bin/<project>_QIO_<version>.bin
  → tos.py flash
  → JIELI platform_flash_bridge.py
```

`JIELI_SDK_ROOT`、`JIELI_TOOL_DIR` 和 SDK 补充 YAML 提供 include、库、宏和
vendor source 配置。没有 Linux `host-client` 时继续生成明确标注的 raw
`app.bin`；没有 `JIELI_FLASH_CMD` 时 flash 只返回配置错误，不退回通用
`tyutool`。

## Acceptance criteria

1. `tos.py config` 能选择 JIELI/AC7916A，并生成 `switch_demo` 的完整配置。
2. `switch_demo` 的 TuyaOpen CMake/Ninja 阶段和 Jieli 最终链接均返回 0。
3. 产物中包含 TuyaOpen app、TAL System、UART、Wi-Fi、BLE adapter 的符号，
   且不包含 TuyaOpen NimBLE 实现和未解析符号。
4. `tos.py flash -p <port>` 进入 Jieli bridge；无烧录命令时给出可操作错误。
5. 有真实设备后，预期通过 115200 UART 看到启动日志；本轮不声称该硬件验收
   已完成。

## Risks and mitigations

- Jieli Wi-Fi/BLE API 与 TuyaOpen TKL 语义不完全一致：通过 TAL driver 做状态
  和事件归一化，不在 TKL 中散落 vendor 条件编译。
- Jieli vendor 库依赖特定 Makefile 链接顺序：保留 vendor linker，使用 staging
  注入 TuyaOpen archive，不替换为通用 CMake linker。
- 完整组件会暴露更多未实现 TKL 接口：先用链接未解析符号清单驱动补齐，禁止
  用空函数掩盖 required runtime capability。
- 没有板级 Wi-Fi/BLE 天线、电源和串口信息：代码完成后只做主机编译和静态
  产物检查，把硬件验证单独标为 blocked。
