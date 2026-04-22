# M1-A Firmware — Execution Plan

| | |
|---|---|
| Spec | `docs/superpowers/specs/2026-04-22-m1a-firmware-entries-time-design.md` |
| Parent plan | `docs/superpowers/plans/2026-04-21-claude-cli-buddy-t5.md` |
| Owner | Firmware subagent (isolated worktree) |
| Status | Proposed |
| Date | 2026-04-22 |
| Regen reference | See spec §4 "Test contracts" |

---

## 0. Operating principles (subagent, read first)

1. **You are working against a frozen wire protocol.** Every edit to
   `buddy_ble.c` must keep the outward frames byte-compatible with
   `BLE_WIRE_PROTOCOL.md` v1.0; you are adding *additional* consumers
   of existing fields, never changing the wire.
2. **TDD discipline where possible.** The firmware side has no unit
   harness, so "tests" are: (a) static grep assertions on the source,
   (b) clean build, (c) on-hardware log capture that matches
   pre-declared patterns. Declare the pattern BEFORE writing the code,
   then watch it appear.
3. **No firmware runtime changes outside the files listed in the spec.**
   If a task tempts you to touch BLE stack init, advertising, or the
   sniffer hook, STOP and escalate.
4. **Follow TuyaOS C style** (`.cursor/rules` TuyaOS-C-Style) for every
   new line: braces on single-statement ifs, `STATIC`/`VOID_T` macros,
   `tal_malloc`/`tal_free` (n/a here — no allocs), `snprintf` over
   `sprintf`, bounds-checked entry text.
5. **Every function gets a Doxygen comment** with `@brief`, `@param`,
   `@return`. English only.
6. **Toolchain** matches M0: `tos.py build`, `tos.py flash` on
   `/dev/ttyACM0` @ 460800, log via
   `/home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py`
   on `/dev/ttyACM1`.
7. **Baseline log is proof.** The milestone closes only when
   `docs/protocol/baseline/<sha>-m1a.log` exists and contains every
   anchor listed in §8 of this plan.

## 1. Task list (execute in order)

### T1 — Declare new DEBUG anchor strings (static, pre-code)

**Pre**: none.

**Action**: Grep first to prove the strings are absent, then record the
exact text you intend to add. This is the "test fails red" step.

```bash
cd /home/share/samba/tyopen/TuyaOpen/apps/tuya_t5_pocket/tuya_t5_pocket_ai
grep -RnE "time sync ok epoch=|entries: idx=|ui clock render HH=" src/ && echo "FAIL: anchors already present" || echo "OK: anchors absent"
```

Deliverable: paste the anchor strings into the task log for this plan:
- `"%s time sync ok epoch=%lld tz=%d"` (in `buddy_ble.c`)
- `"%s entries: idx=%d text=%.80s"` (in `buddy_ble.c`)
- `"ui clock render HH=%02d MM=%02d"` (in `buddy_main_screen.c`)
- `"ui scroll idx=%d count=%d"` (in `buddy_main_screen.c`)

**Post**: zero code changes yet.

### T2 — Extend `buddy_data.h`

**Pre**: T1 complete.

**Action**: Add `buddy_entry_t`, the ring fields, and the wall-clock
triple exactly as specified in spec §3.1. Respect the numeric caps:

- `BUDDY_ENTRY_MAX_CHARS = 79`
- `BUDDY_ENTRIES_RING = 8`

Include guards MUST remain intact. No new external headers included.

**Verification**:

```bash
grep -cE "BUDDY_ENTRY_MAX_CHARS|BUDDY_ENTRIES_RING|wall_epoch_s" \
  apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_data.h
# expect ≥ 4
```

Build: `tos.py build` must succeed. This task alone won't light the new
anchors yet.

### T3 — Extend `buddy_ble.c` heartbeat parser (entries ring)

**Pre**: T2 complete.

**Action**: In `__handle_heartbeat`, after the existing `msg` parse, add:

1. Under the state mutex, call a new helper `__reset_entries(&snap)`
   that zeroes `entries_count`/`entries_head`/`entries[].text` — always
   executed when the heartbeat carries an `entries` array (even if it's
   empty, so stale data never lingers cross-peer).
2. Iterate the array with `cJSON_GetArraySize` + `cJSON_GetArrayItem`.
   Discard items that aren't strings. For strings, call
   `__push_entry(&snap, cJSON_GetArrayItem(entries, i)->valuestring)`.
3. `__push_entry` copies at most `BUDDY_ENTRY_MAX_CHARS` bytes via
   `snprintf` to guarantee NUL termination; advances `entries_head`
   with `(head + 1) % BUDDY_ENTRIES_RING`; saturates `entries_count`.
4. Emit one `PR_DEBUG("%s entries: idx=%d text=%.80s", ...)` per stored
   entry, so the anchor pattern is observable in the baseline log.

Static-analysis constraints (TuyaOS C security rule):
- Entry source pointer checked for NULL before `snprintf`.
- Ring math done in `uint8_t` — index can't overflow the ring (8 slots,
  wraps within `uint8_t` range trivially).
- No `strcpy`/`strcat`/`sprintf`.
- No logging of `msg` or `entries` at INFO/NOTICE level.

**Verification**:

```bash
grep -nE "__reset_entries|__push_entry|entries: idx=" \
  apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c
# expect all 3 symbols present
```

Build: `tos.py build` clean.

### T4 — Extend `buddy_ble.c` `__handle_line` (time sync)

**Pre**: T3 complete.

**Action**: Replace the existing `"time sync received"` debug with the
parsed variant. Store `wall_epoch_s` + `wall_tz_min` +
`wall_local_ms_at_rx` under the state mutex. Emit `"time sync ok
epoch=<lld> tz=<d>"` at DEBUG.

Validate inputs: require `cJSON_IsArray(time_arr)`, size ≥ 2, both
items numeric. On any validation failure, fall through silently (bad
peer, don't crash, don't amplify).

**Verification**:

```bash
grep -nE "time sync ok epoch=|wall_epoch_s" \
  apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c
```

Build: `tos.py build` clean.

### T5 — Add clock helper + entry formatter (UI)

**Pre**: T4 complete.

**Action** in `buddy_main_screen.c`:

- Add `__format_clock(const buddy_tama_state_t *s, char *out, size_t n)`
  that computes `hours`, `minutes` from `(wall_epoch_s + (now_ms -
  wall_local_ms_at_rx)/1000 + wall_tz_min*60) % 86400` and formats
  `"HH:MM"`. If `wall_epoch_s == 0` write `"--:--"`. All intermediate
  math uses `int64_t` — verify no `UINT32_T` wrap by casting ints to
  `int64_t` BEFORE arithmetic. Emit `"ui clock render HH=%02d MM=%02d"`
  at DEBUG on each call.
- Add `__format_entry_line(const buddy_entry_t *e, uint64_t abs_epoch_s,
  char *out, size_t n)` that produces `"HH:MM  <text>"` using
  `__format_clock`-style offset math applied to the per-entry stamp (if
  absent, use the current wall clock).

### T6 — Add scroll-aware body renderer

**Pre**: T5 complete.

**Action**:
- Add `STATIC uint8_t s_entries_scroll = 0;` at file scope.
- Add `STATIC lv_obj_t *entries_lines[4];` container.
- Build the container in `__build_body` below the existing status /
  prompt card area. Hide the 4 labels if `entries_count == 0`.
- `__refresh_locked` computes the window
  `[s_entries_scroll, s_entries_scroll + 4)` over the ring (newest is
  index 0) and sets each visible label with the newest-first text.
- Emit `"ui scroll idx=%d count=%d"` at DEBUG when the scroll changes.

### T7 — Remap buttons (modeless scroll)

**Pre**: T6 complete.

**Action**: Rewrite `__keyboard_event_cb` exactly to the table in spec
§3.3:

```c
switch (key) {
case KEY_ENTER:  __send_decision("once");                              break;
case KEY_LEFT:   __send_decision("deny");                              break;
case KEY_RIGHT:  __send_decision("always");                            break;
case KEY_UP:
    if (s_entries_scroll + 4U < s_staged_state.entries_count) {
        s_entries_scroll++;
        __refresh_locked();
    }
    break;
case KEY_DOWN:
    if (s_entries_scroll > 0U) {
        s_entries_scroll--;
        __refresh_locked();
    }
    break;
case KEY_JOYCON:
    if (s_staged_state.ble_connected) {
        (VOID_T)buddy_ble_send_cmd("status");
    }
    break;
case KEY_ESC:    screen_back();                                        break;
default:                                                               break;
}
```

`__send_decision` is the existing helper; no change.

**Verification**:

```bash
grep -nE "case KEY_UP:|case KEY_DOWN:|case KEY_LEFT:|case KEY_RIGHT:|case KEY_JOYCON:" \
  apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_main_screen.c
# expect 5 matches, in the order above
```

Build: `tos.py build` clean. No warnings.

### T8 — Update `doc/UI_INTERACTION.md`

**Pre**: T7 complete.

**Action**: Replace the button-map table with the new mapping. Add an
"Entries panel" section describing the ring, scroll behaviour, clamp,
and that scrolling is always available (does not gate on prompt state).
Add an "HH:MM clock" line in the header description.

### T9 — Update `BLE_WIRE_PROTOCOL.md`

**Pre**: T8 complete.

**Action**: Edits to
`docs/protocol/BLE_WIRE_PROTOCOL.md`:

1. §3.2 — flip the `entries[]` row from "❌ Not parsed" to
   "✅ parsed (ring of 8 × 80 B)". Add a short subsection §3.2.1 that
   describes ring semantics and truncation.
2. §3.3 — flip `time[]` row from "logged only" to "parsed; applied as
   UI-side offset; RTC unchanged".
3. §7 — change rows A and E to "Closed in M1-A (commit `<sha>`)"; leave
   other rows untouched.
4. Bump the header table `Version` to `v1.1` and `Date` to the commit
   date.

### T10 — Clean build + flash + capture baseline log

**Pre**: T1–T9 complete. Current working tree has all the edits.

**Action**:

```bash
cd /home/share/samba/tyopen/TuyaOpen/apps/tuya_t5_pocket/tuya_t5_pocket_ai
tos.py build                                                              # MUST be warning-clean
git rev-parse --short HEAD > /tmp/m1a_sha
SHA=$(cat /tmp/m1a_sha)

# Identify the two UART ports via the hardware-debug-helper skill:
python3 /home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
  list-devices --json

# Flash on /dev/ttyACM0, monitor on /dev/ttyACM1 (consistent with M0):
tos.py flash --port /dev/ttyACM0 --baud 460800

# Start detached logging service on /dev/ttyACM1:
python3 /home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
  service stop
python3 /home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
  service start --detach --port /dev/ttyACM1 --baud 460800

# Drive the device from Claude Desktop:
#  a) connect
#  b) let ≥ 2 heartbeats flow
#  c) `cmd:"status"` roundtrip
#  d) issue one permission prompt; press ENTER on the device
#  e) issue another permission prompt; press LEFT
#  f) issue another permission prompt; press RIGHT
#  g) press JOYCON to nudge a refresh
#  h) scroll entries with UP/DOWN

# Stop logging and copy:
python3 /home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
  service stop
cp .target_logging/*.log \
  apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/${SHA}-m1a.log
```

**Verification gate** (each line MUST return a hit):

```bash
LOG=apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/${SHA}-m1a.log
grep -c 'buddy_ble init done\|peer connected'                 "$LOG"   # ≥ 1
grep -c 'time sync ok epoch='                                 "$LOG"   # ≥ 1
grep -c 'entries: idx='                                       "$LOG"   # ≥ 3
grep -c 'ui clock render HH='                                 "$LOG"   # ≥ 1
grep -c 'ui scroll idx='                                      "$LOG"   # ≥ 2
grep -c '"cmd":"permission","id":".*","decision":"once"'      "$LOG"   # ≥ 1
grep -c '"cmd":"permission","id":".*","decision":"deny"'      "$LOG"   # ≥ 1
grep -c '"cmd":"permission","id":".*","decision":"always"'    "$LOG"   # ≥ 1
```

If the canonical DTR-reset limitation from M0 applies (no `buddy_ble
init done` line), accept the runtime anchor set instead — document the
decision in the PR description, same way M0 did.

### T11 — Append to `docs/protocol/baseline/README.md`

**Pre**: T10 log exists.

**Action**: Add a new row to the "Baseline index" table with `<sha>-m1a.log`
and a short note about what session it captures. Mention that M1-A was
the first milestone to exercise `entries[]` and `time[]` end-to-end.

### T12 — Self-review checklist

**Pre**: T11 done.

**Action**: walk the list below and leave per-item PASS/FAIL notes in
the PR description.

- [ ] `git diff` shows changes **only** in:
      `src/display/ui/buddy_ui/buddy_data.h`,
      `src/display/ui/buddy_ui/buddy_ble.c`,
      `src/display/ui/buddy_ui/buddy_main_screen.c`,
      `doc/UI_INTERACTION.md`,
      `docs/protocol/BLE_WIRE_PROTOCOL.md`,
      `docs/protocol/baseline/*.log`,
      `docs/protocol/baseline/README.md`,
      and plan/spec files under `docs/superpowers/`.
- [ ] No new `malloc`/`free` calls (check `git diff -G '\bmalloc|\bfree\b'`).
- [ ] No unbounded string ops (check `git diff -G 'strcpy|strcat|\bsprintf\b|\bgets\b'`).
- [ ] Every new/changed function has a Doxygen block.
- [ ] `grep -n 'PR_INFO\|PR_NOTICE' src/display/ui/buddy_ui/buddy_ble.c`
      does **not** print any of the new `entries:` / `time sync` lines
      (logged at DEBUG only).
- [ ] M0 regression anchors (§7 of the wire protocol doc) still resolve
      in the new baseline log.
- [ ] BLE stack init path untouched (`git diff` doesn't touch
      `__ble_init`, sniffer registration, or `buddy_ble_start`).

## 2. Parallelism / subagent boundary

This plan is designed to run inside ONE subagent / worktree dedicated to
M1-A. It is independent of M1-C and does not share files with the
plugin side. Safe to dispatch in parallel with the M1-C plan.

## 3. Exit criteria (matches umbrella plan M1-A EXIT)

1. Spec §4 tests all pass (build, static greps, runtime anchors).
2. `BLE_WIRE_PROTOCOL.md` at v1.1 with gaps A + E closed.
3. New baseline log committed under `docs/protocol/baseline/`.
4. `doc/UI_INTERACTION.md` updated and accurate.
5. Commit/PR links back to the spec and this plan.
