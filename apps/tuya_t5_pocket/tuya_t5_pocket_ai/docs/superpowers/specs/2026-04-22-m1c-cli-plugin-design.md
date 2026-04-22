# M1-C Claude Code CLI Plugin — BLE central, Python daemon

| | |
|---|---|
| Milestone | M1-C (Plugin sub-project round 1) |
| Parent umbrella spec | `docs/superpowers/specs/2026-04-21-claude-cli-buddy-t5-design.md` |
| Parent umbrella plan | `docs/superpowers/plans/2026-04-21-claude-cli-buddy-t5.md` |
| Protocol reference | `docs/protocol/BLE_WIRE_PROTOCOL.md` v1.0 (peer side) |
| Upstream reference | [op7418/m5-paper-buddy `plugin/`](https://github.com/op7418/m5-paper-buddy/tree/main/plugin) |
| Owner | CLI-plugin sub-project |
| Status | Proposed |
| Date | 2026-04-22 |

---

## 1. Goal

Ship a **Claude Code plugin** (slash commands + hooks + background Python
daemon) that turns a running Claude Code session into a BLE central,
connects to a T5AI-Pocket advertising as `Claude_XXXX`, and exchanges
the exact frames documented in `BLE_WIRE_PROTOCOL.md`.

Baseline behaviour to match: **parity with Claude Desktop on the device
side.** If M1-A's firmware is plugged in and the plugin is installed,
the user should see:

- heartbeats updating sessions / tokens / msg / entries panel,
- permission prompts appearing on the device,
- approve/deny/always button presses returning to Claude Code as the
  hook's stdout (so Claude Code proceeds or aborts accordingly).

Structure, file layout, and install UX mirror
`op7418/m5-paper-buddy/plugin/` (decision `m1c_shape:vendored`).

## 2. In-scope / Out-of-scope

### In scope

- A `claude-cli-plugin/` directory vendored under
  `apps/tuya_t5_pocket/`, laid out exactly like m5-paper-buddy's
  `plugin/`:
  - `plugin.json` — manifest.
  - `commands/buddy-*.md` — slash command specs.
  - `scripts/{install,install-hooks,start,stop,status,common}.sh` —
    install/lifecycle shell scripts (PowerShell equivalents added for
    Windows per OS decision).
  - `settings/hooks.json` — the hook block merged into the user's
    `~/.claude/settings.json`.
  - `daemon/` (Python package) — BLE central + HTTP hook endpoint.
- Python daemon built on `bleak` (decision `m1c_ble_lib:bleak`),
  connecting to NUS, sending heartbeat / `time[]` / `cmd:"owner"` /
  permission prompts, receiving `cmd:"permission"` / `ack:*` /
  `cmd:"status"`.
- Hook shape 1:1 with m5-paper-buddy (decision `m1c_hooks:match_m5paper`):
  `SessionStart`, `Stop`, `UserPromptSubmit`, `PreToolUse` (matcher `.*`),
  `PostToolUse` (matcher `.*`).
- Per-session daemon lifecycle (decision `m1c_daemon_life:per_session`):
  the `SessionStart` hook starts the daemon if not running and writes
  pid + session id under `%LOCALAPPDATA%/tuya-pocket-buddy/` (Windows)
  or `~/.tuya-pocket-buddy/` (POSIX); the `Stop` hook stops it.
- OS priority (decision `m1c_os_clarify:windows_primary`):
  - **Windows 10/11 x64** — first-class, tested, smoke-loop in CI (or
    manual script when no CI).
  - macOS 13+ and Linux (kernel ≥ 5.10 with BlueZ ≥ 5.64) — best-effort.
    Install scripts present but flagged as untested.

### Out of scope

- Firmware changes. M1-A carries all firmware scope.
- Character pack / persona upload (`char_begin`/`file`/`chunk`/
  `file_end`/`char_end`). Skeleton-only per umbrella spec sub-project B.
  The daemon MUST NOT emit these frames in M1-C.
- `evt:"turn"` push. The daemon does not push turn events in M1-C
  because M1-A does not consume them.
- Real `unpair` semantics — the plugin MAY send `cmd:"unpair"` if the
  user invokes `/buddy-unpair`, but MUST document that v1.0 firmware
  acks cosmetically.
- Packaging as a PyPI or Homebrew artefact. v1 is install-from-repo.
- Authentication / LESC enforcement on the BLE link. `data.sec:false`
  remains the baseline; the plugin does not attempt encrypted pairing.

## 3. Design

### 3.1 Directory layout

```
apps/tuya_t5_pocket/claude-cli-plugin/
├── plugin.json
├── README.md
├── commands/
│   ├── buddy-install.md
│   ├── buddy-start.md
│   ├── buddy-stop.md
│   ├── buddy-status.md
│   ├── buddy-pair.md         # T5AI scan + select
│   └── buddy-unpair.md       # sends cmd:"unpair" (cosmetic in v1)
├── settings/
│   └── hooks.json
├── scripts/
│   ├── common.sh             # POSIX helpers
│   ├── common.ps1            # Windows helpers (first-class)
│   ├── install.sh
│   ├── install.ps1
│   ├── install-hooks.sh
│   ├── install-hooks.ps1
│   ├── start.sh
│   ├── start.ps1
│   ├── stop.sh
│   ├── stop.ps1
│   ├── status.sh
│   └── status.ps1
└── daemon/
    ├── pyproject.toml
    ├── README.md
    ├── tuya_pocket_buddy/
    │   ├── __init__.py
    │   ├── __main__.py        # `python -m tuya_pocket_buddy`
    │   ├── config.py          # resolves state dir, logs, env vars
    │   ├── hook_server.py     # HTTP server on 127.0.0.1:<port>
    │   ├── hook_router.py     # dispatches hook payloads → state
    │   ├── ble_client.py      # bleak wrapper: scan / connect / RX / TX
    │   ├── wire.py            # encodes/decodes the M0 frozen frames
    │   ├── state.py           # session + prompt tracker
    │   └── permissions.py     # PreToolUse ↔ buddy permission bridge
    └── tests/
        ├── test_wire.py
        ├── test_permissions.py
        └── test_hook_router.py
```

This 1:1 mirrors the `plugin.json` layout of m5-paper-buddy (`commands:
"./commands"`, `hooks: "./settings/hooks.json"`, `scripts: "./scripts"`).
The Python package lives next to the scripts in `daemon/` instead of
being embedded in the parent firmware repo — that matches the
"plugin ships as a self-contained unit" posture.

### 3.2 Wire encoding (`wire.py`)

All frames follow `BLE_WIRE_PROTOCOL.md` exactly. The Python module
SHALL expose pure, side-effect-free functions, each of which returns
`bytes` ending in a single `\n`:

```python
def heartbeat(total, running, waiting, tokens, tokens_today, msg,
              entries=None, prompt=None) -> bytes: ...
def time_sync(epoch_s: int, tz_min: int) -> bytes: ...
def owner(name: str) -> bytes: ...
def status_request() -> bytes: ...
def unpair() -> bytes: ...
```

Constraints:
- UTF-8 encoded JSON, no pretty-printing, no trailing whitespace before
  the newline.
- Single line per call (no embedded `\n`).
- Individual line ≤ 4 KB to stay clear of the firmware's 5120 B cap
  (§6 hardening notes).
- `entries` is a list of strings. Each string is truncated at 80 bytes
  UTF-8-safe before emission (must match firmware ring).
- `prompt` is a dict with `id` + `tool` + `hint`. The `id` MUST be
  generated from `secrets.token_hex(10)` — crypto-safe PRNG per Golang /
  Java security rules (the applicable analogue for Python is
  `secrets.*`, not `random.*`).

Decoded frames from the device (`ack:*`, `cmd:"permission"`, `cmd:"status"`)
get parsed by symmetric functions. Unknown frames are logged at DEBUG
and discarded.

### 3.3 BLE client (`ble_client.py`)

Responsibilities:
1. Scan for peripherals whose local name matches `^Claude_[0-9A-Za-z]+$`.
   `bleak.BleakScanner.discover()` with a 10-s window.
2. If multiple candidates, prompt the user via the `/buddy-pair`
   slash command output (text list → user replies with index). Selected
   MAC is persisted in `state.json`.
3. Connect, subscribe to TX notify (NUS `6E400003-...`), write to
   RX characteristic (NUS `6E400002-...`) in chunks ≤ `MTU-3`.
4. Reassemble incoming notifications into newline-delimited lines
   (same framing as §2.4 of the wire protocol) before feeding to
   `wire.parse_frame()`.
5. Auto-reconnect with exponential backoff (1 s → 16 s, capped).
   Disconnect clears in-flight state and re-emits "disconnected" to the
   hook router.
6. Expose two async queues (`rx_lines`, `tx_lines`) for the hook
   router to read/write without caring about BLE internals.

Platform notes:
- Windows: `bleak` uses WinRT. First connect may prompt the user to
  allow access; `/buddy-pair` script instructs on this.
- Linux: `bleak` uses BlueZ. The daemon must have `cap_net_raw` or run
  as a user in the `bluetooth` group. Documented in `README.md`.
- macOS: CoreBluetooth requires user to grant Bluetooth permission to
  the Python interpreter on first run.

### 3.4 Hook server + router (`hook_server.py`, `hook_router.py`)

Port: **127.0.0.1:9878** (distinct from m5-paper-buddy's 9876 to allow
both plugins to coexist on one host).

HTTP protocol:
- `POST /hook` — body = Claude Code hook payload JSON.
- Response: JSON object forwarded back to Claude Code. For
  `PreToolUse` responses this is how we return an approval/deny decision.
- Timeout on the hook side stays at 40 s (matches m5-paper-buddy), which
  is more than enough budget for the user to press a device button.

Router logic, per `hook_event_name`:

| Event | Router action |
|---|---|
| `SessionStart` | Mark session active, kick off connect attempt if not already. Send `cmd:"owner"` + initial heartbeat to device. |
| `UserPromptSubmit` | Update in-memory `waiting` counter, emit heartbeat. |
| `PreToolUse` | Allocate a `prompt_id`, emit heartbeat with `prompt:{id, tool, hint}`, block until the user presses a button or timeout (40 s). Translate `permission:"once"`→`{"decision":"approve"}` / `"deny"`→`{"decision":"deny"}` / `"always"`→`{"decision":"approve", "permanent":true}` for Claude Code. |
| `PostToolUse` | Append a sanitized entry to the ring (≤ 80 B UTF-8), emit heartbeat with fresh `entries[]`. |
| `Stop` | Emit heartbeat with `msg:"session ended"`, `waiting:0`, `running:0`. Optionally close BLE. Stop daemon if this was the last active session (per-session lifecycle). |

Concurrency: a single `asyncio` loop, one task per responsibility. No
threads. State mutation goes through `asyncio.Lock`.

### 3.5 Permission bridge (`permissions.py`)

This is the tightest module and the one most likely to surface bugs.
Design:

```python
@dataclass
class PendingPrompt:
    id: str
    tool: str
    hint: str
    future: asyncio.Future   # resolved by the BLE-side permission frame
    created_at: float

class PermissionBridge:
    async def ask(self, tool: str, hint: str) -> dict:
        """Called from the PreToolUse handler; blocks until a button
        is pressed or 35 s elapse (leaves 5 s hook budget headroom)."""
```

Rules:
- Exactly one pending prompt at a time. A new `PreToolUse` while one is
  pending SHALL timeout the old one with `decision:"deny"` (matches the
  firmware's "single in-flight prompt" contract in protocol §5).
- A `cmd:"permission"` whose `id` doesn't match the current pending
  prompt MUST be silently discarded (also matches §5).
- Timeout: return `{"decision":"deny"}` after 35 s with a log line, so
  Claude Code aborts the tool call rather than hanging.
- All time-based comparisons use a 64-bit monotonic source
  (`time.monotonic_ns`) — no `time.time()` for deltas.

### 3.6 Security

Applies Go/Java/Android security rules (the closest analogue to a
Python daemon per the workspace's rules set):

- **No hardcoded secrets.** Nothing under `claude-cli-plugin/` may
  contain real device UUIDs, cloud keys, or API tokens; config lives in
  `~/.tuya-pocket-buddy/config.json` or `%LOCALAPPDATA%/...`.
- **Local-only HTTP.** Hook server binds `127.0.0.1` only; no 0.0.0.0.
  Rejects any request whose Host header isn't `127.0.0.1:9878`.
- **No shell interpolation.** All `subprocess` calls use list form; no
  `shell=True`. Applies especially to `/buddy-flash` (if added later).
- **Input validation.** All BLE RX lines are parsed via `json.loads`
  with max length 8 KB (daemon-side cap, smaller than the firmware's
  5120 B because it's *per message*, not per buffer). Lines bigger are
  discarded. Unknown top-level keys are ignored.
- **CSPRNG for ids.** `secrets.token_hex` only. `random.*` forbidden.
- **Logging.** No prompt hints longer than 80 chars, no owner names, no
  device MAC at INFO level. Logs land in
  `~/.tuya-pocket-buddy/daemon.log` with daily rotation; retention =
  7 days.
- **Windows path handling.** `pathlib.Path` everywhere, never
  string-concat, so `\..\` traversal from a misconfigured state file
  path cannot escape the state dir.

### 3.7 Install flow

`/buddy-install` runs `scripts/install.{sh,ps1}`, which:

1. Verifies Python ≥ 3.10 is on PATH. (`bleak` requires ≥ 3.8; 3.10 is
   our floor for `match` + `typing`.)
2. Creates a venv at `~/.tuya-pocket-buddy/venv`
   (`%LOCALAPPDATA%\tuya-pocket-buddy\venv` on Windows).
3. `pip install -e ./daemon` (or `pip install ./daemon` for release).
4. Merges `settings/hooks.json` into `~/.claude/settings.json`, backing
   the original up to `settings.json.buddy-backup-<ts>`.
5. Prints the next-step instructions: "Start Claude Code; run
   `/buddy-pair` to select your device."

Uninstall: `/buddy-stop` then manually remove the hook block and the
state directory. Documented in README.

## 4. Test contracts

Umbrella spec §3.3 mandates on-hardware verification; for the plugin
the "hardware" is the same T5AI-Pocket running **M1-A firmware**. But
the plugin also has pure-Python testable surface area — we exploit that.

### 4.1 Unit tests (pure Python, no hardware)

Under `daemon/tests/`, using `pytest`:

- `test_wire.py`:
  - Every encoder function round-trips through `json.loads` and matches
    the expected shape in `BLE_WIRE_PROTOCOL.md` §3.
  - Entries are truncated to 80 bytes UTF-8-safe (boundary tests on
    multi-byte characters).
  - `prompt.id` uses `secrets.token_hex` output length (20 hex chars).
- `test_permissions.py`:
  - Single-inflight rule: issuing a second prompt while one is pending
    resolves the first with `"deny"` and logs a warning.
  - Mismatched `id` in a BLE reply is discarded (no future resolution).
  - 35-s timeout fires and resolves with `"deny"`.
- `test_hook_router.py`:
  - Malformed hook payload → HTTP 400, no daemon crash.
  - Oversized payload (> 64 KB) → HTTP 413, no daemon crash.
  - `POST /hook` from a non-loopback Host header → HTTP 403.

Gate: `pytest daemon/tests` must pass with `-q` exit 0 before the
milestone can close.

### 4.2 Integration smoke (daemon + real firmware)

Executed manually on the developer machine (Windows primary; Linux/macOS
best-effort). Captured as
`docs/protocol/baseline/<sha>-m1c-smoke.log`:

1. Start Claude Code in a scratch workspace; verify hook file updated.
2. Flash M1-A firmware to the T5AI-Pocket.
3. Run `/buddy-pair`; select the detected `Claude_XXXX` device.
4. Run `/buddy-start`; daemon log shows `connected` and the device
   screen shows the host owner name within 3 s of connect.
5. Ask Claude Code to run a tool that triggers `PreToolUse`
   (e.g. `Read`). The device shows the prompt card.
6. Press ENTER on the device → Claude Code proceeds. Press LEFT → Claude
   Code aborts with a deny message. Press RIGHT → Claude Code proceeds
   and logs `permission:"always"`.
7. Verify the device's entries panel shows the last ≤ 8 tools invoked.
8. `/buddy-stop` — device shows "session ended", daemon exits.

Evidence: daemon log (`daemon.log`), screenshot or description of the
device panel at each step, plus the captured UART baseline log from the
firmware side (via `agent_target_tool.py`, exactly as in M0).

### 4.3 Protocol conformance

The daemon MUST generate frames byte-compatible with the M0 baseline.
Specifically, after M1-C lands, `grep -c '"prompt"' docs/protocol/baseline/
<sha>-m1c-smoke.log` ≥ 1 and the `id` field values match
`secrets.token_hex(10)` format (`^[0-9a-f]{20}$`).

## 5. Constraints

- Python ≥ 3.10, `bleak ≥ 0.22`.
- Windows-first install/lifecycle scripts tested end-to-end. POSIX
  scripts exist but carry a banner "**Untested in v1**".
- Plugin directory name and manifest keys MUST match m5-paper-buddy's
  conventions so a user who knows that plugin recognises this one.
- The plugin does **not** ship firmware. `/buddy-flash` is intentionally
  absent; flashing is documented as a separate, manual tos.py step to
  avoid tempting the plugin into embedding build tools.
- Static analysis: `ruff check daemon/` must pass with project defaults,
  and `mypy --strict daemon/tuya_pocket_buddy` must pass for files under
  the package.

## 6. Amendments

Reasons to revise this spec:
- **Protocol changes**: any rev of `BLE_WIRE_PROTOCOL.md` beyond v1.0
  invalidates `wire.py`'s encoders; bump plugin version + add a
  compatibility matrix.
- **OS tier promotion**: if Linux or macOS move from best-effort to
  first-class, install scripts need hardening and CI coverage added.
- **Daemon lifecycle change**: per-session is intentionally simple but
  fragile when the user opens many short-lived Claude Code sessions.
  If we see > 5 daemon starts per minute in telemetry, switch to
  persistent daemon (requires a new spec round).
