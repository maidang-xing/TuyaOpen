"""Claude Code hook event router.

Translates Claude Code hook JSON payloads into wire frames sent to the
device, and blocks on the permission bridge for ``PreToolUse`` events.

M2 additions:
  - Per-session tracking (SessionInfo): name from first prompt, token usage
    from ``Stop`` event usage field, running state.
  - Model detection from environment / Claude Code settings file.
  - Extended heartbeat: ``model`` + ``sessions`` list + ``mstats``.

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
            return value[:PROMPT_HINT_MAX_CHARS]
    try:
        return json.dumps(tool_input, ensure_ascii=False)[:PROMPT_HINT_MAX_CHARS]
    except Exception:
        return ""


def _format_entry(tool_name: str, payload: dict[str, Any]) -> str:
    hh_mm = time.strftime("%H:%M", time.localtime())
    hint = _short_hint(payload)
    return f"{hh_mm} {tool_name} {hint}" if hint else f"{hh_mm} {tool_name}"


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
        s = self._state
        sid = str(payload.get("session_id") or "")
        s.active = True
        s.total += 1
        s.msg = "session started"
        if not s.model:
            s.model = _detect_model()
        if sid:
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
        s = self._state
        sid = str(payload.get("session_id") or "")
        s.waiting += 1
        s.msg = "prompt received"
        prompt_text = str(payload.get("prompt") or "").strip()
        if sid and sid in s.session_map and not s.session_map[sid].name:
            s.session_map[sid].name = prompt_text.replace("\n", " ")[:wire.SESSION_NAME_MAX]
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
        tool_name = str(payload.get("tool_name") or "tool")
        s.entries.append(_format_entry(tool_name, payload))
        s.msg = f"done: {tool_name}"
        await self._emit_heartbeat()
        return {}

    async def _on_stop(self, payload: dict[str, Any]) -> dict[str, Any]:
        s = self._state
        sid = str(payload.get("session_id") or "")
        s.active = False
        s.waiting = 0
        s.running = 0
        s.msg = "session ended"

        # Track OUTPUT tokens only (matches REFERENCE.md definition)
        usage = payload.get("usage") or {}
        if isinstance(usage, dict):
            to = int(usage.get("output_tokens", 0))
            if to > 0:
                s.tokens += to
                s.tokens_today += to
                if sid and sid in s.session_map:
                    s.session_map[sid].tokens_out += to
                m_key = (
                    s.session_map[sid].model if (sid and sid in s.session_map) else ""
                ) or s.model or "unknown"
                s.model_usage[m_key] = s.model_usage.get(m_key, 0) + to

        if sid and sid in s.session_map:
            s.session_map[sid].is_running = False

        await self._emit_heartbeat()
        return {}


_EVENT_HANDLERS = {
    "SessionStart":      Router._on_session_start,
    "UserPromptSubmit":  Router._on_user_prompt_submit,
    "PreToolUse":        Router._on_pre_tool_use,
    "PostToolUse":       Router._on_post_tool_use,
    "Stop":              Router._on_stop,
}
