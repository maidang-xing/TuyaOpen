"""Encoders/decoders for the BLE wire protocol (v1.0).

Every public encoder returns a ``bytes`` value terminated by a single
``\\n`` so it can be written straight to the NUS RX characteristic. See
``docs/protocol/BLE_WIRE_PROTOCOL.md`` for the frame catalogue.

All encoders are pure (no I/O, no global state). IDs are sourced from
``secrets.token_hex`` — never ``random`` — per the workspace security
rules.
"""

from __future__ import annotations

import json
import secrets
from typing import Any

# Device-side bounds lifted from BLE_WIRE_PROTOCOL.md §3 and §6.
MAX_LINE_BYTES = 4 * 1024          # Daemon self-cap; firmware accepts 5120 B.
MAX_RX_LINE_BYTES = 8 * 1024       # §6 security cap for inbound parsing.
ENTRY_MAX_BYTES = 80               # Firmware ring entry width.
OWNER_MAX_BYTES = 30               # Spec §3.2 — truncate long owner names.
PROMPT_ID_BYTES = 10               # secrets.token_hex(10) → 20 hex chars.


def _truncate_utf8(value: str, max_bytes: int) -> str:
    """Return *value* trimmed so its UTF-8 encoding fits in ``max_bytes``.

    Truncation never splits a codepoint: if cutting at ``max_bytes`` would
    land mid-sequence, the trailing partial bytes are dropped first.
    """
    encoded = value.encode("utf-8")
    if len(encoded) <= max_bytes:
        return value
    # Walk backwards from the cut point until we land on a codepoint start.
    cut = max_bytes
    while cut > 0 and (encoded[cut] & 0xC0) == 0x80:
        cut -= 1
    return encoded[:cut].decode("utf-8", errors="ignore")


def _dump(obj: dict[str, Any]) -> bytes:
    """Serialize *obj* as compact UTF-8 JSON terminated by ``\\n``."""
    payload = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
    line = payload.encode("utf-8") + b"\n"
    if len(line) > MAX_LINE_BYTES:
        raise ValueError(f"wire frame too large: {len(line)} bytes > {MAX_LINE_BYTES}")
    return line


SESSION_ID_SHORT = 11    # keep first 11 chars of session_id as "id"
SESSION_NAME_MAX = 28   # match BUDDY_SESSION_NAME_LEN
MODEL_MAX        = 22   # match BUDDY_MODEL_LEN


def heartbeat(
    total: int,
    running: int,
    waiting: int,
    tokens: int,
    tokens_today: int,
    msg: str,
    entries: list[str] | None = None,
    prompt: dict[str, Any] | None = None,
    model: str | None = None,
    sessions: list[dict[str, Any]] | None = None,
    mstats: list[dict[str, Any]] | None = None,
) -> bytes:
    """Encode a heartbeat snapshot (peer → device) per §3.1.

    New optional fields (M2):
      ``model``    — current Claude model short name (e.g. "sonnet-4-6").
      ``sessions`` — list of session dicts with short keys:
                     ``id``, ``n`` (name), ``m`` (model),
                     ``ti`` (tokens_in), ``to`` (tokens_out), ``r`` (running).
    """
    obj: dict[str, Any] = {
        "total": int(total),
        "running": int(running),
        "waiting": int(waiting),
        "tokens": int(tokens),
        "tokens_today": int(tokens_today),
        "msg": msg,
    }
    if entries is not None:
        obj["entries"] = [_truncate_utf8(e, ENTRY_MAX_BYTES) for e in entries]
    if prompt is not None:
        pid = prompt.get("id") or secrets.token_hex(PROMPT_ID_BYTES)
        obj["prompt"] = {
            "id": pid,
            "tool": prompt.get("tool", ""),
            "hint": prompt.get("hint", ""),
        }
    if model:
        obj["model"] = _truncate_utf8(model, MODEL_MAX)
    if sessions:
        obj["sessions"] = [
            {
                "id": _truncate_utf8(str(s.get("id", ""))[:SESSION_ID_SHORT], SESSION_ID_SHORT),
                "n":  _truncate_utf8(str(s.get("n",  "")), SESSION_NAME_MAX),
                "m":  _truncate_utf8(str(s.get("m",  "")), MODEL_MAX),
                "to": int(s.get("to", 0)),   # output tokens only
                "r":  bool(s.get("r", False)),
            }
            for s in sessions
        ]
    if mstats:
        obj["mstats"] = [
            {
                "m":  _truncate_utf8(str(ms.get("m", "")), MODEL_MAX),
                "to": int(ms.get("to", 0)),  # output tokens
            }
            for ms in mstats
        ]
    return _dump(obj)


def _require_int(value: Any, name: str) -> int:
    """Reject anything that isn't a plain ``int`` (bool is also rejected)."""
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{name} must be int, got {type(value).__name__}")
    return int(value)


def time_sync(epoch_s: int, tz_sec: int) -> bytes:
    """Encode a ``{"time":[epoch, tz]}`` frame per REFERENCE.md §Transport.

    ``tz_sec`` is the UTC offset in **seconds** (e.g. UTC-7 → -25200),
    matching the Claude Desktop app's wire protocol.
    Both arguments MUST be plain ``int``.
    """
    _require_int(epoch_s, "epoch_s")
    _require_int(tz_sec, "tz_sec")
    return _dump({"time": [epoch_s, tz_sec]})


def owner(name: str) -> bytes:
    """Encode a ``{"cmd":"owner","name":...}`` frame (§3.4).

    The name is truncated to ``OWNER_MAX_BYTES`` UTF-8 bytes — device side
    caps at 23 bytes used, but we leave a little headroom for future
    firmware rev.
    """
    if not isinstance(name, str):
        raise TypeError(f"name must be str, got {type(name).__name__}")
    safe = _truncate_utf8(name, OWNER_MAX_BYTES)
    return _dump({"cmd": "owner", "name": safe})


def status_request() -> bytes:
    """Encode a ``{"cmd":"status"}`` frame (§3.4)."""
    return _dump({"cmd": "status"})


def unpair() -> bytes:
    """Encode a ``{"cmd":"unpair"}`` frame (§3.4).

    Note: firmware v1.0 acks cosmetically; bonds are not actually erased.
    """
    return _dump({"cmd": "unpair"})


def parse_frame(line: bytes) -> tuple[str, dict[str, Any]]:
    """Parse a single device-originated frame.

    Returns a ``(kind, payload)`` tuple where *kind* is one of:

    - ``"ack"``         — frame with an ``ack`` key (generic envelope).
    - ``"permission"``  — ``{"cmd":"permission", ...}`` per §3.6.
    - ``"cmd_status"``  — ``{"cmd":"status"}`` (status request; unusual
      from the device but handled for symmetry).
    - ``"cmd_unknown"`` — any other ``cmd`` field.
    - ``"unknown"``     — parseable JSON object without a recognised key.
    - ``"invalid"``     — JSON parse failed or value isn't an object.

    Raises ``ValueError`` if the line exceeds ``MAX_RX_LINE_BYTES``
    (protects the daemon from hostile or misbehaving peers).
    """
    if len(line) > MAX_RX_LINE_BYTES:
        raise ValueError(
            f"inbound line too large: {len(line)} > {MAX_RX_LINE_BYTES}"
        )
    payload = line.rstrip(b"\r\n")
    try:
        decoded = json.loads(payload)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return ("invalid", {})
    if not isinstance(decoded, dict):
        return ("invalid", {})
    if "ack" in decoded:
        return ("ack", decoded)
    cmd = decoded.get("cmd")
    if cmd == "permission":
        return ("permission", decoded)
    if cmd == "status":
        return ("cmd_status", decoded)
    if isinstance(cmd, str):
        return ("cmd_unknown", decoded)
    return ("unknown", decoded)
