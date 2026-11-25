#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Rank functions by the `pc-sample:` lines plat/n64/hot-profile.c logs.

usage: pc-rank.py ENGINE_LOG CART_ELF [TOP]
Statistical occupancy (a timer samples the interrupted PC): a ranking aid for
plat/n64/hot.ld, not cycle or cache-miss counts. NM selects the nm binary
(default $N64_GCCPREFIX/bin/mips64-elf-nm).
"""
import bisect
import collections
import os
import pathlib
import re
import subprocess
import sys

if len(sys.argv) not in (3, 4):
    raise SystemExit(__doc__)
log, elf = sys.argv[1:3]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 60
nm = os.environ.get("NM") or str(pathlib.Path(os.environ.get("N64_GCCPREFIX", "/opt/mips64-toolchain")) / "bin/mips64-elf-nm")
text = subprocess.run([nm, "-S", "-n", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
funcs = []
for line in text.splitlines():
    fields = line.split()
    if len(fields) == 4 and fields[2] in "tTwW":
        funcs.append((int(fields[0], 16), int(fields[1], 16), fields[3]))
funcs.sort()
starts = [f[0] for f in funcs]
counts = collections.Counter()
total = 0
for match in re.finditer(r"^pc-sample: ([0-9a-f]+)$", pathlib.Path(log).read_text(errors="replace"), re.M):
    pc = int(match[1], 16) & 0xFFFFFFFF
    i = bisect.bisect_right(starts, pc) - 1
    name = f"?{pc:08x}"
    if i >= 0 and pc < funcs[i][0] + max(funcs[i][1], 4):
        name = funcs[i][2]
    counts[name] += 1
    total += 1
if not total:
    raise SystemExit("pc-rank: no pc-sample lines")
size = {f[2]: f[1] for f in funcs}
address = {f[2]: f[0] for f in funcs}
print(f"samples {total}")
cumulative = 0
for name, n in counts.most_common(top):
    cumulative += size.get(name, 0)
    print(f"{n:5d} {100 * n / total:5.1f}% size={size.get(name, 0):5d} cum_bytes={cumulative:6d} {address.get(name, 0):08x} {name}")
