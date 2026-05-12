#!/usr/bin/env python3
"""PreToolUse hook handler for tuya-pocket-buddy.

Reads the Claude Code hook payload from stdin, POSTs it to the daemon,
and exits with the correct code so Claude Code respects the BLE decision:

  exit 0  → approve  (tool may run)
  exit 2  → block    (tool is prevented, shown as "Permission denied")

If the daemon isn't reachable, exits 0 (fall-through) so Claude Code
continues normally — the device is an enhancement, not a hard dependency.
"""

import json
import sys
import urllib.error
import urllib.request

DAEMON_URL = "http://127.0.0.1:9878/hook"
TIMEOUT_S  = 42   # slightly above the 40-s BLE approval window


def main() -> int:
    payload = sys.stdin.buffer.read()

    try:
        req = urllib.request.Request(
            DAEMON_URL,
            data=payload,
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
            body = resp.read()
    except (urllib.error.URLError, OSError):
        # Daemon not running or unreachable — let Claude Code proceed normally.
        sys.stdout.write("{}\n")
        return 0

    try:
        decision = json.loads(body)
    except json.JSONDecodeError:
        sys.stdout.write("{}\n")
        return 0

    # Echo the response so Claude Code can read any extra fields.
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.write(b"\n")

    # exit 2 = block (Claude Code treats this as "hook denied the action").
    if decision.get("decision") in ("deny", "block"):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
