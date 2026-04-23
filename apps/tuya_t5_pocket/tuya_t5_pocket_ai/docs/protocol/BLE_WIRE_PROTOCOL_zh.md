# T5AI-Pocket × Claude — BLE 线路协议（中文版）

| | |
|---|---|
| **版本** | v1.1 |
| **日期** | 2026-04-22 |
| **状态** | 已冻结（变更须按 §8 的流程处理） |
| **维护者** | 固件子项目 A、CLI 插件子项目 C |
| **权威来源** | 本文件。源自 `apps/tuya_t5_pocket/claude-desktop-buddy/REFERENCE.md` 与 `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c` |
| **英文原版** | `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md`（若中英不一致，**以本中文版为准**） |

---

## 1. 速览

一台烧录了本固件的 T5AI-Pocket 会以 Nordic UART Service（NUS）外设身份广播，设备名格式为 `Claude_XXXX`。中央设备（当前为 macOS/Windows 上的 Claude Desktop，M1-C 完成后增加 Claude Code CLI 插件）完成连接、协商 MTU，然后通过 NUS 的 RX/TX 特征交换**以 `\n` 分隔的 UTF-8 JSON 行**。设备在 384×168 单色屏上展示会话计数 / Token 计数 / 单行状态 / 当前待审批权限请求；用户通过三颗物理按键做出决定，固件以 `{"cmd":"permission", ...}` 的形式回传给对端。

```
┌──────────────────────┐                      ┌─────────────────────────────┐
│  Claude Desktop      │  —— 广播 ——→          │  T5AI-Pocket (Claude_XXXX)  │
│  Claude Code 插件    │                      │                             │
│  （BLE 中央）         │  ← GATT 连接 ─────→    │  BLE 外设（NUS）              │
│                      │                      │                             │
│   心跳帧     ──→     │   RX 特征（写）       │                             │
│   回合事件   ──→     │                      │   → UI + 按键状态            │
│   time[]     ──→     │                      │                             │
│   cmd: ...   ──→     │                      │                             │
│                      │                      │                             │
│   ack: ...   ←──     │   TX 特征（Notify）    │                             │
│   cmd: perm  ←──     │                      │                             │
│   cmd: status←──     │                      │                             │
└──────────────────────┘                      └─────────────────────────────┘
```

所有会话片段与工具提示都会在链路上明文流转，因此在未加密的 GATT 配对下运行本协议是**已知**的安全权衡（详见 §6）。v1.0 固件**不强制** LE Secure Connections；启用该特性已在 Gap Table 中登记。

## 2. 传输层

### 2.1 GATT 服务

Nordic UART Service，128 位 UUID（原样复用）：

| 角色 | UUID |
|---|---|
| Service（主服务） | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX 特征（中央 → 设备，write / write-without-response） | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX 特征（设备 → 中央，notify） | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

固件以小端（little-endian）字节序存储 service UUID；参见 `buddy_ble.c` 中的 `BUDDY_NUS_UUID128_SVC` 常量。

### 2.2 广播名与广播数据

广播名格式：`Claude_XXXX`。

推导顺序（参见 `buddy_ble.c` 的 `__derive_name()`，约第 190–223 行）：

1. 调用 `tal_ble_address_get(&addr)`。若调用成功**且** `addr.addr[1]` 或 `addr.addr[0]` 非零，按 `Claude_%02X%02X` 使用 `(addr.addr[1], addr.addr[0])` 格式化，即每字节 2 个十六进制字符，合计 4 个十六进制字符，后缀取自 BLE 地址低两字节（以主机可见顺序）。
2. 否则，取 Tuya IoT 客户端：若 `activate.devid` 非空则使用它，否则使用 `config.uuid`。按 `Claude_%s` 使用该字符串的**后四个字符**格式化。
3. 兜底：裸 `Claude`。

名称缓冲区 `BUDDY_BLE_NAME_MAX = 20` 字节（含 NUL）。**对端不得**假设后缀一定是十六进制——兜底路径 2 可能保留设备 UUID 字母表中的任意可打印字符。

广播数据（ADV，总 31 字节预算）：
```
02 01 06                               ; Flags: LE General Discoverable + BR/EDR Not Supported
11 07 <16 字节 NUS service UUID 小端>  ; 完整 128 位 service UUID 列表
```

扫描响应（31 字节预算）：
```
<1+N> 09 <N 字节 s_device_name>        ; Complete Local Name
```

广播间隔：`adv_interval_min = 0x30`，`adv_interval_max = 0x60`，单位 0.625 ms（即 30–60 ms），可连接无定向广播（`TAL_BLE_ADV_TYPE_CS_UNDIR`）。

设备在 Tuya 云起来**之后**再接管广播：`buddy_ble_start()` 会调用 `tuya_ble_pair_monitor_disable(TRUE)` 抑制 Tuya 30 秒配对超时以及周期性监视器，否则后者会重新发布 Tuya 的配网名。在云激活之前探测到设备的对端会先看到 Tuya 名称；这是预期行为，不是协议缺陷。

### 2.3 MTU

- 设备默认 `BUDDY_BLE_DEFAULT_MTU = 23`。
- 收到 `TAL_BLE_EVT_MTU_REQUEST` 时，设备原样采纳对端提议的 MTU。
- Notify 负载 = `MTU - 3`，**硬上限** `BUDDY_BLE_MAX_NOTIFY_CHUNK = 180` 字节。超过一个分片的负载由设备分片发送，中央端通过拼接完成重组。

### 2.4 帧格式

每条逻辑消息恰好是一行以 `\n` 结尾的 UTF-8 JSON 对象。

**设备 → 对端（notify 方向）：**
- 设备在每个 JSON 对象后追加一个 `\n`，然后交由 `__send_raw` 按 MTU 分片送出。中央端负责拼接 notification 并在 `\n` 上切分。

**对端 → 设备（write 方向）：**
- 设备在 5120 字节环缓冲（`BUDDY_BLE_RX_BUF_CAP`）中累积入站字节。
- 每次收到数据，扫描下一个 `\n`，把前面的字节拷贝到堆分配的一行中，然后交给 `__handle_line`。
- 一旦缓冲即将溢出 5120 字节上限，**整个**缓冲会被丢弃（`__rx_accumulate` 打印 warn 日志并重置 `s_rx_len`）。对端**不得**依赖溢出之后的半帧送达——它们会丢失在途帧。
- JSON 解析失败（`cJSON_Parse` 返回 NULL）会被静默丢弃，仅打印 warn 日志（`buddy_ble bad json`），**不**回复错误帧。

## 3. 报文字典

"fw status" 列的图例：
- ✅ 当前固件已接受并应用
- ⚠️ 已接受但未应用（仅记录日志，不改状态）
- ❌ 未处理（静默丢弃，除非另有说明）

### 3.1 心跳快照（对端 → 设备）

触发时机：桌面/CLI 端状态有变化时发送；另每 ~10 秒保活。

```json
{
  "total": 3,
  "running": 1,
  "waiting": 1,
  "msg": "approve: Bash",
  "entries": ["10:42 git push", "10:41 yarn test", "10:39 reading file..."],
  "tokens": 184502,
  "tokens_today": 31200,
  "prompt": {
    "id": "req_abc123",
    "tool": "Bash",
    "hint": "rm -rf /tmp/foo"
  }
}
```

| 字段 | JSON 类型 | 取值范围 | fw status | 说明 |
|---|---|---|---|---|
| `total` | 整数 | 0..255 | ✅（转 `uint8_t`） | 桌面端全部会话计数。 |
| `running` | 整数 | 0..255 | ✅ | 正在生成的会话数。 |
| `waiting` | 整数 | 0..255 | ✅ | 被权限审批阻塞的会话数。 |
| `msg` | 字符串 | 去 NUL 后 ≤ 63 字节 | ✅ | 单行摘要；设备端通过 `__copy_str` 截断。 |
| `entries[]` | `array<string>` | 见 §3.1.1 | ✅ | 解析进 `BUDDY_ENTRIES_RING`（8 槽）× `BUDDY_ENTRY_MAX_CHARS`（79 字节）的环缓冲，设备端渲染。v1.1（M1-A）新增。 |
| `tokens` | 数字 | 可进 `uint32_t` | ✅（转型） | 桌面 App 启动以来的累计输出 Token。 |
| `tokens_today` | 数字 | 可进 `uint32_t` | ✅（转型） | 本地零点以来的 Token（对端负责持久化）。 |
| `prompt` | 对象 | 见 §5 | ✅ | 出现时驱动"批准 / 拒绝" UI。 |
| `prompt.id` | 字符串 | ≤ 39 字节 | ✅ | 不透明关联 ID；设备在 §3.6 中逐字节原样回显。 |
| `prompt.tool` | 字符串 | ≤ 31 字节 | ✅ | 仅用于显示（"Bash"、"Read" 等）。 |
| `prompt.hint` | 字符串 | ≤ 63 字节 | ✅ | 仅用于显示的参数摘要。 |

整帧大小仅受 5120 字节 RX 行上限约束。

#### 3.1.1 `entries[]` 环语义（v1.1，M1-A）

每次带有 `entries` 数组的心跳都会**重建**设备端的实时脚本环：

- 非字符串项会被静默跳过（对端**仍应**只发字符串）。
- 若数组长度超过 `BUDDY_ENTRIES_RING`，只保留**最后** `BUDDY_ENTRIES_RING` 项。
- 每个采纳的条目通过 `snprintf` 以 `BUDDY_ENTRY_MAX_CHARS`（79 字节 UTF-8 + NUL）为上限做长度受限拷贝；过长会被静默截断。因此对端**应当**保持条目简短。
- **每次**包含 `entries` 字段的心跳（即使数组为空）都会在重填之前清空整个环。因此跨对端、跨会话都不会泄露陈旧脚本文本。
- v1.1 固件不跟踪每条的时间戳；在渲染时会在每条可见行前附加设备当前的 `HH:MM`。对端**可以**自行在字符串里嵌入时间戳前缀。

每条存储的 entry 写入如下 DEBUG 日志锚点：
`buddy_ble entries: idx=<n> text=<..前 80 字符..>`。

### 3.2 回合事件（对端 → 设备）

```json
{
  "evt": "turn",
  "role": "assistant",
  "content": [{ "type": "text", "text": "..." }]
}
```

序列化后单行 ≤ 4 KB（由对端自行限制；桌面端超限会在发送前丢弃）。

fw status：**❌**（顶层 `evt` 键会被忽略——该帧在 `cmd` / `time` / 心跳判断中都不会匹配，最终落到 `__handle_heartbeat`，由于没有 `total`/`running`/… 字段，状态保持不变）。在 §7 中登记，目标里程碑 post-M1-A。

### 3.3 连接后一次性帧（对端 → 设备）

```json
{"time":[1775731234, -420]}
```
（示例：epoch 秒、PST 为 `-420` 分钟）

| 字段 | 类型 | 含义 | fw status |
|---|---|---|---|
| `time[0]` | 整数 | Unix epoch 秒（UTC） | ✅（v1.1，M1-A）在接收时连同本地 `tal_system_get_millisecond()` 时间戳一起解析存储；用作 UI 侧墙钟偏移以渲染 `HH:MM`。**不写 RTC。** |
| `time[1]` | 整数 | 时区偏移，单位为 **UTC 东向分钟**。v1.1 固件以 `int16_t` 存储，并在推导本地时间时乘以 60。 | ✅（v1.1，M1-A）解析后仅作用于 UI。 |

> **单位澄清（v1.1）：** v1.0 把 `time[1]` 描述为"UTC 东向秒"同时说明仅作日志输出。v1.1 将单位修正为**分钟**，以匹配 `int16_t` 存储与如下推导公式：
> `local = epoch + (now_ms - rx_ms)/1000 + tz_min × 60`。
> 对端**必须**发送分钟（例如 PST 为 `-420`）。DEBUG 锚点 `buddy_ble time sync ok epoch=<lld> tz=<d>` 会回显已存储的值，便于联调时排查单位错用。

```json
{"cmd":"owner","name":"Felix"}
```

处理方式见 §3.4。

### 3.4 命令帧（对端 → 设备）

每帧恰好包含一个 `cmd` 字段。`cmd` 存在时，设备忽略所有其他顶层键。

| cmd | 附加字段 | fw status | 设备 ACK（§3.7） | 说明 |
|---|---|---|---|---|
| `status` | — | ✅ | `ack:"status"` 附带 `data{}` | 详见 §3.7.2（即 §3.5）。 |
| `name` | `name: string`（最多使用 19 字节） | ✅ | `ack:"name", ok:true, n:0` | 下次重启广播时覆盖本地名。 |
| `owner` | `name: string`（最多使用 23 字节） | ✅ | `ack:"owner", ok:true, n:0` | 存到 `s_owner_name`，UI 中展示。 |
| `unpair` | — | ✅（仅 ACK） | `ack:"unpair", ok:true, n:0` | **v1.0 并不会真正擦除 BLE 绑定** —— ACK 是"装饰性"的。已在 §7 登记。 |
| `char_begin` / `file` / `chunk` / `file_end` / `char_end` | 见 REFERENCE.md | ❌（不 ACK 即视为拒收） | — | 由 B 子项目（角色包）负责。对端应把静默视为"设备不接受"。 |
| 其他 | — | ❌ | — | 在 DEBUG 中以 `unhandled cmd '<x>'` 记录。 |

### 3.5 设备 → 对端：状态响应

在收到 `{"cmd":"status"}` 时发送：

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

| 字段 | v1.0 中的 fw 值 |
|---|---|
| `data.name` | 当前 `s_device_name` |
| `data.sec` | 恒为 `false`（未实现 LESC） |
| `data.sys.up` | `tal_system_get_millisecond() / 1000` |
| `data.bat` | v1.0 **省略** |
| `data.stats` | v1.0 **省略** |

### 3.6 设备 → 对端：权限决策

在 `has_prompt` 为真时，由用户按键触发固件发送：

```json
{"cmd":"permission","id":"req_abc123","decision":"once"}
{"cmd":"permission","id":"req_abc123","decision":"deny"}
{"cmd":"permission","id":"req_abc123","decision":"always"}
```

| 字段 | 约束 |
|---|---|
| `id` | **必须**与设备最近观测到的 `prompt.id` 逐字节相等。固件**绝不**合成 `id` 值。 |
| `decision` | 取值 `"once" | "deny" | "always"` 之一。对端**可以**后续扩展新值；对未知值**应当**按 `"deny"` 处理。 |

对端侧，任何 `cmd:"permission"` 帧若其 `id` 与当前待处理请求不匹配，**必须**丢弃。

### 3.7 通用 ACK 信封

除 `permission`（设备 → 对端）之外，所有由对端发起的 `cmd` 都会收到一条 ACK：

```json
{"ack":"<与 cmd 同值>","ok":true,"n":0}
```

失败变体（保留；v1.0 固件不会发送）：

```json
{"ack":"...","ok":false,"error":"short reason"}
```

`n` 是通用计数器——未来用于分片 ACK 时表示字节数，否则为 `0`。

## 4. 顺序与存活检测

- 收到 `TAL_BLE_EVT_PERIPHERAL_CONNECT` 时，设备通过 `__reset_state(TRUE)` 重置 UI 状态并等待入站帧。设备**不会**主动发送 "hello"。
- 推荐的对端上电序列（桌面端今天就按此执行；M1-C CLI 插件**应当**一致）：
  1. 连接。
  2. `{"time":[...]}`。
  3. `{"cmd":"owner","name":"..."}` — 触发设备发出首个 `ack:"owner"`。
  4. 首个心跳快照。
- 保活：对端每 ~10 秒发送一次心跳；设备不主动发保活。
- 存活检测：若对端连续 ~30 秒未收到心跳，**应当**将链路视为失效。固件当前**不会**对对端静默做任何动作——v1.0 刻意如此；M1-A 之后可能新增"断连"视觉状态。
- 收到 `TAL_BLE_EVT_DISCONNECT` 时，固件重置 UI 状态，且若 `s_started` 为真，则重启广播。

## 5. 权限关联

- `prompt.id` 是心跳 `prompt` 对象中携带的不透明字符串。
- 固件把它存入 `s_state.prompt_id`，这是一个 40 字节字段（39 字节可用 + NUL；详见 `buddy_data.h` 的 `buddy_tama_state_t.prompt_id`）。
- 用户按下决策按键时，`buddy_ble_send_permission()` 以 `{"cmd":"permission","id":"<s_state.prompt_id>","decision":"..."}` 的形式发送，**不做任何变换** —— 逐字节回显。
- 若对端收到 `permission` 帧但 `id` 与当前待处理 prompt 不匹配，**必须**丢弃（用以防御"快速取消 + 再次发起 prompt"后设备侧陈旧状态带来的误匹配）。
- 当前固件仅保证**单条在途** prompt：心跳里新的 `prompt` 字段会覆盖之前的 `prompt_id`。没有 per-prompt 队列。

## 6. 安全与加固

v1.0 基线（所有差距都是有意的，登记为非目标或差距）：

| 维度 | 当前行为 | 理由 / 负责人 |
|---|---|---|
| 链路加密 | 未强制 LE Secure Connections。特征未标记 encrypted-only。§3.5 的 `data.sec` 恒为 `false`。 | 根据 umbrella spec §1.3 为非目标。 |
| 行长度 | 入站行上限 `BUDDY_BLE_RX_BUF_CAP = 5120` 字节。溢出会丢弃**整个**缓冲，而不是仅丢弃当前帧。 | RX 缓冲共享；从中间截断会损坏**下一**帧。 |
| JSON 解析失败 | 静默丢弃 + `buddy_ble bad json` warn 日志。不回错误帧。 | 避免给攻击者可控的放大向量。 |
| `cmd` 白名单 | 仅 `status`、`name`、`owner`、`unpair` 会被处理。未知 `cmd` 在 DEBUG 记录并通过"不 ACK"的方式拒收。 | 依 REFERENCE.md —— 静默即拒收。 |
| 出站 JSON 注入 | `buddy_ble_send_cmd()` 拒绝 `cmd` 中包含 `"`、`\\` 或低于 `0x20` 的字节，并把长度截到 32 字节。`buddy_ble_send_permission()` 依赖 §3.1 对 `prompt_id` 的长度约束。 | 中央端仍**应**在服务端侧先校验 `prompt_id` 再转发。 |
| 名称欺骗 | 任何人都可以广播 `Claude_XXXX` —— 对端仍**必须**在配对前给用户一次确认。 | 非目标；仅供 CLI 插件 UX 参考。 |
| `unpair` 语义 | 只 ACK 不擦绑定。 | 已在 §7 登记；M1-A 任务。 |
| 角色包（`char_*`）校验 | 设备端未实现，因此当前无路径穿越风险。 | B 子项目将在放行 `char_begin` 前加入文件名白名单。 |

## 7. Gap Table（REFERENCE.md × 当前固件）

| 字段 / 帧 | 方向 | 固件状态 | 目标里程碑 | 备注 |
|---|---|---|---|---|
| `entries[]`（心跳） | 对端 → 设备 | ✅ 已在 M1-A 关闭（固件 HEAD 在 616464c5 之后） | — | 8 × 79 字节环；详见 §3.1.1。 |
| `evt: "turn"` | 对端 → 设备 | ❌ 静默丢弃 | Post-M1-A | 可为"最后一条助理回复"预览提供能力。 |
| `time` 数组 | 对端 → 设备 | ✅ 已在 M1-A 关闭（固件 HEAD 在 616464c5 之后） | — | 解析后作为 UI 侧偏移使用；详见 §3.3。明确不写 RTC。 |
| `cmd: "unpair"` | 对端 → 设备 | ⚠️ 已 ACK 但未擦除绑定 | Post-M1-A | 需要追加一次 `tal_ble_bond_erase()` 调用（API 名待定）。 |
| `char_begin` / `file` / `chunk` / `file_end` / `char_end` | 对端 → 设备 | ❌ 拒收（不 ACK） | **M4-Tools**（接口冻结）/ **M3-GIF 骨架**（占位）/ B 子项目（真实实现） | 启用前需要路径穿越白名单与 CRC 校验。 |
| `status.data.bat` | 设备 → 对端 | ❌ 省略 | 后续子项目 | 需要接电量驱动。 |
| `status.data.stats` | 设备 → 对端 | ❌ 省略 | 后续子项目 | 可选；桌面 UI 可容忍缺失。 |
| `sec = true`（LESC） | 设备 → 对端 | 恒 `false` | v1.0 非目标 | 仅登记以求完整。 |
| 存活超时 UI 反应 | 设备内部 | ❌ 30 秒静默后 UI 无变化 | Post-M1-A | 当前 UI 会永远停在最后一份快照。 |
| 心跳快照核心字段 | 对端 → 设备 | ✅ | — | `total`、`running`、`waiting`、`msg`、`tokens`、`tokens_today`、`prompt{id,tool,hint}`。 |
| `cmd: "status"` + ACK | 双向 | ✅ | — | — |
| `cmd: "name"` + ACK | 对端 → 设备 | ✅ | — | — |
| `cmd: "owner"` + ACK | 对端 → 设备 | ✅ | — | — |
| `cmd: "permission"` | 设备 → 对端 | ✅ | — | 回显合同见 §5。 |

## 8. 修订流程

对本文档的任何变更都是相应固件 / 插件变更的**前置条件** —— 次序不可颠倒。

**实质性变更**（新增帧、新增字段、修改语义）：

1. 在 `docs/superpowers/specs/<日期>-wire-protocol-amend-<主题>-design.md` 开一次子项目 brainstorm。
2. 运行 `superpowers:writing-plans` 在 `docs/superpowers/plans/` 下产出对应 plan。
3. 更新本文档版本到 v1.x，`Version` 字段随之提升，并在文末 Changelog 追加带日期条目。
4. **之后**才能改固件 / 插件代码；协议文档变更必须**与该变更同 commit 或在合并序列中先行 commit**。

**非实质性变更**（Gap Table 状态 ❌/⚠️ ↔ ✅ 的翻转等）：

- 可不走完整修订循环。
- 仍需把版本升到 v1.x.y，并追加一行 Changelog。

## Changelog

- **v1.1 · 2026-04-22** — M1-A 改动。心跳 `entries[]` 数组现被解析进设备侧环并在 UI 滚动面板中渲染（§3.1.1）；`{"time":[...]}` 作为 UI 侧墙钟偏移被应用（TuyaOS RTC 仍不写入）；`time[1]` 单位澄清为 UTC 东向分钟（§3.3）。§7 Gap Table 中 A（`entries[]`）与 E（`time`）两行翻为 ✅。与 v1.0 相比**无**线路变更——v1.1 仅为消费者/语义澄清，所有兼容 v1.0 的对端都能继续与 v1.1 固件协作。
- **v1.0 · 2026-04-21** — 首次冻结。捕获提交 `616464c5`（工作区）上实现的协议。Gap Table 反映尚未接入 `buddy_ble.c` 的字段与帧。
