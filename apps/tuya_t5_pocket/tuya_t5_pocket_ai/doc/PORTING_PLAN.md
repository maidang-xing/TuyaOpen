# Claude Desktop Buddy - TuyaOpen 移植实施计划

## 项目概述

将 Claude Desktop Buddy 固件从 Arduino/M5StickC Plus 平台移植到 TuyaOpen SDK（T5AI Pocket 设备），使用 TAL BLE 进行蓝牙通信，LVGL v9 进行界面展示。

**原始项目：** 基于 ESP32 M5StickC Plus 的桌面宠物，通过 BLE Nordic UART Service 与 Claude 桌面应用实时连接，将 Claude 工作状态映射为动画角色。

**移植目标：** 复用 TuyaOpen SDK 的 BLE 外设层、LVGL 显示框架和 KV 存储，在 T5AI Pocket（384x168 LCD）上实现相同功能。

---

## 当前阶段（UI 复刻，本次迭代）

> 本次任务聚焦 **UI 复刻**，不包含 BLE、JSON 协议和持久化。通过注入 **demo state** 把 `claude-desktop-buddy/src/main.cpp` 里的显示行为在 T5AI Pocket 的 LVGL 屏上再现，作为后续移植的骨架。

### 目标

1. 复用现有 `tuya_t5_pocket_ai` 应用的 **屏幕栈管理** 与 **ducky GIF 资源**。
2. 在 `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/` 下实现：
   - `buddy_main_screen.c/h` — 主屏幕（状态栏 + 宠物 GIF + 三种信息面板 + 导航提示）
   - `buddy_approval_screen.c/h` — 权限审批界面
   - `buddy_ui_entry.h` — 外部 include 汇总头，由菜单调用
   - `buddy_data.h` — 状态枚举与 Demo/数据结构（已存在）
3. 在 `menu_scan_screen.c` 中 **第一项** 新增 `Claude Buddy`，点击后通过 `screen_load(&buddy_main_screen)` 进入。
4. 通过内置 **Demo state** 循环驱动 7 种 persona 动画与 6 页 Info 内容，不依赖 BLE。

### 主屏幕布局（384×168 横屏）

```
┌──────────────────────────────────────────────────────────────┐
│ BLE:linked   Claude Buddy        t:3 r:1 w:0     [busy]      │ 20px 顶部状态栏
├──────────────────┬───────────────────────────────────────────┤
│                  │  NORMAL 模式: tokens / 会话 / message     │
│   Ducky GIF      │  STATS  模式: mood/fed/energy 条 + Lv/Tok │  120px 主区域
│   160x120        │  INFO   模式: 6 页文字（About/Buttons/…） │
│                  │                                           │
├──────────────────┴───────────────────────────────────────────┤
│ ENTER:mode  L/R:page  U/D:persona  ESC:back         1/3      │ 20px 导航提示
└──────────────────────────────────────────────────────────────┘
```

### 按键交互（与原项目等价映射）

| 原按键 | 本项目按键 | 行为 |
|--------|------------|------|
| A 短按 | `KEY_ENTER` | 切换 NORMAL → STATS → INFO |
| B 短按 | `KEY_RIGHT` | INFO 模式翻页 / 审批时 deny |
| —      | `KEY_LEFT`  | INFO 模式回退页 |
| —      | `KEY_UP/DOWN` | Demo：切换 persona 状态（验证动画） |
| A 长按 | `KEY_ESC`  | 返回上一屏（菜单） |

### Persona → GIF 映射（复用已有 ducky 资源）

| Persona | 使用的 `lv_img_dsc_t` | 视觉 |
|---------|----------------------|------|
| SLEEP | `ducky_sleep` | 睡觉 |
| IDLE | `ducky_stand_still` / `ducky_walk` / `ducky_blink` 循环 | 站立、走动、眨眼 |
| BUSY | `ducky_dance` | 忙碌 |
| ATTENTION | `ducky_sick` | 警觉 |
| CELEBRATE | `ducky_dance` | 庆祝 |
| DIZZY | `ducky_emotion_cry` | 眩晕 |
| HEART | `ducky_emotion_happy` | 爱心 |

### 本次产出文件

- **新增：**
  - `src/display/ui/buddy_ui/buddy_main_screen.c`
  - `src/display/ui/buddy_ui/buddy_approval_screen.c`
  - `src/display/ui/buddy_ui/buddy_approval_screen.h`
  - `src/display/ui/buddy_ui/buddy_ui_entry.h`
  - `doc/BUDDY_UI_PORT.md`（本次实现总结）
- **修改：**
  - `src/display/ui/menu_scan_screen.c`（新增首项 Claude Buddy）
  - `src/display/ui/menu_scan_screen.h`（无需改动，若需可扩展）
  - `doc/PORTING_PLAN.md`（新增本节）
- **保留：** 现有 `buddy_data.h`、`buddy_main_screen.h`。

### 验证方式

- `tos.py build`（通过 `/tuyaopen-build` 技能） 编译 `apps/tuya_t5_pocket/tuya_t5_pocket_ai`；
- 交互验证：进入菜单首项，切换 persona、mode、page。

### 后续阶段（不在本次迭代）

保留原 PORTING_PLAN 中的 任务 2～任务 11（BLE 桥接、JSON 协议、状态机、统计持久化、全系统集成、CLI 烟雾测试），等 UI 骨架稳定后再逐步接入。

---

## 整体架构

```
┌─────────────────────────────────────────────────────────┐
│ tuya_main.c （主入口）                                    │
│  - 启动初始化所有模块                                      │
│  - JSON 行缓冲区拼接                                      │
│  - 各子系统间回调连接                                      │
│  - CLI 调试命令                                           │
└────────┬──────────┬──────────┬───────────┬──────────────┘
         │          │          │           │
    ┌────▼────┐ ┌───▼───┐ ┌───▼───┐ ┌────▼──────┐
    │buddy_ble│ │buddy_ │ │buddy_ │ │buddy_     │
    │ BLE桥接 │ │protocol│ │state  │ │stats      │
    │         │ │JSON协议│ │状态机  │ │统计与持久化│
    │初始化   │ │解析    │ │7种状态 │ │tokens     │
    │广播/连接│ │响应构建│ │推导    │ │等级/KV    │
    │收发数据 │ │        │ │        │ │           │
    └────┬────┘ └───┬───┘ └───┬───┘ └───────────┘
         │          │          │
         │     ┌────▼──────────▼────────────────┐
         │     │ buddy_display.c （显示调度）      │
         │     │  - 初始化 LVGL                   │
         │     │  - 状态 → 屏幕路由               │
         │     └────┬───────────┬───────────┬───┘
         │     ┌────▼────┐ ┌───▼─────┐ ┌───▼───┐
         │     │主屏幕    │ │审批屏幕  │ │统计屏幕│
         │     │GIF动画  │ │批准/拒绝 │ │进度条  │
         │     │HUD状态栏│ │按钮操作  │ │等级    │
         │     └─────────┘ └─────────┘ └───────┘
         │
    ┌────▼────────────────────┐
    │ TuyaOpen SDK 底层       │
    │ TAL BLE (tal_bluetooth) │
    │ LVGL v9 (lv_vendor)     │
    │ tal_kv, tal_sw_timer    │
    └─────────────────────────┘
```

---

## 平台对照表

| 功能 | 原始平台 (M5StickC Plus) | 目标平台 (TuyaOpen T5AI Pocket) |
|------|-------------------------|-------------------------------|
| MCU | ESP32 | T5AI |
| 显示 | 135x240 TFT, 直接 SPI 驱动 | 384x168 LCD, LVGL v9 |
| 蓝牙 | Arduino BLE, Nordic UART Service | TAL BLE 外设模式 (tal_bluetooth.h) |
| 存储 | Arduino Preferences (NVS) | tal_kv 键值存储 |
| JSON | ArduinoJson 7.0 | cJSON |
| 定时器 | millis() / FreeRTOS | tal_sw_timer |
| 按钮 | GPIO 直读 | LVGL indev + tdl_button |
| 动画 | ASCII 字符画 + AnimatedGIF 库 | LVGL GIF 控件 (lv_gif) |
| 构建 | PlatformIO | CMake + Kconfig |

---

## 关键 API 映射

### BLE 通信层

| 原始 API | TuyaOpen 等效 API | 说明 |
|---------|------------------|------|
| `BLEDevice::init()` | `tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, cb)` | 初始化 BLE 外设角色 |
| `pServer->startAdvertising()` | `tal_ble_advertising_start(&params)` | 开始广播 |
| `pCharacteristic->notify()` | `tal_ble_server_common_send(&pkt)` | 通过 Notify 特征发送数据 |
| `onWrite()` 回调 | `TAL_BLE_EVT_WRITE_REQ` 事件 | 接收桌面端写入的数据 |
| `onConnect()` / `onDisconnect()` | `TAL_BLE_EVT_PERIPHERAL_CONNECT` / `TAL_BLE_EVT_DISCONNECT` | 连接/断开事件 |

**传输方案：** 使用 TAL BLE 默认的 Tuya 服务 (UUID 0xFD50) 进行数据传输。Write 特征 (index 0) 作为桌面端→设备通道，Notify 特征 (index 1) 作为设备→桌面端通道。上层 JSON 行协议保持不变。

### 显示层

| 原始实现 | TuyaOpen 等效 | 说明 |
|---------|-------------|------|
| `M5.Lcd.fillScreen()` | LVGL `lv_obj_set_style_bg_color()` | 背景色 |
| ASCII 字符画渲染 | `lv_gif_create()` + GIF 资源 | 动画渲染 |
| `M5.Lcd.drawString()` | `lv_label_create()` + `lv_label_set_text()` | 文字显示 |
| 直接帧缓冲操作 | LVGL 控件树 + `lv_vendor_disp_lock/unlock` | 线程安全显示更新 |

### 存储层

| 原始 API | TuyaOpen 等效 | 说明 |
|---------|-------------|------|
| `preferences.begin("buddy")` | `tal_kv_init(&cfg)` | 初始化键值存储 |
| `preferences.putUInt("tokens", val)` | `tal_kv_set("key", data, len)` | 写入数据 |
| `preferences.getUInt("tokens", 0)` | `tal_kv_get("key", &buf, &len)` | 读取数据 |

---

## 文件结构

```
apps/tuya_t5_pocket/tuya_t5_pocket_buddy/
├── CMakeLists.txt                      # 构建配置
├── app_default.config                  # 板级/功能选择 (T5AI Pocket)
├── Kconfig                             # 配置选项 (LVGL, BLE)
│
├── include/
│   ├── buddy_ble.h                     # BLE 桥接：初始化、发送、接收回调
│   ├── buddy_protocol.h                # JSON 协议：解析心跳/权限/传输消息
│   ├── buddy_state.h                   # 状态机：TamaState → PersonaState
│   ├── buddy_stats.h                   # 统计：tokens、审批、等级、KV 持久化
│   └── buddy_display.h                 # 显示初始化与消息调度
│
├── src/
│   ├── tuya_main.c                     # 主入口、初始化序列、CLI 测试命令
│   ├── buddy_ble.c                     # BLE 外设实现
│   ├── buddy_protocol.c               # JSON 解析/序列化实现
│   ├── buddy_state.c                   # 状态机实现
│   ├── buddy_stats.c                   # 统计 + KV 实现
│   ├── buddy_display.c                 # 显示调度实现
│   │
│   └── display/
│       ├── CMakeLists.txt              # 显示子模块构建
│       ├── ui/
│       │   ├── screen_manager.c/h      # 栈式屏幕导航（复用现有应用）
│       │   ├── buddy_main_screen.c/h   # 主屏幕：宠物动画 + HUD
│       │   ├── buddy_approval_screen.c/h # 权限审批界面
│       │   └── buddy_stats_screen.c/h  # 统计页面
│       ├── anim/                       # 动画资源（复用 ducky GIF）
│       ├── icons/                      # 图标资源（电池、WiFi）
│       └── fonts/                      # 字体资源（Terminus）
```

---

## 模块职责

### 1. buddy_ble — BLE 通信桥接

**职责：** 封装 TAL BLE 外设模式，广播设备名 "Claude-Buddy"，管理单连接，提供原始字节收发。

**对外接口：**
```c
// 初始化 BLE 外设，注册数据接收和连接状态回调
OPERATE_RET buddy_ble_init(buddy_ble_recv_cb_t recv_cb, buddy_ble_conn_cb_t conn_cb);

// 通过 Notify 特征发送数据
OPERATE_RET buddy_ble_send(const uint8_t *data, uint16_t len);

// 查询连接状态
bool buddy_ble_is_connected(void);
```

**工作流程：**
1. 调用 `tal_ble_bt_init()` 初始化 BLE 栈
2. 收到 `TAL_BLE_STACK_INIT` 事件后设置广播数据并开始广播
3. 连接建立时通知上层，开始转发写入数据
4. 断开连接后自动重新开始广播

### 2. buddy_protocol — JSON 协议解析

**职责：** 解析 Claude 桌面端发来的 JSON 行消息，构建响应消息。

**消息类型：**

| 类型 | 方向 | 格式 |
|------|------|------|
| 心跳 | 桌面→设备 | `{"total":3, "running":1, "waiting":0, "msg":"Working...", "tokens":12000}` |
| 权限请求 | 桌面→设备 | `{"prompt":{"id":"abc", "tool":"Read", "hint":"src/main.cpp"}}` |
| 权限响应 | 设备→桌面 | `{"cmd":"permission", "id":"abc", "decision":"once"}` |

**对外接口：**
```c
// 解析一行 JSON，输出消息类型和数据
OPERATE_RET buddy_protocol_parse(const char *json_line, buddy_msg_t *out);

// 构建并发送权限响应
OPERATE_RET buddy_protocol_send_permission_response(const char *id, const char *decision);
```

### 3. buddy_state — 状态机

**职责：** 根据 BLE 连接状态、心跳数据和用户操作，推导出 7 种人格状态。

**状态定义与转换规则：**

```
┌─────────────────────────────────────────────────────────────┐
│                        状态转换图                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [未连接] ──────────────────────────────→ SLEEP (睡眠)      │
│                                                             │
│  [已连接, 无活跃会话] ──────────────────→ IDLE (空闲)       │
│                                                             │
│  [已连接, 有运行中会话] ────────────────→ BUSY (忙碌)       │
│                                                             │
│  [收到权限请求] ────────────────────────→ ATTENTION (警觉)   │
│                                                             │
│  [5秒内批准权限] ──→ HEART (爱心, 2秒) ──→ 返回基础状态     │
│                                                             │
│  [升级 (每50K tokens)] → CELEBRATE (庆祝, 3秒) → 返回基础状态│
│                                                             │
│  [摇晃设备] ────→ DIZZY (眩晕, 2秒) ────→ 返回基础状态      │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

| 状态 | 触发条件 | 持续时间 | 动画 |
|------|---------|---------|------|
| SLEEP | BLE 未连接 | 持续 | 闭眼缓慢呼吸 |
| IDLE | 已连接，无运行会话 | 持续 | 走动、眨眼、站立 |
| BUSY | 有运行中的会话 | 持续 | 忙碌工作动画 |
| ATTENTION | 收到权限审批请求 | 直到处理 | 警觉状态 |
| HEART | 5秒内快速批准 | 2秒 | 漂浮爱心 |
| CELEBRATE | 升级（每50K tokens） | 3秒 | 弹跳庆祝 |
| DIZZY | 摇晃设备 | 2秒 | 螺旋眼 |

### 4. buddy_stats — 统计与持久化

**职责：** 追踪 token 消耗、审批记录、等级计算，通过 tal_kv 持久化到 Flash。

**数据结构：**
```c
typedef struct {
    uint32_t total_tokens;    // 累计 token 消耗
    uint16_t level;           // 等级 = total_tokens / 50000
    uint16_t approvals;       // 批准次数
    uint16_t rejections;      // 拒绝次数
    uint8_t  mood;            // 心情 0-100（基于批准率）
    uint8_t  food;            // 饱食度 0-100（基于 token 消耗）
    uint8_t  energy;          // 精力 0-100
} buddy_stats_t;
```

**关键机制：**
- **等级：** 每消耗 50,000 tokens 升 1 级，触发 CELEBRATE 动画
- **心情：** 基于批准率，拒绝越多心情越差
- **饱食度：** 每 5,000 tokens 增加 1 格
- **持久化：** 使用 `tal_kv_set/get` 以 `"buddy_stats"` 为键存储整个结构体

### 5. buddy_display — 显示调度

**职责：** 初始化 LVGL，管理屏幕生命周期，将状态变化路由到对应屏幕。

### 6. buddy_main_screen — 主屏幕

**布局（384x168 像素）：**

```
┌─────────────────────────────────────────┐
│ BLE: Connected              IDLE        │ ← 状态栏 (22px)
├─────────────────────────────────────────┤
│                                         │
│          [鸭子 GIF 动画区域]              │ ← 宠物区域 (120px)
│           159x164 像素                   │
│           可左右移动 ±80px               │
│                                         │
├─────────────────────────────────────────┤
│ Working on feature...                   │ ← 消息栏 (26px)
└─────────────────────────────────────────┘
```

**状态→动画映射（使用现有 ducky GIF）：**

| PersonaState | 使用的 GIF 资源 | 视觉效果 |
|-------------|----------------|---------|
| SLEEP | ducky_sleep | 闭眼，缓慢呼吸 |
| IDLE | ducky_stand_still / walk / blink | 站立、走动、眨眼自动切换 |
| BUSY | ducky_dance | 快速移动，忙碌状态 |
| ATTENTION | ducky_sick | 警觉姿态（复用生病动画） |
| HEART | ducky_emotion_happy | 开心表情，爱心 |
| CELEBRATE | ducky_dance | 弹跳庆祝 |
| DIZZY | ducky_emotion_cry | 眩晕表情 |

### 7. buddy_approval_screen — 审批屏幕

**布局：**

```
┌─────────────────────────────────────────┐
│ PERMISSION REQUEST                      │ ← 标题 (橙色背景)
├─────────────────────────────────────────┤
│                                         │
│ Tool: Read                              │ ← 工具名称
│ File: src/main.cpp                      │ ← 文件提示（可滚动）
│                                         │
├─────────────────────────────────────────┤
│    [ENTER] 批准    [ESC] 拒绝            │ ← 操作按钮提示
└─────────────────────────────────────────┘
```

### 8. buddy_stats_screen — 统计屏幕

**布局：**

```
┌─────────────────────────────────────────┐
│ BUDDY STATS                             │
├─────────────────────────────────────────┤
│ Mood:   [████████░░] 80%                │ ← 橙色进度条
│ Food:   [██████░░░░] 60%                │ ← 绿色进度条
│ Energy: [██████████] 100%               │ ← 蓝色进度条
├─────────────────────────────────────────┤
│ Level: 3          Tokens: 156K          │
│ Approved: 42  Rejected: 3              │
└─────────────────────────────────────────┘
```

---

## 实施任务清单

### 任务 1：项目脚手架

**目标：** 创建应用目录结构、构建配置、所有头文件桩、最小可编译的主入口。

**产出文件：**
- `CMakeLists.txt` — 应用构建配置
- `app_default.config` — 板级选择 (T5AI Pocket)
- `Kconfig` — 功能开关 (LVGL, BLE)
- `include/*.h` — 5 个模块头文件（接口定义）
- `src/tuya_main.c` — 最小主入口（仅初始化日志和 KV）
- `src/display/CMakeLists.txt` — 显示子模块构建

**验证：** `tos build tuya_t5_pocket_buddy` 编译通过。

---

### 任务 2：BLE 通信桥接

**目标：** 实现 BLE 外设模式，广播 "Claude-Buddy"，支持连接/断开/收发。

**产出文件：**
- `src/buddy_ble.c` — BLE 外设完整实现

**关键实现：**
- 使用 `tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, callback)` 初始化
- 广播数据包含设备名 "Claude-Buddy"
- 广播间隔：60ms ~ 120ms（快速发现）
- 连接后停止广播，断开后自动重新广播
- 接收数据通过 `TAL_BLE_EVT_WRITE_REQ` 事件
- 发送数据通过 `tal_ble_server_common_send()`

**验证：** 编译通过，设备上电后可被手机蓝牙扫描到。

---

### 任务 3：JSON 协议解析

**目标：** 解析心跳和权限请求 JSON，构建权限响应 JSON。

**产出文件：**
- `src/buddy_protocol.c` — JSON 解析与序列化

**关键实现：**
- 使用 cJSON 库解析 JSON
- 心跳消息：提取 total/running/waiting/msg/tokens 字段
- 权限消息：提取 prompt.id/tool/hint 字段
- 响应构建：生成 `{"cmd":"permission","id":"...","decision":"once/deny"}` + 换行符

**验证：** 编译通过。

---

### 任务 4：状态机

**目标：** 实现 7 种人格状态的推导和转换逻辑。

**产出文件：**
- `src/buddy_state.c` — 状态机实现

**关键实现：**
- 基础状态推导：BLE 断开→SLEEP，无会话→IDLE，有会话→BUSY，有权限请求→ATTENTION
- 瞬态状态：HEART (2秒)、CELEBRATE (3秒)、DIZZY (2秒)，到期后返回基础状态
- 使用 `tal_sw_timer` 实现瞬态状态定时回退
- 等级检测：token 增量导致等级变化时触发 CELEBRATE

**验证：** 编译通过。

---

### 任务 5：统计与持久化

**目标：** 追踪 token、审批记录和等级，持久化到 Flash。

**产出文件：**
- `src/buddy_stats.c` — 统计模块实现

**关键实现：**
- 启动时从 `tal_kv_get("buddy_stats")` 恢复数据
- 每次数据变化时 `tal_kv_set("buddy_stats")` 保存
- 等级 = total_tokens / 50000
- 心情 = 100 - 拒绝率百分比
- 饱食度 = (tokens % 50000) / 5000，上限 100

**验证：** 编译通过。

---

### 任务 6：复用共享显示资源

**目标：** 从现有 `tuya_t5_pocket_ai` 应用复制屏幕管理器、GIF 动画、图标、字体。

**操作：**
- 复制 `screen_manager.c/h` → `display/ui/`
- 复制 11 个 ducky GIF C 文件 → `display/anim/`
- 复制电池/WiFi 图标 → `display/icons/`
- 复制 Terminus 字体 → `display/fonts/`

**验证：** 编译通过，资源文件正确链接。

---

### 任务 7：主屏幕

**目标：** 实现宠物 GIF 动画、状态栏和消息条的主界面。

**产出文件：**
- `src/display/ui/buddy_main_screen.c/h`

**关键实现：**
- 预加载 9 个 GIF 对象（stand/walk/walk_left/blink/sleep/dance/sick/happy/cry）
- 100ms 动画定时器切换 GIF 显示
- 200ms 移动定时器控制 IDLE 状态下的走动行为
- 状态栏显示 BLE 连接状态和当前人格状态名
- 底部消息条显示桌面端推送的消息文本

**验证：** 编译通过，屏幕上显示 ducky 动画。

---

### 任务 8：审批屏幕

**目标：** 实现权限请求的审批/拒绝界面。

**产出文件：**
- `src/display/ui/buddy_approval_screen.c/h`

**关键实现：**
- 显示工具名和文件提示
- Enter 键批准，Esc 键拒绝
- 操作后自动返回主屏幕
- 通过回调通知上层处理结果

**验证：** 编译通过。

---

### 任务 9：统计屏幕

**目标：** 实现统计数据的可视化展示。

**产出文件：**
- `src/display/ui/buddy_stats_screen.c/h`

**关键实现：**
- 三个进度条：心情（橙）、饱食度（绿）、精力（蓝）
- 数值标签：等级、token 总数、批准/拒绝次数
- Esc 键返回主屏幕

**验证：** 编译通过。

---

### 任务 10：全系统集成

**目标：** 将所有子系统连接起来，实现完整数据流。

**产出文件：**
- `src/buddy_display.c` — 显示调度层
- 更新 `src/buddy_ble.c` — 增加连接状态回调
- 更新 `src/tuya_main.c` — 完整初始化和回调连接

**数据流：**

```
BLE 接收 ──→ JSON 行拼接 ──→ 协议解析 ──→ 消息分发
                                           │
                              ┌─────────────┼──────────────┐
                              ▼             ▼              ▼
                         心跳消息       权限请求        其他消息
                              │             │
                              ▼             ▼
                         状态机更新    状态机→ATTENTION
                              │             │
                              ▼             ▼
                         主屏幕动画    弹出审批屏幕
                         更新消息栏         │
                                           ▼
                                    用户按键操作
                                    Enter=批准 / Esc=拒绝
                                           │
                                           ▼
                                    构建 JSON 响应
                                           │
                                           ▼
                                    BLE 发送至桌面端
```

**验证：** 完整编译通过，烧录后设备正常启动并显示界面。

---

### 任务 11：CLI 烟雾测试

**目标：** 添加串口 CLI 命令，无需 Claude 桌面端即可测试所有功能。

**测试命令：**

| 命令 | 作用 | 预期结果 |
|------|------|---------|
| `buddy_test connect` | 模拟 BLE 连接 | 状态 SLEEP→IDLE，宠物开始走动 |
| `buddy_test heartbeat` | 模拟心跳（running=1） | 状态→BUSY，消息栏更新 |
| `buddy_test permission` | 模拟权限请求 | 状态→ATTENTION，弹出审批屏 |
| `buddy_test approve` | 模拟快速批准 | 状态→HEART (2秒)→BUSY |
| `buddy_test reject` | 模拟拒绝 | 记录拒绝，返回基础状态 |
| `buddy_test shake` | 模拟摇晃 | 状态→DIZZY (2秒) |
| `buddy_test stats` | 打印统计数据 | 串口输出 tokens/level/mood 等 |
| `buddy_test disconnect` | 模拟 BLE 断开 | 状态→SLEEP |

**验证：** 所有命令执行后屏幕和串口输出符合预期。

---

## 后续扩展（不在本次移植范围内）

1. **自定义 GIF 角色包：** 支持通过 BLE 文件传输协议推送自定义 GIF 动画包
2. **NUS 兼容性：** 注册自定义 128-bit UUID 服务以兼容原始 Claude Desktop 连接
3. **IMU 摇晃检测：** 接入 BMI270 加速度计实现真实摇晃触发 DIZZY 状态
4. **时钟模式：** 充电时自动切换到时钟显示
5. **18 种 ASCII 角色：** 移植原始项目的 18 种 ASCII 角色到 LVGL Canvas 渲染
6. **声音反馈：** 不同状态切换时播放提示音
7. **Tuya 云联动：** 通过 Tuya IoT 平台远程查看宠物状态和统计数据

---

## 构建与烧录

```bash
# 1. 配置环境
cd /home/share/samba/tyopen/TuyaOpen
source export.sh

# 2. 编译
tos build tuya_t5_pocket_buddy

# 3. 烧录
tos flash tuya_t5_pocket_buddy

# 4. 串口调试（测试 CLI 命令）
tos monitor
```
