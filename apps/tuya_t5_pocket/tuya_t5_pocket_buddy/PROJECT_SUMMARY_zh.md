# Tuya T5 Pocket Buddy — 项目文档完整总结（中文版）

> 基于 `tuya_t5_pocket_ai/docs/`、`tuya_t5_pocket_ai/doc/` 及 `claude-cli-plugin/` 下所有文档
> 综合整理，截至 2026-05-07。
> 原始文档均为中文（权威），部分存在英文对照版本。

---

## 1. 项目概述

**目标：** 在 **T5AI-Pocket**（384×168 单色/彩色 LCD，LVGL v9）上运行固件，通过 BLE（低功耗蓝牙）连接到 Windows 主机上的 **Claude Code CLI**，实时显示 Claude 会话状态、Token 用量、成本以及权限审批请求。用户通过 4 方向摇杆（上/下/左/右 + 中心按下）加两颗物理按键（`Enter`、`Esc`）完成全部交互操作，板载 LED 提供辅助状态反馈。

**平台范围：** 以 Windows 10/11 为优先目标。macOS / Linux 延期处理——代码中预留平台分支但返回"暂不支持"。

**参考项目：**
- `apps/tuya_t5_pocket/claude-desktop-buddy/src/buddies`（ASCII 角色来源）
- https://github.com/anthropics/claude-desktop-buddy.git（官方参考）
- https://github.com/op7418/m5-paper-buddy.git（M5 纸片 Buddy，插件架构参考）

**文档语言：** 项目内所有新增文档均使用**中文**输出。

---

## 2. 系统架构（5 层模型）

整个数据链路从 Claude Code CLI 到 T5AI-Pocket 设备屏幕，分为 5 个层次：

```
┌──────────────────────────────────────────────────────────────────┐
│ 第 1 层：Claude Code CLI                                         │
│   - 触发 Hook 事件：SessionStart、UserPromptSubmit、             │
│     PreToolUse、PostToolUse、Stop                                 │
│   - 由 Claude 进程负责产生事件                                    │
└──────────────────────┬───────────────────────────────────────────┘
                       │ stdin JSON payload（Claude 向 hook 传递数据）
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ 第 2 层：Claude Hooks（hooks.json + hook_handler.py）             │
│   - 一对多 fire-and-forget 通知：SessionStart/UserPromptSubmit/  │
│     PostToolUse/Stop → curl POST 到 127.0.0.1:9878/hook          │
│   - 阻塞式审批 PreToolUse → hook_handler.py 处理                 │
│     (exit 0 = 批准, exit 2 = 拒绝)                                │
└──────────────────────┬───────────────────────────────────────────┘
                       │ HTTP POST 127.0.0.1:9878/hook
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ 第 3 层：Daemon 守护进程（Python 3.10+，bleak，asyncio）         │
│   - hook_server.py： 本地 HTTP 服务器（仅监听 loopback）         │
│   - hook_router.py： 事件分发、状态聚合、审批桥接（系统大脑）     │
│   - permissions.py： 权限审批桥，管理 prompt_id 匹配与超时       │
│   - wire.py：        JSONL 帧编码/解码（换行符分隔 JSON）        │
│   - ble_client.py：  BLE 中央设备（扫描 Claude_XXXX，连接收发）  │
│   - state.py：       会话状态追踪                                 │
│   - config.py：      PID 文件管理、持久化配置                     │
│   - __main__.py：    主入口，组装所有组件，驱动后台循环            │
└──────────────────────┬───────────────────────────────────────────┘
                       │ NUS / BLE JSON 行（Nordic UART Service）
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ 第 4 层：设备端 BLE 协议桥（buddy_ble.c/h）                      │
│   - 以 Claude_XXXX 名称广播（Nordic UART Service）               │
│   - 接收中央写入的数据分片，按 \n 重组为完整 JSON 行             │
│   - 解析 JSON 后更新共享状态 buddy_tama_state_t                  │
│   - 负责分片发送 TX 通知（设备→主机方向）                        │
│   - buddy_ble_send_permission() 回传审批决策                     │
└──────────────────────┬───────────────────────────────────────────┘
                       │ 共享状态快照（buddy_tama_state_t）
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ 第 5 层：设备端 UI 层（buddy_main_screen + 子屏幕）              │
│   - 主屏：左侧人格画布（ASCII 角色动画）+ 右侧文本面板           │
│   - 审批屏：全屏权限卡片（覆盖整个 Body）                        │
│   - 会话屏：单个会话详细信息 + 日志                              │
│   - 状态屏：Claude 版本 / Token / 成本 / 模型等汇总              │
│   - 曲线图屏：最近 7/14/28 天 Token 使用趋势                     │
│   - 饼状图屏：各模型 Token 使用占比                              │
│   - screen_manager.c：页面栈管理（最大栈深 6）                   │
└──────────────────────────────────────────────────────────────────┘
```

**核心设计原则：** 设备端 UI 是 `buddy_tama_state_t` 共享状态的**纯消费者**。UI 层不做 Claude 业务逻辑推导，不直接读取网络或 `.claude/` 目录，不访问文件系统。Daemon 是系统的大脑——它从 Hook 事件 + `~/.claude/stats-cache.json` + `~/.claude/projects/*.jsonl` 三个数据源聚合 Claude 状态，并通过每 ~10 秒一次的心跳帧推送给设备。

---

## 3. BLE 通信协议（Nordic UART Service，v1.1 · 2026-04-22 冻结）

### 3.1 GATT 服务与特征

| 角色 | UUID |
|---|---|
| Service（主服务） | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX 特征（中央→设备，write / write-without-response） | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX 特征（设备→中央，notify） | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

> 中央（daemon）向设备写入 RX 特征；设备通过 TX 特征 notify 数据给中央。UUID 采用小端字节序，协议复刻自 `claude-desktop-buddy/REFERENCE.md`。

### 3.2 广播名与广播数据

**广播名格式：** `Claude_XXXX`，推导顺序如下（`buddy_ble.c::__derive_name()`）：

1. **BLE 地址优先：** 调用 `tal_ble_address_get(&addr)` 获取设备 BLE MAC。若成功且地址非空，取 MAC 低两字节（`addr.addr[1]`、`addr.addr[0]`），格式化为 `Claude_%02X%02X`（4 个十六进制字符）。
2. **Tuya UUID 回退：** 若无 BLE 地址，取 Tuya IoT `activate.devid` 或 `config.uuid`，取其**后四字符**格式化为 `Claude_%s`。该路径可能包含 UUID 字母表中任意可打印字符（不一定是十六进制）。
3. **最终兜底：** 裸 `Claude`。

名称缓冲区 `BUDDY_BLE_NAME_MAX = 20` 字节（含 NUL）。对端**不得**假设后缀一定是十六进制。

**广播数据（ADV，共 31 字节预算）：**
```
02 01 06                               ; Flags: LE General Discoverable, BR/EDR Not Supported
11 07 <16 字节 NUS service UUID 小端>  ; 完整 128 位 service UUID 列表
```

**扫描响应（31 字节预算）：**
```
<1+N> 09 <N 字节 s_device_name>        ; Complete Local Name
```

**广播间隔：** 30–60 ms（`adv_interval_min=0x30`, `adv_interval_max=0x60`，单位 0.625ms），可连接无定向广播。

**广播接管时机：** 设备在 Tuya 云上线**之后**才切换到 Claude Buddy 广播模式。`buddy_ble_start()` 调用 `tuya_ble_pair_monitor_disable(TRUE)` 抑制 Tuya 的 30 秒配对超时和周期性监视器（否则会重新发布 Tuya 配网名称）。在云激活之前扫描到设备的对端会先看到 Tuya 名称——这是预期行为。

### 3.3 MTU 协商

- 设备默认 MTU = 23。
- 收到对端 MTU 请求后直接采纳。
- Notify 实际负载 = `min(MTU - 3, 180)` 字节（`BUDDY_BLE_MAX_NOTIFY_CHUNK = 180`）。
- 超过一个分片的负载由发送方分片，接收方通过拼接完成重组。

### 3.4 帧格式（Newline-Delimited JSON，JSONL）

每条逻辑消息恰好是一行以 `\n` 结尾的 UTF-8 JSON 对象（紧凑编码）。

**设备 → 对端（notify 方向）：**
- 设备在每个 JSON 对象后追加 `\n`，然后通过 `__send_raw()` 按 MTU 分片送出。
- 中央负责接收 notify 片段、拼接并在 `\n` 处切分行。

**对端 → 设备（write 方向）：**
- 设备在 5120 字节环缓冲（`BUDDY_BLE_RX_BUF_CAP`）中累积入站字节。
- 每次收到数据，扫描下一个 `\n`，将前面的字节拷贝到堆分配的行中，然后调用 `__handle_line()` 处理。
- **溢出处理：** 一旦缓冲超过 5120 字节上限，**整个**缓冲全部丢弃（打印 warn 日志并重置缓冲）。对端**不得**依赖溢出后的半帧送达——在途帧会完全丢失。
- **JSON 解析失败：** 静默丢弃，仅打印 warn 日志（`buddy_ble bad json`），**不**回复错误帧。这是有意的安全设计，防止攻击者利用无效 JSON 作为放大向量。

### 3.5 报文字典

#### 3.5.1 心跳帧（Heartbeat，对端 → 设备）—— 最核心帧

触发时机：桌面/CLI 端状态变化时发送，另每 ~10 秒固定保活一次。

```json
{
  "total": 3,
  "running": 1,
  "waiting": 1,
  "msg": "approve: Bash",
  "tokens": 184502,
  "tokens_today": 31200,
  "tokens_in": 13975,
  "tokens_in_today": 4500,
  "cache_read": 114211566,
  "cache_write": 11284300,
  "ctx_used": 290262,
  "ctx_total": 1000000,
  "entries": [
    "10:42 Bash git push",
    "10:41 Bash yarn test",
    "10:39 Read src/main.py"
  ],
  "prompt": {
    "id": "a1b2c3d4e5f6a1b2c3d4",
    "tool": "Bash",
    "hint": "rm -rf /tmp/foo"
  },
  "model": "claude-sonnet-4-6",
  "sessions": [
    {"sid":"abc12345678","name":"t5-pocket-ai","p":"project-name","running":true}
  ],
  "mstats": [
    {"model":"claude-sonnet-4-6","tokens":823958}
  ],
  "ver": "1.2.3",
  "cost_td": 0.45,
  "cost_all": 12.30,
  "daily": [
    {"date":"2026-04-22","tokens":119440}
  ]
}
```

**字段详解：**

| 字段 | JSON 类型 | 取值范围 / 约束 | 说明 |
|---|---|---|---|
| `total` | 整数 | 0..255（转 uint8_t） | Claude 总会话数 |
| `running` | 整数 | 0..255 | 正在生成中的会话数 |
| `waiting` | 整数 | 0..255 | 等待权限审批的会话数 |
| `msg` | 字符串 | ≤ 63 字节 | 单行状态摘要，如 "approve: Bash"、"working on..." |
| `tokens` | 整数 | uint32_t | Daemon 启动以来累计输出 Token |
| `tokens_today` | 整数 | uint32_t | 今日输出 Token（本地午夜重置） |
| `tokens_in` | 整数 | uint32_t | 累计输入 Token |
| `tokens_in_today` | 整数 | uint32_t | 今日输入 Token |
| `cache_read` | 整数 | uint32_t | 累计缓存读取 Token |
| `cache_write` | 整数 | uint32_t | 累计缓存创建 Token |
| `ctx_used` | 整数 | uint32_t | 当前 context window 使用量 |
| `ctx_total` | 整数 | uint32_t | context window 总上限（由模型名推断） |
| `entries[]` | array\<string\> | 最多 8 条，每条 ≤ 79 字节 UTF-8 | 最近工具调用的滚动转写，格式 `HH:MM ToolName hint` |
| `prompt` | object | 可选 | 当前有待审批权限请求时出现；无此字段 = 无待决请求 |
| `prompt.id` | string | ≤ 39 字节 | 权限请求唯一 ID（20 hex 字符） |
| `prompt.tool` | string | ≤ 31 字节 | 工具名，如 "Read"、"Write"、"Bash" |
| `prompt.hint` | string | ≤ 63 字节 | 工具参数摘要，如文件路径或命令 |
| `model` | string | ≤ 31 字节 | 当前使用的模型名 |
| `sessions[]` | array\<object\> | — | 各活跃会话摘要（sid、name、project、running 状态） |
| `mstats[]` | array\<object\> | — | 各模型累计 Token 统计（供饼状图使用） |
| `ver` | string | ≤ 15 字节 | Claude Code 版本号 |
| `cost_td` | 数字 | — | 今日费用估算（USD） |
| `cost_all` | 数字 | — | 累计总费用估算（USD） |
| `daily[]` | array\<object\> | 最近 28 天 | 每日 Token 历史（供曲线图使用） |

**`entries[]` 环语义（v1.1）：**
- 每次心跳中带有 `entries` 数组时，**完全重建**设备端的转写环。
- 数组长度超过 8 条时只保留最后 8 条。
- 每个条目取 ≤ 79 字节（UTF-8 + NUL）；超长部分静默截断。
- **每次**收到 `entries`（即使数组为空）都会清空整个环再填充，绝不在跨对端、跨会话之间泄露陈旧文本。
- v1.1 固件不追踪每条的时间戳；渲染时在每条可见行前面加上设备当前 `HH:MM`。对端也可以在字符串内自行嵌入时间戳前缀。

#### 3.5.2 时间同步帧（Time Sync，对端 → 设备）

```json
{"time": [1775731234, -420]}
```

| 字段 | 类型 | 含义 |
|---|---|---|
| `time[0]` | 整数 | Unix epoch 秒数（UTC） |
| `time[1]` | 整数 | 时区偏移，**单位：分钟**，东向为正。如 PST = −420，CST = +480 |

设备端推导公式：`local = epoch + (now_ms - rx_ms)/1000 + tz_min × 60`

> **重要约束：** 设备**不修改** TuyaOS 系统 RTC。该值仅用于 UI 侧壁钟显示（顶栏 HH:MM、entry 行时间戳）。未收到时间同步前显示 `"--:--"`。

#### 3.5.3 命令帧（对端 → 设备）

| cmd | 附加字段 | 设备 ACK 形式 | 说明 |
|---|---|---|---|
| `status` | — | `ack:"status"` 附带 `data{}`（name/ sec/ sys.up） | 请求设备返回自身状态 |
| `name` | `name: string`（≤ 19 字节） | `ack:"name", ok:true` | 覆盖本地广播名（下次广播重启生效） |
| `owner` | `name: string`（≤ 23 字节） | `ack:"owner", ok:true` | 保存所有者昵称，UI 展示 |
| `unpair` | — | `ack:"unpair", ok:true` | **仅 ACK，v1.0 不擦除实际 BLE bond** |
| `char_begin/file/chunk/file_end/char_end` | REFERENCE.md 定义 | ❌ 不 ACK（拒收） | 角色包传输（M4 预留，当前不实现） |

#### 3.5.4 通用 ACK 信封（设备 → 对端）

```json
{"ack": "<原始cmd>", "ok": true, "n": 0}
```

失败变体（v1.0 不实际发出）：
```json
{"ack": "...", "ok": false, "error": "简述原因"}
```

#### 3.5.5 权限决策帧（Permission，设备 → 对端）

```json
{"cmd": "permission", "id": "a1b2c3d4e5f6a1b2c3d4", "decision": "once"}
```

| decision 值 | 映射到 Claude Hook 的回应 |
|---|---|
| `"once"` | `{"decision": "approve", "permanent": false}` |
| `"always"` | `{"decision": "approve", "permanent": true}` |
| `"deny"` | `{"decision": "deny"}` |

- **id 必须**与设备最近观测到的 `prompt.id` 逐字节相等。固件绝不合成 id 值。
- 对端收到不匹配的 `permission` 帧**必须**丢弃（防止快速取消+再次发起 prompt 后设备状态陈旧导致的误匹配）。
- 当前固件仅保证**单条在途** prompt：新的 `prompt` 字段会直接覆盖之前的 `prompt_id`，没有 per-prompt 队列。

#### 3.5.6 状态响应帧（Status Reply，设备 → 对端）

```json
{
  "ack": "status",
  "ok": true,
  "data": {
    "name": "Claude_A1B2",
    "sec": false,
    "sys": { "up": 8412 }
  }
}
```

| 字段 | v1.0 实际值 |
|---|---|
| `data.name` | 当前 `s_device_name` |
| `data.sec` | 恒为 `false`（LESC 未实现） |
| `data.sys.up` | 设备运行时间（秒）= `tal_system_get_millisecond() / 1000` |
| `data.bat` | v1.0 **省略** |
| `data.stats` | v1.0 **省略** |

### 3.6 连接顺序与保活

**推荐的对端上电序列：**
1. 建立 GATT 连接
2. `{"time":[...]}` — 时间同步
3. `{"cmd":"owner","name":"..."}` — 触发设备首个 `ack:"owner"`
4. 发送首个心跳快照

**保活策略：**
- 对端每 ~10 秒发送一次心跳帧；设备不主动发保活。
- 若对端连续 ~30 秒未收到心跳，应视链路为失效。
- 设备端在 `TAL_BLE_EVT_DISCONNECT` 时重置 UI 状态并自动重启广播。

### 3.7 权限审批完整流程（PreToolUse 双向闭环）

这是全系统唯一的真正双向强闭环——让设备端按键直接决定 Claude 是否执行工具。

```
Claude Code
  │ 1. 即将调用工具，触发 PreToolUse
  ▼
hook_handler.py
  │ 2. 读取 stdin JSON
  │ 3. POST → http://127.0.0.1:9878/hook
  ▼
hook_server.py
  │ 4. 解析 hook_event_name = "PreToolUse"
  ▼
hook_router.py + PermissionBridge
  │ 5. PermissionBridge.ask(tool, hint)
  │ 6. 生成唯一 prompt_id（secrets.token_hex(10)，20 字符 hex）
  │ 7. wire.heartbeat(prompt={id,tool,hint})
  ▼
ble_client.py
  │ 8. 分片写入 NUS RX 特征
  ▼
T5AI-Pocket / buddy_ble.c
  │ 9. 重组 JSON 行
  │10. 更新 buddy_tama_state_t → has_prompt = true
  ▼
buddy_main_screen.c
  │11. 检测到 has_prompt，切到审批页，LED 开始快闪
  ▼
buddy_approval_screen.c
  │12. 用户按键选择 once / always / deny
  │13. ENTER=once, LEFT=deny, RIGHT=always
  ▼
buddy_ble_send_permission()
  │14. 回传 {cmd:"permission", id:"...", decision:"..."}
  ▼
ble_client.py → __main__.py::_rx_pump
  │15. wire.parse_frame() → kind="permission"
  ▼
PermissionBridge.handle_permission()
  │16. 通过 prompt_id 匹配当前 pending prompt
  │17. 生成 Claude reply
  ▼
hook_router.py → hook_handler.py
  │18. 根据 decision 决定 exit 0 (允许) / exit 2 (拒绝)
  ▼
Claude Code
  │19. 执行工具 or 阻止工具
```

**超时策略：** `DEFAULT_TIMEOUT_S = 35.0` 秒，在 Claude 40 秒 hook 总预算中预留 5 秒余量。超时默认返回 deny。

**安全约束：** 同一时刻只允许一个权限请求在途；新的请求替代旧的；设备端单 buffer 存储，无队列。

---

## 4. UI 交互规格详情

### 4.1 屏幕分区总览（384×168 px）

```
┌─────────────────────────────────────────────────────────────┐  HEADER  — 20 px
│ Claude Buddy       BLE: linked        HH:MM        Claude_XX│
├──────────────────────┬──────────────────────────────────────┤  BODY    — 124 px
│                      │  msg / sessions / tokens / owner     │
│   PERSONA CANVAS     │  ─── Entries（最多 4 行，可滚动） ───  │
│   184 × 120 px       │  HH:MM  Ran Read(./src/foo.c)       │
│   (ASCII 动画/GIF)   │  HH:MM  Turn 4: 2.1k tok            │
│                      │  HH:MM  Session start: t5-pocket-ai │
│                      │  HH:MM  ...                         │
├──────────────────────┴──────────────────────────────────────┤  FOOTER  — 24 px
│ Contextual key hints                                        │
└─────────────────────────────────────────────────────────────┘
```

> 当 `has_prompt == true` 时，**审批卡**覆盖整个 BODY 区域（包括人格画布），直至用户做出决策。

### 4.2 各区域详细规格

#### Header（顶栏，20 px）

| 槽位 | 数据来源 | 说明 |
|---|---|---|
| 标题（左侧） | 硬编码 `"Claude Buddy"` | 静态标签 |
| BLE 状态（左中） | `ble_connected` | `"BLE: linked"` 或 `"BLE: -"` |
| HH:MM 时钟（中部） | `wall_epoch_s` + `wall_tz_min` + `wall_local_ms_at_rx` | UI 侧整数换算；未同步时显示 `"--:--"`；**不写** TuyaOS RTC |
| 设备名（右侧） | `device_name` | 广播名 `Claude_XXXX` |

#### Body（主体，124 px）

左半部分为人格画布（184×120 px），右侧为文本面板：

**人格画布（左）：**
- 渲染器：`ascii_persona.c`，使用 `lv_font_terminusTTF_Bold_14` 定宽字体
- 帧率：~30 Hz（由 `lv_timer` 周期性调用 `ascii_persona_tick(t_ms)`）
- 保持与上游 `claude-desktop-buddy` 坐标系 1:1 对齐
- 单色兼容：RGB565 颜色参数在单色 OLED 上被忽略

**文本面板（右）：**
- 顶部四行紧凑摘要：`msg / sessions / tokens / owner`
- 下方 Entry 面板：最多 4 行，顶部最新（加粗 16px），下面 3 行缩小（14px）
- Entry 存储：内存环 8 条 × 79 字节
- 滚动：`UP/DOWN` 始终可用，`s_entries_scroll` 控制窗口起点

#### Footer（底栏，24 px）

| 上下文 | 按键提示 |
|---|---|
| 权限审批就绪 | `ENTER=OK LEFT=deny RIGHT=always UP/DOWN=scroll ESC=back` |
| 已连接，无审批 | `LEFT/RIGHT=persona UP/DOWN=scroll JOYCON=refresh ESC=back` |
| 未连接 | `Waiting for Claude desktop...   ESC=back` |

### 4.3 按键映射

T5 Pocket 硬件：4 方向摇杆 + 中心按下（`KEY_UP/DOWN/LEFT/RIGHT/JOYCON`）+ `KEY_ENTER` + `KEY_ESC`。

#### 有审批卡时（`has_prompt == true`）

| 按键 | 行为 | 发送帧 |
|---|---|---|
| `KEY_ENTER` | 批准本次 | `{"cmd":"permission","id":"<id>","decision":"once"}` |
| `KEY_LEFT` | 拒绝 | `{"cmd":"permission","id":"<id>","decision":"deny"}` |
| `KEY_RIGHT` | 批准并记住（永久） | `{"cmd":"permission","id":"<id>","decision":"always"}` |
| `KEY_UP` | Entry 向上（更旧）滚动 | — |
| `KEY_DOWN` | Entry 向下（更新）滚动 | — |
| `KEY_JOYCON` | 请求主机重发快照 | `{"cmd":"status"}` |
| `KEY_ESC` | 返回上一屏 | — |

#### 无审批卡时（`has_prompt == false`）

| 按键 | 行为 |
|---|---|
| `KEY_ENTER` | 保留（暂无操作） |
| `KEY_LEFT` | **切换到上一个 persona**（持久化到 `tal_kv:"buddy.pid"`） |
| `KEY_RIGHT` | **切换到下一个 persona**（持久化到 `tal_kv:"buddy.pid"`） |
| `KEY_UP` | Entry 向上（更旧）滚动 |
| `KEY_DOWN` | Entry 向下（更新）滚动 |
| `KEY_JOYCON` | 请求主机重发快照 |
| `KEY_ESC` | 返回上一屏 |

**关键约束：**
- `KEY_UP` 仅当 `s_entries_scroll + 4 < entries_count` 时推进。
- `KEY_DOWN` 仅当 `s_entries_scroll > 0` 时回退。
- 无 prompt 时 `LEFT/RIGHT` 做 persona 切换（环形遍历），切换时重置动画时基防止姿态残留。
- 有 prompt 时 `LEFT/RIGHT` 做拒绝/批准，两个上下文按键语义互斥。

### 4.4 人格状态机（7 态）

| 状态 | 触发条件 | 视觉表现 | LED |
|---|---|---|---|
| `SLEEP` | `ble_connected == false` 且 ≥ 30 秒无心跳 | 呼吸/睡姿 | 常暗（常暗/慢呼吸） |
| `IDLE` | 已连接，无会话活动 | 待机动作 | 微亮 |
| `BUSY` | `tokens_rate > 0` 或最近有 entry 更新 | 敲键/工作动作 | 快闪 |
| `ATTENTION` | `has_prompt == true` | 转头注视 | 慢闪 |
| `CELEBRATE` | 会话完成（瞬时态，持续 3 秒） | 庆祝动作 | 单次强闪 |
| `DIZZY` | 收到错误 / 连续超时 | 旋转花眼 | 快闪 |
| `HEART` | 审批通过后回执（瞬时态，持续 2 秒） | 爱心动画 | 单次强闪 |

- 状态通过 `__derive_persona_state()` 每帧从 `buddy_tama_state_t` 计算。
- 瞬时态到期自动回落到当前应有的稳态。

### 4.5 18 种人格角色清单

人格 ID 范围 `[0, 17]`，在 `persona_registry.c` 中以固定顺序登记：

| ID | 英文名 | 中文名 | ID | 英文名 | 中文名 | ID | 英文名 | 中文名 |
|---|---|---|---|---|---|---|---|---|
| 0 | capybara | 水豚 | 6 | octopus | 章鱼 | 12 | axolotl | 蝾螈 |
| 1 | duck | 鸭子 | 7 | owl | 猫头鹰 | 13 | cactus | 仙人掌 |
| 2 | goose | 鹅 | 8 | penguin | 企鹅 | 14 | robot | 机器人 |
| 3 | blob | 果冻 | 9 | turtle | 乌龟 | 15 | rabbit | 兔子 |
| 4 | cat | 猫 | 10 | snail | 蜗牛 | 16 | mushroom | 蘑菇 |
| 5 | dragon | 龙 | 11 | ghost | 幽灵 | 17 | chonk | 胖猫 |

**关键规则：**
- 新增/下线人格必须**追加**到数组末尾，**禁止插入中间索引**。
- 原因是 `tal_kv` 中 `"buddy.pid"` 键存储的是 uint8 索引值，插入中间会导致旧值指向错误角色。
- 持久化：`tal_kv` 键 `"buddy.pid"`，值 `uint8_t`。启动时读取一次，失败/越界回落到 `0`（capybara）。切换后立即写入，写入失败仅 print `PR_WARN` 不阻塞 UI。

### 4.6 GIF 自定义角色占位（M4 预留）

- 入口：`buddy_gif_stub_attach(canvas_parent)`，与 ASCII 画布**互斥**。
- 显示：在 184×120 画布内渲染双行居中文本——自定义名称（默认 `"(gif)"`）和 `"(gif stub - M4)"` 提示。
- 当前为 M1 占位，**不实现**实际 GIF Flash 写入或渲染。
- 切换到 GIF 模式的条件：由 M3 的 `{"cmd":"buddy","mode":"gif","name":"..."}` 触发（暂未打通）。

### 4.7 LED 映射

`buddy_led.c` 包装 `tdl_led_manage.h`，按 `buddy_led_state_e` 枚举驱动 LED：

| `buddy_led_state_e` | `tdl_led` 调用 | 典型对应 |
|---|---|---|
| `BUDDY_LED_STATE_OFF` | `tdl_led_set_status(LED_OFF)` | 无连接 / SLEEP |
| `BUDDY_LED_STATE_ON_DIM` | `tdl_led_set_status(LED_ON)`（弱光） | IDLE |
| `BUDDY_LED_STATE_BLINK_SLOW` | `tdl_led_blink(on=500ms, off=500ms)` | ATTENTION |
| `BUDDY_LED_STATE_BLINK_FAST` | `tdl_led_blink(on=150ms, off=150ms)` | BUSY / DIZZY |
| `BUDDY_LED_STATE_FLASH_ONCE` | `tdl_led_flash(count=1)` | CELEBRATE / HEART |

- LED 设备名由板级配置的 `LED_NAME` 决定；未定义时自动降级为 no-op 并打印一次 `PR_WARN`。
- 状态切换做**去重**：相同状态重复设置不下发硬件命令，避免抖动。

### 4.8 运行时日志锚点

所有运维观测点均在 `PR_DEBUG` 级别（避免 `PR_INFO/PR_NOTICE` 泄露路径、命令名等敏感信息）：

| 锚点 | 触发时机 |
|---|---|
| `buddy_ble entries: idx=<n> text=<前80字符>` | 每条心跳 entry 解析 |
| `buddy_ble time sync ok epoch=<lld> tz=<d>` | 每次时间同步成功 |
| `ui clock render HH=%02d MM=%02d` | 表头与 entry 行重绘 |
| `ui scroll idx=<n> count=<c>` | UP/DOWN 按键滚动 |
| `ui persona id=<n> name=<...> state=<s>` | 人格状态变化 |
| `ui persona cycle dir=<+1\|-1> new_id=<n>` | LEFT/RIGHT 切换人格 |
| `ui led state <OFF\|ON_DIM\|BLINK_SLOW\|BLINK_FAST\|FLASH_ONCE>` | LED 下发 |
| `ui gif stub attach name=<...>` | GIF 占位挂载 |

---

## 5. Daemon 数据源详情

Daemon 不只是事件透传器，它是 **Claude 本地状态聚合器**。数据来自三个源：

### 5.1 源一：Claude Hook（实时事件推送）

| Hook 事件 | 获取数据 | 模式 |
|---|---|---|
| `SessionStart` | session_id, cwd, model | fire-and-forget |
| `UserPromptSubmit` | session_id, prompt 文本（生成会话名） | fire-and-forget |
| `PreToolUse` | tool_name, tool_input 参数 | **阻塞**（等设备审批） |
| `PostToolUse` | tool_name, tool_result | fire-and-forget |
| `Stop` | usage.{input_tokens, output_tokens, cache_*} | fire-and-forget |

### 5.2 源二：`~/.claude/stats-cache.json`（累计统计）

每次会话结束时更新，提供：
- 各模型 Token 累计总量（`modelUsage[model]`）
- 缓存读取/写入累计量
- 每日 Token 历史（最近 28 天）
- 每日活动统计（消息数、会话数、工具调用数）
- 总会话数 / 总消息数
- 使用高峰时段分布（0-23 小时）
- 最长会话记录、首次使用日期

daemon 在首次 `SessionStart` 事件时读取此文件，用于初始化累计计数器，避免"daemon 重启后 token 清零"。

### 5.3 源三：`~/.claude/projects/*.jsonl`（会话级别明细）

每行对应一次 API 调用或事件，实时追加写入。从最后一条 `type=assistant` 条目中可获取：

- **ctx_used 计算：** `input_tokens + cache_creation_input_tokens + cache_read_input_tokens`
- 模型名、output_tokens
- 缓存分层详情（5 分钟 / 1 小时有效期的分别计算）

daemon 读取策略：只需读取 JSONL 尾部 16 KB，取最后一条 assistant 消息的 `message.usage`，即可获得当前 context 用量。

### 5.4 无法外部获取的数据（仅 `/status` 可见）

以下数据只存在于 Claude Code 进程运行时内存中，任何文件读取或 Hook 方式均无法获取：

| `/status` 显示项 | 原因 |
|---|---|
| System prompt token 分项 | 进程启动时加载，不写文件 |
| System tools token 分项 | 内置工具定义，不写文件 |
| MCP tools token 分项 | 运行时推断，不写文件 |
| Skills token 分项 | 运行时加载，不写文件 |
| Messages 分项 | 即时计算，不写文件 |
| Autocompact buffer 大小 | 压缩策略内部状态 |

这些分项的**总和**（ctx_used）可以通过 JSONL 推算，但无法拆解到各子分类。

---

## 6. 项目里程碑

（来自 `PLAN_zh.md`）

| 里程碑 | 名称 | 依赖 | 可并行 | 产出 | 状态 |
|---|---|---|---|---|---|
| **M0** | 协议文档中文化 + 基线留档 | — | 否 | `BLE_WIRE_PROTOCOL_zh.md`、基线日志 | ✅ 完成 |
| **M1-UI** | 18 个 ASCII 角色 + 状态/审批/占位 GIF UI | M0 | 可与 M2 并行 | 角色注册表、ASCII 渲染器、审批屏、GIF 占位、LED 控制、中文字体 | ✅ 完成 |
| **M2-BLE** | Windows BLE 中央（bleak）联调 | M0 | 可与 M1 并行 | daemon 更新、端到端 BLE 打通、单元测试 | ✅ 完成 |
| **M3-Plugin** | CLI 插件中文化与一键安装 | M2 | 否 | 中文 README、命令文案、安装/启动/停止脚本、hooks.json 安全合并 | ✅ 完成 |
| **M4-Tools** | GIF 预处理/刷写 CLI **接口冻结**（**仅文档**） | M1 | 否 | `tools/README.md`、格式规范文档、协议附录 | ✅ 接口冻结 |
| **M5** | Windows 端到端集成验证 | M1+M2+M3 | 否 | 验证日志/视频 | ⏳ 待验证 |

### 明确不做（非目标）

- ❌ Tuya 云联动（统计/等级/心情系统已移除）
- ❌ 自定义 GIF 角色的真实 Flash 写入与渲染（仅留 UI 占位）
- ❌ LE Secure Connections 配对
- ❌ OTA 固件升级
- ❌ macOS / Linux 可执行脚本（仅保留平台分支占位）
- ❌ 游戏宠物、鸭子动画、游戏屏、电子书、摄像头功能
- ❌ RFID/打印机/AI 日志等 extend 扩展子系统

---

## 7. Claude CLI Plugin 结构

```
claude-cli-plugin/
├── .claude-plugin/
│   ├── plugin.json              # 插件清单
│   └── marketplace.json         # 市场上架信息
├── commands/                    # Slash 命令定义（Markdown 格式）
│   ├── buddy-install.md
│   ├── buddy-pair.md
│   ├── buddy-start.md
│   ├── buddy-stop.md
│   ├── buddy-status.md
│   └── buddy-unpair.md
├── daemon/
│   ├── pyproject.toml
│   ├── tuya_pocket_buddy/       # Python 守护进程主包
│   │   ├── __main__.py          # 入口：组装 Router + BLE + HTTP，驱动后台循环
│   │   ├── ble_client.py        # BLE 中央设备（bleak）；扫描/连接/分片/重组
│   │   ├── hook_server.py       # 本地 HTTP 服务（127.0.0.1:9878）；安全边界
│   │   ├── hook_router.py       # 事件分发 + 状态聚合 + 心跳生成（系统的"大脑"）
│   │   ├── permissions.py       # 权限审批桥（prompt_id 匹配 + 35s 超时）
│   │   ├── wire.py              # JSONL 帧编码/解码；heartbeat() / parse_frame()
│   │   ├── state.py             # 内存状态快照
│   │   └── config.py            # PID 文件、持久化配置、路径管理
│   └── tests/
│       ├── test_wire.py
│       ├── test_permissions.py
│       ├── test_hook_router.py
│       ├── test_ble_client.py
│       └── test_hook_server.py
├── scripts/
│   ├── install.ps1 / install.sh     # 一键安装（创建 venv，合并 hooks.json，pip install）
│   ├── start.ps1 / start.sh         # 启动 daemon 后台进程
│   ├── stop.ps1 / stop.sh           # 按 PID 终止 daemon
│   ├── status.ps1 / status.sh       # 显示 PID、BLE 状态、最后心跳时间、最后错误
│   ├── hook_handler.py              # PreToolUse 阻塞审批处理器
│   └── install-hooks.py / install-hooks.sh  # Hook JSON 合并工具
├── settings/
│   └── hooks.json                   # Claude Code Hook 事件定义（5 种事件）
└── docs/
    ├── CLAUDE_DATA_SOURCES_zh.md    # Claude Code 可读取数据全览
    └── TOKEN_DATA_SOURCES_zh.md     # Token 数据来源说明
```

### Daemon 生命周期与后台循环

**安装（buddy-install）：**
- 在 `%LOCALAPPDATA%\TuyaPocketBuddy\.venv` 创建 Python 虚拟环境
- `pip install -e daemon`（开发模式可编辑安装）
- 将 `hooks.json` 安全合并到 Claude Code 用户 settings（非覆盖合并，冲突时交互式询问）
- 安装失败时自动回滚并打印人类可读错误原因

**启动（buddy-start）：**
- 以后台进程启动 daemon，PID 写入 `%LOCALAPPDATA%\TuyaPocketBuddy\daemon.pid`
- `__main__.py` 创建并组装 State / PermissionBridge / BleClient / Router / HTTP Server
- BLE 客户端开始扫描 `Claude_XXXX` 并自动连接

**两个关键后台循环：**

1. **`_rx_pump()`** — 从 BLE rx_queue 读取设备→主机帧：
   - 读到完整一行 → `wire.parse_frame()` 解析
   - 如果是 `permission`：交给 `PermissionBridge.handle_permission()`
   - 如果是 `asr`：交给 `Router.handle_asr()`（ASR 文本注入到对应会话）
   - 如果是 `status` ack：更新设备连接元数据

2. **`_heartbeat_loop()`** — 每 10 秒触发一次：
   - 如果 BLE 已连接，发送 `time_sync`（更新设备 UI 时钟）
   - 调用 `router.tick()` 生成最新状态快照并推送心跳帧
   - 定期重读 `.claude/` 统计数据

**停止（buddy-stop）：** 按 PID 文件读取 daemon PID，发送 `SIGTERM`（Windows 用 `taskkill`）。

**状态（buddy-status）：** 同时显示 PID、BLE 连接状态、最后一次心跳时间戳、最后一次错误信息。

---

## 8. 设备端文件结构

```
tuya_t5_pocket_buddy/
├── CMakeLists.txt                    # 主构建文件
├── Kconfig                           # 菜单配置（ENABLE_CLAUDE_DESKTOP_BUDDY_BLE 等）
├── app_default.config                # 默认 Kconfig 值
├── PROJECT_SUMMARY.md                # 项目文档总结（本文件）
├── claude-cli-plugin/                # Python 守护进程 + CLI 插件（见 §7）
│
├── include/                          # 头文件（含桩代码）
│   ├── tuya_config.h                 # 项目配置（PRODUCT_ID、UUID、AUTHKEY）
│   ├── game_pet.h                    # 桩（仅需声明，无实际宠物逻辑）
│   ├── reset_netcfg.h                # 桩（resnet 相关函数返回 0）
│   ├── uart_expand.h                 # 桩（仅声明 uart_print_write）
│   └── app_display.h                # 简化版显示头文件
│
├── src/
│   ├── buddy_main.c                  # 主入口：IoT 初始化 + buddy_ble 启动
│   ├── buddy_indev.c                 # 输入设备：按键/摇杆 → LVGL 键值
│   ├── buddy_ai_chat.c              # AI 聊天集成：ASR 语音识别 → BLE 发送
│   ├── media/
│   │   ├── media_pet.c              # 音频提示数据
│   │   └── media_pet.h
│   │
│   └── display/
│       ├── CMakeLists.txt
│       ├── fonts/                    # TerminusTTF 14/16/18 + puhui 18 CJK
│       │   ├── lv_font_terminusTTF_Bold_14.c
│       │   ├── lv_font_terminusTTF_Bold_16.c
│       │   ├── lv_font_terminusTTF_Bold_18.c
│       │   └── ui_font_puhui_18_2.c  # 中文 fallback 字体
│       ├── icons/                    # WiFi/蓝牙/电池/菜单图标（~38 文件）
│       ├── logo/                     # 启动 logo
│       │   ├── logo.png
│       │   └── ai_pet_logo.c
│       └── ui/
│           ├── screen_manager.c/h    # 页面栈管理器（最大栈深 6）
│           ├── startup_screen.c/h    # 启动闪屏（"Claude Buddy / T5AI Pocket"）
│           ├── main_screen.c/h       # 别名 → buddy_main_screen
│           │
│           └── buddy_ui/             # Buddy 核心 UI（46 文件，~11300 行 C 代码）
│               ├── buddy_ble.c/h          # BLE NUS 协议桥
│               ├── buddy_data.h           # 共享状态数据结构
│               ├── buddy_ui_entry.h       # UI 入口（screens_init）
│               ├── buddy_main_screen.c/h  # 主屏（角色 + 项目/会话列表 + Entry）
│               ├── buddy_approval_screen.c/h # 权限审批屏
│               ├── buddy_session_screen.c/h # 会话详情屏
│               ├── buddy_status_screen.c/h # 状态总览屏
│               ├── buddy_chart_screen.c/h # Token 曲线图（7/14/28 天）
│               ├── buddy_pie_screen.c/h   # 模型 Token 占比饼状图
│               ├── buddy_status_bar.c/h   # 通用状态栏（Header/Footer）
│               ├── buddy_cjk_font.c/h     # CJK 中文字体包装（fallback = puhui）
│               ├── buddy_gif_stub.c/h     # GIF 角色占位屏
│               ├── buddy_led.c/h          # LED 状态驱动
│               ├── ascii_persona.c/h      # ASCII 角色帧渲染
│               ├── persona_registry.c/h   # 18 角色注册表
│               └── persona_*.c            # 18 个独立角色实现文件
```

---

## 9. Daemon 数据流全链路

### 9.1 Token 数据流

```
API 调用完成
     │
     ▼
Claude Code 写入 JSONL
~/.claude/projects/<project>/<session>.jsonl
└─ type=assistant
   └─ message.usage
        ├─ input_tokens              ──┐
        ├─ cache_creation_input_tokens ├── ctx_used (当前 context 总用量)
        └─ cache_read_input_tokens   ──┘

                    ↓ PostToolUse / Stop hook 触发

daemon hook_router.py
├─ _read_last_usage(session_id)  ← 读 JSONL 尾部 16 KB
│   └─ 更新 ctx_used, ctx_total
├─ _on_stop()
│   └─ 从 hook payload 提取 output/input/cache tokens
│      （fallback: 再次读 JSONL）
└─ 首次 SessionStart 时
    └─ _aggregate_stats_cache()  ← 读 stats-cache.json
        └─ 初始化累计 tokens / cache_read / cache_write

                    ↓ _emit_heartbeat()

BLE wire frame (JSON)
{
  "tokens": 118791,           ← 累计 output tokens
  "tokens_in": 649,           ← 累计 input tokens
  "cache_read": 16366719,     ← 累计缓存读取
  "cache_write": 1017013,     ← 累计缓存写入
  "ctx_used": 290262,         ← 当前 context 用量
  "ctx_total": 1000000,       ← 模型上限
  ...
}
```

### 9.2 数据刷新策略

| 刷新频率 | 数据项 | 来源 |
|---|---|---|
| 实时（≤ 5s） | ctx_used, 最新工具名 | 当前 SESSION.jsonl 尾部 16 KB |
| 中频（10–30s） | 累计 token、每日趋势、会话汇总 | stats-cache.json + session-meta/ |
| 低频（会话开始一次） | 模型、权限、插件配置 | settings.json |
| 零延迟推送 | Hook 事件 | Claude Code Hook |

---

## 10. 安全设计与加固

v1.0 安全基线（所有差距均有记录）：

| 维度 | 当前行为 | 说明 |
|---|---|---|
| **链路加密** | 不强制 LE Secure Connections，特征未标记 encrypted-only | 按项目规格为非目标 |
| **网络监听** | HTTP 仅监听 `127.0.0.1`，禁止 `0.0.0.0` | 拒绝非 loopback Host 请求（403） |
| **请求体验证** | HTTP 体最大 64 KB；拒绝非 JSON / 非对象 / 无 `hook_event_name` 的请求 | 防止资源耗尽与格式攻击 |
| **行长度** | 入站行上限 5120 字节；溢出丢弃**整个**缓冲 | RX 共享缓冲，中间截断会损坏后续帧 |
| **JSON 解析失败** | 静默丢弃 + warn 日志；不回复错误帧 | 防止攻击者可控的放大向量 |
| **cmd 白名单** | 仅 `status`/`name`/`owner`/`unpair` 被处理；未知 cmd 记录日志并拒绝（不 ACK） | 遵循"沉默即拒绝"原则 |
| **出站 JSON 注入防护** | `buddy_ble_send_cmd()` 过滤 `"`、`\` 及控制字符（< 0x20），cmd 长度截至 32 字节 | 对端仍应在服务端再做校验 |
| **ID 安全** | `prompt_id` 使用 `secrets.token_hex(10)` 生成（20 hex 字符） | 不可预测 |
| **日志脱敏** | 禁止打印完整 BLE MAC、Token、权限请求原文；`id`/`tool`/`hint` 做截断脱敏 | 防止敏感信息泄露 |
| **权限在途限制** | 单条在途 prompt；新请求覆盖旧请求 | 无队列 |
| **超时防御** | 35 秒超时后默认 deny | Claude 40 秒 hook 预算中预留 5 秒余量 |
| **unpair 语义** | v1.0 仅 ACK，不擦除 BLE bond | 需操作系统蓝牙设置手动移除 |

---

## 11. 故障排查速查表

| 症状 | 优先检查项 | 关键文件 |
|---|---|---|
| 设备完全无反应 | daemon 是否启动？`127.0.0.1:9878/hook` 是否监听？hook 是否合并到 settings？BLE 是否已配对？设备是否进入 `Claude_XXXX` 广播？ | `hook_server.py`, `__main__.py`, `buddy_ble.c` |
| Claude 工具未被设备拦截 | PreToolUse 是否走 `hook_handler.py`？reply 中 decision 是否是 deny？prompt_id 是否匹配？PermissionBridge 是否超时？ | `hooks.json`, `hook_handler.py`, `permissions.py`, `wire.py` |
| UI 统计数据不正确 | stats-cache.json 是否有数据？JSONL session 文件是否可读？router 是否在 tick 中刷新？heartbeat 对应字段是否携带？ | `hook_router.py`, `wire.py`, `buddy_data.h`, `buddy_status_screen.c` |
| 时钟不更新 | `_heartbeat_loop()` 是否 10 秒触发？`time_sync` 是否发送成功？`buddy_ble.c` 是否正确解析 `{"time":[epoch,tz]}`？ | `__main__.py`, `wire.py`, `buddy_ble.c` |
| 中文显示为方框 | puhui 字体文件是否编译？`buddy_cjk_font_init()` 是否在 screens_init 中调用？`DISABLE_BUDDY_CJK_FONT` 是否被误开？ | `buddy_cjk_font.c`, `ui_font_puhui_18_2.c` |
| 程序崩溃 | host_main_thread_hdl 线程大小是否充足？内存是否泄漏（free-heap 持续下降）？BLE 缓冲区是否溢出？ | `buddy_main.c`, `buddy_ble.c` |
| ASR 注入失败 | BLE 是否连接？选中会话是否有效？`buddy_ble_send_asr()` 返回是否 OPRT_OK？daemon 是否识别 `asr` 帧？ | `buddy_ai_chat.c`, `buddy_ble.c`, `wire.py`, `hook_router.py` |

---

## 12. 关键设计规则

1. **协议优先：** BLE_WIRE_PROTOCOL 的任何变更都必须先走 M0 修订子循环（spec → plan → 更新协议文档 → 再改代码），不可颠倒顺序。
2. **状态驱动 UI：** 设备端 UI 仅消费 `buddy_tama_state_t` 快照，不做 Claude 业务推导，不主动读网络或文件系统。
3. **异步模型：** Daemon 使用 asyncio 事件循环，不引入多线程。Hook 永远不直接阻塞 BLE 操作。
4. **安全边界：** HTTP 仅本地回环（127.0.0.1），`secrets.token_hex` 生成 ID，日志严格脱敏，禁止明文记录 MAC/Token。
5. **幂等恢复：** Daemon 重启通过 PID 文件恢复；Hook 在 daemon 不在时返回 `{}` 不阻塞 Claude。
6. **单条在途 prompt：** 同一时刻只处理一个权限请求；新请求直接覆盖旧请求，不做排队。
7. **追加式人格 ID：** 角色注册表必须按固定顺序，新增/删除仅在数组末尾操作，确保 `tal_kv` 持久化跨版本兼容。
8. **文档语言：** 项目内所有新增/修改文档以**中文**为权威版本；英文版只做对照参考并在顶部注明"以中文版为准"。

---

## 13. 关键技术栈

### 固件侧
- **SDK：** TuyaOpen SDK
- **BLE：** TAL BLE（NimBLE），Nordic UART Service
- **UI：** LVGL v9，lv_vendor
- **协议：** cJSON（JSON 解析/构造）
- **存储：** tal_kv（键值持久化）
- **定时器：** tal_sw_timer（软件定时器），lv_timer（LVGL 动画）
- **LED：** tdl_led_manage
- **输入：** tdl_button_manage，tdl_joystick_manage
- **字体：** TerminusTTF 14/16/18 + puhui 18 CJK

### 主机侧
- **语言：** Python 3.10+
- **BLE：** bleak（跨平台 BLE 中央）
- **HTTP：** aiohttp（本地 HTTP 服务器）
- **异步：** asyncio（事件循环，无多线程）
- **测试：** pytest

### 构建
- **固件：** `tos.py build` / `tos.py flash -p COMx` / `tos.py monitor -p COMx -b 460800`
- **Daemon 测试：** `cd daemon && pip install -e . && pytest -q`
- **插件安装：** `powershell -ExecutionPolicy Bypass -File scripts\install.ps1`

---

## 14. 协议版本历史

- **v1.1 · 2026-04-22** — M1-A 改动。心跳 `entries[]` 数组被解析进设备侧环并渲染；`{"time":[...]}` 作为 UI 侧壁钟偏移应用；`time[1]` 单位明确为 UTC 东向分钟。Gap Table 中 `entries[]` 和 `time` 翻为 ✅。**无线路变更**，所有 v1.0 兼容对端继续可用。
- **v1.0 · 2026-04-21** — 首次冻结。捕获 `616464c5` 提交上的协议实现。

---

*文档生成时间：2026-05-07。所有源文档位于 `tuya_t5_pocket_ai/docs/`、`tuya_t5_pocket_ai/doc/` 及 `claude-cli-plugin/docs/`。*
