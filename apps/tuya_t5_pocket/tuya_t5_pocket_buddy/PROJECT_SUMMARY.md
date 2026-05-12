# Tuya T5 Pocket Buddy — Project Documentation Summary

> Generated from all documentation files in `tuya_t5_pocket_ai/docs/`, `tuya_t5_pocket_ai/doc/`,
> and `claude-cli-plugin/` as of 2026-05-07.
> All source documents were originally in Chinese (authoritative) with English mirrors.

---

## 1. Project Overview

**Goal:** Run firmware on a **T5AI-Pocket** (384×168 LCD, LVGL v9) that connects via BLE to **Claude Code CLI**
running on a Windows host. The device displays real-time Claude session state, token usage, and permission
approval requests. User interacts via a 4-direction joystick + 2 physical buttons (`Enter`, `Esc`), with
an LED for auxiliary status feedback.

**Scope:** Windows-first (Windows 10/11). macOS / Linux are deferred — code stubs exist but return
"not yet supported."

---

## 2. Architecture (5 Layers)

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. Claude Code CLI                                                │
│    - Emits hooks: SessionStart, UserPromptSubmit, PreToolUse,     │
│      PostToolUse, Stop                                            │
└──────────────────┬───────────────────────────────────────────────┘
                   │ stdin JSON payload
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 2. Claude Hooks (hooks.json)                                      │
│    - Fire-and-forget: SessionStart, UserPromptSubmit,             │
│      PostToolUse, Stop → curl POST to 127.0.0.1:9878/hook        │
│    - Blocking approval: PreToolUse → hook_handler.py              │
│      (exit 0 = approve, exit 2 = deny)                            │
└──────────────────┬───────────────────────────────────────────────┘
                   │ HTTP POST 127.0.0.1:9878/hook
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 3. Daemon (Python 3.10+, bleak, asyncio)                          │
│    - hook_server.py:   Local HTTP (loopback-only)                 │
│    - hook_router.py:   Event dispatch, state aggregation          │
│    - permissions.py:   Permission bridge with 35s timeout         │
│    - wire.py:          JSONL frame encode/decode                  │
│    - ble_client.py:    BLE central (scan Claude_XXXX, connect)    │
│    - state.py:         Session state tracking                     │
│    - config.py:        PID file, persistent config                │
└──────────────────┬───────────────────────────────────────────────┘
                   │ NUS / BLE JSON lines
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 4. Device BLE Bridge (buddy_ble.c)                                │
│    - Advertises as Claude_XXXX (Nordic UART Service)              │
│    - Reassembles RX writes into newline-delimited JSON            │
│    - Updates shared buddy_tama_state_t                            │
│    - Fragments and sends TX notifications                         │
└──────────────────┬───────────────────────────────────────────────┘
                   │ state snapshot
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 5. Device UI (buddy_main_screen + sub-screens)                    │
│    - Main: persona canvas (left) + session/token text (right)     │
│    - Approval: fullscreen permission card                         │
│    - Session, Status, Chart, Pie screens                          │
└──────────────────────────────────────────────────────────────────┘
```

**Design principle:** The device UI is a pure consumer of `buddy_tama_state_t`. It does not
derive Claude business logic, read the network, or access `.claude/` directories. The daemon
aggregates state from hooks + `~/.claude/stats-cache.json` + `~/.claude/projects/*.jsonl`
and pushes heartbeat frames every ~10 seconds.

---

## 3. BLE Protocol (Nordic UART Service, v1.1 frozen 2026-04-22)

### Service UUIDs

| Role | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (central→device, write) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (device→central, notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

### Advertising

- Name: `Claude_XXXX` (derived from BLE MAC address last 4 hex chars, or device UUID fallback)
- ADV flags + NUS service UUID in scan response
- Interval: 30–60 ms

### Frame Format

- UTF-8 JSON, newline-delimited (`\n` terminator per line)
- Max single notify chunk: 180 bytes
- Device RX buffer: 5120 bytes (overflow discards entire buffer)
- Daemon TX limit: 4096 bytes/line

### Key Frames

| Frame | Direction | Key Fields |
|---|---|---|
| Heartbeat | host→device | `total`, `running`, `waiting`, `msg`, `tokens`, `tokens_today`, `tokens_in`, `tokens_in_today`, `cache_read`, `cache_write`, `ctx_used`, `ctx_total`, `entries[]`, `prompt{id,tool,hint}`, `model`, `sessions[]`, `mstats[]`, `ver`, `cost_td`, `cost_all`, `daily[]` |
| Time Sync | host→device | `{"time": [epoch_seconds, tz_minutes]}` (UI clock only, no RTC write) |
| Permission | device→host | `{"cmd":"permission","id":"<prompt_id>","decision":"once\|deny\|always"}` |
| Status Request | host→device | `{"cmd":"status"}` → device replies with ack + data |
| Owner/Name | host→device | `{"cmd":"owner","name":"..."}` / `{"cmd":"name","name":"..."}` |
| Ack | device→host | `{"ack":"<cmd>","ok":true,"n":0}` |

### Permission Flow

1. Claude Code `PreToolUse` hook triggers
2. `hook_handler.py` POSTs to daemon
3. Daemon creates pending prompt with unique `prompt_id`, sends heartbeat with `prompt` field
4. Device detects `has_prompt=true`, shows approval screen, LED fast-blinks
5. User presses: `ENTER`=once, `LEFT`=deny, `RIGHT`=always
6. Device sends `{"cmd":"permission","id":"<id>","decision":"..."}`
7. Daemon matches `prompt_id`, returns decision to Claude Code
8. Timeout: 35 seconds (5s headroom in 40s hook budget), defaults to deny

### Security Notes (v1.0)

- No encryption (all JSON plaintext over GATT)
- No LE Secure Connections (`data.sec` always `false`)
- `buddy_ble_send_cmd()` filters `"`, `\`, and control characters to prevent JSON injection
- JSON parse failures silently dropped (no error frames)
- Single in-flight prompt only (new prompt overwrites old)

---

## 4. UI Interaction Specification (M1-UI)

### Screen Layout (384×168)

```
┌─────────────────────────────────────────────────────────────┐  HEADER  20 px
│ Claude Buddy       BLE: linked        HH:MM        Claude_XX│
├──────────────────────┬──────────────────────────────────────┤  BODY   124 px
│                      │  msg / sessions / tokens / owner     │
│   PERSONA CANVAS     │  Entries (up to 4 rows, UP/DOWN scrl)│
│   184 × 120 px       │  HH:MM  Ran Read(./src/foo.c)       │
│   (ASCII or GIF stub)│  HH:MM  Turn 4: 2.1k tok            │
├──────────────────────┴──────────────────────────────────────┤  FOOTER  24 px
│ Contextual key hints                                        │
└─────────────────────────────────────────────────────────────┘
```

Permission card overlays entire BODY when `has_prompt == true`.

### Key Mappings

**With pending approval:**

| Key | Action | Frame Sent |
|---|---|---|
| `ENTER` | Approve once | `{"cmd":"permission","id":"<id>","decision":"once"}` |
| `LEFT` | Deny | `{"cmd":"permission","id":"<id>","decision":"deny"}` |
| `RIGHT` | Approve always | `{"cmd":"permission","id":"<id>","decision":"always"}` |
| `UP`/`DOWN` | Scroll entries | — |
| `JOYCON` | Request refresh | `{"cmd":"status"}` |
| `ESC` | Back | — |

**Without pending approval:**

| Key | Action |
|---|---|
| `LEFT`/`RIGHT` | Cycle persona (persisted to `tal_kv:"buddy.pid"`) |
| `UP`/`DOWN` | Scroll entries |
| `JOYCON` | Request refresh |
| `ESC` | Back |

### Persona State Machine (7 States)

| State | Trigger | LED |
|---|---|---|
| `SLEEP` | No BLE connection ≥30s | OFF |
| `IDLE` | Connected, no activity | ON_DIM |
| `BUSY` | `tokens_rate > 0` or recent entry | BLINK_FAST |
| `ATTENTION` | `has_prompt == true` | BLINK_SLOW |
| `CELEBRATE` | Session completed (transient 3s) | FLASH_ONCE |
| `DIZZY` | Error / consecutive timeouts | BLINK_FAST |
| `HEART` | Approval accepted (transient 2s) | FLASH_ONCE |

### 18 Persona Species

| ID | Name | ID | Name | ID | Name |
|---|---|---|---|---|---|
| 0 | capybara | 6 | octopus | 12 | axolotl |
| 1 | duck | 7 | owl | 13 | cactus |
| 2 | goose | 8 | penguin | 14 | robot |
| 3 | blob | 9 | turtle | 15 | rabbit |
| 4 | cat | 10 | snail | 16 | mushroom |
| 5 | dragon | 11 | ghost | 17 | chonk |

Persona selection is persisted via `tal_kv` key `"buddy.pid"` (uint8). New species must be
appended to the end of the registry array — never inserted in the middle — to preserve
cross-version compatibility.

---

## 5. Daemon Data Sources

The daemon aggregates Claude state from three sources:

1. **Claude hooks** — provide real-time events (session start, prompt submit, tool use)
2. **`~/.claude/stats-cache.json`** — accumulated token counts, costs, daily history, model info
3. **`~/.claude/projects/*/*.jsonl`** — per-session details: names, recent usage, context windows

This means many UI fields (cost, daily history, session names) are not directly from hooks
but are locally derived by the daemon.

---

## 6. Project Milestones (from PLAN_zh.md)

| Milestone | Description | Status |
|---|---|---|
| **M0** | Protocol docs in Chinese + baseline logs | Complete |
| **M1-UI** | 18 ASCII personas + approval screen + LED + GIF stub | Complete |
| **M2-BLE** | Windows BLE central (bleak) integration | Complete |
| **M3-Plugin** | CLI plugin Chinese localization + one-click install | Complete |
| **M4-Tools** | GIF pack format + flasher CLI contracts (docs only) | Interface frozen |
| **M5** | Windows end-to-end integration verification | Pending |

### Non-Goals (Explicitly Excluded)

- No Tuya cloud联动 (stats/level/mood system removed)
- No custom GIF flash write or render (UI placeholder only)
- No Secure Connections pairing, no OTA
- No macOS / Linux executable scripts (stubs only)

---

## 7. Claude CLI Plugin Structure

```
claude-cli-plugin/
├── .claude-plugin/
│   ├── plugin.json           # Plugin manifest
│   └── marketplace.json
├── commands/                  # Slash command definitions
│   ├── buddy-install.md
│   ├── buddy-pair.md
│   ├── buddy-start.md
│   ├── buddy-stop.md
│   ├── buddy-status.md
│   └── buddy-unpair.md
├── daemon/
│   ├── pyproject.toml
│   ├── tuya_pocket_buddy/    # Python package
│   │   ├── __main__.py       # Entry point
│   │   ├── ble_client.py     # BLE central (bleak)
│   │   ├── hook_server.py    # HTTP server
│   │   ├── hook_router.py    # Event dispatcher
│   │   ├── permissions.py    # Permission bridge
│   │   ├── wire.py           # Frame encode/decode
│   │   ├── state.py          # Session state
│   │   └── config.py         # Persistent config
│   └── tests/
├── scripts/
│   ├── install.ps1 / start.ps1 / stop.ps1 / status.ps1
│   ├── hook_handler.py       # PreToolUse blocking handler
│   └── install-hooks.py
└── settings/
    └── hooks.json            # Claude Code hook definitions
```

### Daemon Lifecycle

- `buddy-install`: Creates venv at `%LOCALAPPDATA%\TuyaPocketBuddy\.venv`, pip installs daemon,
  merges `hooks.json` into Claude Code settings (non-destructive)
- `buddy-start`: Launches daemon background process, PID file at `%LOCALAPPDATA%\TuyaPocketBuddy\daemon.pid`
- `buddy-stop`: Kills daemon by PID
- `buddy-status`: Shows PID, BLE connection state, last heartbeat time, last error
- Background loops: `_rx_pump()` (process device→host frames), `_heartbeat_loop()` (10s periodic push)

---

## 8. Device-Side File Map

```
src/
├── buddy_main.c              # Main entry (IoT init + buddy_ble_init)
├── buddy_indev.c             # Button/joystick → LVGL key codes
├── buddy_ai_chat.c           # AI chat / ASR → BLE bridge
├── media/media_pet.c         # Audio alert data
└── display/
    ├── CMakeLists.txt
    ├── fonts/                 # TerminusTTF 14/16/18 + CJK puhui
    ├── icons/                 # WiFi/Battery/Cellular/Menu icons
    ├── logo/                  # Boot logo
    └── ui/
        ├── screen_manager.c/h    # Screen stack (max depth 6)
        ├── startup_screen.c/h    # Splash → buddy_main_screen
        ├── main_screen.c/h       # Alias → buddy_main_screen
        └── buddy_ui/             # 46 files, ~11,300 lines
            ├── buddy_ble.{c,h}           # BLE NUS bridge
            ├── buddy_data.h             # Shared state structures
            ├── buddy_main_screen.{c,h}   # Main UI (persona + text)
            ├── buddy_approval_screen.{c,h}
            ├── buddy_session_screen.{c,h}
            ├── buddy_chart_screen.{c,h}
            ├── buddy_pie_screen.{c,h}
            ├── buddy_status_screen.{c,h}
            ├── buddy_status_bar.{c,h}
            ├── buddy_cjk_font.{c,h}
            ├── buddy_gif_stub.{c,h}
            ├── buddy_led.{c,h}
            ├── ascii_persona.{c,h}
            ├── persona_registry.{c,h}
            └── persona_*.c (18 files)
```

---

## 9. Troubleshooting Quick Reference

| Symptom | Check |
|---|---|
| Device shows nothing | daemon running? `127.0.0.1:9878/hook` listening? hooks merged? |
| Claude tool not blocked by device | `PreToolUse` using `hook_handler.py`? `prompt_id` matching? timeout? |
| UI stats incorrect | `stats-cache.json` has data? JSONL session files readable? heartbeat includes field? |
| Clock not updating | `_heartbeat_loop()` running at 10s? `time_sync` sent? `wall_epoch_s` parsed correctly? |

---

## 10. Key Design Rules

1. **Protocol-first:** BLE_WIRE_PROTOCOL changes require M0 amendment sub-cycle
2. **State-driven UI:** Device UI consumes `buddy_tama_state_t` snapshot only
3. **Async pattern:** asyncio event loop for daemon, no threads
4. **Security:** Local-only HTTP (127.0.0.1), `secrets.token_hex` for IDs, no plaintext logging of MACs/tokens
5. **Idempotency:** Daemon restart recovers from PID file; hooks emit `{}` when daemon is down
6. **Single in-flight prompt:** New prompt overwrites previous; no per-prompt queue

---

*Document generated 2026-05-07. Source documents in `tuya_t5_pocket_ai/docs/` and `tuya_t5_pocket_ai/doc/`.*
