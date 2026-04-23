# Claude → 设备 BLE JSON 数据参考

本文档列出所有可通过蓝牙（Nordic UART Service，换行符分隔 UTF-8 JSON）在 Claude Code
Daemon 与 T5AI-Pocket 设备之间传输的数据帧。

---

## 传输基础

| 项目 | 值 |
|------|----|
| BLE Service | Nordic UART Service (NUS) |
| Service UUID | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX UUID（中央→设备，写入）| `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX UUID（设备→中央，通知）| `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| 帧格式 | UTF-8 JSON，以 `\n` 结尾，每行一帧 |
| 最大单块通知 | 180 字节（`BUDDY_BLE_MAX_NOTIFY_CHUNK`） |
| 设备 RX 缓冲 | 5120 字节 |
| Daemon 发送上限 | 4096 字节/行 |

---

## 1. 中央 → 设备（Daemon 发出）

### 1.1 心跳帧（Heartbeat）

最核心的帧，携带 Claude 的实时状态。连接期间每次状态变化或每 ~10 秒发送一次。

```json
{
  "total": 3,
  "running": 1,
  "waiting": 1,
  "msg": "approve: Bash",
  "tokens": 184502,
  "tokens_today": 31200,
  "entries": [
    "10:42 Bash git push",
    "10:41 Bash yarn test",
    "10:39 Read src/main.py"
  ],
  "prompt": {
    "id": "a1b2c3d4e5f6a1b2c3d4",
    "tool": "Bash",
    "hint": "rm -rf /tmp/foo"
  }
}
```

**字段说明：**

| 字段 | JSON 类型 | 长度限制 | 说明 |
|------|-----------|---------|------|
| `total` | integer | uint8 | 所有 Claude Code 会话总数 |
| `running` | integer | uint8 | 正在生成中的会话数 |
| `waiting` | integer | uint8 | 等待权限审批的会话数 |
| `msg` | string | ≤63 字节 | 单行状态摘要，如 `"approve: Bash"`、`"working on..."` |
| `tokens` | integer | uint32 | 自 Daemon 启动以来累计输出 token 数 |
| `tokens_today` | integer | uint32 | 今日 token 计数（本地午夜重置） |
| `entries[]` | array\<string\> | 最多 8 条，每条 ≤79 字节 | 最近工具调用的滚动转写，格式 `"HH:MM ToolName hint"`，新→旧排列 |
| `prompt` | object | 可选 | 有待审批的权限请求时存在 |
| `prompt.id` | string | 20 字节（10 字节随机 hex） | 权限请求唯一 ID |
| `prompt.tool` | string | ≤31 字节 | 工具名，如 `"Read"`、`"Write"`、`"Bash"` |
| `prompt.hint` | string | ≤60 字节 | 工具参数摘要，如文件路径或命令 |

> **无 `prompt` 字段** = 当前无待决权限请求，设备退出审批界面。

---

### 1.2 时间同步帧（Time Sync）

连接建立后立即发送一次，用于设备 UI 侧壁钟渲染。

```json
{"time": [1775731234, -420]}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `time[0]` | integer | Unix epoch 秒数（UTC） |
| `time[1]` | integer | 时区偏移，**单位：分钟**，东向为正。如 PST = −420，CST = +480 |

> 设备不修改系统 RTC，该值仅用于 UI 显示。

---

### 1.3 设备名称命令

```json
{"cmd": "name", "name": "Clawd"}
```

| 字段 | 约束 | 说明 |
|------|------|------|
| `cmd` | `"name"` | 固定 |
| `name` | ≤19 字节 | 新广播名（下次广告重启时生效） |

---

### 1.4 所有者名称命令

```json
{"cmd": "owner", "name": "Alice"}
```

| 字段 | 约束 | 说明 |
|------|------|------|
| `cmd` | `"owner"` | 固定 |
| `name` | ≤23 字节 | 所有者昵称，存入设备 `s_owner_name` |

---

### 1.5 状态查询命令

```json
{"cmd": "status"}
```

无附加字段。设备收到后立即返回状态 ack（见 2.1）。

---

### 1.6 解绑命令

```json
{"cmd": "unpair"}
```

无附加字段。通知设备解除绑定关系。

> v1.0 固件仅返回 ack，不擦除 BLE bond。需在操作系统蓝牙设置中手动移除。

---

## 2. 设备 → 中央（设备发出）

### 2.1 通用 Ack 帧

对所有中央发出的 `cmd` 帧（`status`、`name`、`owner`、`unpair`）回复：

```json
{"ack": "status", "ok": true, "n": 0}
```

| 字段 | 说明 |
|------|------|
| `ack` | 回显的命令名 |
| `ok` | `true` 成功，`false` 失败 |
| `n` | 通用计数器（分块 ack 时为字节数，否则为 0） |
| `error` | 失败时的原因描述（可选，v1.0 不发送） |

**状态查询 ack 附带详情：**

```json
{
  "ack": "status",
  "ok": true,
  "data": {
    "name": "Claude_A1B2",
    "sec": false,
    "sys": {"up": 8412}
  }
}
```

| 字段 | 说明 |
|------|------|
| `data.name` | 当前广播名 |
| `data.sec` | 是否启用 LE Secure Connections（v1.0 固定 `false`）|
| `data.sys.up` | 设备运行时间（秒） |

---

### 2.2 权限决策帧（Permission）

用户在设备屏幕上按键后发出：

```json
{"cmd": "permission", "id": "a1b2c3d4e5f6a1b2c3d4", "decision": "once"}
```

| 字段 | 可能值 | 说明 |
|------|--------|------|
| `cmd` | `"permission"` | 固定 |
| `id` | string | 原样回传 `prompt.id`，字节级匹配 |
| `decision` | `"once"` \| `"always"` \| `"deny"` | 用户决策 |

**decision 映射到 Claude Code Hook 响应：**

| decision | Hook 响应 |
|----------|-----------|
| `"once"` | `{"decision": "approve", "permanent": false}` |
| `"always"` | `{"decision": "approve", "permanent": true}` |
| `"deny"` | `{"decision": "deny"}` |

---

## 3. 字段大小汇总

| 常量 | 值 | 对应字段 |
|------|----|---------|
| `BUDDY_ENTRY_MAX_CHARS` | 79 | `entries[]` 每条最大长度 |
| `BUDDY_ENTRIES_RING` | 8 | `entries[]` 最多条数 |
| `msg` 缓冲区 | 64 字节 | `msg` 字段 |
| `prompt_id` 缓冲区 | 40 字节 | `prompt.id` 字段 |
| `prompt_tool` 缓冲区 | 32 字节 | `prompt.tool` 字段 |
| `prompt_hint` 缓冲区 | 64 字节 | `prompt.hint` 字段 |
| `owner_name` 缓冲区 | 32 字节 | `cmd:owner` 的 `name` 字段 |
| `device_name` 缓冲区 | 20 字节 | `cmd:name` 的 `name` 字段 |
| `PROMPT_HINT_MAX_CHARS` | 60 字节 | Daemon 截断 `hint` 的上限 |
| `DEFAULT_TIMEOUT_S` | 35 秒 | 权限请求阻塞超时 |

---

## 4. 完整通信时序示例

```
Claude Code PreToolUse Hook
    │
    ▼
Daemon 生成 prompt_id（20 hex 字符）
    │
    ▼ 心跳帧（含 prompt 字段）
设备接收 → 显示审批卡片，LED 快闪
    │
    ▼ 用户按键
设备发出 {"cmd":"permission","id":"...","decision":"once"}
    │
    ▼
Daemon 匹配 id → 返回 {"decision":"approve","permanent":false}
    │
    ▼
Claude Code 执行工具
    │
    ▼ PostToolUse Hook
Daemon 追加 entry，发送新心跳（无 prompt 字段）
    │
    ▼
设备退出审批界面，LED 恢复常态，转写列表滚动更新
```

---

## 5. 安全注意事项

- v1.0 **不加密**，所有 JSON 明文传输
- `buddy_ble_send_cmd()` 会过滤 `"`、`\` 及控制字符，防止 JSON 注入
- 设备 RX 缓冲溢出时丢弃整个缓冲区（而非截断），防止帧损坏
- JSON 解析失败时静默丢弃，不回复错误帧
- 同一时刻只允许一个待决权限请求，新请求自动替代旧请求
