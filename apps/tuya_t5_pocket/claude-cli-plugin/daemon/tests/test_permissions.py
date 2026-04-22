"""Tests for ``tuya_pocket_buddy.permissions``."""

from __future__ import annotations

import asyncio
import logging

import pytest

from tuya_pocket_buddy.permissions import (
    DEFAULT_TIMEOUT_S,
    PermissionBridge,
)

pytestmark = pytest.mark.asyncio


async def test_ask_resolves_on_permission_frame() -> None:
    bridge = PermissionBridge()

    async def resolver() -> None:
        while bridge.current_id() is None:
            await asyncio.sleep(0)
        await bridge.handle_permission({"id": bridge.current_id(),
                                         "decision": "once"})

    decision_task = asyncio.create_task(bridge.ask("Read", "./foo"))
    resolver_task = asyncio.create_task(resolver())
    decision = await asyncio.wait_for(decision_task, timeout=1.0)
    await resolver_task
    assert decision == {"decision": "approve", "permanent": False}


async def test_ask_deny() -> None:
    bridge = PermissionBridge()

    async def resolver() -> None:
        while bridge.current_id() is None:
            await asyncio.sleep(0)
        await bridge.handle_permission({"id": bridge.current_id(),
                                         "decision": "deny"})

    decision_task = asyncio.create_task(bridge.ask("Bash", "rm -rf /tmp"))
    resolver_task = asyncio.create_task(resolver())
    decision = await asyncio.wait_for(decision_task, timeout=1.0)
    await resolver_task
    assert decision == {"decision": "deny"}


async def test_ask_always() -> None:
    bridge = PermissionBridge()

    async def resolver() -> None:
        while bridge.current_id() is None:
            await asyncio.sleep(0)
        await bridge.handle_permission({"id": bridge.current_id(),
                                         "decision": "always"})

    decision_task = asyncio.create_task(bridge.ask("Edit", "foo.c"))
    resolver_task = asyncio.create_task(resolver())
    decision = await asyncio.wait_for(decision_task, timeout=1.0)
    await resolver_task
    assert decision == {"decision": "approve", "permanent": True}


async def test_mismatched_id_discarded() -> None:
    bridge = PermissionBridge(timeout_s=0.2)
    decision_task = asyncio.create_task(bridge.ask("Read", "x"))
    # Wait until the prompt is registered.
    for _ in range(50):
        if bridge.current_id() is not None:
            break
        await asyncio.sleep(0.01)
    # Feed a mismatched id — should be ignored, future still pending.
    await bridge.handle_permission({"id": "bogus_id",
                                     "decision": "once"})
    decision = await decision_task
    # Since we didn't resolve with matching id, timeout path takes over.
    assert decision == {"decision": "deny"}


async def test_second_prompt_denies_first(
    caplog: pytest.LogCaptureFixture,
) -> None:
    caplog.set_level(logging.WARNING, logger="tuya_pocket_buddy.permissions")
    bridge = PermissionBridge()

    first_task = asyncio.create_task(bridge.ask("Read", "./one"))
    # Wait for first to register.
    for _ in range(50):
        if bridge.current_id() is not None:
            break
        await asyncio.sleep(0.01)
    first_id = bridge.current_id()

    async def resolve_second() -> None:
        while bridge.current_id() == first_id:
            await asyncio.sleep(0.005)
        await bridge.handle_permission({"id": bridge.current_id(),
                                         "decision": "once"})

    resolver = asyncio.create_task(resolve_second())
    second = await bridge.ask("Read", "./two")
    first_result = await first_task
    await resolver

    assert first_result == {"decision": "deny"}
    assert second == {"decision": "approve", "permanent": False}
    warned = any(
        "superseded" in rec.message or "supersedes" in rec.message
        for rec in caplog.records
    )
    assert warned


async def test_timeout_denies() -> None:
    bridge = PermissionBridge(timeout_s=0.05)
    result = await bridge.ask("Read", "./slow")
    assert result == {"decision": "deny"}


async def test_default_timeout_is_35s() -> None:
    assert DEFAULT_TIMEOUT_S == 35.0
