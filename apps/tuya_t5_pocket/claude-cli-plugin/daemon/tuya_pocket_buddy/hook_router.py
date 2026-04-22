"""Claude Code hook event router.

Translates Claude Code hook JSON payloads into wire frames sent to the
device, and blocks on the permission bridge for ``PreToolUse`` events.
"""

from __future__ import annotations

import logging
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Any, Protocol

from . import wire
from .permissions import PermissionBridge

log = logging.getLogger(__name__)

# How many recent tool invocations to keep on the device's entries panel.
ENTRIES_RING_CAP: int = 8

# Character budget for the one-line "hint" shown inside the prompt card.
PROMPT_HINT_MAX_CHARS: int = 60


class TxSink(Protocol):
    """Anything the router can push newline-terminated frames at."""

    async def send(self, line: bytes) -> None:
        ...  # pragma: no cover - Protocol signature only


@dataclass
class State:
    """Mutable in-memory snapshot mirrored to the device."""

    owner_name: str = ""
    active: bool = False
    total: int = 0
    running: int = 0
    waiting: int = 0
    tokens: int = 0
    tokens_today: int = 0
    msg: str = ""
    entries: deque[str] = field(
        default_factory=lambda: deque(maxlen=ENTRIES_RING_CAP)
    )


def _short_hint(payload: dict[str, Any]) -> str:
    """Render a compact one-line hint from a tool_input payload."""
    tool_input = payload.get("tool_input") or {}
    if not isinstance(tool_input, dict):
        return ""
    # Prefer the most common argument keys in Claude Code tools.
    for key in ("command", "file_path", "path", "url", "pattern", "query"):
        value = tool_input.get(key)
        if isinstance(value, str) and value:
            return value[:PROMPT_HINT_MAX_CHARS]
    # Fallback: compact JSON of tool_input.
    try:
        import json as _json

        return _json.dumps(tool_input, ensure_ascii=False)[:PROMPT_HINT_MAX_CHARS]
    except Exception:  # pragma: no cover - defensive
        return ""


def _format_entry(tool_name: str, payload: dict[str, Any]) -> str:
    """Compose a ring-buffer entry: ``HH:MM tool hint``."""
    hh_mm = time.strftime("%H:%M", time.localtime())
    hint = _short_hint(payload)
    if hint:
        return f"{hh_mm} {tool_name} {hint}"
    return f"{hh_mm} {tool_name}"


class Router:
    """Dispatches Claude Code hook events."""

    def __init__(
        self,
        state: State,
        tx: TxSink,
        permissions: PermissionBridge,
    ) -> None:
        self._state = state
        self._tx = tx
        self._perm = permissions

    async def _emit_heartbeat(
        self, prompt: dict[str, Any] | None = None
    ) -> None:
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
        )
        await self._tx.send(frame)

    async def route(
        self, event: str, payload: dict[str, Any]
    ) -> dict[str, Any]:
        """Route *event* with *payload*; return hook reply (may be empty)."""
        handler = _EVENT_HANDLERS.get(event)
        if handler is None:
            log.debug("router: ignoring unknown event=%s", event)
            return {}
        return await handler(self, payload)

    # --- Individual event handlers -------------------------------------------------

    async def _on_session_start(self, payload: dict[str, Any]) -> dict[str, Any]:
        s = self._state
        s.active = True
        s.total = max(1, s.total + 1)
        s.msg = "session started"
        if s.owner_name:
            await self._tx.send(wire.owner(s.owner_name))
        await self._emit_heartbeat()
        return {}

    async def _on_user_prompt_submit(
        self, payload: dict[str, Any]
    ) -> dict[str, Any]:
        s = self._state
        s.waiting += 1
        s.msg = "prompt received"
        await self._emit_heartbeat()
        return {}

    async def _on_pre_tool_use(
        self, payload: dict[str, Any]
    ) -> dict[str, Any]:
        import asyncio

        s = self._state
        tool_name = str(payload.get("tool_name") or "tool")
        hint = _short_hint(payload)
        s.running += 1
        s.msg = f"approve: {tool_name}"

        # Kick the bridge (which owns id allocation) and wait for the
        # prompt to register before emitting the heartbeat carrying it.
        ask_task = asyncio.create_task(self._perm.ask(tool_name, hint))
        for _ in range(200):
            if self._perm.current_id() is not None:
                break
            await asyncio.sleep(0.005)
        current_id = self._perm.current_id()
        if current_id is None:
            log.warning("router: permission bridge failed to register prompt")
            return await ask_task

        await self._emit_heartbeat(prompt={
            "id": current_id,
            "tool": tool_name,
            "hint": hint,
        })

        decision = await ask_task
        s.running = max(0, s.running - 1)
        s.msg = f"result: {decision.get('decision', 'deny')}"
        await self._emit_heartbeat()
        return decision

    async def _on_post_tool_use(
        self, payload: dict[str, Any]
    ) -> dict[str, Any]:
        s = self._state
        tool_name = str(payload.get("tool_name") or "tool")
        entry = _format_entry(tool_name, payload)
        s.entries.append(entry)
        s.msg = f"done: {tool_name}"
        await self._emit_heartbeat()
        return {}

    async def _on_stop(self, payload: dict[str, Any]) -> dict[str, Any]:
        s = self._state
        s.active = False
        s.waiting = 0
        s.running = 0
        s.msg = "session ended"
        await self._emit_heartbeat()
        return {}


_EVENT_HANDLERS = {
    "SessionStart": Router._on_session_start,
    "UserPromptSubmit": Router._on_user_prompt_submit,
    "PreToolUse": Router._on_pre_tool_use,
    "PostToolUse": Router._on_post_tool_use,
    "Stop": Router._on_stop,
}
