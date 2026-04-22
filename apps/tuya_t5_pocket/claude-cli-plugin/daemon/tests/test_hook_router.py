"""Tests for ``tuya_pocket_buddy.hook_router``."""

from __future__ import annotations

import asyncio
import json
from typing import Any

import pytest

from tuya_pocket_buddy import wire
from tuya_pocket_buddy.hook_router import Router, State
from tuya_pocket_buddy.permissions import PermissionBridge

pytestmark = pytest.mark.asyncio


def _decode(line: bytes) -> dict[str, Any]:
    assert line.endswith(b"\n")
    return json.loads(line[:-1])


class FakeTx:
    def __init__(self) -> None:
        self.frames: list[bytes] = []

    async def send(self, line: bytes) -> None:
        self.frames.append(line)


async def test_session_start_emits_owner_and_heartbeat() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())

    response = await router.route("SessionStart",
                                   {"session_id": "s1", "cwd": "/tmp/x"})
    assert response == {}
    assert state.active is True
    assert len(tx.frames) == 2
    owner_frame = _decode(tx.frames[0])
    assert owner_frame == {"cmd": "owner", "name": "Felix"}
    heartbeat_frame = _decode(tx.frames[1])
    assert heartbeat_frame["total"] >= 1


async def test_user_prompt_submit_increments_waiting() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())
    await router.route("SessionStart", {"session_id": "s1"})
    tx.frames.clear()

    await router.route("UserPromptSubmit", {"session_id": "s1"})
    hb = _decode(tx.frames[-1])
    assert hb["waiting"] >= 1


async def test_pre_tool_use_blocks_on_permission() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    permissions = PermissionBridge(timeout_s=2.0)
    router = Router(state=state, tx=tx, permissions=permissions)
    await router.route("SessionStart", {"session_id": "s1"})
    tx.frames.clear()

    async def resolver() -> None:
        while permissions.current_id() is None:
            await asyncio.sleep(0.005)
        await permissions.handle_permission({
            "id": permissions.current_id(),
            "decision": "once",
        })

    resolver_task = asyncio.create_task(resolver())
    response = await router.route("PreToolUse",
                                   {"tool_name": "Read",
                                    "tool_input": {"file_path": "./foo"}})
    await resolver_task

    assert response["decision"] == "approve"
    assert response["permanent"] is False

    sent_heartbeat = None
    for frame in tx.frames:
        obj = _decode(frame)
        if "prompt" in obj:
            sent_heartbeat = obj
            break
    assert sent_heartbeat is not None
    assert sent_heartbeat["prompt"]["tool"] == "Read"
    assert len(sent_heartbeat["prompt"]["id"]) == 20


async def test_pre_tool_use_deny_passes_through() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    permissions = PermissionBridge(timeout_s=2.0)
    router = Router(state=state, tx=tx, permissions=permissions)
    await router.route("SessionStart", {"session_id": "s1"})

    async def resolver() -> None:
        while permissions.current_id() is None:
            await asyncio.sleep(0.005)
        await permissions.handle_permission({
            "id": permissions.current_id(),
            "decision": "deny",
        })

    resolver_task = asyncio.create_task(resolver())
    response = await router.route("PreToolUse",
                                   {"tool_name": "Bash",
                                    "tool_input": {"command": "rm -rf /"}})
    await resolver_task
    assert response == {"decision": "deny"}


async def test_post_tool_use_appends_entry() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())
    await router.route("SessionStart", {"session_id": "s1"})
    tx.frames.clear()

    await router.route("PostToolUse",
                        {"tool_name": "Read",
                         "tool_input": {"file_path": "./a.c"}})
    hb = _decode(tx.frames[-1])
    assert "entries" in hb
    assert len(hb["entries"]) >= 1
    for entry in hb["entries"]:
        assert len(entry.encode("utf-8")) <= wire.ENTRY_MAX_BYTES


async def test_post_tool_use_ring_caps_at_eight() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())
    await router.route("SessionStart", {"session_id": "s1"})

    for i in range(12):
        await router.route("PostToolUse",
                            {"tool_name": "Read",
                             "tool_input": {"file_path": f"./f{i}.c"}})
    hb = _decode(tx.frames[-1])
    assert len(hb["entries"]) == 8


async def test_stop_emits_session_ended() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())
    await router.route("SessionStart", {"session_id": "s1"})
    tx.frames.clear()

    response = await router.route("Stop", {"session_id": "s1"})
    assert response == {}
    hb = _decode(tx.frames[-1])
    assert hb["msg"] == "session ended"
    assert hb["waiting"] == 0
    assert hb["running"] == 0
    assert state.active is False


async def test_unknown_event_noop() -> None:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())
    response = await router.route("NotifyWeird", {})
    assert response == {}
    assert tx.frames == []
