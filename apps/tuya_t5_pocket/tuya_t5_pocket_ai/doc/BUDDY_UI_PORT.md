# Claude Desktop Buddy UI Port

Port of the UI portion of `claude-desktop-buddy` to the TuyaOpen
`tuya_t5_pocket_ai` application. This iteration focuses on **visual and
interaction replication only**; the real BLE + JSON backend is stubbed with
a demo timer so the art, layouts, and input flow can be validated on the
384x168 landscape panel.

---

## 1. Goals

| Goal | Status |
|------|--------|
| Replicate Claude Buddy UI under `src/display/ui/buddy_ui/` | Done |
| Use the existing LVGL screen-stack manager | Done |
| Add Buddy entry as the first item of `menu_scan_screen` | Done |
| Build cleanly for `T5AI / TUYA_T5AI_POCKET` | Done (`BUILD SUCCESS`) |
| Integrate live BLE/JSON backend | Deferred |

## 2. Deliverables

```
apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/
  buddy_data.h                (pre-existing - persona / stats types)
  buddy_main_screen.h         (pre-existing - extern Screen_t)
  buddy_main_screen.c         NEW  main 384x168 UI + demo timer
  buddy_approval_screen.h     NEW  permission prompt screen
  buddy_approval_screen.c     NEW  approve/deny flow
  buddy_ui_entry.h            NEW  umbrella header
```

Menu integration:

```
apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/menu_scan_screen.c
  + #include "buddy_ui_entry.h"
  + first list button: "[BLUETOOTH] Claude Buddy" -> buddy_main_screen
  + renumbered switch() cases (0..9)
```

No `CMakeLists.txt` edits were needed - `src/display/ui/buddy_ui` is already
picked up by `aux_source_directory`.

## 3. Main Screen Layout (384 x 168)

```
+-----------------------------------------------------------+
| BLE  Claude Buddy          s:3 ok m:12     [BUSY]         |  20 px status bar
+--------------------+--------------------------------------+
|                    |  NORMAL | STATS | INFO               |
|                    |                                      |
|      [ ducky GIF ] |  mode-dependent content (128 px)     |
|      160 x 128 px  |                                      |
|                    |                                      |
+--------------------+--------------------------------------+
| MODE: NORMAL   [ENTER] cycle   [LEFT/RIGHT] page  p 1/1   |  20 px nav bar
+-----------------------------------------------------------+
```

### Persona to GIF map

Identical to the original project:

| Persona             | Asset (`ducky_*_gif`) |
|---------------------|------------------------|
| `SLEEP`             | `sleep` |
| `IDLE`              | `idle` |
| `BUSY`              | `walk` |
| `ATTENTION`         | `sparkles` |
| `CELEBRATE`         | `wedding` |
| `CELEBRATE_SUCCESS` | `happy_1` |
| `CELEBRATE_FAIL`    | `sad_1` |
| `DIZZY`             | `sick_1` |
| `HEART`             | `love_1` |

All GIFs are pre-created with `lv_gif_create()` and hidden via
`LV_OBJ_FLAG_HIDDEN`; `show_only_gif()` toggles the active one to avoid
re-decoding at every persona switch.

### Modes and pages

| Mode | Pages | Page content |
|------|-------|--------------|
| `NORMAL` | 1 | session, tokens in/out, last tool |
| `STATS`  | 2 | (1) mood / fed / energy bars   (2) level / XP / approvals |
| `INFO`   | 6 | About, Buttons, Claude, Device, Bluetooth, Credits |

## 4. Key Bindings

| Key | Action |
|-----|--------|
| `KEY_ENTER` | cycle mode `NORMAL -> STATS -> INFO -> NORMAL` |
| `KEY_LEFT`  | previous page (STATS/INFO) |
| `KEY_RIGHT` | next page (STATS/INFO) |
| `KEY_UP`    | demo: advance persona |
| `KEY_DOWN`  | demo: rewind persona |
| `KEY_ESC`   | `screen_back()` - return to menu |

## 5. Demo Simulation

`demo_timer_cb()` fires every 800 ms and walks a 6-entry scene table
(`DEMO_SCENES`) that rotates every 8 seconds, mutating:

- `s_tama.state`           (IDLE / THINKING / RESPONDING / CELEBRATING / TIRED / DIZZY / HEART)
- `s_tama.mood / fed / energy / level / xp`
- `s_tama.last_tool`       (`"Read"`, `"Edit"`, `"Bash"`, `"Glob"`, ...)
- `s_stats.{tokens_in, tokens_out, sessions_started, approvals_requested, approvals_granted}`

Persona is then recomputed by `derive_persona()` using the same rules as
`claude-desktop-buddy/src/main.cpp::derivePersona()`:

```
last_event == ACT_COMPLETED              -> CELEBRATE_SUCCESS (6 s)
last_event == ACT_FAILED                 -> CELEBRATE_FAIL     (6 s)
state      == TAMA_DIZZY                 -> DIZZY
state      == TAMA_HEART                 -> HEART
energy < 20 or mood < 20                 -> CELEBRATE_FAIL proxy / DIZZY
busy_active                              -> BUSY / ATTENTION
otherwise                                -> IDLE / SLEEP
```

## 6. Approval Screen

`buddy_approval_screen` mirrors `drawApproval()` from the original sketch:

- Title bar (hot orange) with static label and elapsed seconds
  (turns hot after 10 s).
- `Tool: <name>` and `File: <hint>` labels.
- Footer with key hints: `[ENTER] approve   [RIGHT] deny   [ESC] back`.

Staging API for callers (e.g. the future BLE handler):

```c
buddy_approval_screen_set_prompt(id, tool, hint);
screen_load(&buddy_approval_screen);
```

Pressing ENTER or RIGHT flips an internal `s_decided` flag and logs the
decision via `printf` - this is the hook where the real `{"cmd":"permission",
"id":..., "decision":"once|deny"}` payload will be emitted once the BLE
transport is wired in.

## 7. Menu Integration

`menu_scan_screen` is the scan/utility landing page. Claude Buddy is now the
first entry:

```
[BLUETOOTH] Claude Buddy          <- NEW, launches buddy_main_screen
[WIFI]      WiFi scan demo
[LIST]      I2C device scan demo
[PLAY]      Dino Game
...
```

The selected index pipe was shifted by +1 so the existing behaviours remain
reachable.

## 8. Thread Safety

All deferred callbacks that touch LVGL primitives wrap their updates with
`lv_vendor_disp_lock()` / `lv_vendor_disp_unlock()`, matching the pattern
used by `ai_chat_screen.c`. The timer callbacks are already on the LVGL
thread, so they operate without an explicit lock.

## 9. Build Verification

```
$ cd apps/tuya_t5_pocket/tuya_t5_pocket_ai
$ tos.py build
...
====================[ BUILD SUCCESS ]===================
 Target    : tuya_t5_pocket_ai_QIO_0.0.1.bin
 Platform  : T5AI
 Chip      : T5AI
 Board     : TUYA_T5AI_POCKET
 Framework : base
========================================================
```

One iteration fix was required during the build: `buddy_main_screen.c`
needed `#include "lv_vendor.h"` to pick up `lv_vendor_disp_lock/unlock()`
(the v9 port exposes them via that header, same as `ai_chat_screen.c`).

## 10. Deferred Work

These live under the "Phase 2" section of `PORTING_PLAN.md`:

- BLE peripheral bring-up (`BuddyService` equivalent on Tuya BLE).
- JSON framing (`{"cmd":"state", "cmd":"event", "cmd":"permission"}`).
- Replace `DEMO_SCENES` with live `buddy_state_update()` /
  `buddy_event_ingest()` calls from the BLE layer.
- Connect `buddy_approval_screen` decision to the BLE writer and honor the
  10-second deny-on-timeout guarantee.
- Asset trimming: several ducky GIFs currently referenced only by the demo
  cycle can be pruned when the real persona feed limits the active set.
