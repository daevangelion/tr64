# SPDX-License-Identifier: MIT
"""Fail an ELF (test or shipping) retaining a libc printf or decimal-FP formatter
engine, or, with --size-json, the SDK crash-inspector cluster."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("elf", type=Path)
p.add_argument("--nm", default=str(Path(os.environ.get("N64_GCCPREFIX", "/opt/mips64-toolchain")) / "bin/mips64-elf-nm"))
p.add_argument("--size-json", type=Path, help="size_gate report: also fail if the SDK inspector cluster is linked")
a = p.parse_args()
result = subprocess.run([a.nm, "--defined-only", "--format=posix", str(a.elf)],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
if result.returncode:
    sys.exit("formatter_link ERROR: " + result.stderr.strip())
stdio = {"printf", "iprintf", "fprintf", "fiprintf", "sprintf", "siprintf", "snprintf", "sniprintf",
         "asprintf", "asiprintf", "vprintf", "viprintf", "vfprintf", "vfiprintf", "vsprintf", "vsiprintf",
         "vsnprintf", "vsniprintf", "vasprintf", "vasiprintf", "svfprintf", "svfiprintf",
         "printf_i", "printf_float", "dtoa", "ldtoa", "ftoa"}
bad = []
for line in result.stdout.splitlines():
    fields = line.split()
    if not fields:
        continue
    name = fields[0]
    base = name.lstrip("_")
    if base.endswith("_r"):
        base = base[:-2]
    if base in stdio:
        bad.append(name)
if bad:
    print("formatter_link OVER: retained libc printf/float engines: " + ",".join(sorted(bad)))
    sys.exit(1)
if a.size_json:
    cluster = {"libdragon.a(inspector.o)", "libdragon.a(console.o)", "libdragon.a(graphics.o)", "libdragon.a(symtable.o)"}
    retained = cluster.intersection(json.loads(a.size_json.read_text())["objects"])
    if retained:
        print("formatter_link OVER: SDK crash inspector cluster linked: " + ",".join(sorted(retained)))
        sys.exit(1)
print("formatter_link PASS: no libc printf/decimal-FP engines" + (" or SDK inspector cluster" if a.size_json else "") + " in the ELF")
