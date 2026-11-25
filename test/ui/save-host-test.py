#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Two processes, exact wire CRCs, and fail-closed malformed-device controls."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def run(binary, operation, path):
    subprocess.run(["rtk", str(binary), operation, str(path)], check=True)


def header(bank, offset, length, magic, version=1):
    value = bytearray(bank[offset:offset + length])
    assert value[:4] == magic
    assert struct.unpack_from(">HH", value, 4) == (version, length)
    checksum = struct.unpack_from(">I", value, 16)[0]
    value[16:20] = bytes(4)
    assert zlib.crc32(value) == checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="host-sram-", dir=args.out) as temporary:
        path = Path(temporary) / "device.srm"
        run(args.binary, "write", path)
        bank = path.read_bytes()
        assert len(bank) == 32768
        # Directory: two copies (6000, 6080), sequence at 12; the four writes of the three saves
        # alternate them (1 fresh in copy 0, then 2 in copy 1, 3 in copy 0, 4 in copy 1).
        sequences = []
        for copy in range(2):
            offset = 24576 + 128 * copy
            header(bank, offset, 128, b"SD64", version=2)
            assert struct.unpack_from(">I", bank, offset + 8)[0] == 3
            assert struct.unpack_from(">3I", bank, offset + 20) == (32768, 6144, 4)
            assert bank[offset + 44:offset + 128] == bytes(84)
            sequences.append(struct.unpack_from(">I", bank, offset + 12)[0])
        assert sequences == [3, 4], sequences
        newest = 24576 + 128
        assert struct.unpack_from(">3I", bank, newest + 32) == (0, 1, 2), "slot -> region map"
        for slot, length in enumerate((6050, 3903, 381)):
            offset = 6144 * slot
            header(bank, offset, 64, b"SG64", version=3)
            assert struct.unpack_from(">I", bank, offset + 48)[0] == 0xa5000002 + slot, "level identity"
            assert bank[offset + 52:offset + 64] == bytes(12)
            actual, checksum = struct.unpack_from(">2I", bank, offset + 8)
            assert actual == length
            assert zlib.crc32(bank[offset + 64:offset + 64 + length]) == checksum
        assert bank[3 * 6144:4 * 6144] == b"\xff" * 6144, "the spare region is untouched"
        # Settings: two copies (6100, 7000); one write, so copy 0 holds it at sequence 1 and copy 1 is erased.
        header(bank, 24832, 32, b"ST64", version=2)
        assert struct.unpack_from(">2I", bank, 24840) == (32, zlib.crc32(bank[24864:24896]))
        assert struct.unpack_from(">I", bank, 24832 + 20)[0] == 1
        assert bank[24832 + 3840:24832 + 3840 + 32] == b"\xff" * 32
        run(args.binary, "read", path)
        assert path.read_bytes() == bank, "read-only session mutated the save device"
        path.write_bytes(bank[:100])
        run(args.binary, "io", path)
        assert path.read_bytes() == bank[:100], "malformed existing device was truncated/reformatted"
        corrupt = bytearray(bank)
        corrupt[24576 + 16] ^= 1
        corrupt[24576 + 128 + 16] ^= 1  # both copies: one intact copy is a usable directory
        path.write_bytes(corrupt)
        run(args.binary, "corrupt", path)
        assert path.read_bytes() == corrupt
    print("host SRAM power-cycle/wire PASS: independent CRCs, metadata, no truncation, fail-closed directory")


if __name__ == "__main__":
    main()
