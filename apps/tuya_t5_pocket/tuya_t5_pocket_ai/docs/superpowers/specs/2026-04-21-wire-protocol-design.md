# M0 · Wire Protocol Doc + Baseline Log — Sub-project Design Spec

**Status:** Draft v1
**Date:** 2026-04-21
**Parent:** `../specs/2026-04-21-claude-cli-buddy-t5-design.md` (umbrella spec, §2.2 E, §3.1 M0)
**Scope:** Single milestone — produce the authoritative on-device BLE wire-protocol document and capture the current-HEAD baseline boot log. **No source changes.**

---

## 1. Goal

> Freeze, in a single authoritative document, the BLE Nordic UART wire protocol between the T5AI-Pocket firmware and any peer (Claude Desktop today; Claude Code CLI after M1-C). Record a baseline boot log of the current firmware so every subsequent milestone has a known regression reference.

## 2. In-scope

- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` v1.0
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/<git-short-sha>-boot.log`
- `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/README.md`

## 3. Out of scope

- Any firmware source change (including missing fields like `entries[]`, `evt:turn`, `time` application, `char_*` GIF push — they get logged in the gap table only)
- Any peer-side implementation (Desktop stays as-is; CLI plugin lands in M1-C)
- Amendments to the protocol after v1.0 (those go through a dedicated amendment sub-cycle in later milestones)

## 4. Protocol document outline (section list the writer must cover)

1. **TL;DR** — one paragraph describing what talks to what, plus a direction diagram (desktop/CLI → device and device → desktop/CLI)
2. **Transport**
   - NUS UUIDs (service / RX / TX) verbatim from `REFERENCE.md`
   - Advertising name rule: `Claude_XXXX` derivation — MAC last 2 bytes in hex (from `tal_ble_address_get`, printed as `%02X%02X` of bytes [1][0]); fallback to last 4 chars of `tuya_iot_client_get()->activate.devid` or `->config.uuid`; last resort bare `Claude`. (Matches `__derive_name()` in `buddy_ble.c`.)
   - ADV payload layout (Flags + complete 128-bit UUID in ADV; complete local name in scan response)
   - MTU negotiation (default 23 → peer-negotiated, capped at `BUDDY_BLE_MAX_NOTIFY_CHUNK = 180` on notify)
   - Newline-delimited JSON framing: peer concatenates notifications and splits on `\n`; device accumulates in a 5120-byte ring and dispatches each `\n`-terminated line
3. **Message catalogue (frame-by-frame)**
   For each frame: direction, trigger, required / optional fields, size cap, firmware status.
   - Desktop → device:
     - Heartbeat snapshot (`total`, `running`, `waiting`, `msg`, `entries[]`, `tokens`, `tokens_today`, `prompt{id,tool,hint}`)
     - Turn event (`evt:"turn"`, `role`, `content[]`, serialized JSON ≤ 4 KB)
     - Time sync (`{"time":[epoch, tz_offset_sec]}`)
     - `{"cmd":"owner","name":"..."}`
     - `{"cmd":"unpair"}`
     - Character pack push (`char_begin` / `file` / `chunk` / `file_end` / `char_end`) — B milestone
   - Device → desktop:
     - `{"cmd":"permission","id":"...","decision":"once|deny|always"}` — `id` MUST be bit-for-bit equal to the `prompt.id` the device received
     - `{"cmd":"status"}` (unsolicited poke)
     - `{"ack":"<name>","ok":<bool>,"n":<int>}` for `name|owner|unpair`
     - `{"ack":"status","ok":true,"data":{"name":"Claude_XXXX","sec":false,"sys":{"up":<sec>}}}`
4. **Ordering and liveness**
   - On connect: device waits; desktop/CLI SHOULD send `time` then `owner` then the first heartbeat
   - Heartbeat cadence: keepalive every ~10 s; plus event-driven bursts
   - Liveness: if no inbound frame for ~30 s, peer is presumed dead (device does not act on this today — documented as a gap for future A-sub-project)
5. **Permission correlation**
   - `prompt.id` is an opaque string (currently ≤ 39 bytes after `buddy_data.h`'s `prompt_id[40]`)
   - Device echoes it byte-for-byte in the `permission` reply
   - Any `id` mismatch MUST be discarded on the peer side
6. **Security & hardening notes**
   - No LE Secure Connections in v1.0 (non-goal, per umbrella spec §1.3)
   - JSON parse: line length capped at 5120 bytes; malformed JSON silently dropped with a warn log, buffer flushed at overflow
   - Inbound `cmd` accepted on a whitelist only (`status`, `name`, `owner`, `unpair`, `permission` as desktop-side convenience, plus the char-pack family for B)
   - `buddy_ble_send_cmd` rejects non-ASCII / quote / backslash / ctrl chars to prevent JSON injection
7. **Gap table (REFERENCE.md × current firmware)**
   Columns: `Field | Direction | Firmware status | Target milestone | Notes`.
   Rows (pre-filled, matches Shell scan of `buddy_ble.c`):
   | Field | Dir | Status | Target | Notes |
   |---|---|---|---|---|
   | `entries[]` | in | ❌ not parsed | M1-A (display) | Would feed a scrolling recent-lines list |
   | `evt:"turn"` | in | ❌ ignored | M1-A later | Optional UX — show last assistant reply |
   | `time` array | in | ⚠️ debug-only | M1-A | Apply to device RTC for timestamp rendering |
   | `char_begin/file/chunk/file_end/char_end` | in | ❌ declined | B (M3 only skeleton, real impl later) | GIF pack push path |
   | all other frames above | both | ✅ implemented | — | — |
8. **Amendment procedure**
   - Any change to this document is the prerequisite for corresponding firmware / plugin changes
   - Amendments open a dedicated `brainstorm → writing-plans` sub-cycle under `docs/superpowers/specs/<date>-wire-protocol-amend-<topic>-design.md`
   - The gap table is the one section that may be updated without a full amendment — status flips from `❌` to `✅` happen as sub-projects close

## 5. Baseline log capture

**Command sequence** (executed on the machine that has the T5AI-Pocket attached):

```bash
# A. identify ports
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py pick-port
#    -> FLASH_PORT (e.g. /dev/ttyACM0), MONITOR_PORT (e.g. /dev/ttyACM1)

# B. build current HEAD
cd apps/tuya_t5_pocket/tuya_t5_pocket_ai
tos.py build

# C. flash
tos.py flash -p ${FLASH_PORT}

# D. capture ~30 seconds of boot-to-connect on monitor port
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
        debug-session run -p ${MONITOR_PORT} \
        --log-suffix "baseline_$(git -C <repo> rev-parse --short HEAD)" --hw-reset

# E. locate and copy the resulting log into the protocol tree
cp .target_logging/<latest>.log \
   apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/<short-sha>-boot.log
```

**Contents required inside the baseline log:**

- `buddy_ble init done name=Claude_XXXX` — confirms naming derivation fires
- `buddy_ble advertising as Claude_XXXX` — confirms adv take-over after MQTT up
- At least one `buddy_ble peer connected conn=` or `peer disconnected` transition (optional — hand-trigger from Desktop if possible; otherwise the capture stays at "advertising idle")
- No `Mem Overflow`, `assert`, or `ERROR` lines — if any appear, the baseline is not captured and the run is retried

**README for the baseline directory** lists:

- What the file represents (current-HEAD behaviour at capture date)
- Which lines to grep for regression checks in later milestones
- How to regenerate it on a different branch / commit (same loop above)

## 6. Open questions (none currently blocking)

- Whether to also capture a second baseline after Desktop has connected + exchanged one `owner` + one heartbeat — nice-to-have, not required for v1.0

## 7. Next step

Per brainstorming skill → invoke `superpowers:writing-plans` to produce
`docs/superpowers/plans/2026-04-21-wire-protocol.md` with TDD-style checkboxes, then execute.
