# Claude CLI Buddy on T5AI-Pocket — Umbrella Roadmap Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to orchestrate **across** milestones. **Inside each milestone**, a fresh `brainstorm → writing-plans → executing-plans` cycle is required per sub-project — TDD-level task steps live in those sub-plans, not here. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a T5AI-Pocket firmware + Claude Code CLI plugin pair that renders Claude session/permission state on a mono 384×168 UI and routes physical keys back to the CLI — protocol-compatible with Claude Desktop.

**Architecture:** Five loosely-coupled sub-systems (A firmware UI, B GIF skeleton, C CLI plugin, D PC tools, E wire-protocol doc) sequenced as a protocol-freeze spike (M0) followed by two parallel tracks (M1-A firmware, M1-C plugin), then interface-freeze only for tools (M2) and GIF skeleton (M3). macOS/Linux adaptation (M4) reserved for later.

**Tech Stack:** LVGL v9, TuyaOpen SDK (NimBLE), TKL/TAL BLE on device · Python 3 + `bleak` daemon, Node-free Claude Code plugin manifest on host · `tos.py build/flash/monitor` + `agent_target_tool.py debug-session run` for on-device verification.

**Spec:** `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/superpowers/specs/2026-04-21-claude-cli-buddy-t5-design.md`

---

## Scope reminder (from spec §1.4)

This umbrella plan is **roadmap-level only**. It sequences milestones, locks their exit criteria, and points at where each sub-project's detailed plan will live. It deliberately does **not** contain TDD task steps — those are written per sub-project during M0 / M1 / M2 / M3 in a dedicated `brainstorm → writing-plans` cycle.

## Canonical firmware verification loop

Every firmware-touching milestone (M0 baseline, M1-A, M3) closes with this exact sequence and pins the resulting `.target_logging/*.log` as evidence:

```bash
# 1. identify dual-serial layout
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py pick-port
#   -> FLASH_PORT (typical /dev/ttyACM0), MONITOR_PORT (typical /dev/ttyACM1)

# 2. build
cd apps/tuya_t5_pocket/tuya_t5_pocket_ai
tos.py build                              # must end with "BUILD SUCCESS"

# 3. flash
tos.py flash -p ${FLASH_PORT}

# 4. capture boot log with hw-reset (T5AI monitor baud 460800)
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
        debug-session run -p ${MONITOR_PORT} \
        --log-suffix "<milestone>_<yyyymmdd>" --hw-reset

# 5. inspect
python3 .agents/skills/agent-hardware-debug-helper-tools/agent_target_tool.py logs latest
```

Monitor baud for T5AI is **460800** per the `tuyaopen-flash-monitor` skill. The C / D hosts verify against real hardware flashed by this same loop; no emulation substitutes.

---

## Milestones

The planning unit here is the **milestone**, not the code step. Each milestone below owns:

- a deliverable set (spec / plan / code / doc)
- an exit criteria checklist (objective, verifiable)
- a verification action (log capture or doc review)
- a handoff to a sub-project cycle when implementation is involved

---

### Task M0: Protocol freeze + baseline log

**Owner:** single track, one author (the protocol doc must not race the parallel tracks).

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/current-head-<yyyymmdd>.log`
- Reference: `apps/tuya_t5_pocket/claude-desktop-buddy/REFERENCE.md`
- Reference: current firmware — `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.{c,h}`

- [ ] **Step 1: Open an M0 sub-project cycle**
  - Invoke `superpowers:brainstorming` with topic "wire protocol doc + baseline log capture"
  - Required output: `docs/superpowers/specs/<date>-wire-protocol-design.md`

- [ ] **Step 2: Invoke `superpowers:writing-plans`** for the M0 sub-project
  - Required output: `docs/superpowers/plans/<date>-wire-protocol.md` with TDD-style checkboxes for every section of the protocol doc (UUIDs, advertising name derivation, field-by-field table, ordering, `id` echo rule, MTU, liveness, amendment procedure, gap table vs REFERENCE.md).

- [ ] **Step 3: Execute the M0 plan inline or via subagent**
  - Use `superpowers:executing-plans` or `superpowers:subagent-driven-development`.
  - One of the steps inside that plan MUST run the canonical verification loop against current HEAD and archive the log to `docs/protocol/baseline/`.

**Exit criteria (check all):**

- [ ] `docs/protocol/BLE_WIRE_PROTOCOL.md` v1.0 exists and is committed.
- [ ] Every field in REFERENCE.md is either marked "implemented" or appears in the gap table with an owning milestone.
- [ ] The advertising-name derivation rule is unambiguous and matches current firmware behaviour (MAC last 4 hex → fallback to devid last 4).
- [ ] The permission `id` echo rule is explicitly marked MUST.
- [ ] Baseline log archived under `docs/protocol/baseline/` with a filename tied to current HEAD commit.
- [ ] At least one second reviewer has signed off (inline comment or review request).

**Verification action:**
- [ ] Diff `docs/protocol/BLE_WIRE_PROTOCOL.md` vs `claude-desktop-buddy/REFERENCE.md` — every divergence has a row in the gap table.

**Commit message template:** `docs(protocol): freeze BLE wire protocol v1.0 + baseline log`

---

### Task M1-A: Firmware UI + 18 ASCII personas (parallel with M1-C)

**Owner:** firmware track.

**Files (final expected, produced by the M1-A sub-project):**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/ascii_persona.{c,h}`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/persona_registry.{c,h}`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_led.{c,h}` (wraps `tdl_led`)
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_main_screen.{c,h}` — consume persona_registry + mono theme
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_data.h` — add `persona_id` field if needed (protocol-driven only)
- Reference: `apps/tuya_t5_pocket/claude-desktop-buddy/src/buddies/*.cpp` (18 source personas)
- Reference: `src/peripherals/led/tdl_led/include/tdl_led_manage.h`
- Reference: M0's `BLE_WIRE_PROTOCOL.md`

- [ ] **Step 1: Open an M1-A sub-project cycle**
  - Invoke `superpowers:brainstorming` with topic "firmware UI — 18 ASCII personas on 384×168 mono".
  - Required output: design spec that decides per-persona bounding box (≤ 192×140 from risk R4), mono font strategy, LED state table, demo/test CLI for walking personas.

- [ ] **Step 2: Invoke `superpowers:writing-plans`** for M1-A.
  - Required output: TDD-style plan with one task per persona port + integration tasks for `persona_registry`, `buddy_main_screen`, `buddy_led`.

- [ ] **Step 3: Execute the M1-A plan.**
  - Every task closes with the canonical verification loop; every log is captured with `--log-suffix "m1a_<component>"`.

**Exit criteria (check all):**

- [ ] All 18 personas from `claude-desktop-buddy/src/buddies/` have a renderer in `ascii_persona.c` and at least one on-device frame per persona captured in logs.
- [ ] `buddy_main_screen` consumes `persona_registry_by_id()` — no string-literal persona references remain.
- [ ] LED state table implemented; each LED state change logs a line that a reviewer can grep.
- [ ] 30-minute live soak against Claude Desktop — free-heap at start vs end drift below a documented threshold; log archived.
- [ ] Permission flow end-to-end still works (ENTER / RIGHT / UP decisions delivered with correct `id` echo).
- [ ] No regression vs M0 baseline log — a dedicated "diff vs baseline" step must appear in the sub-plan and pass.

**Verification action:**
- [ ] Run the canonical loop and pin the log at `docs/verification/m1a-final-<yyyymmdd>.log`.

**Commit message template (per sub-task):** `feat(buddy): port ascii persona <name>` / `feat(buddy): wire persona_registry` etc.

---

### Task M1-C: Claude Code CLI plugin + BLE daemon (parallel with M1-A)

**Owner:** host-tooling track.

**Files (final expected, produced by the M1-C sub-project):**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/plugin.json`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/README.md`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/commands/{buddy-install,buddy-start,buddy-stop,buddy-status,buddy-flash}.md`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/scripts/install.ps1` (Windows)
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/scripts/install.sh` (mac/linux placeholder — warns and exits gracefully)
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/scripts/buddy_daemon.py` (bleak BLE central + local HTTP)
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/scripts/common.py`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/plugin/settings/hooks.json`
- Reference: `op7418/m5-paper-buddy` plugin/ tree (layout model)
- Reference: M0's `BLE_WIRE_PROTOCOL.md`
- Reference: `apps/tuya_t5_pocket/claude-desktop-buddy/src/` (desktop-side implementation for cross-check)

- [ ] **Step 1: Open an M1-C sub-project cycle**
  - Invoke `superpowers:brainstorming` with topic "Claude Code plugin layout + BLE daemon (Windows first)".
  - Required output: design spec that covers plugin manifest fields, slash command contracts, hook JSON merge strategy, daemon lifecycle (start/stop/pid), local HTTP API shape, BLE reconnect policy, Windows-only deliverable scope with macOS/Linux placeholder behaviour.

- [ ] **Step 2: Invoke `superpowers:writing-plans`** for M1-C.
  - Required output: TDD-style plan — unit tests for JSON framing on both sides, integration test against a real flashed M1-A-or-newer device, Windows clean-box install test.

- [ ] **Step 3: Execute the M1-C plan.**
  - Every BLE-integration test flashes the current `master` firmware using the canonical verification loop before running daemon tests, and archives both the daemon log and device log.

**Exit criteria (check all):**

- [ ] `/buddy-install` → `/buddy-start` on a clean Windows 10/11 machine succeeds in one shot (video or transcript pinned).
- [ ] `buddy_daemon.py` restart is idempotent (pid file + graceful stop tested).
- [ ] When daemon is not running, installed Claude Code hooks emit `{}` and do not block CLI operation — verified by a test that removes the daemon and runs a `PreToolUse`-triggering command.
- [ ] 30-minute heartbeat against a real device — zero disconnects in daemon log; device side shows no heartbeat stall via its own captured log.
- [ ] Every outgoing frame matches the exact schema in `BLE_WIRE_PROTOCOL.md`. Permission responses echo `id` bit-for-bit.
- [ ] macOS/Linux installer exits with a human-readable "not yet supported" message — does not pretend to install.

**Verification action:**
- [ ] Pin the 30-minute soak daemon log + device log under `docs/verification/m1c-final-<yyyymmdd>/`.

**Commit message template (per sub-task):** `feat(plugin): add buddy_daemon BLE central` / `feat(plugin): wire PreToolUse hook` etc.

---

### Task M1 × Integration checkpoint

**Trigger:** M1-A exit criteria met AND M1-C exit criteria met.

**Files:**
- Create: `docs/verification/umbrella-e2e-<yyyymmdd>.log` (firmware side)
- Create: `docs/verification/umbrella-e2e-<yyyymmdd>-daemon.log`
- Create: `docs/verification/umbrella-e2e-<yyyymmdd>-ui.mp4` (screen / phone recording)

- [ ] **Step 1: Fresh Windows host setup**
  - Clean machine (or clean user profile). Install prereqs per the plugin README. Do not skip any step.

- [ ] **Step 2: Flash current firmware HEAD** using the canonical verification loop with `--log-suffix "umbrella_e2e"` and keep the session running.

- [ ] **Step 3: Install the plugin**
  - `/buddy-install` → `/buddy-start`. Confirm device advertising name matches `Claude_XXXX` derivation from M0 doc.

- [ ] **Step 4: Run a PreToolUse-triggering CLI command**
  - Example: `claude-code "run: rm ./tmp/*"` or equivalent that triggers the Bash PreToolUse hook per current Claude Code hook docs. The exact command is chosen at execution time from the current Claude Code docs — **do not hardcode a command that might be renamed upstream**.

- [ ] **Step 5: Expect and verify the full round trip within 10 s**
  - Device shows PERMISSION REQUEST within 1 s.
  - User presses ENTER on device.
  - CLI receives `allow` and continues.
  - Daemon log shows one request in, one decision out.
  - Firmware log shows one matching `permission` frame with echoed `id`.

**Exit criteria (check all):**

- [ ] All three evidence artefacts exist and are committed.
- [ ] No `ERROR` / `ASSERT` / `Mem Overflow` lines anywhere in either log.
- [ ] Round-trip time measured by correlating timestamps between daemon log and firmware log is < 10 s.

**On failure:** roll back to M0 (if the discrepancy is contract-level) or to M1-A / M1-C (if it is implementation-level). Do **not** "patch" by modifying the protocol doc without going back through M0's review.

---

### Task M2: PC-side tool interface freeze

**Owner:** documentation-only (no Python code this round).

**Files:**
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` — append an annex "Character pack transport (serial + BLE)" with CRC + chunking wire format.
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/tools/README.md` describing the future scripts `prep_gif_pack.py` and `flash_character.py` — CLI args, input schema, pack binary header layout.

- [ ] **Step 1: Open an M2 sub-project cycle**
  - Invoke `superpowers:brainstorming` for "GIF pack format + flasher CLI contracts".
  - Required spec sections: pack file header (magic, version, state count, offset table), per-state frame table (width × height, frame count, duration[], pixel-plane offset, pixel format = 1-bit or 4-bit dither), CLI arg vocabulary for both scripts, transport-level chunking / CRC for BLE + serial.

- [ ] **Step 2: Invoke `superpowers:writing-plans`** for M2.
  - Required output: TDD-style plan whose tasks are all documentation-writing tasks (no Python this round per spec §1.2 D).

- [ ] **Step 3: Execute the M2 plan.**

**Exit criteria (check all):**

- [ ] `tools/README.md` describes `prep_gif_pack.py` and `flash_character.py` in enough detail that a future engineer can start implementation without a follow-up brainstorm.
- [ ] Pack header is byte-for-byte specified (offsets, endianness, size of every field).
- [ ] Both transports have an explicit framing: start marker, length prefix, payload, CRC32 (or equivalent) — documented in the protocol annex.

**Verification action:**
- [ ] Spec coverage check against spec §2.2 D: every bullet in D has at least one paragraph in `tools/README.md`.

**Commit message:** `docs(tools): freeze GIF pack format and flasher CLI contracts`

---

### Task M3: GIF persona UI skeleton

**Owner:** firmware track (re-enters after M1-A is closed).

**Files (final expected, produced by the M3 sub-project):**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_gif_stub.{c,h}`
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/persona_registry.{c,h}` — add GIF-typed entries and dispatch to the stub
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` — finalize the Flash partition name and KV key-naming scheme for character packs (documentation of the allocation; actual writing code is future work).

- [ ] **Step 1: Open an M3 sub-project cycle**
  - Invoke `superpowers:brainstorming` for "GIF persona UI skeleton — placeholder without runtime".
  - Required spec: behaviour when `persona_registry` picks a GIF entry but no pack is installed (show the placeholder, log one `[gif_stub] placeholder rendered, id=...` line).

- [ ] **Step 2: Invoke `superpowers:writing-plans`** for M3.
  - Required output: TDD-style plan including (a) host-side unit test for `persona_backend_t` abstraction, (b) on-device test that forces the registry to resolve a fake GIF id and captures the placeholder log.

- [ ] **Step 3: Execute the M3 plan** and run the canonical verification loop, archive log with `--log-suffix "m3_gif_skeleton"`.

**Exit criteria (check all):**

- [ ] `buddy_gif_stub.c/h` exists, ASCII and GIF renderers share `persona_backend_t` (spec §2.2 B).
- [ ] Flash partition + KV naming appear verbatim in the protocol doc.
- [ ] Switching to a GIF persona on device logs the expected placeholder line; no assert / crash.
- [ ] Baseline diff vs M1-A log shows no regression in ASCII persona paths.

**Commit message:** `feat(buddy): add GIF persona UI skeleton (placeholder only)`

---

### Task M4 (RESERVED): macOS / Linux plugin adaptation

Not delivered in this umbrella. Tracked here so that M1-C deliberately leaves extension points (platform detection in `install.sh`, abstracted BLE backend in `buddy_daemon.py`).

- [ ] Sub-project spec + plan filed when the umbrella re-opens M4.

Exit criteria carry forward from M1-C, substituting Windows-only checks with per-platform equivalents.

---

## Cross-cutting requirements

These apply to **every** milestone's sub-plan:

- [ ] Every sub-project spec lives at `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/superpowers/specs/<date>-<name>-design.md`.
- [ ] Every sub-project plan lives at `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/superpowers/plans/<date>-<name>.md`.
- [ ] Sub-plans follow `superpowers:writing-plans` strictly: bite-sized steps, exact file paths, concrete code in every step that touches code, exact commands with expected output.
- [ ] No sub-plan modifies `BLE_WIRE_PROTOCOL.md` without first running a protocol-amendment sub-cycle (brainstorm → writing-plans → merge) — this is the R1 / R7 mitigation.
- [ ] No sub-plan ships firmware without closing with the canonical verification loop and pinning the log.
- [ ] Every commit that is user-visible follows the project's C / Python / Java style rules from the workspace guidelines (TuyaOS C Style, Go / Java / Android / iOS rules as applicable to that component).

---

## Self-review

This section was run by the plan author against this document after writing.

**Spec coverage (spec §x → plan task):**

| Spec section | Umbrella plan coverage |
|---|---|
| §1.1 Goal | Restated in header `Goal:` |
| §1.2 A firmware UI | Task M1-A |
| §1.2 B GIF skeleton | Task M3 |
| §1.2 C CLI plugin | Task M1-C |
| §1.2 D PC tools (interface only) | Task M2 |
| §1.2 E protocol doc | Task M0 |
| §1.3 Non-goals | Respected — no task contradicts them (no OTA, no SC pairing, no stats) |
| §1.4 Verification toolchain | "Canonical firmware verification loop" section + referenced in every firmware task |
| §2.1 Architecture diagram | Reflected in Task M1-A / M1-C ownership split |
| §2.2 A/B/C/D/E contracts | Each is a dedicated task — A→M1-A, B→M3, C→M1-C, D→M2, E→M0 |
| §3.1 Milestones | One-to-one mapping M0 / M1-A / M1-C / Integration / M2 / M3 / M4 |
| §3.2 R1..R8 risks | R1/R7 → "Cross-cutting requirements" amendment rule. R2 → M1-C follows m5-paper-buddy layout verbatim. R3 → M1-C Windows clean-box test. R4 → M1-A persona bounding-box decision. R5 → mono theme carried into M1-A. R6 → M3 `persona_backend_t` abstraction. R8 → M0 exit criterion on advertising-name derivation. |
| §3.3 Test contracts | Each task's Exit criteria mirrors the spec's per-sub-system contract; canonical loop section is reused. |

Gaps found: **none** — every spec bullet is mapped to at least one task. No new spec requirement surfaced during planning.

**Placeholder scan:** searched for TBD/TODO/FIXME inside this document — zero hits.

**Type / name consistency:**

- `persona_backend_t`: introduced in spec §2.2 B, referenced consistently in M3 task. No alternate names.
- `buddy_tama_state_t`: used only in referenced existing code; not renamed by any task.
- `buddy_main_screen_update_state` / `_set_persona`: referenced only in M1-A, consistent with spec §2.2 A.
- `buddy_ble_send_permission` / `buddy_ble_send_cmd`: referenced implicitly through existing code — no new names invented.
- Log path `docs/protocol/baseline/` and `docs/verification/*`: both used consistently; baseline is M0 output, verification is milestone-run evidence.

**Scale check:** the plan is intentionally at milestone granularity (not step granularity). This matches the user's explicit choice of an umbrella spec and doc-only deliverable scope. Step-granular TDD plans are required at each sub-project level and are invoked explicitly in every task's Step 1 / Step 2.

---

## Execution handoff

This umbrella plan's "tasks" are milestones, and each milestone itself will launch its own `brainstorm → writing-plans → executing-plans` cycle. Therefore the standard `writing-plans` execution choice (subagent-driven vs inline) applies **per sub-project**, not at this umbrella level.

Recommended orchestration at the umbrella level:

1. **Manual gate through M0.** The protocol doc sets contracts for everyone else; one human reviewer should approve the M0 merged doc before opening M1-A and M1-C.
2. **Subagent-driven for M1-A and M1-C in parallel.** Each sub-project's `writing-plans` step should choose `superpowers:subagent-driven-development` so fresh subagents per task keep context clean and isolated.
3. **Inline execution for M2** (docs only, short).
4. **Subagent-driven for M3** (small firmware change, but touches registry).
5. **M4 deferred.** Do not auto-open until a user explicitly requests macOS / Linux support.

The integration checkpoint (M1 × Integration) is explicitly a manual activity with three evidence artefacts — it is **not** a subagent task.

---

**End of umbrella plan.**
