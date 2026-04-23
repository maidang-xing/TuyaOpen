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

Protocol note:
  tokens / tokens_today track OUTPUT tokens only, matching REFERENCE.md.
"""

from __future__ import annotations

import json
import logging
import os
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

from . import wire
from .permissions import PermissionBridge

log = logging.getLogger(__name__)

ENTRIES_RING_CAP: int = 8
PROMPT_HINT_MAX_CHARS: int = 60


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


def _find_session_jsonl(session_id: str) -> Path | None:
    """Locate ~/.claude/projects/*/<session_id>.jsonl, if it exists."""
    if not session_id:
        return None
    projects_dir = Path.home() / ".claude" / "projects"
    if not projects_dir.is_dir():
        return None
    for proj_dir in projects_dir.iterdir():
        if not proj_dir.is_dir():
            continue
        candidate = proj_dir / f"{session_id}.jsonl"
        if candidate.is_file():
            return candidate
    return None


def _read_last_usage(session_id: str) -> dict[str, Any] | None:
    """Return the usage dict from the most recent assistant message in the
    session JSONL, or None if unavailable.

    Reads only the last _JSONL_TAIL_BYTES so large files are cheap.
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
    # model_name -> [tokens_out]  (output tokens per model)
    model_usage: dict[str, int] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Payload builders
# ---------------------------------------------------------------------------

def _sessions_payload(state: State) -> list[dict[str, Any]] | None:
    """Build sessions list: running first, then by start time. Max 6."""
    if not state.session_map:
        return None
    sessions = sorted(
        state.session_map.values(),
        key=lambda s: (not s.is_running, s.started_at),
    )[:6]
    return [
        {
            "id": s.session_id[:11],
            "n":  s.name[:wire.SESSION_NAME_MAX],
            "m":  (s.model or state.model)[:wire.MODEL_MAX],
            "to": s.tokens_out,
            "r":  s.is_running,
        }
        for s in sessions
    ]


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
        )
        await self._tx.send(frame)

    async def tick(self) -> None:
        """Keepalive heartbeat from the periodic heartbeat task."""
        await self._emit_heartbeat()

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
        # Fallback: generate a stable enough ID so the session is always tracked
        if not sid:
            sid = "s-" + _sec.token_hex(4)
        s.active = True
        s.total += 1
        s.msg = "session started"
        if not s.model:
            s.model = _detect_model()
        if len(s.session_map) >= 6:
            oldest = min(
                (v for v in s.session_map.values() if not v.is_running),
                key=lambda v: v.started_at,
                default=None,
            )
            if oldest:
                del s.session_map[oldest.session_id]
        s.session_map[sid] = SessionInfo(session_id=sid, model=s.model, is_running=True)
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
        s.entries.append(_format_entry(tool_name, payload))
        s.msg = f"done: {tool_name}"
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
