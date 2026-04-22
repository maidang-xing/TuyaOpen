# M1-C CLI Plugin — Execution Plan

| | |
|---|---|
| Spec | `docs/superpowers/specs/2026-04-22-m1c-cli-plugin-design.md` |
| Parent plan | `docs/superpowers/plans/2026-04-21-claude-cli-buddy-t5.md` |
| Upstream reference | [op7418/m5-paper-buddy `plugin/`](https://github.com/op7418/m5-paper-buddy/tree/main/plugin) |
| Owner | Plugin subagent (isolated worktree) |
| Status | Proposed |
| Date | 2026-04-22 |

---

## 0. Operating principles (subagent, read first)

1. **TDD first.** This is a pure-Python project with `pytest`; write
   each unit test RED, make it GREEN, only then touch the next behaviour.
   No code is committed without a matching test.
2. **Frozen wire.** Every frame you emit MUST match
   `docs/protocol/BLE_WIRE_PROTOCOL.md` v1.0 byte-for-byte. Run `diff`
   against example frames listed in §3 of that document if unsure.
3. **Mirror m5-paper-buddy layout** verbatim for `plugin.json`,
   `commands/`, `scripts/`, `settings/hooks.json`. Adjust only the
   daemon implementation (BLE target, state dir name, port number).
4. **Windows is primary.** All install + lifecycle scripts ship in two
   flavours (`.sh` + `.ps1`). CI (or manual dev loop) validates on
   Windows 10/11; POSIX scripts ship with an "**Untested in v1**" banner.
5. **Security defaults on** (per Go/Java security rules in workspace):
   - `secrets` module for ids, never `random`.
   - Hook HTTP server binds `127.0.0.1` only; Host header check.
   - `subprocess` always list form, never `shell=True`.
   - Input caps: 8 KB per BLE line, 64 KB per hook payload.
   - Logs DEBUG-only for prompt hints, owner names, device MACs.
6. **Reference docs cite upstream** so a reader can tell which design
   choices are ours vs inherited.

## 1. Task list (execute in order)

### T1 — Test harness bootstrap

**Pre**: none.

**Action**:

```bash
mkdir -p apps/tuya_t5_pocket/claude-cli-plugin/daemon/tuya_pocket_buddy
mkdir -p apps/tuya_t5_pocket/claude-cli-plugin/daemon/tests
cd apps/tuya_t5_pocket/claude-cli-plugin/daemon
# pyproject.toml (write via Write tool, not echo)
```

Create `pyproject.toml`:

```toml
[project]
name = "tuya-pocket-buddy"
version = "0.1.0"
requires-python = ">=3.10"
dependencies = [
  "bleak>=0.22",
  "aiohttp>=3.9",     # hook HTTP server (asyncio-native)
]
[project.optional-dependencies]
dev = ["pytest>=8", "pytest-asyncio>=0.23", "ruff>=0.4", "mypy>=1.8"]
```

Create empty `tuya_pocket_buddy/__init__.py` and
`tests/__init__.py`.

**Verification**:

```bash
cd apps/tuya_t5_pocket/claude-cli-plugin/daemon
python3.10 -m venv .venv-check
.venv-check/bin/pip install -e '.[dev]'
.venv-check/bin/pytest -q          # 0 tests collected is fine; must exit 0
rm -rf .venv-check
```

### T2 — `wire.py` RED → GREEN (heartbeat encoder)

**Pre**: T1 complete.

**Action (RED)**: write `tests/test_wire.py` first with these cases:

- `test_heartbeat_minimal`: expected output is
  `{"total":1,"running":0,"waiting":0,"tokens":0,"tokens_today":0,"msg":""}\n`
  — asserted byte-for-byte (`json.loads` roundtrip then `json.dumps`
  with `separators=(",", ":")` normalises ordering).
- `test_heartbeat_with_entries_truncates_at_80B`: pass a 200-char
  ASCII string; assert the emitted entry string is ≤ 80 bytes in UTF-8
  and ends on a codepoint boundary.
- `test_heartbeat_with_entries_multibyte_safe`: pass a string of 40 CJK
  chars (120 UTF-8 bytes); assert the truncation does NOT split a
  codepoint.
- `test_heartbeat_with_prompt_id_is_hex20`: after `heartbeat(...,
  prompt={"tool":"Read","hint":"foo"})`, the emitted `prompt.id` must
  match `^[0-9a-f]{20}$`.

Run: `pytest -q` → all 4 fail (module doesn't exist).

**Action (GREEN)**: Implement `tuya_pocket_buddy/wire.py` with:

- `_truncate_utf8(s, max_bytes)` helper that drops the trailing partial
  codepoint if any.
- `heartbeat(total, running, waiting, tokens, tokens_today, msg,
  entries=None, prompt=None) -> bytes` that accepts optional entries
  (list of strings; each truncated via `_truncate_utf8(s, 80)`) and
  optional prompt dict. When `prompt` is provided WITHOUT an `id`,
  generate `secrets.token_hex(10)` and inject it.
- `json.dumps(obj, ensure_ascii=False, separators=(",", ":"))` then
  append `b"\n"`.

Run: `pytest -q` → all 4 pass.

### T3 — `wire.py` time_sync / owner / status_request / unpair

**Pre**: T2 green.

Add tests and impls for:

- `time_sync(epoch_s, tz_min)` — emits `{"time":[epoch,tz]}\n`; tests
  cover positive/negative `tz_min` (e.g. `-480` for UTC+8 west-of-UTC
  naïve user input if we ever hit it) and reject non-integers.
- `owner(name)` — emits `{"cmd":"owner","name":"..."}\n`; name >
  30 chars truncated UTF-8-safe.
- `status_request()` — emits exactly `{"cmd":"status"}\n`.
- `unpair()` — emits exactly `{"cmd":"unpair"}\n`.

All pure functions; `ruff check` + `mypy --strict` clean.

### T4 — `wire.py` RX parser

**Pre**: T3 green.

Tests in `test_wire.py`:

- `test_parse_ack`: `b'{"ack":"owner","ok":true,"n":0}\n'` →
  `('ack', {'ack': 'owner', 'ok': True, 'n': 0})`.
- `test_parse_permission`: `b'{"cmd":"permission","id":"abc","decision":"once"}\n'`
  → `('permission', {...})`.
- `test_parse_discards_unknown`: `b'{"foo":1}\n'` → `('unknown', ...)`.
- `test_parse_rejects_oversized`: a 10 KB line → raises `ValueError`.

Impl: `parse_frame(line: bytes) -> tuple[str, dict]` with 8 KB cap and
a `try/except json.JSONDecodeError` returning `('invalid', {})`.

### T5 — `permissions.py` RED → GREEN

**Pre**: T4 green.

Tests in `tests/test_permissions.py` using `pytest-asyncio`:

- `test_ask_resolves_on_permission_frame`:
  - `bridge.ask("Read", "./foo")` is awaited on one task.
  - Another task calls `bridge.handle_permission({"id": <captured id>,
    "decision": "once"})`.
  - Expected return: `{"decision": "approve", "permanent": False}`.
- `test_ask_deny`: same but decision `"deny"` → `{"decision": "deny"}`.
- `test_ask_always`: decision `"always"` → `{"decision": "approve",
  "permanent": True}`.
- `test_mismatched_id_discarded`: `handle_permission` with an unknown
  id does not resolve the pending future; await still hits timeout.
- `test_second_prompt_denies_first`: issuing a second `ask` while the
  first is pending resolves the first with `{"decision":"deny"}` and a
  WARNING log entry.
- `test_timeout_denies`: mock `asyncio` clock; after 35 s unresolved,
  await returns `{"decision":"deny"}`.

Impl: `PermissionBridge` class with an `asyncio.Lock`, a
`current_prompt: PendingPrompt | None`, and `asyncio.wait_for` around
the `future`.

### T6 — `hook_router.py` RED → GREEN

**Pre**: T5 green.

Tests in `tests/test_hook_router.py`:

- Build a `Router` instance with an injected mock `wire`, mock
  `PermissionBridge`, and an in-memory `State`.
- `SessionStart` → Router emits `owner` + heartbeat; state flagged
  `active`.
- `UserPromptSubmit` → heartbeat with `waiting+=1`.
- `PreToolUse` → Router calls `PermissionBridge.ask` with the tool's
  matcher + hint; when bridge resolves, Router returns the equivalent
  Claude Code payload (`{"decision":"approve"}` etc.).
- `PostToolUse` → appends entry (≤ 80 B), emits heartbeat with latest
  `entries[]`.
- `Stop` → emits heartbeat `msg:"session ended"`; state flipped
  inactive.

Impl: stateless function `route(event, payload, deps) -> dict`.

### T7 — `hook_server.py` RED → GREEN

**Pre**: T6 green.

Tests: use `aiohttp.test_utils` to boot the server in a test loop.

- `POST /hook` on `127.0.0.1` with a valid `SessionStart` body →
  200 with `{}` body.
- `POST /hook` with `Host: attacker.example:9878` → 403.
- `POST /hook` with 128 KB body → 413.
- `POST /hook` with invalid JSON → 400.
- Daemon MUST bind to `127.0.0.1`, never `0.0.0.0`. Verify by inspecting
  the chosen socket.

Impl in `hook_server.py`:

```python
async def handle_hook(request):
    if request.host not in ("127.0.0.1:9878", "localhost:9878"):
        raise web.HTTPForbidden()
    data = await request.read()
    if len(data) > 64 * 1024:
        raise web.HTTPRequestEntityTooLarge(max_size=64*1024, actual_size=len(data))
    try:
        payload = json.loads(data)
    except json.JSONDecodeError:
        raise web.HTTPBadRequest()
    # ... route(...)
```

Port = `9878` (constant; m5-paper-buddy uses 9876).

### T8 — `ble_client.py` RED → GREEN (integration-style, mocked)

**Pre**: T7 green.

For M1-C we do NOT attempt to run `bleak` in unit tests (no hardware in
the test lane). Instead:

- `tests/test_ble_client.py`: mock the `BleakClient` and
  `BleakScanner.discover()` surface. Exercise:
  - `test_connect_matches_claude_name_pattern`: scanner returns 3
    peripherals; only the one named `Claude_A1B2` is kept.
  - `test_rx_reassembles_split_frames`: the `_on_notify` callback is
    fed two bytes chunks that together form one NDJSON line; the
    `rx_lines` queue receives one complete line.
  - `test_tx_chunks_to_mtu_minus_3`: enqueuing a 100-byte frame with
    MTU 23 causes 5 GATT writes, each of size ≤ 20.
  - `test_auto_reconnect_backoff`: simulated disconnect; client
    schedules a reconnect after 1 s, backs off 2/4/8/16 capped.

Impl: `BleClient` class exposing `async def start()`, `stop()`, and two
`asyncio.Queue` instances.

### T9 — `__main__.py` — daemon entrypoint

**Pre**: T8 green.

Wire everything together. `python -m tuya_pocket_buddy` runs:

1. Load config (state dir, port, device MAC from `state.json`).
2. Boot `hook_server` on `127.0.0.1:9878`.
3. Boot `BleClient` in background.
4. On SIGTERM/SIGINT, graceful shutdown: stop BLE client, close HTTP
   server, drain futures, exit 0.

No new unit tests (pure wiring); covered by the integration smoke in
T14.

### T10 — `plugin.json` + `settings/hooks.json`

**Pre**: T9 green.

Write `apps/tuya_t5_pocket/claude-cli-plugin/plugin.json`:

```json
{
  "name": "tuya-pocket-buddy",
  "version": "0.1.0",
  "description": "Claude Code companion on a Tuya T5AI-Pocket. Mirrors sessions, routes permission prompts to hardware buttons over BLE (Nordic UART Service).",
  "author": "TuyaOpen Project",
  "homepage": "https://github.com/TuyaOpen/TuyaOpen",
  "commands": "./commands",
  "hooks": "./settings/hooks.json",
  "scripts": "./scripts"
}
```

Write `apps/tuya_t5_pocket/claude-cli-plugin/settings/hooks.json` with
the same 5 hook entries as m5-paper-buddy's `plugin/settings/hooks.json`,
pointing at `http://127.0.0.1:9878/hook` (the `curl ... || echo '{}'`
fallback keeps Claude Code working when the daemon is down).

### T11 — Slash command specs (`commands/*.md`)

**Pre**: T10.

Create:
- `buddy-install.md` — runs `scripts/install.{sh,ps1}`.
- `buddy-start.md` — runs `scripts/start.{sh,ps1}`.
- `buddy-stop.md` — runs `scripts/stop.{sh,ps1}`.
- `buddy-status.md` — runs `scripts/status.{sh,ps1}`.
- `buddy-pair.md` — invokes daemon's scan+select flow via `python -m
  tuya_pocket_buddy pair`.
- `buddy-unpair.md` — invokes `python -m tuya_pocket_buddy unpair`
  (sends `cmd:"unpair"` — cosmetic in v1, documented so).

Each file mirrors the tone and length of m5-paper-buddy's command docs.

### T12 — Install / lifecycle scripts (POSIX + PowerShell)

**Pre**: T11.

Write `.sh` and matching `.ps1` versions of:
- `install`, `install-hooks`, `start`, `stop`, `status`
- (No `flash` script — flashing is out of scope.)

POSIX scripts begin with:

```sh
#!/usr/bin/env bash
set -euo pipefail
```

PowerShell scripts begin with:

```powershell
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
```

`install` scripts:
1. Verify Python ≥ 3.10 on PATH.
2. Create venv at the state dir (`~/.tuya-pocket-buddy/venv` or
   `%LOCALAPPDATA%\tuya-pocket-buddy\venv`).
3. `pip install ./daemon`.
4. Call `install-hooks` to merge `settings/hooks.json` into
   `~/.claude/settings.json` (or `%USERPROFILE%/.claude/settings.json`).

`start` scripts:
1. If pid file exists and process is alive, exit 0 (idempotent).
2. Launch `python -m tuya_pocket_buddy` detached; record pid.

`stop` scripts: read pid file, `kill` / `Stop-Process`, remove pid
file.

`status` scripts: print daemon pid (or "not running"), selected device
MAC (if any), tail of `daemon.log`.

No unit tests — these are tested by the T14 smoke loop.

### T13 — `README.md` for the plugin

**Pre**: T12.

Mirror the layout of m5-paper-buddy's `plugin/README.md`:
- What it does (feature bullets).
- Install (`/buddy-install`) + `/buddy-pair` + `/buddy-start`.
- Transport section (BLE only; no USB serial in v1).
- Files listing.
- Uninstall.

Explicitly cite the upstream in the credits:
> Directory layout, hook wiring, and install UX are modelled on
> op7418/m5-paper-buddy. Used with gratitude; not a fork.

### T14 — Integration smoke (HARDWARE REQUIRED, Windows primary)

**Pre**: T13 complete; M1-A firmware flashed to a T5AI-Pocket.

**Action (Windows 11 host)**:

```powershell
# 1. Install plugin (after merging Claude Code settings.json with hooks)
pwsh scripts/install.ps1

# 2. Open a Claude Code session in a scratch dir
claude  # or the CLI entrypoint

# 3. /buddy-pair → choose Claude_XXXX
# 4. /buddy-start
# 5. Issue `Read ./foo.c` so PreToolUse fires; verify the device shows
#    the prompt within 3 s, press ENTER on the device.
# 6. Repeat with LEFT (deny) and RIGHT (always).
# 7. /buddy-stop

Get-Content $env:LOCALAPPDATA\tuya-pocket-buddy\daemon.log -Tail 200 |
    Out-File apps/tuya_t5_pocket/tuya_t5_pocket_ai/docs/protocol/baseline/${SHA}-m1c-smoke.log
```

Capture the UART side as well:

```bash
python3 /home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
  service start --detach --port /dev/ttyACM1 --baud 460800
# ... run steps above ...
python3 /home/share/samba/tyopen/TuyaOpen/.cursor/skills/agent-hardware-debug-helper-tools/agent_target_tool.py \
  service stop
cp .target_logging/*.log docs/protocol/baseline/${SHA}-m1c-uart.log
```

**Verification gate** (daemon log side):

```powershell
Select-String 'connected to Claude_' docs/protocol/baseline/${SHA}-m1c-smoke.log  # ≥ 1
Select-String 'pre_tool_use.*id=[0-9a-f]{20}' ...                                 # ≥ 1
Select-String 'permission.*decision=once' ...                                     # ≥ 1
Select-String 'permission.*decision=deny' ...                                     # ≥ 1
Select-String 'permission.*decision=always' ...                                   # ≥ 1
Select-String 'session ended' ...                                                  # ≥ 1
```

Verification gate (UART side) — MUST match the M1-A gate plus a
plugin-origin `owner` ack:

```bash
LOG=docs/protocol/baseline/${SHA}-m1c-uart.log
grep -c 'peer connected'                                            "$LOG"  # ≥ 1
grep -c '"ack":"owner"'                                             "$LOG"  # ≥ 1
grep -c 'time sync ok epoch='                                       "$LOG"  # ≥ 1
grep -c 'entries: idx='                                             "$LOG"  # ≥ 3
grep -cE '"cmd":"permission","id":"[0-9a-f]{20}","decision":"(once|deny|always)"' "$LOG"  # ≥ 3
```

If any gate line returns zero, fail the milestone and fix before
retrying. The 20-hex-digit id pattern is the strongest proof that the
plugin — not Claude Desktop — drove the session.

### T15 — Update `baseline/README.md` index

**Pre**: T14 done.

Add rows for the two new logs. Note that the `-m1c-smoke.log` file
originates from the Windows daemon and may contain backslash line
endings; that's intentional and captured for fidelity.

### T16 — Self-review

**Pre**: T15 done.

- [ ] `pytest -q` exits 0.
- [ ] `ruff check daemon/` exits 0.
- [ ] `mypy --strict daemon/tuya_pocket_buddy` exits 0.
- [ ] No `random.*` import outside tests. (`grep -rnE '^import random|from random' daemon/tuya_pocket_buddy/`)
- [ ] No `shell=True` anywhere. (`grep -rnE 'shell=True' daemon/`)
- [ ] `plugin.json` keys match m5-paper-buddy naming.
- [ ] Hook endpoint returns `{}` on the silent-fallback failure mode
      (so Claude Code keeps working with the daemon off).
- [ ] README credits m5-paper-buddy.
- [ ] Every new public function has a docstring.
- [ ] Git diff touches only `apps/tuya_t5_pocket/claude-cli-plugin/**`,
      `docs/protocol/baseline/*.log`,
      `docs/protocol/baseline/README.md`, and plan/spec docs.

## 2. Parallelism / subagent boundary

Run this plan inside ONE subagent / worktree dedicated to M1-C. No
files overlap with M1-A. Safe to dispatch in parallel with the M1-A
plan.

Known shared dependency: the integration smoke (T14) needs a device
already flashed with M1-A firmware. Two ways to handle in parallel:

- **Option a (recommended)**: M1-C subagent runs T1–T13 in parallel
  with the whole M1-A subagent; T14 is gated on M1-A completing. The
  wait is naturally short since T1–T13 is CPU-work and will fill the
  parallel budget.
- **Option b**: M1-C subagent runs all tasks against a mocked BLE peer
  (inject a fake device simulator into `BleClient`) and defers T14 to
  a later integration round. Accept this only if M1-A is materially
  delayed.

## 3. Exit criteria (matches umbrella plan M1-C EXIT)

1. Unit tests (§4.1 of spec) all pass; static checks clean.
2. Integration smoke log captured on Windows + matching UART log.
3. Plugin installs cleanly on Windows via `/buddy-install`.
4. Daemon survives an entire Claude Code session without restart.
5. Commit/PR links back to the spec and this plan.
