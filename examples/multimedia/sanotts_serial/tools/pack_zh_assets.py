#!/usr/bin/env python3
"""
pack_zh_assets.py -- Pack and compress Chinese sanoTTS models & eSpeak cmn dictionary
for TUYA_T5AI_CORE 8MB Flash dedicated partition.

Files packed:
- front_f32.bin (IEEE-754 4-byte shuffled + zlib)
- dec_f32.bin   (IEEE-754 4-byte shuffled + zlib)
- cmn_dict      (zlib)
- phontab       (zlib)
- phonindex     (zlib)
- phondata      (zlib)
- intonations   (zlib)
- cmn           (zlib)
- meta.json     (zlib)

Output binary header:
- Magic: "SNT1" (0x534E5431)
- TOC with name, offset, comp_size, orig_size, crc32, flags
"""

import os
import sys
import zlib
import struct

MAGIC = 0x534E5431
FLAG_SHUFFLE_F32 = 1
FLAG_COMPRESSED   = 2

ENTRY_STRUCT = struct.Struct("<32sIIIIHH") # 32s + 4*I + 2*H = 32 + 16 + 4 = 52 bytes
MAX_ENTRIES = 16
HEADER_STRUCT = struct.Struct(f"<IIII{MAX_ENTRIES * 52}s") # magic, ver, n_entries, total_sz, entries_blob

def shuffle_f32(raw: bytes) -> bytes:
    assert len(raw) % 4 == 0
    n = len(raw) // 4
    b0 = bytearray(n)
    b1 = bytearray(n)
    b2 = bytearray(n)
    b3 = bytearray(n)
    for i in range(n):
        b0[i] = raw[i*4]
        b1[i] = raw[i*4+1]
        b2[i] = raw[i*4+2]
        b3[i] = raw[i*4+3]
    return bytes(b0 + b1 + b2 + b3)

def unshuffle_f32(shuffled: bytes) -> bytes:
    assert len(shuffled) % 4 == 0
    n = len(shuffled) // 4
    out = bytearray(len(shuffled))
    b0 = shuffled[0:n]
    b1 = shuffled[n:2*n]
    b2 = shuffled[2*n:3*n]
    b3 = shuffled[3*n:4*n]
    for i in range(n):
        out[i*4] = b0[i]
        out[i*4+1] = b1[i]
        out[i*4+2] = b2[i]
        out[i*4+3] = b3[i]
    return bytes(out)

def pack_assets(model_dir: str, out_path: str):
    file_list = [
        ("front_f32.bin", True),
        ("dec_f32.bin", True),
        ("cmn_dict", False),
        ("phontab", False),
        ("phonindex", False),
        ("phondata", False),
        ("intonations", False),
        ("cmn", False),
        ("meta.json", False),
    ]

    entries_data = []
    payloads = []
    current_offset = HEADER_STRUCT.size

    # First pass: prepare compressed payloads
    print(f"Header size: {HEADER_STRUCT.size} bytes")
    total_orig = 0
    total_comp = 0

    for fname, is_f32 in file_list:
        fpath = os.path.join(model_dir, fname)
        if not os.path.exists(fpath):
            raise FileNotFoundError(f"Missing required model asset: {fpath}")

        raw = open(fpath, "rb").read()
        orig_crc = zlib.crc32(raw)
        orig_sz = len(raw)
        total_orig += orig_sz

        flags = FLAG_COMPRESSED
        to_compress = raw
        if is_f32:
            flags |= FLAG_SHUFFLE_F32
            to_compress = shuffle_f32(raw)

        # Use maximum compression level 9
        comp = zlib.compress(to_compress, level=9)
        comp_sz = len(comp)
        total_comp += comp_sz

        print(f"  {fname:15s}: orig={orig_sz:8d} B -> comp={comp_sz:8d} B (ratio={comp_sz/orig_sz*100:5.1f}%, crc32=0x{orig_crc:08X}, flags=0x{flags:04X})")

        # Verify round-trip lossless decompression immediately
        decomp = zlib.decompress(comp)
        if is_f32:
            decomp = unshuffle_f32(decomp)
        assert decomp == raw, f"Bit-exact round-trip check FAILED for {fname}!"

        # Align payload to 4 bytes
        pad = (4 - (comp_sz % 4)) % 4
        payload = comp + (b"\x00" * pad)

        name_bytes = fname.encode("utf-8")
        entry_bytes = ENTRY_STRUCT.pack(name_bytes, current_offset, comp_sz, orig_sz, orig_crc, flags, 0)
        entries_data.append(entry_bytes)
        payloads.append(payload)

        current_offset += len(payload)

    # Pad remaining entries
    while len(entries_data) < MAX_ENTRIES:
        entries_data.append(b"\x00" * ENTRY_STRUCT.size)

    all_entries_bytes = b"".join(entries_data)
    total_size = current_offset

    header_bytes = HEADER_STRUCT.pack(MAGIC, 1, len(file_list), total_size, all_entries_bytes)
    assert len(header_bytes) == HEADER_STRUCT.size

    with open(out_path, "wb") as f:
        f.write(header_bytes)
        for p in payloads:
            f.write(p)

    out_sz = os.path.getsize(out_path)
    print(f"\nSuccessfully generated {out_path}:")
    print(f"  Total original data:   {total_orig} bytes ({total_orig/1024/1024:.2f} MiB)")
    print(f"  Total packed image:    {out_sz} bytes ({out_sz/1024/1024:.2f} MiB)")
    print(f"  Overall compression:   {out_sz / total_orig * 100:.1f}%")
    print(f"  Dedicated Flash limit: 6,078,464 bytes (5.79 MiB)")
    print(f"  Flash headroom:        {6078464 - out_sz} bytes ({(6078464 - out_sz)/1024:.1f} KiB)")
    assert out_sz <= 6078464, f"Packed image exceeds dedicated partition size limit ({out_sz} > 6078464)!"

if __name__ == "__main__":
    model_dir = sys.argv[1] if len(sys.argv) > 1 else "examples/multimedia/sanotts_serial/models/chinese"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "examples/multimedia/sanotts_serial/models/chinese/snt_zh_assets.bin"
    pack_assets(model_dir, out_path)
