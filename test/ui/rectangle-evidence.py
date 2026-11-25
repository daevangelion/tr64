#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check captured production HUD paths, world invariance, and FORCE_BLEND ramps."""
import argparse
import gzip
import json
from pathlib import Path
import re
import statistics
from PIL import Image


def picture(path):
    value = Image.open(path).convert("RGB")
    assert value.size == (320, 240)
    return value


def record_fields(text, prefix, ignored=()):
    return [{key: value for key, value in re.findall(r"(\w+)=([^ ]+)", line)
             if key not in ignored} for line in text.splitlines() if line.startswith(prefix)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = {"paths": {}}
    captures = {}
    hashes = world = None
    for mode in ("off", "fill", "one-cycle", "textured"):
        root = args.evidence / mode
        captures[mode] = picture(root / "picture.png")
        with gzip.open(root / "engine.log.gz", "rt") as stream:
            log = stream.read()
        current_hashes = record_fields(log, "game-tick:", ("tick_us",))
        current_world = record_fields(log, "game-render:", ("cpu_us", "done_us"))
        assert len(current_hashes) == len(current_world) == 21
        if hashes is None:
            hashes, world = current_hashes, current_world
        else:
            assert current_hashes == hashes and current_world == world, "HUD changed Game/3D frame output"
        full = [int(value) for value in re.findall(r"^game-render:.*?done_us=(\d+)", log, re.M)]
        submit = [int(value) for value in re.findall(r"^ui: submit_us=(\d+)", log, re.M)]
        assert len(full) == 21 and len(submit) == (0 if mode == "off" else 21)
        report["paths"][mode] = {"frames": 21, "frame_done_mean_us": statistics.mean(full)}
        if submit:
            report["paths"][mode]["enqueue_us"] = {"min": min(submit), "mean": statistics.mean(submit), "max": max(submit)}
    reference = captures["fill"].tobytes()
    assert all(captures[mode].tobytes() == reference for mode in ("one-cycle", "textured"))
    differences = []
    for y in range(240):
        for x in range(320):
            if captures["fill"].getpixel((x, y)) != captures["off"].getpixel((x, y)):
                differences.append((x, y))
                assert 212 <= x < 314 and (4 <= y < 11 or 14 <= y < 21), "HUD changed world pixels"
    assert len(differences) == 1428
    ramp = picture(args.evidence / "shade" / "picture.png")
    rejected_wrong_weight = 0
    for c in range(32):
        for y, weight in ((90, 32), (110, 14), (130, 10)):
            actual = tuple(value >> 3 for value in ramp.getpixel((10 * c + 5, y)))
            assert actual == (c * weight >> 5,) * 3, (c, y, actual)
            if weight == 14 and actual != (c * 13 >> 5,) * 3:
                rejected_wrong_weight += 1
    assert rejected_wrong_weight, "wrong-coefficient negative control did not fail"
    report.update(pass_=True, identical_hud_paths=True, world_changed_pixels=0,
                  hud_changed_pixels=len(differences), shade_channel_levels=32,
                  wrong_weight_rejected_levels=rejected_wrong_weight,
                  timing_scope="CPU enqueue measured; whole-frame paired deltas include layout/overlap variance, not isolated RDP cost")
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
