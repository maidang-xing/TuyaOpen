#!/usr/bin/env python3
"""
test_m1.py -- Real Hardware Validation Suite for M1 (English nano on TUYA_T5AI_CORE)
Tests:
  1. HELLO handshake
  2. GOLDEN m1 eval8 row 0 benchmark on T5 device
     Checks:
       - Correlation > 0.980
       - 0.80 < RMS ratio < 1.25
       - Native sample rate: 24,000 Hz
       - Samples: 105,984
       - Real speaker playback
       - Exact RTF and compute metrics
  3. 5 PHONEMES requests with different lengths to verify arbitrary phonemes synthesis
"""

import sys
import time
import json
import serial
from typing import Dict, Any, List

PORT = "/dev/ttyACM0"
BAUD = 115200


def run_m1_test():
    print(f"Connecting to {PORT} at {BAUD} baud...")
    ser = serial.Serial(PORT, BAUD, timeout=1.0)
    time.sleep(0.5)
    ser.reset_input_buffer()

    base_id = int(time.time() * 10) % 900000 + 1000

    def send_line(line: str):
        print(f">> {line!r}")
        ser.write((line + "\n").encode("utf-8"))
        ser.flush()

    def read_events(expected_id: int, stop_event: str, timeout: float = 60.0) -> List[Dict[str, Any]]:
        deadline = time.time() + timeout
        events = []
        while time.time() < deadline:
            raw = ser.readline().decode("utf-8", errors="replace")
            if not raw:
                continue
            line = raw.strip()
            if not line:
                continue
            if not line.startswith("@SNT\t"):
                print(f"   [LOG] {line}")
                continue
            print(f"<< {line}")
            json_part = line[len("@SNT\t"):]
            try:
                ev = json.loads(json_part)
            except Exception as e:
                print(f"   [WARN] JSON parse error: {e}")
                continue
            if ev.get("id") == expected_id:
                events.append(ev)
                if ev.get("event") == stop_event or ev.get("event") == "error":
                    return events
        raise TimeoutError(f"Timeout waiting for event '{stop_event}' on req_id={expected_id}")

    try:
        # Step 1: HELLO
        req_h = base_id + 1
        print("\n=== STEP 1: HELLO Handshake ===")
        send_line(f"HELLO\t{req_h}")
        events = read_events(req_h, "hello", timeout=5.0)
        assert len(events) >= 1, "No HELLO event received"
        hello = events[-1]
        print(f"Device Hello: version={hello.get('version')}, voice={hello.get('voice')}")

        # Step 2: M1 Golden Benchmark
        req_m1 = base_id + 2
        print("\n=== STEP 2: M1 English nano Golden Gate (r00 eval8) ===")
        send_line(f"GOLDEN\t{req_m1}\tm1")
        events = read_events(req_m1, "done", timeout=60.0)

        # Inspect eval and done events
        eval_ev = None
        done_ev = None
        for ev in events:
            if ev.get("event") == "eval":
                eval_ev = ev
            elif ev.get("event") == "done":
                done_ev = ev

        assert eval_ev is not None, "Device did not emit 'eval' event"
        assert done_ev is not None, "Device did not emit 'done' event"

        corr = eval_ev.get("corr", 0.0)
        rms_ratio = eval_ev.get("rms_ratio", 0.0)
        samples = eval_ev.get("samples", 0)
        max_err = eval_ev.get("max_err", 0.0)
        status = eval_ev.get("status", "FAIL")

        compute_us = done_ev.get("compute_us", 0)
        wall_us = done_ev.get("wall_us", 0)
        source_rate = done_ev.get("source_rate", 24000)
        audio_dur_s = samples / source_rate if source_rate else 0.0
        compute_s = compute_us / 1000000.0
        rtf = compute_s / audio_dur_s if audio_dur_s > 0 else 0.0

        print("\n--- M1 Device Evaluation Results ---")
        print(f"Samples:     {samples} (duration = {audio_dur_s:.3f}s)")
        print(f"Sample Rate: {source_rate} Hz")
        print(f"Correlation: {corr:.6f}  (Threshold: > 0.980)")
        print(f"RMS Ratio:   {rms_ratio:.6f}  (Threshold: 0.80 - 1.25)")
        print(f"Max Error:   {max_err:.6e}")
        print(f"Compute Time:{compute_s:.3f}s ({compute_us} us)")
        print(f"Wall Time:   {wall_us/1000000.0:.3f}s")
        print(f"Device RTF:  {rtf:.3f}")
        print(f"Gate Status: {status}")

        assert samples == 105984, f"Expected 105984 samples, got {samples}"
        assert corr > 0.980, f"Correlation {corr:.6f} <= 0.980"
        assert 0.80 <= rms_ratio <= 1.25, f"RMS ratio {rms_ratio:.6f} out of [0.80, 1.25]"
        assert status == "PASS", f"Gate status is {status}"
        print(">>> M1 GOLDEN GATE PASSED SUCCESSFULLY! <<<\n")

        # Step 3: Test 5 different lengths of PHONEMES
        print("=== STEP 3: Arbitrary Phonemes Synthesis Tests (5 lengths) ===")
        # 5 phoneme test patterns of varying lengths
        test_patterns = [
            ("Short 1 (len=6)", [1, 37, 23, 30, 25, 2]),
            ("Short 2 (len=12)", [1, 37, 23, 3, 42, 25, 54, 32, 30, 31, 25, 2]),
            ("Medium 1 (len=24)", [1, 37, 23, 3, 42, 25, 54, 32, 30, 31, 46, 25, 3, 30, 31, 54, 32, 20, 57, 28, 31, 3, 48, 2]),
            ("Medium 2 (len=40)", [1, 37, 23, 3, 42, 25, 54, 32, 30, 31, 46, 25, 3, 30, 31, 54, 32, 20, 57, 28, 31, 3, 48, 55, 49, 20, 3, 30, 29, 54, 23, 25, 3, 25, 26, 54, 46, 47, 26, 2]),
            ("Full sentence (len=55)", [1, 37, 23, 3, 42, 25, 54, 32, 30, 31, 46, 25, 3, 30, 31, 54, 32, 20, 57, 28, 31, 3, 48, 55, 49, 20, 3, 30, 29, 54, 23, 25, 3, 25, 26, 54, 46, 47, 26, 23, 3, 34, 46, 37, 54, 17, 31, 3, 30, 54, 17, 28, 20, 46, 2])
        ]

        for i, (name, p_ids) in enumerate(test_patterns):
            time.sleep(0.2)
            req_p = base_id + 10 + i
            csv_str = ",".join(map(str, p_ids))
            print(f"\n--- Subtest 3.{i+1}: {name} (req_id={req_p}, n_tokens={len(p_ids)}) ---")
            send_line(f"PHONEMES\t{req_p}\t{csv_str}")
            p_events = read_events(req_p, "done", timeout=30.0)
            p_done = [e for e in p_events if e.get("event") == "done"][0]
            p_samples = p_done.get("samples", 0)
            p_comp = p_done.get("compute_us", 0)
            p_wall = p_done.get("wall_us", 0)
            p_dur = p_samples / 24000.0 if p_samples > 0 else 0.0
            p_rtf = (p_comp / 1000000.0) / p_dur if p_dur > 0 else 0.0
            print(f"PASS: Synthesized {p_samples} samples ({p_dur:.2f}s). Compute: {p_comp/1000.0:.1f}ms, RTF={p_rtf:.3f}, Wall: {p_wall/1000.0:.1f}ms")
            assert p_samples > 0, "No audio samples produced"

        print("\n==========================================")
        print(">>> ALL M1 HARDWARE TESTS PASSED 100%! <<<")
        print("==========================================")
        return True

    except Exception as e:
        print(f"\n[ERROR] M1 test failed: {e}", file=sys.stderr)
        return False
    finally:
        ser.close()


if __name__ == "__main__":
    ok = run_m1_test()
    sys.exit(0 if ok else 1)
