#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Decode the QR symbol of the tr64 hardware probe (tools/hwprobe/probe.c).

    decode.py [--image photo.png | TEXT]

TEXT is `TR64.P1.<base-45>.` as a phone shows it. With --image the symbol is
read with zxing-cpp or OpenCV (private venv, docs/architecture.md section 3.1).
Prints every verdict the screen shows, plus the exception records (vector,
ExcCode, EPC, BadVAddr) the screen does not have room for.
"""
import argparse
import importlib.util
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("fatal_decode", ROOT / "tools/fatal/decode.py")
fatal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fatal)
LETTER = "PFXD"
ALIAS = "PAFX"
STALE = "PFFX"
MISS = ["M1 load miss KX=1 (want vec 1 exc 2)", "M2 load miss KX=0 (want vec 0 exc 2)",
        "M3 store miss KX=1 (want vec 1 exc 3)", "M4 fetch miss KX=1 (want vec 1 exc 2, EPC=BadVAddr)",
        "M5 load of V=0 page (want vec 3 exc 2)", "M6 store to D=0 page (want vec 3 exc 1)",
        "M7 load miss with EXL set (want vec 3 exc 2, EPC unchanged 0x12345678)"]


def parse(text):
    text = text.strip()
    if not text.startswith("TR64.P1.") or not text.endswith("."):
        raise ValueError("not a hardware probe record (TR64.P1.<base-45>.)")
    data = fatal.base45_decode(text[len("TR64.P1."):-1])
    if len(data) < 97 or data[0] != 1:
        raise ValueError(f"record of {len(data)} bytes, version {data[0] if data else None}")
    return data


def cells(data, pos, count, bits):
    out = []
    for i in range(count):
        byte = data[pos + i * bits // 8]
        shift = 8 - bits - (i * bits) % 8
        out.append((byte >> shift) & ((1 << bits) - 1))
    return out


def report(data):
    u32 = lambda p: int.from_bytes(data[p:p + 4], "big")
    lines = [f"PRId {u32(1):08X}  Status at start {u32(5):08X}  Wired {data[9]}"]
    t = data[10]
    names = ["T1 TLB 1 MiB readback and data", "T2 TLB 4 KiB pair readback and data", "T3 TLBP", None, "T5 execute through the 1 MiB mapping"]
    for name, v in zip([names[0], names[1], names[2], names[4]], [t >> 6, t >> 4 & 3, t >> 2 & 3, t & 3]):
        lines.append(f"{name:42} {LETTER[v]}")
    bits = int.from_bytes(data[11:13], "big")
    n = 0
    for style in "AB":
        for mode in "10":
            row = "".join("P" if not (bits >> (15 - n - i)) & 1 else "x" for i in range(4))
            n += 4
            lines.append(f"DATA style {style} KX={mode}: 1B {row[:2]}  1F {row[2:]}   (x = not pass; screen letter has the kind)")
    pos = 13
    for stub in ("JR", "LW"):
        for style in "AB":
            for mode in "10":
                c = [LETTER[v] for v in cells(data, pos, 8, 2)]
                pos += 2
                lines.append(f"EXEC {stub} style {style} KX={mode}: 1B {''.join(c[:4])}  1F {''.join(c[4:])}"
                             "   (cells: cache-op none/invalidate x fill DMA/CPU)")
    seq = data[pos]
    lines.append(f"SEQ  tlbcode_load sequence + snapshot: {LETTER[seq >> 2 & 3]}   without snapshot: {LETTER[seq & 3]}")
    pos += 1
    a = data[pos]
    ku = data[pos + 2]
    lines.append(f"ALIAS fetch at VA 0xC0200000 (mapped returns 1; physical alias returns 2): KX=1 {ALIAS[a >> 6]}  KX=0 {ALIAS[a >> 4 & 3]}"
                 f"; at kuseg 0x50000000 KX=1 {ALIAS[ku & 3]}   (P mapped fetch, A physical alias taken, F other value, X fault)")
    lines.append(f"ICACHE Hit_Invalidate_I on a mapped address of a different colour: {STALE[a >> 2 & 3]}   (F = stale line)")
    lines.append(f"SPEED kseg2 alias of kseg0 code: {LETTER[a & 3]}  ratio {data[pos + 1] / 16:.2f}x of kseg0 time")
    pos += 3
    for i, name in enumerate(MISS + ["SEQ fault record (first fault of a failing SEQ step)"]):
        b = data[pos]
        epc = int.from_bytes(data[pos + 1:pos + 4], "big")
        badv = u32(pos + 4)
        pos += 8
        lines.append(f"{name}: {'FAIL' if b >> 7 else 'PASS'} vec {b >> 5 & 3} exc {b & 31} EPC low24 {epc:06X} BadVAddr low32 {badv:08X}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("text", nargs="?")
    parser.add_argument("--image")
    args = parser.parse_args()
    text = fatal.decode_image(args.image) if args.image else args.text or sys.stdin.read()
    print(report(parse(text)))


if __name__ == "__main__":
    main()
