# tuya-pocket-buddy 守护进程

> 本 README 只描述 `daemon/` 这一 Python 包。面向 **Claude Code CLI 用户**
> 与本地开发者，说明如何让 Claude Code 通过这个插件，用蓝牙找到并连接
> 一台已经刷好固件的 **Tuya T5AI-Pocket**，把会话/权限事件送到设备屏幕与
> 按键。

---

## 1. 这个进程做什么

它是一个常驻后台的 Python 进程，同时扮演三个角色：

- **BLE 中央** —— 扫描名字形如 `Claude_XXXX` 的 T5AI-Pocket 外设，连接到
  Nordic UART Service（固件文档 `docs/protocol/BLE_WIRE_PROTOCOL.md`），
  用换行分隔的 JSON 帧与设备双向通信。
- **Hook HTTP 服务器** —— 仅监听 `127.0.0.1:9878`，接收 Claude Code 的
  Hook 载荷（`SessionStart / UserPromptSubmit / PreToolUse / PostToolUse /
  Stop`），转成心跳/权限请求帧推到设备。
- **权限桥** —— `PreToolUse` 发起时阻塞等待设备上按键决策，把
  `approve / deny / approve+permanent` 回传给 Claude Code。

架构示意：

```
Claude Code ──hook──▶  127.0.0.1:9878  ──BLE──▶  T5AI-Pocket
                             │                       │
                             ▼                       ▼
                     daemon.log / state.json    OLED + 物理按键 + LED
```

> 本插件**不负责刷固件**。固件构建/烧录请到
> `apps/tuya_t5_pocket/tuya_t5_pocket_ai/`，用 `tos.py build / flash`。

---

## 2. 前置条件

| 项 | 要求 |
|---|---|
| 操作系统 | Windows 10/11（主流程经过验证）；macOS/Linux 的 bash 脚本保留但**未充分测试** |
| Python | ≥ 3.10（`py -3` 或 `python3` 能直接启动） |
| Claude Code CLI | 已安装并能执行 `/plugins install` 命令 |
| Tuya T5AI-Pocket | 已烧录 `tuya_t5_pocket_ai` 固件（M1-A 及以上），能广播出 `Claude_XXXX` |
| 蓝牙 | Windows 设置 → 设备 → 蓝牙 **处于开启状态**；蓝牙适配器支持 BLE 4.2+ |
| 网络 | 安装阶段需能 `pip install bleak aiohttp` 等依赖 |

---

## 3. 快速上手（≈ 3 分钟）

> **关键概念**：本插件是一个"本地 marketplace + 插件"。Claude Code 不能
> 直接 `/plugin install` 一个普通目录，必须先把它注册成 marketplace，再从
> 这个 marketplace 装插件。仓库里 `.claude-plugin/marketplace.json` 与
> `.claude-plugin/plugin.json` 已就位，你只需要跑下面四步。

在任意 Claude Code 会话里依次：

```text
# 0. 注册本仓库为 marketplace（路径换成你自己的克隆位置；只用做一次）
/plugin marketplace add D:\tuya_proj\TuyaOpen\apps\tuya_t5_pocket\claude-cli-plugin

# 1. 从该 marketplace 安装插件（@后面的名字必须是 marketplace 名）
/plugin install tuya-pocket-buddy@tuya-pocket-buddy

# 上面两步成功后，重启一次 Claude Code 让 slash 命令注册生效
# 然后才能用 /buddy-* 系列命令：

# 2. 一次性环境安装（建 venv、装 Python 守护进程、合并 Hook 配置）
/buddy-install

# 3. 蓝牙扫描配对，10 秒内选到 Claude_XXXX 并持久化 MAC
/buddy-pair

# 4. 后台启动守护进程、连上设备、开始转发 Hook
/buddy-start
```

> **Windows 路径写法**：marketplace add 接受绝对路径或相对路径；带空格
> 的路径用双引号包起来：`/plugin marketplace add "D:\path with spaces\..."`。
> 如果你的 Claude Code 工作目录就在仓库里，也可以直接相对路径：
> `/plugin marketplace add ./apps/tuya_t5_pocket/claude-cli-plugin`。

命令执行完后，设备屏幕上 header 会变成 `BLE: linked`，Claude Buddy 进入
idle 态。后续**无需再做任何动作**——只要 daemon 在后台，每一次你和
Claude Code 的交互都会自动同步到设备。

### 验证插件已加载

`/plugin list` 应能看到 `tuya-pocket-buddy@tuya-pocket-buddy ✓ enabled`。
找不到时检查：

- 仓库里 `apps/tuya_t5_pocket/claude-cli-plugin/.claude-plugin/marketplace.json`
  与 `.claude-plugin/plugin.json` 都存在；
- `/plugin marketplace list` 里应能看到 `tuya-pocket-buddy`，状态正常；
- 没看到 `/buddy-*` 命令时**重启** Claude Code（slash 命令注册需要新会话）。

---

## 4. 首次安装：`/buddy-install` 做了什么

底层对应 `scripts/install.ps1`（Windows）或 `scripts/install.sh`（POSIX）：

1. **定位 Python 解释器** —— 优先 `py -3`，依次回退 `python / python3 /
   python3.12/.11/.10`，拒绝 < 3.10 的版本。
2. **创建 venv** —— 在用户状态目录下建立：
   - Windows：`%LOCALAPPDATA%\tuya-pocket-buddy\venv`
   - POSIX：`~/.tuya-pocket-buddy/venv`
3. **安装守护进程** —— `pip install <plugin_root>/daemon`（含 `bleak`、
   `aiohttp` 两条外部依赖）。
4. **合并 Hook 到 Claude 用户设置** ——
   `scripts/install-hooks.py --merge` 把 `settings/hooks.json` 里的
   `SessionStart / UserPromptSubmit / PreToolUse / PostToolUse / Stop`
   注入到 `~/.claude/settings.json`（Windows 为 `%USERPROFILE%\.claude\`），
   并保留一份 `settings.json.buddy-backup-<epoch>`；重跑安装**不会**重复
   追加（由 `_tuya_pocket_buddy_managed` 标记保证幂等）。

> 安装是幂等的。任何步骤失败都会抛异常并保留原 `settings.json`，可以把
> 报错贴出来重跑。

### 手动等价命令（如果 slash 命令不可用）

PowerShell：

```powershell
cd apps\tuya_t5_pocket\claude-cli-plugin
powershell -ExecutionPolicy Bypass -File scripts\install.ps1
```

Bash（未充分测试，参考用）：

```bash
cd apps/tuya_t5_pocket/claude-cli-plugin
bash scripts/install.sh
```

---

## 5. 打开蓝牙并配对：`/buddy-pair`

对应 `python -m tuya_pocket_buddy pair`，在当前进程里做一次性的 BLE 扫描。

### 动作序列

1. 确认 **Windows 蓝牙已开启**（设置 → 设备 → 蓝牙和其他设备）。如果
   系统使用的是 USB 外置 BLE 适配器，插入后等 WinRT 栈识别完再运行。
2. 把 T5AI-Pocket 开机、留在主屏幕（不要在别的配对流程里）；固件会广播
   出 `Claude_XXXX`（`XXXX` 为 MAC 尾四字节 Hex）。
3. 运行：

   ```
   /buddy-pair
   ```

4. 扫描进行 **10 秒**：
   - 只找到一台时自动选中，并持久化到 `state.json`；
   - 找到多台时会列出类似 `[0] Claude_7A3F  AA:BB:CC:...`，终端里输入下标
     回车。
5. **首次连接**时，Windows 可能弹出一次 WinRT 权限授权对话框（"允许此应用
   使用蓝牙"），选允许即可。

持久化结果：

```
state.json 内容（示例）
{
  "device_address": "AA:BB:CC:11:22:33",
  "owner_name":     "alice"
}
```

之后每次 `/buddy-start` 都会直接用这个 MAC，不再扫描。

### 换一台设备？

```
/buddy-unpair     # 发送一次 cmd:"unpair"（见固件 gap table）并清掉 MAC
/buddy-pair       # 重新扫描选新设备
```

> 注意：固件 v1.0 对 `cmd:"unpair"` 只做表面 ack，**BLE bond 实际不会被
> 擦除**。想彻底清理要在 Windows 的蓝牙设置里手动移除该设备。

---

## 6. 启动守护进程：`/buddy-start`

对应 `scripts/start.ps1` 或 `scripts/start.sh`，后台拉起
`python -m tuya_pocket_buddy run`。

启动后状态目录下会出现：

```
%LOCALAPPDATA%\tuya-pocket-buddy\
├─ daemon.pid        # 进程 PID
├─ daemon.log        # 按日轮转，保留 7 天
├─ state.json        # device_address、owner_name
└─ venv\             # 隔离虚拟环境
```

幂等：`daemon.pid` 指向一个仍在运行的进程时，`/buddy-start` 是 no-op。

连接成功后：

- 设备 header 显示 `BLE: linked` 和 `HH:MM`（时间同步自主机）。
- 主屏左侧人格进入 `IDLE` 状态；LED 进入 `ON_DIM`。
- `daemon.log` 里会看到类似：
  `daemon: starting; config=...`、`ble: connected`、`hook: POST /hook 200`。

---

## 7. 日常使用 —— Hook 自动转发 + 按键审批

### 7.1 已经注入的 Hook

安装阶段合并到 `~/.claude/settings.json` 的五条 Hook 都是同一套 `curl`：

```
curl -sS --max-time <N> -X POST --data-binary @- http://127.0.0.1:9878/hook \
     2>/dev/null || echo '{}'
```

| Hook              | timeout | 行为 |
|---|---|---|
| `SessionStart`    | 3 s  | 通知守护进程新会话开始 |
| `UserPromptSubmit`| 3 s  | 最新提示词推给设备（只取摘要） |
| `PreToolUse`      | 40 s | **阻塞**等待设备按键决策（`ENTER/LEFT/RIGHT`） |
| `PostToolUse`     | 3 s  | 工具完成后同步 entries |
| `Stop`            | 3 s  | 会话结束，设备进入 celebrate 片段 |

`|| echo '{}'` 是保险：**守护进程不在线时，Claude Code 不会卡住**，只是
设备不同步。

### 7.2 典型权限审批流

1. Claude 准备调用 `Read / Bash / Write` 等工具，`PreToolUse` 触发；
2. Hook 把 `{tool, hint, id}` 推到 daemon，daemon 转成
   `{"cmd":"prompt",...}` 通过 BLE 发到设备；
3. 设备屏幕弹审批卡片，人格进入 `ATTENTION`，LED 慢闪；
4. 在设备上按：
   - `KEY_ENTER` → `decision="once"`（本次批准）
   - `KEY_LEFT`  → `decision="deny"`
   - `KEY_RIGHT` → `decision="always"`（永久白名单）
5. 设备把 `{"cmd":"permission","id":"<id>","decision":"..."}` 回发，
   daemon 转回 Claude Code 的 Hook 响应；人格进入 `HEART` 短动画；
6. 全程若超过 40 秒没响应，Hook 走 `|| echo '{}'` 默认放行。

### 7.3 设备上辅助键位

- `KEY_UP / KEY_DOWN`：entries 面板上下翻
- `KEY_LEFT / KEY_RIGHT`（**无审批时**）：切换 18 个 ASCII 人格，选择会写到
  设备 `tal_kv` 持久化
- `KEY_JOYCON`：请求主机重发一次快照（`{"cmd":"status"}`）
- `KEY_ESC`：返回上一屏

完整交互规格见 `apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/UI_INTERACTION_zh.md`。

---

## 8. 查看状态 / 停止 / 解绑

```
/buddy-status     # daemon 是否在跑、MAC、最近日志尾行
/buddy-stop       # 停守护进程（清掉 daemon.pid，保留 state.json）
/buddy-unpair     # 一次性 unpair 帧 + 清 device_address
```

开发者可直接调用包：

```
python -m tuya_pocket_buddy status
python -m tuya_pocket_buddy pair
python -m tuya_pocket_buddy unpair
python -m tuya_pocket_buddy run    # 前台运行，调试用
```

---

## 9. 开发者模式（前台、无 Hook 注入）

调试 daemon 代码时绕过 slash 命令：

```powershell
cd apps\tuya_t5_pocket\claude-cli-plugin\daemon
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
python -m tuya_pocket_buddy run         # 前台，日志打到 stdout + daemon.log
```

测试：

```powershell
pytest -q
```

---

## 10. 故障排查

| 现象 | 可能原因 / 排查 |
|---|---|
| `Unknown command: /buddy-install` | 插件还没装上 Claude Code。按 §3 第 0、1 步先 `/plugin marketplace add` 再 `/plugin install tuya-pocket-buddy@tuya-pocket-buddy`，最后**重启** Claude Code |
| `/plugin install` 输入后只显示 (no content) | 你少了 `<plugin>@<marketplace>` 参数；正确写法：`/plugin install tuya-pocket-buddy@tuya-pocket-buddy` |
| `/plugin marketplace add` 报路径不存在 | Windows 路径用反斜杠或正斜杠都可以，但带空格必须双引号；确认目录里能看到 `.claude-plugin/marketplace.json` |
| `/buddy-install` 抱怨 Python 版本 | 安装 Python ≥ 3.10，并确保 `py -3 -V` 或 `python --version` 能看到；必要时在 PATH 里调顺序 |
| `/buddy-pair` 一直 "no Claude_XXXX peripheral found" | ① Windows 蓝牙未开；② 设备未通电 / 不在主屏；③ 之前配过而未清，Windows 蓝牙设置里先移除旧 `Claude_XXXX`；④ 距离 > 3 m |
| 配对时弹 WinRT 权限但拒绝了 | Windows 设置 → 隐私 → 蓝牙 → 允许 "Python" / `tuya-pocket-buddy` 访问；或在"其他蓝牙应用"里重新勾选 |
| 设备 header 一直 `BLE: -` | 查 `daemon.log` 尾部是否有 `bleak.exc.BleakError`；检查 MAC 在 Windows 蓝牙设置里有没有配对残留；必要时 `/buddy-unpair` → 清 Windows 蓝牙记录 → `/buddy-pair` |
| Claude Code 的 PreToolUse 明显变慢 | daemon 挂了但 Hook 还在尝试——`/buddy-status` 看 pid；真要临时屏蔽可删掉 `~/.claude/settings.json` 里带 `_tuya_pocket_buddy_managed` 的条目，或 `install-hooks.py --unmerge` |
| 设备能连上，但审批按键无反应 | 确认设备屏幕已进入 `ATTENTION`（有审批卡片）；`daemon.log` 有无 `permission: resolved id=...`；可能是 40 s Hook 超时已走默认放行 |
| 端口 9878 被占 | 改环境变量 `TUYA_POCKET_BUDDY_PORT=<port>`，同时改 `settings/hooks.json` 里对应端口后 `install-hooks.py --merge` 重注入 |
| Windows Defender 告警守护进程访问蓝牙 | 首次执行时选"允许专用网络和公用网络"；或为 `%LOCALAPPDATA%\tuya-pocket-buddy\venv\Scripts\python.exe` 添加入站规则 |

需要更详细的运行期证据，看：

```
Get-Content "$env:LOCALAPPDATA\tuya-pocket-buddy\daemon.log" -Tail 200 -Wait
```

---

## 11. 安全约束（本项目强制）

- Hook 服务器**只**监听 `127.0.0.1:9878`，从不开放公网；需要远程联调时只
  能通过 SSH 端口转发，不得直接改成 `0.0.0.0`。
- `daemon.log` 只记录结构化字段：`cmd / tool / decision / ble state`。
  `hint` 与 `prompt` 原文会被截断/脱敏，不落完整原文。
- `state.json` 只存 MAC + 昵称，不含任何凭据；若被误提交，MAC 不视为强
  机密，但仍建议 `git clean`。
- 不会请求蓝牙以外的系统权限；不会写注册表；不会尝试修改固件。

---

## 12. 文件地图

```
apps/tuya_t5_pocket/claude-cli-plugin/
├─ plugin.json                        # Claude Code 插件清单
├─ commands/                          # 六条 slash 命令（buddy-install/pair/...）
├─ settings/hooks.json                # 合并进 ~/.claude/settings.json 的 Hook 块
├─ scripts/                           # install/start/stop/status（.ps1 + .sh）
│  ├─ run.py                          # slash 命令 → 对应 .ps1/.sh 的跨平台分发
│  └─ install-hooks.py                # 幂等合并/拆卸 Hook
└─ daemon/                            # 本 README 所在目录
   ├─ pyproject.toml
   ├─ tuya_pocket_buddy/
   │   ├─ __main__.py                 # 入口（run / pair / unpair / status）
   │   ├─ ble_client.py               # bleak 扫描+连接+重连
   │   ├─ hook_server.py              # aiohttp 127.0.0.1:9878
   │   ├─ hook_router.py              # hook 载荷 → BLE JSON 帧
   │   ├─ permissions.py              # PreToolUse 阻塞 → 设备按键
   │   ├─ wire.py                     # JSON 帧编解码
   │   ├─ state.py / config.py        # 状态目录 / 持久化 / 日志轮转
   └─ tests/                          # pytest + pytest-asyncio，不依赖真实设备
```

如需对协议字段、固件状态机、或端到端 BLE 抓包留档进行追溯，请到
`apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/`（协议基线）与
`apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/UI_INTERACTION_zh.md`（交互规格）。
