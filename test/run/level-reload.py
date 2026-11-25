# SPDX-License-Identifier: MIT
"""Real Core-only LEVEL1/GYM/LEVEL1 release gate; no game/renderer restart claim.

BUILD_DIR=... TMPDIR=... TR64_LEVEL_PACK=... python3 test/run/level-reload.py [--ares]
The input pack must contain both PC level1 and gym; it is never rewritten.
"""
import argparse
import importlib.util
import math
import os
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
CORE = " ".join("engine/res/" + name + ".c" for name in
                ("load", "view", "pack", "level", "validate", "tex", "load-unit", "stream", "sound", "image"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ares", action="store_true")
    parser.add_argument("--audio-hooks", action="store_true",
                        help="exercise optional sound boot/loader-pump/release ordering without a mixer")
    args = parser.parse_args()
    seconds = float(os.environ.get("ARES_TIMEOUT", "90")) if args.ares else 0.0
    if args.ares and (not math.isfinite(seconds) or seconds <= 0):
        raise ValueError("ARES_TIMEOUT must be positive finite seconds")
    pack = Path(os.environ["TR64_LEVEL_PACK"]).resolve(strict=True)
    mode = "ares" if args.ares else "host"
    if args.audio_hooks:
        mode += "-audio"
    root = Path(os.environ["BUILD_DIR"]).resolve() / "level-reload" / mode
    build, out = root / "build", root / "run"
    for pattern in ("ares-test-*.ppm", "boot.ppm", "engine.log", "runner.log"):
        for stale in out.glob(pattern):
            stale.unlink()
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / "test/scenes/level-reload.route"
    meta, bytecode = scene.routec.parse(source)
    route = out / "scene.rbc"
    route.write_bytes(bytecode)
    # Core-only means real production loader/code/I/O/memory, not fake loading.
    # Game and renderer dispatch fragments must not leak into this isolated link.
    options = ["TR64_LEVEL_LOADER=1", "TR64_PLAY=0", "TR64_TEST=1",
               "TR64_ENGINE_SOURCES=" + CORE, "TR64_HOST_SOURCES=", "TR64_CART_SOURCES=",
               "TR64_CODE_UNIT_FRAGMENTS=engine/res/load.def",
               "TR64_SCENE_TEST_SOURCES=engine/test/level-reload-probe.c" +
               (" engine/test/audio-hooks-probe.c" if args.audio_hooks else ""),
               "TR64_LEVEL_PACK=" + str(pack)]
    scene.command(["make", "--no-print-directory", "-j1", f"BUILD_DIR={build}",
                   f"SCENE={source}", *options, "rom" if args.ares else "host"],
                  out / "build.log", timeout=120)
    executable = build / ("tr64.z64" if args.ares else "host/tr64-host")
    if args.ares:
        expected_build = scene.guest_identity.expected_identity(build)
        scene.command([ROOT / "test/run/ares.sh", out, executable, out / "engine.log", str(seconds)],
                      out / "runner.log", timeout=seconds + 10)
        shots = sorted(out.glob("ares-test-*.ppm"))
        if len(shots) != 1:
            raise AssertionError("reload cart did not finish with one fresh no-level card")
        shot = shots[0]
    else:
        shot = out / "boot.ppm"
        scene.command([executable, "--route", route, "--shot", shot], out / "engine.log", timeout=20)
    text = (out / "engine.log").read_text()
    if args.ares:
        scene.guest_identity.require_identity(text, expected_build)
    records = re.findall(r"^level-reload: loaded=(\w+) ordinal=(\d+) hash=([0-9a-f]{8})$", text, re.M)
    if [r[:2] for r in records] != [("level1", "0"), ("gym", "1"), ("level1", "2")] or records[0][2] != records[2][2] or records[0][2] == records[1][2]:
        raise AssertionError("reload did not compare all three actual published payloads")
    freed = re.findall(r"^level-reload: released=(\w+) level=0 state=0 code=0 io=0$", text, re.M)
    if freed != ["level1", "gym", "level1"]:
        raise AssertionError("reload retained an arena, game state, code mapping or I/O lease")
    if args.audio_hooks:
        stopped = re.findall(r"^audio hook stop active=0 state=(\d+) test_loaded=1$", text, re.M)
        reports = re.findall(r"^audio hook report boot=1 starts=0 ticks=0 pumps=(\d+) stops=3 active=0$", text, re.M)
        if stopped != ["37", "38", "39"] or not reports or int(reports[-1]) <= 2 or text.count("audio hook boot\n") != 1:
            raise AssertionError("sound stop did not precede reset, loader did not pump, or sound boot repeated")
        if args.ares and not re.search(r"audioheap: used=0 peak=32 calls=1 post_boot_allocations=0 frozen=1", text):
            raise AssertionError("native sound scope was not charged or froze incorrectly")
    ctx = dict(meta=meta, scene=source, mode=mode, cart=args.ares, build=build,
               executable=executable, artifacts=out, log=text, shot=shot, results={})
    for name, operands in meta["observables"]:
        scene.BUILTINS[name](ctx, operands)
    print(f"level reload PASS {mode}: LEVEL1={records[0][2]} GYM={records[1][2]} LEVEL1={records[2][2]}; three releases, fresh planners/frames/state; artifacts={out}")


if __name__ == "__main__":
    main()
