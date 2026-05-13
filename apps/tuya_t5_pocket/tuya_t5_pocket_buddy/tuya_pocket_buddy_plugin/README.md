# Tuya Pocket Buddy Plugin 使用文档

## 概述

`tuya_pocket_buddy_plugin` 是一个 Claude Code 插件，运行 PC 端 Node.js 守护进程，作为 Claude Code CLI 与 T5AI Pocket 硬件设备之间的桥梁。

**工作原理：**

```
┌─────────────┐      Hook HTTP       ┌──────────────────┐     WebSocket      ┌──────────────┐
│ Claude Code  │  ──────────────────► │  Buddy Daemon    │ ◄───────────────►  │  T5AI Pocket │
│   CLI        │  localhost:9878      │  (Node.js)       │   0.0.0.0:7681     │  硬件设备     │
└─────────────┘                      └──────────────────┘                    └──────────────┘
```

- **Hook Server** (HTTP :9878)：接收 Claude Code 的 Hook 事件（会话启动、工具调用、结束等）
- **WebSocket Server** (:7681/buddy)：与 T5AI Pocket 设备保持双向通信
- **Permission Bridge**：硬件审批流程——设备可以批准或拒绝 Claude 的工具调用

## 功能

| 功能 | 说明 |
|------|------|
| 实时状态镜像 | 将 Claude 会话状态（token 用量、活跃会话、模型信息等）推送到设备显示 |
| 硬件审批 | 设备可以通过按键批准/拒绝 Claude 的 PreToolUse 请求 |
| 时间同步 | 自动同步 PC 时间和时区到设备 |
| 操作日志 | 记录最近 8 条工具调用，在设备上滚动显示 |
| 多设备支持 | 最多同时连接 4 台设备 |

## 快速开始

### 环境要求

- Node.js >= 18
- npm
- Claude Code CLI（已安装并可用）

### 第一步：获取插件源码

```bash
git clone https://github.com/tuya/TuyaOpen.git
cd TuyaOpen
```

插件位于 `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/tuya_pocket_buddy_plugin/`。

### 第二步：加载插件到 Claude Code

使用 `--plugin-dir` 参数启动 Claude Code，将插件目录传入：

```bash
claude --plugin-dir ./apps/tuya_t5_pocket/tuya_t5_pocket_buddy/tuya_pocket_buddy_plugin
```

> **这一步做了什么？** Claude Code 读取插件目录下的 `.claude-plugin/plugin.json` 识别插件，自动加载 `commands/` 下的 Slash Commands 和 `hooks/hooks.json` 中的 Hooks 配置。插件的所有功能立即可用，无需手动配置。

### 第三步：安装依赖并编译

在 Claude Code 终端中执行：

```
/tuya-pocket-buddy:install
```

这会自动运行 `npm install` 和 `npm run build` 编译 TypeScript 源码。**首次使用时需要执行一次。**

### 第四步：启动守护进程

```
/tuya-pocket-buddy:start
```

启动成功后会显示：
- Hook server: `http://127.0.0.1:9878`
- WebSocket server: `ws://0.0.0.0:7681/buddy`

### 第五步：连接设备

T5AI Pocket 设备开机后，通过设备 CLI 配置 WebSocket 地址：

```
buddy ws set <PC的IP地址> 7681
```

设备自动连接到 `ws://<IP>:7681/buddy`，连接后屏幕显示 Claude 实时状态。

---

### 所有 Slash Commands

| 命令 | 说明 |
|------|------|
| `/tuya-pocket-buddy:install` | 安装 npm 依赖并编译 TypeScript |
| `/tuya-pocket-buddy:start` | 启动守护进程 |
| `/tuya-pocket-buddy:stop` | 停止守护进程 |
| `/tuya-pocket-buddy:status` | 查看守护进程状态 |
| `/tuya-pocket-buddy:uninstall` | 停止守护进程 |

### 日常使用

```bash
# 每次启动 Claude Code 时带上 --plugin-dir 参数
claude --plugin-dir ./apps/tuya_t5_pocket/tuya_t5_pocket_buddy/tuya_pocket_buddy_plugin

# 然后在 Claude 终端中：
/tuya-pocket-buddy:start      ← 启动守护进程
# ... 正常使用 Claude Code ...  ← 设备自动显示状态
/tuya-pocket-buddy:stop       ← 结束时停止
```

> **提示：** 设置 shell alias 简化每次启动：
> ```bash
> # 添加到 ~/.bashrc 或 ~/.zshrc
> alias claude-buddy='claude --plugin-dir /absolute/path/to/tuya_pocket_buddy_plugin'
> ```
> 之后直接运行 `claude-buddy` 即可。

## 插件工作原理

### 插件目录结构

Claude Code 插件系统通过以下约定自动发现组件：

```
tuya_pocket_buddy_plugin/
├── .claude-plugin/
│   └── plugin.json          # 插件元信息（name 字段决定命令的命名空间）
├── commands/                 # Slash Commands（自动注册为 /tuya-pocket-buddy:xxx）
│   ├── install.md           # → /tuya-pocket-buddy:install
│   ├── start.md             # → /tuya-pocket-buddy:start
│   ├── stop.md              # → /tuya-pocket-buddy:stop
│   ├── status.md            # → /tuya-pocket-buddy:status
│   └── uninstall.md         # → /tuya-pocket-buddy:uninstall
├── hooks/
│   └── hooks.json           # Hooks 配置（插件加载时自动生效）
├── scripts/
│   └── hook_handler.js      # PreToolUse 同步 hook 脚本
├── settings/
│   └── hooks.json           # Hooks 配置模板（供手动安装参考）
├── src/                      # TypeScript 源码
│   ├── index.ts             # 入口，启动所有服务
│   ├── config.ts            # 配置常量
│   ├── hook-server.ts       # HTTP hook 接收服务
│   ├── hook-router.ts       # 事件路由 + 状态聚合
│   ├── ws-server.ts         # WebSocket 设备连接管理
│   ├── permissions.ts       # 硬件审批桥接
│   └── wire.ts              # 通信协议定义
├── dist/                     # 编译输出
├── package.json
└── tsconfig.json
```

### 关键机制

**命令命名空间：** `plugin.json` 中的 `name` 字段（`tuya-pocket-buddy`）作为命令的命名空间前缀。`commands/install.md` 自动注册为 `/tuya-pocket-buddy:install`。

**Hooks 自动加载：** `hooks/hooks.json` 中的 hooks 配置在插件加载时自动生效。不需要手动编辑 `~/.claude/settings.json`。插件卸载后 hooks 自动移除。

**`${CLAUDE_PLUGIN_ROOT}`：** 在 hooks 命令中使用此变量引用插件内部文件，Claude Code 自动替换为插件的实际安装路径。

## 配置参数

配置在 `src/config.ts` 中定义：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `hookPort` | 9878 | Hook HTTP 服务端口（仅监听 127.0.0.1） |
| `wsPort` | 7681 | WebSocket 服务端口 |
| `maxDevices` | 4 | 最大同时连接设备数 |
| `permissionTimeoutMs` | 35000 | 审批超时时间（毫秒），超时自动拒绝 |
| `heartbeatIntervalMs` | 10000 | 心跳推送间隔（毫秒） |
| `maxPayloadBytes` | 65536 | 最大 Hook 负载大小 |

## 通信协议

### PC → 设备 (Heartbeat JSON)

每 10 秒或 Hook 事件触发时推送：

```json
{
  "total": 3,
  "running": 1,
  "waiting": 0,
  "tokens": 15000,
  "tokens_today": 5000,
  "model": "claude-opus-4-6",
  "ver": "1.0.30",
  "entries": [{"t": "t", "n": "Bash", "h": "git status"}],
  "sessions": [{"sid": "abc123", "name": "fix bug", "model": "opus", "tok": 1200, "proj": "myproject", "run": true, "ent": ["Read", "Edit"]}],
  "mstats": [{"m": "opus", "tok": 10000}],
  "daily": [500, 1200, 800],
  "prompt": {"id": "p_123", "tool": "Bash", "hint": "rm -rf /tmp"},
  "time": [1715500000, 480]
}
```

### 设备 → PC (Device Frame JSON)

```json
{"cmd": "permission", "id": "p_123", "decision": "once"}
{"cmd": "hb_req"}
{"cmd": "asr", "text": "approve"}
```

## 手动安装（不使用插件系统）

如果不通过 `--plugin-dir` 加载插件，也可以手动操作：

```bash
# 1. 安装和编译
cd tuya_pocket_buddy_plugin && npm install && npm run build

# 2. 手动合并 hooks 到 Claude Code 设置
# 将 settings/hooks.json 的内容合并到 ~/.claude/settings.json

# 3. 启动
node dist/index.js run &

# 4. 停止
pkill -f "node dist/index.js run"
```

## 卸载旧版 claude-cli-plugin

如果你之前从 `xb/claude_buddy_pocket` 分支安装了旧版 `claude-cli-plugin`，按以下步骤清理：

### 1. 停止旧进程

```bash
ps aux | grep -i "claude-cli-plugin\|buddy.*daemon" | grep -v grep
pkill -f "claude-cli-plugin"
```

### 2. 删除旧的 hooks 配置

```bash
# 查看是否有旧 hooks
cat ~/.claude/settings.json | python3 -m json.tool | grep -A2 "claude-cli-plugin"
```

如果有匹配条目，编辑 `~/.claude/settings.json` 删除它们。

### 3. 删除旧插件文件

```bash
rm -rf apps/tuya_t5_pocket/tuya_t5_pocket_buddy/claude-cli-plugin
npm uninstall -g claude-cli-plugin 2>/dev/null
```

## 常见问题

**Q: 设备连不上？**
- 确认守护进程已启动：`/tuya-pocket-buddy:status`
- 确认 PC 防火墙允许 7681 端口入站
- 确认设备和 PC 在同一网络
- 检查设备 WS 配置：`buddy ws status`

**Q: Claude 执行工具时卡住了？**
- 可能是 PreToolUse hook 在等待设备审批（35 秒超时）
- 如果不需要硬件审批，编辑 `hooks/hooks.json` 删除 `PreToolUse` 部分

**Q: 想禁用硬件审批但保留状态显示？**
- 编辑 `hooks/hooks.json`，删除 `PreToolUse` 条目
- 运行 `/reload-plugins` 使更改生效
