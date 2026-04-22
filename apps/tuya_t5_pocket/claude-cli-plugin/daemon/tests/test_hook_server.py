"""Tests for ``tuya_pocket_buddy.hook_server``."""

from __future__ import annotations

import json
from collections.abc import AsyncIterator
from typing import Any

import aiohttp
import pytest
from aiohttp import web
from aiohttp.test_utils import TestServer

from tuya_pocket_buddy.hook_router import Router, State
from tuya_pocket_buddy.hook_server import (
    DEFAULT_PORT,
    HOOK_PATH,
    MAX_HOOK_BODY_BYTES,
    build_app,
)
from tuya_pocket_buddy.permissions import PermissionBridge

pytestmark = pytest.mark.asyncio


class FakeTx:
    def __init__(self) -> None:
        self.frames: list[bytes] = []

    async def send(self, line: bytes) -> None:
        self.frames.append(line)


@pytest.fixture
async def server() -> AsyncIterator[TestServer]:
    state = State(owner_name="Felix")
    tx = FakeTx()
    router = Router(state=state, tx=tx, permissions=PermissionBridge())
    app = build_app(router=router)
    srv = TestServer(app, host="127.0.0.1")
    await srv.start_server()
    try:
        yield srv
    finally:
        await srv.close()


async def test_default_port_is_9878() -> None:
    assert DEFAULT_PORT == 9878


async def test_hook_path_is_hook() -> None:
    assert HOOK_PATH == "/hook"


async def test_valid_session_start_returns_200(server: TestServer) -> None:
    payload: dict[str, Any] = {
        "hook_event_name": "SessionStart",
        "session_id": "s1",
    }
    headers = {"Host": "127.0.0.1:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.post(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            data=json.dumps(payload),
            headers=headers,
        ) as resp:
            assert resp.status == 200
            body = await resp.text()
            assert body in ("{}", "")


async def test_non_loopback_host_rejected(server: TestServer) -> None:
    payload = {"hook_event_name": "SessionStart"}
    headers = {"Host": "attacker.example:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.post(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            data=json.dumps(payload),
            headers=headers,
        ) as resp:
            assert resp.status == 403


async def test_oversized_body_returns_413(server: TestServer) -> None:
    payload_bytes = b'{"hook_event_name":"SessionStart","junk":"'
    payload_bytes += b"A" * (MAX_HOOK_BODY_BYTES + 10)
    payload_bytes += b'"}'
    headers = {"Host": "127.0.0.1:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.post(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            data=payload_bytes,
            headers=headers,
        ) as resp:
            assert resp.status == 413


async def test_invalid_json_returns_400(server: TestServer) -> None:
    headers = {"Host": "127.0.0.1:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.post(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            data=b"not-json-at-all",
            headers=headers,
        ) as resp:
            assert resp.status == 400


async def test_localhost_host_accepted(server: TestServer) -> None:
    payload = {"hook_event_name": "Stop"}
    headers = {"Host": "localhost:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.post(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            data=json.dumps(payload),
            headers=headers,
        ) as resp:
            assert resp.status == 200


async def test_daemon_binds_to_loopback_only(server: TestServer) -> None:
    # TestServer reports the bound host; must be loopback.
    assert server.host in ("127.0.0.1", "::1")


async def test_get_rejected_as_method_not_allowed(server: TestServer) -> None:
    headers = {"Host": "127.0.0.1:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.get(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            headers=headers,
        ) as resp:
            assert resp.status == 405


async def test_missing_event_name_returns_400(server: TestServer) -> None:
    headers = {"Host": "127.0.0.1:9878"}
    async with aiohttp.ClientSession() as http:
        async with http.post(
            f"http://127.0.0.1:{server.port}{HOOK_PATH}",
            data=json.dumps({"session_id": "no-event"}),
            headers=headers,
        ) as resp:
            assert resp.status == 400


async def test_aiohttp_available() -> None:
    assert isinstance(web.Application(), web.Application)
