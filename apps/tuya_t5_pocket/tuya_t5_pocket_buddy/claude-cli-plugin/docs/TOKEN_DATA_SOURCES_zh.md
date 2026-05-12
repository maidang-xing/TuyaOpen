# Token 数据来源说明

> 本文档说明 `tuya-pocket-buddy` 插件从哪里获取 Claude Code 的 token
> 使用量数据，以及为什么设备上显示的数据与 `/status` 命令输出存在差异。

---

## 1. `/status` 命令的数据来自哪里

在 Claude Code CLI 中执行 `/status` 会看到：

```
Context Usage
████░░░░░░░░░░░░░  Sonnet 4.6 (1M context)
31.8k/1m tokens (3%)

Estimated usage by category:
⛁ System prompt:    6.4k tokens (0.6%)
⛁ System tools:    24.4k tokens (2.4%)
⛁ MCP tools:         289 tokens (0.0%)
⛁ Skills:            721 tokens (0.1%)
⛁ Messages:            8 tokens (0.0%)
⛶ Free space:      935.2k (93.5%)
⛝ Autocompact buffer: 33k tokens (3.3%)
```

**这些数据全部来自 Claude Code 进程的运行时内存，不写入任何文件。**

| 字段 | 计算方式 |
|------|----------|
| System prompt | Claude Code 启动时加载系统提示词，用内置 tokenizer 计数 |
| System tools | Bash/Read/Write 等内置工具定义的 token 数，启动时固定 |
| MCP tools | 已连接 MCP server 提供的工具定义 token 数 |
| Skills | 已加载的 skill 文件 token 数 |
| Messages | 当前会话对话历史的 token 数，随对话增长 |
| 总上下文用量 | 上面五项之和 |
| 模型上限 | 从模型名推断：`[1m]` → 1,000,000；默认 200,000 |

**结论：分类明细无法从外部获取。** Claude Code 没有将这些内存数据暴露给任何 hook 或文件。

---

## 2. 插件实际能获取的数据

### 2.1 来源一：Claude Code Hook Payload

Claude Code 在以下事件触发时将 JSON payload 通过 stdin 发送给 hook 命令：

| Hook 事件 | 触发时机 | payload 中的 token 字段 |
|-----------|----------|------------------------|
| `SessionStart` | 新会话建立 | 无 token 数据 |
| `UserPromptSubmit` | 用户发送 prompt | 无 token 数据 |
| `PreToolUse` | 工具调用前（权限审批） | 无 token 数据 |
| `PostToolUse` | 工具调用完成 | 无 token 数据 |
| `Stop` | 会话轮次结束 | **有 `usage` 字段**（部分版本） |

Stop hook 的 `usage` 结构（若存在）：
```json
{
  "hook_event_name": "Stop",
  "session_id": "...",
  "usage": {
    "input_tokens": 3,
    "output_tokens": 1625,
    "cache_creation_input_tokens": 290259,
    "cache_read_input_tokens": 0
  }
}
```

**局限**：`usage` 字段是否存在取决于 Claude Code 版本，且只反映本次轮次（turn），不是累计值。

### 2.2 来源二：Session JSONL 文件（最可靠的实时数据）

Claude Code 将每个会话的完整对话写入：
```
~/.claude/projects/<project-hash>/<session-id>.jsonl
```

每条 API 调用完成后，`type=assistant` 条目包含真实的 API 响应 usage：

```json
{
  "type": "assistant",
  "message": {
    "model": "claude-sonnet-4-6",
    "usage": {
      "input_tokens": 3,
      "cache_creation_input_tokens": 290259,
      "cache_read_input_tokens": 0,
      "output_tokens": 1625,
      "cache_creation": {
        "ephemeral_5m_input_tokens": 290259,
        "ephemeral_1h_input_tokens": 0
      }
    }
  }
}
```

**`ctx_used`（当前上下文窗口用量）计算方式：**

```
ctx_used = input_tokens + cache_creation_input_tokens + cache_read_input_tokens
```

这就是 `/status` 显示的 `31.8k/1M (3%)` 中的分子（31.8k）。更新时机：每次 API 调用完成后（即每次 PostToolUse / Stop 触发时，daemon 读取 JSONL 尾部 16KB）。

### 2.3 来源三：stats-cache.json（最准确的累计数据）

Claude Code 在每次会话结束后更新：
```
~/.claude/stats-cache.json
```

```json
{
  "modelUsage": {
    "claude-sonnet-4-6": {
      "outputTokens": 118791,
      "inputTokens": 649,
      "cacheReadInputTokens": 16366719,
      "cacheCreationInputTokens": 1017013
    }
  },
  "dailyModelTokens": [
    { "date": "2026-04-22", "tokensByModel": { "claude-sonnet-4-6": 119440 } }
  ]
}
```

**这是最可靠的累计 token 来源**，涵盖了 daemon 启动之前的所有历史会话。

daemon 在第一个 `SessionStart` 事件时读取此文件，用于初始化累计计数器，避免"daemon 重启后 token 清零"的问题。

---

## 3. 数据可用性对比表

| 数据项 | `/status` 显示 | 设备上显示 | 数据来源 | 精度 |
|--------|---------------|-----------|----------|------|
| **Context 总用量** (`ctx_used`) | ✅ `31.8k` | ✅ `Ctx[###...]24% 244k/1M` | JSONL 最新 assistant.usage | 轮次级（每轮更新） |
| **Context 上限** (`ctx_total`) | ✅ `/1M` | ✅ `/1M` | 模型名推断 | 精确 |
| **Context 占比** | ✅ `(3%)` | ✅ `24%` | 计算值 | 精确 |
| **累计 output token** | ❌ 不显示 | ✅ `Out:3.1k` | stats-cache + Stop hook | 会话级 |
| **缓存读取** (cache read) | ❌ 不显示 | ✅ `R:244k` | stats-cache + Stop hook | 会话级 |
| **缓存写入** (cache write) | ❌ 不显示 | ✅ `W:55` | stats-cache + Stop hook | 会话级 |
| **System prompt 分项** | ✅ `6.4k` | ❌ 无法获取 | 进程内存（不可访问） | — |
| **System tools 分项** | ✅ `24.4k` | ❌ 无法获取 | 进程内存（不可访问） | — |
| **MCP tools 分项** | ✅ `289` | ❌ 无法获取 | 进程内存（不可访问） | — |
| **Skills 分项** | ✅ `721` | ❌ 无法获取 | 进程内存（不可访问） | — |
| **Messages 分项** | ✅ `8` | ❌ 无法获取 | 进程内存（不可访问） | — |
| **Free space** | ✅ `935.2k` | ✅ 可推算 | `ctx_total - ctx_used` | 轮次级 |

---

## 4. 数据流全链路

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
├─ _read_last_usage(session_id)  ← 读 JSONL 尾部 16KB
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
  "cache_read": 16366719,     ← 累计缓存读取
  "cache_write": 1017013,     ← 累计缓存写入
  "ctx_used": 290262,         ← 当前 context 用量
  "ctx_total": 1000000,       ← 模型上限
  ...
}

                    ↓ BLE (Nordic UART Service)

设备 buddy_ble.c → buddy_main_screen.c
Status 页显示：
  Ctx[###.........] 29%  290k/1M
  Out:119k  R:16.4M  W:1M
```

---

## 5. 已知局限

### 5.1 不能复现 /status 的分类明细

System prompt / tools / MCP / skills / messages 的分项 token 数只存在于 Claude Code 进程内存，既不写文件，也不通过 hook 暴露。要实现这个功能，必须对 Claude Code 进行二次开发或使用内部 API（目前 Anthropic 未开放）。

### 5.2 累计 token 仅精确到会话级

`stats-cache.json` 在会话结束后更新。如果 daemon 正在运行的会话还未结束，设备上的累计数字不含本次会话的增量，待 Stop hook 触发后才会加上。

### 5.3 多会话并发时的 ctx_used

`ctx_used` 反映的是最近一次读取的 JSONL 中最后一条 API 调用的 context 用量。如果有多个 Claude Code 会话在同时运行，每次心跳读取的 JSONL 对应最近触发 hook 的那个会话，不代表所有会话的综合情况。

### 5.4 ctx_total 为估算值

模型上限通过模型名中的 `[1m]` 标记推断（`_model_ctx_size()` 函数）。Claude Code 本身知道精确上限，但不通过任何外部接口暴露。

---

## 6. 相关代码位置

| 功能 | 文件 | 函数 |
|------|------|------|
| JSONL 读取 | `daemon/tuya_pocket_buddy/hook_router.py` | `_read_last_usage()` |
| stats-cache 读取 | `daemon/tuya_pocket_buddy/hook_router.py` | `_read_stats_cache()` / `_aggregate_stats_cache()` |
| ctx_used 计算 | `daemon/tuya_pocket_buddy/hook_router.py` | `_apply_jsonl_usage()` |
| 模型上限推断 | `daemon/tuya_pocket_buddy/hook_router.py` | `_model_ctx_size()` |
| BLE 帧编码 | `daemon/tuya_pocket_buddy/wire.py` | `heartbeat()` |
| 固件解析 | `src/display/ui/buddy_ui/buddy_ble.c` | `__handle_heartbeat()` |
| 设备显示 | `src/display/ui/buddy_ui/buddy_main_screen.c` | `__refresh_page0()` |
