#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Rank the statics the sampled functions address, for plat/n64/hot.ld's data list.

usage: data-rank.py ENGINE_LOG PROFILE_ELF [CART_ELF] [TOP]
ENGINE_LOG holds the `pc-sample:` lines of plat/n64/hot-profile.c and PROFILE_ELF
is the ELF that probe ran in (as for pc-rank.py). Every function's weight is its
share of the samples. CART_ELF (default PROFILE_ELF) is the ELF to read the data
addresses from, so the placement being tuned need not contain the probe; the
functions are matched by name. For each function, the disassembly is scanned for
`lui` / `lw|sw|addiu` pairs and gp-relative accesses; each address is resolved to
the static object (data, bss, rodata) that contains it.

Statistical and static: a function's weight says how often it runs, not how many
of its addresses it touches per call, and the register tracking does not follow
branches, so a few false pairs remain. It ranks candidates for the hot data
group; the placement itself is measured (docs/architecture.md section 6.2).
NM and OBJDUMP select the binutils (default $N64_GCCPREFIX/bin/mips64-elf-*).
"""
import bisect
import collections
import os
import pathlib
import re
import subprocess
import sys

args = sys.argv[1:]
top = int(args.pop()) if args and args[-1].isdigit() else 60
if len(args) not in (2, 3):
    raise SystemExit(__doc__)
log, profile_elf = args[:2]
cart_elf = args[2] if len(args) == 3 else profile_elf
prefix = pathlib.Path(os.environ.get("N64_GCCPREFIX", "/opt/mips64-toolchain")) / "bin/mips64-elf-"
nm = os.environ.get("NM") or f"{prefix}nm"
objdump = os.environ.get("OBJDUMP") or f"{prefix}objdump"


def symbols(elf):
    funcs, objects, gp = [], [], None
    text = subprocess.run([nm, "-S", "-n", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[2] in "tTwW":
            funcs.append((int(fields[0], 16), int(fields[1], 16), fields[3]))
        elif len(fields) == 4 and fields[2] in "dDbBrRsSgG":
            objects.append((int(fields[0], 16), max(int(fields[1], 16), 1), fields[3], fields[2]))
        elif len(fields) == 3 and fields[2] == "_gp":
            gp = int(fields[0], 16)
    return sorted(funcs), sorted(objects), gp


def lookup(table, starts, address):
    i = bisect.bisect_right(starts, address) - 1
    found = None
    for k in range(max(0, i - 3), min(len(table), i + 4)):  # aliases share a start address
        if table[k][0] <= address < table[k][0] + table[k][1]:
            found = table[k]
    return found


p_funcs, _, _ = symbols(profile_elf)
p_starts = [f[0] for f in p_funcs]
samples = collections.Counter()
for match in re.finditer(r"^pc-sample: ([0-9a-f]+)", pathlib.Path(log).read_text(errors="replace"), re.M):
    hit = lookup(p_funcs, p_starts, int(match[1], 16))
    samples[hit[2] if hit else None] += 1
total = sum(samples.values())
if not total:
    raise SystemExit("no pc-sample lines in " + log)

funcs, objects, gp = symbols(cart_elf)
o_starts = [o[0] for o in objects]
text = subprocess.run([objdump, "-d", "-M", "no-aliases", "--no-show-raw-insn", "--section=.text", cart_elf],
                      capture_output=True, text=True, check=True).stdout
BRANCH = ("b", "j")
NO_DEST = ("sw", "sb", "sh", "sd", "swc1", "sdc1", "swl", "swr", "sc", "beq", "bne", "bgez", "bltz", "blez", "bgtz")
users = collections.defaultdict(collections.Counter)
current, high, delay = None, {}, False
for line in text.splitlines():
    header = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
    if header:
        current, high, delay = header[1], {}, False
        continue
    insn = re.match(r"\s*[0-9a-f]+:\s+(\S+)\s*(.*)$", line)
    if not insn or not current:
        continue
    op, operands = insn[1], insn[2].split("#")[0].split("<")[0].strip()
    clear = delay  # the delay slot of the previous branch is the last user of the tracked registers
    delay = op.startswith(BRANCH) and op != "break"
    if op == "lui":
        pair = re.match(r"(\S+),0x([0-9a-f]+)", operands)
        if pair:
            high[pair[1]] = int(pair[2], 16) << 16
    else:
        base = offset = None
        mem = re.match(r"(\S+),(-?\d+|0x[0-9a-f]+)\((\S+)\)$", operands)
        imm = re.match(r"(\S+),(\S+),(-?\d+|0x[0-9a-f]+)$", operands)
        if mem:
            offset, base = int(mem[2], 0), mem[3]
        elif imm and op in ("addiu", "daddiu"):
            offset, base = int(imm[3], 0), imm[2]
        if base is not None:
            address = gp + offset if base in ("gp", "$gp") and gp else (high[base] + offset) & 0xFFFFFFFF if base in high else None
            if address is not None and 0x80000000 <= address < 0x80800000:
                hit = lookup(objects, o_starts, address)
                if hit:
                    users[hit[2]][current] += 1
        dest = re.match(r"(\S+),", operands)
        if dest and op not in NO_DEST:
            high.pop(dest[1], None)
    if clear:
        high = {}

by_name = {o[2]: o for o in objects}
rows = []
for name, functions in users.items():
    weight = sum(samples[f] for f in functions)
    if weight:
        rows.append((weight, name, functions))
rows.sort(key=lambda r: -r[0])
print(f"samples {total}")
for weight, name, functions in rows[:top]:
    address, size, _, kind = by_name[name]
    best = sorted(functions, key=lambda f: -samples[f])[:4]
    print(f"{100 * weight / total:5.1f}% {address:08x} {size:6d} {kind} {name:<28} " + ",".join(f"{f}:{samples[f]}" for f in best))
