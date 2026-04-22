"""Unit tests for ``tuya_pocket_buddy.wire``.

Per the BLE wire protocol (v1.0) every frame is a single newline-terminated
UTF-8 JSON object. Tests here check encoder output byte-for-byte where
feasible and round-trip shape otherwise.
"""

from __future__ import annotations

import json
import re

import pytest

from tuya_pocket_buddy import wire


def _decode(line: bytes) -> dict:
    assert line.endswith(b"\n"), "wire frames must be newline-terminated"
    assert b"\n" not in line[:-1], "wire frames must be single-line"
    return json.loads(line[:-1].decode("utf-8"))


class TestHeartbeat:
    def test_heartbeat_minimal(self) -> None:
        out = wire.heartbeat(total=1, running=0, waiting=0, tokens=0,
                             tokens_today=0, msg="")
        expected = (
            b'{"total":1,"running":0,"waiting":0,'
            b'"tokens":0,"tokens_today":0,"msg":""}\n'
        )
        assert out == expected

    def test_heartbeat_with_entries_truncates_at_80b(self) -> None:
        long_entry = "A" * 200
        out = wire.heartbeat(total=1, running=0, waiting=0, tokens=0,
                             tokens_today=0, msg="", entries=[long_entry])
        obj = _decode(out)
        assert obj["entries"] == ["A" * 80]
        assert len(obj["entries"][0].encode("utf-8")) <= 80

    def test_heartbeat_with_entries_multibyte_safe(self) -> None:
        entry = "中" * 40
        out = wire.heartbeat(total=1, running=0, waiting=0, tokens=0,
                             tokens_today=0, msg="", entries=[entry])
        obj = _decode(out)
        sent = obj["entries"][0]
        assert len(sent.encode("utf-8")) <= 80
        assert sent == "中" * 26

    def test_heartbeat_with_prompt_id_is_hex20(self) -> None:
        out = wire.heartbeat(total=1, running=0, waiting=1, tokens=10,
                             tokens_today=10, msg="approve: Read",
                             prompt={"tool": "Read", "hint": "./foo"})
        obj = _decode(out)
        assert "prompt" in obj
        assert re.fullmatch(r"[0-9a-f]{20}", obj["prompt"]["id"])
        assert obj["prompt"]["tool"] == "Read"
        assert obj["prompt"]["hint"] == "./foo"

    def test_heartbeat_with_prompt_preserves_provided_id(self) -> None:
        out = wire.heartbeat(total=1, running=0, waiting=1, tokens=0,
                             tokens_today=0, msg="",
                             prompt={"id": "custom_id", "tool": "Bash",
                                     "hint": "ls"})
        obj = _decode(out)
        assert obj["prompt"]["id"] == "custom_id"

    def test_heartbeat_no_embedded_newline(self) -> None:
        out = wire.heartbeat(total=1, running=0, waiting=0, tokens=0,
                             tokens_today=0,
                             msg="multi\nline",
                             entries=["first\nrow"])
        assert out.count(b"\n") == 1
        obj = _decode(out)
        assert obj["msg"] == "multi\nline"
        assert obj["entries"][0] == "first\nrow"

    def test_heartbeat_ascii_compact_separators(self) -> None:
        out = wire.heartbeat(total=2, running=1, waiting=0, tokens=5,
                             tokens_today=5, msg="hi")
        assert b", " not in out
        assert b": " not in out


class TestTimeSync:
    def test_time_sync_positive(self) -> None:
        out = wire.time_sync(1775731234, -25200)
        assert out == b'{"time":[1775731234,-25200]}\n'

    def test_time_sync_positive_tz(self) -> None:
        out = wire.time_sync(1, 28800)
        assert out == b'{"time":[1,28800]}\n'

    def test_time_sync_rejects_non_int_epoch(self) -> None:
        with pytest.raises(TypeError):
            wire.time_sync(1.5, 0)  # type: ignore[arg-type]

    def test_time_sync_rejects_non_int_tz(self) -> None:
        with pytest.raises(TypeError):
            wire.time_sync(1, "0")  # type: ignore[arg-type]

    def test_time_sync_rejects_bool(self) -> None:
        # bool is a subclass of int in Python; guard against it explicitly.
        with pytest.raises(TypeError):
            wire.time_sync(True, 0)  # type: ignore[arg-type]


class TestOwner:
    def test_owner_short(self) -> None:
        out = wire.owner("Felix")
        assert out == b'{"cmd":"owner","name":"Felix"}\n'

    def test_owner_truncates_utf8_safe(self) -> None:
        name = "中" * 20  # 60 UTF-8 bytes
        out = wire.owner(name)
        obj = json.loads(out[:-1])
        assert len(obj["name"].encode("utf-8")) <= 30
        assert obj["name"] == "中" * 10

    def test_owner_ascii_long(self) -> None:
        out = wire.owner("A" * 200)
        obj = json.loads(out[:-1])
        assert obj["name"] == "A" * 30


class TestStatusRequest:
    def test_status_request_exact(self) -> None:
        assert wire.status_request() == b'{"cmd":"status"}\n'


class TestUnpair:
    def test_unpair_exact(self) -> None:
        assert wire.unpair() == b'{"cmd":"unpair"}\n'


class TestParseFrame:
    def test_parse_ack(self) -> None:
        line = b'{"ack":"owner","ok":true,"n":0}\n'
        kind, obj = wire.parse_frame(line)
        assert kind == "ack"
        assert obj == {"ack": "owner", "ok": True, "n": 0}

    def test_parse_ack_without_newline(self) -> None:
        kind, obj = wire.parse_frame(b'{"ack":"status","ok":true}')
        assert kind == "ack"
        assert obj["ack"] == "status"

    def test_parse_permission(self) -> None:
        line = b'{"cmd":"permission","id":"abc","decision":"once"}\n'
        kind, obj = wire.parse_frame(line)
        assert kind == "permission"
        assert obj["id"] == "abc"
        assert obj["decision"] == "once"

    def test_parse_status(self) -> None:
        line = b'{"ack":"status","ok":true,"data":{"name":"Claude_A1B2"}}\n'
        kind, obj = wire.parse_frame(line)
        assert kind == "ack"
        assert obj["data"]["name"] == "Claude_A1B2"

    def test_parse_cmd_status_request(self) -> None:
        kind, obj = wire.parse_frame(b'{"cmd":"status"}\n')
        assert kind == "cmd_status"
        assert obj["cmd"] == "status"

    def test_parse_discards_unknown(self) -> None:
        kind, obj = wire.parse_frame(b'{"foo":1}\n')
        assert kind == "unknown"
        assert obj == {"foo": 1}

    def test_parse_rejects_oversized(self) -> None:
        big = b'{"ack":"x","pad":"' + b"A" * (wire.MAX_RX_LINE_BYTES + 10) + b'"}\n'
        with pytest.raises(ValueError):
            wire.parse_frame(big)

    def test_parse_invalid_json(self) -> None:
        kind, obj = wire.parse_frame(b'{"ack":oops}\n')
        assert kind == "invalid"
        assert obj == {}

    def test_parse_not_object(self) -> None:
        kind, _ = wire.parse_frame(b'[1,2,3]\n')
        assert kind == "invalid"

    def test_parse_unknown_cmd(self) -> None:
        kind, obj = wire.parse_frame(b'{"cmd":"weird"}\n')
        assert kind == "cmd_unknown"
        assert obj["cmd"] == "weird"
