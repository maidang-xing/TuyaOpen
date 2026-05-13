# Tuya T5 Pocket Buddy 重构设计规格

> 日期：2026-05-12
> 状态：待实施
> 基于：参考分支 `xb/claude_buddy_pocket` + `mimiclaw/ws_server.c`

---

## 1. 项目概述

### 1.1 目标

将 T5AI-Pocket 设备（384x168 LCD, LVGL v9）改造为 Claude Code 的物理伴侣设备。设备通过 WiFi WebSocket 长连接连接 PC 端的 Claude Code CLI，实现：

1. **实时显示** Claude 会话状态、token 用量、session 列表
2. **硬件审批** 对 Claude 工具调用进行 approve/deny 操作
3. **语音输入** 通过设备端 ASR 向 Claude 注入用户消息

### 1.2 与参考实现的差异

| 维度 | 参考实现 | 本次重构 |
|------|----------|----------|
| 通信方式 | BLE NUS（Nordic UART Service） | WiFi WebSocket |
| 连接方向 | PC（BLE central）→ 设备（peripheral） | 设备（WS client）→ PC（WS server） |
| 插件语言 | Python + asyncio + bleak | Node.js / TypeScript |
| 代码结构 | UI/通信耦合（buddy_ble.c 2500行） | transport/protocol/display 三层解耦 |
| 分片机制 | 两层分片（BLE ATT + 应用层 chunk） | 无需分片（WebSocket 原生帧边界） |

### 1.3 约束

1. 所有改动集中在 `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/` 路径下
2. 注释使用英文，文档使用中文
3. UI 与通信/协议代码解耦
4. 每完成一个模块生成一个 md 文档总结

---

## 2. 系统架构

### 2.1 五层架构

```
Layer 1: Claude Code CLI
         hooks: SessionStart, UserPromptSubmit, PreToolUse, PostToolUse, Stop
         │ stdin JSON payload
         ▼
Layer 2: Hook Handler (hook_handler.js)
         fire-and-forget: curl POST → daemon
         blocking: PreToolUse → exit 0/2
         │ HTTP POST 127.0.0.1:9878/hook
         ▼
Layer 3: Node.js Daemon
         ├── hook-server.ts    (HTTP :9878, loopback-only)
         ├── hook-router.ts    (event dispatch, state aggregation)
         ├── permissions.ts    (approval bridge, 35s timeout)
         ├── ws-server.ts      (WebSocket :7681, multi-device)
         └── wire.ts           (frame encoder)
         │ WebSocket JSON frames
         ▼
Layer 4: Device Transport (buddy_ws.c)
         ├── WS client, auto-reconnect
         ├── JSON frame recv → buddy_protocol.c parse
         └── buddy_state.c → mutex-protected state
         │ buddy_tama_state_t snapshot
         ▼
Layer 5: Device UI (LVGL, screen_manager + 7 screens)
         ├── main_screen (persona + session list)
         ├── approval_screen (permission card)
         ├── session_screen / status_screen
         ├── chart_screen / pie_screen
         └── startup_screen
```

### 2.2 数据流概览

```
Claude Code ──hooks──> [Hook Server] ──route──> [Hook Router]
                                                      │
                                         aggregates sessions[], tokens,
                                         reads ~/.claude/stats-cache.json
                                                      │
                                                [WS Server :7681]
                                                      │
                                            ──ws heartbeat──>  [Device]
                                            <──ws permission── [Device]
                                            <──ws asr──────── [Device]
                                                      │
[Hook Handler] <──decision (exit code)───────────────┘
      │
Claude Code continues / aborts tool
```

---

## 3. 项目目录结构

```
apps/tuya_t5_pocket/tuya_t5_pocket_buddy/
├── CMakeLists.txt                    # 构建配置
├── Kconfig                           # 功能开关
├── app_default.config
├── config/
│   ├── TUYA_T5AI_POCKET.config       # T5AI 设备配置
│   └── TUYA_LINUX_LVGL_SIMULATOR.config  # PC 模拟器配置
│
├── include/
│   ├── tuya_config.h                 # 设备授权配置
│   ├── buddy_types.h                 # 共享数据结构定义
│   ├── buddy_protocol.h              # 协议帧类型和解析接口
│   ├── buddy_transport.h             # 通信抽象接口
│   └── app_display.h                 # 显示初始化入口
│
├── src/
│   ├── buddy_main.c                  # 主入口，Tuya IoT 初始化
│   │
│   ├── transport/                    # === 通信层 ===
│   │   ├── buddy_ws.c               # WebSocket client 实现
│   │   └── buddy_ws.h
│   │
│   ├── protocol/                     # === 协议层 ===
│   │   ├── buddy_protocol.c          # JSON 帧编解码
│   │   └── buddy_state.c             # 状态管理（mutex 保护读写）
│   │
│   ├── display/                      # === 显示层 ===
│   │   ├── CMakeLists.txt
│   │   ├── screen_manager.c          # 屏幕栈导航
│   │   ├── screen_manager.h
│   │   ├── screens/                  # 各屏幕实现
│   │   │   ├── startup_screen.c/h    # 启动画面
│   │   │   ├── main_screen.c/h       # 主界面
│   │   │   ├── approval_screen.c/h   # 审批界面
│   │   │   ├── session_screen.c/h    # Session 详情
│   │   │   ├── status_screen.c/h     # 统计仪表盘
│   │   │   ├── chart_screen.c/h      # Token 历史图
│   │   │   └── pie_screen.c/h        # 模型分布图
│   │   ├── widgets/                  # 可复用 UI 组件
│   │   │   ├── status_bar.c/h        # 顶部状态栏
│   │   │   └── led_indicator.c/h     # LED 控制
│   │   ├── persona/                  # 角色动画
│   │   │   ├── persona_registry.c/h  # 角色注册表
│   │   │   └── persona_*.c           # 18 个 ASCII 角色
│   │   └── fonts/                    # 字体资源
│   │       ├── lv_font_terminusTTF_Bold_14.c
│   │       ├── lv_font_terminusTTF_Bold_16.c
│   │       ├── lv_font_terminusTTF_Bold_18.c
│   │       └── ui_font_puhui_18_2.c
│   │
│   ├── media/                        # 媒体资源
│   │   └── media_pet.c/h
│   │
│   └── input/                        # 输入处理
│       └── buddy_indev.c             # 按键/摇杆输入
│
├── tuya_pocket_buddy_plugin/         # === Claude CLI 插件 ===
│   ├── package.json
│   ├── tsconfig.json
│   ├── .claude-plugin/
│   │   └── plugin.json               # Claude Code 插件清单
│   ├── commands/                     # 插件命令定义
│   │   ├── buddy-install.md
│   │   ├── buddy-start.md
│   │   ├── buddy-stop.md
│   │   └── buddy-status.md
│   ├── settings/
│   │   └── hooks.json                # Claude Code hooks 配置
│   ├── scripts/
│   │   └── hook_handler.js           # PreToolUse 阻塞式处理
│   └── src/
│       ├── index.ts                  # daemon 入口
│       ├── hook-server.ts            # HTTP hook 接收 (:9878)
│       ├── hook-router.ts            # Claude 事件路由 + 状态聚合
│       ├── ws-server.ts              # WebSocket server (:7681)
│       ├── permissions.ts            # 审批桥接
│       ├── wire.ts                   # 协议编解码
│       └── config.ts                 # 配置项
│
└── docs/                             # 文档
    └── 2026-05-12-buddy-refactor-design.md  # 本文档
```

**解耦原则**：

| 层 | 职责 | 依赖 |
|----|------|------|
| transport | 收发原始 JSON 字符串，管理 WS 连接生命周期 | TAL network API |
| protocol | JSON 帧解析，状态快照管理（mutex 保护） | cJSON, buddy_types.h |
| display | 读取状态快照，渲染 LVGL UI | LVGL, buddy_types.h（只读） |

---

## 4. 通信协议

### 4.1 传输层

| 项目 | 规格 |
|------|------|
| 传输协议 | WebSocket (RFC 6455), Text Frame |
| 连接方向 | 设备（client）→ PC（server） |
| URL | `ws://<host>:<port>/buddy` |
| 默认端口 | 7681 |
| 帧格式 | JSON（每个 WS frame 一个完整 JSON 对象） |
| 最大帧大小 | 8 KB（WS buffer 配置） |
| 心跳间隔 | ~10 秒 |

### 4.2 帧类型

#### PC → 设备

**Heartbeat（核心状态同步帧）**

```json
{
  "total": 5,
  "running": 2,
  "waiting": 1,
  "tokens": 12345,
  "tokens_today": 5000,
  "tokens_in": 8000,
  "tokens_in_today": 3000,
  "cache_read": 1000,
  "cache_write": 500,
  "ctx_used": 50000,
  "ctx_total": 200000,
  "model": "opus-4",
  "ver": "1.0.40",
  "cost_td": 150,
  "cost_all": 2000,
  "entries": [
    {"t": "tool", "n": "Bash", "h": "git status"},
    {"t": "done", "n": "fix-bug", "h": "completed"}
  ],
  "sessions": [
    {
      "sid": "abc123",
      "name": "fix auth bug",
      "model": "opus-4",
      "tok": 1000,
      "proj": "myapp",
      "run": true,
      "ent": ["Bash:git status", "Read:src/main.ts"]
    }
  ],
  "mstats": [
    {"m": "opus-4", "tok": 5000},
    {"m": "sonnet-4", "tok": 2000}
  ],
  "daily": [100, 200, 300, 450, 380, 500, 600, 550, 700, 800, 650, 720, 810, 900, 850, 920, 1000, 1100, 950, 1050, 1200, 1150, 1300, 1250, 1400, 1350, 1500, 1450],
  "prompt": {
    "id": "p001",
    "tool": "Bash",
    "hint": "rm -rf /"
  },
  "time": [1715500000, 480]
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| total | uint8 | 总 session 数 |
| running | uint8 | 运行中 session 数 |
| waiting | uint8 | 等待中 session 数 |
| tokens | uint32 | 累计输出 token |
| tokens_today | uint32 | 今日输出 token |
| tokens_in | uint32 | 累计输入 token |
| tokens_in_today | uint32 | 今日输入 token |
| cache_read | uint32 | 缓存读取 token |
| cache_write | uint32 | 缓存写入 token |
| ctx_used | uint32 | 当前上下文已用 |
| ctx_total | uint32 | 上下文总量 |
| model | string(15) | 当前模型名 |
| ver | string(19) | Claude 版本号 |
| cost_td | uint32 | 今日花费（微美元） |
| cost_all | uint32 | 总花费（微美元） |
| entries[] | array(8) | 最近 8 条事件（t=类型, n=名称, h=提示） |
| sessions[] | array(12) | 活跃 session 列表 |
| mstats[] | array(4) | 按模型统计 |
| daily[] | array(28) | 28 天 token 历史 |
| prompt | object/null | 待审批请求 |
| time | [epoch_s, tz_min] | 时间同步 |

**Status Query**

```json
{"cmd": "status"}
```

**Owner Set**

```json
{"cmd": "owner", "name": "maidang"}
```

#### 设备 → PC

**Permission Decision**

```json
{"cmd": "permission", "id": "p001", "decision": "once"}
```

decision 取值：`"once"` | `"always"` | `"deny"`

**ASR Text**

```json
{"cmd": "asr", "text": "帮我查一下日志", "sid": "abc123"}
```

**Heartbeat Request**

```json
{"cmd": "hb_req", "page": "main"}
```

**Status Ack**

```json
{"ack": "status", "ok": true}
```

### 4.3 连接管理

**设备侧**：
1. WiFi 就绪后从 KV 读取 `buddy_host` + `buddy_port`
2. 发起 WS 连接：`ws://host:port/buddy`
3. 握手时携带 Header：`X-Buddy-Name: Claude_XXXX`、`X-Buddy-Version: 1.0`
4. 断连后指数退避重连：5s → 10s → 20s → 40s → 60s（上限）
5. 重连成功后 PC 立即推送完整 heartbeat

**PC 侧**：
1. 启动时监听 `0.0.0.0:7681`
2. 设备连接后创建 DeviceSession
3. 支持最多 4 路并发连接
4. 同名设备重连时替换旧 session

---

## 5. 设备侧实现

### 5.1 共享数据结构（buddy_types.h）

```c
#define BUDDY_ENTRIES_RING     8
#define BUDDY_ENTRY_NAME_LEN   24
#define BUDDY_ENTRY_HINT_LEN   48
#define BUDDY_SESSIONS_MAX     12
#define BUDDY_SESSION_NAME_LEN 32
#define BUDDY_MODEL_LEN        15
#define BUDDY_MSTATS_MAX       4
#define BUDDY_DAILY_HISTORY    28

typedef struct {
    char type;                              // 't'=tool, 'd'=done, 'e'=error
    char name[BUDDY_ENTRY_NAME_LEN + 1];
    char hint[BUDDY_ENTRY_HINT_LEN + 1];
} buddy_entry_t;

typedef struct {
    char    sid[16];
    char    name[BUDDY_SESSION_NAME_LEN + 1];
    char    model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens;
    char    project[16];
    bool    is_running;
    char    local_entries[4][BUDDY_ENTRY_NAME_LEN + 1];
    uint8_t local_entries_count;
} buddy_session_t;

typedef struct {
    char     model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens;
} buddy_mstat_t;

typedef enum {
    PERSONA_SLEEP = 0,
    PERSONA_IDLE,
    PERSONA_BUSY,
    PERSONA_ATTENTION,
    PERSONA_CELEBRATE,
    PERSONA_HEART,
    PERSONA_DIZZY,
} buddy_persona_state_e;

typedef enum {
    LED_OFF = 0,
    LED_ON_DIM,
    LED_BLINK_SLOW,
    LED_BLINK_FAST,
    LED_FLASH_ONCE,
} buddy_led_state_e;

typedef struct {
    // Connection
    bool     ws_connected;

    // Session counts
    uint8_t  sessions_total;
    uint8_t  sessions_running;
    uint8_t  sessions_waiting;

    // Token accounting
    uint32_t tokens;
    uint32_t tokens_today;
    uint32_t tokens_in;
    uint32_t tokens_in_today;
    uint32_t cache_read;
    uint32_t cache_write;
    uint32_t ctx_used;
    uint32_t ctx_total;

    // Status
    char     msg[64];
    char     owner_name[32];
    char     model[BUDDY_MODEL_LEN + 1];
    char     claude_version[20];
    uint32_t cost_today_ucc;
    uint32_t cost_total_ucc;

    // Pending approval
    bool     has_prompt;
    char     prompt_id[40];
    char     prompt_tool[32];
    char     prompt_hint[64];

    // Entries ring buffer
    buddy_entry_t entries[BUDDY_ENTRIES_RING];
    uint8_t       entries_count;
    uint8_t       entries_head;

    // Sessions
    buddy_session_t sessions[BUDDY_SESSIONS_MAX];
    uint8_t         sessions_count;

    // Per-model stats
    buddy_mstat_t mstats[BUDDY_MSTATS_MAX];
    uint8_t       mstats_count;

    // Daily token history (28 days)
    uint32_t daily_tokens[BUDDY_DAILY_HISTORY];

    // Wall clock (from time sync)
    int64_t  wall_epoch_s;
    int16_t  wall_tz_min;
    uint64_t wall_local_ms_at_rx;

    // UI-derived state
    uint8_t               persona_id;
    buddy_persona_state_e persona_state;
    buddy_led_state_e     led_state;
} buddy_tama_state_t;
```

### 5.2 通信层（transport/buddy_ws.c）

**公共接口**：

```c
OPERATE_RET buddy_ws_init(void);
OPERATE_RET buddy_ws_start(void);
OPERATE_RET buddy_ws_stop(void);
bool        buddy_ws_is_connected(void);

// Send frames to PC
OPERATE_RET buddy_ws_send_permission(const char *id, const char *decision);
OPERATE_RET buddy_ws_send_asr(const char *text, const char *sid);
OPERATE_RET buddy_ws_send_hb_req(const char *page);
OPERATE_RET buddy_ws_send_ack(const char *cmd);
```

**实现要点**：
- 使用 TAL network API（`tal_net_*`）建立 TCP 连接
- 自行实现 WS 握手（参考 `mimiclaw/ws_server.c` 的 `ws_build_accept_key` 等函数，改为 client 方向）
- WS frame encode/decode（opcode 0x1=text, 0x8=close, 0x9=ping, 0xA=pong）
- client 发送帧需要 masking（server 不需要）
- 接收线程独立运行，收到完整 text frame 后调用 `buddy_protocol_on_recv(json_str)`
- 自动重连线程：指数退避 5s → 60s

**IP 配置 CLI**：

```
buddy ws set 192.168.1.100         # default port 7681
buddy ws set 192.168.1.100 8080    # custom port
buddy ws status                     # show connection info
```

通过 `tal_kv_set/get` 持久化 `buddy_ws_host` 和 `buddy_ws_port`。

### 5.3 协议层（protocol/）

**buddy_protocol.c**：
- `buddy_protocol_on_recv(const char *json)` — 解析收到的 JSON 帧
- 根据帧内容类型分派：heartbeat → `buddy_state_update_from_heartbeat(cJSON *)`
- 时间同步 → `buddy_state_update_time(epoch, tz_min)`
- Owner → `buddy_state_set_owner(name)`

**buddy_state.c**：
- 内部持有 `static buddy_tama_state_t s_state` + `MUTEX_HANDLE s_mutex`
- `buddy_state_snapshot(buddy_tama_state_t *out)` — mutex 保护下的原子拷贝
- `buddy_state_set_connected(bool)` — 更新连接状态
- UI 层通过 `buddy_state_snapshot()` 获取只读快照

### 5.4 显示层

#### screen_manager

栈式屏幕管理（最大深度 6），每个 screen 注册 `init/deinit` 回调：

```c
typedef struct {
    void (*init)(lv_obj_t *parent);
    void (*deinit)(void);
    lv_obj_t *screen;
} buddy_screen_t;

void screen_manager_init(void);
void screen_manager_push(buddy_screen_t *screen);
void screen_manager_pop(void);
buddy_screen_t *screen_manager_current(void);
```

#### 屏幕列表

| 屏幕 | 文件 | 布局 |
|------|------|------|
| startup | startup_screen.c | Logo + 版本号，2s 后自动跳转 main |
| main | main_screen.c | 左(144px): persona canvas + name + level; 右(238px): session list; 顶(20px): header; 底(6px): nav tabs |
| approval | approval_screen.c | 全屏 permission card: 标题 + tool + hint + 3 个选项（once/always/deny） |
| session | session_screen.c | Session 详情：name, model, tokens, entries, context bar |
| status | status_screen.c | 8 行统计：version, model, owner, sessions, today tokens/cost, total, cache, context |
| chart | chart_screen.c | 28 天 token 柱状图 |
| pie | pie_screen.c | 模型 token 占比分布图 |

#### 屏幕导航

```
startup → main
            ├── LEFT  → status
            ├── RIGHT → chart
            ├── DOWN  → pie
            ├── ENTER → session (当前选中)
            └── (auto) → approval (当 has_prompt=true)

status ← LEFT/ESC → main
chart  ← LEFT/ESC → main
pie    ← LEFT/ESC → main
session ← ESC → main
approval → (decision sent) → main
```

#### Persona 状态派生

main_screen 中根据 `buddy_tama_state_t` 派生 persona 状态：

```
if (!ws_connected)            → SLEEP
else if (has_prompt)          → ATTENTION
else if (sessions_running > 0) → BUSY
else if (recently_completed)   → CELEBRATE (3s transient)
else if (quick_approval)       → HEART (2s transient)
else if (error_state)          → DIZZY (2s transient)
else                           → IDLE
```

18 个 ASCII 角色共享相同的状态动画（不同 ASCII art，相同帧时序）。

---

## 6. 插件侧实现（Node.js/TypeScript）

### 6.1 模块架构

```
index.ts (daemon main)
  ├── HookServer (:9878)     接收 Claude Code hook POST
  ├── HookRouter             事件路由 + 状态聚合
  ├── PermissionBridge       审批桥接（35s timeout）
  └── WsServer (:7681)       WebSocket 多设备管理
        └── DeviceSession × N
```

### 6.2 hook-server.ts

- HTTP server 监听 `127.0.0.1:9878`
- 仅接受 loopback 请求（安全校验）
- POST `/hook` 接收 Claude Code hook payload（JSON）
- 最大 payload 64 KB
- 路由到 `HookRouter.route(eventName, payload)`

### 6.3 hook-router.ts

核心状态聚合模块，职责：

1. **Session 跟踪**：
   - `SessionStart` → 创建 SessionInfo（sid, cwd, model）
   - `UserPromptSubmit` → 更新 session（first prompt = name）
   - `PostToolUse` → 追加 tool call 到 local_entries
   - `Stop` → 记录 tokens_out，标记完成

2. **统计聚合**：
   - 读取 `~/.claude/stats-cache.json` → daily tokens, costs, model usage
   - 扫描 `~/.claude/projects/` → 历史 session 信息
   - 每 10s 构造并推送 heartbeat

3. **Heartbeat 构造**：
   ```typescript
   interface Heartbeat {
     total: number;
     running: number;
     waiting: number;
     tokens: number;
     tokens_today: number;
     tokens_in: number;
     tokens_in_today: number;
     cache_read: number;
     cache_write: number;
     ctx_used: number;
     ctx_total: number;
     model: string;
     ver: string;
     cost_td: number;
     cost_all: number;
     entries: Entry[];
     sessions: SessionInfo[];
     mstats: ModelStat[];
     daily: number[];
     prompt?: PromptInfo;
     time: [number, number];
   }
   ```

### 6.4 ws-server.ts

- 使用 `ws` npm 包监听 `0.0.0.0:7681`
- 每个连接创建 `DeviceSession`：
  - 记录 `X-Buddy-Name` header
  - 连接后立即推送完整 heartbeat
  - 接收设备帧并路由

- 消息路由：
  | 设备帧 cmd | 处理 |
  |-----------|------|
  | permission | `PermissionBridge.resolve(id, decision)` |
  | asr | 注入 Claude 当前会话（如果可用） |
  | hb_req | 立即推送最新 heartbeat |
  | ack | 更新设备在线状态 |

- 广播：`broadcast(frame)` 向所有已连接设备发送

### 6.5 permissions.ts

- `create(promptId, tool, hint)` → 创建 pending prompt + Promise
- `waitForApproval(promptId, timeout=35s)` → 等待设备决策
- `resolve(promptId, decision)` → 从 WS 收到决策后 resolve
- 超时自动 deny
- 同一时刻只有一个 pending prompt（新 prompt 覆盖旧 prompt）

### 6.6 hooks.json

```json
{
  "hooks": {
    "SessionStart": [{
      "type": "command",
      "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
    }],
    "UserPromptSubmit": [{
      "type": "command",
      "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
    }],
    "PostToolUse": [{
      "type": "command",
      "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
    }],
    "Stop": [{
      "type": "command",
      "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
    }],
    "PreToolUse": [{
      "type": "command",
      "command": "node tuya_pocket_buddy_plugin/scripts/hook_handler.js"
    }]
  }
}
```

### 6.7 hook_handler.js

PreToolUse 的阻塞式处理脚本：

1. 从 stdin 读取 hook payload
2. POST 到 `127.0.0.1:9878/hook`（包含 event_name: "PreToolUse"）
3. 等待 daemon 返回决策（最长 40s）
4. `exit 0` = approve, `exit 2` = deny

### 6.8 配置项（config.ts）

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| hookPort | 9878 | HTTP hook 监听端口 |
| wsPort | 7681 | WebSocket 监听端口 |
| maxDevices | 4 | 最大并发设备连接 |
| permissionTimeout | 35000 | 审批超时（毫秒） |
| heartbeatInterval | 10000 | 心跳推送间隔（毫秒） |
| timeSyncInterval | 30000 | 时间同步间隔（毫秒） |

---

## 7. 审批流程（端到端）

```
1. Claude Code 触发工具调用
2. hooks.json → PreToolUse → hook_handler.js 启动
3. hook_handler.js POST payload → daemon :9878/hook
4. HookRouter 收到 PreToolUse 事件
5. PermissionBridge.create(prompt_id, tool, hint)
6. HookRouter 构造带 prompt 字段的 heartbeat
7. WsServer.broadcast(heartbeat) → 所有设备
8. 设备收到 heartbeat，buddy_state 更新 has_prompt=true
9. main_screen 检测到 has_prompt，自动 push approval_screen
10. 用户按键：ENTER=once, RIGHT=always, LEFT=deny
11. buddy_ws_send_permission(id, decision) → WS → PC
12. WsServer 收到 permission 帧 → PermissionBridge.resolve(id, decision)
13. hook_handler.js 收到响应 → exit 0 (approve) 或 exit 2 (deny)
14. Claude Code 继续执行或中止工具调用
15. HookRouter 下一次 heartbeat 不再包含 prompt 字段
16. 设备 approval_screen pop，回到 main_screen
```

超时处理：35 秒未收到设备决策 → 自动 deny → hook_handler.js exit 2。

---

## 8. ASR 流程

```
1. 用户在设备上按下录音键
2. 设备端 ASR 模块进行语音识别
3. 识别完成后发送帧：{"cmd":"asr","text":"...","sid":"<current_session>"}
4. WsServer 收到 asr 帧
5. HookRouter 校验 sid（匹配当前活跃 session）
6. 将 text 作为用户输入注入 Claude（通过 CLI stdin 或 API）
7. 可选：返回 {"ack":"asr","ok":true} 给设备
```

注：ASR 功能依赖设备端 ASR SDK 可用性，协议已预留。

---

## 9. 实施里程碑

### M1：基础框架

| 任务 | 文件 |
|------|------|
| 项目骨架（CMakeLists.txt, Kconfig, configs） | CMakeLists.txt, Kconfig, config/*.config |
| 共享类型定义 | include/buddy_types.h |
| 通信抽象接口 | include/buddy_transport.h |
| 协议接口定义 | include/buddy_protocol.h |
| 主入口 | src/buddy_main.c |
| 输入处理 | src/input/buddy_indev.c |

**产出文档**：`docs/m1-framework.md`

### M2：通信层

| 任务 | 文件 |
|------|------|
| WebSocket client 实现 | src/transport/buddy_ws.c/h |
| JSON 帧解析 | src/protocol/buddy_protocol.c |
| 状态管理 | src/protocol/buddy_state.c |
| CLI 命令（buddy ws set/status） | buddy_main.c 中注册 |

**产出文档**：`docs/m2-transport.md`

### M3：UI 框架

| 任务 | 文件 |
|------|------|
| 屏幕管理器 | src/display/screen_manager.c/h |
| 启动画面 | src/display/screens/startup_screen.c/h |
| 主界面（persona + session list） | src/display/screens/main_screen.c/h |
| 状态栏 | src/display/widgets/status_bar.c/h |
| LED 控制 | src/display/widgets/led_indicator.c/h |
| 字体资源 | src/display/fonts/*.c |

**产出文档**：`docs/m3-ui-framework.md`

### M4：审批流程

| 任务 | 文件 |
|------|------|
| 审批界面 | src/display/screens/approval_screen.c/h |
| 设备端 permission 帧发送 | transport/buddy_ws.c 扩展 |

**产出文档**：`docs/m4-approval.md`

### M5：完整 UI

| 任务 | 文件 |
|------|------|
| Session 详情 | src/display/screens/session_screen.c/h |
| 统计仪表盘 | src/display/screens/status_screen.c/h |
| Token 历史图 | src/display/screens/chart_screen.c/h |
| 模型分布图 | src/display/screens/pie_screen.c/h |

**产出文档**：`docs/m5-full-ui.md`

### M6：Persona 动画

| 任务 | 文件 |
|------|------|
| 角色注册表 | src/display/persona/persona_registry.c/h |
| 18 个 ASCII 角色 | src/display/persona/persona_*.c |

**产出文档**：`docs/m6-persona.md`

### M7：Claude CLI 插件

| 任务 | 文件 |
|------|------|
| 项目初始化（package.json, tsconfig） | tuya_pocket_buddy_plugin/ |
| Plugin 清单 | .claude-plugin/plugin.json |
| Hook 配置 | settings/hooks.json |
| Hook 处理脚本 | scripts/hook_handler.js |
| HTTP Hook Server | src/hook-server.ts |
| 事件路由 + 状态聚合 | src/hook-router.ts |
| WebSocket Server | src/ws-server.ts |
| 审批桥接 | src/permissions.ts |
| 协议编码 | src/wire.ts |
| 配置管理 | src/config.ts |
| Daemon 入口 | src/index.ts |
| 命令定义 | commands/*.md |

**产出文档**：`docs/m7-plugin.md`

### M8：ASR 语音输入

| 任务 | 文件 |
|------|------|
| 设备端 ASR 集成 | src/media/media_pet.c 或新增 |
| ASR 帧发送 | transport/buddy_ws.c 扩展 |
| 插件端 ASR 接收 | src/hook-router.ts 扩展 |

**产出文档**：`docs/m8-asr.md`

### M9：联调与验证

| 任务 | 验收标准 |
|------|----------|
| 端到端连接 | 设备输入 IP 后自动连接，显示 heartbeat 数据 |
| 审批流程 | approve/deny/always 三种决策正确传递 |
| ASR 注入 | 语音文本成功注入 Claude session |
| 断连重连 | 循环断网 20 次，每次自动恢复 |
| 多设备 | 2 台设备同时连接，各自独立工作 |

**产出文档**：`docs/m9-integration.md`

---

## 10. 可行性评估

| 维度 | 评估 | 风险等级 | 说明 |
|------|------|----------|------|
| 设备侧 WS Client | 可行 | 低 | `mimiclaw/ws_server.c` 验证了 WS 帧处理在 TuyaOpen 平台可用，client 方向实现类似 |
| 协议迁移 | 可行 | 低 | 沿用参考实现的 JSON 帧格式，去掉 chunk 层反而更简单 |
| UI 复现 | 可行 | 低 | 参考实现 UI 代码成熟，解耦后复用 |
| 插件 TS 重写 | 可行 | 中 | 参考 Python daemon ~2500 行，逻辑清晰但需要完整重写 |
| ASR 集成 | 可行 | 中-高 | 取决于设备端 ASR SDK 可用性，协议已预留 |
| 资源优化 | 改善 | - | 去掉 NimBLE 栈节省 ~16KB RTOS 栈，无需 chunk 分片 |

**总体结论**：完全可行。核心技术点（WS 通信、LVGL UI、Claude hooks）均有成熟参考实现，主要风险在 ASR 集成和联调稳定性上。

---

## 11. 约束与风险

| 约束/风险 | 说明 | 缓解措施 |
|-----------|------|----------|
| 同一局域网要求 | 设备和电脑须在同一 WiFi 网络 | 设备已接 WiFi（MQTT 条件），通常自然满足 |
| 电脑 IP 变化 | DHCP 重新分配导致重连失败 | 建议静态 IP；后续可用 mDNS 自动发现 |
| 无传输加密 | `ws://` 明文，局域网内可嗅探 | 局域网可接受；敏感场景升级 `wss://`（需 mbedtls） |
| 审批超时 | 设备下线后挂起审批无法完成 | 35 秒超时自动 deny |
| 端口冲突 | 7681 已被占用 | 配置文件支持修改端口 |
| ASR SDK 依赖 | 设备端 ASR 能力未确认 | 协议预留，模块可独立开发 |
