#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A shipping ROM, the profiler overlay switched on and off with the pad chord (L + D-pad Down).

Through the shared harness (test/run/ares.sh, TR64_ARES_HOST=<remote-host> TR64_TIMEOUT_SCALE=8 to run it there): boot, title,
new game, LEVEL1; the chord on; two published windows (the `perf:` log lines, twice a second of the cart's clock);
a picture with the overlay up; the chord again; the overlay's lines stop and the picture is the plain game.
Writes summary.json and the two pictures (PNG, 320x240) into --out. ares models no RDP cost and no cache misses:
the numbers it shows are emulator numbers, valid as counts only (docs/perf-overlay.md).
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from gate_logs import expected_identity
from drive import Drive, GateFailure, skip_films, word
from gate_runs import FILMS, title_new_game, verdict

ROOT = Path(__file__).resolve().parents[2]


def last_picture(text, before, after=0):
    """The last picture ares announced between two log offsets that the run kept (the pruner deletes most)."""
    found = [Path(m[1]) for m in re.finditer(r"^\[ares\] screenshot (\S+\.ppm) ", text, re.M) if after <= m.start() < before]
    kept = [p for p in found if p.exists()]
    return kept[-1] if kept else None


def to_png(source, target):
    from PIL import Image
    Image.open(source).convert("RGB").save(target)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--identity-header", type=Path, help="independent cart_identity.h for a relocated ROM")
    ap.add_argument("--pack", type=Path, required=True, help="the campaign pack the ROM is meant to carry")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--ares", type=Path, default=Path(os.environ.get(
        "ARES_BINARY", "/tmp/tr64/builds/MenuUI/ares-sram2/ares")))
    ap.add_argument("--timeout", type=int, default=1500)
    ap.add_argument("--windows", type=int, default=3, help="published windows to wait for with the overlay up")
    args = ap.parse_args()
    try:
        expected = expected_identity(args.rom, args.pack, args.identity_header)
    except (OSError, ValueError) as error:
        ap.error(str(error))
    out = args.out.resolve()
    drive = Drive(ROOT, args.rom.resolve(), out / "run", timeout=args.timeout, ares=args.ares)
    drive.servicers.append(skip_films(FILMS))
    mark, failure = {}, None
    try:
        ready = title_new_game(drive)
        drive.wait_frames(ready, 40)
        mark["ready"], mark["before_on"] = ready, len(drive.text())
        drive.press(word("L", "DOWN"), "L+D-pad Down: overlay on", hold=3, gap=4)
        mark["on"] = len(drive.text())
        at = mark["on"]
        for n in range(args.windows):
            at = drive.wait(r"^perf: .* frames=\d+ glyphs=\d+$", after=at)
        mark["windows"] = at
        drive.wait_frames(at, 40)
        mark["shown"] = len(drive.text())
        drive.press(word("L", "DOWN"), "L+D-pad Down: overlay off", hold=3, gap=4)
        mark["off"] = len(drive.text())
        drive.wait_frames(mark["off"], 60)
        mark["after_off"] = len(drive.text())
    except GateFailure as error:
        failure = str(error)
    drive.stop()
    text = drive.text()
    lines = [(m.start(), m[0]) for m in re.finditer(r"^perf: .*$", text, re.M)]
    off_at = mark.get("off", len(text))
    on_lines = [l for o, l in lines if mark.get("on", 0) <= o < off_at]
    after_off = [l for o, l in lines if o >= mark.get("after_off", len(text)) - 1]
    shot_on = last_picture(text, mark.get("shown", 0) + 1, mark.get("windows", 0)) if "shown" in mark else None
    shot_before = last_picture(text, mark["on"], mark["before_on"]) if "on" in mark else None
    shot_off = last_picture(text, mark["after_off"] + 1, mark["off"]) if "after_off" in mark else None
    out.mkdir(parents=True, exist_ok=True)
    pictures = {}
    for name, shot in (("overlay-on", shot_on), ("overlay-off", shot_off), ("before-on", shot_before)):
        if shot and shot.exists():
            to_png(shot, out / f"{name}.png")
            pictures[name] = str(out / f"{name}.png")
    # What the overlay costs, from the cart's own per-frame lines: render CPU time and time to GPU completion
    # of the frames before the chord, with the overlay up (after its first window), and after the chord off.
    frames = [(m.start(), int(m[1]), int(m[2])) for m in
              re.finditer(r"^game-render: .*? cpu_us=(\d+) done_us=(\d+)", text, re.M)]
    def mean_of(lo, hi, column):
        values = [f[column] for f in frames if lo <= f[0] < hi]
        return (round(sum(values) / len(values), 1), len(values)) if values else None
    cost = {}
    if "windows" in mark and "before_on" in mark:
        for name, lo, hi in (("before_on", mark["ready"], mark["before_on"]),
                             ("overlay_up", mark["windows"], mark["off"]),
                             ("after_off", mark["off"] + 1, len(text))):
            cost[name] = {"cpu_us": mean_of(lo, hi, 1), "done_us": mean_of(lo, hi, 2)}
    # The overlay's lines come only while it is on: none before the chord, and none in the last second after it.
    before_lines = [l for o, l in lines if o < mark.get("on", 0)]
    checks = {
        "no_lines_before_on": not before_lines,
        "windows_while_on": len(on_lines) >= args.windows,
        "no_lines_after_off": not after_off,
        "picture_on": "overlay-on" in pictures,
        "picture_off": "overlay-off" in pictures,
        "no_fatal": "tr64 fatal:" not in text,
    }
    summary = verdict(text, expected, checks, failure, rom=str(args.rom), pictures=pictures,
                      frame_cost=cost, perf_lines=on_lines[:12])
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({k: summary[k] for k in ("pass", "failure", "identity_failure", "checks", "pictures", "frame_cost")}, indent=2))
    for line in on_lines[:4]:
        print(line)
    sys.exit(0 if summary["pass"] else 1)


if __name__ == "__main__":
    main()
