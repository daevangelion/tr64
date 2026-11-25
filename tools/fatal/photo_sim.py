#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Simulated photographs of the 320x240 fatal screen, to choose a QR version.

    photo_sim.py [--trials N] [--seed S] [--save DIR] SPEC...

SPEC is VERSION[LEVEL]:MODULE_PX, e.g. 6M:4 10M:3 12L:3. For each SPEC a
random alphanumeric payload of the version's capacity is drawn as a QR symbol
(segno) in the lower right corner of a dark 320x240 screen, quiet zone four
modules; then each trial photographs it: upscaled by 3 to 4 (the TV), scan-line
shading, perspective jitter (up to 5 % of a side) and rotation (up to 4
degrees), a downscale of the whole frame to 480 to 900 px, Gaussian blur,
noise, gain and gamma, and JPEG. Prints
the decode rate of zxing-cpp and of OpenCV's Aruco-based detector. Needs numpy,
segno, zxing-cpp, opencv-python-headless and pillow (private venv,
docs/architecture.md section 3.1). Not a gate: a decision aid with a seed.
"""
import argparse
import random
import sys
from pathlib import Path

import cv2
import numpy
import segno
import zxingcpp

sys.dont_write_bytecode = True
ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"
# Alphanumeric capacities (ISO/IEC 18004 Table 7), by version for L and M.
CAPACITY = {
    ("L", 6): 195, ("M", 6): 154, ("L", 8): 279, ("M", 8): 221, ("L", 9): 335, ("M", 9): 262,
    ("L", 10): 395, ("M", 10): 311, ("L", 11): 468, ("M", 11): 366, ("L", 12): 535, ("M", 12): 419,
    ("L", 13): 619, ("M", 13): 483, ("L", 14): 667, ("M", 14): 528,
}


def screen(spec, rng):
    version, level, px = spec
    text = "".join(rng.choice(ALPHABET) for _ in range(CAPACITY[(level, version)]))
    code = segno.make(text, version=version, error=level, mode="alphanumeric", boost_error=False, micro=False)
    matrix = numpy.array([[1 - bit for bit in row] for row in code.matrix], dtype=numpy.uint8) * 255
    edge = matrix.shape[0] + 8
    padded = numpy.full((edge, edge), 255, dtype=numpy.uint8)
    padded[4:-4, 4:-4] = matrix
    big = numpy.kron(padded, numpy.ones((px, px), dtype=numpy.uint8))
    image = numpy.full((240, 320), 24, dtype=numpy.uint8)
    h, w = big.shape
    if h > 240 or w > 320:
        raise SystemExit(f"{version}{level} at {px}px is {w}x{h}: does not fit 320x240")
    image[240 - h:, 320 - w:] = big
    return text, image, (320 - w, 240 - h, w, h)


def photograph(image, rng, nrng):
    scale = rng.choice((3, 4))
    tv = cv2.resize(image, None, fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST).astype(numpy.float32)
    # CRT-like scan-line shading: every scale-th row a little darker.
    tv[::scale] *= 0.9
    h, w = tv.shape
    jitter = lambda: nrng.uniform(-0.05, 0.05, 2) * numpy.array([w, h])
    src = numpy.float32([[0, 0], [w, 0], [w, h], [0, h]])
    dst = src + numpy.float32([jitter(), jitter(), jitter(), jitter()])
    angle = numpy.radians(rng.uniform(-4, 4))
    centre = numpy.float32([w / 2, h / 2])
    rot = numpy.float32([[numpy.cos(angle), -numpy.sin(angle)], [numpy.sin(angle), numpy.cos(angle)]])
    dst = ((dst - centre) @ rot.T + centre).astype(numpy.float32)
    pad = int(0.12 * max(w, h))
    dst += pad
    warped = cv2.warpPerspective(tv, cv2.getPerspectiveTransform(src, dst), (w + 2 * pad, h + 2 * pad),
                                 flags=cv2.INTER_LINEAR, borderValue=40)
    # The photo: the whole warped frame reaches 480 to 900 px wide (a phone held
    # at arm's length), then optics blur and sensor noise act at that resolution.
    target = rng.uniform(480, 900)
    small = cv2.resize(warped, None, fx=target / warped.shape[1], fy=target / warped.shape[1], interpolation=cv2.INTER_AREA)
    blurred = cv2.GaussianBlur(small, (0, 0), rng.uniform(0.8, 2.0))
    gain, gamma = rng.uniform(0.6, 1.1), rng.uniform(0.8, 1.5)
    shot = 255 * (numpy.clip(blurred / 255, 0, 1) ** gamma) * gain
    shot += nrng.normal(0, rng.uniform(4, 14), shot.shape)
    shot = numpy.clip(shot, 0, 255).astype(numpy.uint8)
    ok, jpeg = cv2.imencode(".jpg", shot, [cv2.IMWRITE_JPEG_QUALITY, rng.choice((40, 60, 80))])
    return cv2.imdecode(jpeg, cv2.IMREAD_GRAYSCALE)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("specs", nargs="+")
    parser.add_argument("--trials", type=int, default=60)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--save", type=Path)
    args = parser.parse_args()
    for raw in args.specs:
        vl, px = raw.split(":")
        spec = (int(vl[:-1]), vl[-1], int(px))
        rng, nrng = random.Random(args.seed), numpy.random.default_rng(args.seed)
        text, image, box = screen(spec, rng)
        zx = cvx = 0
        detector = cv2.QRCodeDetectorAruco()
        for trial in range(args.trials):
            shot = photograph(image, rng, nrng)
            found = zxingcpp.read_barcodes(shot, formats=zxingcpp.BarcodeFormat.QRCode)
            zx += any(r.text == text for r in found)
            got, _, _ = detector.detectAndDecode(shot)
            cvx += got == text
            if args.save and trial < 3:
                args.save.mkdir(parents=True, exist_ok=True)
                cv2.imwrite(str(args.save / f"{raw.replace(':', 'px')}-{trial}.png"), shot)
        print(f"{raw:8} symbol {box[2]}x{box[3]} px, {len(text)} chars: zxing-cpp {zx}/{args.trials}, OpenCV {cvx}/{args.trials}")


if __name__ == "__main__":
    main()
