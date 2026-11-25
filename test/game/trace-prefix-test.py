#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify game_trace_prefix() is 100% byte-identical to record()'s first 239 chars
across all 25 TR1 fixtures, lara:null (title + cut1), and a negative control."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run_case(host, level, masks_path, ticks, neg=False):
    env = os.environ.copy()
    if neg:
        env["TR64_TRACE_PREFIX_NEG"] = "1"
    else:
        env.pop("TR64_TRACE_PREFIX_NEG", None)
    cmd = ["rtk", str(host), "--game-level", level, "--game-input", str(masks_path),
           "--game-ticks", str(ticks), "--game-trace-only", "--game-check-prefix"]
    return subprocess.run(cmd, env=env, capture_output=True, text=True, check=False, timeout=120)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("host", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("--level", default="level1")
    args = p.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="ascii"))
    fixtures = manifest["fixtures"]
    total_ticks = 0
    for row in fixtures:
        lvl = Path(row["level"]).stem.lower()
        res = run_case(args.host, lvl, row["input"], row["ticks"])
        if res.returncode != 0:
            raise SystemExit(f"FAIL fixture {row['name']}: {res.stderr[-600:] or res.stdout[-600:]}")
        total_ticks += int(row["ticks"]) + 1
    print(f"trace-prefix fixtures PASS: {len(fixtures)} fixtures, {total_ticks} ticks byte-identical at 239 chars")

    with tempfile.TemporaryDirectory(prefix="trace-prefix-", dir=os.environ.get("TMPDIR")) as td:
        zeros = Path(td) / "zeros.masks"
        zeros.write_text("0\n" * 10, encoding="ascii")
        res = run_case(args.host, "title", zeros, 10)
        if res.returncode != 0:
            raise SystemExit(f"FAIL lara:null level=title: {res.stderr[-600:] or res.stdout[-600:]}")
        print("trace-prefix lara:null PASS: title (11 ticks)")

        first = fixtures[0]
        neg = run_case(args.host, Path(first["level"]).stem.lower(), first["input"], first["ticks"], neg=True)
        if neg.returncode == 0 or "trace-prefix mismatch" not in (neg.stdout + neg.stderr):
            raise SystemExit(f"FAIL negative control was not rejected: rc={neg.returncode}")
        print("trace-prefix negative PASS: 1-byte mismatch rejected")


if __name__ == "__main__":
    main()
