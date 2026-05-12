"""Claude Code hook event router.

Translates Claude Code hook JSON payloads into wire frames sent to the
device, and blocks on the permission bridge for ``PreToolUse`` events.

M2 additions:
  - Per-session tracking (SessionInfo): name from first prompt, token usage
    from ``Stop`` event usage field, running state.
  - Model detection from environment / Claude Code settings file.
  - Extended heartbeat: ``model`` + ``sessions`` list + ``mstats``.

M3 additions:
  - Context window tracking: ctx_used / ctx_total / cache_write.
  - JSONL reader: parses ~/.claude/projects/*/<session_id>.jsonl to get
    per-call token usage (mirrors what ``/status`` shows in the CLI).

M4 additions:
  - Per-session project name (derived from cwd last path component).
  - Per-session local_entries: last 4 tool calls for session detail view.
  - Daily token history (28 days) from stats-cache.json dailyModelTokens.
  - Total / today cost (micro-USD) from stats-cache.json modelUsage.costUSD.
  - Claude version from ~/.claude/settings.json.

Protocol note:
  tokens / tokens_today track OUTPUT tokens only, matching REFERENCE.md.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import re
import shutil
import subprocess
import time
from collections import deque
from dataclasses import dataclass, field
from datetime import date, timedelta
from pathlib import Path
from typing import Any, Protocol

from . import wire
from .permissions import PermissionBridge

log = logging.getLogger(__name__)

ENTRIES_RING_CAP: int = 8
PROMPT_HINT_MAX_CHARS: int = 60

_claude_exe: str | None = None


def _resolve_claude_exe() -> str | None:
    """Find the absolute path of the ``claude`` CLI executable (cached)."""
    global _claude_exe  # noqa: PLW0603
    if _claude_exe is not None:
        return _claude_exe
    found = shutil.which("claude")
    if found:
        _claude_exe = found
        log.info("claude CLI resolved: %s", found)
    else:
        log.warning("claude CLI not found on PATH")
    return _claude_exe

# How many sessions to show in total (live + scanned)
SESSIONS_PAYLOAD_MAX: int = 12
# Max sessions to scan per project dir (limits I/O per tick)
SESSIONS_SCAN_PER_PROJECT: int = 4
# Head bytes to read for session name (first user prompt)
_JSONL_HEAD_BYTES: int = 4 * 1024


class TxSink(Protocol):
    async def send(self, line: bytes) -> None:
        ...  # pragma: no cover


# ---------------------------------------------------------------------------
# Model detection (best-effort; never raises)
# ---------------------------------------------------------------------------

def _detect_model() -> str:
    for var in ("ANTHROPIC_MODEL", "CLAUDE_MODEL"):
        v = os.environ.get(var, "").strip()
        if v:
            return v
    settings_path = Path.home() / ".claude" / "settings.json"
    try:
        data = json.loads(settings_path.read_text(encoding="utf-8"))
        m = data.get("model", "")
        if isinstance(m, str) and m:
            return m
    except Exception:
        pass
    return ""


def _model_ctx_size(model: str) -> int:
    """Return context window token limit inferred from model name."""
    m = model.lower()
    if "[1m]" in m or "-1m" in m:
        return 1_000_000
    if "200k" in m:
        return 200_000
    return 200_000  # conservative default


# ---------------------------------------------------------------------------
# JSONL reader — mirrors /status context window data
# ---------------------------------------------------------------------------

_JSONL_TAIL_BYTES = 16 * 1024  # read last 16 KB to find latest usage


def _read_stats_cache() -> dict[str, Any]:
    """Read ~/.claude/stats-cache.json — Claude Code's persisted usage totals.

    Returns the full parsed JSON dict, or {} if unavailable.
    """
    path = Path.home() / ".claude" / "stats-cache.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            return data
    except Exception:
        pass
    return {}


def _aggregate_stats_cache() -> dict[str, int]:
    """Sum all models in stats-cache and return totals as a flat dict."""
    totals: dict[str, int] = {
        "output_tokens": 0,
        "input_tokens": 0,
        "cache_read_input_tokens": 0,
        "cache_creation_input_tokens": 0,
    }
    cache = _read_stats_cache()
    for model_data in (cache.get("modelUsage") or {}).values():
        if not isinstance(model_data, dict):
            continue
        totals["output_tokens"]              += int(model_data.get("outputTokens", 0))
        totals["input_tokens"]               += int(model_data.get("inputTokens", 0))
        totals["cache_read_input_tokens"]    += int(model_data.get("cacheReadInputTokens", 0))
        totals["cache_creation_input_tokens"]+= int(model_data.get("cacheCreationInputTokens", 0))
    return totals


def _read_cost_totals() -> tuple[int, int]:
    """Return (cost_total_ucc, cost_today_ucc) in micro-USD from stats-cache.

    cost_today_ucc is derived from dailyModelTokens if today matches
    lastComputedDate; otherwise 0.
    """
    cache = _read_stats_cache()
    model_usage = cache.get("modelUsage") or {}
    total_usd = sum(
        float(m.get("costUSD", 0.0))
        for m in model_usage.values()
        if isinstance(m, dict)
    )
    cost_total_ucc = int(total_usd * 1_000_000)

    # For today's cost, check if lastComputedDate matches today and use the
    # most recent dailyModelTokens entry.
    cost_today_ucc = 0
    today_str = date.today().isoformat()
    last_date = cache.get("lastComputedDate", "")
    if last_date == today_str:
        daily_list = cache.get("dailyModelTokens") or []
        if daily_list and isinstance(daily_list[-1], dict):
            latest = daily_list[-1]
            if latest.get("date", "") == today_str:
                tokens_by_model = latest.get("tokensByModel") or {}
                # Approximate cost: $15/MTok output for average estimate.
                # This is a rough estimate; exact cost requires per-model pricing.
                today_tokens = sum(
                    int(v) for v in tokens_by_model.values()
                    if isinstance(v, (int, float))
                )
                cost_today_ucc = int(today_tokens * 15)

    return cost_total_ucc, cost_today_ucc


def _read_today_tokens() -> int:
    """Return total output tokens consumed today from dailyModelTokens.

    Sums all models in the entry whose ``date`` matches today.  Returns 0
    if the cache is stale or the entry is missing.
    """
    today_str = date.today().isoformat()
    cache = _read_stats_cache()
    for entry in reversed(cache.get("dailyModelTokens") or []):
        if not isinstance(entry, dict):
            continue
        if entry.get("date") == today_str:
            tokens_by_model = entry.get("tokensByModel") or {}
            return sum(
                int(v) for v in tokens_by_model.values()
                if isinstance(v, (int, float))
            )
    return 0


def _read_daily_tokens(days: int = 28) -> list[int]:
    """Return last ``days`` daily output token totals, index 0 = today.

    Reads dailyModelTokens from stats-cache.json and sums all models per day.
    Missing days are filled with 0.
    """
    cache = _read_stats_cache()
    daily_list = cache.get("dailyModelTokens") or []

    # Build a mapping date_str -> total_tokens from the cache
    date_map: dict[str, int] = {}
    for entry in daily_list:
        if not isinstance(entry, dict):
            continue
        d = entry.get("date", "")
        if not d:
            continue
        tokens_by_model = entry.get("tokensByModel") or {}
        total = sum(
            int(v) for v in tokens_by_model.values()
            if isinstance(v, (int, float))
        )
        date_map[d] = total

    # Generate last `days` dates, today first
    result: list[int] = []
    today = date.today()
    for i in range(days):
        d = (today - timedelta(days=i)).isoformat()
        result.append(date_map.get(d, 0))
    return result


def _detect_claude_version() -> str:
    """Detect Claude Code version from CLI.

    ``claude --version`` prints e.g. "2.1.121 (Claude Code)".
    Uses ``shutil.which`` to resolve the full path (required on Windows
    where ``.cmd`` wrappers are not found by ``subprocess`` without shell).
    """
    exe = _resolve_claude_exe()
    if not exe:
        return ""
    try:
        result = subprocess.run(
            [exe, "--version"],
            capture_output=True, text=True, timeout=5,
        )
        line = (result.stdout or "").strip().split("\n")[0]
        if line:
            return line[:wire.VERSION_MAX]
    except Exception:
        pass
    return ""


def _project_from_cwd(cwd: str) -> str:
    """Extract a short project name from the working directory path."""
    if not cwd:
        return ""
    # Take the last non-empty path component
    parts = cwd.replace("\\", "/").rstrip("/").split("/")
    name = next((p for p in reversed(parts) if p), "")
    return name[:wire.SESSION_PROJECT_MAX]


def _project_from_dir_name(dir_name: str) -> str:
    """Derive a human-readable project name from a .claude/projects/ dir name.

    Claude Code mangles the CWD into a directory name by replacing path
    separators with "-" and the drive colon-separator with "--".
    Example: "D:\\tuya_proj\\TuyaOpen" → "D--tuya-proj-TuyaOpen"

    Strategy: find the last CamelCase word (usually the project folder name),
    otherwise fall back to the last hyphen-separated segment.
    """
    # Strip drive letter prefix (before first "--")
    after_drive = dir_name.split("--", 1)[-1] if "--" in dir_name else dir_name
    # Prefer last CamelCase word (e.g. "TuyaOpen", "DuckyClaw")
    camel_words = re.findall(r"[A-Z][a-zA-Z0-9]+", after_drive)
    if camel_words:
        return camel_words[-1][:wire.SESSION_PROJECT_MAX]
    # Fallback: last segment after the last hyphen
    parts = after_drive.rsplit("-", 1)
    return parts[-1][:wire.SESSION_PROJECT_MAX] if parts[-1] else after_drive[:wire.SESSION_PROJECT_MAX]


def _read_session_name_from_jsonl(jsonl_path: Path) -> str:
    """Return the first real user prompt text from a JSONL session file.

    Reads the first _JSONL_HEAD_BYTES to keep this fast on large files.
    Skips system-injected context messages (those starting with '<').
    """
    try:
        with jsonl_path.open("rb") as fh:
            chunk = fh.read(_JSONL_HEAD_BYTES)
        for raw in chunk.split(b"\n"):
            raw = raw.strip()
            if not raw:
                continue
            try:
                entry = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if entry.get("type") != "user":
                continue
            msg = entry.get("message") or {}
            content = msg.get("content", "")
            if isinstance(content, str):
                text = content.strip()
                # Skip system-injected context messages (XML-like tags)
                if text and not text.startswith("<"):
                    return text.replace("\n", " ")[:wire.SESSION_NAME_MAX]
            elif isinstance(content, list):
                for block in content:
                    if not isinstance(block, dict):
                        continue
                    if block.get("type") != "text":
                        continue
                    text = (block.get("text") or "").strip()
                    # Skip system context blocks
                    if text and not text.startswith("<"):
                        return text.replace("\n", " ")[:wire.SESSION_NAME_MAX]
    except OSError:
        pass
    return ""


def _read_session_last_output_tokens(jsonl_path: Path) -> int:
    """Return output_tokens from the last assistant message in the JSONL."""
    try:
        with jsonl_path.open("rb") as fh:
            fh.seek(0, 2)
            size = fh.tell()
            fh.seek(max(0, size - _JSONL_TAIL_BYTES))
            chunk = fh.read()
        for raw in reversed(chunk.split(b"\n")):
            raw = raw.strip()
            if not raw:
                continue
            try:
                entry = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if entry.get("type") == "assistant":
                msg = entry.get("message") or {}
                usage = msg.get("usage") or {}
                to = usage.get("output_tokens", 0)
                if to:
                    return int(to)
    except OSError:
        pass
    return 0


def _scan_claude_sessions(
    exclude_ids: set[str],
    max_n: int = SESSIONS_PAYLOAD_MAX,
) -> list[dict[str, Any]]:
    """Scan ~/.claude/projects/ for recent sessions not in exclude_ids.

    Returns up to max_n session dicts sorted by file mtime (most recent first).
    Each dict matches the sessions[] heartbeat format.

    Reads only _JSONL_HEAD_BYTES + tail of each file for efficiency.
    Limits to SESSIONS_SCAN_PER_PROJECT files per project directory.
    """
    projects_dir = Path.home() / ".claude" / "projects"
    if not projects_dir.is_dir():
        return []

    found: list[tuple[float, dict[str, Any]]] = []

    try:
        proj_dirs = sorted(
            (p for p in projects_dir.iterdir() if p.is_dir()),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )
    except OSError:
        return []

    for proj_dir in proj_dirs:
        project_name = _project_from_dir_name(proj_dir.name)
        try:
            jsonl_files = sorted(
                proj_dir.glob("*.jsonl"),
                key=lambda f: f.stat().st_mtime,
                reverse=True,
            )
        except OSError:
            continue

        count = 0
        for jsonl_file in jsonl_files:
            if count >= SESSIONS_SCAN_PER_PROJECT:
                break
            session_id = jsonl_file.stem
            # Skip non-UUID-looking filenames
            if len(session_id) < 8 or " " in session_id:
                count += 1
                continue
            # Skip sessions already tracked live by hooks
            if session_id in exclude_ids or session_id[:11] in exclude_ids:
                count += 1
                continue
            try:
                mtime = jsonl_file.stat().st_mtime
                name = _read_session_name_from_jsonl(jsonl_file)
                tokens_out = _read_session_last_output_tokens(jsonl_file)
                found.append((mtime, {
                    "id": session_id[:11],
                    "n":  name or "",
                    "m":  "",
                    "to": tokens_out,
                    "r":  False,
                    "p":  project_name,
                }))
            except Exception:
                pass
            count += 1

        # Early-exit if we already have plenty of candidates
        if len(found) >= max_n * 3:
            break

    found.sort(key=lambda t: t[0], reverse=True)
    return [d for _, d in found[:max_n]]


def _find_session_jsonl(session_id: str) -> Path | None:
    """Locate ~/.claude/projects/*/<session_id>.jsonl, if it exists.

    Supports both full UUIDs and 11-char short-id prefix matching.
    """
    if not session_id:
        return None
    projects_dir = Path.home() / ".claude" / "projects"
    if not projects_dir.is_dir():
        return None
    for proj_dir in projects_dir.iterdir():
        if not proj_dir.is_dir():
            continue
        # Try exact match first.
        candidate = proj_dir / f"{session_id}.jsonl"
        if candidate.is_file():
            return candidate
    # Prefix match (11-char short id).
    if len(session_id) < 36:
        for proj_dir in projects_dir.iterdir():
            if not proj_dir.is_dir():
                continue
            try:
                for f in proj_dir.iterdir():
                    if f.suffix == ".jsonl" and f.stem.startswith(session_id):
                        return f
            except OSError:
                continue
    return None


def _read_last_usage(session_id: str) -> dict[str, Any] | None:
    """Return the usage dict from the most recent assistant message in the
    session JSONL, or None if unavailable.

    Reads only the last _JSONL_TAIL_BYTES so large files are cheap.
    This gives us the CURRENT context window size (total input tokens for
    the most recent API call = what /status shows as "context used").
    """
    jsonl = _find_session_jsonl(session_id)
    if jsonl is None:
        return None
    try:
        with jsonl.open("rb") as fh:
            fh.seek(0, 2)
            size = fh.tell()
            fh.seek(max(0, size - _JSONL_TAIL_BYTES))
            chunk = fh.read()
        last_usage: dict[str, Any] | None = None
        for raw in chunk.split(b"\n"):
            raw = raw.strip()
            if not raw:
                continue
            try:
                entry = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if entry.get("type") == "assistant":
                msg = entry.get("message")
                if isinstance(msg, dict):
                    u = msg.get("usage")
                    if isinstance(u, dict) and u:
                        last_usage = u
        return last_usage
    except OSError:
        return None


# ---------------------------------------------------------------------------
# Per-session snapshot
# ---------------------------------------------------------------------------

@dataclass
class SessionInfo:
    session_id: str
    name: str = ""
    model: str = ""
    tokens_out: int = 0       # output tokens only (REFERENCE.md)
    is_running: bool = False
    started_at: float = field(default_factory=time.time)
    project: str = ""                              # cwd last component (M4)
    local_entries: list[str] = field(default_factory=list)  # last 4 tool entries (M4)


# ---------------------------------------------------------------------------
# Router state
# ---------------------------------------------------------------------------

@dataclass
class State:
    owner_name: str = ""
    active: bool = False
    total: int = 0
    running: int = 0
    waiting: int = 0
    tokens: int = 0           # cumulative output tokens (REFERENCE.md)
    tokens_today: int = 0     # output tokens since local midnight
    tokens_in: int = 0        # cumulative input tokens
    tokens_in_today: int = 0  # input tokens since local midnight
    cache_read: int = 0       # cumulative cache_read_input_tokens
    cache_write: int = 0      # cumulative cache_creation_input_tokens
    ctx_used: int = 0         # current context window used (last API call total input)
    ctx_total: int = 0        # model context window size
    msg: str = ""
    model: str = field(default_factory=_detect_model)
    entries: deque[str] = field(
        default_factory=lambda: deque(maxlen=ENTRIES_RING_CAP)
    )
    session_map: dict[str, SessionInfo] = field(default_factory=dict)
    model_usage: dict[str, int] = field(default_factory=dict)
    # M4: extended stats
    claude_version: str = ""
    cost_today_ucc: int = 0
    cost_total_ucc: int = 0
    daily_tokens: list[int] = field(default_factory=lambda: [0] * 28)
    # M5: sessions scanned from ~/.claude/projects/ (filled by tick())
    scanned_sessions: list[dict[str, Any]] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Payload builders
# ---------------------------------------------------------------------------

def _sessions_payload(state: State) -> list[dict[str, Any]] | None:
    """Build sessions list: live hook-tracked first, then .claude/ scanned.

    Total capped at SESSIONS_PAYLOAD_MAX (12). Running sessions always first.
    Scanned sessions fill the remaining slots, deduplicated by 11-char ID prefix.
    """
    result: list[dict[str, Any]] = []
    seen_ids: set[str] = set()

    # 1. Live sessions from hook tracking (running first)
    live = sorted(
        state.session_map.values(),
        key=lambda s: (not s.is_running, s.started_at),
    )
    for s in live:
        if len(result) >= SESSIONS_PAYLOAD_MAX:
            break
        sid11 = s.session_id[:11]
        seen_ids.add(sid11)
        seen_ids.add(s.session_id)
        result.append({
            "id": sid11,
            "n":  s.name[:wire.SESSION_NAME_MAX],
            "m":  (s.model or state.model)[:wire.MODEL_MAX],
            "to": s.tokens_out,
            "r":  s.is_running,
            **( {"p": s.project[:wire.SESSION_PROJECT_MAX]} if s.project else {} ),
            **( {"e": s.local_entries[-4:]} if s.local_entries else {} ),
        })

    # 2. Scanned historical sessions to fill remaining slots
    for scanned in state.scanned_sessions:
        if len(result) >= SESSIONS_PAYLOAD_MAX:
            break
        sid = scanned.get("id", "")
        if sid in seen_ids:
            continue
        seen_ids.add(sid)
        result.append(scanned)

    return result if result else None


def _mstats_payload(state: State) -> list[dict[str, Any]] | None:
    """Per-model output token stats, descending by tokens. Max 4."""
    if not state.model_usage:
        return None
    items = sorted(state.model_usage.items(), key=lambda kv: kv[1], reverse=True)
    return [
        {"m": m[:wire.MODEL_MAX], "to": v}
        for m, v in items[:4]
    ]


def _short_hint(payload: dict[str, Any]) -> str:
    tool_input = payload.get("tool_input") or {}
    if not isinstance(tool_input, dict):
        return ""
    for key in ("command", "file_path", "path", "url", "pattern", "query"):
        value = tool_input.get(key)
        if isinstance(value, str) and value:
            # Strip newlines so the hint stays on one line on the device display
            single = value.replace("\r\n", " ").replace("\n", " ").replace("\r", " ")
            return single[:PROMPT_HINT_MAX_CHARS]
    try:
        raw = json.dumps(tool_input, ensure_ascii=False)
        return raw.replace("\n", " ")[:PROMPT_HINT_MAX_CHARS]
    except Exception:
        return ""


def _format_entry(tool_name: str, payload: dict[str, Any]) -> str:
    hh_mm = time.strftime("%H:%M", time.localtime())
    hint = _short_hint(payload)
    return f"{hh_mm} {tool_name} {hint}" if hint else f"{hh_mm} {tool_name}"


def _apply_jsonl_usage(s: State, usage: dict[str, Any]) -> None:
    """Update ctx_used/ctx_total from a JSONL assistant message usage dict.

    Only updates if the computed ctx is larger than what hooks already
    provided (hooks are authoritative; JSONL is a supplement).
    """
    ti = int(usage.get("input_tokens", 0))
    cr = int(usage.get("cache_read_input_tokens", 0))
    cc = int(usage.get("cache_creation_input_tokens", 0))
    ctx_now = ti + cr + cc
    if ctx_now > s.ctx_used:
        s.ctx_used = ctx_now
        s.ctx_total = _model_ctx_size(s.model)
    log.debug("ctx from jsonl: %d / %d", ctx_now, s.ctx_total)


# ---------------------------------------------------------------------------
# Router
# ---------------------------------------------------------------------------

class Router:
    def __init__(self, state: State, tx: TxSink, permissions: PermissionBridge) -> None:
        self._state = state
        self._tx = tx
        self._perm = permissions
        self._tick_count: int = 0

    async def _emit_heartbeat(self, prompt: dict[str, Any] | None = None) -> None:
        s = self._state
        frame = wire.heartbeat(
            total=max(1, s.total),
            running=s.running,
            waiting=s.waiting,
            tokens=s.tokens,
            tokens_today=s.tokens_today,
            tokens_in=s.tokens_in,
            tokens_in_today=s.tokens_in_today,
            cache_read=s.cache_read,
            cache_write=s.cache_write,
            ctx_used=s.ctx_used,
            ctx_total=s.ctx_total,
            msg=s.msg,
            entries=list(s.entries) if s.entries else None,
            prompt=prompt,
            model=s.model or None,
            sessions=_sessions_payload(s),
            mstats=_mstats_payload(s),
            claude_ver=s.claude_version or None,
            cost_today_ucc=s.cost_today_ucc,
            cost_total_ucc=s.cost_total_ucc,
            daily_tokens=s.daily_tokens if any(s.daily_tokens) else None,
        )
        await self._tx.send(frame)

    async def tick(self) -> None:
        """Keepalive heartbeat (every 10 s).

        Refreshes ctx_used from JSONL for every running session, and every
        ~60 s also re-reads stats-cache.json to pick up cumulative totals
        from sessions that ended while we weren't watching (e.g. after a
        daemon restart). Also refreshes M4 extended stats (daily tokens,
        cost, Claude version) on the same ~60s cadence.
        """
        s = self._state
        self._tick_count += 1

        # Re-read cumulative totals from stats-cache every ~60 s (6 ticks).
        if self._tick_count % 6 == 0:
            cache = _aggregate_stats_cache()
            if cache["output_tokens"] > s.tokens:
                s.tokens = cache["output_tokens"]
            if cache["cache_read_input_tokens"] > s.cache_read:
                s.cache_read = cache["cache_read_input_tokens"]
            if cache["cache_creation_input_tokens"] > s.cache_write:
                s.cache_write = cache["cache_creation_input_tokens"]
            today_tok = _read_today_tokens()
            if today_tok > s.tokens_today:
                s.tokens_today = today_tok
            log.debug("tick: refreshed stats-cache: out=%d today=%d r=%d w=%d",
                      s.tokens, s.tokens_today, s.cache_read, s.cache_write)

            # M4: refresh extended stats
            s.cost_total_ucc, s.cost_today_ucc = _read_cost_totals()
            s.daily_tokens = _read_daily_tokens(28)
            if not s.claude_version:
                s.claude_version = _detect_claude_version()

            # M5: scan .claude/projects/ for historical sessions
            live_ids: set[str] = set(s.session_map.keys())
            for sid in list(s.session_map.keys()):
                live_ids.add(sid[:11])
            s.scanned_sessions = _scan_claude_sessions(
                exclude_ids=live_ids,
                max_n=SESSIONS_PAYLOAD_MAX,
            )
            log.debug("tick: scanned %d historical sessions", len(s.scanned_sessions))

        # Refresh ctx_used from JSONL for every running session.
        for sid, info in s.session_map.items():
            if info.is_running:
                usage = _read_last_usage(sid)
                if usage:
                    _apply_jsonl_usage(s, usage)

        await self._emit_heartbeat()

    async def handle_hb_request(self, page: str = "") -> None:
        """Device-pull request for a fresh heartbeat snapshot.

        Tuya firmware sends ``{"cmd":"hb_req","page":"<name>"}`` whenever
        the user switches to a screen that needs server-side data, so the
        UI doesn't have to wait up to 10 s for the next keepalive push.

        ``page`` is advisory; we currently emit the full snapshot for any
        page since the marginal cost of extra fields is negligible compared
        with one BLE write. Future optimisation could send page-targeted
        partials (chart-only / pie-only) once the firmware learns to
        accept them.

        Compatibility: the official Anthropic Claude Desktop never sends
        ``cmd:hb_req`` from the device, so this is a Tuya-only extension
        invisible to non-Tuya peers (REFERENCE.md §3.4 lists ``hb_req``
        as an optional command that desktop apps may ignore).
        """
        log.debug("rx: hb_req page=%s", page or "<none>")
        await self._emit_heartbeat()

    async def handle_asr(self, text: str, sid: str) -> None:
        """Route a device-originated ASR transcript to the matching session.

        ``sid`` is the 11-char short id used by the firmware UI; we resolve it
        back to the full session id by prefix-matching ``state.session_map``
        (live sessions) or by scanning ``~/.claude/projects/`` for JSONL files.

        Injection: launches ``claude --resume <full_sid> -p "text" --print``
        as a detached subprocess so the ASR text becomes a real user message
        in the target Claude Code session.  Falls back to persistence-only
        when the full session id cannot be resolved.

        Persistence: also appends a JSON Lines record to
        ``~/.claude/buddy-asr/<sid>.jsonl`` for audit/replay.

        Never raises: subprocess and I/O errors are logged at debug level and
        swallowed so a flaky home directory cannot stall the BLE RX pump.
        """
        if not text:
            return
        s = self._state
        # Resolve short sid → full sid via live session map.
        full_sid = ""
        if sid:
            for key in s.session_map.keys():
                if key.startswith(sid):
                    full_sid = key
                    break
        # If not found in live sessions, try scanning JSONL files.
        if not full_sid and sid:
            found = _find_session_jsonl(sid)
            if found is not None:
                full_sid = found.stem
        target = full_sid or (sid or "unknown")

        # Reflect the latest transcript in the heartbeat msg field.
        s.msg = ("asr: " + text)[:wire.ENTRY_MAX_BYTES]
        if full_sid and full_sid in s.session_map:
            entry = f"{time.strftime('%H:%M', time.localtime())} ASR {text}"
            info = s.session_map[full_sid]
            info.local_entries.append(entry[:wire.SESSION_ENTRY_MAX])
            if len(info.local_entries) > 4:
                info.local_entries = info.local_entries[-4:]
            s.entries.append(entry[:wire.ENTRY_MAX_BYTES])

        # Persist transcript to ~/.claude/buddy-asr/
        try:
            asr_dir = Path.home() / ".claude" / "buddy-asr"
            asr_dir.mkdir(parents=True, exist_ok=True)
            safe = re.sub(r"[^A-Za-z0-9._-]", "_", target)[:64] or "unknown"
            out_path = asr_dir / f"{safe}.jsonl"
            record = json.dumps(
                {"ts": int(time.time()), "text": text, "sid": target},
                ensure_ascii=False,
            )
            with out_path.open("a", encoding="utf-8") as fh:
                fh.write(record + "\n")
        except OSError as exc:
            log.debug("asr: failed to persist transcript: %s", exc)

        # Inject ASR text into the Claude session via async subprocess.
        if full_sid:
            asyncio.ensure_future(self._inject_asr_subprocess(full_sid, text))
        else:
            log.debug("asr: no full session id resolved for sid=%s", sid)

        await self._emit_heartbeat()

    async def _inject_asr_subprocess(self, full_sid: str, text: str) -> None:
        """Launch ``claude --resume <sid> -p <text>`` and log result."""
        exe = _resolve_claude_exe()
        if not exe:
            log.warning("asr: cannot inject — claude CLI not on PATH")
            return
        try:
            log.info("asr: injecting into session %s: %s",
                     full_sid[:11], text[:60])
            proc = await asyncio.create_subprocess_exec(
                exe, "--resume", full_sid, "-p", text,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.PIPE,
                stdin=asyncio.subprocess.DEVNULL,
            )
            _, stderr = await asyncio.wait_for(proc.communicate(), timeout=30)
            if proc.returncode != 0:
                err = stderr.decode(errors="replace").strip() if stderr else ""
                log.warning("asr: claude --resume exit=%d stderr=%s",
                            proc.returncode, err[:200])
            else:
                log.info("asr: injection ok for session %s", full_sid[:11])
        except asyncio.TimeoutError:
            log.warning("asr: claude --resume timed out (30s)")
            try:
                proc.kill()
            except Exception:
                pass
        except Exception as exc:
            log.warning("asr: subprocess failed: %s", exc)

    async def route(self, event: str, payload: dict[str, Any]) -> dict[str, Any]:
        handler = _EVENT_HANDLERS.get(event)
        if handler is None:
            log.debug("router: ignoring unknown event=%s", event)
            return {}
        return await handler(self, payload)

    # --- Event handlers ------------------------------------------------------

    async def _on_session_start(self, payload: dict[str, Any]) -> dict[str, Any]:
        import secrets as _sec
        s = self._state
        sid = str(payload.get("session_id") or "")
        if not sid:
            sid = "s-" + _sec.token_hex(4)
        s.active = True
        s.total += 1
        s.msg = "session started"
        if not s.model:
            s.model = _detect_model()

        # Seed cumulative totals from stats-cache.json on first session so
        # we reflect all historical usage, not just what the daemon has seen.
        if s.total == 1:
            cache = _aggregate_stats_cache()
            if cache["output_tokens"] > s.tokens:
                s.tokens = cache["output_tokens"]
            today_tok = _read_today_tokens()
            if today_tok > s.tokens_today:
                s.tokens_today = today_tok
            if cache["cache_read_input_tokens"] > s.cache_read:
                s.cache_read = cache["cache_read_input_tokens"]
            if cache["cache_creation_input_tokens"] > s.cache_write:
                s.cache_write = cache["cache_creation_input_tokens"]
            s.ctx_total = _model_ctx_size(s.model)
            log.debug("seeded from stats-cache: out=%d cache_r=%d cache_w=%d",
                      s.tokens, s.cache_read, s.cache_write)
            # M4: seed extended stats on first session
            s.cost_total_ucc, s.cost_today_ucc = _read_cost_totals()
            s.daily_tokens = _read_daily_tokens(28)
            if not s.claude_version:
                s.claude_version = _detect_claude_version()
            # Seed per-model usage from stats-cache so pie chart shows data immediately
            cache = _read_stats_cache()
            model_usage_raw = cache.get("modelUsage") or {}
            for mname, mdata in model_usage_raw.items():
                if not isinstance(mdata, dict):
                    continue
                out_tok = int(mdata.get("outputTokens", 0))
                if out_tok > 0:
                    short_name = mname[:wire.MODEL_MAX]
                    s.model_usage[short_name] = (
                        s.model_usage.get(short_name, 0) + out_tok
                    )
            log.debug("seeded model_usage from stats-cache: %d models",
                      len(s.model_usage))
            # M5: initial scan for historical sessions on first session start
            s.scanned_sessions = _scan_claude_sessions(
                exclude_ids={sid, sid[:11]},
                max_n=SESSIONS_PAYLOAD_MAX,
            )
            log.debug("seeded %d historical sessions from .claude/", len(s.scanned_sessions))

        if len(s.session_map) >= 6:
            oldest = min(
                (v for v in s.session_map.values() if not v.is_running),
                key=lambda v: v.started_at,
                default=None,
            )
            if oldest:
                del s.session_map[oldest.session_id]

        # M4: derive project name from cwd
        cwd = str(payload.get("cwd") or "")
        project = _project_from_cwd(cwd)

        s.session_map[sid] = SessionInfo(
            session_id=sid, model=s.model, is_running=True, project=project
        )
        if s.owner_name:
            await self._tx.send(wire.owner(s.owner_name))
        await self._emit_heartbeat()
        return {}

    async def _on_user_prompt_submit(self, payload: dict[str, Any]) -> dict[str, Any]:
        import secrets as _sec
        s = self._state
        sid = str(payload.get("session_id") or "")
        s.waiting += 1
        s.msg = "prompt received"

        prompt_text = str(payload.get("prompt") or "").strip()

        # Create session entry if missing (SessionStart may have been missed)
        if sid and sid not in s.session_map:
            if not s.model:
                s.model = _detect_model()
            s.session_map[sid] = SessionInfo(
                session_id=sid, model=s.model, is_running=True
            )
            s.total = max(s.total, len(s.session_map))
        elif not sid:
            # No session_id at all — still record a session so UI shows something
            sid = "s-" + _sec.token_hex(4)
            s.session_map[sid] = SessionInfo(
                session_id=sid, model=s.model or _detect_model(), is_running=True
            )
            s.total = max(s.total, len(s.session_map))

        # Capture first prompt as session name
        if sid in s.session_map and not s.session_map[sid].name and prompt_text:
            clean = prompt_text.replace("\n", " ")[:wire.SESSION_NAME_MAX]
            s.session_map[sid].name = clean

        await self._emit_heartbeat()
        return {}

    async def _on_pre_tool_use(self, payload: dict[str, Any]) -> dict[str, Any]:
        import asyncio
        s = self._state
        sid = str(payload.get("session_id") or "")
        tool_name = str(payload.get("tool_name") or "tool")
        hint = _short_hint(payload)
        s.running += 1
        s.msg = f"approve: {tool_name}"
        if sid and sid in s.session_map:
            s.session_map[sid].is_running = True

        ask_task = asyncio.create_task(self._perm.ask(tool_name, hint))
        for _ in range(200):
            if self._perm.current_id() is not None:
                break
            await asyncio.sleep(0.005)
        current_id = self._perm.current_id()
        if current_id is None:
            log.warning("router: permission bridge failed to register prompt")
            return await ask_task

        await self._emit_heartbeat(prompt={"id": current_id, "tool": tool_name, "hint": hint})
        decision = await ask_task
        s.running = max(0, s.running - 1)
        s.msg = f"result: {decision.get('decision', 'deny')}"
        await self._emit_heartbeat()
        return decision

    async def _on_post_tool_use(self, payload: dict[str, Any]) -> dict[str, Any]:
        s = self._state
        sid = str(payload.get("session_id") or "")
        tool_name = str(payload.get("tool_name") or "tool")
        entry = _format_entry(tool_name, payload)
        s.entries.append(entry)
        s.msg = f"done: {tool_name}"
        # M4: append to per-session local entries (keep last 4)
        if sid and sid in s.session_map:
            info = s.session_map[sid]
            info.local_entries.append(entry)
            if len(info.local_entries) > 4:
                info.local_entries = info.local_entries[-4:]
        # Refresh context window stats from JSONL after each tool use.
        usage = _read_last_usage(sid)
        if usage:
            _apply_jsonl_usage(s, usage)
        await self._emit_heartbeat()
        return {}

    async def _on_stop(self, payload: dict[str, Any]) -> dict[str, Any]:
        s = self._state
        sid = str(payload.get("session_id") or "")
        s.active = False
        s.waiting = 0
        s.running = 0
        s.msg = "session ended"

        # Claude Code may put usage at top-level, under "usage", or inside
        # the final assistant message.  Try all known locations.
        usage: dict[str, Any] = {}
        for candidate in (
            payload.get("usage"),
            (payload.get("message") or {}).get("usage"),
            next(
                (
                    e.get("usage")
                    for e in reversed(payload.get("transcript") or [])
                    if isinstance(e, dict) and e.get("usage")
                ),
                None,
            ),
        ):
            if isinstance(candidate, dict) and candidate:
                usage = candidate
                break

        to = int(usage.get("output_tokens", 0))
        ti = int(usage.get("input_tokens", 0))
        cr = int(usage.get("cache_read_input_tokens", 0))
        cc = int(usage.get("cache_creation_input_tokens", 0))
        if to > 0:
            s.tokens += to
            s.tokens_today += to
            if sid and sid in s.session_map:
                s.session_map[sid].tokens_out += to
            m_key = (
                s.session_map[sid].model if (sid and sid in s.session_map) else ""
            ) or s.model or "unknown"
            s.model_usage[m_key] = s.model_usage.get(m_key, 0) + to
        if ti > 0:
            s.tokens_in += ti
            s.tokens_in_today += ti
        if cr > 0:
            s.cache_read += cr
        if cc > 0:
            s.cache_write += cc
        # ctx_used = total input tokens for this API call
        ctx_now = ti + cr + cc
        if ctx_now > 0:
            s.ctx_used = ctx_now
            s.ctx_total = _model_ctx_size(s.model)

        if sid and sid in s.session_map:
            s.session_map[sid].is_running = False

        # Also refresh ctx from JSONL as a fallback / cross-check
        jsonl_usage = _read_last_usage(sid)
        if jsonl_usage:
            _apply_jsonl_usage(s, jsonl_usage)

        await self._emit_heartbeat()
        return {}


_EVENT_HANDLERS = {
    "SessionStart":      Router._on_session_start,
    "UserPromptSubmit":  Router._on_user_prompt_submit,
    "PreToolUse":        Router._on_pre_tool_use,
    "PostToolUse":       Router._on_post_tool_use,
    "Stop":              Router._on_stop,
}
