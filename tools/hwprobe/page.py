# SPDX-License-Identifier: MIT
"""The 8 KiB page pair the hardware probe DMAs from the cart (probe.c image_build).

Page 0: break words, then the stubs: +0x00 ori v0,zero,0x5A5A; jr ra; nop and
+0x20 lw v0,0x20(t9); jr ra; nop (t9 = entry, so it reads +0x40 = 0xC0DEC0DE),
words 0xD0A70000 + page*0x10000 + offset/4 from +0x100. Page 1: the same
words from +0. The last word is "HWPR".
"""
import struct
import sys


def word(page, offset):
    return 0xD0A70000 + page * 0x10000 + (offset >> 2)


def build():
    w = [0x0000000D] * 2048
    w[0:3] = [0x34025A5A, 0x03E00008, 0]
    w[8:11] = [0x8F220020, 0x03E00008, 0]
    w[16] = 0xC0DEC0DE
    for i in range(0x100 // 4, 1024):
        w[i] = word(0, i * 4)
    for i in range(1024):
        w[1024 + i] = word(1, i * 4)
    w[2047] = 0x48575052
    return struct.pack(">2048I", *w)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as out:
        out.write(build())
