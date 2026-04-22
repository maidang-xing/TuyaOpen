"""Tests for ``tuya_pocket_buddy.ble_client``.

No hardware: the ``bleak`` surface is mocked. These tests exercise the
daemon-side logic (name matching, frame reassembly, MTU chunking,
reconnect backoff) against in-memory doubles.
"""

from __future__ import annotations

import asyncio
import re
from dataclasses import dataclass

from tuya_pocket_buddy.ble_client import (
    CLAUDE_NAME_RE,
    BleClient,
    compute_backoff_schedule,
    pick_candidate,
    reassemble_lines,
    tx_chunks,
)


@dataclass
class FakeAd:
    """Stand-in for a BleakScanner advertisement tuple entry."""

    name: str | None
    address: str


def test_claude_name_pattern() -> None:
    assert re.fullmatch(CLAUDE_NAME_RE, "Claude_A1B2")
    assert re.fullmatch(CLAUDE_NAME_RE, "Claude_abcd")
    assert not re.fullmatch(CLAUDE_NAME_RE, "SomeOtherDevice")
    assert not re.fullmatch(CLAUDE_NAME_RE, "Claude")


def test_pick_candidate_filters_name() -> None:
    ads = [
        FakeAd(name="AirPods", address="AA:BB:CC:DD:EE:01"),
        FakeAd(name="Claude_A1B2", address="AA:BB:CC:DD:EE:02"),
        FakeAd(name=None, address="AA:BB:CC:DD:EE:03"),
    ]
    matches = pick_candidate(ads)
    assert len(matches) == 1
    assert matches[0].address == "AA:BB:CC:DD:EE:02"


def test_pick_candidate_multiple_claudes() -> None:
    ads = [
        FakeAd(name="Claude_A1B2", address="AA:01"),
        FakeAd(name="Claude_XY99", address="AA:02"),
    ]
    matches = pick_candidate(ads)
    assert len(matches) == 2


def test_reassemble_lines_joins_fragments() -> None:
    state = {"buf": b""}
    out1 = reassemble_lines(state, b'{"ack":"own')
    assert out1 == []
    out2 = reassemble_lines(state, b'er","ok":true}\n')
    assert out2 == [b'{"ack":"owner","ok":true}']
    assert state["buf"] == b""


def test_reassemble_lines_multiple_in_one_chunk() -> None:
    state = {"buf": b""}
    out = reassemble_lines(state, b'{"a":1}\n{"b":2}\ntail')
    assert out == [b'{"a":1}', b'{"b":2}']
    assert state["buf"] == b"tail"


def test_reassemble_lines_caps_buffer() -> None:
    state = {"buf": b""}
    # No newline, massive buffer — must be discarded once over cap.
    big = b"x" * (9 * 1024)
    out = reassemble_lines(state, big)
    assert out == []
    # Buffer reset to protect memory.
    assert len(state["buf"]) <= 8 * 1024


def test_tx_chunks_to_mtu_minus_3() -> None:
    frame = b"A" * 100
    chunks = list(tx_chunks(frame, mtu=23))
    assert len(chunks) == 5
    for chunk in chunks:
        assert len(chunk) <= 20
    assert b"".join(chunks) == frame


def test_tx_chunks_larger_mtu() -> None:
    frame = b"A" * 100
    chunks = list(tx_chunks(frame, mtu=185))
    assert len(chunks) == 1
    assert chunks[0] == frame


def test_tx_chunks_empty() -> None:
    assert list(tx_chunks(b"", mtu=23)) == []


def test_tx_chunks_honours_hard_cap() -> None:
    frame = b"A" * 500
    chunks = list(tx_chunks(frame, mtu=1024))
    for chunk in chunks:
        # Firmware guidance: never exceed 180-byte chunk.
        assert len(chunk) <= 180


def test_backoff_schedule() -> None:
    schedule = list(compute_backoff_schedule(n=6, base=1.0, cap=16.0))
    assert schedule == [1.0, 2.0, 4.0, 8.0, 16.0, 16.0]


async def test_ble_client_tx_queue_enqueues_chunks() -> None:
    client = BleClient(mtu=23)
    await client.enqueue_tx(b"Z" * 100 + b"\n")
    drained: list[bytes] = []
    # Poll the queue without running the real BLE loop.
    while True:
        try:
            chunk = client.tx_queue.get_nowait()
        except asyncio.QueueEmpty:
            break
        drained.append(chunk)
    assert b"".join(drained) == b"Z" * 100 + b"\n"
    for chunk in drained:
        assert len(chunk) <= 20


def test_ble_client_exposes_rx_queue() -> None:
    client = BleClient()
    assert isinstance(client.rx_queue, asyncio.Queue)
    assert isinstance(client.tx_queue, asyncio.Queue)


async def test_ble_client_on_notify_feeds_rx_queue() -> None:
    client = BleClient()
    client.on_notify(b'{"ack":"own')
    client.on_notify(b'er","ok":true}\n')
    line = await asyncio.wait_for(client.rx_queue.get(), timeout=1.0)
    assert line == b'{"ack":"owner","ok":true}'


async def test_auto_reconnect_backoff_progression() -> None:
    """The scheduler yields the documented 1/2/4/8/16-then-cap sequence."""
    schedule = list(compute_backoff_schedule(n=7, base=1.0, cap=16.0))
    assert schedule[:5] == [1.0, 2.0, 4.0, 8.0, 16.0]
    # All subsequent entries are capped.
    assert all(s == 16.0 for s in schedule[5:])
