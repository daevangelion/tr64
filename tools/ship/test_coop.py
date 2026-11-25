# SPDX-License-Identifier: MIT
"""Malformed co-op cart evidence must fail the production predicates."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from coop import (BAR_HEIGHT, BAR_WIDTH, BAR_X, HALF_RGB, PICTURE_VIEWS, PPM_HEADER, GateFailure,
                  command_masks, read_picture, verify_picture, verify_pictures, verify_restored_poses, verify_wolf_intervals)


def reject(function, reason, *args):
    try:
        function(*args)
    except GateFailure as error:
        assert reason in str(error), error
        print(f"reject PASS: {error}")
    else:
        raise AssertionError(f"accepted malformed evidence: {reason}")


def replay_capacity(host):
    with tempfile.TemporaryDirectory(prefix="coop-replay-bound-") as directory:
        masks = Path(directory) / "masks"

        def run(schema, ticks, masks_text):
            masks.write_text(masks_text)
            return subprocess.run(["rtk", "proxy", host, "--game-level", "level1", "--game-trace-only",
                                   "--game-trace-schema", str(schema), "--game-input", masks,
                                   "--game-ticks", str(ticks)], capture_output=True, text=True)

        accepted = run(5, 601, "coop 1\njoin 1\n" + "0 0\n" * 601)
        assert accepted.returncode == 0 and "game-tick: tick=601 " in accepted.stdout, accepted.stderr
        legacy = run(4, 601, "0\n" * 601)
        assert legacy.returncode != 0 and "(600; a cutscene level, the level before one, LEVEL4 and LEVEL8B 4096)" in legacy.stderr
        too_long = run(5, 4097, "coop 1\njoin 1\n" + "0 0\n" * 4097)
        assert too_long.returncode != 0 and "(schema 5 limit 4096)" in too_long.stderr
    print("co-op host replay bound PASS: 601 accepted only in schema 5, 4097 rejected")


def replace_region(target, source, x, y, width, height, *, source_y=None):
    if source_y is None:
        source_y = y
    for offset in range(height):
        a = len(PPM_HEADER) + 3 * (320 * (y + offset) + x)
        b = len(PPM_HEADER) + 3 * (320 * (source_y + offset) + x)
        target[a:a + 3 * width] = source[b:b + 3 * width]


def picture_controls(host, evidence):
    """Re-use genuine VI captures, including the post-leave retired split buffer."""
    manifest = json.loads((evidence / "picture-control.json").read_text())
    log = (evidence / "cart/ares.log").read_text()
    vi = evidence / "cart/vi"
    with tempfile.TemporaryDirectory(prefix="coop-pictures-") as directory:
        out = Path(directory)
        shots = out / "pictures"
        shots.mkdir()
        for label in PICTURE_VIEWS:
            (shots / f"{label}.ppm").write_bytes(read_picture(vi / manifest["captures"][label]))
        refs = out / "picture-reference"
        print(json.dumps(verify_pictures(host, log, manifest["captures"], out), indent=2))
        reject(verify_picture, "does not match the 1-view host render", "left",
               vi / manifest["retired_split"], refs / "left.ppm")
        data = read_picture(shots / "apart.ppm")
        swapped = out / "swapped.ppm"
        swapped.write_bytes(PPM_HEADER + data[-HALF_RGB:] + data[len(PPM_HEADER):-HALF_RGB])
        reject(verify_picture, "does not match the 2-view host render", "apart",
               swapped, refs / "apart.ppm", refs / "apart-solo.ppm")
        for page, absent in (("ring", "apart"), ("both-dead", "left")):
            reject(verify_picture, "does not match the 1-view host render", page,
                   shots / f"{absent}.ppm", refs / f"{page}.ppm")
            # Remove only the full-screen label band from the real shot.
            # The rest of the image stays correct and should pass the broad
            # half-image comparison, but the focused glyph check must fail.
            muted = out / f"{page}-no-label.ppm"
            data = bytearray(read_picture(shots / f"{page}.ppm"))
            for y in range(216, 236):
                i = len(PPM_HEADER) + 3 * (320 * y + 80)
                data[i:i + 3 * 160] = bytes(3 * 160)
            muted.write_bytes(data)
            reject(verify_picture, "missing full-screen", page, muted, refs / f"{page}.ppm")
        stale_hud = out / "left-stale-hud.ppm"
        data = bytearray(read_picture(shots / "left.ppm"))
        replace_region(data, read_picture(vi / manifest["retired_split"]), BAR_X, 124, BAR_WIDTH, 17)
        stale_hud.write_bytes(data)
        reject(verify_picture, "HUD bar row 124, expected empty", "left",
               stale_hud, refs / "left.ppm", refs / "left-solo.ppm")

        for label, source, expectation in (
            ("joined", shots / "left.ppm", "present"),
            ("apart", vi / manifest["retired_split"], "empty"),
        ):
            stale = out / f"{label}-wrong-hud.ppm"
            data = bytearray(read_picture(shots / f"{label}.ppm"))
            replace_region(data, read_picture(source), BAR_X, 124, BAR_WIDTH, 17)
            stale.write_bytes(data)
            reject(verify_picture, f"HUD bar row 124, expected {expectation}", label,
                   stale, refs / f"{label}.ppm", refs / f"{label}-solo.ppm")

        moved = out / "bar-moved-to-p1.ppm"
        original = read_picture(shots / "one-dead.ppm")
        data = bytearray(original)
        replace_region(data, original, BAR_X, 4, BAR_WIDTH, BAR_HEIGHT, source_y=124)
        lower_world = read_picture(refs / "no-large-medipack-world.ppm")
        replace_region(data, lower_world, BAR_X, 124, BAR_WIDTH, BAR_HEIGHT)
        moved.write_bytes(data)
        reject(verify_picture, "HUD bar row 4, expected empty", "one-dead", moved, refs / "one-dead.ppm")

        for label, y, height in (("pickup", 141, 99), ("no-large-medipack", 149, 17)):
            missing = out / f"{label}-missing.ppm"
            world = refs / f"{label}-world.ppm"
            data = bytearray(read_picture(shots / f"{label}.ppm"))
            replace_region(data, read_picture(world), 0, y, 320, height)
            missing.write_bytes(data)
            reject(verify_picture, "missing player-2 HUD notice", label, missing, refs / f"{label}.ppm", None, world)
    print("co-op pictures PASS: eight layouts and focused HUD; real stale split, moved bar and missing notices rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, help="test-enabled host executable for replay controls")
    parser.add_argument("--picture-evidence", type=Path,
                        help="retained VI captures and picture-control.json (requires --host)")
    args = parser.parse_args()
    if args.picture_evidence and not args.host:
        parser.error("--picture-evidence requires --host")
    p0 = 'coop-start-pose: epoch=4 slot=0 {"pos":[12,34,56],"rot":[0,100,0],"room":6}\n'
    p1 = p0.replace('slot=0', 'slot=1')
    saved = dict(pos=(12, 34, 56), rot=(0, 100, 0), room=6)
    tick0 = "coop-tick: epoch=4 tick=0 input=0000,0000 present=3 alive=3 room=6,6 hash=12345678\n"
    tick1 = tick0.replace("tick=0", "tick=1")
    verify_restored_poses(tick0 + p1 + p0 + tick1, saved)
    reject(verify_restored_poses, "one restored pose for each slot", tick0 + p0 + p0 + tick1, saved)
    reject(verify_restored_poses, "off-spawn checkpoint", tick0 + p0 + p1.replace("[12,34,56]", "[75264,3072,3584]") + tick1, saved)
    reject(verify_restored_poses, "before the first simulation tick", tick0.replace("present=3 alive=3", "present=1 alive=1") + p0 + p1 + tick1, saved)
    reject(verify_restored_poses, "one restored pose for each slot", tick0 + p0 + tick1 + p1, saved)
    with tempfile.TemporaryDirectory(prefix="coop-command-replay-") as directory:
        masks = Path(directory) / "masks"
        tick2 = tick0.replace("tick=0", "tick=2")
        command_masks(tick0 + "play-coop: leave busy\n" + tick1 +
                      "play-coop: no-medipack player=2\n" + tick2, 4, 2, masks)
        assert masks.read_text().splitlines()[:4] == ["coop 1", "join 0", "leave-result 1 1", "warp 2 1"]
    hit1 = "coop-wolf: hit=1 health_before=1000 health_after=900\n"
    hit0 = hit1.replace('hit=1', 'hit=0')
    verify_wolf_intervals(hit1, hit0)
    reject(verify_wolf_intervals, "did not bite slot 1", "", hit0)
    reject(verify_wolf_intervals, "did not bite slot 0", hit1, "")
    reject(verify_wolf_intervals, "damaged the distant slot 0", hit0 + hit1, hit0)
    reject(verify_wolf_intervals, "damaged the distant slot 1", hit1, hit0 + hit1)
    print("co-op verdict PASS: distinct saved slots, each bite required, distant-partner damage rejected in both intervals")
    if args.host:
        host = args.host.resolve(strict=True)
        replay_capacity(host)
        if args.picture_evidence:
            picture_controls(host, args.picture_evidence)


if __name__ == "__main__":
    main()
