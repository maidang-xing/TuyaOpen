# Tuya Pocket Buddy Plugin

## 概述

`tuya_pocket_buddy_plugin` 是一个 Claude Code 插件，运行 PC 端 Node.js 守护进程，作为 Claude Code CLI 与 T5AI Pocket 硬件设备之间的桥梁。

```
┌─────────────┐   Hook HTTP (9878)   ┌──────────────────┐   WebSocket (7681)   ┌──────────────┐
│ Claude Code  │ ──────────────────► │  Buddy Daemon    │ ◄──────────────────► │  T5AI Pocket │
│   CLI        │                     │  (Node.js)       │                      │  硬件设备     │
└─────────────┘                      └──────────────────┘                      └──────────────┘
```

- **Hook Server** (HTTP :9878)：接收 Claude Code 的 Hook 事件（会话启动、工具调用、结束等）
- **WebSocket Server** (:7681/buddy)：与 T5AI Pocket 设备保持双向通信
- **Permission Bridge**：设备审批流程——设备可以批准或拒绝 Claude 的工具调用

## 功能

| 功能 | 说明 |
|------|------|
| 实时状态镜像 | 将 Claude 会话状态（token 用量、活跃会话、模型信息等）推送到设备显示 |
| 硬件审批 | **仅在设备连接时生效**：设备可通过按键批准/拒绝 Claude 的 PreToolUse 请求 |
| 操作日志 | 记录最近 8 条工具调用，在设备上滚动显示 |
| 多设备支持 | 最多同时连接 4 台设备 |

> **注意：** 无设备连接时，所有工具调用自动放行，守护进程仅作为状态监控使用。

## 快速开始

### 环境要求

- Node.js >= 18
- npm
- Claude Code CLI（`claude` 命令可用）

### 方式一：`--plugin-dir` 加载（推荐）

使用 Claude Code 的 `--plugin-dir` 参数直接加载插件目录，hooks 和 slash commands 自动注册，**无需手动编辑配置文件**：

```bash
# 启动 Claude Code 并加载插件
claude --plugin-dir /path/to/tuya_pocket_buddy_plugin
```

首次使用时，在 Claude 会话中执行一键安装命令完成编译和防火墙配置：

```
/tuya-pocket-buddy:install
```

该命令自动完成：npm install → 编译 TypeScript → 配置 Windows 防火墙（如适用）。

然后启动守护进程：

```
/tuya-pocket-buddy:start
```

> **提示：** 可将 `--plugin-dir` 参数添加到 shell alias 中以便每次自动加载：
> ```bash
> alias claude='claude --plugin-dir /path/to/tuya_pocket_buddy_plugin'
> ```

### 方式二：全局 hooks 注册（不使用 --plugin-dir）

适合不想每次都传 `--plugin-dir` 参数的用户。将 hooks 写入 `~/.claude/settings.json`，对所有 Claude Code 会话生效。

#### 第一步：安装依赖并编译

```bash
cd /path/to/tuya_pocket_buddy_plugin
npm install
npm run build
```

#### 第二步：注册 hooks

```bash
node scripts/setup-hooks.js
```

幂等操作，重复运行会更新而不会重复添加。执行后重启 Claude Code 或打开 `/hooks` 菜单使配置生效。

#### 第三步：配置 Windows 防火墙（仅需一次，需管理员权限）

设备通过 WebSocket 连接 PC 需放行 7681 端口入站：

```powershell
New-NetFirewallRule -DisplayName 'Tuya Pocket Buddy WS' -Direction Inbound -Protocol TCP -LocalPort 7681 -Action Allow
```

Linux/macOS 通常无需配置，如设备无法连接请检查 `ufw`/`iptables`。

#### 第四步：启动守护进程

```bash
cd /path/to/tuya_pocket_buddy_plugin
node dist/index.js run
```

> **⚠️ 重要：** 方式一和方式二不可同时使用。`--plugin-dir` 会自动加载 `hooks/hooks.json`，若 `~/.claude/settings.json` 中同时配置了相同 hooks，两套会叠加触发，PreToolUse 的并发请求会相互阻断。

### 方式三：手动编辑 settings.json

如果 `setup-hooks.js` 脚本不适用于你的环境，可手动将 hooks 配置合并到 `~/.claude/settings.json`。

<details>
<summary>点击展开手动配置</summary>

将以下内容合并到 `~/.claude/settings.json` 的 `hooks` 字段中（完整参考见 [`settings/hooks.json`](settings/hooks.json)）：

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "curl -s -X POST http://127.0.0.1:9878/hook -H 'Content-Type: application/json' --data-binary @- --max-time 5 2>/dev/null || true",
            "timeout": 10,
            "async": true
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "curl -s -X POST http://127.0.0.1:9878/hook -H 'Content-Type: application/json' --data-binary @- --max-time 5 2>/dev/null || true",
            "timeout": 10,
            "async": true
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "node -e \"const http=require('http'),c=[];process.stdin.on('data',d=>c.push(d));process.stdin.on('end',()=>{const p=Buffer.concat(c).toString();const req=http.request({hostname:'127.0.0.1',port:9878,path:'/hook',method:'POST',headers:{'Content-Type':'application/json'},timeout:42000},res=>{const r=[];res.on('data',d=>r.push(d));res.on('end',()=>{try{const b=JSON.parse(Buffer.concat(r).toString());if(b.decision==='block')process.exit(2);}catch(e){}process.exit(0);});});req.on('error',()=>process.exit(0));req.on('timeout',()=>{req.destroy();process.exit(0);});req.write(p);req.end();});\"",
            "timeout": 45
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "curl -s -X POST http://127.0.0.1:9878/hook -H 'Content-Type: application/json' --data-binary @- --max-time 5 2>/dev/null || true",
            "timeout": 10,
            "async": true
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "curl -s -X POST http://127.0.0.1:9878/hook -H 'Content-Type: application/json' --data-binary @- --max-time 5 2>/dev/null || true",
            "timeout": 10,
            "async": true
          }
        ]
      }
    ]
  }
}
```

> **注意：** `PreToolUse` 使用内联 node 脚本而非 curl。原因：curl 加 `|| true` 始终 exit(0)，无法将守护进程的 `{"decision":"block"}` 传递给 Claude Code；内联 node 脚本在收到 block 响应时以 exit(2) 退出，Claude Code 据此拒绝该工具调用。

</details>

### 连接设备

T5AI Pocket 设备开机后，通过串口终端配置 WebSocket 地址：

```
buddy ws set <PC的IP地址> 7681
```

设备连接到 `ws://<IP>:7681/buddy`，连接后屏幕显示 Claude 实时状态。

---

## Slash Commands（需通过 --plugin-dir 加载）

使用 `claude --plugin-dir ./tuya_pocket_buddy_plugin` 启动时，以下命令自动可用：

| 命令 | 说明 |
|------|------|
| `/tuya-pocket-buddy:install` | 安装 npm 依赖、编译 TypeScript、配置防火墙 |
| `/tuya-pocket-buddy:start` | 启动守护进程（后台运行） |
| `/tuya-pocket-buddy:stop` | 停止守护进程 |
| `/tuya-pocket-buddy:status` | 查看守护进程状态 |
| `/tuya-pocket-buddy:uninstall` | 停止守护进程并清理 |

> **首次使用流程：** `claude --plugin-dir ...` → `/tuya-pocket-buddy:install` → `/tuya-pocket-buddy:start` → 设备配置 `buddy ws set <IP> 7681`

---

## 插件目录结构

```
tuya_pocket_buddy_plugin/
├── commands/                 # Slash Commands（--plugin-dir 时自动注册）
│   ├── install.md           # → /tuya-pocket-buddy:install （跨平台）
│   ├── start.md             # → /tuya-pocket-buddy:start   （跨平台）
│   ├── stop.md              # → /tuya-pocket-buddy:stop    （跨平台）
│   ├── status.md            # → /tuya-pocket-buddy:status
│   └── uninstall.md         # → /tuya-pocket-buddy:uninstall （跨平台）
├── hooks/
│   └── hooks.json           # Hooks 配置（--plugin-dir 时自动加载）
├── scripts/
│   ├── setup-hooks.js       # 注册 hooks 到 ~/.claude/settings.json（幂等）
│   ├── remove-hooks.js      # 从 ~/.claude/settings.json 移除 hooks
│   └── hook_handler.js      # 已弃用，功能已内联到 hooks.json 命令字符串中
├── settings/
│   └── hooks.json           # 手动安装参考配置（与 hooks/hooks.json 内容一致）
├── src/
│   ├── index.ts             # 入口，启动所有服务
│   ├── config.ts            # 配置常量
│   ├── hook-server.ts       # HTTP hook 接收服务
│   ├── hook-router.ts       # 事件路由 + 状态聚合
│   ├── ws-server.ts         # WebSocket 设备连接管理
│   ├── permissions.ts       # 硬件审批桥接
│   └── wire.ts              # 通信协议定义
├── dist/                     # 编译输出（tsc 生成，不提交到 git）
├── package.json
└── tsconfig.json
```

---

## Hook 工作机制

### 各事件说明

| Hook 事件 | 传输方式 | 说明 |
|-----------|---------|------|
| SessionStart | curl（异步） | 通知守护进程新会话建立 |
| UserPromptSubmit | curl（异步） | 传递用户输入，用于会话命名 |
| PreToolUse | node 内联脚本（**同步**） | 有设备连接时等待设备审批；无设备时立即放行 |
| PostToolUse | curl（异步） | 记录工具调用历史 |
| Stop | curl（异步） | 通知守护进程会话结束 |

### PreToolUse 阻断逻辑

```
Claude 准备执行工具
       │
       ▼
  hook 脚本运行（node 内联）
       │
       ▼
  POST 到 127.0.0.1:9878/hook
       │
       ├─ 连接失败（守护进程未运行）→ exit(0) → 放行 ✓
       │
       ▼
  守护进程检查已连接设备数
       │
       ├─ 无设备连接 → 返回 {} → exit(0) → 放行 ✓
       │
       ▼
  向所有设备推送审批请求（心跳中带 prompt 字段）
       │
       ├─ 设备按键批准 → 返回 {} → exit(0) → 放行 ✓
       ├─ 设备按键拒绝 → 返回 {"decision":"block"} → exit(2) → 阻断 ✗
       └─ 35 秒无响应 → 返回 {"decision":"block"} → exit(2) → 阻断 ✗
```

### 关于 `${CLAUDE_PLUGIN_ROOT}`

Claude Code 插件系统在 `hooks.json` 的 `command` 字符串中**不展开** `${CLAUDE_PLUGIN_ROOT}` shell 变量（该占位符仅在 `args` 数组形式的命令中有效，`command` 字符串通过 shell 执行，`${CLAUDE_PLUGIN_ROOT}` 会被 shell 当作未设置的环境变量处理为空）。

旧版使用 `node "${CLAUDE_PLUGIN_ROOT}/scripts/hook_handler.js"` 会导致：hook 报错 `No stderr output`，阻断所有工具调用。

**当前修复：** 将 hook_handler.js 的逻辑内联到 `command` 字符串中，彻底消除对文件路径的依赖。

---

## 配置参数

配置在 `src/config.ts` 中，修改后需重新 `npm run build`：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `hookPort` | 9878 | Hook HTTP 服务端口（仅监听 127.0.0.1） |
| `wsPort` | 7681 | WebSocket 服务端口 |
| `maxDevices` | 4 | 最大同时连接设备数 |
| `permissionTimeoutMs` | 35000 | 审批超时（毫秒），超时后自动拒绝 |
| `heartbeatIntervalMs` | 10000 | 心跳推送间隔（毫秒） |
| `maxPayloadBytes` | 65536 | 最大 Hook 负载大小 |

---

## 通信协议

### PC → 设备（Heartbeat JSON）

每 10 秒或 Hook 事件触发时推送：

```json
{
  "total": 3,
  "running": 1,
  "tokens": 15000,
  "tokens_today": 5000,
  "model": "claude-sonnet-4-6",
  "ver": "1.0.30",
  "entries": [{"t": "t", "n": "Bash", "h": "git status"}],
  "sessions": [
    {
      "sid": "abc123",
      "name": "fix bug",
      "model": "sonnet",
      "tok": 1200,
      "proj": "TuyaOpen",
      "run": true,
      "ent": ["Read", "Edit"]
    }
  ],
  "prompt": {"id": "p_123", "tool": "Bash", "hint": "rm -rf /tmp"},
  "time": [1715500000, 480]
}
```

`prompt` 字段仅在有设备连接且等待审批时出现。

### 设备 → PC（Device Frame JSON）

```json
{"cmd": "permission", "id": "p_123", "decision": "once"}
{"cmd": "permission", "id": "p_123", "decision": "deny"}
{"cmd": "hb_req"}
```

---

## 停止守护进程

使用 slash command（推荐）：
```
/tuya-pocket-buddy:stop
```

或手动停止：
```bash
# Linux/macOS
pkill -f "node dist/index.js run"

# Windows：找到监听 9878 端口的进程 PID
netstat -ano | findstr :9878
taskkill /F /PID <PID>
```

---

## 常见问题

**Q: 守护进程未运行时，PreToolUse hook 会造成延迟吗？**

不会。node 脚本连接 9878 失败（ECONNREFUSED）后立即 exit(0) 放行，延迟可忽略不计。

**Q: 不需要硬件审批功能，只想要状态显示，如何禁用 PreToolUse？**

从 `~/.claude/settings.json` 的 hooks 中删除 `PreToolUse` 条目即可。设备仍会收到心跳并显示状态，但不会拦截工具调用。

**Q: 设备连接后，Claude 工具调用被拒绝或超时？**

设备审批默认超时 35 秒。确保设备屏幕上出现审批提示并及时操作。如需关闭审批，删除 `PreToolUse` hook 或停止守护进程。

**Q: 设备连不上 PC？**

1. 确认守护进程运行：`curl -s -X POST http://127.0.0.1:9878/hook -d '{}'`（返回 `{}` 为正常）
2. 确认 Windows 防火墙已放行 7681 端口入站
3. 确认设备和 PC 在同一局域网
4. 检查设备配置：`buddy ws status`

**Q: 出现 `hook error: No stderr output` 错误？**

原因及解决方法：

1. **旧版 bug（已修复）**：守护进程运行但无设备连接时，35 秒超时后会阻断所有工具调用。请更新到最新版本并重新编译：`npm run build`
2. **守护进程仍在运行**：停止旧进程（`taskkill /F /PID <PID>`）后重启
3. **hooks 重复注册**：检查是否同时在 `~/.claude/settings.json` 和 `--plugin-dir` 中都配置了 PreToolUse hook，删除其中一处
