# SPDX-License-Identifier: MIT
"""Explicit executable selection, no silent fallback, same production launcher."""
import argparse
import importlib.util
import os
import shutil
from pathlib import Path
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--rom", required=True, type=Path)
p.add_argument("--identity-header", type=Path, help="independent cart_identity.h for a relocated ROM")
args = p.parse_args()
try:
    expected_build = scene.guest_identity.expected_identity(args.rom.parent, args.identity_header)
except ValueError as error:
    p.error(str(error))
base = Path(os.environ["BUILD_DIR"]).resolve() / "ares-binary-tests"
base.mkdir(parents=True, exist_ok=True)
with args.rom.open("rb") as f:
    header = f.read(64)
if len(header) != 64 or header[0x3f] >> 4 != 3:
    raise AssertionError("D6 SRAM256k/32KiB header not produced by Makefile")
for name, value in [("empty", ""), ("missing", str(base / "does-not-exist"))]:
    out = base / name
    if out.exists():
        shutil.rmtree(out)
    env = dict(os.environ, ARES_BINARY=value)
    scene.command([ROOT / "test/run/ares.sh", out, args.rom.resolve(), out / "engine.log", "30"],
                  base / (name + ".log"), timeout=5, expected=2, env=env)
    if "ares missing:" not in (base / (name + ".log")).read_text() or out.exists():
        raise AssertionError("invalid override silently fell back or acquired a slot")
out = base / "explicit"
if out.exists():
    shutil.rmtree(out)
env = dict(os.environ, ARES_BINARY="/tmp/ares/build/desktop-ui/ares",
           ARES_EXPANSION_PAK="false")
scene.command([ROOT / "test/run/ares.sh", out, args.rom.resolve(), out / "engine.log", "90"],
              base / "explicit.log", timeout=100, env=env)
text = (out / "engine.log").read_text()
scene.guest_identity.require_identity(text, expected_build)
if not scene.guest_identity.has_exit_record(text, 0) or len(list(out.glob("ares-test-*.ppm"))) != 1:
    raise AssertionError("explicit emulator path did not finish the actual guest boot")
print("ares binary PASS: explicit executable boots; empty/missing overrides fail before slot; SRAM32KiB header=3")
