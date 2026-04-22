# Claude CLI Buddy on T5AI-Pocket — Umbrella Design Spec

**Status:** Draft v1 (awaiting user review)
**Date:** 2026-04-21
**Scope:** Project-level roadmap and sub-system interface contracts. **No implementation steps at this layer.** Each sub-system (A/B/C/D/E) will get its own `brainstorm → plan → implementation` cycle separately.
**Source of requirements:** `apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/dev.md`
**Reference projects:**
- `apps/tuya_t5_pocket/claude-desktop-buddy/` (local clone of `anthropics/claude-desktop-buddy`)
- `op7418/m5-paper-buddy` (Claude Code plugin layout reference)

---

## 1. Goal, Scope, Constraints

### 1.1 Goal (one sentence)

> Build a hardware "Claude buddy" on T5AI-Pocket that works with **both Claude Desktop and Claude Code CLI** over the same BLE Nordic UART protocol: the device renders session / token / permission state in a black-white eink-style 384×168 LVGL UI with 18 ASCII personas, routes physical keys directly to permission approval, and ships a Claude Code plugin that auto-connects the CLI to the device on launch.

### 1.2 In-scope (umbrella level)

| ID | Sub-system | Scope this umbrella |
|----|------------|---------------------|
| **A** | Firmware UI & interaction | Port all 18 ASCII personas, BLE status gating, key-driven permission, LED feedback |
| **B** | Custom GIF persona channel | **Skeleton only** — interface, flash/KV naming, placeholder renderer. No runtime decode. |
| **C** | Claude Code CLI plugin | Windows-first plugin (manifest + slash commands + hooks + Python `bleak` daemon) |
| **D** | PC-side tooling | GIF pack pre-processor + flasher (BLE / USB-serial). **Freeze CLI + IO contracts only**, no code this round. |
| **E** | Unified wire protocol doc | One authoritative document covering Desktop + CLI endpoints |

### 1.3 Out of scope (explicit non-goals)

- GIF runtime decode / frame scheduling
- Firmware OTA
- macOS / Linux CLI plugin (extension point reserved, not delivered)
- LE Secure Connections pairing (continue the existing no-SC choice)
- Offline / no-BLE operating mode
- Stats / level / XP system from the original `claude-desktop-buddy` (the simplified UI already dropped these, confirmed)

### 1.4 Constraints and preconditions

| Category | Constraint |
|----------|-----------|
| Hardware | T5AI-Pocket, physical panel 384×168. UI designed **as if the panel were a mono eink** — use only black/white + dither greys in the LVGL theme, no colour. |
| Framework | LVGL v9, TuyaOpen SDK, NimBLE host via TKL/TAL BLE |
| Existing state (baked in) | Static co-registration of NUS GATT is live; `buddy_ble` bridge, simplified text `buddy_main_screen`, and JSON line protocol with Claude Desktop all work. |
| Protocol | 100 % reuse of the Claude Desktop Buddy NUS protocol (`claude-desktop-buddy/REFERENCE.md`). No proprietary extensions without a written amendment in `docs/protocol/BLE_WIRE_PROTOCOL.md`. |
| Security | No plaintext logging of keys / MAC / credentials. JSON parse is length-capped and field-whitelisted. `cmd` accepts a whitelist only. Follows TuyaOS C Security rules (input validation, buffer bounds, no magic strings in logs). |
| Delivery shape | **This umbrella round delivers only this spec + the matching umbrella plan.** No source changes and no protocol doc yet. Each sub-system below describes what will be produced at its own milestone, in its own `brainstorm → plan → implementation` cycle. |
| Verification toolchain | All firmware-touching milestones verify on real T5AI-Pocket hardware via `tos.py build` → `tos.py flash -p <FLASH_PORT>` → `agent_target_tool.py debug-session run -p <MONITOR_PORT> --hw-reset` (T5AI monitor baud = 460800, dual-serial layout per `tuyaopen-flash-monitor` skill). Evidence = a `.target_logging/*.log` file committed or pinned into each sub-project's plan. |

---

## 2. Architecture & sub-system contracts

### 2.1 System diagram

```
PC (Windows first)
  Claude Code CLI ──┐  hooks (exec)
                    ▼
   ┌─────────────────────────────────────────────────────────┐
   │ Plugin: claude-cli-buddy-t5                              │
   │   plugin.json                                            │
   │   commands/  (/buddy-install, /buddy-start, ...)         │
   │   scripts/   install.{sh,ps1}, buddy_daemon.py, common.py│
   │   settings/hooks.json   (merged into ~/.claude/settings) │
   └───────────────────────────┬─────────────────────────────┘
                               │  local HTTP / pipe
                               ▼
           ┌─────────────────────────────────────────┐
           │  buddy_daemon.py  (persistent)          │
           │   - session tracker / heartbeat         │
           │   - BLE NUS central (bleak)             │
           └───────────────┬─────────────────────────┘
                           │
                [also valid] Claude Desktop (unchanged path)
                           │
                           │ BLE NUS (newline-delimited JSON)
                           ▼
─────────────────────────────────────────────────────────────
T5AI-Pocket firmware
   buddy_ble (bridge)        buddy_storage (B · skeleton)
       │                          │
       ▼                          ▼
   buddy_tama_state_t    persona_id (ASCII | GIF)
       │                          │
       └────────►  buddy_main_screen (A) ◄─── persona_registry
                       │                       │
             key events│                 ascii_persona (18)
                       │                 buddy_gif_stub  (B placeholder)
                       ▼
                  LVGL canvas 384×168 b/w
                       │
                  tdl_led (LED feedback)
```

### 2.2 Per-sub-system contract

#### A · Firmware UI & interaction

- **Responsibility:** Convert `buddy_tama_state_t` snapshots into a 384×168 black/white LVGL frame. Translate key events into BLE commands via `buddy_ble_send_permission()` / `buddy_ble_send_cmd()`. Drive the 18-persona registry. Fan state changes out to `tdl_led`.
- **Public entry points (to be formalized in the A sub-project spec):**
  - `buddy_main_screen_update_state(const buddy_tama_state_t *s)`
  - `buddy_main_screen_set_persona(persona_id_t id)`
  - `persona_registry_first() / _next() / _by_id()`
- **Dependencies:** `buddy_ble` (downlink state, uplink commands), `persona_registry` (ASCII enumeration), `tdl_led`.
- **Does not do:** protocol parsing, BLE transport, GIF decode.

#### B · Custom GIF persona channel (skeleton only, delivered by the B sub-project)

- **Final deliverable of the B sub-project (not this umbrella round):**
  - File stubs: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_gif_stub.{c,h}`
  - A placeholder rendering path that shows "GIF persona #N — runtime not installed" when `persona_registry` encounters a `GIF` entry
  - A documented Flash partition name + KV key-naming scheme (as part of the B sub-project's plan)
- **Frozen abstraction (umbrella-level):**
  ```c
  typedef enum { PERSONA_BACKEND_ASCII, PERSONA_BACKEND_GIF } persona_backend_e;

  typedef struct persona_backend_s {
      persona_backend_e type;
      const char       *id;              /* "owl", "dragon", "custom_bufo", ... */
      OPERATE_RET (*draw_state)(lv_obj_t *parent, buddy_persona_state_e st);
  } persona_backend_t;
  ```
  `ascii_persona` (A) and `buddy_gif_stub` (B) both implement this shape.
- **Does not do:** real decode, real BLE/serial transport, real flash writes. All of that lives in a future B sub-project.

#### C · Claude Code CLI plugin

Layout mirrors m5-paper-buddy/plugin:

```
apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/
  plugin.json                    # Claude Code plugin manifest
  README.md                      # install / daily use / uninstall
  commands/
    buddy-install.md
    buddy-start.md
    buddy-stop.md
    buddy-status.md
    buddy-flash.md
  scripts/
    install.ps1                  # Windows installer
    install.sh                   # cross-platform fallback (mac/linux placeholder)
    buddy_daemon.py              # persistent BLE NUS client (bleak), local HTTP
    common.py                    # config path, logging, PID file
  settings/hooks.json            # merged into ~/.claude/settings.json
```

- **Input to CLI:** Claude Code hooks (`PreToolUse`, `UserPromptSubmit`, `Stop`) call local HTTP of `buddy_daemon`, forward permission prompts, relay tool names / hints.
- **Input to device:** daemon holds a persistent BLE connection, sends `{ "total", "running", "waiting", "msg", "tokens", "tokens_today", "prompt"? }` frames per `REFERENCE.md`. On connect, sends `{ "time": [...] }` + `{ "cmd": "owner", "name": "..." }` in order.
- **Output from device:** `{ "cmd": "permission", "id": "...", "decision": "once|deny|always" }` gets translated back to the hook's stdout per Claude Code hook contract (`{"permissionDecision": "allow"}` or equivalent).
- **State directory:** `~/.claude-buddy-t5/` (pid file, daemon log, cached device MAC).
- **Fallback:** when daemon is not running, hooks emit `{}` so CLI keeps working.
- **Does not do:** extend the protocol, modify firmware, handle macOS/Linux.

#### D · PC-side tools (interface freeze only)

This round only locks:

- `tools/prep_gif_pack.py`
  - Input directory: `manifest.json` + `<state>.gif` (states: `sleep / idle / busy / attention / celebrate / dizzy / heart`)
  - Output: one binary pack file with a fixed header + per-state frame index + pixel blobs (96 × N, 1-bit)
  - Frozen contract: CLI args, input schema, output binary header layout
- `tools/flash_character.py`
  - Input: pack file from `prep_gif_pack.py`
  - Args: `--transport {ble,serial}`, `--port`, `--device`
  - Frozen contract: CLI args, CRC + chunking wire format (spec doc-level, not code)

All actual Python code is deferred to the B sub-project.

#### E · Unified wire protocol document

- **File:** `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md`
- **Required contents:**
  1. NUS UUIDs (service / RX / TX)
  2. Advertising name rule `Claude_XXXX` with **canonical source of XXXX** (MAC last 4 hex → fallback to devid last 4 chars; documented as the single truth)
  3. JSON line protocol — every field with direction, optionality, size limit, and purpose
  4. Connection handshake ordering (first `time`, then `owner`, then heartbeats)
  5. Permission request / response correlation (device MUST echo the exact `id`, string equality)
  6. Heartbeat cadence and 30 s liveness timeout
  7. MTU negotiation and line reassembly requirements
  8. "Current firmware vs REFERENCE.md" gap table (fields not yet honored by firmware, with a target milestone)
  9. Amendment procedure: any change to this doc is the precondition for corresponding firmware / plugin changes
- **Authority:** this doc is the single source of truth for A, B, C, D. It supersedes `claude-desktop-buddy/REFERENCE.md` wherever they differ.

---

## 3. Roadmap, risk register, test contracts

### 3.1 Milestones (approach C — protocol spike + two parallel tracks)

```
M0  Protocol freeze (single-track, ~1 week)
  - Write docs/protocol/BLE_WIRE_PROTOCOL.md
  - Audit every REFERENCE.md field; mark "implemented" vs "not yet"
  - Capture a baseline firmware log using the canonical verification loop
    (build current HEAD -> flash -> debug-session run --hw-reset) and
    store it under docs/protocol/baseline/ as regression reference
  - Gap table feeds into M1-A
  EXIT: doc v1.0 merged, reviewed, baseline log archived

M1-A Firmware UI (parallel with M1-C)       M1-C CLI plugin (parallel with M1-A)
  - ascii_persona (18 species)                - plugin.json + slash commands
  - persona_registry + switching              - buddy_daemon.py (bleak)
  - BLE status gating, LED feedback           - hooks -> daemon -> BLE
  - rework buddy_main_screen to consume       - Windows end-to-end path
  DEMO: runs against Claude Desktop           DEMO: CLI prompt drives device frame
  └──────────── integration checkpoint (M1-A × M1-C) ────────────┘

M2  Tool interface freeze (D)
  - prep_gif_pack.py CLI / IO contract
  - flash_character.py CLI / transport contract
  - pack binary format: header + state index
  EXIT: contract accepted in spec

M3  GIF UI skeleton (B)
  - buddy_gif_stub.{c,h} placeholder renderer
  - persona_registry GIF branch
  - flash partition + KV naming finalized
  EXIT: B sub-project can start brainstorming on its own spec

M4  macOS / Linux adaptation (RESERVED, not delivered this umbrella)
```

Notes:
- **M0 is a hard prerequisite.** Neither M1-A nor M1-C begins until the protocol doc is frozen — prevents contract drift between the two parallel tracks.
- **M1-A and M1-C are independent demos.** A works against the existing Desktop integration. C can target any running firmware.
- **Integration is a checkpoint, not a milestone.** Failure rolls back to either M0 (doc) or M1 (impl).
- **M2 and M3 ship interfaces only this umbrella.** Real code lands in the individual sub-project plans.

### 3.2 Risk register

| # | Risk | Impact | Mitigation |
|---|------|--------|-----------|
| R1 | REFERENCE.md has fields the firmware does not implement yet (e.g. `time`, `turn`) | M0 doc incomplete; M1-A/C diverge on semantics | M0 must include the "current firmware vs doc" gap table; missing items routed to M1-A or deferred |
| R2 | Claude Code CLI plugin / hook mechanism differs from Desktop | M1-C stalls | Follow m5-paper-buddy layout verbatim — do not invent |
| R3 | Windows BLE stack (bleak + WinRT) behaves differently from Linux | C Windows demo flaky | Validate early with bleak Windows sample; keep no-SC pairing |
| R4 | Some of the 18 ASCII personas render wider than half the 384 px width (original screens were 240×135) | Layout breakage | Spec constrains persona bounding box to ≤ 192×140 px; oversize ones are cropped / compressed in M1-A |
| R5 | Physical panel is colour, we render mono — under-utilised contrast | Purely cosmetic | Keep mono LVGL theme; only a theme swap is needed later if user ever wants colour |
| R6 | GIF skeleton placeholder becomes throwaway when real runtime lands | Rework in the B round | Freeze `persona_backend_t` now so ASCII and GIF implementations share the interface |
| R7 | Permission reply `id` mismatch slips through | Safety bug — wrong approval routed | M0 marks the `id` echo as MUST with exact string equality; M1-A plan must include a unit-test-style sanity check |
| R8 | Advertising name `Claude_XXXX` differs between runs (MAC vs devid) | CLI cannot find device | M0 defines the single derivation rule and encodes it in the gap table |

### 3.3 Test contracts (what each sub-system must prove at its own completion)

All firmware verification is done on real hardware using the `tuyaopen-flash-monitor` workflow. No host-side emulation replaces an on-device run.

**Canonical firmware verification loop** (referenced by every A / B test below)

```
# 0. (once per machine) serial permission
sudo usermod -aG dialout $USER && reboot

# 1. Identify T5 dual-port layout using agent_target_tool
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
        list-devices                     # confirm VID 0x1a86 / PID 0x55d2
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
        pick-port                        # -> FLASH_PORT, MONITOR_PORT

# 2. Build inside the app directory
cd apps/tuya_t5_pocket/tuya_t5_pocket_ai
tos.py build                             # must end with "BUILD SUCCESS"

# 3. Flash on the flash port
tos.py flash -p ${FLASH_PORT}            # typically /dev/ttyACM0

# 4. Capture a fresh boot log on the monitor port
#    T5AI monitor baud is 460800, auto-selected by tos.py monitor
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
        debug-session run -p ${MONITOR_PORT} \
        --log-suffix "<subsystem>_<milestone>" --hw-reset

# 5. Inspect captured log
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
        logs latest
```

Every A / B / umbrella E2E test below **must capture a `.target_logging/<...>.log` file as the evidence artefact**. Claims like "works on device" without a pinned log file are not acceptance.

| Sub-system | Test contract (to be honored in that sub-project's plan) |
|------------|---------------------------------------------------------|
| **A UI** | (1) Static JSON fixtures (shipped in test/fixtures) reproduce deterministic UI frames on device, verified by boot-log probes printed from `buddy_main_screen`. (2) All 18 personas render and switch without crashes — flash once, capture a 60-second log that walks every persona via a test CLI or a BLE fixture. (3) 30-minute soak against Claude Desktop — live log captured with `debug-session run`; free-heap value at start vs end must not drift beyond a documented threshold. (4) LED reflects pending-approval and connection states per the spec LED-table — verified by a log line per LED change plus user-visible confirmation. |
| **B skeleton** | (1) `persona_backend_t` host-side unit test. (2) On the device, switching to a GIF-type persona must log `"[gif_stub] placeholder rendered, id=..."` and produce no asserts — capture the boot-and-switch log. |
| **C plugin** | (1) `/buddy-install` then `/buddy-start` on a clean Windows box — one-shot success. (2) Daemon restart is idempotent. (3) CLI keeps working when daemon is absent (hooks emit `{}`). (4) 30-minute heartbeat against a real flashed device (firmware built via the loop above) — no disconnect; device-side log captured simultaneously. |
| **D tools** | (1) `prep_gif_pack` on a fixture directory produces a pack whose SHA is stable. (2) `flash_character` over both BLE and serial writes the pack end-to-end to a real device with CRC validation; serial path is verified by `tos.py monitor -p ${MONITOR_PORT} -b 460800` log output. |
| **E protocol** | Field-by-field diff vs `REFERENCE.md` — zero gap left without a gap-table entry. Any new field means doc first, implementation second. Device-side conformance to the doc is verified by the A / B / C / D loops above. |

**Umbrella-level end-to-end smoke** (single mandatory acceptance run — no substitute)

> On a clean Windows machine with the T5AI-Pocket attached:
> 1. Build and flash the firmware using the canonical loop above.
> 2. Start a `debug-session run --hw-reset` on the monitor port to collect a live log.
> 3. Install the plugin: `/buddy-install`, then `/buddy-start`.
> 4. Launch Claude Code CLI and run a command that triggers `PreToolUse`.
> 5. Device must show the PERMISSION REQUEST card within 1 s.
> 6. User presses ENTER on the device.
> 7. CLI must continue with `allow`. Round-trip under 10 s.
>
> Evidence required to close the umbrella: **three artefacts** — firmware boot log (`.target_logging/*_umbrella_e2e.log`), daemon log, and a short screen recording of the device UI transition. Absence of any of the three = test did not happen.

**Regressions of previously-verified behaviour:** every sub-project's plan must include a "rerun the canonical loop and diff against a baseline log" step before closing the milestone. The baseline log for the current firmware state is captured **during M0** (empty snapshot of current `buddy_main_screen` behaviour) and stored alongside the protocol doc.

---

## 4. Open questions (none currently blocking)

- Exact LED behaviour table (colour / pattern per state) — left for M1-A sub-project spec
- Whether to vendor the Chinese/Japanese TTF from m5-paper-buddy for CJK support — **defer**, current UI is ASCII-only
- Whether `/buddy-flash` should support firmware flash on top of character flash — **defer** to M2+

## 5. Next step

Per brainstorming skill, once this spec is approved:
→ Invoke the `writing-plans` skill to emit a roadmap-level plan at
`apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/superpowers/plans/2026-04-21-claude-cli-buddy-t5.md`.
That plan lists only the M0..M3 deliverables and their exit criteria — detailed TDD steps happen in each sub-project's own plan.
