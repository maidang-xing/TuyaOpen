"""Encoders/decoders for the BLE wire protocol (v1.0).

Every public encoder returns a ``bytes`` value terminated by a single
``\\n`` so it can be written straight to the NUS RX characteristic. See
``docs/protocol/BLE_WIRE_PROTOCOL.md`` for the frame catalogue.

All encoders are pure (no I/O, no global state). IDs are sourced from
``secrets.token_hex`` — never ``random`` — per the workspace security
rules.

Application-level chunking
--------------------------
Large frames (>``CHUNK_THRESHOLD`` bytes) are split into numbered chunk
envelopes via :func:`chunk_encode`.  Each envelope is a self-contained
JSON line with keys ``_f`` (frame id), ``_n`` (1-based seq), ``_t``
(total), ``_d`` (partial payload as a JSON string).  The receiver
reassembles via :class:`ChunkReassembler` before dispatch.  Frames
below the threshold pass through unchanged, so the mechanism is fully
backward-compatible with peers that do not understand chunking.
"""

from __future__ import annotations

import json
import secrets
import time as _time_mod
from typing import Any

# Device-side bounds lifted from BLE_WIRE_PROTOCOL.md §3 and §6.
MAX_LINE_BYTES = 8 * 1024          # Raised for chunking; individual chunks are small.
MAX_RX_LINE_BYTES = 8 * 1024       # §6 security cap for inbound parsing.

# Application-level chunking constants.
CHUNK_THRESHOLD = 480              # Frames larger than this get split.
CHUNK_RAW_SIZE = 400               # Raw payload bytes per chunk piece.
CHUNK_MAX = 20                     # Max pieces per frame (~8 KB payload).
CHUNK_TIMEOUT_S = 5.0              # Discard incomplete frames after this.
DEVICE_RX_LINE_CAP_BYTES = 1024    # Firmware must accept each chunk envelope.

_chunk_tx_seq: int = 0
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


SESSION_ID_SHORT    = 11    # keep first 11 chars of session_id as "id"
SESSION_NAME_MAX    = 28   # match BUDDY_SESSION_NAME_LEN
MODEL_MAX           = 22   # match BUDDY_MODEL_LEN
SESSION_PROJECT_MAX = 23   # match BUDDY_SESSION_PROJECT_LEN
SESSION_ENTRY_MAX   = 40   # per-session local entry width (shorter than global ENTRY_MAX_BYTES)
DAILY_HISTORY_DAYS  = 28   # match BUDDY_DAILY_HISTORY_DAYS
VERSION_MAX         = 19   # match claude_version[20]-1
COST_SCALE          = 1_000_000  # micro-USD factor: cost_ucc / COST_SCALE = USD


def heartbeat(
    total: int,
    running: int,
    waiting: int,
    tokens: int,
    tokens_today: int,
    msg: str,
    tokens_in: int = 0,
    tokens_in_today: int = 0,
    cache_read: int = 0,
    cache_write: int = 0,
    ctx_used: int = 0,
    ctx_total: int = 0,
    entries: list[str] | None = None,
    prompt: dict[str, Any] | None = None,
    model: str | None = None,
    sessions: list[dict[str, Any]] | None = None,
    mstats: list[dict[str, Any]] | None = None,
    claude_ver: str | None = None,
    cost_today_ucc: int = 0,
    cost_total_ucc: int = 0,
    daily_tokens: list[int] | None = None,
) -> bytes:
    """Encode a heartbeat snapshot (peer → device) per §3.1."""
    obj: dict[str, Any] = {
        "total": int(total),
        "running": int(running),
        "waiting": int(waiting),
        "tokens": int(tokens),
        "tokens_today": int(tokens_today),
        "msg": msg,
    }
    if tokens_in:
        obj["tokens_in"] = int(tokens_in)
    if tokens_in_today:
        obj["tokens_in_today"] = int(tokens_in_today)
    if cache_read:
        obj["cache_read"] = int(cache_read)
    if cache_write:
        obj["cache_write"] = int(cache_write)
    if ctx_used:
        obj["ctx_used"] = int(ctx_used)
    if ctx_total:
        obj["ctx_total"] = int(ctx_total)
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
                "to": int(s.get("to", 0)),
                "r":  bool(s.get("r", False)),
                **( {"p": _truncate_utf8(str(s["p"]), SESSION_PROJECT_MAX)} if s.get("p") else {} ),
                **( {"e": [_truncate_utf8(x, SESSION_ENTRY_MAX) for x in s["e"][:4]]} if s.get("e") else {} ),
            }
            for s in sessions
        ]
    if mstats:
        obj["mstats"] = [
            {
                "m":  _truncate_utf8(str(ms.get("m", "")), MODEL_MAX),
                "to": int(ms.get("to", 0)),
            }
            for ms in mstats
        ]
    if claude_ver:
        obj["ver"] = _truncate_utf8(claude_ver, VERSION_MAX)
    if cost_today_ucc:
        obj["cost_td"] = int(cost_today_ucc)
    if cost_total_ucc:
        obj["cost_all"] = int(cost_total_ucc)
    if daily_tokens is not None:
        obj["daily"] = [int(t) for t in daily_tokens[:DAILY_HISTORY_DAYS]]
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
    - ``"asr"``         — ``{"asr":"<text>","sid":"<short>"}`` ASR transcript.
    - ``"permission"``  — ``{"cmd":"permission", ...}`` per §3.6.
    - ``"cmd_status"``  — ``{"cmd":"status"}`` (status request; unusual
      from the device but handled for symmetry).
    - ``"cmd_hb_req"``  — ``{"cmd":"hb_req","page":"<name>"}`` device-pull
      request for a fresh heartbeat (Tuya extension). The official
      Anthropic Claude Desktop ignores unknown ``cmd`` frames from the
      device, so this stays REFERENCE.md-compatible.
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
    if "asr" in decoded:
        # Device → host ASR transcript (firmware v5.x).
        # Shape: {"asr": "<utf-8>", "sid": "<11-char short id>"}.
        text = decoded.get("asr")
        sid = decoded.get("sid", "")
        if not isinstance(text, str) or not isinstance(sid, str):
            return ("invalid", {})
        # Mirror the firmware 1024-char ceiling, with headroom for escapes.
        if len(text) == 0 or len(text) > 4096 or len(sid) > 32:
            return ("invalid", {})
        return ("asr", {"asr": text, "sid": sid})
    cmd = decoded.get("cmd")
    if cmd == "permission":
        return ("permission", decoded)
    if cmd == "status":
        return ("cmd_status", decoded)
    if cmd == "hb_req":
        # Tuya extension: device-side pull for a fresh heartbeat.
        # Optional ``page`` field (str ≤ 16 bytes) hints which screen
        # the device just opened so the daemon can prioritise data.
        page = decoded.get("page", "")
        if not isinstance(page, str) or len(page) > 32:
            page = ""
        return ("cmd_hb_req", {"cmd": "hb_req", "page": page})
    if isinstance(cmd, str):
        return ("cmd_unknown", decoded)
    return ("unknown", decoded)


# ---------------------------------------------------------------------------
# Application-level chunking
# ---------------------------------------------------------------------------

def _utf8_safe_split(data: bytes, pos: int) -> int:
    """Back up *pos* so it does not land on a UTF-8 continuation byte."""
    while pos > 0 and (data[pos] & 0xC0) == 0x80:
        pos -= 1
    return pos


def chunk_encode(frame: bytes) -> list[bytes]:
    """Split *frame* into application-level chunk envelopes when it exceeds
    ``CHUNK_THRESHOLD``.

    Small frames are returned as-is in a single-element list.  Each item in
    the returned list is a complete newline-terminated JSON line that can be
    fed individually to the BLE-level chunker (``tx_chunks``).
    """
    global _chunk_tx_seq  # noqa: PLW0603
    if len(frame) <= CHUNK_THRESHOLD:
        return [frame]

    payload = frame.rstrip(b"\n")
    pieces: list[str] = []
    pos = 0
    while pos < len(payload):
        end = min(pos + CHUNK_RAW_SIZE, len(payload))
        if end < len(payload):
            end = _utf8_safe_split(payload, end)
            if end <= pos:
                end = pos + 1
        pieces.append(payload[pos:end].decode("utf-8", errors="replace"))
        pos = end

    _chunk_tx_seq = (_chunk_tx_seq + 1) & 0xFF
    fid = f"{_chunk_tx_seq:02x}"
    total = len(pieces)
    if total > CHUNK_MAX:
        raise ValueError(
            f"wire frame requires too many chunks: {total} > {CHUNK_MAX}"
        )
    result: list[bytes] = []
    for i, piece_text in enumerate(pieces):
        env: dict[str, Any] = {
            "_f": fid, "_n": i + 1, "_t": total, "_d": piece_text,
        }
        line = (
            json.dumps(env, ensure_ascii=False, separators=(",", ":"))
            .encode("utf-8") + b"\n"
        )
        result.append(line)
    return result


class ChunkReassembler:
    """Stateful reassembler for application-level chunk envelopes.

    Feed each received line via :meth:`feed`.  Non-chunk lines pass through
    unchanged.  Chunk envelopes (those with a ``_f`` key) are buffered
    until all pieces of a frame arrive, at which point the reassembled
    payload is returned.  Incomplete frames are discarded after
    ``CHUNK_TIMEOUT_S`` seconds.
    """

    def __init__(self) -> None:
        self._frames: dict[str, dict[str, Any]] = {}

    def feed(self, line: bytes) -> bytes | None:
        """Return the reassembled frame once complete, or *None* if waiting.

        Non-chunk lines are returned immediately.
        """
        stripped = line.rstrip(b"\r\n")
        try:
            obj = json.loads(stripped)
        except (json.JSONDecodeError, UnicodeDecodeError):
            return line

        if not isinstance(obj, dict) or "_f" not in obj:
            return line

        fid = obj.get("_f", "")
        n = obj.get("_n", 0)
        t = obj.get("_t", 0)
        d = obj.get("_d", "")
        if (not isinstance(fid, str) or not isinstance(n, int)
                or not isinstance(t, int) or not isinstance(d, str)):
            return None
        if n < 1 or t < 1 or t > CHUNK_MAX or n > t:
            return None

        now = _time_mod.monotonic()
        expired = [k for k, v in self._frames.items()
                   if now - v["ts"] > CHUNK_TIMEOUT_S]
        for k in expired:
            del self._frames[k]

        if fid not in self._frames:
            self._frames[fid] = {"total": t, "parts": {}, "ts": now}

        fr = self._frames[fid]
        fr["parts"][n] = d
        fr["ts"] = now

        if len(fr["parts"]) >= fr["total"]:
            del self._frames[fid]
            parts = [fr["parts"].get(i, "") for i in range(1, fr["total"] + 1)]
            full = "".join(parts) + "\n"
            return full.encode("utf-8")

        return None
