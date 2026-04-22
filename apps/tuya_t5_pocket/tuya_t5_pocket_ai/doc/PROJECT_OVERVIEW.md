# Claude Desktop Buddy - 项目介绍

一款基于 ESP32 (M5StickC Plus) 的桌面宠物固件，通过 BLE（低功耗蓝牙）与 Claude 桌面应用（macOS / Windows）实时连接，将 Claude 的工作状态映射为可爱的动画角色。用户可以在硬件设备上直接审批权限请求、查看会话摘要，并通过互动积累统计数据与等级。

github: https://github.com/maidang-xing/claude-desktop-buddy.git

---

## 整体架构

```mermaid
graph TB
    subgraph Desktop["Claude 桌面应用 (macOS / Windows)"]
        DM[开发者模式]
        HBW[Hardware Buddy 窗口]
        DM --> HBW
    end

    subgraph BLE["BLE 通信层"]
        NUART["Nordic UART Service<br/>6e400001-b5a3-..."]
        RX["RX 特征 (6e400002)<br/>设备 → 桌面"]
        TX["TX 特征 (6e400003)<br/>桌面 → 设备"]
        NUART --- RX
        NUART --- TX
    end

    subgraph Device["ESP32 M5StickC Plus"]
        BLEBridge["BLE Bridge<br/>连接管理 / 配对 / 收发"]
        DataParser["Data Parser<br/>JSON 解析 / 时间同步"]
        StateMachine["状态机<br/>derive(TamaState) → PersonaState"]
        Renderer["渲染引擎<br/>60fps Sprite → LCD"]
        Input["输入处理<br/>按钮 / IMU / 摇晃"]
        Stats["统计系统<br/>NVS 持久化"]
        Xfer["文件传输<br/>角色包安装"]

        BLEBridge --> DataParser
        DataParser --> StateMachine
        StateMachine --> Renderer
        Input --> StateMachine
        Input --> Stats
        Xfer --> FS["LittleFS<br/>角色 GIF 存储"]

        subgraph AnimEngine["动画引擎"]
            ASCII["ASCII 模式<br/>18 种物种"]
            GIF["GIF 模式<br/>自定义角色包"]
        end

        StateMachine --> AnimEngine
        AnimEngine --> Renderer
        FS --> GIF
    end

    HBW <-->|JSON over BLE| NUART
    NUART <--> BLEBridge
```

---

## 数据流

桌面端与设备之间通过 BLE Nordic UART Service 以 JSON 行协议通信，支持双向数据交换：

```mermaid
sequenceDiagram
    participant D as Claude 桌面应用
    participant B as BLE 通道
    participant E as ESP32 设备

    Note over D,E: 连接与配对
    D->>B: 发起 BLE 连接
    B->>E: 配对请求 (LE Secure Connections)
    E->>E: 屏幕显示 6 位配对码
    E->>B: 配对确认
    B->>D: 连接建立

    Note over D,E: 心跳同步 (每 10s)
    D->>E: {"total":3, "running":1, "waiting":0,<br/>"msg":"Working...", "tokens":12000, ...}
    E->>E: 解析 → 更新 TamaState → 推导 PersonaState
    E->>E: 角色动画切换 (idle/busy/...)

    Note over D,E: 权限审批
    D->>E: {"prompt":{"id":"abc", "tool":"Read",<br/>"hint":"src/main.cpp"}}
    E->>E: 进入 attention 状态, LED 闪烁
    E->>D: {"cmd":"permission", "id":"abc",<br/>"decision":"once"}

    Note over D,E: 角色包推送
    D->>E: {"folder":"bufo", "files":["manifest.json",...]}
    D->>E: {"file":"manifest.json", "chunk":"base64...",<br/>"final":true}
    E->>D: {"ack":"file", "ok":true, "n":1234}
    D->>E: {"done":true}
    E->>E: 切换到 GIF 模式
```

---

## 状态机

设备根据桌面端推送的会话信息，推导出 7 种情绪状态，驱动角色动画：

```mermaid
stateDiagram-v2
    [*] --> sleep : 未连接桌面

    sleep --> idle : BLE 连接建立
    idle --> busy : 有会话正在运行
    idle --> attention : 审批请求到达
    busy --> idle : 所有会话结束
    busy --> attention : 审批请求到达
    attention --> heart : 5 秒内批准
    attention --> idle : 批准 (>5s) 或拒绝
    heart --> idle : 2 秒后

    idle --> celebrate : 升级 (每 50K tokens)
    busy --> celebrate : 升级 (每 50K tokens)
    celebrate --> idle : 3 秒后

    idle --> dizzy : 摇晃设备
    busy --> dizzy : 摇晃设备
    dizzy --> idle : 2 秒后

    idle --> sleep : 30 秒无交互 / 面朝下
    busy --> sleep : BLE 断开

    note right of sleep : 眼睛闭合, 缓慢呼吸
    note right of idle : 眨眼, 四处张望
    note right of busy : 冒汗, 忙碌工作
    note right of attention : 警觉, LED 闪红
    note right of celebrate : 彩带, 弹跳
    note right of dizzy : 螺旋眼, 摇晃
    note right of heart : 漂浮爱心
```

---

## 核心模块

```mermaid
graph LR
    subgraph 固件源码 ["src/"]
        main["main.cpp<br/>主循环 / 状态机 / UI"]
        ble["ble_bridge.h<br/>BLE 通信"]
        data["data.h<br/>JSON 解析"]
        stats["stats.h<br/>统计与持久化"]
        buddy["buddy.cpp<br/>ASCII 角色分发"]
        char["character.cpp<br/>GIF 解码渲染"]
        xfer["xfer.h<br/>文件传输协议"]
    end

    subgraph 角色库 ["src/buddies/"]
        b1["capybara.cpp"]
        b2["duck.cpp"]
        b3["cat.cpp"]
        b4["...共 18 种"]
    end

    subgraph 工具 ["tools/"]
        t1["prep_character.py<br/>GIF 预处理"]
        t2["flash_character.py<br/>USB 刷写角色"]
    end

    main --> ble
    main --> data
    main --> stats
    main --> buddy
    main --> char
    main --> xfer
    buddy --> b1
    buddy --> b2
    buddy --> b3
    buddy --> b4
    t1 -.->|生成| charpack["characters/<br/>角色 GIF 包"]
    t2 -.->|刷写| charpack
    charpack -.->|LittleFS| char
```

---

## 18 种 ASCII 角色

每种角色都实现了 7 种情绪状态的独立 ASCII 动画：

| 序号 | 角色 | 英文名 | 特色描述 |
|------|------|--------|----------|
| 1 | 水豚 | Capybara | 侧躺睡觉、打哈欠 |
| 2 | 鸭子 | Duck | 摇摆走路 |
| 3 | 鹅 | Goose | 张嘴嘎嘎叫 |
| 4 | 果冻 | Blob | Q 弹变形 |
| 5 | 猫 | Cat | 转头、竖耳 |
| 6 | 龙 | Dragon | 喷火、展翅 |
| 7 | 章鱼 | Octopus | 触手飘动 |
| 8 | 猫头鹰 | Owl | 转头、眨眼 |
| 9 | 企鹅 | Penguin | 扇翅、滑行 |
| 10 | 乌龟 | Turtle | 缩壳 |
| 11 | 蜗牛 | Snail | 缓慢移动 |
| 12 | 幽灵 | Ghost | 飘浮、隐现 |
| 13 | 蝾螈 | Axolotl | 鳃须飘动 |
| 14 | 仙人掌 | Cactus | 开花 |
| 15 | 机器人 | Robot | 天线闪烁 |
| 16 | 兔子 | Rabbit | 耳朵竖起 |
| 17 | 蘑菇 | Mushroom | 孢子飘散 |
| 18 | 胖猫 | Chonk | 圆滚滚的 |

---

## 统计与成长系统

```mermaid
graph TD
    subgraph 输入
        A[审批操作] -->|记录响应时间| V[速度环形缓冲区]
        A -->|计数| AC[审批/拒绝次数]
        T[Token 消耗] -->|每 50K 升 1 级| L[等级系统]
        N[面朝下休眠] -->|累计时长| E[精力值]
    end

    subgraph 计算
        V --> M[中位响应速度]
        M --> MT[心情等级 0~4]
        AC -->|拒绝率惩罚| MT
        T --> F[饱食度<br/>5K tokens = 1 格]
        L -->|升级| C[celebrate 动画]
    end

    subgraph 持久化 ["NVS 存储"]
        AC --> NVS[(Preferences<br/>namespace: buddy)]
        L --> NVS
        E --> NVS
        MT --> NVS
    end

    subgraph 显示
        MT --> PET[宠物状态页]
        F --> PET
        E --> PET
        L --> PET
        AC --> PET
    end
```

**关键机制：**

- **等级**：每消耗 50,000 tokens 升 1 级，触发 celebrate 动画
- **心情**：基于审批响应速度中位数（越快心情越好），高拒绝率会降低心情
- **精力**：将设备面朝下进入"午睡"模式，累计休眠时长恢复精力条
- **饱食度**：由 token 消耗驱动，每 5,000 tokens 增加 1 格

---

## 显示模式与交互

设备提供多种显示界面，通过按钮切换：

```mermaid
graph LR
    Normal["主页<br/>角色 + HUD"] -->|"A 键"| Pet["宠物页<br/>心情/饱食/精力/等级"]
    Pet -->|"A 键"| Info["信息页<br/>6 页循环"]
    Info -->|"A 键"| Normal

    Normal -->|"长按 A"| Menu["菜单"]
    Pet -->|"长按 A"| Menu
    Info -->|"长按 A"| Menu

    Menu --> S1["亮度调节"]
    Menu --> S2["声音开关"]
    Menu --> S3["LED 开关"]
    Menu --> S4["HUD 开关"]
    Menu --> S5["切换角色"]
    Menu --> S6["删除角色包"]
    Menu --> S7["恢复出厂"]

    Normal -->|"审批到达"| Approval["审批界面<br/>A=批准 B=拒绝"]
    Approval -->|"操作完成"| Normal

    Normal -->|"充电中"| Clock["时钟模式<br/>竖屏/横屏"]
```

---

## 技术栈

```mermaid
mindmap
  root((Claude Desktop Buddy))
    硬件
      ESP32 微控制器
      M5StickC Plus
      135x240 TFT LCD
      6 轴 IMU 加速度计
      BLE 5.0
      AXP192 电源管理
    固件
      C++ / Arduino 框架
      PlatformIO 构建
      160 MHz CPU
      LittleFS 文件系统
      NVS 键值存储
    通信协议
      BLE Nordic UART Service
      JSON 行协议
      LE Secure Connections
      Base64 文件传输
    依赖库
      M5StickCPlus
      AnimatedGIF 2.1.1
      ArduinoJson 7.0.0
    工具链
      Python 3
      Pillow 图像处理
      gifsicle GIF 优化
```

---

## 快速上手

```mermaid
graph TD
    A[安装 PlatformIO] --> B["pio run -t upload<br/>刷写固件"]
    B --> C[设备启动<br/>显示 ASCII 角色]
    C --> D["Claude 桌面应用<br/>启用开发者模式"]
    D --> E["Developer →<br/>Open Hardware Buddy"]
    E --> F["点击 Connect<br/>选择 Claude-XXXX"]
    F --> G[输入 6 位配对码]
    G --> H[连接成功!]

    H --> I{想用自定义角色?}
    I -->|是| J["准备 GIF 角色包<br/>tools/prep_character.py"]
    J --> K["拖拽到 Hardware Buddy 窗口"]
    K --> L[设备切换到 GIF 模式]
    I -->|否| M["菜单 → next pet<br/>切换 18 种 ASCII 角色"]
```

---

## 项目目录结构

```
claude-desktop-buddy/
├── src/                          # 固件源码
│   ├── main.cpp                  # 主循环、状态机、UI 渲染 (1266 行)
│   ├── buddy.cpp / buddy.h       # ASCII 角色分发与渲染
│   ├── buddy_common.h            # 共享渲染工具、物种注册表
│   ├── ble_bridge.h              # BLE Nordic UART 通信
│   ├── data.h                    # JSON 解析、TamaState 数据结构
│   ├── stats.h                   # NVS 统计、等级、心情计算
│   ├── xfer.h                    # BLE 文件传输协议
│   ├── character.h / character.cpp  # GIF 角色加载与渲染
│   └── buddies/                  # 18 种 ASCII 角色实现
│       ├── capybara.cpp
│       ├── duck.cpp, cat.cpp, dragon.cpp ...
│       └── (每个约 150~200 行)
│
├── characters/                   # 示例 GIF 角色包
│   └── bufo/                     # bufo 蟾蜍角色
│       ├── manifest.json         # 角色配置 (颜色、状态映射)
│       └── *.gif                 # 各状态 GIF 动画
│
├── tools/                        # Python 工具
│   ├── prep_character.py         # GIF 预处理 (缩放、裁剪、优化)
│   └── flash_character.py        # USB 直接刷写角色包
│
├── docs/                         # 文档与截图
├── platformio.ini                # 构建配置
├── REFERENCE.md                  # BLE 线路协议完整规范
└── CONTRIBUTING.md               # 贡献指南
```

---

## 总结

Claude Desktop Buddy 是一个将 AI 工作流具象化的硬件项目。它通过 BLE 协议将 Claude 桌面应用的实时状态传递到一个小巧的 ESP32 设备上，让一个可爱的桌面宠物反映你的编程节奏 —— 忙碌时冒汗、等待审批时闪灯提醒、快速批准时飘出爱心。18 种内置 ASCII 角色 + 自定义 GIF 角色包，配合统计成长系统，让开发过程多了一份趣味与陪伴。
