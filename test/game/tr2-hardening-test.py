#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed controls for the second-game hardening review.

Each case is compiled against the unfixed sources and the worktree.
A case that must refuse fails on the unfixed tree (the defect is
accepted, or AddressSanitizer reports the overflow) and passes once
the worktree refuses it by name.
"""
import subprocess
import sys
from pathlib import Path

WT = Path(__file__).resolve().parents[2]
GEN = Path("/tmp/tr64/tmp/GHarden6/generated")
UNFIXED = Path("/tmp/tr64/tmp/GHarden6/unfixed")

OUT = Path("/tmp/tr64/tmp/GHarden6")
TEST = WT / "test/game/tr2-hardening.c"

# name, sources relative to the tree, expect failure on the unfixed tree.
# rooms: the hazards contact pass removed the visited-room bitmap, so
# a712dc4 no longer demonstrates that overflow. Both trees must visit
# room 160 without a fault. The named capacity refusal is separate.
CASES = [
    ("rooms", ["game/collision.c"], False),
    ("capacity", ["game/collision.c"], False),
    ("links", ["game/animation.c"], True),
    ("state", ["game/animation.c"], True),
    ("sound", ["game/animation.c"], True),
    ("zip", ["game/animation.c"], True),
    ("tiger", ["game/tr2/enemy_tiger.c"], True),
    ("crow", ["game/tr2/enemy_crow.c"], True),
    ("spider", ["game/tr2/enemy_small_spider.c"], True),
    ("boulder", ["game/tr2/hazard_boulder.c"], False),
    ("harness", ["game/room.c"], True),
]


def compile_case(tree, name, sources):
    binary = OUT / f"harden-{name}-{tree.name}"
    isolated = OUT / f"src-{name}-{tree.name}"
    isolated.mkdir(parents=True, exist_ok=True)
    cmd = [
        "cc", "-m32", "-std=c99", "-Wall", "-Wextra", "-Wno-unused-function",
        "-g", "-fsanitize=address", "-fno-omit-frame-pointer",
        "-DTR64_HOST=1", "-DTR64_GAME=2", f"-DHARDEN_{name.upper()}=1",
        f"-I{WT}", f"-I{WT}/game", f"-I{WT}/game/tr2", f"-I{WT}/include", f"-I{GEN}",
        "-ffunction-sections", "-fdata-sections",
        "-o", str(binary), str(TEST),
    ]
    for src in sources:
        dest = isolated / Path(src).name
        dest.write_bytes((tree / src).read_bytes())
        cmd.append(str(dest))
    cmd += ["-Wl,--gc-sections", "-lm"]
    run = subprocess.run(cmd, capture_output=True, text=True)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        raise SystemExit(f"compile {name} against {tree} failed")
    return binary


def run_case(binary, name):
    run = subprocess.run([str(binary), name], capture_output=True, text=True)
    return run.returncode, (run.stdout + run.stderr).strip()


def main():
    failed = 0
    for name, sources, fail_unfixed in CASES:
        if name == "capacity":
            fixed = compile_case(WT, name, sources)
            code, text = run_case(fixed, name)
            ok = code == 0
            print(f"{'PASS' if ok else 'FAIL'} fixed {name}: {text}")
            failed += not ok
            continue
        unfixed = compile_case(UNFIXED, name, sources)
        fixed = compile_case(WT, name, sources)
        u_code, u_text = run_case(unfixed, name)
        f_code, f_text = run_case(fixed, name)
        u_ok = (u_code != 0) if fail_unfixed else (u_code == 0)
        f_ok = f_code == 0
        print(f"{'PASS' if u_ok else 'FAIL'} unfixed {name} exit {u_code}: {u_text.splitlines()[-1] if u_text else ''}")
        print(f"{'PASS' if f_ok else 'FAIL'} fixed   {name} exit {f_code}: {f_text.splitlines()[-1] if f_text else ''}")
        failed += not u_ok or not f_ok
    if failed:
        raise SystemExit(f"{failed} control(s) failed")
    print("all hardening controls passed")


if __name__ == "__main__":
    main()
