# Claude Code 可读取数据全览

> 基于对 `C:\Users\<user>\.claude\` 目录的完整扫描，汇总所有可供数据面板使用的字段。
> 所有文件均为纯文本（JSON / JSONL / Markdown），无需注入或 hook 即可直接读取。

---

## 快速索引

| 数据类别 | 文件 | 更新频率 | 适合面板场景 |
|---------|------|---------|------------|
| 累计 token 用量 | `stats-cache.json` | 每次会话结束 | 总览卡片、趋势图 |
| 每日活动统计 | `stats-cache.json` | 每次会话结束 | 日历热力图 |
| 实时 context 用量 | `projects/.../SESSION.jsonl` 尾部 | 每次 API 调用后 | 实时进度条 |
| 会话完整对话 | `projects/.../SESSION.jsonl` | 每条消息追加 | 消息流 |
| 会话汇总指标 | `usage-data/session-meta/` | 会话结束时写入 | 会话卡片 |
| 命令历史 | `history.jsonl` | 每次输入命令 | 命令频率图 |
| 配置与插件 | `settings.json` | 手动修改时 | 配置面板 |
| IDE 连接状态 | `ide/<PID>.lock` | IDE 启动/关闭 | 连接指示灯 |
| 子 Agent | `projects/.../subagents/` | Agent 运行时 | Agent 树 |
| 文件编辑历史 | `file-history/` | 每次文件修改 | 文件变更时间线 |

---

## 1. stats-cache.json — 最重要的统计来源

**路径：** `~/.claude/stats-cache.json`
**更新时机：** 每次会话正常结束（Stop 事件）

### 1.1 完整字段

```json
{
  "version": 3,
  "lastComputedDate": "2026-04-23",

  "modelUsage": {
    "claude-sonnet-4-6": {
      "inputTokens": 13975,
      "outputTokens": 823958,
      "cacheReadInputTokens": 114211566,
      "cacheCreationInputTokens": 11284300,
      "webSearchRequests": 0,
      "costUSD": 0,
      "contextWindow": 0,
      "maxOutputTokens": 0
    }
  },

  "dailyModelTokens": [
    {
      "date": "2026-04-22",
      "tokensByModel": {
        "claude-sonnet-4-6": 119440
      }
    }
  ],

  "dailyActivity": [
    {
      "date": "2026-04-22",
      "messageCount": 486,
      "sessionCount": 3,
      "toolCallCount": 136
    }
  ],

  "totalSessions": 20,
  "totalMessages": 2089,

  "longestSession": {
    "sessionId": "UUID",
    "duration": 85288104,
    "messageCount": 1109,
    "timestamp": "2026-04-22T10:00:00.000Z"
  },

  "firstSessionDate": "2026-04-22T09:00:00.000Z",

  "hourCounts": {
    "9": 2,
    "10": 4,
    "14": 6
  },

  "totalSpeculationTimeSavedMs": 0
}
```

### 1.2 面板可展示内容

| 面板组件 | 使用字段 |
|---------|---------|
| 各模型 Token 总量 | `modelUsage[model].{inputTokens, outputTokens}` |
| 缓存命中率 | `cacheReadInputTokens / (inputTokens + cacheReadInputTokens)` |
| 缓存省钱估算 | `cacheReadInputTokens * (cache_hit_price - full_price)` |
| 每日 Token 趋势折线图 | `dailyModelTokens[].tokensByModel` |
| 每日消息数 / 工具调用数 | `dailyActivity[].{messageCount, toolCallCount}` |
| 使用高峰时段热力图 | `hourCounts` (0–23 小时分布) |
| 总会话数 / 总消息数 | `totalSessions`, `totalMessages` |
| 最长会话记录 | `longestSession.{duration, messageCount}` |
| 首次使用日期 | `firstSessionDate` |

---

## 2. 会话 JSONL — 实时对话流与 Token 明细

**路径模式：** `~/.claude/projects/<PROJECT_HASH>/<SESSION_ID>.jsonl`
**更新时机：** 每条消息、每次工具调用实时追加
**典型大小：** 1–5 MB / 活跃会话（17 小时 ≈ 2.7 MB）

### 2.1 行类型与字段

#### `type: user` — 用户输入
```json
{
  "type": "user",
  "message": {
    "role": "user",
    "content": "用户输入的文本"
  },
  "uuid": "UUID",
  "timestamp": "ISO-8601",
  "promptId": "UUID",
  "sessionId": "UUID",
  "cwd": "D:\\tuya_proj\\TuyaOpen",
  "gitBranch": "master",
  "version": "1.x.x",
  "entrypoint": "cli"
}
```

#### `type: assistant` — Claude 响应（含 token 用量）
```json
{
  "type": "assistant",
  "message": {
    "role": "assistant",
    "id": "msg_xxx",
    "model": "claude-sonnet-4-6",
    "content": [
      { "type": "text", "text": "响应文本" },
      { "type": "thinking", "thinking": "内部推理（extended thinking）" },
      { "type": "tool_use", "id": "toolu_xxx", "name": "Bash", "input": {...} }
    ],
    "usage": {
      "input_tokens": 3,
      "output_tokens": 1625,
      "cache_creation_input_tokens": 290259,
      "cache_read_input_tokens": 0
    }
  },
  "uuid": "UUID",
  "timestamp": "ISO-8601"
}
```

> **`ctx_used` 计算公式（对应 /status 的上下文用量）：**
> ```
> ctx_used = input_tokens + cache_creation_input_tokens + cache_read_input_tokens
> ```

#### `type: user (tool_result)` — 工具调用结果
```json
{
  "type": "user",
  "message": {
    "role": "user",
    "content": [
      {
        "type": "tool_result",
        "tool_use_id": "toolu_xxx",
        "content": "工具输出文本"
      }
    ]
  },
  "toolUseResult": {
    "durationMs": 243,
    "numFiles": 3,
    "filenames": ["src/main.c", "include/main.h"],
    "truncated": false
  },
  "uuid": "UUID",
  "timestamp": "ISO-8601"
}
```

#### `type: attachment` — Hook 执行记录
```json
{
  "type": "attachment",
  "attachment": {
    "type": "hook_success",
    "hookName": "PostToolUse:Bash",
    "toolUseID": "toolu_xxx",
    "stdout": "hook 脚本输出",
    "stderr": "",
    "exitCode": 0,
    "command": "/path/to/hook_handler.py",
    "durationMs": 45
  },
  "uuid": "UUID",
  "timestamp": "ISO-8601"
}
```

#### `type: permission-mode` — 权限模式切换
```json
{
  "type": "permission-mode",
  "permissionMode": "default",
  "sessionId": "UUID"
}
```

#### `type: system` / `task_reminder` / `invoked_skills`
- 上下文管理内部元数据，记录 skill 调用、任务提醒等

### 2.2 JSONL 最优读取策略

| 需求 | 方法 | 开销 |
|-----|------|------|
| 当前 ctx_used | 读尾部 16 KB，取最后一条 `type=assistant` 的 `message.usage` | 极低 |
| 本轮输出 token | Stop hook payload 的 `usage.output_tokens` | 零（hook 推送） |
| 全会话 token 总量 | 累加所有 `type=assistant` 行的 `message.usage.output_tokens` | 全文解析 |
| 工具调用列表 | 筛选 `type=assistant` + `content[].type=tool_use` | 全文解析 |
| 错误统计 | 筛选 `attachment.type=hook_non_blocking_error` | 全文解析 |

---

## 3. session-meta — 每会话汇总（最完整的单会话快照）

**路径：** `~/.claude/usage-data/session-meta/<SESSION_ID>.json`
**更新时机：** 会话结束时写入，单文件 1–2 KB

### 3.1 完整字段

```json
{
  "session_id": "UUID",
  "project_path": "C:\\tuya_proj\\TuyaOpen",
  "start_time": "2026-04-23T09:00:00.000Z",
  "duration_minutes": 1031,

  "messaging": {
    "user_message_count": 22,
    "assistant_message_count": 208,
    "message_hours": [17, 17, 18, 10, 11]
  },

  "tools": {
    "tool_counts": {
      "Glob": 7,
      "Bash": 64,
      "Read": 29,
      "Edit": 19,
      "Write": 1,
      "Monitor": 2,
      "Agent": 1,
      "TaskOutput": 4
    },
    "tool_errors": 12,
    "tool_error_categories": {
      "Command Failed": 7,
      "User Rejected": 5
    }
  },

  "code_analytics": {
    "languages": {
      "C": 2,
      "Python": 6,
      "Markdown": 26,
      "JSON": 4
    },
    "lines_added": 366,
    "lines_removed": 19,
    "files_modified": 8,
    "git_commits": 0,
    "git_pushes": 0
  },

  "tokens": {
    "input_tokens": 616,
    "output_tokens": 112647
  },

  "user_behavior": {
    "user_interruptions": 6,
    "user_response_times": [6.328, 245.549, 12.1]
  },

  "features": {
    "uses_task_agent": true,
    "uses_mcp": false,
    "uses_web_search": false,
    "uses_web_fetch": false,
    "first_prompt": "用户的第一条消息"
  },

  "timestamps": {
    "user_message_timestamps": ["ISO-8601", "..."]
  }
}
```

### 3.2 面板可展示内容

| 面板组件 | 使用字段 |
|---------|---------|
| 会话时长 | `duration_minutes` |
| 用户消息 vs Claude 响应 | `messaging.{user_message_count, assistant_message_count}` |
| 工具调用排行 | `tools.tool_counts` (柱状图) |
| 错误率 | `tools.tool_errors / sum(tool_counts)` |
| 错误分类饼图 | `tools.tool_error_categories` |
| 代码语言分布 | `code_analytics.languages` |
| 净代码变更 | `lines_added - lines_removed` |
| 修改文件数 | `code_analytics.files_modified` |
| 用户中断次数 | `user_behavior.user_interruptions` |
| 用户响应时间分布 | `user_behavior.user_response_times` (箱线图) |
| 特性采用率 | `features.{uses_mcp, uses_web_search, uses_task_agent}` |
| 项目关联 | `project_path` |

---

## 4. history.jsonl — 命令历史

**路径：** `~/.claude/history.jsonl`
**更新时机：** 每次用户在 CLI 中输入命令

```json
{
  "display": "/model",
  "pastedContents": {},
  "timestamp": 1776998750227,
  "project": "D:\\tuya_proj\\TuyaOpen",
  "sessionId": "UUID"
}
```

### 面板可展示内容

| 面板组件 | 方法 |
|---------|------|
| 最近命令列表 | 读取最后 20 条 |
| 斜杠命令频率 | 筛选 `display` 以 `/` 开头，统计频次 |
| 每日命令量趋势 | 按 `timestamp` 分日统计 |
| 跨项目活动 | 按 `project` 字段分组 |

---

## 5. settings.json — 配置与 Hook 状态

**路径：** `~/.claude/settings.json`

```json
{
  "model": "claude-sonnet-4-6",
  "effortLevel": "low|medium|high",

  "env": {
    "ANTHROPIC_BASE_URL": "自定义端点",
    "ANTHROPIC_AUTH_TOKEN": "API Key（敏感）",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "模型覆盖",
    "CLAUDE_CODE_DISABLE_TELEMETRY": "1"
  },

  "permissions": {
    "allow": ["Bash(*)", "Read(*)", "Write(*)"]
  },

  "hooks": {
    "SessionStart":     [{ "hooks": [{ "type": "command", "command": "..." }] }],
    "UserPromptSubmit": [...],
    "PreToolUse":       [...],
    "PostToolUse":      [...],
    "Stop":             [...]
  },

  "enabledPlugins": {
    "tuya-pocket-buddy@marketplace": true
  }
}
```

### 面板可展示内容

- 当前模型名称
- 推理强度（effortLevel）
- 已授权工具权限列表
- 已注册 Hook 数量与类型
- 已启用插件清单

---

## 6. IDE 连接状态

**路径：** `~/.claude/ide/<PID>.lock`
**更新时机：** IDE 启动时创建，关闭时删除

```json
{
  "pid": 29292,
  "workspaceFolders": ["D:\\tuya_proj\\TuyaOpen"],
  "ideName": "Cursor",
  "transport": "ws",
  "runningInWindows": true,
  "authToken": "UUID"
}
```

**面板用途：** 检测文件是否存在即可知晓 IDE 是否连接，`ideName` 区分 Cursor / VS Code。

---

## 7. 子 Agent 数据

**路径：** `~/.claude/projects/<PROJECT>/<SESSION_ID>/subagents/`

```
agent-<AGENT_ID>.jsonl       ← 子 Agent 完整对话（同主会话 JSONL 格式）
agent-<AGENT_ID>.meta.json   ← 元数据
```

**meta.json：**
```json
{
  "agentType": "Explore",
  "description": "Scan codebase for BLE protocol usage"
}
```

**面板可展示：** Agent 调用树、子 Agent token 用量（来自其 JSONL）、并发 Agent 数量。

---

## 8. 数据面板架构建议

### 8.1 刷新策略

```
实时（≤5s）：
  → 读 当前 SESSION.jsonl 尾部 16 KB   ← ctx_used, 最新工具名
  → 读 ide/*.lock 文件列表              ← IDE 连接状态

中频（10–30s）：
  → stats-cache.json                   ← 累计 token、每日趋势
  → session-meta/<当前SESSION>.json    ← 本次会话汇总（若已生成）

低频（会话开始时一次）：
  → settings.json                      ← 模型、权限、插件配置
  → history.jsonl 最后 100 条          ← 命令历史
```

### 8.2 Hook 推送（最低延迟，无需轮询）

通过 Claude Code Hook 可以**零延迟**获取以下事件：

| Hook 事件 | 可获取数据 |
|-----------|-----------|
| `SessionStart` | session_id, cwd, model |
| `UserPromptSubmit` | session_id, prompt 文本 |
| `PreToolUse` | tool_name, tool_input 参数 |
| `PostToolUse` | tool_name, tool_result（部分版本）|
| `Stop` | usage.{input_tokens, output_tokens, cache_*} |

### 8.3 面板卡片与数据来源对照

```
┌─────────────────────────────────────────────────────────────┐
│  Token 总览卡片                                              │
│  └── stats-cache.json → modelUsage[model].outputTokens      │
│                                                              │
│  当前 Context 进度条                                         │
│  └── SESSION.jsonl 尾 → 最新 assistant.message.usage        │
│                                                              │
│  今日活动摘要                                                │
│  └── stats-cache.json → dailyActivity[today]                │
│                                                              │
│  工具调用排行（本会话）                                       │
│  └── session-meta/<SESSION>.json → tools.tool_counts        │
│                                                              │
│  代码变更统计                                                │
│  └── session-meta → code_analytics.{lines_added, languages} │
│                                                              │
│  错误日志流                                                  │
│  └── SESSION.jsonl → attachment.type=hook_non_blocking_error │
│                                                              │
│  命令频率热力图                                              │
│  └── history.jsonl → 按小时/项目分组                         │
│                                                              │
│  IDE 连接状态                                                │
│  └── ide/*.lock 文件是否存在                                 │
│                                                              │
│  插件状态                                                    │
│  └── settings.json → enabledPlugins                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 9. 无法从外部获取的数据（仅 /status 可见）

以下数据**只存在于 Claude Code 进程内存**，任何文件读取方式均无法获取：

| /status 显示项 | 原因 |
|--------------|------|
| System prompt token 分项 | 进程启动时加载，不写文件 |
| System tools token 分项 | 内置工具定义，不写文件 |
| MCP tools token 分项 | 运行时推断，不写文件 |
| Skills token 分项 | 运行时加载，不写文件 |
| Messages 分项 | 即时计算，不写文件 |
| Autocompact buffer 大小 | 压缩策略内部状态 |

这些分项的**总和**（ctx_used）可以从 JSONL 推算，但无法拆解到各分类。

---

*文档生成时间：2026-04-24 | 扫描路径：C:\Users\maida\.claude*
