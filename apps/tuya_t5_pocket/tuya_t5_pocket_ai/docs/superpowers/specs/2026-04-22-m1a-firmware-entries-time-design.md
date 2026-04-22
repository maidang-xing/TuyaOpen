# M1-A Firmware UI — `entries[]` scroll + `time[]` UI offset

| | |
|---|---|
| Milestone | M1-A (Firmware UI round 1) |
| Parent umbrella spec | `docs/superpowers/specs/2026-04-21-claude-cli-buddy-t5-design.md` |
| Parent umbrella plan | `docs/superpowers/plans/2026-04-21-claude-cli-buddy-t5.md` |
| Protocol reference | `docs/protocol/BLE_WIRE_PROTOCOL.md` v1.0 §7 gaps A & E |
| Baseline | `docs/protocol/baseline/616464c5-runtime.log` |
| Owner | Firmware sub-project |
| Status | Proposed |
| Date | 2026-04-22 |

---

## 1. Goal

Close two — and only two — of the §7 gaps in the frozen wire protocol:

1. **Gap A — `entries[]` array.** Today the firmware parses the heartbeat
   `msg` field (a single status string) but silently drops the heartbeat's
   optional `entries[]` array (a short running transcript). Consume it and
   render on-device.
2. **Gap E — `time[]` offset.** Today the firmware logs `time sync received`
   and throws the values away. Store an in-RAM epoch-seconds + tz-minutes
   offset and use it to format HH:MM in the header bar.

Everything else in §7 (real `unpair` bond-erase, `evt:"turn"`, `status.sec`
/ LESC, 30-s liveness timeout) is **out of scope** and stays on the gap
table for a later cycle.

## 2. In-scope / Out-of-scope

### In scope
- Extend `buddy_tama_state_t` with an entries ring and a wall-clock offset.
- Extend `buddy_ble.c` heartbeat parser to fill the ring.
- Extend `buddy_ble.c` `__handle_line` to accept `{"time":[epoch,tz]}` and
  update the offset (not the system RTC).
- Extend `buddy_main_screen.c` to render the entries ring and the HH:MM
  clock, and to route joystick UP/DOWN to a scroll index.
- Update `BLE_WIRE_PROTOCOL.md` §3.2 (mark `entries[]` parsed) and §3.3
  (mark `time[]` applied to UI), then flip those two rows in §7 from
  "Gap" to "Implemented in M1-A".
- Update `doc/UI_INTERACTION.md` to reflect the new button map and the
  entries panel.
- Capture a new baseline log `docs/protocol/baseline/<sha>-m1a.log`
  demonstrating entries rendering + time sync in one session.

### Out of scope
- LESC / encrypted characteristics. `status.sec` stays `false`.
- Real `tal_ble_bond_erase` on `cmd:"unpair"`. Stays cosmetic ack.
- `evt:"turn"` consumption. Turns stay as side-effect-free pass-through.
- Liveness timeout / "disconnected" banner. Link-state derived from
  `TAL_BLE_EVT_DISCONNECT` only, as today.
- Pushing `time[]` to TuyaOS RTC (`tal_time_set_posix` or equivalent).
  Explicitly **not** done; the offset is UI-only.
- Any change to the advertising name derivation or transport framing.
- Any change to the plugin side (M1-C). M1-A delivers a firmware binary
  that is wire-compatible with both Claude Desktop and the M1-C plugin.

## 3. Design

### 3.1 State model

Add to `buddy_data.h`:

```c
#define BUDDY_ENTRY_MAX_CHARS  79   /* 80 B incl. NUL */
#define BUDDY_ENTRIES_RING     8

typedef struct {
    char  text[BUDDY_ENTRY_MAX_CHARS + 1];
} buddy_entry_t;

typedef struct {
    /* ... existing fields unchanged ... */

    /* Running transcript ring: newest at head, oldest at tail.
     * `count` is how many slots are populated, <= BUDDY_ENTRIES_RING.
     * `head` points at the most recent entry. */
    buddy_entry_t entries[BUDDY_ENTRIES_RING];
    uint8_t       entries_count;
    uint8_t       entries_head;

    /* Wall-clock sync from `{"time":[epoch, tz]}`. Both zero means
     * "not synced yet"; UI renders "--:--". */
    int64_t       wall_epoch_s;      /* value at wall_local_ms_at_rx */
    int16_t       wall_tz_min;       /* signed offset in minutes     */
    uint64_t      wall_local_ms_at_rx; /* tal_system_get_millisecond() */
} buddy_tama_state_t;
```

Invariants:
- Total state struct growth: `8*80 + 1 + 1 + 8 + 2 + 8 = 660` B. Round to
  664 with padding. Acceptable inside the existing singleton.
- `entries_head` always wraps modulo `BUDDY_ENTRIES_RING`.
- Scroll index lives in `buddy_main_screen.c`, not in `buddy_data.h`; the
  BLE bridge owns only the ring.

### 3.2 BLE bridge changes (`buddy_ble.c`)

Heartbeat parser diff (conceptual):

```c
cJSON *entries = cJSON_GetObjectItem(root, "entries");
if (cJSON_IsArray(entries)) {
    int n = cJSON_GetArraySize(entries);
    int first = (n > BUDDY_ENTRIES_RING) ? (n - BUDDY_ENTRIES_RING) : 0;
    __reset_entries(&snap);                      /* clear ring before refill */
    for (int i = first; i < n; i++) {
        cJSON *e = cJSON_GetArrayItem(entries, i);
        if (cJSON_IsString(e) && e->valuestring) {
            __push_entry(&snap, e->valuestring); /* bounded copy */
        }
    }
}
```

`__push_entry` truncates at `BUDDY_ENTRY_MAX_CHARS` using `snprintf`
(no unbounded string ops, per TuyaOS C security rule). `__reset_entries`
zeroes head/count/text so stale ring data can never bleed across peers.

Time parser diff:

```c
cJSON *time_arr = cJSON_GetObjectItem(root, "time");
if (cJSON_IsArray(time_arr) && cJSON_GetArraySize(time_arr) >= 2) {
    cJSON *epoch = cJSON_GetArrayItem(time_arr, 0);
    cJSON *tz    = cJSON_GetArrayItem(time_arr, 1);
    if (cJSON_IsNumber(epoch) && cJSON_IsNumber(tz)) {
        tal_mutex_lock(s_state_mutex);
        s_state.wall_epoch_s       = (int64_t)epoch->valuedouble;
        s_state.wall_tz_min        = (int16_t)tz->valuedouble;
        s_state.wall_local_ms_at_rx = tal_system_get_millisecond();
        tal_mutex_unlock(s_state_mutex);
        PR_DEBUG("%s time sync ok epoch=%lld tz=%d",
                 BUDDY_BLE_TAG,
                 (long long)s_state.wall_epoch_s,
                 (int)s_state.wall_tz_min);
    }
}
```

Integer overflow: `wall_epoch_s` is 64-bit; `wall_local_ms_at_rx` is
64-bit. Derived local time = `wall_epoch_s + (now_ms - wall_local_ms_at_rx)
/ 1000 + wall_tz_min * 60`. All computed in 64-bit, no `UINT32_T` wrap.
(Per TuyaOS C security · Integer Overflow rule.)

No change to TX path (permission/status/ack). No change to advertising
name derivation. No change to the TAL sniffer hook.

### 3.3 UI changes (`buddy_main_screen.c`)

**Layout.** The 384×168 panel is partitioned:

```
┌─────────────────────────────────────────────────────────────┐ 20 px header
│ Claude_A1B2        HH:MM              [BLE ●]               │
├─────────────────────────────────────────────────────────────┤ 124 px body
│                                                             │
│  Prompt card OR status lines (unchanged existing behaviour) │
│                                                             │
│  ───── Entries ring (new, bottom portion of body) ───────── │
│   > 15:04  Ran Read(./src/foo.c)        ← newest, bold      │
│     15:03  Turn 4: 2.1k tok                                  │
│     15:02  Session start: t5-pocket-ai                       │
│     ...    (up to 4 visible at once)                         │
│                                                             │
├─────────────────────────────────────────────────────────────┤ 24 px footer
│ ENTER=OK LEFT=deny RIGHT=always ↑↓=scroll                   │
└─────────────────────────────────────────────────────────────┘
```

Rendering rules (chosen to keep it mono-e-ink-legible, per umbrella spec
§2.1):
- Entries area: 4 visible rows of 14 px font (= 56 px incl. 2 px leading).
- Each line = `HH:MM  <text>` where `HH:MM` uses the stored wall offset;
  if `wall_epoch_s == 0`, render `--:--`.
- Scroll index `s_entries_scroll = 0` shows newest 4; incrementing pushes
  the window one entry back. Clamped to `[0, entries_count - 1]`.
- No highlight / no animation. The newest visible entry is drawn bold.

**Buttons** (modeless — per brainstorm decision `m1a_buttons:modeless_scroll`):

| Input        | Action                                                    |
|--------------|-----------------------------------------------------------|
| `KEY_ENTER`  | `permission:"once"` (unchanged)                           |
| `KEY_LEFT`   | `permission:"deny"` — **moved from `KEY_RIGHT`**          |
| `KEY_RIGHT`  | `permission:"always"` — **moved from `KEY_UP`**           |
| `KEY_UP`     | `s_entries_scroll++` (clamped). **Always scrolls.**        |
| `KEY_DOWN`   | `s_entries_scroll--` (clamped). **Always scrolls.**        |
| `KEY_JOYCON` | `buddy_ble_send_cmd("status")` — **moved from `KEY_DOWN`**|
| `KEY_ESC`    | `screen_back()` (unchanged)                               |

Rationale: existing board (`game_pet_indev.c`) maps joystick UP/DOWN/
LEFT/RIGHT plus a centre press (`KEY_JOYCON`) plus two physical buttons
(`KEY_ENTER`, `KEY_ESC`), so all seven slots exist in hardware. Scrolling
never interferes with pending-prompt workflow because approve/deny/always
are on a disjoint input set.

`doc/UI_INTERACTION.md` MUST be updated to reflect the new map in the
same PR that lands the code.

### 3.4 Protocol document changes

Edits to `docs/protocol/BLE_WIRE_PROTOCOL.md`:
- §3.2 "Peer → device: heartbeat" — change the `entries[]` row to
  `✅ parsed into a ring of BUDDY_ENTRIES_RING entries of up to
  BUDDY_ENTRY_MAX_CHARS bytes` and reference the new §3.2.1 (add)
  describing ring semantics.
- §3.3 "Peer → device: time sync" — change `time[]` application from
  "logged only" to "UI offset; no RTC write".
- §7 "Gap table" — flip rows A (`entries[]`) and E (`time[]` application)
  from "Gap" to "Closed in M1-A (commit <sha>)". Leave B/C/D/F/G open.
- Bump doc version to v1.1. Amendment procedure per §8 is satisfied by:
  spec approval (this file) + plan approval + on-hardware baseline
  capture + firmware commit.

## 4. Test contracts

Per umbrella spec §3.3, all claims require a captured log from the
canonical loop. Proof artefacts:

1. **Build.** `cd apps/tuya_t5_pocket/tuya_t5_pocket_ai && tos.py build`
   succeeds on a clean checkout. Zero warnings introduced.
2. **Static self-check.** `grep -nE 'BUDDY_ENTRIES_RING|BUDDY_ENTRY_MAX_CHARS'`
   resolves only in `buddy_data.h`, `buddy_ble.c`, and `buddy_main_screen.c`.
3. **Flash.** `tos.py flash` to `/dev/ttyACM0`, baud `460800`. No flasher
   error. Device re-enumerates.
4. **Runtime proof.** Capture `docs/protocol/baseline/<sha>-m1a.log` via
   `agent_target_tool.py service start --detach` on `/dev/ttyACM1`. Log
   must contain in this order:
   a. `buddy_ble init done` (if reset-capable; otherwise reuse the M0
      runtime-log waiver documented in `docs/protocol/baseline/README.md`).
   b. `time sync ok epoch=` with a plausible epoch (> 1.7e9).
   c. At least 3 distinct `entries:` prefixed DEBUG lines from
      `__handle_heartbeat` (to be added), each with a different index.
   d. A `HH:MM` rendered on the panel (visual gate — photograph or UI
      dump; if no camera is in the loop, the log's `time sync ok` line
      plus a `ui clock render HH=%d MM=%d` DEBUG line is sufficient).
5. **Interaction proof.** With a pending prompt loaded, press UP/DOWN
   and observe `s_entries_scroll=N` DEBUG lines, followed by `ENTER` /
   `LEFT` / `RIGHT` and observe the matching `permission:` TX lines. All
   captured in the same baseline log.
6. **Regression.** The M0 baseline (`616464c5-runtime.log`) anchors
   (`advertising as Claude_`, `peer connected`, `MTU 517`, `ack:"owner"`)
   all still match against the new log. No wire-protocol regression.

Failure = any of 1–6 missing; the spec is unsatisfied and the milestone
does not close.

## 5. Constraints

Inherited from umbrella spec:
- Firmware language and style per `.cursor/rules` TuyaOS C rule.
- Sensitive-data logging discipline — `entries[]` text may contain file
  paths or command fragments; log at DEBUG only, never at INFO/NOTICE.
  Individual entry text is already capped at 80 B so no log-explosion risk.
- Memory via `tal_malloc`/`tal_free`; no direct `malloc`. (Ring lives in
  the existing singleton — no new heap allocation needed.)

Milestone-specific:
- No change to BLE stack initialisation path. M1-A is a pure
  application-layer change; the "静态共注册" strategy from the earlier
  BLE work is preserved.
- Must build on the same platform target as the M0 baseline
  (`platform/tuyaopen/platform.yaml` pinned to the existing SHA).

## 6. Amendments from this spec

If on-hardware bring-up uncovers that the 80 B per-entry cap truncates
visibly useful text, the implementer MAY raise the cap to 96 B and drop
the ring to 6 slots (total ≈ 580 B, still under our 1 KB budget). Any
other deviation — especially anything that touches the wire protocol —
requires a new spec round.
