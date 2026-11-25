# SPDX-License-Identifier: MIT
"""Host proof of the fatal-screen QR encoder (engine/core/qr.c) and payload.

Every symbol is painted by the real C encoder at the cart's 3 pixel module
size, then checked three ways, none of which shares code with it:
  * bit-exact module matrix against the `segno` reference encoder (version 13,
    level M, mask 4, alphanumeric mode),
  * decoded text equal to the input by zxing-cpp and by OpenCV's
    QRCodeDetectorAruco (the classic QRCodeDetector is not a gate: it misses
    clean symbols that the others read),
  * for payload records, tools/fatal/decode.py recovers every field.
Needs numpy, segno, zxing-cpp and opencv-python-headless: run it with a
virtualenv interpreter (`make qr-tests PYTHON=<venv>/bin/python`); see the
Diagnostics section of docs/architecture.md. Missing modules are a failure,
not a skip.

segno quirk (extra pad byte when the terminator ends on a byte boundary): see
test/run/qr_reference.py; the reference is run both ways.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import random
import subprocess
import sys
import time
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
try:
    import cv2
    import numpy
    import segno  # noqa: F401 (qr_reference needs it; fail early with the install hint)
    import zxingcpp
except ImportError as error:
    sys.exit(f"qr-tests: {error}. Create a venv and `pip install numpy opencv-python-headless segno zxing-cpp`, "
             "then run with that interpreter (make qr-tests PYTHON=<venv>/bin/python).")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


scene = load("scene", ROOT / "test/run/scene.py")
decode = load("fatal_decode", ROOT / "tools/fatal/decode.py")
reference = load("qr_reference", ROOT / "test/run/qr_reference.py")
ALNUM = decode.ALPHABET
SCALE, EDGE, MODULES, QUIET = 3, 77, 69, 4
SIDE = SCALE * EDGE
MAX_BYTES = 316
HEADER = 273
CHARS = 483
MESSAGE_FIRST = 32
ROOM = MAX_BYTES - HEADER
GPRS = [i for i in range(1, 32) if i not in (26, 27)]


def draw(exe, texts):
    """[(painted, uint8 matrix of 0/1 dark modules, uint8 image)] for each text."""
    data = subprocess.run([exe, "draw"], input="".join(t + "\n" for t in texts).encode(),
                          capture_output=True, check=True).stdout
    out, at = [], 0
    for _ in texts:
        painted = data[at]
        at += 1
        if not painted:
            out.append((0, None, None))
            continue
        image = numpy.frombuffer(data[at:at + SIDE * SIDE], dtype=numpy.uint8).reshape(SIDE, SIDE)
        at += SIDE * SIDE
        centres = [SCALE * (QUIET + i) + SCALE // 2 for i in range(MODULES)]
        matrix = (image[numpy.ix_(centres, centres)] == 0).astype(numpy.uint8)
        # Each module must be a uniform square, the quiet zone entirely light.
        blocks = image.reshape(EDGE, SCALE, EDGE, SCALE)
        if (blocks.min(axis=(1, 3)) != blocks.max(axis=(1, 3))).any():
            raise AssertionError("module is not a uniform block")
        quiet = numpy.ones((EDGE, EDGE), bool)
        quiet[QUIET:QUIET + MODULES, QUIET:QUIET + MODULES] = False
        if (blocks[:, 0, :, 0][quiet] != 255).any():
            raise AssertionError("quiet zone is not light")
        out.append((1, matrix, image))
    if at != len(data):
        raise AssertionError("driver output length")
    return out


class Stats:
    def __init__(self):
        self.symbols = self.exact = self.shipped_equal = self.zxing = self.opencv = self.aligned = 0


def check_symbols(exe, texts, stats, detector):
    for start in range(0, len(texts), 400):
        batch = texts[start:start + 400]
        for text, (painted, matrix, image) in zip(batch, draw(exe, batch)):
            if not painted:
                raise AssertionError(f"encoder rejected valid text {text!r}")
            stats.symbols += 1
            if not (matrix == reference.matrix(text, True)).all():
                raise AssertionError(f"module matrix differs from segno (spec padding) for {text!r}")
            stats.exact += 1
            if reference.aligned(len(text)):
                stats.aligned += 1
            elif not (matrix == reference.matrix(text, False)).all():
                raise AssertionError(f"module matrix differs from shipped segno for {text!r}")
            else:
                stats.shipped_equal += 1
            found = zxingcpp.read_barcodes(image, formats=zxingcpp.BarcodeFormat.QRCode)
            if len(found) != 1 or found[0].text != text:
                raise AssertionError(f"zxing-cpp decoded {[f.text for f in found]} for {text!r}")
            stats.zxing += 1
            got, _, _ = detector.detectAndDecode(image)
            if got != text:
                raise AssertionError(f"OpenCV decoded {got!r} for {text!r}")
            stats.opencv += 1


def random_text(rng, length):
    return "".join(rng.choice(ALNUM) for _ in range(length))


def good_frame(rng):
    return rng.choice([0x80000000 + rng.randrange(0, 0x400000, 4),
                       0xC0000000 + rng.randrange(0, 0x50000, 4),
                       0xC1000000 + rng.randrange(0, 0x1000000, 4)])


def random_entry(rng):
    index = rng.choice([0xFF, rng.randrange(32)])
    hi = rng.randrange(4) << 62 | rng.randrange(256) << 32 | rng.getrandbits(32)
    return {"index": index, "hi": hi, "lo0": rng.getrandbits(32), "lo1": rng.getrandbits(32),
            "mask": rng.choice([0, 0x1FE000, rng.randrange(0x1000) << 13])}


def random_record(rng):
    nframes = rng.randint(0, 7)
    frames = [good_frame(rng) for _ in range(nframes)]
    cut = None
    if frames and rng.random() < 0.15:  # an unrepresentable address ends the list
        cut = rng.randrange(len(frames))
        frames[cut] = rng.choice([0x00000001, 0x80000002, 0xC2000000, 0xA0000000, 0xFFFFFFFC])
    message = bytes(rng.choice(range(1, 256)) if rng.random() < 0.1 else rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij0123456789 :/._-\n")
                    for _ in range(rng.choice([0, 1, 2, 5, 20, 31, 32, 33, 39, 43, 44, 60, 100])))
    return {"kind": rng.choice([1, 2, 3]), "code": rng.randrange(32), "nframes": nframes,
            "flags": rng.getrandbits(8), "vec": rng.randrange(4), "count": rng.randrange(256),
            "tlb_valid": rng.randrange(33), "unit": rng.choice([0xFF, rng.randrange(32)]),
            "build": "".join(rng.choice("0123456789ABCDEF") for _ in range(12)),
            "pc": rng.choice([0, 1, 0xFFFFFFFF, good_frame(rng), rng.getrandbits(32)]),
            "ra": rng.choice([0, good_frame(rng), rng.getrandbits(32)]),
            "badvaddr": rng.choice([rng.getrandbits(64), 0xFFFFFFFFFFFFC0C0, rng.getrandbits(32)]),
            "sr": rng.getrandbits(32), "cause": rng.getrandbits(32), "epc": rng.getrandbits(32),
            "entryhi": rng.getrandbits(64), "context": rng.getrandbits(64),
            "level": rng.randint(-1, 20), "tick": rng.choice([0, rng.getrandbits(32), rng.randrange(100000)]),
            "units": rng.choice([0, 0xFFFFFFFF, rng.getrandbits(21), rng.getrandbits(32)]),
            "frame": rng.choice([0, rng.randrange(0, 0x400000, 4096)]), "gpr_wide": rng.getrandbits(32),
            "gpr": [rng.getrandbits(32) for _ in range(32)],
            "epc_words": [rng.getrandbits(32) for _ in range(4)], "entry_words": [rng.getrandbits(32) for _ in range(4)],
            "mapped_words": [rng.getrandbits(32) for _ in range(4)],
            "pc_entry": random_entry(rng), "unit_entry": random_entry(rng),
            "frames": frames, "message": message, "cut": cut}


def entry_tokens(e):
    return f"{e['index']:x} {e['hi'] >> 32:x} {e['hi'] & 0xFFFFFFFF:x} {e['lo0']:x} {e['lo1']:x} {e['mask']:x}"


def record_line(r):
    words = lambda key: " ".join(f"{w:x}" for w in r[key])
    return (f"{r['kind']:x} {r['code']:x} {r['nframes']:x} {r['flags']:x} {r['vec']:x} {r['count']:x} {r['tlb_valid']:x} "
            f"{r['unit']:x} {r['build']} {r['pc']:x} {r['ra']:x} {r['badvaddr'] >> 32:x} {r['badvaddr'] & 0xFFFFFFFF:x} "
            f"{r['sr']:x} {r['cause']:x} {r['epc']:x} {r['entryhi'] >> 32:x} {r['entryhi'] & 0xFFFFFFFF:x} "
            f"{r['context'] >> 32:x} {r['context'] & 0xFFFFFFFF:x} {r['level']} {r['tick']:x} {r['units']:x} {r['frame']:x} "
            f"{r['gpr_wide']:x} {words('gpr')} {words('epc_words')} {words('entry_words')} {words('mapped_words')} "
            f"{entry_tokens(r['pc_entry'])} {entry_tokens(r['unit_entry'])} "
            + "".join(f"{f:x} " for f in r["frames"]) + (r["message"].hex() or "-"))


def kept(r):
    """Frames and message bytes the packer keeps: the message first up to 32 bytes, call sites next, the message last."""
    length = min(len(r["message"]), ROOM)
    room = ROOM - min(length, MESSAGE_FIRST)
    frames = []
    for index, frame in enumerate(r["frames"][:r["nframes"]]):
        if index == r["cut"] or 3 * (len(frames) + 1) > room:
            break
        frames.append(frame)
    return frames, r["message"][:min(length, ROOM - 3 * len(frames))].decode("latin-1")


def check_records(exe, records):
    lines = "".join(record_line(r) + "\n" for r in records)
    texts = subprocess.run([exe, "pack"], input=lines.encode(), capture_output=True, check=True).stdout.decode().splitlines()
    if len(texts) != len(records):
        raise AssertionError("pack output count")
    for r, text in zip(records, texts):
        if len(text) > CHARS or any(c not in ALNUM for c in text) or not text.startswith("TR64.2.") or not text.endswith("."):
            raise AssertionError(f"bad packed text {text!r}")
        f = decode.parse(text)
        frames, message = kept(r)
        want = {"kind_code": r["kind"], "exccode": r["code"], "build": r["build"], "pc": r["pc"], "ra": r["ra"],
                "badvaddr": r["badvaddr"], "sr": r["sr"], "cause": r["cause"], "epc": r["epc"], "entryhi": r["entryhi"],
                "context": r["context"], "level": None if r["level"] < 0 else r["level"], "tick": r["tick"],
                "units_mask": r["units"], "flags": r["flags"], "vec": r["vec"], "exc_count": r["count"],
                "tlb_valid": r["tlb_valid"], "unit_slot": r["unit"], "gpr_wide": r["gpr_wide"], "frame": r["frame"],
                "gpr": {i: r["gpr"][i] for i in GPRS}, "epc_words": r["epc_words"], "entry_words": r["entry_words"],
                "mapped_words": r["mapped_words"], "frames": frames, "message": message,
                "pc_entry": {**r["pc_entry"], "index": None if r["pc_entry"]["index"] == 0xFF else r["pc_entry"]["index"]},
                "unit_entry": {**r["unit_entry"], "index": None if r["unit_entry"]["index"] == 0xFF else r["unit_entry"]["index"]}}
        for key in ("pc_entry", "unit_entry"):  # the packer keeps hi bits 63:62, 39:32 and 31:0, and mask / 0x2000 in 16 bits
            hi = want[key]["hi"]
            want[key]["hi"] = (hi >> 62) << 62 | ((hi >> 32) & 0xFF) << 32 | (hi & 0xFFFFFFFF)
        for key, value in want.items():
            if f[key] != value:
                raise AssertionError(f"{key}: packed {f[key]!r} expected {value!r} (text {text})")
    return texts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed", type=int, default=64)
    parser.add_argument("--per-length", type=int, default=4, help="random texts for every length 1..483")
    parser.add_argument("--records", type=int, default=3000)
    args = parser.parse_args()
    build = Path(os.environ["BUILD_DIR"]).resolve() / "qr-tests"
    build.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("HOST_CC", "cc").split()
    scene.command([*cc, "-m32", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I", ROOT,
                   ROOT / "engine/core/qr.c", ROOT / "engine/test/qr-test.c", "-o", build / "qr-test"],
                  build / "build.log", timeout=60)
    exe = str(build / "qr-test")
    rng = random.Random(args.seed)
    started = time.time()
    detector = cv2.QRCodeDetectorAruco()
    stats = Stats()
    # Edge payloads: every length (all terminator/padding alignments), single
    # characters, runs of one character, the full set, and the 483 limit.
    texts = [random_text(rng, n) for n in range(1, CHARS + 1) for _ in range(args.per_length)]
    texts += [c * n for c in ALNUM for n in (1, 2, 3, CHARS - 1, CHARS)]
    texts += [ALNUM, ALNUM * 3 + ALNUM[:19], "TR64.2.", "A" * 241 + "B" * 242]
    check_symbols(exe, texts, stats, detector)
    records = [random_record(rng) for _ in range(args.records)]
    record_texts = check_records(exe, records)
    check_symbols(exe, record_texts, stats, detector)
    # Negative controls: the comparison must be able to fail (another mask, a
    # changed last character).
    sample = texts[len(texts) // 2]
    ((_, matrix, _),) = draw(exe, [sample])
    if (matrix == reference.matrix(sample, True, mask=5)).all():
        raise AssertionError("negative control: a different mask compared equal")
    if (matrix == reference.matrix(sample[:-1] + ("A" if sample[-1] != "A" else "B"), True)).all():
        raise AssertionError("negative control: a different text compared equal")
    # Rejections paint nothing: too long, and characters outside the set.
    for bad in ["A" * (CHARS + 1), "lower", "A\tB", "A~", "é"]:
        if draw(exe, [bad])[0][0]:
            raise AssertionError(f"encoder accepted {bad!r}")
    print(f"qr PASS: {stats.symbols} symbols ({len(texts)} text, {len(record_texts)} payload records) bit-exact against segno "
          f"({stats.shipped_equal} equal to segno as shipped, {stats.aligned} byte-aligned-terminator cases equal with its "
          f"padding corrected), decoded equal by zxing-cpp {stats.zxing} and OpenCV {stats.opencv}; 5 rejections, 2 negative controls; "
          f"packed lengths {min(map(len, record_texts))}..{max(map(len, record_texts))} of {CHARS}; "
          f"{time.time() - started:.0f} s")


if __name__ == "__main__":
    main()
