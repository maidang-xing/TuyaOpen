#!/usr/bin/env python3
"""
Host unit tests for sanoTTS Serial Protocol and State Machine (snt_serial.c).
Directly drives the compiled C parser via ctypes to verify all boundaries.
"""

import os
import sys
import json
import ctypes
import unittest
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "../.."))
SRC_DIR = os.path.join(APP_DIR, "src")
SO_PATH = os.path.join(SCRIPT_DIR, "libsnt_serial.so")

def build_shared_lib():
    c_file = os.path.join(SRC_DIR, "snt_serial.c")
    include_dir = SRC_DIR
    cmd = [
        "gcc", "-O2", "-fPIC", "-shared",
        "-I", include_dir,
        c_file, "-o", SO_PATH
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Failed to compile snt_serial.c:\n{res.stderr}")

build_shared_lib()
lib = ctypes.CDLL(SO_PATH)

# Types
TX_FN = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p)
EXEC_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)
CANCEL_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p)

lib.snt_serial_size.restype = ctypes.c_size_t

class SntSerialStruct(ctypes.Structure):
    _fields_ = [
        ("dummy", ctypes.c_byte * 8192)  # Generous buffer > sizeof(snt_serial_t)
    ]

lib.snt_serial_init.argtypes = [
    ctypes.c_void_p,
    TX_FN, ctypes.c_void_p,
    EXEC_FN, ctypes.c_void_p,
    CANCEL_FN, ctypes.c_void_p,
]
lib.snt_serial_feed.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
lib.snt_serial_notify_synthesizing.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
lib.snt_serial_notify_playing.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
lib.snt_serial_notify_done.argtypes = [
    ctypes.c_void_p, ctypes.c_uint32,
    ctypes.c_uint32, ctypes.c_uint32,
    ctypes.c_uint64, ctypes.c_uint64,
    ctypes.c_uint32
]
lib.snt_serial_notify_error.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_char_p]
lib.snt_serial_notify_cancelled.argtypes = [ctypes.c_void_p, ctypes.c_uint32]


class TestSntSerialProtocol(unittest.TestCase):
    def setUp(self):
        self.output_lines = []
        self.exec_requests = []
        self.cancelled_count = 0

        @TX_FN
        def on_tx(data, length, user):
            line = data[:length].decode("utf-8")
            self.output_lines.append(line)

        @EXEC_FN
        def on_exec(req_ptr, user):
            # req_ptr points to snt_request_t
            # type (4 bytes), id (4 bytes)
            req_type = ctypes.cast(req_ptr, ctypes.POINTER(ctypes.c_int32))[0]
            req_id = ctypes.cast(req_ptr, ctypes.POINTER(ctypes.c_uint32))[1]
            self.exec_requests.append((req_type, req_id))

        @CANCEL_FN
        def on_cancel(user):
            self.cancelled_count += 1

        self.cb_tx = on_tx
        self.cb_exec = on_exec
        self.cb_cancel = on_cancel

        self.snt = SntSerialStruct()
        lib.snt_serial_init(
            ctypes.byref(self.snt),
            self.cb_tx, None,
            self.cb_exec, None,
            self.cb_cancel, None
        )

    def feed(self, b: bytes):
        lib.snt_serial_feed(ctypes.byref(self.snt), b, len(b))

    def parse_events(self):
        events = []
        for line in self.output_lines:
            self.assertTrue(line.startswith("@SNT\t"), f"Line missing @SNT prefix: {line!r}")
            self.assertTrue(line.endswith("\n"), f"Line missing LF: {line!r}")
            json_str = line[len("@SNT\t"):-1]
            try:
                ev = json.loads(json_str)
                events.append(ev)
            except Exception as e:
                self.fail(f"Invalid JSON in {line!r}: {e}")
        return events

    def test_hello(self):
        self.feed(b"HELLO\t1\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 1)
        self.assertEqual(events[0]["event"], "hello")
        self.assertEqual(events[0]["version"], "1.0.0")
        self.assertIn("HELLO", events[0]["commands"])
        self.assertEqual(events[0]["limits"]["max_line"], 1024)
        self.assertEqual(events[0]["limits"]["max_phonemes"], 256)

    def test_say_flow_with_notifications(self):
        self.feed(b"SAY\t10\t\xe4\xbd\xa0\xe5\xa5\xbd\n")  # "你好"
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 10)
        self.assertEqual(events[0]["event"], "accepted")
        self.assertEqual(len(self.exec_requests), 1)
        self.assertEqual(self.exec_requests[0][1], 10)

        # Notify synthesizing and playing
        lib.snt_serial_notify_synthesizing(ctypes.byref(self.snt), 10)
        lib.snt_serial_notify_playing(ctypes.byref(self.snt), 10)
        lib.snt_serial_notify_done(ctypes.byref(self.snt), 10, 16000, 16000, 500000, 1000000, 0)

        events = self.parse_events()
        self.assertEqual(len(events), 4)
        self.assertEqual(events[1]["event"], "synthesizing")
        self.assertEqual(events[2]["event"], "playing")
        self.assertEqual(events[3]["event"], "done")
        self.assertEqual(events[3]["samples"], 16000)

    def test_crlf_and_lf(self):
        self.feed(b"HELLO\t1\r\n")
        self.feed(b"HELLO\t2\n")
        events = self.parse_events()
        self.assertEqual(len(events), 2)
        self.assertEqual(events[0]["id"], 1)
        self.assertEqual(events[1]["id"], 2)

    def test_packet_sticking_nianbao(self):
        # Multiple commands in one feed
        self.feed(b"HELLO\t100\nHELLO\t101\nHELLO\t102\n")
        events = self.parse_events()
        self.assertEqual(len(events), 3)
        self.assertEqual([e["id"] for e in events], [100, 101, 102])

    def test_packet_splitting_chaibao(self):
        # Feeding 1 byte at a time
        msg = b"HELLO\t200\n"
        for byte in msg:
            self.feed(bytes([byte]))
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 200)

    def test_utf8_split_across_chunks(self):
        # Chinese character "你" is 3 bytes: 0xE4 0xBD 0xA0
        # Split across feeds
        self.feed(b"SAY\t300\t")
        self.feed(b"\xe4")
        self.feed(b"\xbd")
        self.feed(b"\xa0\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 300)
        self.assertEqual(events[0]["event"], "accepted")

    def test_1024_byte_boundary(self):
        # Line of exactly 1024 bytes (including LF)
        prefix = b"SAY\t400\t"
        payload_len = 1024 - len(prefix) - 1 # 1 for \n
        payload = b"A" * payload_len
        line = prefix + payload + b"\n"
        self.assertEqual(len(line), 1024)

        self.feed(line)
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 400)
        self.assertEqual(events[0]["event"], "accepted")

    def test_overflow_and_recovery(self):
        # Line longer than 1024 bytes
        overflow = b"SAY\t500\t" + b"X" * 1050 + b"\n"
        self.feed(overflow)

        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "error")
        self.assertEqual(events[0]["code"], -2)  # SNT_E2BIG

        # Now send a valid command immediately after overflow: must parse normally!
        self.output_lines.clear()
        self.feed(b"HELLO\t501\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 501)
        self.assertEqual(events[0]["event"], "hello")

    def test_invalid_ids(self):
        # Non-numeric ID
        self.feed(b"HELLO\tabc\n")
        # Negative ID
        self.feed(b"HELLO\t-5\n")
        # Missing ID
        self.feed(b"HELLO\t\n")
        events = self.parse_events()
        self.assertEqual(len(events), 3)
        for ev in events:
            self.assertEqual(ev["event"], "error")
            self.assertEqual(ev["code"], -1)

    def test_duplicate_id(self):
        self.feed(b"HELLO\t600\n")
        self.feed(b"HELLO\t600\n")
        events = self.parse_events()
        self.assertEqual(len(events), 2)
        self.assertEqual(events[0]["event"], "hello")
        self.assertEqual(events[1]["event"], "error")
        self.assertEqual(events[1]["code"], -1)
        self.assertIn("duplicate", events[1]["msg"])

    def test_phonemes_parsing(self):
        # Valid CSV
        self.feed(b"PHONEMES\t700\t1,12,35,2\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "accepted")

        # Invalid CSV (letters)
        self.output_lines.clear()
        self.feed(b"PHONEMES\t701\t1,abc,3\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["code"], -1)

        # Invalid CSV (negative numbers)
        self.output_lines.clear()
        self.feed(b"PHONEMES\t702\t1,-5,3\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["code"], -1)

        # > 256 phonemes
        self.output_lines.clear()
        long_csv = ",".join(str(i) for i in range(257)).encode()
        self.feed(b"PHONEMES\t703\t" + long_csv + b"\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["code"], -2)  # SNT_E2BIG

    def test_busy_queue(self):
        # 1 active + 1 pending + 1 rejected as BUSY
        self.feed(b"SAY\t801\tFirst\n")
        self.feed(b"SAY\t802\tSecond\n")
        self.feed(b"SAY\t803\tThird\n")

        events = self.parse_events()
        self.assertEqual(len(events), 3)
        self.assertEqual(events[0]["id"], 801)
        self.assertEqual(events[0]["event"], "accepted")
        self.assertEqual(events[1]["id"], 802)
        self.assertEqual(events[1]["event"], "accepted")
        self.assertEqual(events[2]["id"], 803)
        self.assertEqual(events[2]["event"], "error")
        self.assertEqual(events[2]["code"], -4)  # SNT_EBUSY

    def test_stop_priority_cancellation(self):
        # Queue active (901) and pending (902)
        self.feed(b"SAY\t901\tFirst\n")
        self.feed(b"SAY\t902\tSecond\n")
        self.output_lines.clear()

        # Send STOP
        self.feed(b"STOP\t903\n")
        self.assertEqual(self.cancelled_count, 1)

        events = self.parse_events()
        # Active cancelled, pending cancelled, STOP accepted & done
        event_types = [(e["id"], e["event"]) for e in events]
        self.assertEqual(event_types, [
            (901, "cancelled"),
            (902, "cancelled"),
            (903, "accepted"),
            (903, "done")
        ])

        # New request after STOP works immediately
        self.output_lines.clear()
        self.feed(b"SAY\t904\tAfterStop\n")
        events = self.parse_events()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["id"], 904)
        self.assertEqual(events[0]["event"], "accepted")

    def test_stop_when_idle(self):
        self.feed(b"STOP\t1000\n")
        events = self.parse_events()
        self.assertEqual(len(events), 2)
        self.assertEqual(events[0]["event"], "accepted")
        self.assertEqual(events[1]["event"], "done")


if __name__ == "__main__":
    unittest.main()
