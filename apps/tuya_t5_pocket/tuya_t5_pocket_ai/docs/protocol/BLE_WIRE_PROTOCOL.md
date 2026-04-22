# T5AI-Pocket × Claude — BLE Wire Protocol

| | |
|---|---|
| **Version** | v1.1 |
| **Date** | 2026-04-22 |
| **Status** | Frozen (amendments follow the procedure in §8) |
| **Maintainers** | Firmware sub-project A, CLI plugin sub-project C |
| **Source of truth** | This document. Derived from `apps/tuya_t5_pocket/claude-desktop-buddy/REFERENCE.md` + `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c` |

---

## 1. TL;DR

A T5AI-Pocket running this firmware advertises itself as a Nordic UART Service (NUS) peripheral named `Claude_XXXX`. A central (Claude Desktop on macOS/Windows today, the Claude Code CLI plugin after M1-C) connects, negotiates MTU, and exchanges **newline-delimited UTF-8 JSON** over the NUS RX/TX characteristics. The device surfaces session counts / token counts / one-line status / the currently-pending permission prompt on its 384×168 monochrome panel; the user responds via three physical buttons, which the firmware echoes back as `{"cmd":"permission", ...}`.

```
┌──────────────────────┐                      ┌─────────────────────────────┐
│  Claude Desktop      │ — advertising —→     │  T5AI-Pocket (Claude_XXXX)  │
│  Claude Code plugin  │                      │                             │
│  (BLE central)       │ ← GATT connect ─────→│  BLE peripheral (NUS)        │
│                      │                      │                             │
│   heartbeat  ──→     │   RX char (write)    │                             │
│   turn evt   ──→     │                      │   → UI + button state       │
│   time[]     ──→     │                      │                             │
│   cmd: ...   ──→     │                      │                             │
│                      │                      │                             │
│   ack: ...   ←──     │   TX char (notify)   │                             │
│   cmd: perm  ←──     │                      │                             │
│   cmd: status←──     │                      │                             │
└──────────────────────┘                      └─────────────────────────────┘
```

All transcript fragments and tool hints flow over the link, so operating this over an unencrypted GATT pairing is a *known* security trade-off (see §6). In v1.0 the firmware does **not** enforce LESC; enabling it is tracked in the gap table.

## 2. Transport

### 2.1 GATT service

Nordic UART Service, 128-bit UUIDs, verbatim:

| Role | UUID |
|---|---|
| Service (primary) | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX characteristic (central → device, write / write-without-response) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX characteristic (device → central, notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

The firmware stores the service UUID in little-endian on-wire byte order; see the `BUDDY_NUS_UUID128_SVC` constant in `buddy_ble.c`.

### 2.2 Advertising name and payload

Advertised name pattern: `Claude_XXXX`.

Derivation order (see `__derive_name()` in `buddy_ble.c`, lines ~190–223):

1. Call `tal_ble_address_get(&addr)`. If it succeeds **and** the upper byte `addr.addr[1]` or lower byte `addr.addr[0]` is non-zero, format `Claude_%02X%02X` using `(addr.addr[1], addr.addr[0])`. This gives 2 hex characters per byte → 4 hex characters total, suffix taken from the *low two bytes* of the BLE address in host-visible order.
2. Otherwise, fetch the Tuya IoT client. If `activate.devid` is non-empty use it, else use `config.uuid`. Format `Claude_%s` with the **last four characters** of that string.
3. Last resort: bare `Claude`.

The name buffer is `BUDDY_BLE_NAME_MAX = 20` bytes, including NUL. The peer MUST NOT assume the suffix is hexadecimal — fallback path 2 can leave any printable characters from the device UUID alphabet.

ADV payload (31 B budget):
```
02 01 06                               ; Flags: LE General Discoverable + BR/EDR Not Supported
11 07 <16 bytes NUS service UUID LE>   ; Complete list of 128-bit service UUIDs
```

Scan response (31 B budget):
```
<1+N> 09 <N bytes of s_device_name>    ; Complete Local Name
```

Advertising interval: `adv_interval_min = 0x30`, `adv_interval_max = 0x60` in units of 0.625 ms (i.e. 30–60 ms), connectable undirected (`TAL_BLE_ADV_TYPE_CS_UNDIR`).

The device takes ownership of advertising *after* the Tuya cloud comes up: `buddy_ble_start()` calls `tuya_ble_pair_monitor_disable(TRUE)` to suppress the 30-second Tuya pair timeout and the periodic monitor that would otherwise re-publish the Tuya provisioning name. Peers discovering the device before cloud activation will see the Tuya name first; this is expected and not a protocol bug.

### 2.3 MTU

- Device defaults to `BUDDY_BLE_DEFAULT_MTU = 23`.
- On `TAL_BLE_EVT_MTU_REQUEST` the device adopts the peer-proposed MTU verbatim.
- Notify payload = `MTU - 3`, hard-capped at `BUDDY_BLE_MAX_NOTIFY_CHUNK = 180` bytes for safety. Payloads longer than one chunk are fragmented by the device; central re-assembles by concatenation.

### 2.4 Framing

Every logical message is exactly one `\n`-terminated UTF-8 JSON object.

Device → peer (notify direction):
- The device appends a single `\n` after each JSON object and forwards the bytes into `__send_raw`, which fragments at the chunk size computed from MTU. The central is responsible for concatenating notifications and splitting on `\n`.

Peer → device (write direction):
- The device accumulates incoming bytes in a 5120-byte ring (`BUDDY_BLE_RX_BUF_CAP`).
- On each arrival, it scans for the next `\n`, copies the preceding bytes into a heap-allocated line, and dispatches them into `__handle_line`.
- If the buffer would overflow the 5120-byte cap, the **entire** buffer is discarded (`__rx_accumulate` prints a warn and resets `s_rx_len`). Peers MUST NOT rely on partial-frame delivery after an overflow — they will lose the in-flight frame.
- Malformed JSON (`cJSON_Parse` returns NULL) is silently dropped with a warn log (`buddy_ble bad json`). No error frame is emitted.

## 3. Message catalogue

Legend for the "fw status" column:
- ✅ accepted & applied by the current firmware
- ⚠️ accepted but not applied (logged, no state change)
- ❌ not handled (silently dropped, unless otherwise noted)

### 3.1 Heartbeat snapshot (peer → device)

Trigger: emitted whenever something changes on the desktop/CLI side, plus a keepalive every ~10 s.

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

| Field | JSON type | Bound | fw status | Notes |
|---|---|---|---|---|
| `total` | integer | 0..255 | ✅ (cast to `uint8_t`) | Count of all sessions on the desktop. |
| `running` | integer | 0..255 | ✅ | Sessions actively generating. |
| `waiting` | integer | 0..255 | ✅ | Sessions blocked on a permission prompt. |
| `msg` | string | ≤ 63 bytes after NUL | ✅ | One-line summary; truncated on device via `__copy_str`. |
| `entries[]` | array<string> | see §3.1.1 | ✅ | Parsed into a ring of `BUDDY_ENTRIES_RING` (8) slots × `BUDDY_ENTRY_MAX_CHARS` (79) bytes; rendered on-device. Added in v1.1 (M1-A). |
| `tokens` | number | fits `uint32_t` | ✅ (cast) | Cumulative output tokens since the desktop app started. |
| `tokens_today` | number | fits `uint32_t` | ✅ (cast) | Tokens since local midnight (peer persists). |
| `prompt` | object | see §5 | ✅ | When present, drives the "approve / deny" UI. |
| `prompt.id` | string | ≤ 39 bytes | ✅ | Opaque correlation id; device echoes it byte-for-byte in §3.6. |
| `prompt.tool` | string | ≤ 31 bytes | ✅ | Display-only ("Bash", "Read", …). |
| `prompt.hint` | string | ≤ 63 bytes | ✅ | Display-only argument summary. |

Whole-frame size is only bounded by the 5120-byte RX line cap.

#### 3.1.1 `entries[]` ring semantics (v1.1, M1-A)

Each heartbeat carrying an `entries` array rebuilds the device's running
transcript ring:

- Non-string items are silently skipped (peer MUST still send strings).
- If the array length exceeds `BUDDY_ENTRIES_RING`, only the last
  `BUDDY_ENTRIES_RING` items are retained.
- Each accepted item is bounded-copied at `BUDDY_ENTRY_MAX_CHARS` bytes
  (79 bytes of UTF-8 + trailing NUL) via `snprintf`; longer strings are
  silently truncated. The peer SHOULD therefore keep entries short.
- The ring is cleared *before* refill on every heartbeat that carries
  the `entries` field (even if the array is empty). Stale transcript
  text is therefore never leaked across peers or sessions.
- The firmware does not track per-entry timestamps in v1.1; it prepends
  the device's current `HH:MM` to each visible row on render. Peers MAY
  embed a timestamp prefix inside the string itself.

DEBUG anchors written per stored entry:
`buddy_ble entries: idx=<n> text=<..first 80 chars..>`.

### 3.2 Turn event (peer → device)

```json
{
  "evt": "turn",
  "role": "assistant",
  "content": [{ "type": "text", "text": "..." }]
}
```

Serialized line ≤ 4 KB (peer-enforced; the desktop drops larger events before transmit).

fw status: **❌** (the top-level `evt` key is ignored — the frame simply fails every `cmd`/`time`/heartbeat branch and falls through to `__handle_heartbeat`, which sees no `total`/`running`/… fields and leaves state unchanged). Tracked in §7 for M1-A.

### 3.3 One-shot on connect (peer → device)

```json
{"time":[1775731234, -420]}
```
(example: epoch seconds, PST as `-420` minutes)

| Field | Type | Meaning | fw status |
|---|---|---|---|
| `time[0]` | integer | Unix epoch seconds, UTC | ✅ (v1.1, M1-A) parsed and stored alongside the local `tal_system_get_millisecond()` timestamp at receive; used as a UI-side wall-clock offset to render `HH:MM`. **RTC is not written.** |
| `time[1]` | integer | Timezone offset in **minutes east of UTC**. The v1.1 firmware stores this in an `int16_t` and multiplies by 60 when computing local time. | ✅ (v1.1, M1-A) parsed and applied to UI only. |

> **Unit clarification (v1.1):** v1.0 described `time[1]` as "seconds east of UTC" while noting the value was only logged. v1.1 fixes the unit to **minutes** to match the `int16_t` storage and the derivation formula
> `local = epoch + (now_ms - rx_ms)/1000 + tz_min × 60`.
> Peers MUST send minutes (e.g. `-420` for PST). The DEBUG anchor
> `buddy_ble time sync ok epoch=<lld> tz=<d>` echoes the stored values so log consumers can spot mismatched units during bring-up.

```json
{"cmd":"owner","name":"Felix"}
```

See §3.4 for handling.

### 3.4 Command frames (peer → device)

Exactly one `cmd` field per frame. Device ignores all other top-level keys when `cmd` is present.

| cmd | Extra fields | fw status | Device ack (§3.7) | Notes |
|---|---|---|---|---|
| `status` | — | ✅ | `ack:"status"` with `data{}` | See §3.7.2. |
| `name` | `name: string` (≤ 19 bytes used) | ✅ | `ack:"name", ok:true, n:0` | Overwrites advertised local name on next advertising restart. |
| `owner` | `name: string` (≤ 23 bytes used) | ✅ | `ack:"owner", ok:true, n:0` | Stored in `s_owner_name`, shown in UI. |
| `unpair` | — | ✅ (acked) | `ack:"unpair", ok:true, n:0` | **v1.0 does not actually erase stored BLE bonds** — the ack is cosmetic. Tracked in §7. |
| `char_begin` / `file` / `chunk` / `file_end` / `char_end` | see REFERENCE.md | ❌ (declined by not acking) | — | B sub-project (character pack). Peer should treat silence as "device doesn't accept". |
| anything else | — | ❌ | — | Logged at DEBUG as `unhandled cmd '<x>'`. |

### 3.5 Device → peer: status reply

Emitted in response to `{"cmd":"status"}`:

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

| Field | fw value in v1.0 |
|---|---|
| `data.name` | current `s_device_name` |
| `data.sec` | always `false` (LESC not implemented) |
| `data.sys.up` | `tal_system_get_millisecond() / 1000` |
| `data.bat` | **omitted** in v1.0 |
| `data.stats` | **omitted** in v1.0 |

### 3.6 Device → peer: permission decision

Emitted by the firmware in response to a user button press while `has_prompt` is true.

```json
{"cmd":"permission","id":"req_abc123","decision":"once"}
{"cmd":"permission","id":"req_abc123","decision":"deny"}
{"cmd":"permission","id":"req_abc123","decision":"always"}
```

| Field | Constraint |
|---|---|
| `id` | MUST be byte-for-byte equal to the most recent `prompt.id` the device observed. The firmware never synthesises `id` values. |
| `decision` | One of `"once" | "deny" | "always"`. Peers MAY add values later; unknown decisions SHOULD be treated as `"deny"`. |

On the peer side, any `cmd:"permission"` frame whose `id` does not match a currently-pending request **MUST** be discarded.

### 3.7 Generic ack envelope

All `cmd`s originated by the peer (except `permission`, which flows device → peer) receive an ack:

```json
{"ack":"<same as cmd>","ok":true,"n":0}
```

Failure variant (reserved; v1.0 firmware does not emit):

```json
{"ack":"...","ok":false,"error":"short reason"}
```

`n` is a generic counter — byte count for future chunk acks, otherwise `0`.

## 4. Ordering and liveness

- On `TAL_BLE_EVT_PERIPHERAL_CONNECT`, the device resets UI state (`__reset_state(TRUE)`) and awaits inbound frames. No proactive "hello" is sent.
- Recommended peer bring-up sequence (desktop follows this today; CLI plugin M1-C SHOULD match):
  1. Connect.
  2. `{"time":[...]}`.
  3. `{"cmd":"owner","name":"..."}` — triggers the first `ack:"owner"` from the device.
  4. First heartbeat snapshot.
- Keepalive: a heartbeat frame every ~10 s from the peer; no device-originated keepalive.
- Liveness: if the peer stops receiving heartbeats for ~30 s it SHOULD treat the link as dead. The firmware does **not** currently trigger any action on peer silence — this is intentional for v1.0; M1-A may add a visual "disconnected" state after a silent period.
- On `TAL_BLE_EVT_DISCONNECT` the firmware resets UI state and, if `s_started` is true, republishes advertising.

## 5. Permission correlation

- `prompt.id` is an opaque string carried in the heartbeat's `prompt` object.
- The firmware captures it into `s_state.prompt_id`, a 40-byte field (39 usable + NUL; see `buddy_tama_state_t.prompt_id` in `buddy_data.h`).
- When the user presses a decision button, `buddy_ble_send_permission()` emits `{"cmd":"permission","id":"<s_state.prompt_id>","decision":"..."}` **without any transformation** — byte-for-byte echo.
- If the peer receives a `permission` frame whose `id` does not match a currently-pending prompt, the peer MUST discard it (defence against stale device state after a quick prompt-cancel + prompt-reissue cycle).
- Current firmware guarantees only a single in-flight prompt: new `prompt` fields in a heartbeat overwrite the previous `prompt_id`. There is no per-prompt queue.

## 6. Security and hardening

v1.0 baseline (all intentional, documented as gaps or non-goals):

| Area | Current behaviour | Rationale / amendment owner |
|---|---|---|
| Link encryption | No LE Secure Connections enforcement. Characteristics are not marked encrypted-only. `data.sec` in §3.5 is always `false`. | Non-goal per umbrella spec §1.3. |
| Line length | Inbound lines capped at `BUDDY_BLE_RX_BUF_CAP = 5120` bytes. Overflow discards the **whole** buffer, not just the current frame. | Rationale: the RX buffer is shared; truncating mid-frame would corrupt the *next* frame. |
| JSON parse failure | Silent drop with a `buddy_ble bad json` warn log. No error frame. | Avoids an attacker-controlled amplification vector. |
| `cmd` whitelist | Only `status`, `name`, `owner`, `unpair` are acted on. Unknown `cmd`s log at DEBUG and are declined by not acking. | Per REFERENCE.md, silence = decline. |
| Outbound JSON injection | `buddy_ble_send_cmd()` rejects any `cmd` byte that is `"`, `\\`, or below `0x20`, and caps length at 32 bytes. `buddy_ble_send_permission()` relies on the `prompt_id` being bounded by §3.1. | Central should still validate `prompt_id` server-side before forwarding. |
| Name spoofing | Anyone can advertise `Claude_XXXX` — the peer must still offer a user confirmation step before pairing. | Non-goal; documented for the CLI plugin UX. |
| `unpair` semantics | Ack-only, no bond erase. | Tracked in §7; M1-A task. |
| Folder push (`char_*`) validation | Not implemented on device, so no path-traversal risk yet. | B sub-project will add filename whitelist before accepting `char_begin`. |

## 7. Gap table (REFERENCE.md × current firmware)

| Field / frame | Direction | Firmware status | Target milestone | Notes |
|---|---|---|---|---|
| `entries[]` (heartbeat) | peer → device | ✅ Closed in M1-A (firmware HEAD after 616464c5) | — | Ring of 8 × 79 B; see §3.1.1. |
| `evt: "turn"` | peer → device | ❌ silently dropped | Post-M1-A | Useful for a "last assistant reply" preview. |
| `time` array | peer → device | ✅ Closed in M1-A (firmware HEAD after 616464c5) | — | Parsed and applied as UI-side offset; see §3.3. RTC explicitly not written. |
| `cmd: "unpair"` | peer → device | ⚠️ acked, bonds not erased | Post-M1-A | Add a `tal_ble_bond_erase()` call (API name TBD). |
| `char_begin` / `file` / `chunk` / `file_end` / `char_end` | peer → device | ❌ declined (no ack) | M3 (skeleton) / B sub-project (real) | Path-traversal whitelist required before enabling. |
| `status.data.bat` | device → peer | ❌ omitted | A later sub-project | Requires battery driver plumbing. |
| `status.data.stats` | device → peer | ❌ omitted | A later sub-project | Optional; desktop UI handles absence. |
| `sec = true` (LESC) | device → peer | Always `false` | Out of scope v1.0 | Tracked here for completeness. |
| Liveness timeout reaction | device internal | ❌ no UI change after 30 s silence | Post-M1-A | Currently the UI still shows the last snapshot forever. |
| Heartbeat snapshot (core fields) | peer → device | ✅ | — | `total`, `running`, `waiting`, `msg`, `tokens`, `tokens_today`, `prompt{id,tool,hint}`. |
| `cmd: "status"` + ack | both | ✅ | — | — |
| `cmd: "name"` + ack | peer → device | ✅ | — | — |
| `cmd: "owner"` + ack | peer → device | ✅ | — | — |
| `cmd: "permission"` | device → peer | ✅ | — | Echo contract in §5. |

## 8. Amendment procedure

Any change to this document is the prerequisite for the corresponding firmware / plugin change — not the other way round.

For substantive changes (new frames, new fields, modified semantics):

1. Open a sub-project brainstorm under `docs/superpowers/specs/<date>-wire-protocol-amend-<topic>-design.md`.
2. Run `superpowers:writing-plans` to produce a matching plan in `docs/superpowers/plans/`.
3. Update this document to v1.x, bumping the `Version` field in the front-matter and appending a dated changelog entry at the bottom.
4. Only then modify firmware and/or plugin code; the protocol doc update lands **in the same commit or an earlier commit** in the merge series.

For non-substantive changes (gap table status flips from ❌/⚠️ to ✅, or vice versa):

- Allowed without a full amendment cycle.
- Still update the version to v1.x.y and add a one-line changelog entry.

## Changelog

- **v1.1 · 2026-04-22** — M1-A amendments. Heartbeat `entries[]` array is
  now parsed into a device-side ring (§3.1.1) and rendered in the UI's
  scroll panel. `{"time":[...]}` is now applied as a UI-side wall-clock
  offset (the TuyaOS RTC is still not written); the `time[1]` unit is
  clarified as minutes east of UTC (§3.3). §7 gap rows A (`entries[]`)
  and E (`time`) flipped to ✅. No on-wire changes versus v1.0 — v1.1 is
  purely a consumer/semantic clarification, so every v1.0-compatible peer
  keeps working against v1.1 firmware.
- **v1.0 · 2026-04-21** — Initial freeze. Captures the protocol implemented on commit `616464c5` (working tree). Gap table reflects the fields and frames not yet wired through `buddy_ble.c`.
