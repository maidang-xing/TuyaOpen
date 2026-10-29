#!/usr/bin/env python3
"""
sanoTTS Serial Smoke Test Tool (M0 Hardware & Protocol Validation)
Supports --port, --suite m0, --timeout, and optional --mock mode.
"""

import sys
import time
import json
import argparse
from typing import Dict, Any, Optional, List

try:
    import serial
except ImportError:
    serial = None


class SntClient:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 30.0, mock: bool = False):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.mock = mock
        self.ser = None
        self.logs: List[str] = []

    def connect(self):
        if self.mock:
            print(f"[SMOKE] Running in MOCK mode (simulated device)")
            return
        if serial is None:
            raise RuntimeError("pyserial is required for real serial testing (pip install pyserial)")
        print(f"[SMOKE] Opening serial port {self.port} at {self.baudrate} baud...")
        self.ser = serial.Serial(self.port, self.baudrate, timeout=1.0)
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_line(self, line: str):
        if not line.endswith("\n"):
            line += "\n"
        print(f">> {line.strip()!r}")
        if self.mock:
            return
        self.ser.write(line.encode("utf-8"))
        self.ser.flush()

    def read_event(self, req_id: Optional[int] = None, event_type: Optional[str] = None, timeout: Optional[float] = None) -> Dict[str, Any]:
        deadline = time.time() + (timeout if timeout is not None else self.timeout)

        if self.mock:
            # Mock responder for testing without hardware
            time.sleep(0.05)
            if event_type == "hello":
                return {"id": req_id, "event": "hello", "version": "1.0.0", "voice": "en-nano", "limits": {"max_line": 1024, "max_phonemes": 256}, "commands": ["HELLO", "SAY", "PHONEMES", "GOLDEN", "STOP"]}
            elif event_type == "accepted":
                return {"id": req_id, "event": "accepted", "voice": "en-nano"}
            elif event_type == "done":
                return {"id": req_id, "event": "done", "samples": 16000, "source_rate": 16000, "compute_us": 10000, "wall_us": 1050000, "underruns": 0}
            elif event_type == "cancelled":
                return {"id": req_id, "event": "cancelled"}
            return {"id": req_id, "event": event_type or "ok"}

        buf = ""
        while time.time() < deadline:
            raw_line = self.ser.readline().decode("utf-8", errors="replace")
            if not raw_line:
                continue
            line = raw_line.strip()
            if not line:
                continue

            if not line.startswith("@SNT\t"):
                # Device system log
                print(f"   [LOG] {line}")
                self.logs.append(line)
                continue

            print(f"<< {line}")
            json_part = line[len("@SNT\t"):]
            try:
                ev = json.loads(json_part)
            except Exception as e:
                print(f"[WARN] Failed to parse JSON: {json_part!r} ({e})")
                continue

            # Match event
            ev_id = ev.get("id")
            ev_type = ev.get("event")

            if req_id is not None and ev_id != req_id:
                continue
            if event_type is not None and ev_type != event_type:
                continue

            return ev

        raise TimeoutError(f"Timeout waiting for response (req_id={req_id}, event={event_type}) after {self.timeout}s")


def run_suite_m0(client: SntClient) -> bool:
    print("\n=== Running Suite M0: Hardware & Protocol Smoke Test ===")

    # 1. HELLO Test
    print("\n--- Test 1: HELLO handshake ---")
    client.send_line("HELLO\t1\n")
    ev = client.read_event(req_id=1, event_type="hello")
    assert ev.get("version") == "1.0.0", f"Unexpected version: {ev}"
    limits = ev.get("limits", {})
    assert limits.get("max_line") == 1024, f"Unexpected limits: {limits}"
    assert limits.get("max_phonemes") == 256, f"Unexpected limits: {limits}"
    print("PASS: HELLO response valid.")

    # 2. 1-second 440 Hz test tone playback
    print("\n--- Test 2: Audio playback & drain (GOLDEN) ---")
    client.send_line("GOLDEN\t2\n")
    ev_acc = client.read_event(req_id=2, event_type="accepted")
    print(f"PASS: Accepted (voice={ev_acc.get('voice')})")

    # Wait for completion after ~1 second playback
    ev_done = client.read_event(req_id=2, event_type="done", timeout=5.0)
    samples = ev_done.get("samples", 0)
    rate = ev_done.get("source_rate", 0)
    wall_us = ev_done.get("wall_us", 0)
    print(f"PASS: Done received. Samples={samples}, Rate={rate}, Wall={wall_us/1000.0:.1f}ms, Underruns={ev_done.get('underruns')}")
    assert samples >= 15000, f"Expected ~16000 samples, got {samples}"

    # 3. STOP Priority Cancellation Test
    print("\n--- Test 3: STOP priority cancellation ---")
    client.send_line("GOLDEN\t3\n")
    client.send_line("STOP\t4\n")

    # Expect request 3 cancelled
    ev_cancel = client.read_event(req_id=3, event_type="cancelled", timeout=3.0)
    print(f"PASS: Request 3 cancelled: {ev_cancel}")

    # Expect STOP 4 accepted and done
    client.read_event(req_id=4, event_type="accepted", timeout=2.0)
    client.read_event(req_id=4, event_type="done", timeout=2.0)
    print("PASS: STOP completed successfully.")

    # 4. Pipeline recovery test after STOP
    print("\n--- Test 4: Post-STOP recovery test ---")
    client.send_line("GOLDEN\t5\n")
    client.read_event(req_id=5, event_type="accepted", timeout=2.0)
    ev_done5 = client.read_event(req_id=5, event_type="done", timeout=5.0)
    print(f"PASS: Post-STOP playback successful (samples={ev_done5.get('samples')}).")

    print("\n==========================================")
    print(">>> ALL M0 SMOKE TESTS PASSED SUCCESSFULLY! <<<")
    print("==========================================")
    return True


def main():
    parser = argparse.ArgumentParser(description="sanoTTS serial smoke test")
    parser.add_argument("--port", type=str, default="/dev/ttyUSB0", help="Serial port")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (default 115200)")
    parser.add_argument("--suite", type=str, default="m0", choices=["m0", "hello", "soak"], help="Test suite")
    parser.add_argument("--timeout", type=float, default=30.0, help="Timeout in seconds")
    parser.add_argument("--mock", action="store_true", help="Run in mock mode without physical serial port")
    parser.add_argument("--requests", type=int, default=10, help="Number of requests for soak suite")
    args = parser.parse_args()

    client = SntClient(args.port, args.baudrate, args.timeout, mock=args.mock)
    try:
        client.connect()
        if args.suite == "m0":
            success = run_suite_m0(client)
        elif args.suite == "hello":
            client.send_line("HELLO\t1\n")
            ev = client.read_event(req_id=1, event_type="hello")
            print("HELLO response:", json.dumps(ev))
            success = True
        elif args.suite == "soak":
            success = True
            for i in range(args.requests):
                print(f"\n--- Soak iteration {i+1}/{args.requests} ---")
                client.send_line(f"GOLDEN\t{100+i}\n")
                client.read_event(req_id=100+i, event_type="accepted")
                client.read_event(req_id=100+i, event_type="done", timeout=5.0)
            print(f"PASS: {args.requests} soak requests succeeded without error.")
        else:
            print(f"Unknown suite: {args.suite}")
            success = False

        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"\n[ERROR] Smoke test failed: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        client.close()


if __name__ == "__main__":
    main()
