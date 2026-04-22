# Baseline Logs

This directory contains captured serial logs of the T5AI-Pocket firmware while the BLE wire protocol (`../BLE_WIRE_PROTOCOL.md`) is active. Each baseline is pinned to a specific firmware commit and documents **known-good behaviour** that every later milestone (M1-A firmware UI, M1-C CLI plugin, etc.) must preserve or intentionally supersede.

## What a baseline is

A baseline log is the single authoritative "reference recording" of a firmware commit talking to a BLE central peer. Subsequent sub-projects use these logs to:

- **Regress-grep** new firmware captures against the baseline (e.g. "`buddy_ble peer connected` must still appear; new log must still show `send notify ok` after `{"cmd":"status"}`").
- **Detect format drift** in the wire frames (e.g. heartbeat fields, ack envelope shapes).
- **Sanity-check hardware/driver changes** — if a new baseline on a refactored BLE path lacks any of the grep anchors below, the change is very likely a regression.

## Regeneration command block

Run from the repo root with the T5AI-Pocket attached, Claude Desktop optionally connected (for end-to-end protocol coverage).

```bash
# 1. Identify the two serial ports on the T5AI CH340 dual-bridge.
#    Convention: lower ttyACM = flash, higher = log/monitor.
python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py --json list-devices | head -c 500

# 2. Build current HEAD and capture its short sha.
cd apps/tuya_t5_pocket/tuya_t5_pocket_ai
tos.py build
SHA=$(git -C /home/share/samba/tyopen/TuyaOpen rev-parse --short HEAD)
cd -

# 3. Flash.
cd apps/tuya_t5_pocket/tuya_t5_pocket_ai
tos.py flash -p /dev/ttyACM0
cd -

# 4. Start a detached log capture on the monitor port. Baud defaults to 460800 (T5AI).
python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py --json service stop
python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py --json \
        service start --detach -p /dev/ttyACM1 --log-suffix "baseline_${SHA}"

# 5. Trigger a reset so the capture contains boot + advertising.
#    If the bridge's DTR is NOT wired to the T5AI RST line (common on
#    CH340 dual-serial adapters), use the physical RST button instead.
python3 - <<'PY'
import json, socket
s = socket.create_connection(('127.0.0.1', 58761), timeout=5)
s.sendall((json.dumps({'op':'hw_reset'}) + '\n').encode())
print(s.recv(4096).decode())
PY

# 6. Let it run for ~30 seconds (boot + MQTT up + BLE advertising; optionally
#    let Claude Desktop connect for richer coverage).
sleep 30

# 7. Stop the capture and locate the log.
python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py --json service stop
python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py --json logs latest

# 8. Copy the log into this directory.
SRC=$(python3 .cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py --json logs latest \
      | python3 -c 'import sys,json; print(json.load(sys.stdin)["recommended"])')
cp "${SRC}" apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/${SHA}-<variant>.log
```

Pick `<variant>`:

| Variant suffix | Capture mode | Expected content |
|---|---|---|
| `-boot.log` | Physical RST pressed, Desktop not yet connected | `buddy_ble init done`, `advertising as Claude_XXXX` |
| `-runtime.log` | Board already up, Desktop connected | `buddy_ble peer connected`, MTU update, heartbeat frames, `send notify ok` |
| `-handshake.log` | Physical RST then Desktop connects during capture | Both of the above |

## Grep anchors (applied on every capture)

At least **one** of the baselines in this directory MUST contain every line below. Individual captures may contain a subset (see the variant table). New captures are compared against the combined coverage.

### Hard gates (must be true on every capture)

| Anchor regex | Meaning | Action on failure |
|---|---|---|
| `(?i)(mem overflow\|hardfault\|panic\|assert)` ⇒ **0 matches** | No crash paths | Discard capture, investigate before trusting the baseline |
| `ipc_router_tx_cmpl_isr error` | Platform-level BLE HCI IPC warnings on T5AI (non-fatal) | Informational; noted here so future captures don't flag them as regressions |

### Coverage gates (combined across variants)

| Anchor | Variant that must contain it |
|---|---|
| `buddy_ble init done name=Claude_[0-9A-Fa-f]{4,}` | `-boot.log` |
| `buddy_ble advertising as Claude_` | `-boot.log` |
| `buddy_ble peer connected conn=` | `-runtime.log` / `-handshake.log` |
| `buddy_ble mtu=` | `-runtime.log` / `-handshake.log` |
| `buddy_ble RX {"time":\[` | `-runtime.log` / `-handshake.log` |
| `buddy_ble RX {"cmd":"status"}` | `-runtime.log` / `-handshake.log` |
| `send notify ok` | `-runtime.log` / `-handshake.log` |
| `buddy_ble RX {"total":` | `-runtime.log` / `-handshake.log` |

## Baseline index

| File | Captured | Short SHA | Variant | Notes |
|---|---|---|---|---|
| `616464c5-runtime.log` | 2026-04-22 16:47 | `616464c5` | runtime | T5AI-Pocket, firmware with BLE co-registration. Claude Desktop connected live. Covers peer connect → MTU=256 → time sync → `{"cmd":"status"}` → heartbeat frames. `ipc_router_tx_cmpl_isr error @423` warnings observed (platform, non-fatal). DTR-based hw-reset on the CH340 bridge did **not** reset the MCU — a boot-variant baseline therefore needs a physical RST press and is deferred until the next milestone that changes boot-path behaviour. |
| `616464c5-m1a.log` | 2026-04-22 17:30 | `616464c5` | runtime (M1-A) | **First capture exercising M1-A firmware changes (`entries[]` ring + `time[]` UI offset) end-to-end against a live Claude Desktop peer.** Confirms the new DEBUG anchors `buddy_ble time sync ok epoch=<epoch> tz=<tz>` and `ui clock render HH=%02d MM=%02d` fire on real hardware (1 and 32 hits respectively) and that `peer connected` / heartbeat parsing is unchanged vs M0. **Known runtime gaps:** the currently deployed Claude Desktop peer sends `"entries":[]` on every heartbeat and never issues a permission prompt, so the `entries: idx=`, `ui scroll idx=`, and `"cmd":"permission"` TX anchors do **not** fire in this capture. Those anchors are static-verified in source (see `grep -n` over `buddy_ble.c` / `buddy_main_screen.c`) and will be exercised end-to-end once the M1-C plugin lands and populates `entries[]`. Same M0 DTR-reset limitation applies: no fresh `buddy_ble init done` on the `-m1a` capture. |

## When to add a new baseline

A new baseline is **required** whenever any of the following are modified:

- `src/display/ui/buddy_ui/buddy_ble.c` / `buddy_ble.h`
- `src/display/ui/buddy_ui/buddy_data.h` (field layout affects wire-to-UI mapping)
- Advertising-name derivation (`__derive_name` in `buddy_ble.c`)
- BLE init ordering (`buddy_ble_init` / `buddy_ble_start`, Tuya `ble_mgr` ordering)
- Any TAL/TKL BLE layer API that `buddy_ble.c` depends on

For UI-only changes that do not touch wire traffic, a new baseline is **not** required — the existing baseline still represents the wire contract.

## When to retire a baseline

- When a later, broader-coverage baseline from the same sha supersedes it.
- When the commit the baseline pins has been rebased / squashed out of the history; in that case capture a new baseline on the current tip and add a note in this README linking old→new.

Never delete a baseline silently; always update this README with a one-line retirement note.
