"""Daemon entry point: wire together BLE, hook server, and router.

Subcommands:

* ``run``     (default) — boot the hook server + BLE client.
* ``pair``    — scan for ``Claude_XXXX`` peripherals and persist the MAC.
* ``unpair``  — send ``{"cmd":"unpair"}`` once to the currently-paired
                device and forget the persisted MAC.
* ``status``  — print resolved config + whether a daemon pid file exists.

No new unit tests cover this file (pure wiring; see the plan §T9).
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import signal
import sys

import time

from . import wire
from .ble_client import SCAN_DURATION_S, BleClient, pick_candidate
from .config import (
    DaemonConfig,
    config_as_dict,
    load_config,
    save_state,
    setup_logging,
)
from .hook_router import Router, State
from .hook_server import DEFAULT_PORT, run_server
from .permissions import PermissionBridge

# How often to send a keepalive heartbeat (seconds).
HEARTBEAT_INTERVAL_S: float = 10.0

log = logging.getLogger("tuya_pocket_buddy")


class _BleTxAdapter:
    """Adapter that exposes :meth:`BleClient.enqueue_tx` as a TxSink.

    Large frames are transparently split into application-level chunk
    envelopes via :func:`wire.chunk_encode` before being handed to the
    BLE-level fragmenter.
    """

    def __init__(self, ble: BleClient) -> None:
        self._ble = ble

    async def send(self, line: bytes) -> None:
        for chunk_line in wire.chunk_encode(line):
            await self._ble.enqueue_tx(chunk_line)


async def _rx_pump(
    ble: BleClient, permissions: PermissionBridge, router: Router
) -> None:
    """Forward inbound RX lines to the permission bridge / router / logger.

    Incoming lines are first fed through a :class:`wire.ChunkReassembler`
    so that application-level chunk envelopes are transparently reassembled
    before dispatch.
    """
    reassembler = wire.ChunkReassembler()
    while True:
        raw_line = await ble.rx_queue.get()
        line = reassembler.feed(raw_line)
        if line is None:
            continue
        try:
            kind, payload = wire.parse_frame(line)
        except ValueError:
            log.warning("rx: oversized line dropped (%d B)", len(line))
            continue
        if kind == "permission":
            await permissions.handle_permission(payload)
        elif kind == "asr":
            try:
                await router.handle_asr(
                    payload.get("asr", ""), payload.get("sid", "")
                )
            except Exception as exc:  # noqa: BLE001
                log.debug("rx: asr handler failed: %s", exc)
        elif kind == "cmd_hb_req":
            try:
                await router.handle_hb_request(payload.get("page", ""))
            except Exception as exc:  # noqa: BLE001
                log.debug("rx: hb_req handler failed: %s", exc)
        elif kind == "ack":
            log.debug("rx: ack %s", payload.get("ack"))
        elif kind == "invalid":
            log.debug("rx: invalid frame ignored")
        else:
            log.debug("rx: %s %s", kind, payload.get("cmd"))


async def _heartbeat_loop(
    ble: BleClient, router: Router, stop_event: asyncio.Event
) -> None:
    """Periodically send time-sync + heartbeat to keep the device in sync."""
    while not stop_event.is_set():
        await asyncio.sleep(HEARTBEAT_INTERVAL_S)
        if stop_event.is_set():
            break
        if not ble.is_connected():
            continue
        try:
            tz_sec = -int(time.timezone)   # UTC offset in seconds (REFERENCE.md)
            for c in wire.chunk_encode(wire.time_sync(int(time.time()), tz_sec)):
                await ble.enqueue_tx(c)
            await router.tick()
        except Exception as exc:  # noqa: BLE001
            log.debug("heartbeat: send failed: %s", exc)


async def _run_async(cfg: DaemonConfig) -> int:
    setup_logging(cfg.log_path)
    log.info("daemon: starting; config=%s", config_as_dict(cfg))

    # Write pid file early so scripts/status.* can see us.
    _write_pid(cfg)

    state = State(owner_name=cfg.owner_name)
    permissions = PermissionBridge()
    ble = BleClient(target_address=cfg.device_address)
    router = Router(state=state, tx=_BleTxAdapter(ble), permissions=permissions)

    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig_name in ("SIGTERM", "SIGINT"):
        if hasattr(signal, sig_name):
            try:
                loop.add_signal_handler(getattr(signal, sig_name),
                                         stop_event.set)
            except NotImplementedError:
                # Windows: signal handlers for SIGINT only work on main
                # thread; skip silently.
                pass

    runner = await run_server(router, port=cfg.port)
    await ble.start()
    rx_task = asyncio.create_task(_rx_pump(ble, permissions, router))
    hb_task = asyncio.create_task(_heartbeat_loop(ble, router, stop_event))

    # Send initial time sync as soon as possible.
    try:
        tz_sec = -int(time.timezone)   # UTC offset in seconds per REFERENCE.md
        for c in wire.chunk_encode(wire.time_sync(int(time.time()), tz_sec)):
            await ble.enqueue_tx(c)
        await router.tick()
    except Exception as exc:  # noqa: BLE001
        log.debug("initial heartbeat failed: %s", exc)

    try:
        await stop_event.wait()
    finally:
        log.info("daemon: shutting down")
        hb_task.cancel()
        rx_task.cancel()
        for task in (hb_task, rx_task):
            try:
                await task
            except asyncio.CancelledError:
                pass
        await ble.stop()
        await runner.cleanup()
        _remove_pid(cfg)

    return 0


def _write_pid(cfg: DaemonConfig) -> None:
    try:
        cfg.pid_path.write_text(str(os.getpid()), encoding="utf-8")
    except OSError:
        log.warning("daemon: unable to write pid file %s", cfg.pid_path)


def _remove_pid(cfg: DaemonConfig) -> None:
    try:
        cfg.pid_path.unlink(missing_ok=True)
    except OSError:  # pragma: no cover - best-effort
        pass


async def _pair_async(cfg: DaemonConfig) -> int:
    try:
        from bleak import BleakScanner
    except Exception:
        print("bleak is not installed; run `/buddy-install` first",
              file=sys.stderr)
        return 2

    print(f"scanning for {int(SCAN_DURATION_S)} seconds ...")
    ads = await BleakScanner.discover(timeout=SCAN_DURATION_S)
    matches = pick_candidate(ads)
    if not matches:
        print("no Claude_XXXX peripheral found")
        return 1

    for idx, ad in enumerate(matches):
        print(f"  [{idx}] {ad.name}  {ad.address}")
    if len(matches) == 1:
        chosen = matches[0]
    else:
        raw = input("select index: ").strip()
        try:
            chosen = matches[int(raw)]
        except (ValueError, IndexError):
            print("invalid selection", file=sys.stderr)
            return 1

    save_state(cfg.state_path, {
        "device_address": chosen.address,
        "owner_name": cfg.owner_name,
    })
    print(f"paired with {chosen.name} ({chosen.address})")
    return 0


async def _unpair_async(cfg: DaemonConfig) -> int:
    if not cfg.device_address:
        print("no device paired")
        return 1
    ble = BleClient(target_address=cfg.device_address)
    await ble.enqueue_tx(wire.unpair())
    # Best-effort: give bleak a few seconds to connect and send the frame.
    await ble.start()
    await asyncio.sleep(3.0)
    await ble.stop()
    save_state(cfg.state_path, {"owner_name": cfg.owner_name})
    print("unpaired (cosmetic in firmware v1.0)")
    return 0


def _cmd_status(cfg: DaemonConfig) -> int:
    for key, value in config_as_dict(cfg).items():
        print(f"{key:16s} {value}")
    if cfg.pid_path.is_file():
        try:
            pid_txt = cfg.pid_path.read_text(encoding="utf-8").strip()
            print(f"daemon.pid       {pid_txt}")
        except OSError:
            print("daemon.pid       (unreadable)")
    else:
        print("daemon.pid       (not running)")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="tuya-pocket-buddy")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="Hook server TCP port (default: 9878)")
    sub = parser.add_subparsers(dest="cmd")
    sub.add_parser("run", help="Start the daemon (default)")
    sub.add_parser("pair", help="Scan for Claude_XXXX and pair")
    sub.add_parser("unpair", help="Send cmd:\"unpair\" and forget MAC")
    sub.add_parser("status", help="Print resolved config + pid state")
    return parser


def main(argv: list[str] | None = None) -> int:
    """CLI entry point. Dispatches to the requested subcommand."""
    args = _build_parser().parse_args(argv)
    cfg = load_config(port=args.port)
    cmd = args.cmd or "run"

    if cmd == "run":
        return asyncio.run(_run_async(cfg))
    if cmd == "pair":
        return asyncio.run(_pair_async(cfg))
    if cmd == "unpair":
        return asyncio.run(_unpair_async(cfg))
    if cmd == "status":
        return _cmd_status(cfg)
    print(f"unknown subcommand: {cmd}", file=sys.stderr)
    return 2


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
