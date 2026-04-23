# Claude Buddy — 交互规格（中文）

> 本文件为《UI_INTERACTION.md》的中文对照版，同时覆盖 M1-UI 里程碑新增的
> 左侧 ASCII 人格画布、LED 映射、18 角色切换与自定义 GIF 占位。当两份文档
> 发生不一致时，以本中文版为准。

| | |
|---|---|
| 目标固件 | `apps/tuya_t5_pocket/tuya_t5_pocket_ai` |
| 面板 | 384×168 单色可转彩 LCD（`AI_PET_SCREEN_*`） |
| 主实现 | `src/display/ui/buddy_ui/buddy_main_screen.c` |
| 渲染器 | `src/display/ui/buddy_ui/ascii_persona.{h,c}` |
| 角色注册表 | `src/display/ui/buddy_ui/persona_registry.{h,c}` + `persona_*.c`（18 文件） |
| LED 包装 | `src/display/ui/buddy_ui/buddy_led.{h,c}`（基于 `tdl_led_manage.h`） |
| GIF 占位 | `src/display/ui/buddy_ui/buddy_gif_stub.{h,c}` |
| 审批屏 | `src/display/ui/buddy_ui/buddy_approval_screen.{h,c}` |
| 关联协议 | [`docs/protocol/BLE_WIRE_PROTOCOL.md`](../docs/protocol/BLE_WIRE_PROTOCOL.md) |
| 里程碑 | M1-A（条目 + HH:MM 时钟） / **M1-UI（18 人格 + LED + 审批 + GIF 占位）** |

---

## 1. 屏幕分区总览

```
┌─────────────────────────────────────────────────────────────┐  HEADER  — 20 px
│ Claude Buddy       BLE: linked        HH:MM        <device> │
├──────────────────────┬──────────────────────────────────────┤  BODY    — 124 px
│                      │  msg / sessions / tokens / owner     │
│                      │  ─── Entries（最多 4 行） ───         │
│   PERSONA CANVAS     │   HH:MM  Ran Read(./src/foo.c)       │
│   184 × 120 px       │   HH:MM  Turn 4: 2.1k tok            │
│   (ASCII 或 GIF占位) │   HH:MM  Session start: t5-pocket-ai │
│                      │   HH:MM  ...                         │
├──────────────────────┴──────────────────────────────────────┤  FOOTER  — 24 px
│ Contextual hint line                                        │
└─────────────────────────────────────────────────────────────┘
```

> 当 `has_prompt == true` 时，**审批卡**覆盖整个 BODY（含人格画布），直到
> `ENTER/LEFT/RIGHT` 做出选择或 `ESC` 返回。

### Header

| 槽位 | 数据源 | 备注 |
|---|---|---|
| 标题（左）     | 硬编码 `"Claude Buddy"` | 静态标签 |
| BLE 状态（左中） | `ble_connected` | `"BLE: linked"` / `"BLE: -"` |
| HH:MM 时钟（中） | `wall_epoch_s` + `wall_tz_min` + `wall_local_ms_at_rx` | UI 侧整数换算；未同步时渲染 `"--:--"`；不写 TuyaOS RTC |
| 设备名（右）   | `device_name` | 广播名 `Claude_XXXX` |

---

## 2. Body —— 左 人格 + 右 文本

### 2.1 人格画布（左）

- 画布几何：`184 × 120` px，原点 `(8, HEADER_H)`。
- 渲染器：`ascii_persona.c` 使用 `lv_font_terminusTTF_Bold_14` 固定字宽/行高，
  保证与上游 `claude-desktop-buddy` 的坐标系 1:1 对齐。
- 单色兼容：持有的 RGB565 颜色参数在单色 OLED 上被忽略；sprite 与 overlay
  全部用前景色绘制。
- 驱动：`lv_timer` 以 ~30 Hz 调用 `ascii_persona_tick(t_ms)`，`t` 为自
  attach 以来的毫秒数，**在进入审批卡期间继续推进，但画布被遮挡**。

### 2.2 人格状态机（7 态）

| 状态 | 触发条件 | 视觉 | LED |
|---|---|---|---|
| `SLEEP`     | `ble_connected == false` 且 ≥ 30 s 无心跳 | 呼吸/睡姿 | 常暗 / 慢呼吸 |
| `IDLE`      | 已连但无会话活动 | 待机动作 | 微亮 |
| `BUSY`      | `tokens_rate > 0` 或 `recently_emitted_entry` | 敲键/工作动作 | 快闪 |
| `ATTENTION` | `has_prompt == true` | 转头注视 | 慢闪 |
| `CELEBRATE` | 会话完成（transient, 3 s） | 庆祝动作 | 单次强闪 |
| `DIZZY`     | 收到 `error` / 连续超时 | 旋转花眼 | 快闪 |
| `HEART`     | 审批通过后回执（transient, 2 s） | 爱心动画 | 单次强闪 |

- 转换由 `__derive_persona_state()` 每帧从 `buddy_tama_state_t` 计算得出。
- Transient 状态（CELEBRATE / HEART）使用 `s_*_until_ms` 到期回落到当前应
  有的稳态。

### 2.3 18 人格注册表

人格 ID 范围 `[0, BUDDY_PERSONA_COUNT)`，在 `persona_registry.c` 中以固定
顺序登记，便于持久化跨版本兼容：

```
 0  capybara     6  octopus     12  axolotl
 1  duck         7  owl         13  cactus
 2  goose        8  penguin     14  robot
 3  blob         9  turtle      15  rabbit
 4  cat         10  snail       16  mushroom
 5  dragon      11  ghost       17  chonk
```

> 具体顺序以 `persona_registry.c` 源码为准；新增/下线人格必须 **追加** 到
> 数组末尾，禁止插入中间索引，避免 `tal_kv` 中旧值指向错误角色。

### 2.4 角色持久化

- 存储：`tal_kv`，键 `"buddy.pid"`，值 `uint8_t`。
- 读取：`__load_persona_id()` 在 `__main_screen_init` 内执行一次，失败/越界
  时回落到 `0`（`capybara`）。
- 写入：每次切换后立即 `tal_kv_set`，失败只打 `PR_WARN` 不阻塞 UI。

### 2.5 文本区（右）

与 M1-A 相同，继承 `BUDDY_FONT_CONTENT` / `BUDDY_FONT_HINT`：

- 顶部：`msg / sessions / tokens / owner` 四行紧凑摘要。
- 下方：Entries 面板（最多 4 行，最顶端最新、加粗）。

Entries 规则（对齐英文版 §2.Entries panel）：

- 后端存储：`BUDDY_ENTRIES_RING = 8` 条，每条 `BUDDY_ENTRY_MAX_CHARS = 79`
  字节 UTF-8（含 NUL）。解析器在填充前清空环，避免跨端数据残留。
- 顺序：`entries[entries_head]` 为最新；窗口反向遍历，顶行最新。
- 格式：`HH:MM  <text>`。
- 滚动：`s_entries_scroll` 选择窗口起点；`UP/DOWN` 始终可用，与审批键位不
  冲突。

---

## 3. Footer 提示

| 上下文 | 提示内容 |
|---|---|
| 审批挂起（`has_prompt`） | `ENTER=OK LEFT=deny RIGHT=always UP/DOWN=scroll ESC=back` |
| 已连，无审批             | `LEFT/RIGHT=persona UP/DOWN=scroll JOYCON=refresh ESC=back` |
| 未连接                   | `Waiting for Claude desktop...   ESC=back` |

---

## 4. 键位映射

T5 pocket 具备 4 方向摇杆 + 中心按下（`KEY_UP/DOWN/LEFT/RIGHT/JOYCON`）以及
`KEY_ENTER` 与 `KEY_ESC` 两枚物理按键。

### 4.1 有审批卡时（`has_prompt == true`）

| 键 | 行为 | 发送帧 |
|---|---|---|
| `KEY_ENTER`  | 批准本次（once）                | `{"cmd":"permission","id":"<id>","decision":"once"}` |
| `KEY_LEFT`   | 拒绝                            | `{"cmd":"permission","id":"<id>","decision":"deny"}` |
| `KEY_RIGHT`  | 批准并记住（always）            | `{"cmd":"permission","id":"<id>","decision":"always"}` |
| `KEY_UP`     | Entries 向更旧滚动              | — |
| `KEY_DOWN`   | Entries 向更新滚动              | — |
| `KEY_JOYCON` | 请求主机重发 snapshot           | `{"cmd":"status"}` |
| `KEY_ESC`    | 返回上一屏                      | — |

### 4.2 无审批卡时

| 键 | 行为 | 发送帧 |
|---|---|---|
| `KEY_ENTER`  | 目前保留（未来留给 "输入模式"） | — |
| `KEY_LEFT`   | **切换到上一个 persona**（持久化） | — |
| `KEY_RIGHT`  | **切换到下一个 persona**（持久化） | — |
| `KEY_UP`     | Entries 向更旧滚动              | — |
| `KEY_DOWN`   | Entries 向更新滚动              | — |
| `KEY_JOYCON` | 请求主机重发 snapshot           | `{"cmd":"status"}` |
| `KEY_ESC`    | 返回上一屏                      | — |

夹取规则：

- `KEY_UP` 仅当 `s_entries_scroll + 4 < entries_count` 时推进；`KEY_DOWN`
  仅当 `s_entries_scroll > 0` 时回退。
- persona 切换以 `persona_registry_next_id` / `prev_id` 做环形遍历；切换时
  `ascii_persona_reset_tick()` 置零动画时基，避免跨角色残留姿态。
- 审批键（`ENTER/LEFT/RIGHT`）在无 prompt 时被**静默吞掉**，不影响人格切换
  逻辑（LEFT/RIGHT 在两上下文中语义互斥）。

---

## 5. LED 映射

`buddy_led.c` 包装 `tdl_led_manage.h`，按 `buddy_led_state_e` 驱动板载 LED：

| `buddy_led_state_e`    | `tdl_led_*` 调用 | 典型语义 |
|---|---|---|
| `BUDDY_LED_STATE_OFF`        | `tdl_led_set_status(LED_OFF)`          | 无连接 / SLEEP |
| `BUDDY_LED_STATE_ON_DIM`     | `tdl_led_set_status(LED_ON)`（弱）     | IDLE |
| `BUDDY_LED_STATE_BLINK_SLOW` | `tdl_led_blink(on=500, off=500)`       | ATTENTION |
| `BUDDY_LED_STATE_BLINK_FAST` | `tdl_led_blink(on=150, off=150)`       | BUSY / DIZZY |
| `BUDDY_LED_STATE_FLASH_ONCE` | `tdl_led_flash(count=1)`               | CELEBRATE / HEART |

注意：

- LED 设备名由板级配置的 `LED_NAME` 决定，未定义时模块自动降级为 no-op 并
  打一次 `PR_WARN`。
- 切换时做**去重**：同一 `led_state` 重复 `buddy_led_set` 不会重新下发硬件
  命令，避免抖动。

---

## 6. 自定义 GIF 占位（M4 预留）

- 入口：`buddy_gif_stub_attach(canvas_parent)`，与 ASCII 画布**互斥**。
- 外观：在 184×120 画布内渲染双行居中文本 ——
  1. 自定义名称（默认 `"(gif)"`，由 `buddy_gif_stub_set_name` 覆盖）；
  2. `"(gif stub - M4)"`，作为对 M4 的视觉提示。
- 当前切换到 GIF 模式的条件：暂未打通（M1 只做占位），入口由 M3 的
  `{"cmd":"buddy","mode":"gif","name":"..."}` 触发。

---

## 7. 日志锚点（Runtime verification）

所有运行期观测点均以 `PR_DEBUG` 级打点，避免 `PR_INFO/PR_NOTICE` 泄露路径、
命令名等半敏感字段：

- `buddy_ble entries: idx=<n> text=<..80 chars..>` —— 每条心跳 entry 解析一次。
- `buddy_ble time sync ok epoch=<lld> tz=<d>` —— 每次 `{"time":[...]}` 成功。
- `ui clock render HH=<hh> MM=<mm>` —— 表头与 entry 行重绘时。
- `ui scroll idx=<n> count=<c>` —— UP/DOWN 调整滚动时。
- `ui persona id=<n> name=<...> state=<s>` —— `__derive_persona_state` 切换时。
- `ui persona cycle dir=<+1|-1> new_id=<n>` —— LEFT/RIGHT 切换时。
- `ui led state <OFF|ON_DIM|BLINK_SLOW|BLINK_FAST|FLASH_ONCE>` —— `buddy_led_set` 去重后真正下发时。
- `ui gif stub attach name=<...>` —— GIF 占位 attach 时。

参见 `docs/protocol/baseline/` 中的快照样本进行交叉比对。

---

## 8. 与 M1-A 英文版的差异摘要（便于 review）

| 分类 | M1-A（英文版） | M1-UI（本文件） |
|---|---|---|
| Body 布局   | 单栏全文本 | 左人格画布 + 右文本 |
| 键位        | `LEFT/RIGHT` 固定为 deny/always | 无 prompt 时 `LEFT/RIGHT` 切换 persona |
| LED         | 未使用 | 按 persona state 驱动 |
| 持久化      | 无 | `tal_kv:"buddy.pid"` 保存 persona_id |
| 审批呈现    | 覆盖上半 body | 覆盖整个 body（含人格画布） |
| 占位        | 无 | GIF stub 画布（M4 预留） |

英文版保留 M1-A 原状并在文件顶部加了指向本中文版的提示。
