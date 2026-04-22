"""Permission bridge: Claude Code PreToolUse ↔ device button press.

Exactly one prompt may be pending at a time (mirrors the firmware's
"single in-flight prompt" contract in BLE_WIRE_PROTOCOL.md §5). A
``PreToolUse`` hook calls :meth:`PermissionBridge.ask` which awaits a
device-originated ``cmd:"permission"`` frame fed via
:meth:`PermissionBridge.handle_permission`. Timeouts resolve to
``deny`` so Claude Code aborts the tool call rather than hanging.
"""

from __future__ import annotations

import asyncio
import logging
import secrets
import time
from dataclasses import dataclass, field
from typing import Any

log = logging.getLogger(__name__)

# Leaves 5 s head-room inside Claude Code's 40 s hook budget.
DEFAULT_TIMEOUT_S: float = 35.0

# 20 hex characters — matches ``wire.heartbeat`` prompt.id format.
PROMPT_ID_BYTES: int = 10


@dataclass
class PendingPrompt:
    """In-flight permission request awaiting a device button press."""

    id: str
    tool: str
    hint: str
    future: asyncio.Future[dict[str, Any]]
    created_monotonic_ns: int = field(
        default_factory=lambda: time.monotonic_ns()
    )


def _decision_to_reply(decision: str) -> dict[str, Any]:
    """Translate device button decision → Claude Code hook reply."""
    if decision == "once":
        return {"decision": "approve", "permanent": False}
    if decision == "always":
        return {"decision": "approve", "permanent": True}
    # Treat anything else (including "deny" and unknown values) as deny,
    # per wire protocol §3.6 guidance.
    return {"decision": "deny"}


class PermissionBridge:
    """Coordinates Claude Code hook suspensions with device decisions."""

    def __init__(self, timeout_s: float = DEFAULT_TIMEOUT_S) -> None:
        self._timeout_s = float(timeout_s)
        self._lock = asyncio.Lock()
        self._current: PendingPrompt | None = None

    def current_id(self) -> str | None:
        """Return the id of the currently-pending prompt, or ``None``."""
        prompt = self._current
        return prompt.id if prompt is not None else None

    async def ask(self, tool: str, hint: str) -> dict[str, Any]:
        """Allocate a new pending prompt and await the decision.

        If another prompt is already pending it is superseded: its future
        resolves with ``{"decision":"deny"}`` and a warning is logged.
        """
        loop = asyncio.get_running_loop()
        future: asyncio.Future[dict[str, Any]] = loop.create_future()
        new_id = secrets.token_hex(PROMPT_ID_BYTES)

        async with self._lock:
            stale = self._current
            if stale is not None and not stale.future.done():
                log.warning(
                    "permission: new prompt supersedes pending id=%s", stale.id
                )
                stale.future.set_result({"decision": "deny"})
            self._current = PendingPrompt(
                id=new_id, tool=tool, hint=hint, future=future
            )

        try:
            result = await asyncio.wait_for(future, timeout=self._timeout_s)
            return result
        except asyncio.TimeoutError:
            log.info("permission: timeout id=%s after %.1fs", new_id,
                     self._timeout_s)
            return {"decision": "deny"}
        finally:
            async with self._lock:
                if self._current is not None and self._current.id == new_id:
                    self._current = None

    async def handle_permission(self, frame: dict[str, Any]) -> None:
        """Feed a device-originated ``cmd:"permission"`` frame.

        Frames whose ``id`` does not match the current pending prompt are
        silently discarded (protocol §5). Frames with an unknown
        ``decision`` are treated as ``deny``.
        """
        incoming_id = frame.get("id")
        decision = frame.get("decision")
        if not isinstance(incoming_id, str) or not isinstance(decision, str):
            log.debug("permission: malformed frame %r", frame)
            return
        async with self._lock:
            pending = self._current
            if pending is None or pending.id != incoming_id:
                log.debug("permission: id mismatch (incoming=%s)", incoming_id)
                return
            if pending.future.done():
                return
            pending.future.set_result(_decision_to_reply(decision))

    async def current_prompt_snapshot(self) -> tuple[str, str, str] | None:
        """Return ``(id, tool, hint)`` of the pending prompt, if any."""
        async with self._lock:
            if self._current is None:
                return None
            return (self._current.id, self._current.tool, self._current.hint)
