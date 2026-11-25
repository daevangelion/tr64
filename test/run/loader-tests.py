# SPDX-License-Identifier: MIT
"""Positive level dispatch + raw-ROM DMA control, using synthetic ROM bytes.
The real game-pack loader is Core's module; this gate checks the shared seam.
"""
import importlib.util
import os
import pathlib
import sys
sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = pathlib.Path(os.environ["BUILD_DIR"]) / "loader-probe"
TMP = pathlib.Path(os.environ["TMPDIR"])
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
route = TMP / "loader-probe.route"
lines = (ROOT / "test/scenes/boot.route").read_text().splitlines()
lines = [line for line in lines if not line.startswith(("observe boot-card", "observe code", "expect-shot"))]
lines = ["scene loader-probe" if line == "scene boot" else line for line in lines]
lines += ["load level rom-bind-test", "observe log '(?s)^scene loader probe PASS level=rom-bind-test bytes=32$.*^residency: level$'"]
route.write_text("\n".join(lines) + "\n")
BUILD.mkdir(parents=True, exist_ok=True)
scene_build = BUILD / "scene-builds/loader-probe"
env = dict(os.environ, BUILD_DIR=str(BUILD), TR64_LEVEL_LOADER="0", TR64_SCENE_TEST_SOURCES="engine/test/scene-loader-probe.c")
scene.command(["make", "--no-print-directory", f"BUILD_DIR={scene_build}", f"SCENE={route}", "host"], BUILD / "build.log", timeout=120, env=env)
# The player's files are never involved. Both backends read this ROM's own
# 32-byte title/header range, via their production engine/io implementation.
header = TMP / "loader-probe.cart"
header.write_bytes((scene_build / "tr64.z64").read_bytes()[0x20:0x40])
env["TR64_TEST_CART"] = str(header)
scene.command([ROOT / "test/run/scene.sh", "--host", route], BUILD / "host-gate.log", timeout=120, env=env)
if (BUILD / "scenes/host/loader-probe/boot.ppm").exists():
    raise RuntimeError("level scene invented a host boot-card screenshot")
if "--ares" in sys.argv[1:]:
    env["ARES_EXPANSION_PAK"] = "false"
    scene.command([ROOT / "test/run/scene.sh", "--ares", route], BUILD / "cart-gate.log", timeout=120, env=env)
    if list((BUILD / "scenes/ares/loader-probe").glob("ares-test-*.ppm")):
        raise RuntimeError("memory-only level probe invented a cart screenshot")
    print("scene loader probe PASS host+cart (required identity, no card borrow, raw ROM bounds/DMA, frozen heap)")
else:
    print("scene loader probe PASS host (required identity, no card borrow, modelled file reads)")
