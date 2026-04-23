# Claude CLI Buddy · T5AI-Pocket 实施计划（中文版）

> **本计划依据 `doc/dev.md` 整理，面向 Windows 开发机。** 本计划替代已删除的 `PROJECT_OVERVIEW.md`、`PORTING_PLAN.md`、`BUDDY_UI_PORT.md`，作为本项目当前阶段的唯一权威实施文档。
>
> **给执行者：** 每个里程碑在落地前须先走一次 `superpowers:brainstorming → superpowers:writing-plans → superpowers:executing-plans` 的子循环（TDD 级的任务步骤写在各自子计划中，保存到 `docs/superpowers/plans/<日期>-<名称>.md`）。本计划本身为里程碑级路线图，不列 TDD 单步。

---

## 1. 项目目标（来自 `dev.md`）

- **目标：** 在 **T5AI-Pocket**（384×168 墨水屏，LVGL v9）上运行固件，通过 BLE 与电脑上的 **Claude Code CLI** 对接，实时显示 Claude 的会话、状态、审批请求；用户使用摇杆 + `Enter`/`Esc` 两颗物理按键完成操作，LED 辅助状态提示。
- **兼容性：** 默认目标协议等价于 **Claude Desktop**（参考 `apps/tuya_t5_pocket/claude-desktop-buddy` 的 NUS 协议）。
- **平台：** 本阶段**仅支持 Windows**。macOS / Linux 作为后续扩展，代码需预留平台分支但不提供可执行实现。
- **文档语言：** 项目内所有新增/改写文档使用**中文**输出。

### 1.1 模块拆分（对齐 `dev.md` §项目模块）

| 模块 | 职责 | 当前状态 |
|------|------|---------|
| A. UI | 18 个 ASCII 角色 + 状态/数据/审批面板 + 自定义 GIF 角色占位 UI | 已有基础 `buddy_main_screen.c`（仅文本态），18 个角色未移植 |
| B. 插件 | Claude Code CLI Plugin（Hook 转发），一键安装 | `apps/tuya_t5_pocket/claude-cli-plugin/` 骨架已存在，需中文化与清理 |
| C. BLE 连接 | Windows 下蓝牙中央连接 T5AI-Pocket 外设，实时显示连接态 | 固件端 `buddy_ble.c` 已存在；Windows 端 `bleak` 客户端需联调 |
| D. 通信协议 | 专用文档介绍交互过程 | `docs/protocol/BLE_WIRE_PROTOCOL.md` 英文已存在，需中文版 |

### 1.2 明确不做（非目标）

- 不做 Tuya 云联动、不做统计等级系统（成长值/心情/饱食度等在本阶段全部移除或不显示）。
- 不做自定义 GIF 角色的真实 Flash 写入与渲染——**仅保留 UI 入口/占位**（`dev.md` §UI 设计：用户自定义角色 UI 保留，不进行开发）。
- 不做 Secure Connections 配对、OTA。
- 不做 macOS / Linux 端可执行脚本（仅保留平台分支占位）。

---

## 2. 架构与技术栈

```
┌────────────── Windows 主机 ──────────────┐        ┌────────── T5AI-Pocket 设备 ──────────┐
│ Claude Code CLI                           │        │ tuya_main.c                           │
│   └ 插件 Hook (PreToolUse / SessionStart) │        │   ├ buddy_ble.c  (TAL BLE 外设)       │
│         │                                 │        │   │    ↕  NUS  (0xFD50 Tuya 服务)     │
│         ▼                                 │        │   ├ buddy_protocol.c (JSON 行协议)    │
│ buddy_daemon.py (bleak BLE 中央)          │←──BLE──▶│   ├ buddy_state.c    (状态推导)       │
│   ├ HTTP 本地接口（供 Hook 调用）          │        │   ├ buddy_ui/                         │
│   └ pid 文件 + 生命周期管理               │        │   │    ├ persona_registry  (18 角色) │
└───────────────────────────────────────────┘        │   │    ├ ascii_persona     (渲染)    │
                                                     │   │    ├ buddy_main_screen             │
                                                     │   │    ├ buddy_approval_screen         │
                                                     │   │    ├ buddy_gif_stub    (占位)      │
                                                     │   │    └ buddy_led        (tdl_led)   │
                                                     │   └ LVGL v9 + lv_vendor                │
                                                     └────────────────────────────────────────┘
```

**技术栈：**
- 固件：TuyaOpen SDK、TAL BLE、LVGL v9、`lv_vendor`、`tdl_led`、cJSON、`tal_sw_timer`、`tal_kv`
- 主机：Windows 10/11、Python 3.10+、`bleak`、Claude Code CLI（插件形式）
- 构建/刷写：`tos.py build` / `tos.py flash` / `tos.py monitor`（Windows PowerShell）

---

## 3. 前置清理（已完成部分，本计划启动时执行）

- [x] 删除 `doc/PROJECT_OVERVIEW.md`（描述错误的 M5StickC 参考项目，硬件规格不符）
- [x] 删除 `doc/PORTING_PLAN.md`（目标应用名 `tuya_t5_pocket_buddy` 与实际 `tuya_t5_pocket_ai` 不一致，方案已过时）
- [x] 删除 `doc/BUDDY_UI_PORT.md`（M1-A 早期迭代总结，内容已被代码与 `UI_INTERACTION.md` 覆盖）

**其余文档保留：**
- `doc/dev.md`：需求源头
- `doc/UI_INTERACTION.md`：当前 M1-A UI 交互规格（英文，见 M1-UI 的中文化任务）
- `docs/protocol/BLE_WIRE_PROTOCOL.md` 及 `docs/protocol/baseline/`：协议基线
- `docs/superpowers/specs/` 与 `docs/superpowers/plans/`：历史 spec/plan（保留供追溯）

**其余代码保留：**
- `src/display/ui/buddy_ui/`（`buddy_ble.c/h`、`buddy_data.h`、`buddy_main_screen.c/h`、`buddy_ui_entry.h`）
- `apps/tuya_t5_pocket/claude-cli-plugin/`（Python 守护进程骨架）

**若发现下列情形，执行时应追加删除并在提交信息中说明原因：**
- 新增的 `.md` 文件与 `doc/dev.md`、本计划重复或矛盾
- 与“非目标”（§1.2）相关的历史代码（如成长值/等级/饱食度渲染）

---

## 4. Windows 构建与验证回路（所有固件里程碑通用）

所有涉及固件的里程碑须以该回路收尾，并把产物日志归档到 `docs/verification/<里程碑>-<yyyymmdd>.log`。

```powershell
# 1. 进入应用目录
cd D:\tuya_proj\TuyaOpen\apps\tuya_t5_pocket\tuya_t5_pocket_ai

# 2. 构建（必须以 "BUILD SUCCESS" 结束）
tos.py build

# 3. 刷写（替换为实际串口，如 COM7）
tos.py flash -p COM7

# 4. 监控（T5AI 默认 460800 波特率）；抓取启动日志
tos.py monitor -p COM7 -b 460800

# 5. 若可用，使用硬件调试辅助脚本归档日志
python .agents\skills\agent-hardware-debug-helper-tools\agent_target_tool.py `
        debug-session run -p COM7 --log-suffix "<milestone>_<yyyymmdd>" --hw-reset
```

> Windows 若出现端口占用，先 `Get-Process | ? { $_.ProcessName -like "*tos*" }` 确认无残留进程，再运行。禁止通过修改代码绕过校验来使测试"通过"。

---

## 5. 里程碑总览

| # | 名称 | 依赖 | 可并行 | 产出 |
|---|------|------|--------|------|
| M0 | 协议文档中文化 + 基线留档 | — | 否 | `BLE_WIRE_PROTOCOL_zh.md`、基线日志 |
| M1-UI | 18 个 ASCII 角色 + 状态/审批/占位 GIF UI | M0 | 与 M2-BLE 并行 | `persona_registry.*`、`ascii_persona.*`、`buddy_approval_screen.*`、`buddy_gif_stub.*`、`buddy_led.*` |
| M2-BLE | Windows 中央（bleak）联调 | M0 | 与 M1-UI 并行 | 更新 `claude-cli-plugin/daemon/`，端到端 BLE 打通 |
| M3-Plugin | CLI 插件中文化与一键安装 | M2-BLE | 否 | 中文 `claude-cli-plugin/README.md`、命令文案、`install.ps1` 改进 |
| M4-Tools | GIF 预处理/刷写 CLI **接口冻结** | M1-UI | 否 | `tools/README.md`、文档级格式规范 |
| M5 | Windows 端到端集成验证 | M1-UI ∧ M2-BLE ∧ M3-Plugin | 否 | `docs/verification/umbrella-e2e-*` |

---

## 6. M0 — 协议文档中文化 + 基线留档

**目标：** 在现有 `BLE_WIRE_PROTOCOL.md` 的基础上产出中文版本 `BLE_WIRE_PROTOCOL_zh.md`，覆盖 `dev.md` §通信协议 要求的"整个交互过程"说明，并固化当前 HEAD 的设备日志为基线。

**文件：**
- 新增：`apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL_zh.md`
- 新增（日志）：`apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/<短 hash>-baseline-zh.log`
- 参考：`apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md`
- 参考：`apps/tuya_t5_pocket/claude-desktop-buddy/REFERENCE.md`（若仓库中为空，使用 `docs/superpowers/specs/2026-04-21-wire-protocol-design.md`）

**必须包含的章节（中文）：**

1. 服务与特征（Tuya 0xFD50 Write/Notify 映射、MTU 协商）
2. 广播名推导：MAC 后四字节 Hex（无 MAC 时回退 devid 尾四位），格式 `Claude_XXXX`
3. 帧格式：以 `\n` 分隔的 JSON 行
4. 报文字典：
   - 设备→主机：`{"cmd":"permission", "id":..., "decision":"once|deny|always"}`、`{"cmd":"status"}`
   - 主机→设备：心跳（`total/running/waiting/msg/tokens/owner/entries/time`）、权限请求（`prompt.{id,tool,hint}`）、文件包传输（后续 M4 规范）
5. `id` 回显规则：**MUST**，位级一致
6. 心跳节奏与断链处理（UI 层 "BLE: linked/-" 指示）
7. 时间同步（`{"time":[epoch, tz]}`）与只影响 UI 本地钟不改写 RTC 的约束
8. `entries[]` 语义与最大 8 条缓存（见 `UI_INTERACTION.md`）
9. 与 REFERENCE.md / 当前固件的字段差异表（Gap Table），每项注明所在里程碑
10. 修订流程（"不经 M0 子循环不得直改协议文档"）

**退出标准：**
- [ ] 中文版本与英文版本字段一一对应，中文版覆盖 `dev.md` §通信协议 要求的"整个交互过程"
- [ ] Gap Table 中每条未实现字段标注所在里程碑或"后续"
- [ ] 通过 `tos.py build` + 刷写当前 HEAD 抓取一份干净启动日志，入库 `baseline/`
- [ ] 第二位评审人签名（内嵌评审注释或 commit trailer）

**验证动作：** 执行 §4 的验证回路，归档日志。

**提交模板：** `docs(protocol): 新增 BLE 协议中文版 + 基线日志`

---

## 7. M1-UI — 18 角色 + 状态/审批/GIF 占位

**目标：** 把 `apps/tuya_t5_pocket/claude-desktop-buddy/src/buddies/` 下 18 个 ASCII 角色迁移到 T5AI-Pocket，落地 `buddy_main_screen`（状态栏 + 角色 + 数据面板 + Entries）、`buddy_approval_screen`（审批）、`buddy_gif_stub`（自定义 GIF 占位），并接入 `tdl_led` 做状态提示。

### 7.1 UI 总体布局（384×168 横屏，单色 + 抖动）

```
┌─────────────────────────────────────────────────────────────┐  顶栏 20 px
│ Claude Buddy      BLE:linked       HH:MM      Claude_XXXX  │
├─────────────────────────────────────────────────────────────┤  主体 124 px
│  [ASCII 角色 ≤192×140]  │ 顶: msg / sessions / tokens / owner│
│                         │ 底: entries 最多 4 行（可 UP/DOWN 滚动）│
│                         │ 若收到权限请求 → 覆盖审批卡片       │
├─────────────────────────────────────────────────────────────┤  底栏 24 px
│ 上下文相关按键提示                                           │
└─────────────────────────────────────────────────────────────┘
```

### 7.2 按键映射（沿用 `UI_INTERACTION.md`）

| 按键 | 行为 | 发送帧 |
|------|------|--------|
| `KEY_ENTER` | 批准（有待审批时） | `{"cmd":"permission","id":"<id>","decision":"once"}` |
| `KEY_LEFT`  | 拒绝（有待审批时） | `{"cmd":"permission","id":"<id>","decision":"deny"}` |
| `KEY_RIGHT` | 批准且记住（有待审批时） | `{"cmd":"permission","id":"<id>","decision":"always"}` |
| `KEY_UP`/`KEY_DOWN` | entries 滚动（始终可用） | 无 |
| `KEY_JOYCON` | 请求主机重发快照 | `{"cmd":"status"}` |
| `KEY_ESC` | 返回上一屏 | 无 |

### 7.3 文件

- 新增：
  - `src/display/ui/buddy_ui/persona_registry.c/h` — 18 角色注册表，按 `persona_id` 派发
  - `src/display/ui/buddy_ui/ascii_persona.c/h` — ASCII 帧数据与渲染
  - `src/display/ui/buddy_ui/buddy_approval_screen.c/h` — 权限审批屏
  - `src/display/ui/buddy_ui/buddy_gif_stub.c/h` — GIF 角色占位（未安装包时显示"GIF N/A"与对应日志）
  - `src/display/ui/buddy_ui/buddy_led.c/h` — 包装 `tdl_led_manage.h`，每一次状态迁移记录日志
- 修改：
  - `src/display/ui/buddy_ui/buddy_main_screen.c/h` — 接入 `persona_registry`，顶栏显示 BLE 状态与 `HH:MM`，主体集成角色与 Entries
  - `src/display/ui/buddy_ui/buddy_data.h` — 按需增补 `persona_id`、`tdl_led` 状态枚举（仅由协议驱动）
- 删除：**占位** — 若发现 `buddy_main_screen.c` 中保留的历史 demo timer / stats / mood / fed / energy / level / xp 相关残留，在 M1-UI 的子计划中一并清除（与 `dev.md` 非目标一致）
- 参考：`apps/tuya_t5_pocket/claude-desktop-buddy/src/buddies/*.cpp`、`src/peripherals/led/tdl_led/include/tdl_led_manage.h`

### 7.4 18 角色清单（迁移时须全部保留）

| # | 角色 | 英文名 | # | 角色 | 英文名 | # | 角色 | 英文名 |
|---|------|--------|---|------|--------|---|------|--------|
| 1 | 水豚 | capybara | 7 | 章鱼 | octopus | 13 | 蝾螈 | axolotl |
| 2 | 鸭子 | duck | 8 | 猫头鹰 | owl | 14 | 仙人掌 | cactus |
| 3 | 鹅 | goose | 9 | 企鹅 | penguin | 15 | 机器人 | robot |
| 4 | 果冻 | blob | 10 | 乌龟 | turtle | 16 | 兔子 | rabbit |
| 5 | 猫 | cat | 11 | 蜗牛 | snail | 17 | 蘑菇 | mushroom |
| 6 | 龙 | dragon | 12 | 幽灵 | ghost | 18 | 胖猫 | chonk |

> 若 `claude-desktop-buddy/` 目录在本地仓库为空（非子模块），在 M1-UI 的 brainstorm 阶段先恢复引用项目（`git clone https://github.com/anthropics/claude-desktop-buddy.git` 到同路径下，**不提交**），再着手移植。

### 7.5 退出标准

- [ ] 18 个角色全部在设备上显示至少一帧，日志中可查 `[persona] id=<n> name=<...>`
- [ ] `buddy_main_screen` 不再有任何 demo timer / 等级 / 饱食度 / 心情代码路径
- [ ] 权限审批卡片在收到 `prompt` 时 1 秒内弹出，`id` 在响应帧中原样回显
- [ ] `tdl_led` 状态表与协议状态一一对应，每次变化产出一行日志
- [ ] `tos.py build` 通过，刷写后运行 30 分钟内存无泄漏（`free-heap` 漂移小于文档阈值）
- [ ] `doc/UI_INTERACTION.md` 更新为中文版本 `doc/UI_INTERACTION_zh.md`，并与 M1-UI 实际实现对齐；原英文可保留，但顶部加"以中文版为准"提示

**验证动作：** 执行 §4 验证回路，归档 `docs/verification/m1_ui-<yyyymmdd>.log`；另录制一段 15 秒视频/截图序列展示 18 角色切换，保存为 `docs/verification/m1_ui-personas.mp4`（或等效 .gif / 多张截图）。

**提交模板（每个角色一次）：** `feat(buddy): 移植 ASCII 角色 <名称>` / `feat(buddy): 接入 persona_registry` / `feat(buddy): 新增审批屏幕` 等。

---

## 8. M2-BLE — Windows 中央联调

**目标：** 在 Windows 上通过 `bleak` 实现稳定的 BLE 中央，扫描广播名为 `Claude_XXXX` 的 T5AI-Pocket，维持 Notify 订阅与 Write 通道，向上封装"断线重连 + 状态事件 + 收发 JSON 行"的本地接口，供 M3-Plugin 调用。

**文件（均在 `apps/tuya_t5_pocket/claude-cli-plugin/` 下；**当前已存在骨架，需根据 M0 协议文档校正**）：**
- 修改：`daemon/tuya_pocket_buddy/ble_client.py` — bleak 扫描/连接/Notify/Write；
  - 扫描过滤：`name.startswith("Claude_")`；去除任何明文设备地址日志（脱敏）
  - 重连策略：指数回退，最小 500 ms，最大 10 s，记录每一次重连时间戳
  - 线程模型：asyncio 事件循环专属线程；严禁让 Hook 直接阻塞
- 修改：`daemon/tuya_pocket_buddy/hook_server.py` — 本地 HTTP，仅监听 `127.0.0.1`
  - 拒绝监听 `0.0.0.0`；未认证请求直接返回 401
- 修改：`daemon/tuya_pocket_buddy/wire.py` — JSON 行组帧/拆帧，严格遵循 M0 协议文档字段
- 修改：`daemon/tuya_pocket_buddy/state.py` — `pid` 文件路径放到 `%LOCALAPPDATA%\TuyaPocketBuddy\daemon.pid`
- 修改：`scripts/start.ps1`、`scripts/stop.ps1`、`scripts/status.ps1` — 指向上述 pid 路径，避免硬编码 `C:\` 根目录
- 新增/调整：`daemon/tests/test_ble_client.py`、`test_hook_server.py`、`test_wire.py` — 单元测试须能在无设备的环境下运行（mock bleak）

**安全硬性要求（来自工作区规则）：**
- 所有本地监听默认 `127.0.0.1`；对外监听须**显式用户授权**
- 禁止在日志、状态文件中写入完整 BLE 地址、Token、权限请求原文；日志打印时对 `id`、`tool`、`hint` 截断脱敏
- `bleak` 扫描/连接超时必须显式设定；禁止裸 `await` 无限等
- Python 代码遵守 Windows 路径规范，所有路径 `pathlib.Path` 化；含空格时必须双引号

**退出标准：**
- [ ] `pytest daemon/tests/` 在 Windows（PowerShell）中全绿，覆盖 JSON 组帧、Hook 路由、权限签名、BLE 客户端状态机
- [ ] 在一台真实 T5AI-Pocket 上完成 30 分钟心跳稳定测试，无非预期断连
- [ ] 每一个 outbound 帧字节级匹配 M0 协议文档；permission 帧 `id` 原样回显
- [ ] `buddy-status` 子命令能同时显示：守护进程 PID、BLE 连接态、最近一次心跳时间、最近错误
- [ ] 日志中不得出现 BLE MAC 或权限原文
- [ ] macOS / Linux 分支在 `scripts/install.sh`、`ble_client.py` 平台分支位置返回"暂不支持 Windows 以外平台"（not yet supported），不跑通

**验证动作：**
```powershell
cd apps\tuya_t5_pocket\claude-cli-plugin\daemon
pip install -e .
pytest -q
```
将输出日志归档到 `docs/verification/m2_ble-<yyyymmdd>.log`。

**提交模板：** `feat(plugin): 中央稳定连接与重连` / `fix(plugin): 日志脱敏` 等。

---

## 9. M3-Plugin — Claude Code CLI 插件中文化 + 一键体验

**目标：** `dev.md` §插件 要求的"单独生成一份 README.md 讲解插件作用及如何安装"，并提供用户一键安装体验（Windows 优先）。

**文件：**
- 新增/重写：`apps/tuya_t5_pocket/claude-cli-plugin/README.md`（**中文**），章节至少覆盖：
  - 插件做什么：把 Claude Code 的 Hook 事件转发到 T5AI-Pocket 的 BLE 外设；把设备端按键决策回传给 CLI
  - 依赖：Python 3.10+、Windows 10/11、蓝牙 4.2+
  - 安装：
    ```powershell
    cd apps\tuya_t5_pocket\claude-cli-plugin
    powershell -ExecutionPolicy Bypass -File scripts\install.ps1
    ```
  - 使用：`/buddy-pair`、`/buddy-start`、`/buddy-status`、`/buddy-stop`、`/buddy-unpair`
  - 故障排查：端口占用、蓝牙关闭、权限被 Windows Defender 拦截等
  - 安全说明：本地回环监听、日志脱敏策略
- 修改：`commands/*.md` — 文案中文化，示例命令使用 PowerShell 语法
- 修改：`scripts/install.ps1`：
  - 自动创建虚拟环境 `.venv` 到 `%LOCALAPPDATA%\TuyaPocketBuddy\.venv`
  - `pip install -e daemon`
  - 把 `hooks.json` 合并到 Claude Code 用户配置（**不得覆盖**用户已有 hook；使用 JSON merge，冲突时交互式询问）
  - 失败时回滚并打印人类可读原因
- 修改：`scripts/install.sh`、`install-hooks.sh`、`start.sh`、`stop.sh`、`status.sh`：当前阶段仅保留文件并在第一行打印：
  ```bash
  echo "当前仅支持 Windows（PowerShell）。" >&2
  exit 2
  ```
- 删除：若在 `scripts/` 下发现与上述流程无关、未被引用的遗留脚本（例如废弃的 `run.py` 仅用于早期调试），在子计划中删除；保留之前须 `rg` 搜索确认无任何引用点

**退出标准：**
- [ ] 在一台干净 Windows 10/11 上：`powershell -ExecutionPolicy Bypass -File scripts\install.ps1` 一次成功，记录终端会话
- [ ] `/buddy-install` → `/buddy-start` 全流程无需二次干预
- [ ] `hooks.json` 合并不覆盖用户已有配置；卸载脚本能恢复原始 `hooks.json`
- [ ] 守护进程未运行时，Claude Code 触发 Hook 仍能立即获得 `{}` 响应，不阻塞 CLI（单元测试覆盖）
- [ ] `README.md` 通过 Markdown Lint（标题层级、代码块语言标签无误）

**验证动作：**
```powershell
# 在全新用户目录下模拟
$env:USERPROFILE = "C:\Users\buddy_clean_box"
powershell -ExecutionPolicy Bypass -File apps\tuya_t5_pocket\claude-cli-plugin\scripts\install.ps1
```

**提交模板：** `docs(plugin): 中文 README 与安装指引` / `feat(plugin): 改进 hooks.json 合并策略`

---

## 10. M4-Tools — GIF 预处理 / 刷写 CLI 接口冻结（仅文档）

**目标：** `dev.md` §UI 设计 要求"脚本对 GIF 预处理，通过 BLE 与 USB/Serial 刷写到设备 flash"。本阶段**只冻结接口契约（文档）**，不实现脚本；同时在协议文档追加"角色包传输"附录。

**文件：**
- 新增：`apps/tuya_t5_pocket/tuya_t5_pocket_ai/tools/README.md`
  - `prep_gif_pack.py`：输入 GIF → 输出 `.pack` 二进制（含头、状态帧表、每状态帧元数据、像素平面 offset）
  - `flash_character.py`：支持 `--transport ble|serial`，Windows 下 COM 端口枚举，校验 CRC32
  - 两脚本的 CLI 参数字典、输入输出 schema、退出码
- 修改：`docs/protocol/BLE_WIRE_PROTOCOL_zh.md` — 追加附录"角色包传输（serial + BLE）"
  - 起始标记、长度前缀、payload、CRC32 的逐字节定义
  - BLE 下分片大小（默认 MTU−3）
  - Serial 下波特率（460800）与流控

**退出标准：**
- [ ] 一名未参与 M1-UI 的工程师阅读 `tools/README.md` 后可直接开始实现，不再需要追加 brainstorm
- [ ] 包头字段逐字节规格（字节序、尺寸）已明确
- [ ] 两种传输均有起始标记、长度、CRC32 说明
- [ ] `BLE_WIRE_PROTOCOL_zh.md` 附录与 `tools/README.md` 交叉引用一致

**提交模板：** `docs(tools): 冻结 GIF 角色包格式与刷写 CLI 契约`

---

## 11. M5 — Windows 端到端集成验证

**触发条件：** M1-UI、M2-BLE、M3-Plugin 的退出标准全部满足。

**文件：**
- 新增：`docs/verification/umbrella-e2e-<yyyymmdd>.log`（设备侧日志）
- 新增：`docs/verification/umbrella-e2e-<yyyymmdd>-daemon.log`（守护进程日志）
- 新增：`docs/verification/umbrella-e2e-<yyyymmdd>.mp4`（或截图序列）

**步骤：**

1. 干净 Windows 10/11 账户，预装 Python 3.10+、Claude Code CLI
2. 使用 §4 回路刷写当前 HEAD 固件，保留监控会话
3. `/buddy-install` → `/buddy-start`，确认设备广播名与 M0 推导规则一致
4. 在 CLI 执行一条会触发 `PreToolUse` Hook 的命令（执行时根据 Claude Code 最新文档选定，**不得**硬编码可能被重命名的命令）
5. 设备端 1 秒内弹审批，按 `KEY_ENTER` 批准
6. CLI 收到 `allow` 并继续执行；检查：
   - 守护进程日志：1 条请求入、1 条决策出
   - 固件日志：1 条 `permission` 帧、`id` 位级匹配
   - 时间戳差 < 10 秒

**退出标准：**
- [ ] 三份证据全部提交
- [ ] 任一日志中不得出现 `ERROR` / `ASSERT` / `Mem Overflow`
- [ ] 往返时间 < 10 秒
- [ ] 断开蓝牙 30 秒后恢复，守护进程自动重连，UI 状态栏正确切回 `BLE:-` → `BLE:linked`

**失败处理：** 若协议层不一致回退 M0；若实现层不一致回退 M1-UI / M2-BLE。**严禁**通过直接修改 `BLE_WIRE_PROTOCOL*.md` 绕过 M0 流程。

---

## 12. 横切要求（所有里程碑共同遵守）

- [ ] 所有子项目 spec 存放于 `docs/superpowers/specs/<日期>-<名称>-design.md`
- [ ] 所有子项目 plan 存放于 `docs/superpowers/plans/<日期>-<名称>.md`
- [ ] 子计划遵循 `superpowers:writing-plans`：小步、精确路径、精确命令、可验证期望
- [ ] **禁止**在不经 M0 子循环的情况下修改 `BLE_WIRE_PROTOCOL*.md`（R1/R7 风险缓解）
- [ ] 固件 PR **必须**附带 §4 验证回路日志
- [ ] C 代码遵守工作区 TuyaOS C Style / C Security 规则（函数注释、`STATIC`、`s_` 前缀、内存配对、AES-GCM/≥128 位等）
- [ ] Python 代码使用 `pathlib`、`bleak`、`asyncio`；禁止 `subprocess.call` 拼接用户输入；日志脱敏
- [ ] 所有新增/修改文档使用中文；现有英文文档可以保留，但顶部注明"以中文版为准"（若存在中文对照）

---

## 13. 自查

**`dev.md` 每项要求对应的里程碑：**

| `dev.md` 条目 | 对应里程碑 |
|---|---|
| UI 设计：18 ASCII 角色全部保留 | M1-UI (§7) |
| UI 设计：自定义 GIF 角色 UI 保留，不开发真实写入 | M1-UI (`buddy_gif_stub`) + M4-Tools（接口冻结） |
| UI 设计：基于摇杆/Enter/Esc 与墨水屏的数据/状态/审批展示 | M1-UI (§7.1–§7.2) |
| UI 设计：LED 控制（`tdl_led_manage.h`） | M1-UI (`buddy_led.c/h`) |
| 插件：Claude CLI Hook，单独 README 讲解作用与安装 | M3-Plugin (§9) |
| 蓝牙连接：仅 Windows，实时显示连接状态，首次连接成功后进入 Claude 信息页 | M2-BLE (§8) + M1-UI 顶栏 |
| 蓝牙连接：CLI 启动后自动尝试 BLE 连接 | M3-Plugin（`/buddy-start`） + M2-BLE 自动重连 |
| 通信协议：生成文档介绍整个交互过程 | M0 (§6) + M4-Tools 附录 |
| 所有文档中文输出 | 横切要求 §12 |

**未覆盖项：** 无。每条 `dev.md` 需求均已落到至少一个里程碑。

**占位符扫描：** 本文档不含 `TBD` / `TODO` / `FIXME`。

**命名一致性：**
- `persona_registry`、`ascii_persona`、`buddy_gif_stub`、`buddy_led`、`buddy_approval_screen`：在 §2 架构、§7 文件清单、§11 验证中保持一致
- `BLE_WIRE_PROTOCOL.md`（英文，已存在）与 `BLE_WIRE_PROTOCOL_zh.md`（中文，M0 产出）在全文中区分
- `Claude_XXXX` 广播名规则在 M0、M2-BLE、M5 均引用同一推导（MAC 后四字节 Hex）

---

## 14. 执行交接

本计划为里程碑级路线图。每个里程碑的具体 TDD 单步由各自子计划承载：

| 里程碑 | 推荐执行方式 |
|--------|-------------|
| M0 | `superpowers:executing-plans` 内联执行（文档 + 一次固件日志抓取） |
| M1-UI | `superpowers:subagent-driven-development`（每个角色一个子 agent，并行度高） |
| M2-BLE | `superpowers:subagent-driven-development`（按 `ble_client / wire / hook_server / tests` 切分） |
| M3-Plugin | `superpowers:executing-plans` 内联（范围小，偏文档与脚本） |
| M4-Tools | `superpowers:executing-plans` 内联（纯文档） |
| M5 | **人工验证**（Windows 净机），非 subagent 任务 |

> **当前人工强制 Gate：** 在开始 M1-UI / M2-BLE 之前，必须有一名评审人签核 M0 的中文协议文档。

---

**计划结束。**
