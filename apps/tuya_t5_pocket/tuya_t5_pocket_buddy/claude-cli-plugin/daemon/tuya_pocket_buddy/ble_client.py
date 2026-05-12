"""BLE central wrapper around ``bleak``.

Responsibilities (protocol §2):

* Scan for peripherals whose local name matches ``^Claude_[0-9A-Za-z]+$``.
* Connect, subscribe to TX notify (NUS ``6E400003-...``), write to RX
  characteristic (NUS ``6E400002-...``) in chunks ≤ ``MTU - 3``.
* Reassemble incoming notifications into newline-delimited lines.
* Auto-reconnect with exponential backoff (1 s → 16 s).

Unit tests use the pure helpers in this module (``reassemble_lines``,
``tx_chunks``, ``pick_candidate``, ``compute_backoff_schedule``) without
importing ``bleak``; the ``BleClient`` itself lazily imports ``bleak`` on
``start()``.
"""

from __future__ import annotations

import asyncio
import logging
import re
from collections.abc import Iterable, Iterator
from typing import Any, Protocol

log = logging.getLogger(__name__)

# Nordic UART Service (NUS) UUIDs — see BLE_WIRE_PROTOCOL.md §2.1.
NUS_SERVICE_UUID: str = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_UUID: str = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # central → device
NUS_TX_UUID: str = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # device → central (notify)

# Default MTU if peer negotiation hasn't completed.
DEFAULT_MTU: int = 23
# Device-side hard cap per §2.3 — never exceed this even with bigger MTUs.
MAX_NOTIFY_CHUNK: int = 180

# Daemon-side reassembly cap (protects against a misbehaving peripheral).
RX_REASSEMBLY_CAP_BYTES: int = 8 * 1024

# Scan window in seconds for BleakScanner.discover().
SCAN_DURATION_S: float = 10.0

CLAUDE_NAME_RE: str = r"^Claude_[0-9A-Za-z]+$"
_CLAUDE_NAME_PAT = re.compile(CLAUDE_NAME_RE)


class _HasName(Protocol):
    name: str | None
    address: str


def pick_candidate(ads: Iterable[_HasName]) -> list[_HasName]:
    """Return the subset of advertisements whose name matches ``Claude_XXXX``."""
    return [ad for ad in ads if ad.name and _CLAUDE_NAME_PAT.fullmatch(ad.name)]


def reassemble_lines(
    state: dict[str, bytes], chunk: bytes
) -> list[bytes]:
    """Append *chunk* to the rolling buffer in *state* and return complete lines.

    ``state`` MUST be a dict with a ``"buf"`` key (mutable reference). The
    buffer is discarded when it exceeds ``RX_REASSEMBLY_CAP_BYTES`` to
    avoid unbounded memory growth on a misbehaving peer.
    """
    state["buf"] += chunk
    if len(state["buf"]) > RX_REASSEMBLY_CAP_BYTES:
        log.warning("ble: rx buffer overflow (%d B), resetting", len(state["buf"]))
        state["buf"] = b""
        return []
    lines: list[bytes] = []
    while b"\n" in state["buf"]:
        line, _, rest = state["buf"].partition(b"\n")
        state["buf"] = rest
        if line:
            lines.append(line)
    return lines


def tx_chunks(frame: bytes, mtu: int = DEFAULT_MTU) -> Iterator[bytes]:
    """Split *frame* into GATT write chunks of at most ``mtu - 3`` bytes.

    The chunk size is further hard-capped at ``MAX_NOTIFY_CHUNK`` to
    protect the firmware — see ``BUDDY_BLE_MAX_NOTIFY_CHUNK`` in the wire
    protocol doc §2.3.
    """
    if not frame:
        return
    chunk_size = max(1, min(mtu - 3, MAX_NOTIFY_CHUNK))
    for start in range(0, len(frame), chunk_size):
        yield frame[start:start + chunk_size]


def compute_backoff_schedule(
    n: int = 6, base: float = 1.0, cap: float = 16.0
) -> Iterator[float]:
    """Yield *n* exponential backoff delays doubling from *base* up to *cap*."""
    delay = base
    for _ in range(n):
        yield min(delay, cap)
        delay *= 2


class BleClient:
    """Async BLE central for the T5AI-Pocket buddy device.

    The tx/rx queues expose a transport-agnostic interface to the rest of
    the daemon: callers enqueue newline-terminated frames on
    :attr:`tx_queue` and await complete inbound lines from
    :attr:`rx_queue`.
    """

    def __init__(
        self,
        target_address: str | None = None,
        mtu: int = DEFAULT_MTU,
    ) -> None:
        self._target_address = target_address
        self._mtu = mtu
        self._rx_state: dict[str, bytes] = {"buf": b""}
        self.rx_queue: asyncio.Queue[bytes] = asyncio.Queue()
        self.tx_queue: asyncio.Queue[bytes] = asyncio.Queue()
        self._stop_event = asyncio.Event()
        self._task: asyncio.Task[None] | None = None
        self._connected: bool = False

    # ------------------------------------------------------------------ public API

    async def start(self) -> None:
        """Start the BLE lifecycle (scan → connect → serve I/O)."""
        if self._task is not None:
            return
        self._stop_event.clear()
        self._task = asyncio.create_task(self._lifecycle())

    async def stop(self) -> None:
        """Signal shutdown and wait for the lifecycle task to exit."""
        self._stop_event.set()
        if self._task is not None:
            await self._task
            self._task = None

    async def enqueue_tx(self, frame: bytes) -> None:
        """Queue a newline-terminated frame for transmission."""
        for chunk in tx_chunks(frame, mtu=self._mtu):
            await self.tx_queue.put(chunk)

    def is_connected(self) -> bool:
        """Return True if a GATT connection is currently active."""
        return self._connected

    def on_notify(self, chunk: bytes) -> None:
        """Feed raw TX-notify bytes into the reassembler."""
        for line in reassemble_lines(self._rx_state, chunk):
            try:
                self.rx_queue.put_nowait(line)
            except asyncio.QueueFull:  # pragma: no cover - Queue is unbounded
                log.error("ble: rx queue full; dropping line")

    # ------------------------------------------------------------------ internals

    async def _lifecycle(self) -> None:
        """Scan → connect → serve → retry on disconnect, with backoff."""
        try:
            from bleak import BleakClient, BleakScanner
        except Exception:
            log.exception("ble: bleak not available; client cannot run")
            return

        for delay in _infinite_backoff():
            if self._stop_event.is_set():
                return
            try:
                target = await self._resolve_target(BleakScanner)
                if target is None:
                    await self._sleep_or_stop(delay)
                    continue
                async with BleakClient(target) as client:
                    await self._serve(client)
            except Exception:  # pragma: no cover - integration-only
                log.exception("ble: lifecycle iteration failed")
            await self._sleep_or_stop(delay)

    async def _resolve_target(self, scanner_cls: Any) -> str | None:
        if self._target_address:
            return self._target_address
        ads = await scanner_cls.discover(timeout=SCAN_DURATION_S)
        candidates = pick_candidate(ads)
        if not candidates:
            log.info("ble: no Claude_* peripheral found")
            return None
        chosen = candidates[0]
        log.info("ble: selected peripheral %s", chosen.address)
        return str(chosen.address)

    async def _serve(self, client: Any) -> None:
        """Subscribe to TX and pump the tx_queue until disconnect or stop."""
        def _cb(_char: Any, data: bytearray) -> None:
            self.on_notify(bytes(data))

        await client.start_notify(NUS_TX_UUID, _cb)
        log.info("ble: connected and subscribed to NUS TX")
        self._connected = True
        try:
            while not self._stop_event.is_set() and client.is_connected:
                try:
                    chunk = await asyncio.wait_for(
                        self.tx_queue.get(), timeout=0.5
                    )
                except asyncio.TimeoutError:
                    continue
                await client.write_gatt_char(NUS_RX_UUID, chunk, response=False)
        finally:
            self._connected = False
            try:
                await client.stop_notify(NUS_TX_UUID)
            except Exception:  # pragma: no cover
                pass

    async def _sleep_or_stop(self, delay: float) -> None:
        try:
            await asyncio.wait_for(self._stop_event.wait(), timeout=delay)
        except asyncio.TimeoutError:
            return


def _infinite_backoff(base: float = 1.0, cap: float = 16.0) -> Iterator[float]:
    """Yield an unbounded 1→16-second exponential schedule (capped)."""
    delay = base
    while True:
        yield min(delay, cap)
        delay *= 2
