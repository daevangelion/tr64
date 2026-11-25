#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check each split half against the same camera's standalone 320x120 host render."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = b"P6\n320 240\n255\n"
HALF_BYTES = 320 * 120 * 3


def halves(path):
    image = path.read_bytes()
    if not image.startswith(HEADER) or len(image) != len(HEADER) + HALF_BYTES * 2:
        raise ValueError(f"expected a canonical 320x240 PPM: {path}")
    return image[len(HEADER):len(HEADER) + HALF_BYTES], image[-HALF_BYTES:]


def compare(actual, references):
    for name, observed, expected in zip(("top", "bottom"), actual, references, strict=True):
        if observed != expected:
            pixels = sum(observed[i:i+3] != expected[i:i+3] for i in range(0, HALF_BYTES, 3))
            raise ValueError(f"{name} half: {pixels} different pixels")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    a = ROOT / "test/scenes/m1-level1-start.scene.json"
    b = ROOT / "test/scenes/m1-level1-bridge.scene.json"
    with tempfile.TemporaryDirectory(dir=os.environ["TMPDIR"], prefix="split-halves-") as tmp:
        route = Path(tmp) / "split.route"
        route.write_text("scene split-halves\nstart boot\nload level level1\nticks 2\nseed 1\n"
                         'observe log "^render: .* triangles=[1-9][0-9]* "\nobserve pools\n')
        produced = Path(os.environ["BUILD_DIR"]) / "scenes/host/split-halves/boot.ppm"
        images = {}
        for name, pose, extra in (("top-alone", a, ["--half-view"]), ("bottom-alone", b, ["--half-view"]),
                                  ("split", a, ["--split-camera", b])):
            subprocess.run(["rtk", sys.executable, ROOT / "test/run/scene.py", "--host", "--pose", pose,
                            *extra, route], check=True)
            picture = args.out / f"{name}.ppm"
            shutil.copyfile(produced, picture)
            images[name] = halves(picture)
    top, bottom = (images[name][0] for name in ("top-alone", "bottom-alone"))
    if top == bottom or not any(top) or not any(bottom):
        raise ValueError("standalone reference cameras must produce distinct non-empty views")
    if any(images["top-alone"][1]) or any(images["bottom-alone"][1]):
        raise ValueError("standalone view wrote outside its 320x120 viewport")
    compare(images["split"], (top, bottom))
    try:
        compare(images["split"], (bottom, top))
    except ValueError as e:
        print(f"split negative PASS (swapped cameras): {e}")
    else:
        raise AssertionError("swapped cameras unexpectedly matched")
    print(f"split PASS: both 38,400-pixel halves match their standalone render; {args.out}")


if __name__ == "__main__":
    main()
