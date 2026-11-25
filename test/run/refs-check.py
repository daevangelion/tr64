#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Gate the resident-to-unit references of a cart ELF (docs/architecture.md section 6).

usage: refs-check.py ELF.tlbcode.refs ALLOWLIST [--extra-allow LIST]... [--elf ELF --readelf PATH]
       refs-check.py --selftest

The tlbcode tool writes one line per relocation in resident text or data that
points into a code unit: `<.text|.data>:<symbol>@<addr> -> <unit>:<target>@<addr>`.
Resident code reaches a unit only through the dispatch table, so a resident
reference to unit text or data is a defect (a call into a unit that is not
loaded faults) unless it is listed. Accepted without a listing:

  .data:units -> <unit>:tr64_unit_<unit>     the dispatch rows of engine/code

Every other line (a `.text:` reference, or a `.data:` reference other than a
dispatch row) must match an entry of ALLOWLIST (addresses are not compared):

  <section>:<symbol> -> <unit>:<target>  [callers=<unit>]  # why it is safe

With `callers=UNIT` and --elf, the entry is accepted only if every relocation in
the ELF (--emit-relocs keeps them) against <symbol> lies in the unit UNIT's own
sections, that is, nothing resident can reach <symbol> while UNIT is not loaded.
Lines of the allow-list that match nothing are fine (builds without that unit).
--extra-allow names a further list of the same format that the caller passes only
for builds that link the test sources it covers (the Makefile, for the fatal
probes): a shipping build never sees those entries.
"""
import argparse
import collections
import pathlib
import re
import subprocess
import sys

REF = re.compile(r"^(\.\w+):([^@\s]+)@0x[0-9a-f]+ -> ([\w.]+):(\S+?)@0x[0-9a-f]+$")
ENTRY = re.compile(r"^(\.\w+):(\S+) -> ([\w.]+):(\S+)(?:\s+callers=(\w+))?\s*(?:#.*)?$")


def parse_allow(text):
    allowed = {}
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        if not match:
            raise ValueError(f"allow-list line {number}: not `section:symbol -> unit:target [callers=unit]`: {line}")
        allowed[match.groups()[:4]] = match[5]
    return allowed


def relocation_owners(readelf_text):
    """symbol -> set of output sections holding a relocation against it (debug sections ignored)."""
    owners = collections.defaultdict(set)
    section = None
    for line in readelf_text.splitlines():
        head = re.match(r"Relocation section '\.rel(\.[^']+)' ", line)
        if head:
            section = None if head[1].startswith((".debug", ".gnu_debuglink")) else head[1]
            continue
        fields = line.split()
        if section and len(fields) >= 5 and fields[2].startswith("R_MIPS_"):
            owners[fields[4]].add(section)
    return owners


def check(refs_text, allowed, owners=None):
    """Returns the list of violations (strings)."""
    bad = []
    for number, line in enumerate(refs_text.splitlines(), 1):
        if not line.strip():
            continue
        match = REF.match(line)
        if not match:
            bad.append(f"refs line {number}: unrecognised format: {line}")
            continue
        section, symbol, unit, target = match.groups()
        if section == ".data" and symbol == "units" and target == f"tr64_unit_{unit}":
            continue
        key = (section, symbol, unit, target)
        if key not in allowed:
            bad.append(f"resident reference into unit not in the allow-list: {line}")
            continue
        callers = allowed[key]
        if callers is not None:
            if owners is None:
                bad.append(f"callers={callers} cannot be verified without --elf: {line}")
            elif owners.get(symbol, set()) - {f".tlbcode.{callers}"}:
                outside = sorted(owners[symbol] - {f".tlbcode.{callers}"})
                bad.append(f"{symbol} is referenced from outside unit {callers} ({', '.join(outside)}): {line}")
            elif not owners.get(symbol):
                bad.append(f"no relocation against {symbol} found in the ELF to verify callers={callers}: {line}")
    return bad


def selftest():
    allow = parse_allow("""# comment
.text:fn -> video:tbl  callers=video  # only called inside the unit
.data:obj -> video:<section>
""")
    own = {"fn": {".tlbcode.video"}}
    leaked = {"fn": {".tlbcode.video", ".text"}}
    good = [".data:units@0x8006f174 -> init:tr64_unit_init@0xc1000000",
            ".data:obj@0x8007dba0 -> video:<section>@0xc1100000",
            ".text:fn@0xc00585dc -> video:tbl@0xc1114ea8"]
    if check("\n".join(good) + "\n", allow, own):
        raise AssertionError("allowed references rejected")
    planted = [
        ("unlisted .text reference", ".text:other@0xc0001000 -> video:tbl@0xc1114ea8", own),
        ("unlisted .data reference", ".data:other@0x80070000 -> save:<section>@0xc1980000", own),
        ("dispatch row naming the wrong unit", ".data:units@0x8006f174 -> init:tr64_unit_test@0xc1000000", own),
        ("dispatch-looking row from another symbol", ".data:table@0x8006f174 -> init:tr64_unit_init@0xc1000000", own),
        ("listed symbol, different target", ".text:fn@0xc00585dc -> video:other@0xc1114ea8", own),
        ("listed symbol referenced from resident code", ".text:fn@0xc00585dc -> video:tbl@0xc1114ea8", leaked),
        ("callers not verifiable", ".text:fn@0xc00585dc -> video:tbl@0xc1114ea8", None),
        ("callers absent from the ELF", ".text:fn@0xc00585dc -> video:tbl@0xc1114ea8", {}),
        ("malformed line", ".text:fn 0xc00585dc video:tbl", own),
    ]
    # Test-source entries (--extra-allow): accepted only when that list is passed.
    extra = parse_allow(".text:probe -> test:tr64_unit_test  # a probe linked only through the scene test sources\n")
    probe = ".text:probe@0xc000a63c -> test:tr64_unit_test@0xc1080020\n"
    if not check(probe, allow, own):
        raise AssertionError("test-source reference accepted without its list")
    if check(probe, {**allow, **extra}, own):
        raise AssertionError("test-source reference rejected with its list")
    if not check(".text:probe@0xc000a63c -> video:tbl@0xc1114ea8\n", {**allow, **extra}, own):
        raise AssertionError("test-source list accepted another target")
    print("refs-check selftest: test-source entries apply only with --extra-allow, only to their own target")
    for label, line, owners in planted:
        if not check(line + "\n", allow, owners):
            raise AssertionError(label + " escaped")
        print(f"refs-check selftest rejected {label}")
    try:
        parse_allow(".text:fn video:tbl\n")
    except ValueError:
        print("refs-check selftest rejected malformed allow-list line")
    else:
        raise AssertionError("malformed allow-list line accepted")
    owners = relocation_owners("""Relocation section '.rel.text' at offset 0x1 contains 2 entries:
 Offset     Info    Type            Sym.Value  Sym. Name
c0001000  00000a06 R_MIPS_26         c0058560   fn
Relocation section '.rel.tlbcode.video' at offset 0x2 contains 1 entries:
 Offset     Info    Type            Sym.Value  Sym. Name
c1114874  00000a06 R_MIPS_26         c0058560   fn
""")
    if owners["fn"] != {".text", ".tlbcode.video"}:
        raise AssertionError("relocation owner parse")
    print(f"refs-check selftest PASS ({len(planted) + 4} planted violations, allow-list and relocation parsers)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("refs", type=pathlib.Path, nargs="?")
    ap.add_argument("allow", type=pathlib.Path, nargs="?")
    ap.add_argument("--extra-allow", type=pathlib.Path, action="append", default=[])
    ap.add_argument("--elf", type=pathlib.Path)
    ap.add_argument("--readelf", default="mips64-elf-readelf")
    ap.add_argument("--selftest", action="store_true")
    ns = ap.parse_args()
    if ns.selftest:
        selftest()
        return 0
    if not ns.refs or not ns.allow:
        ap.error("REFS and ALLOWLIST are required")
    allowed = parse_allow(ns.allow.read_text())
    for extra in ns.extra_allow:
        allowed.update(parse_allow(extra.read_text()))
    refs = ns.refs.read_text()
    owners = None
    if ns.elf and any(v for v in allowed.values()):
        owners = relocation_owners(subprocess.run([ns.readelf, "-r", "-W", str(ns.elf)], capture_output=True, text=True, check=True).stdout)
    bad = check(refs, allowed, owners)
    lines = [l for l in refs.splitlines() if l.strip()]
    if bad:
        for line in bad:
            print("refs-check: " + line, file=sys.stderr)
        print(f"refs-check FAIL: {len(bad)} of {len(lines)} resident-to-unit references not accepted ({ns.refs})", file=sys.stderr)
        return 1
    dispatch = sum(1 for l in lines if re.match(r"^\.data:units@", l))
    print(f"refs-check PASS: {len(lines)} resident-to-unit references: {dispatch} dispatch rows, {len(lines) - dispatch} listed in {ns.allow.name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, AssertionError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"refs-check FAIL: {error}")
