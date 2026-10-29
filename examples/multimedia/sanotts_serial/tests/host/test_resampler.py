#!/usr/bin/env python3
"""
Host unit tests for sanoTTS Resampler (snt_resampler.c).
Directly tests compiled C code via ctypes.
"""

import os
import math
import ctypes
import unittest
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "../.."))
SRC_DIR = os.path.join(APP_DIR, "src")
SO_PATH = os.path.join(SCRIPT_DIR, "libsnt_resampler.so")

def build_shared_lib():
    c_file = os.path.join(SRC_DIR, "snt_resampler.c")
    include_dir = SRC_DIR
    cmd = [
        "gcc", "-O2", "-fPIC", "-shared",
        "-I", include_dir,
        c_file, "-o", SO_PATH, "-lm"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Failed to compile snt_resampler.c:\n{res.stderr}")

build_shared_lib()
lib = ctypes.CDLL(SO_PATH)

class SntResamplerStruct(ctypes.Structure):
    _fields_ = [
        ("dummy", ctypes.c_byte * 256)
    ]

lib.snt_resampler_init.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
lib.snt_resampler_init.restype = ctypes.c_int

lib.snt_resampler_reset.argtypes = [ctypes.c_void_p]

lib.snt_resampler_process.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float), ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t)
]
lib.snt_resampler_process.restype = ctypes.c_int

lib.snt_resampler_flush.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t)
]
lib.snt_resampler_flush.restype = ctypes.c_int

lib.snt_float_to_pcm16.argtypes = [ctypes.c_float]
lib.snt_float_to_pcm16.restype = ctypes.c_int16


class TestSntResampler(unittest.TestCase):
    def test_float_to_pcm16(self):
        # Normal range
        self.assertEqual(lib.snt_float_to_pcm16(0.0), 0)
        self.assertEqual(lib.snt_float_to_pcm16(1.0), 32767)
        self.assertEqual(lib.snt_float_to_pcm16(-1.0), -32768)

        # Clamping
        self.assertEqual(lib.snt_float_to_pcm16(2.5), 32767)
        self.assertEqual(lib.snt_float_to_pcm16(-3.0), -32768)

        # NaN
        nan_val = float("nan")
        self.assertEqual(lib.snt_float_to_pcm16(nan_val), 0)

    def test_passthrough(self):
        resampler = SntResamplerStruct()
        self.assertEqual(lib.snt_resampler_init(ctypes.byref(resampler), 16000, 16000), 0)

        in_data = [0.0, 0.5, -0.5, 0.99]
        c_in = (ctypes.c_float * len(in_data))(*in_data)
        c_out = (ctypes.c_int16 * 10)()
        written = ctypes.c_size_t(0)

        ret = lib.snt_resampler_process(
            ctypes.byref(resampler),
            c_in, len(in_data),
            c_out, 10,
            ctypes.byref(written)
        )
        self.assertEqual(ret, 0)
        self.assertEqual(written.value, len(in_data))
        self.assertEqual(c_out[0], 0)
        self.assertAlmostEqual(c_out[1], 16383, delta=2)
        self.assertAlmostEqual(c_out[2], -16383, delta=2)

    def _generate_sine(self, rate, freq, duration, amp=0.5):
        n = int(rate * duration)
        return [amp * math.sin(2.0 * math.pi * freq * (i / rate)) for i in range(n)]

    def _estimate_freq(self, samples, rate):
        # Zero-crossing rate
        crossings = 0
        for i in range(1, len(samples)):
            if (samples[i - 1] < 0 and samples[i] >= 0):
                crossings += 1
        dur = len(samples) / rate
        return crossings / dur if dur > 0 else 0

    def _rms(self, samples):
        return math.sqrt(sum(s * s for s in samples) / len(samples))

    def test_resample_22050_to_16000(self):
        resampler = SntResamplerStruct()
        self.assertEqual(lib.snt_resampler_init(ctypes.byref(resampler), 22050, 16000), 0)

        duration = 0.5  # 0.5s
        sine = self._generate_sine(22050, 440.0, duration, amp=0.8)
        c_in = (ctypes.c_float * len(sine))(*sine)
        out_cap = int(16000 * duration * 1.5)
        c_out = (ctypes.c_int16 * out_cap)()
        written = ctypes.c_size_t(0)

        ret = lib.snt_resampler_process(
            ctypes.byref(resampler),
            c_in, len(sine),
            c_out, out_cap,
            ctypes.byref(written)
        )
        self.assertEqual(ret, 0)

        # Flush
        flush_cap = 100
        c_flush = (ctypes.c_int16 * flush_cap)()
        flush_written = ctypes.c_size_t(0)
        lib.snt_resampler_flush(ctypes.byref(resampler), c_flush, flush_cap, ctypes.byref(flush_written))

        total_samples = [c_out[i] for i in range(written.value)] + [c_flush[i] for i in range(flush_written.value)]
        expected_samples = int(16000 * duration)
        self.assertAlmostEqual(len(total_samples), expected_samples, delta=10)

        est_freq = self._estimate_freq(total_samples, 16000)
        self.assertAlmostEqual(est_freq, 440.0, delta=5.0)

    def test_resample_24000_to_16000(self):
        resampler = SntResamplerStruct()
        self.assertEqual(lib.snt_resampler_init(ctypes.byref(resampler), 24000, 16000), 0)

        duration = 0.5
        sine = self._generate_sine(24000, 440.0, duration, amp=0.8)
        c_in = (ctypes.c_float * len(sine))(*sine)
        out_cap = int(16000 * duration * 1.5)
        c_out = (ctypes.c_int16 * out_cap)()
        written = ctypes.c_size_t(0)

        ret = lib.snt_resampler_process(
            ctypes.byref(resampler),
            c_in, len(sine),
            c_out, out_cap,
            ctypes.byref(written)
        )
        self.assertEqual(ret, 0)

        est_freq = self._estimate_freq([c_out[i] for i in range(written.value)], 16000)
        self.assertAlmostEqual(est_freq, 440.0, delta=5.0)

    def test_anti_aliasing_attenuation(self):
        # 9000 Hz at 22050 Hz sampling rate is above 16000 Hz Nyquist (8000 Hz).
        # Must be attenuated by > 30 dB compared to passband.
        resampler = SntResamplerStruct()
        self.assertEqual(lib.snt_resampler_init(ctypes.byref(resampler), 22050, 16000), 0)

        passband = self._generate_sine(22050, 1000.0, 0.1, amp=0.8)
        c_pb = (ctypes.c_float * len(passband))(*passband)
        c_out1 = (ctypes.c_int16 * 2000)()
        w1 = ctypes.c_size_t(0)
        lib.snt_resampler_process(ctypes.byref(resampler), c_pb, len(passband), c_out1, 2000, ctypes.byref(w1))
        rms_pb = self._rms([c_out1[i] for i in range(w1.value)])

        lib.snt_resampler_reset(ctypes.byref(resampler))

        stopband = self._generate_sine(22050, 9500.0, 0.1, amp=0.8)
        c_sb = (ctypes.c_float * len(stopband))(*stopband)
        c_out2 = (ctypes.c_int16 * 2000)()
        w2 = ctypes.c_size_t(0)
        lib.snt_resampler_process(ctypes.byref(resampler), c_sb, len(stopband), c_out2, 2000, ctypes.byref(w2))
        rms_sb = self._rms([c_out2[i] for i in range(w2.value)])

        attenuation_db = 20 * math.log10(rms_sb / rms_pb)
        print(f"Anti-aliasing attenuation at 9.5 kHz: {attenuation_db:.1f} dB")
        self.assertLess(attenuation_db, -30.0)

    def test_chunked_processing_continuity(self):
        # Compare feeding all at once vs feeding in small 64-sample chunks
        sine = self._generate_sine(22050, 440.0, 0.2, amp=0.8)

        # 1. All at once
        r1 = SntResamplerStruct()
        lib.snt_resampler_init(ctypes.byref(r1), 22050, 16000)
        c_in_all = (ctypes.c_float * len(sine))(*sine)
        c_out1 = (ctypes.c_int16 * 5000)()
        w1 = ctypes.c_size_t(0)
        lib.snt_resampler_process(ctypes.byref(r1), c_in_all, len(sine), c_out1, 5000, ctypes.byref(w1))
        all_samples = [c_out1[i] for i in range(w1.value)]

        # 2. In chunks of 64
        r2 = SntResamplerStruct()
        lib.snt_resampler_init(ctypes.byref(r2), 22050, 16000)
        chunk_samples = []
        chunk_size = 64
        for i in range(0, len(sine), chunk_size):
            chunk = sine[i:i + chunk_size]
            c_chunk = (ctypes.c_float * len(chunk))(*chunk)
            c_out2 = (ctypes.c_int16 * 500)()
            w2 = ctypes.c_size_t(0)
            lib.snt_resampler_process(ctypes.byref(r2), c_chunk, len(chunk), c_out2, 500, ctypes.byref(w2))
            chunk_samples.extend([c_out2[j] for j in range(w2.value)])

        # Compare outputs
        min_len = min(len(all_samples), len(chunk_samples))
        self.assertGreater(min_len, 2000)
        diffs = [abs(all_samples[i] - chunk_samples[i]) for i in range(min_len)]
        max_diff = max(diffs)
        self.assertLessEqual(max_diff, 1)  # At most 1 LSB rounding difference!


if __name__ == "__main__":
    unittest.main()
