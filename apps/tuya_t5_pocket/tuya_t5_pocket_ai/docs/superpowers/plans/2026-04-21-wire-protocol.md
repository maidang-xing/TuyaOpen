# M0 · Wire Protocol Doc + Baseline Log — Execution Plan

**Spec:** `../specs/2026-04-21-wire-protocol-design.md`
**Parent plan:** `2026-04-21-claude-cli-buddy-t5.md` (umbrella roadmap, M0 row)
**Shape:** Docs-only milestone. No firmware source edits. "Tests" are doc-lint + on-device baseline log capture.

---

## Preconditions (verify before any task)

- [ ] Repo clean on `feat/*` branch (no pending firmware edits for this milestone)
- [ ] T5AI-Pocket hardware attached, both serial ports enumerate under `agent_target_tool.py pick-port`
- [ ] `tos.py` available on `PATH` (from `export.sh`)
- [ ] Working dir for commands: `apps/tuya_t5_pocket/tuya_t5_pocket_ai`

## Deliverables

1. `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md`
2. `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/<short-sha>-boot.log`
3. `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/README.md`

---

## Task 1 — Create `docs/protocol/` tree

- [ ] Create directory `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/`
- [ ] Create directory `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/`
- [ ] Add a one-line `docs/protocol/.gitkeep` only if empty — skip if `BLE_WIRE_PROTOCOL.md` lands in the same commit

**Acceptance:** `ls apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/` shows the two intended paths.

---

## Task 2 — Write `BLE_WIRE_PROTOCOL.md` v1.0

Author the file strictly following the 8-section outline in the spec (§4).

- [ ] Section 1 — TL;DR + peer/device direction diagram
- [ ] Section 2 — Transport (NUS UUIDs verbatim, adv name rule, ADV/scan payload split, MTU, NDJSON framing with 5120 B line cap)
- [ ] Section 3 — Message catalogue; one subsection per frame, each with: direction, trigger, required / optional fields (with types), size cap, firmware status (✅ / ⚠️ / ❌), and one concrete JSON example
- [ ] Section 4 — Ordering and liveness
- [ ] Section 5 — Permission correlation rules (`prompt.id` echo, mismatch discard)
- [ ] Section 6 — Security & hardening notes (no LESC, line cap, cmd whitelist, `buddy_ble_send_cmd` char filter)
- [ ] Section 7 — Gap table (pre-filled rows from spec §4.7)
- [ ] Section 8 — Amendment procedure (pointer to spec / writing-plans cycle)
- [ ] Front-matter: title, version `v1.0`, date `2026-04-21`, status `Frozen`, source of truth pointer (`REFERENCE.md` + firmware `buddy_ble.c`)

**Static verification:**

- [ ] `rg '"serviceUUID":\s*"6e400001'` — confirm service UUID matches REFERENCE (expected absent from the protocol doc because stored as plain string, use `rg '6E400001-B5A3-F393-E0A9-E50E24DCCA9E'` instead)
- [ ] `rg '6E400001-B5A3-F393-E0A9-E50E24DCCA9E' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` returns 1 hit
- [ ] `rg '6E400002-B5A3-F393-E0A9-E50E24DCCA9E' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` returns 1 hit (RX)
- [ ] `rg '6E400003-B5A3-F393-E0A9-E50E24DCCA9E' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` returns 1 hit (TX)
- [ ] `rg 'Claude_' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` appears in adv-name section
- [ ] `rg 'entries\[\]' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` appears in both §3 and §7 (doc lists it as ❌ in gap table)
- [ ] `rg 'prompt\.id' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/BLE_WIRE_PROTOCOL.md` appears in §5
- [ ] No TODO / FIXME / XXX strings left in the file
- [ ] Markdown headings monotonic (no jump from `##` to `####`)

**Cross-reference verification:**

- [ ] Every field listed in §3 of the new doc exists in either `apps/tuya_t5_pocket/claude-desktop-buddy/REFERENCE.md` or a `cJSON_GetObjectItem(...)` / `cJSON_CreateString(...)` call in `apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui/buddy_ui/buddy_ble.c`
- [ ] Every row in the gap table states its current firmware status matching the actual `buddy_ble.c` behaviour

**Acceptance:** All checklist items pass; file stands alone as the authoritative source for future sub-projects.

---

## Task 3 — Build current HEAD

- [ ] `cd apps/tuya_t5_pocket/tuya_t5_pocket_ai`
- [ ] `tos.py build`
- [ ] Confirm exit code `0` and firmware artefact under `.build/`

**Acceptance:** Clean build, no warnings promoted to errors; record `git rev-parse --short HEAD` for later filename.

---

## Task 4 — Identify ports & flash

- [ ] `python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py pick-port` → capture `FLASH_PORT` and `MONITOR_PORT`
- [ ] `tos.py flash -p ${FLASH_PORT}` → exit code `0`, flash completed

**Acceptance:** Flash tool reports success; device reboots on its own or via next-step hw-reset.

---

## Task 5 — Capture baseline log

- [ ] Start a debug session with hw-reset:

  ```bash
  python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
          debug-session run -p ${MONITOR_PORT} \
          --log-suffix "baseline_$(git rev-parse --short HEAD)" --hw-reset
  ```

- [ ] Let the session run ≥ 30 seconds (boot → MQTT up → BLE advertising idle)
- [ ] Stop the session cleanly (`debug-session stop` or the tool's documented exit)

**Log quality gates (grep the captured file):**

- [ ] `rg 'buddy_ble init done'` → exactly 1 hit
- [ ] `rg 'name=Claude_[0-9A-Fa-f]{4}'` → ≥ 1 hit (confirms advertising name derivation fires)
- [ ] `rg 'advertising as Claude_'` → ≥ 1 hit (post-MQTT take-over)
- [ ] `rg -i '(mem overflow|assert|hardfault|panic)'` → 0 hits
- [ ] `rg -i 'ERROR'` → 0 hits from the `buddy_ble` tag (tolerate unrelated subsystem errors, note them in README)

If any hard gate fails, **do not copy the log**. Fix the root cause (retry session, verify hardware) and recapture.

**Acceptance:** Session log passes every gate above.

---

## Task 6 — Publish baseline artefacts

- [ ] Let `SHA=$(git rev-parse --short HEAD)`, `SRC=.target_logging/<latest>.log`
- [ ] `cp ${SRC} apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/${SHA}-boot.log`
- [ ] Verify the copied file is > 2 KB and < 2 MB (sanity range)

**Acceptance:** `ls apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/` shows the new log file.

---

## Task 7 — Write `baseline/README.md`

Content requirements:

- [ ] Brief explanation of what a baseline log is and how future milestones should use it (regression-grep reference)
- [ ] Exact regeneration command block (the commands from Task 3 → Task 6)
- [ ] Table listing the mandatory grep anchors (from Task 5 quality gates)
- [ ] Index section listing every captured baseline with: filename, capture date, short-sha, one-line note
- [ ] Rule: "New baseline required whenever `buddy_ble.c`, `buddy_ble.h`, advertising-name logic, or BLE init ordering changes"

**Acceptance:**
- [ ] File exists, `rg 'Regenerate' apps/.../baseline/README.md` hits
- [ ] Index lists the freshly captured log with correct sha + date

---

## Task 8 — Final self-review

- [ ] Re-open the umbrella spec §3.1 M0 EXIT criteria; walk through each line and tick it off against the three deliverables
- [ ] `rg 'TODO|FIXME|XXX' apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/` returns 0
- [ ] `git status` shows only additions under `apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/` (plus the two superpowers docs) — **no firmware source file changed**
- [ ] Umbrella plan M0 row marked complete, M1-A / M1-C rows flagged as unblocked

**M0 EXIT:** reachable when this task's checklist is fully ticked.

---

## Rollback plan

- Deliverables are docs; rollback is a `git restore` of the three added paths.
- If the baseline log must be recaptured on a different commit, the README's regeneration block is the single source of truth.

## Out-of-band risks during execution

- Hardware not enumerating → fall back to "design-frozen, baseline deferred" status; update umbrella plan M0 EXIT to explicitly call out pending baseline, but do **not** start M1-A until the log is captured.
- Current-HEAD build fails → fix build first in a separate tiny sub-project (log the failure in the umbrella risk register); M0 remains blocked until build is green again.
