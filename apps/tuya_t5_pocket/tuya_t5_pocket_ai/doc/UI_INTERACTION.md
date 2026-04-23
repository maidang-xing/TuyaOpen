# Claude Buddy — UI Interaction

> **Note (M1-UI / 2026-04):** This document captures the **M1-A** baseline
> (entries ring + HH:MM clock). The **M1-UI** milestone replaces the body
> layout with a left ASCII-persona canvas + right text column, rebinds
> `KEY_LEFT/RIGHT` to persona cycling when no prompt is pending, and adds
> LED-state mirroring and a custom-GIF placeholder. For the authoritative
> post-M1-UI specification see [`UI_INTERACTION_zh.md`](./UI_INTERACTION_zh.md)
> (Chinese, canonical). Keep this file as-is for historical reference; any
> discrepancy is resolved in favour of the Chinese version until this file
> is rewritten.

| | |
|---|---|
| Target | `apps/tuya_t5_pocket/tuya_t5_pocket_ai` |
| Panel | 384×168 mono-capable colour LCD (`AI_PET_SCREEN_*`) |
| Relevant source | `src/display/ui/buddy_ui/buddy_main_screen.c` |
| Related | [`BUDDY_UI_PORT.md`](./BUDDY_UI_PORT.md), [`docs/protocol/BLE_WIRE_PROTOCOL.md`](../docs/protocol/BLE_WIRE_PROTOCOL.md), [`UI_INTERACTION_zh.md`](./UI_INTERACTION_zh.md) |
| Milestone | M1-A (entries ring + HH:MM clock) — see zh doc for M1-UI delta |

---

## 1. Screen regions

```
┌─────────────────────────────────────────────────────────────┐  HEADER  — 20 px
│ Claude Buddy       BLE: linked        HH:MM        <device>│
├─────────────────────────────────────────────────────────────┤  BODY    — 124 px
│  msg / sessions / tokens / owner    (top half)              │
│  ──────────── Entries transcript panel ─────────────        │
│   HH:MM  Ran Read(./src/foo.c)        ← newest, bold        │
│   HH:MM  Turn 4: 2.1k tok                                   │
│   HH:MM  Session start: t5-pocket-ai                        │
│   HH:MM  ...                          (up to 4 visible)     │
├─────────────────────────────────────────────────────────────┤  FOOTER  — 24 px
│ Contextual hint line                                        │
└─────────────────────────────────────────────────────────────┘
```

### Header

| Slot                    | Source field              | Notes                                                              |
|-------------------------|---------------------------|--------------------------------------------------------------------|
| Title (left)            | hard-coded `"Claude Buddy"` | Static label.                                                    |
| BLE indicator (mid-left)| `ble_connected`           | `"BLE: linked"` / `"BLE: -"`.                                      |
| **HH:MM clock** (centre)| `wall_epoch_s` + `wall_tz_min` + `wall_local_ms_at_rx` | Derived UI-side from the latest `{"time":[epoch, tz]}` frame. Renders `"--:--"` until a time sync has been received. Integer math uses 64-bit throughout. The TuyaOS RTC is **not** written. |
| Device name (right)     | `device_name`             | `Claude_XXXX` as advertised.                                       |

## 2. Body

Two body views are alternated:

- **Idle view** — populated when no permission prompt is pending. Renders
  the compact `msg`, `sessions`, `tokens`, `owner` lines at the top and
  the Entries transcript panel directly below.
- **Permission card** — shown on top of the body when `has_prompt` is
  true. Hides the idle labels and the entries panel so the operator's
  attention is on the decision.

### Entries panel

The entries panel renders up to four lines sourced from the heartbeat
`entries[]` array. Semantics:

- Storage: in-memory ring of `BUDDY_ENTRIES_RING = 8` slots, each up to
  `BUDDY_ENTRY_MAX_CHARS = 79` bytes of UTF-8 text (+ NUL). The parser
  always clears the ring before refilling, so stale text cannot leak
  across peers.
- Ordering: newest entry at `entries[entries_head]`; the panel walks
  backwards through the ring so the top visible row is the most recent.
- Format: `HH:MM  <text>`, where `HH:MM` is the current device-side wall
  clock derived from the time-sync offset (or `--:--` if no sync yet).
- Styling: the newest visible row is drawn in `BUDDY_FONT_CONTENT`
  (bold 16 px); the three older visible rows use `BUDDY_FONT_HINT`
  (bold 14 px). No animation.
- Scroll: `s_entries_scroll` selects the window start. `0` pins the
  window to the newest 4 entries; incrementing pushes the window back
  by one row. The scroll is clamped so the top visible slot is always
  populated. Scrolling is **always** available — it does not gate on
  the prompt state — because approve/deny/always and scroll live on
  disjoint input slots.

## 3. Footer

Context-sensitive hint that summarises the legal buttons:

| State                              | Hint                                                    |
|------------------------------------|---------------------------------------------------------|
| Pending prompt                     | `ENTER=OK LEFT=deny RIGHT=always UP/DOWN=scroll ESC=back` |
| Connected, idle                    | `UP/DOWN=scroll JOYCON=refresh ESC=back`                |
| Disconnected                       | `Waiting for Claude desktop...   ESC=back`              |

## 4. Button map (modeless)

The T5 pocket exposes a 4-way joystick with a centre press
(`KEY_UP/DOWN/LEFT/RIGHT/JOYCON`) plus two physical buttons
(`KEY_ENTER`, `KEY_ESC`).

| Input        | Behaviour                                                                   | TX frame                                                                 |
|--------------|-----------------------------------------------------------------------------|--------------------------------------------------------------------------|
| `KEY_ENTER`  | Approve the pending prompt (if any)                                         | `{"cmd":"permission","id":"<id>","decision":"once"}`                    |
| `KEY_LEFT`   | Deny the pending prompt (if any) — **moved from `KEY_RIGHT`**               | `{"cmd":"permission","id":"<id>","decision":"deny"}`                    |
| `KEY_RIGHT`  | Approve the pending prompt and remember (if any) — **moved from `KEY_UP`**  | `{"cmd":"permission","id":"<id>","decision":"always"}`                  |
| `KEY_UP`     | Scroll the entries window one row toward older entries (always active)       | none — local UI only                                                     |
| `KEY_DOWN`   | Scroll the entries window one row toward newer entries (always active)       | none — local UI only                                                     |
| `KEY_JOYCON` | Ask the desktop to resend its snapshot — **moved from `KEY_DOWN`**          | `{"cmd":"status"}`                                                       |
| `KEY_ESC`    | Return to the previous screen                                                | none — local UI only                                                     |

Clamp rules:

- `KEY_UP` advances `s_entries_scroll` only while
  `s_entries_scroll + 4 < entries_count`.
- `KEY_DOWN` retreats `s_entries_scroll` only while `s_entries_scroll > 0`.
- Without a pending prompt, `KEY_ENTER` / `KEY_LEFT` / `KEY_RIGHT` are
  silently ignored.

## 5. Log anchors

Runtime verification anchors written at `PR_DEBUG` level (never INFO or
NOTICE — entry text may contain file paths):

- `buddy_ble entries: idx=<n> text=<..80 chars..>` — one per stored entry
  when a heartbeat's `entries[]` array is parsed.
- `buddy_ble time sync ok epoch=<lld> tz=<d>` — on every successful
  `{"time":[epoch, tz]}` frame.
- `ui clock render HH=<hh> MM=<mm>` — emitted by `__format_clock` whenever
  the header clock or an entry row is repainted.
- `ui scroll idx=<n> count=<c>` — emitted when UP/DOWN changes the scroll.

Refer to `docs/protocol/baseline/` for captured snapshots demonstrating
these anchors.
