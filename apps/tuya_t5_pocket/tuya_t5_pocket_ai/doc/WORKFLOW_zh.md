# Claude Buddy 端到端工作流说明

> 目标目录：`apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc`
>
> 本文说明当前项目里 **Claude hook → claude-cli plugin/daemon → 本地 HTTP → BLE → T5AI-Pocket UI** 的完整工作流程，重点帮助开发者理解各模块如何协作，以及出现问题时该去哪里排查。
>
> 相关文档：
> - `doc/UI_INTERACTION_zh.md`
> - `doc/PLAN_zh.md`
> - `apps/tuya_t5_pocket/docs/protocol/BLE_WIRE_PROTOCOL_zh.md`
> - `apps/tuya_t5_pocket/claude-cli-plugin/daemon/README.md`

---

## 1. 整体架构

整个链路分成 5 层：

```text
┌──────────────────────────────────────────────────────────────┐
│ 1. Claude Code CLI                                           │
│    - 触发 SessionStart / UserPromptSubmit / PreToolUse / ... │
└──────────────────────┬───────────────────────────────────────┘
                       │ stdin JSON payload
                       ▼
┌──────────────────────────────────────────────────────────────┐
│ 2. Claude hooks                                              │
│    - hooks.json 定义事件                                     │
│    - hook_handler.py 处理 PreToolUse 阻塞审批                │
│    - 其它事件用 curl 直投本地 daemon                         │
└──────────────────────┬───────────────────────────────────────┘
                       │ HTTP POST 127.0.0.1:9878/hook
                       ▼
┌──────────────────────────────────────────────────────────────┐
│ 3. claude-cli plugin / daemon                                │
│    - hook_server.py: 本地 HTTP 入口                          │
│    - hook_router.py: 事件分发、状态更新、审批桥接            │
│    - permissions.py: 等待设备按键审批                        │
│    - wire.py: JSONL 帧编码                                   │
│    - ble_client.py: BLE 中央设备，发往 T5AI-Pocket           │
└──────────────────────┬───────────────────────────────────────┘
                       │ NUS / BLE JSON lines
                       ▼
┌──────────────────────────────────────────────────────────────┐
│ 4. T5AI-Pocket 设备端 BLE 桥                                 │
│    - buddy_ble.c / buddy_ble.h                               │
│    - 接收心跳、prompt、time、cmd 等帧                        │
│    - 更新共享状态 buddy_tama_state_t                         │
└──────────────────────┬───────────────────────────────────────┘
                       │ state snapshot
                       ▼
┌──────────────────────────────────────────────────────────────┐
│ 5. T5AI-Pocket UI                                            │
│    - buddy_main_screen / approval / status / chart / pie     │
│    - screen_manager 管理页面切换                             │
│    - 用户按键后可通过 BLE 回传 permission/status 请求         │
└──────────────────────────────────────────────────────────────┘
```

可以把它理解成：

- **Claude Code** 负责产生事件。
- **hook** 负责把事件送到本地 daemon。
- **daemon** 负责把 Claude 侧事件转成设备能理解的 BLE JSON 帧。
- **设备端 BLE 模块** 负责解析这些帧并更新共享状态。
- **UI 层** 只消费状态并渲染。

这种设计把“Claude 侧逻辑”和“设备 UI 逻辑”解耦了。

---

## 2. 第一层：Claude hook 是入口

### 2.1 hook 配置位置

Claude hook 定义在：

- `apps/tuya_t5_pocket/claude-cli-plugin/settings/hooks.json`

当前使用了 5 个事件：

- `SessionStart`
- `UserPromptSubmit`
- `PreToolUse`
- `PostToolUse`
- `Stop`

对应实现可以直接看 `hooks.json:20` 开始。

### 2.2 两种 hook 触发模式

#### 模式 A：fire-and-forget

下面 4 个事件都走同一种方式：

- `SessionStart`
- `UserPromptSubmit`
- `PostToolUse`
- `Stop`

它们直接执行：

```bash
curl -sS --max-time 3 -X POST --data-binary @- http://127.0.0.1:9878/hook
```

特点：

- Claude Code 把 hook payload 从 stdin 提供出来。
- `curl --data-binary @-` 原样读取 stdin 并 POST 到本地 daemon。
- 最长等 3 秒。
- daemon 不在时，回落为 `{}`，不会阻塞 Claude 正常工作。

这类事件本质上是“状态同步通知”。

#### 模式 B：阻塞审批

`PreToolUse` 不是直接 curl，而是走：

- `apps/tuya_t5_pocket/claude-cli-plugin/scripts/hook_handler.py`

原因很关键：

- `PreToolUse` 需要**真的阻止或放行工具调用**。
- 这要求 hook 脚本必须根据设备端审批结果返回不同退出码。

`hook_handler.py:1` 的逻辑是：

1. 从 stdin 读 Claude Code 的 JSON payload。
2. POST 到 `http://127.0.0.1:9878/hook`。
3. 等 daemon 的 JSON 回复。
4. 如果回复 `{"decision":"deny"}` 或 `{"decision":"block"}`，脚本退出码返回 `2`。
5. 否则返回 `0`。

也就是：

- `exit 0` = 放行工具调用
- `exit 2` = 拦截工具调用

这就是设备按键审批能真正影响 Claude Code 的根本原因。

---

## 3. 第二层：本地 HTTP 是 hook 和 daemon 的桥

### 3.1 HTTP 服务位置

本地 HTTP 服务实现位于：

- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/hook_server.py`

关键常量：

- `BIND_HOST = "127.0.0.1"`
- `DEFAULT_PORT = 9878`
- `HOOK_PATH = "/hook"`

因此 hook 的统一入口就是：

```text
http://127.0.0.1:9878/hook
```

### 3.2 为什么要加这一层 HTTP

原因有三个：

1. **把 Claude hook 和 BLE 守护进程解耦**：hook 不直接碰 BLE。
2. **统一事件入口**：所有 hook 都只管 POST JSON。
3. **便于本地安全控制**：只监听 `127.0.0.1`，不对外开放。

### 3.3 安全约束

`hook_server.py:41` 之后做了几层保护：

- 只接受 `Host: 127.0.0.1:9878` 或 `localhost:9878`
- 非 loopback host 直接 `403`
- 请求体最大 `64KB`
- 非 JSON / 非对象 / 无 `hook_event_name` 都会拒绝

所以这层虽然没有额外鉴权，但攻击面被压到“本机 loopback 调用”。

### 3.4 HTTP 处理流程

`hook_server.py` 的主链路很简单：

1. 接收 `POST /hook`
2. `json.loads(raw)`
3. 取 `payload["hook_event_name"]`
4. 调用 `router.route(event, payload)`
5. 把 router 的返回值作为 JSON response 返回给 hook

其中真正的业务核心都在 `hook_router.py`。

---

## 4. 第三层：Router 负责把 Claude 事件翻译成设备状态

### 4.1 关键文件

- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/hook_router.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/permissions.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/wire.py`

### 4.2 Router 的职责

可以把 Router 看成整个系统的大脑，它做四件事：

1. **识别 hook 类型**
2. **更新内存状态 State**
3. **必要时等待设备审批**
4. **把最新状态编码成 heartbeat 发给设备**

### 4.3 Router 维护哪些状态

从 `hook_router.py` 可以看出，daemon 不是只转发事件，它会维护一份 Claude 当前状态视图，主要包括：

- 当前总会话数 / running / waiting
- output tokens / input tokens
- cache read / cache write
- 当前 context window 使用量 `ctx_used`
- 模型名 `model`
- 每个 session 的摘要信息
- 最近工具调用 entries
- 今日/累计花费
- 最近 28 天 token 历史
- Claude 版本

这些数据并不全来自 hook 本身，还会额外从下面两个地方补齐：

- `~/.claude/stats-cache.json`
- `~/.claude/projects/*/*.jsonl`

所以 daemon 的角色不是“透传器”，而是“**Claude 本地状态聚合器**”。

### 4.4 为什么插件要读 `.claude/`

这点在 `doc/dev_4_24.md:42` 里也明确提到了：

> 根据 UI 开发从 `.claude/` 路径下获取对应的数据通过蓝牙发送到设备并且保持协议兼容。

具体分工是：

- hook 只提供“事件发生了”
- `.claude/stats-cache.json` 提供 token / cost / daily history 等累计统计
- `.claude/projects/*.jsonl` 提供 session 名、最近 usage、上下文窗口等更细粒度信息

因此 UI 上很多看起来不是 hook 直接给的字段，本质上是 daemon 在本地二次整理出来的。

---

## 5. 各类 hook 事件是如何流动的

下面按事件类型说明。

### 5.1 SessionStart

作用：启动一个 Claude 会话时，建立会话上下文。

大致流程：

1. Claude 触发 `SessionStart`
2. hook 通过 curl POST 到 `/hook`
3. `hook_server.py` 调用 `router.route("SessionStart", payload)`
4. router 建立/更新 `SessionInfo`
5. 从 stats-cache 里补历史 token / cost 等数据
6. 通过 `wire.heartbeat()` 编码最新快照
7. 发给设备

设备表现：

- 会话列表里出现新的 session
- 主屏/状态屏的统计值刷新

### 5.2 UserPromptSubmit

作用：用户提交 prompt 后，session 有了“名字来源”和等待状态。

关键逻辑：

- 记录会话名（通常来自第一条用户 prompt）
- waiting 计数增加
- 更新 msg / session 列表
- 推送 heartbeat

设备表现：

- 主屏右侧 session 列表会显示新的会话名
- 对应项目分组下会看到该 session

### 5.3 PreToolUse

这是整个系统里最关键的一条双向链路。

#### 正向链路

1. Claude 即将调用工具，触发 `PreToolUse`
2. hook 运行 `hook_handler.py`
3. `hook_handler.py` 把 JSON POST 给 daemon
4. router 进入 PreToolUse 分支
5. router 调用 `PermissionBridge.ask(tool, hint)`
6. `PermissionBridge` 创建一个 pending prompt，生成唯一 `prompt_id`
7. router 发出一帧带 `prompt` 字段的 heartbeat
8. heartbeat 经 BLE 发给设备
9. 设备 UI 检测到 `has_prompt=true`，切到审批界面

#### 反向链路

10. 用户在设备上按键选择：
    - once
    - always
    - deny
11. 设备通过 BLE 回发：

```json
{"cmd":"permission","id":"<prompt_id>","decision":"once|always|deny"}
```

12. daemon 的 `_rx_pump()` 收到 BLE 行数据
13. `wire.parse_frame()` 识别为 `permission`
14. `PermissionBridge.handle_permission()` 用 `id` 找到当前 pending prompt
15. 生成 Claude 需要的回复格式：
    - `once` → `{"decision":"approve","permanent":false}`
    - `always` → `{"decision":"approve","permanent":true}`
    - 其它 → `{"decision":"deny"}`
16. router 把这个 JSON 返回给 `hook_handler.py`
17. `hook_handler.py` 根据 reply 决定退出码
18. Claude Code 最终执行或阻止工具调用

#### 为什么 PermissionBridge 单独存在

`permissions.py` 把审批流程从普通状态同步里拆开，主要是为了：

- 保证同一时刻只有一个 prompt 在等待
- 管理超时
- 正确完成 `prompt_id` 匹配

当前超时时间是：

- `DEFAULT_TIMEOUT_S = 35.0`

也就是在 Claude 40 秒 hook 预算里预留 5 秒余量。

如果超时，默认返回 deny，避免 Claude 卡住。

### 5.4 PostToolUse

作用：工具执行完后，把结果更新到会话状态和设备 UI。

典型更新包括：

- 把一次工具调用记录进 entries
- 刷新 session 的 local entries
- 从 JSONL 尾部读取最新 usage
- 更新 `ctx_used`
- running/waiting 状态更新
- 发 heartbeat

设备表现：

- 主屏右侧日志和会话列表刷新
- session detail 页面看到最近工具记录
- status/chart/pie 页面统计跟着变化

### 5.5 Stop

作用：Claude 本次响应结束，更新 token 和运行状态。

大致流程：

- hook 通知 daemon 该轮结束
- router 累加 usage
- 标记对应 session 不再 running
- 更新 recently_completed 等字段
- 发 heartbeat

设备表现：

- 运行中的黑点消失
- 可能进入 CELEBRATE 类瞬时 UI 状态
- token/cost 统计刷新

---

## 6. 第四层：wire.py 负责把状态编码成 BLE 能传的 JSON 行

### 6.1 基本传输格式

`wire.py` 规定所有帧都是：

- UTF-8 JSON
- 紧凑编码
- 末尾必须带一个 `\n`

也就是 **newline-delimited JSON**。

这是因为 BLE 传输是分片的，必须用 `\n` 作为一帧结束标记，设备端才能重组。

### 6.2 主机发给设备的核心帧：heartbeat

最重要的编码函数是：

- `wire.heartbeat(...)`

它会把 daemon 当前聚合出的状态打包成一帧，常见字段包括：

- `total`
- `running`
- `waiting`
- `tokens`
- `tokens_today`
- `tokens_in`
- `tokens_in_today`
- `cache_read`
- `cache_write`
- `ctx_used`
- `ctx_total`
- `msg`
- `entries`
- `prompt`
- `model`
- `sessions`
- `mstats`
- `ver`
- `cost_td`
- `cost_all`
- `daily`

这意味着设备 UI 不需要自己推导 Claude 侧业务，只需要消费 heartbeat。

### 6.3 设备发给主机的帧

`wire.parse_frame()` 当前重点处理：

- `{"cmd":"permission", ...}`
- `{"cmd":"status"}`
- `{"ack": ...}`

其中最关键的是 `permission`，因为它闭环到 `PreToolUse` 审批。

### 6.4 关键限制

`wire.py` 还做了一些协议边界控制：

- 单帧最大 `4KB`
- 入站最大 `8KB`
- session name / model / project / entry 长度都会截断

这些限制和设备端结构体大小是对齐的，避免 UI 缓冲区溢出。

---

## 7. 第五层：BLE 客户端负责真正把 JSON 帧送到设备

### 7.1 文件位置

- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/ble_client.py`

### 7.2 BLE 角色

这里 daemon 是 **中央设备（central）**，T5AI-Pocket 是 **外设（peripheral）**。

daemon 负责：

- 扫描名为 `Claude_XXXX` 的设备
- 连接后订阅 TX notify
- 往 RX characteristic 写入数据
- 把设备 notify 回来的分片重组成整行 JSON

### 7.3 UUID

当前使用 Nordic UART Service：

- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

含义：

- **central → device** 写 RX
- **device → central** notify TX

### 7.4 分片与组包

因为 BLE characteristic 一次不能无限写，所以 `ble_client.py` 做了两件事：

#### tx_chunks()

发送时：

- 按 `mtu - 3` 分块
- 同时硬限制 `MAX_NOTIFY_CHUNK = 180`

也就是就算 MTU 更大，也不会给设备发超过 180 字节的单片。

#### reassemble_lines()

接收时：

- 把 notify 分片累加到 buffer
- 遇到 `\n` 才认定一帧结束
- 超过 `8KB` 就清空，避免异常设备导致内存无限涨

### 7.5 连接生命周期

`BleClient._lifecycle()` 的大逻辑是：

1. 扫描 `Claude_XXXX`
2. 连接
3. 订阅 TX notify
4. 持续从 `tx_queue` 取数据写入设备
5. 如果断开，指数退避重连

退避策略大致是：

- 1s → 2s → 4s → 8s → 16s

所以 BLE 层本身是有自恢复能力的。

---

## 8. daemon 主程序如何把 HTTP、Router、BLE 串起来

### 8.1 文件位置

- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/__main__.py`

### 8.2 启动时做了什么

`__main__.py` 启动 daemon 时会创建并连接这些对象：

- `State`
- `PermissionBridge`
- `BleClient`
- `Router`
- HTTP server

其中 tx 通道通过 `_BleTxAdapter` 适配到 router。

### 8.3 两个后台循环

#### `_rx_pump()`

职责：

- 从 BLE `rx_queue` 读到完整一行 JSON
- `wire.parse_frame()` 解析
- 如果是 `permission`，交给 `PermissionBridge.handle_permission()`

这是审批回传的关键链路。

#### `_heartbeat_loop()`

职责：

- 每 `10s` 触发一次
- 如果 BLE 已连接，就发送：
  - `time_sync`
  - `router.tick()`

也就是说，除了 hook 触发的实时更新，系统还有一条**固定周期心跳**。

### 8.4 为什么需要 10 秒心跳

原因：

1. 让设备即使没有新 hook 事件，也能保持“在线感”
2. 定期刷新时钟
3. 定期重读 `.claude` 的统计数据
4. 防止 UI 长时间停留在旧状态

所以这个系统既有：

- **事件驱动更新**：hook 触发
- **周期性保活刷新**：10 秒心跳

两者结合后 UI 才稳定。

---

## 9. 设备端 BLE 模块如何接收这些数据

### 9.1 关键文件

- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.h`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_data.h`

### 9.2 buddy_ble 的角色

设备端 `buddy_ble` 不是 UI，它是 **BLE 协议桥**。

主要职责：

1. 负责以 `Claude_XXXX` 广播自己
2. 监听 NUS GATT 流量
3. 把收到的 JSON 行解析成状态
4. 把结果写入共享状态 `buddy_tama_state_t`
5. 提供 `buddy_ble_snapshot()` 给 UI 读取
6. 在需要时通过 `buddy_ble_send_permission()` 给主机回包

所以设备侧也是分层的：

- `buddy_ble` 处理传输与协议
- `buddy_main_screen` 等处理显示

### 9.3 设备何时开始广播 Claude 模式

从 `tuya_main.c:161` 开始可以看到：

- 在 `TUYA_EVENT_MQTT_CONNECTED` 事件里
- 首次云端连接成功后调用 `buddy_ble_start()`

也就是说：

- Tuya 设备先完成自身网络/云连接
- 然后才切换到 Claude Buddy 广播模式

`buddy_ble_start()` 的作用是：

- 关闭 Tuya 自己的配对超时监控
- 发布 `Claude_XXXX` 广播名
- 带上 NUS UUID

这也是 PC 端能扫描到它的前提。

### 9.4 设备如何收主机发来的 JSON

`buddy_ble.c` 里通过 TAL sniffer callback 监听 NUS GATT 事件：

- 连接事件
- 断连事件
- MTU 更新
- 写请求（主机写 RX characteristic）

当收到写请求时：

1. 找到 NUS RX characteristic
2. 把字节累加到 `s_rx_buf`
3. 以 `\n` 为分隔切出完整行
4. JSON parse
5. 根据字段更新 `s_state`
6. `__push_ui_state()` 推给 UI

所以设备侧和 daemon 侧一样，都是“分片接收 → 行重组 → JSON 解析”。

### 9.5 设备如何回传审批

用户在审批界面做选择后，设备调用：

- `buddy_ble_send_permission(prompt_id, decision)`

它会发送类似：

```json
{"cmd":"permission","id":"...","decision":"once"}
```

主机收到后又回到前面 `PermissionBridge` 的流程。

---

## 10. 设备端共享状态是 UI 的唯一数据源

### 10.1 数据结构位置

- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_data.h`

### 10.2 设计原则

`buddy_data.h` 里已经说明得很明确：

> 设备端只渲染来自 Claude 桌面/CLI 通过 BLE（NUS）推送的字段。

也就是说 UI 不自己去做业务推导，不主动去读网络/Claude，只读 `buddy_tama_state_t`。

### 10.3 这份状态里有什么

`buddy_tama_state_t` 主要包含：

#### 连接与审批状态

- `ble_connected`
- `recently_completed`
- `has_prompt`
- `prompt_id`
- `prompt_tool`
- `prompt_hint`

#### 会话统计

- `sessions_total`
- `sessions_running`
- `sessions_waiting`

#### token / cache / context

- `tokens`
- `tokens_today`
- `tokens_in`
- `tokens_in_today`
- `cache_read`
- `cache_write`
- `ctx_used`
- `ctx_total`

#### 文本与身份信息

- `msg`
- `owner_name`
- `device_name`
- `model`
- `claude_version`

#### 日志与会话详情

- `entries[]`
- `sessions[]`
- `mstats[]`
- `daily_tokens[]`

#### UI 派生状态

- `persona_id`
- `persona_state`
- `led_state`

这也解释了为什么当前设备可以做多页面展示：

- main screen 看 `sessions[]`
- session screen 看某个 `sessions[i]`
- status screen 看 tokens/cost/version
- chart screen 看 `daily_tokens[]`
- pie screen 看 `mstats[]`

---

## 11. UI 层如何消费这些状态

### 11.1 主屏 buddy_main_screen

文件：

- `src/display/ui/buddy_ui/buddy_main_screen.c`

它负责显示：

- 顶部状态栏
- 左侧 persona 区域
- 右侧 project/session 列表
- 运行状态黑点
- prompt 到来时切审批页

关键点在 `buddy_main_screen_update_state()`：

- UI 收到新 state 后先判断 `has_prompt`
- 如果刚从无 prompt 变成有 prompt，直接 `screen_load(&buddy_approval_screen)`
- 否则正常刷新主屏内容

所以审批弹窗不是 BLE 层直接切 UI，而是 **state 驱动 UI 切换**。

### 11.2 审批页 buddy_approval_screen

文件：

- `src/display/ui/buddy_ui/buddy_approval_screen.c`

它从 `buddy_ble_snapshot()` 拿当前 state，显示：

- Tool
- Info（通常是路径或 hint）
- Session
- Owner + Model

然后用户可选择：

- `Approve (once)`
- `Approve (always)`
- `Deny`

最后调用 `buddy_ble_send_permission()` 回传。

这就是 `PreToolUse` 的设备 UI 落点。

### 11.3 Session / Status / Chart / Pie 页面

当前新 UI 结构已经分别拆为：

- `buddy_session_screen.c`
- `buddy_status_screen.c`
- `buddy_chart_screen.c`
- `buddy_pie_screen.c`

各自消费的核心字段分别是：

- session：`sessions[]` + `local_entries`
- status：`claude_version` / `tokens_today` / `cost_today_ucc` / `model` / `owner_name`
- chart：`daily_tokens[]`
- pie：`mstats[]`

这说明当前 UI 已经不再只是单页，而是围绕 daemon 聚合后的状态，做了完整多视图展示。

### 11.4 screen_manager 的作用

文件：

- `src/display/ui/screen_manager.c`

它负责：

- 页面栈管理
- `screen_load()` 切入新页
- `screen_back()` 返回
- `screen_tab()` 做同级页面切换

所以 buddy 系列页面本质上都挂在现有 LVGL 屏幕管理器上，并不是单独维护一套导航系统。

---

## 12. 一条完整的 PreToolUse 审批时序

下面用文本时序把最关键链路串起来。

```text
Claude Code
  │
  │ 1. 即将调用工具，触发 PreToolUse
  ▼
hook_handler.py
  │ 2. 读取 stdin JSON
  │ 3. POST -> http://127.0.0.1:9878/hook
  ▼
hook_server.py
  │ 4. 解析 hook_event_name=PreToolUse
  ▼
hook_router.py
  │ 5. PermissionBridge.ask(tool, hint)
  │ 6. 生成 prompt_id
  │ 7. wire.heartbeat(prompt={id,tool,hint})
  ▼
ble_client.py
  │ 8. 分片写入 NUS RX characteristic
  ▼
T5AI-Pocket / buddy_ble.c
  │ 9. 重组 JSON 行
  │10. 更新 buddy_tama_state_t.has_prompt = true
  ▼
buddy_main_screen.c
  │11. 检测到 has_prompt，从主屏切到审批页
  ▼
buddy_approval_screen.c
  │12. 用户按键选择 once / always / deny
  ▼
buddy_ble_send_permission()
  │13. 发回 {cmd:"permission", id:"...", decision:"..."}
  ▼
ble_client.py
  │14. 接收 notify，重组成一行
  ▼
__main__.py::_rx_pump
  │15. wire.parse_frame() -> permission
  ▼
PermissionBridge.handle_permission()
  │16. 通过 prompt_id 匹配 pending prompt
  │17. 生成 Claude reply
  ▼
hook_router.py
  │18. 返回 JSON 给 HTTP
  ▼
hook_handler.py
  │19. 根据 decision 决定 exit 0 / exit 2
  ▼
Claude Code
  │20. 执行工具 or 阻止工具
```

这条链路里，真正的双向闭环只有一个：

- **PreToolUse ↔ permission decision**

其它 hook 事件大部分是单向状态同步。

---

## 13. 一条 PostToolUse / Stop 的状态同步链

相对简单：

```text
Claude Code
  │ PostToolUse / Stop
  ▼
hook (curl)
  │ POST /hook
  ▼
hook_server.py
  ▼
hook_router.py
  │ 更新 state / session / entries / usage
  │ heartbeat()
  ▼
ble_client.py
  ▼
buddy_ble.c
  │ 更新 buddy_tama_state_t
  ▼
各类 UI 页面刷新
```

它不需要用户交互，只负责让设备 UI 跟上 Claude 的最新状态。

---

## 14. HTTP、BLE、UI 三者分别负责什么

很多时候容易把这些层混在一起，实际职责边界如下：

### HTTP 层

职责：

- 接 Claude hook
- 做本地安全边界
- 转交 Router

不负责：

- BLE 细节
- UI 逻辑

### BLE 层

职责：

- 字节分片/组包
- NUS GATT 收发
- 设备连接管理

不负责：

- Claude 事件理解
- UI 布局

### UI 层

职责：

- 渲染共享状态
- 接收用户按键
- 在需要时发简单命令回主机

不负责：

- Claude session 聚合
- hook 解析
- stats-cache 读取

### Router / State 层

职责：

- 真正把 Claude 世界翻译成设备世界

所以如果要查 bug，第一步要先判断它属于哪一层。

---

## 15. 出问题时如何定位

### 15.1 设备上完全没反应

优先检查：

1. daemon 是否启动
2. `127.0.0.1:9878/hook` 是否在监听
3. hook 是否已经合并到 `~/.claude/settings.json`
4. BLE 是否已配对并连接
5. 设备是否已进入 `Claude_XXXX` 广播模式

相关文件：

- `claude-cli-plugin/daemon/README.md`
- `hook_server.py`
- `__main__.py`
- `buddy_ble.c`

### 15.2 Claude 工具调用没有被设备拦住

优先检查：

1. `PreToolUse` 是否确实走了 `hook_handler.py`
2. `hook_handler.py` 是否收到了 daemon reply
3. reply 里的 `decision` 是否是 `deny`
4. 设备回传的 `prompt_id` 是否匹配
5. `PermissionBridge` 是否超时

关键文件：

- `hooks.json`
- `hook_handler.py`
- `permissions.py`
- `wire.py`
- `__main__.py`

### 15.3 UI 统计值不对

优先检查：

1. `stats-cache.json` 是否有对应数据
2. JSONL session 文件是否存在且格式符合预期
3. router 是否在 tick 中刷新了状态
4. heartbeat 里对应字段是否带出来
5. `buddy_tama_state_t` 是否正确接收

关键文件：

- `hook_router.py`
- `wire.py`
- `buddy_data.h`
- `buddy_status_screen.c`
- `buddy_chart_screen.c`
- `buddy_pie_screen.c`

### 15.4 设备连上了但没有刷新时钟

优先检查：

1. `_heartbeat_loop()` 是否在 10 秒周期触发
2. `wire.time_sync()` 是否发送成功
3. `buddy_ble.c` 是否正确处理 `{"time":[epoch,tz]}`
4. UI 页面是否根据 `wall_epoch_s` 和 `wall_local_ms_at_rx` 计算时钟

---

## 16. 当前工作流的核心特点总结

最后用几句话总结这个系统的设计要点：

1. **hook 是事件入口**，负责把 Claude 行为变成结构化输入。
2. **daemon 是状态中枢**，负责聚合 `.claude/` 数据、维护 session 状态，并把它翻译成设备协议。
3. **HTTP 是本地桥**，把 hook 和 daemon 解耦。
4. **BLE 是传输层**，只负责把 JSON 行稳定送到设备并接回简单命令。
5. **设备 UI 是纯消费层**，依赖 `buddy_tama_state_t` 渲染，不直接理解 Claude 内部逻辑。
6. **PreToolUse 是唯一真正的双向强闭环**，它让设备按键能直接决定 Claude 是否执行工具。
7. **其余事件大多是状态同步**，配合 10 秒心跳，让设备持续感知 Claude 的运行状态。

从工程角度看，这个方案最大的优点是：

- Claude 逻辑、协议逻辑、传输逻辑、UI 逻辑层次清楚。

这样后续无论你要改：

- hook 行为
- 插件统计字段
- BLE 协议字段
- UI 展示样式

都能在相对独立的一层里完成，而不会把整条链打乱。

---

## 17. 关键文件索引

### Claude hook / plugin / daemon

- `apps/tuya_t5_pocket/claude-cli-plugin/settings/hooks.json`
- `apps/tuya_t5_pocket/claude-cli-plugin/scripts/hook_handler.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/hook_server.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/hook_router.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/permissions.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/wire.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/ble_client.py`
- `apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy/__main__.py`

### T5AI-Pocket 设备端

- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.h`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_data.h`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_main_screen.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_approval_screen.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_session_screen.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_status_screen.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_chart_screen.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_pie_screen.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/screen_manager.c`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/tuya_main.c`

### 相关文档

- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/UI_INTERACTION_zh.md`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/PLAN_zh.md`
- `apps/tuya_t5_pocket/docs/protocol/BLE_WIRE_PROTOCOL_zh.md`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/dev_4_24.md`
