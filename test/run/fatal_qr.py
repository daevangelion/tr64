# SPDX-License-Identifier: MIT
"""Cart proof of the fatal-screen QR code, called per case by fatal-tests.py.

From the ares screenshot of one fatal control it checks, with decoders that
share no code with the cart (zxing-cpp, OpenCV's QRCodeDetectorAruco):
  * exactly one symbol decodes, to the text the cart also logged ("fatal-qr:");
  * the symbol area of the screenshot equals segno's rendering of that text,
    pixel for pixel, and the gap beside it is background: nothing overlaps it;
  * every fact in the payload equals what the cart logged for the same fault
    (fatal-context, registers, backtrace, message, build id);
  * tools/fatal/decode.py, run on the screenshot and the build's ELF, names the
    faulting function for PC/RA and the unit set is right.
Needs numpy, segno, zxing-cpp, OpenCV: use a virtualenv interpreter.
"""
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
try:
    import cv2
    import numpy
    import segno  # noqa: F401 (qr_reference needs it; fail early with the install hint)
    import zxingcpp
    from PIL import Image
except ImportError as error:
    raise SystemExit(f"fatal QR checks need decoders ({error}): run with a venv interpreter holding numpy, "
                     "segno, zxing-cpp, opencv-python-headless and Pillow, or pass --no-qr")
spec = importlib.util.spec_from_file_location("fatal_decode", ROOT / "tools/fatal/decode.py")
decode = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decode)
reference_spec = importlib.util.spec_from_file_location("qr_reference", ROOT / "test/run/qr_reference.py")
reference = importlib.util.module_from_spec(reference_spec)
reference_spec.loader.exec_module(reference)
QR_X, QR_Y, SIDE, SCALE = 89, 9, 231, 3
EDGE, QUIET = 77, 4
GAP = 4
# case -> (kind, ExcCode or None, {decode.py output line prefix: regex the line must match}, message regex)
CASES = {
    "exception": ("exception", 4, {"PC": r"tr64_fatal_test_exception", "RA": r"exception_caller", "#1": r"exception_caller",
                                    "#2": r"tr64_scene_load"}, r"Misaligned read"),
    "irq": ("exception", 4, {"PC": r"tr64_fatal_test_exception"}, r"Misaligned read"),
    "nested": ("exception", 4, {"PC": r"tr64_unit_test|\[code unit test\]"}, r"Misaligned read"),
    "assert": ("assert", None, {"RA": r"tr64_scene_load"}, r"^condition\nfatal-probe assert"),
    "c-assert": ("assert", None, {}, r"^condition$"),
    "sys-error": ("error", None, {"RA": r"error_caller"}, r"fatal-probe sys_error marker=37"),
    "rsp": ("error", None, {}, r"^RSP FATAL "),
    "audio": ("error", None, {"RA": r"playing_audio"}, r"fatal-probe audio DMA"),
    "unloaded": ("exception", None, {}, r"code unit test not loaded"),
    "early": ("exception", 4, {"PC": r"tr64_fatal_test_exception"}, r"Misaligned read"),
    "float-assert": ("assert", None, {}, r"^condition\nfatal-probe IEEE"),
}


def glyphs():
    """The cart's own 3x5 font, read from plat/n64/fatal.c."""
    source = (ROOT / "plat/n64/fatal.c").read_text()
    names = re.search(r'glyph_names\[\]="([^"]+)"', source)[1]
    bits = [int(v) for v in re.search(r"glyph_bits\[\]=\{(.*?)\}", source, re.S)[1].split(",") if v.strip()]
    return dict(zip(names, bits), **{" ": 0})


def text_mask(table, string):
    mask = numpy.zeros((5, 4 * len(string)), dtype=bool)
    for k, char in enumerate(string):
        for row in range(5):
            for col in range(3):
                mask[row, 4 * k + col] = bool(table[char] & (1 << (14 - row * 3 - col)))
    return mask


def text_row(pixels, background, table, string):
    """y of the row where string is drawn at x=4 (glyph pixels set, all others background), or None."""
    mask = text_mask(table, string)
    for y in range(0, 236):
        window = (pixels[y:y + 5, 4:4 + mask.shape[1]] != background).any(axis=2)
        if (window == mask).all():
            return y
    return None


def fail(case, text):
    raise AssertionError(f"fatal QR {case}: {text}")


def log_facts(log):
    context = re.search(r"^fatal-context: pc=([0-9a-f]{8}) origin=([0-9a-f]{8}) sr=([0-9a-f]{8}) cause=([0-9a-f]{8}) "
                        r"badvaddr=([0-9a-f]{8})", log, re.M)
    if not context:
        return None
    message = re.search(r"^tr64 fatal: (.*?)^fatal-context:", log, re.M | re.S)
    registers = {int(n): int(v, 16) for n, v in re.findall(r"R(\d+) ([0-9A-F]{16})", "\n".join(re.findall(r"^fatal-register: .*$", log, re.M)))}
    cp0 = re.search(r"^fatal-cp0: epc=([0-9a-f]{8}) badvaddr=([0-9a-f]{16}) entryhi=([0-9a-f]{16}) context=([0-9a-f]{16}) vec=(\d+) "
                    r"count=(\d+) tlb_valid=(\d+) unit=(\d+) flags=([0-9a-f]+) frame=([0-9a-f]{8})$", log, re.M)
    words = re.search(r"^fatal-words: epc=((?:[0-9a-f]{8} ?){4}) entry=((?:[0-9a-f]{8} ?){4}) mapped=((?:[0-9a-f]{8} ?){4})$", log, re.M)
    tlb = re.search(r"^fatal-tlb: pc index=(\d+) hi=([0-9a-f]{16}) lo0=([0-9a-f]{8}) lo1=([0-9a-f]{8}) mask=([0-9a-f]{8}) "
                    r"unit index=(\d+) hi=([0-9a-f]{16}) lo0=([0-9a-f]{8}) lo1=([0-9a-f]{8}) mask=([0-9a-f]{8})$", log, re.M)
    facts = {
        "pc": int(context[1], 16), "origin": int(context[2], 16), "sr": int(context[3], 16),
        "cause": int(context[4], 16), "badvaddr": int(context[5], 16),
        "message": message[1].rstrip("\n") if message else None,
        "r31": registers.get(31, 0) & 0xFFFFFFFF, "regs": registers,
        "trace": [int(pc, 16) for _, pc in re.findall(r"^fatal-backtrace: frame=(\d+) pc=([0-9a-f]{8})$", log, re.M)],
        "qr": re.findall(r"^fatal-qr: (\S.*)$", log, re.M),
    }
    if cp0 and words and tlb:
        facts["cp0"] = {"epc": int(cp0[1], 16), "badvaddr": int(cp0[2], 16), "entryhi": int(cp0[3], 16), "context": int(cp0[4], 16),
                        "vec": int(cp0[5]), "exc_count": int(cp0[6]), "tlb_valid": int(cp0[7]), "unit_slot": int(cp0[8]),
                        "flags": int(cp0[9], 16), "frame": int(cp0[10], 16)}
        facts["words"] = {key: [int(w, 16) for w in words[i].split()] for i, key in enumerate(("epc_words", "entry_words", "mapped_words"), 1)}
        names = ("index", "hi", "lo0", "lo1", "mask")
        parse = lambda start: dict(zip(names, (int(tlb[start]), int(tlb[start + 1], 16), int(tlb[start + 2], 16),
                                                int(tlb[start + 3], 16), int(tlb[start + 4], 16))))
        facts["tlb"] = {"pc": parse(1), "unit": parse(6)}
    return facts


def check(case, directory, log, shot, args):
    """Verify one case; returns a one-line summary."""
    kind, exccode, resolve, message = CASES[case]
    pixels = numpy.asarray(Image.open(shot).convert("RGB"))
    if pixels.shape != (240, 320, 3):
        fail(case, f"screenshot extent {pixels.shape}")
    found = zxingcpp.read_barcodes(Image.open(shot).convert("RGB"), formats=zxingcpp.BarcodeFormat.QRCode)
    if len(found) != 1:
        fail(case, f"zxing-cpp found {len(found)} symbols")
    text = found[0].text
    other, _, _ = cv2.QRCodeDetectorAruco().detectAndDecode(cv2.imread(str(shot)))
    if other != text:
        fail(case, f"OpenCV decoded {other!r}, zxing-cpp {text!r}")
    f = decode.parse(text)
    facts = log_facts(log)
    if facts is None:
        # A constructor fault before main: the log does not exist yet. The symbol's
        # own facts stand in; the screen text is still compared against them.
        facts = {"pc": f["pc"], "origin": f["ra"], "sr": f["sr"], "cause": f["cause"], "badvaddr": f["badvaddr"],
                 "message": f["message"], "r31": f["ra"], "trace": [f["pc"]] + f["frames"], "qr": [text]}
    expected_logs = 1 if case == "nested" else None
    if not facts["qr"] or (expected_logs and len(facts["qr"]) != expected_logs):
        fail(case, f"logged symbols {len(facts['qr'])}")
    if facts["qr"][-1] != text:
        fail(case, f"screen decodes to {text!r} but the last logged symbol is {facts['qr'][-1]!r}")
    # Symbol area: the very pixels segno would draw, quiet zone included; gap: background.
    matrix = reference.matrix(text)
    edge = numpy.ones((EDGE, EDGE), dtype=numpy.uint8)
    edge[QUIET:EDGE - QUIET, QUIET:EDGE - QUIET] = 1 - matrix
    wanted = numpy.kron(edge, numpy.ones((SCALE, SCALE), dtype=numpy.uint8)) * 255
    area = pixels[QR_Y:QR_Y + SIDE, QR_X:QR_X + SIDE]
    if QR_Y + SIDE != 240 or QR_X + SIDE != 320:
        fail(case, "symbol is not flush with the screen corner")
    if not (area[:, :, 0] == area[:, :, 1]).all() or not (area[:, :, 0] == wanted).all():
        fail(case, "symbol area differs from the reference rendering (overlap or encoder error)")
    background = pixels[QR_Y, QR_X - 2]
    if (pixels[QR_Y:, QR_X - GAP:QR_X] != background).any():
        fail(case, "text or other pixels in the gap beside the symbol")
    # Facts.
    build = re.search(r'TR64_BUILD_ID "([0-9A-F]{12})"', (Path(directory) / "generated/build_id.h").read_text())[1]
    cp0 = facts.get("cp0")
    want = {"kind": kind, "build": build, "pc": facts["pc"], "badvaddr": cp0["badvaddr"] if cp0 else facts["badvaddr"],
            "sr": facts["sr"], "cause": facts["cause"], "level": None, "format": 2}
    for key, value in want.items():
        if f[key] != value:
            fail(case, f"{key}: QR {f[key]!r} but the cart logged {value!r}")
    if cp0:
        for key in ("epc", "entryhi", "context", "vec", "exc_count", "tlb_valid", "unit_slot", "flags", "frame"):
            if f[key] != cp0[key]:
                fail(case, f"{key}: QR {f[key]!r} but the cart logged {cp0[key]!r}")
        for key in ("epc_words", "entry_words", "mapped_words"):
            if f[key] != facts["words"][key]:
                fail(case, f"{key}: QR {f[key]!r} but the cart logged {facts['words'][key]!r}")
        for key, entry in (("pc_entry", facts["tlb"]["pc"]), ("unit_entry", facts["tlb"]["unit"])):
            got = f[key]
            if (got["index"] if got["index"] is not None else 255, got["lo0"], got["lo1"], got["mask"]) != \
                    (entry["index"], entry["lo0"], entry["lo1"], entry["mask"]) or \
                    got["hi"] != (entry["hi"] >> 62) << 62 | ((entry["hi"] >> 32) & 0xFF) << 32 | (entry["hi"] & 0xFFFFFFFF):
                fail(case, f"{key}: QR {got!r} but the cart logged {entry!r}")
        for number, value in facts["regs"].items():
            if number in f["gpr"]:
                if f["gpr"][number] != value & 0xFFFFFFFF:
                    fail(case, f"${number}: QR {f['gpr'][number]:08x} but the cart logged {value:016x}")
                wide = (value >> 32) != (0xFFFFFFFF if value & 0x80000000 else 0)
                if bool(f["gpr_wide"] >> number & 1) != wide:
                    fail(case, f"${number}: wide flag {f['gpr_wide'] >> number & 1} for {value:016x}")
        if kind == "exception":
            if "stash" not in f["flag_names"] or f["exc_count"] < 1 or f["epc"] != facts["origin"]:
                fail(case, f"exception without a vector stash: flags {f['flag_names']} count {f['exc_count']} EPC {f['epc']:08x}")
            if f["exccode"] == 4 and f["vec"] != 3:
                fail(case, f"address error taken at vector {f['vec']}, not the general vector 3")
            if f["exccode"] in (2, 3) and f["vec"] not in (0, 1, 3):
                fail(case, f"TLB exception at vector {f['vec']}")
    if kind == "exception":
        if f["exccode"] != (facts["cause"] >> 2 & 31) or (exccode is not None and f["exccode"] != exccode):
            fail(case, f"ExcCode {f['exccode']}")
        if f["ra"] != facts["r31"]:
            fail(case, f"RA {f['ra']:08x} != logged R31 {facts['r31']:08x}")
    elif f["ra"] != facts["origin"] or f["exccode"] != 0:
        fail(case, f"RA {f['ra']:08x} != logged origin {facts['origin']:08x}")
    if case == "nested":
        if f["frames"]:
            fail(case, "nested fault: the first report's symbol should hold no walk")
    elif f["frames"] != facts["trace"][1:1 + len(f["frames"])] or len(f["frames"]) < min(len(facts["trace"]) - 1, 3):
        fail(case, f"frames {[hex(a) for a in f['frames']]} != logged backtrace {[hex(a) for a in facts['trace']]}")
    if not re.search(message, f["message"]) or f["message"] not in (facts["message"] or "") + "\n":
        fail(case, f"message {f['message']!r} does not match {message!r} / the logged {facts['message']!r}")
    # The screen text shows the same facts the symbol carries.
    table = glyphs()
    shown = [f"BUILD {build}", f"PC {f['pc']:08X}", f"RA {f['ra']:08X}", f"SR {f['sr']:08X}", f"CAUSE {f['cause']:08X}",
             f"EPC {f['epc']:08X}", f"BVA {f['badvaddr']:016X}", f"EHI {f['entryhi']:016X}", f"CTX {f['context']:016X}",
             f"V{f['vec']} N{f['exc_count']} T{f['tlb_valid']} U{f['unit_slot']:02X} F{f['flags']:02X}"]
    shown += [f"{i} {facts['trace'][i]:08X}" for i in range(len(facts["trace"]))]
    for line in shown:
        if text_row(pixels, background, table, line) is None:
            fail(case, f"the screen does not show {line!r}")
    if text_row(pixels, background, table, shown[0]) != 10:
        fail(case, "build row is not at y=10")
    # decode.py: symbols, units, line numbers.
    elf = Path(directory) / "tr64.elf"
    ran = subprocess.run([sys.executable, str(ROOT / "tools/fatal/decode.py"), "--elf", str(elf), "--image", str(shot)],
                         capture_output=True, text=True)
    if ran.returncode or "WARNING" in ran.stdout:
        fail(case, f"decode.py failed ({ran.returncode}): {ran.stdout}{ran.stderr}")
    for prefix, pattern in resolve.items():
        line = next((l for l in ran.stdout.splitlines() if l.strip().startswith(prefix)), "")
        if not re.search(pattern, line) or not re.search(r"\.(c|S):\d+|\[code unit", line):
            fail(case, f"decode.py line {prefix!r}: {line!r} lacks {pattern!r} with a source line\n{ran.stdout}")
    if case == "nested" and "units     mapped: test" not in ran.stdout:
        fail(case, f"unit set lacks test:\n{ran.stdout}")
    if "epc-words" in f["flag_names"]:
        line = next((l for l in ran.stdout.splitlines() if l.startswith("EPC-4..+8")), "")
        if "DIFFERENT" in line or "EPC-4..+8" not in line:
            fail(case, f"decode.py: the words at EPC differ from the ELF or are missing:\n{ran.stdout}")
    if args.evidence:
        Path(args.evidence).mkdir(parents=True, exist_ok=True)
        Image.open(shot).convert("RGB").save(Path(args.evidence) / f"{args.profile}-{case}.png", optimize=True)
    return (f"fatal QR PASS {case}: {len(text)} chars decoded by zxing-cpp and OpenCV, symbol pixels equal to segno's, "
            f"facts equal the log, decode.py resolved {', '.join(resolve) or 'facts only'}")


def negatives(case, directory, log, shot, args):
    """The check must be able to fail: three tampered copies of a passing screenshot, each rejected for its reason."""
    pixels = numpy.asarray(Image.open(shot).convert("RGB")).copy()
    background = pixels[QR_Y, QR_X - 2].copy()
    flipped = pixels.copy()
    block = flipped[QR_Y + 3 * (4 + 34):QR_Y + 3 * (4 + 35), QR_X + 3 * (4 + 34):QR_X + 3 * (4 + 35)]
    block[:] = 255 - block
    gap = pixels.copy()
    gap[QR_Y + 50, QR_X - 2] = (255, 255, 255)
    wiped = pixels.copy()
    wiped[28:100, :QR_X - GAP] = background
    out = Path(directory) / "run"
    for name, image, reason in (("module", flipped, "symbol area differs"), ("gap", gap, "gap beside the symbol"),
                                ("text", wiped, "screen does not show")):
        path = out / f"negative-{name}.png"
        Image.fromarray(image).save(path)
        try:
            check(case, directory, log, path, type("A", (), {"evidence": None, "profile": ""}))
        except AssertionError as error:
            if reason not in str(error):
                raise AssertionError(f"negative control {name} failed for the wrong reason: {error}") from None
        else:
            raise AssertionError(f"negative control {name} was accepted")
    return f"fatal QR negative controls PASS: a flipped module, a stray pixel in the gap and wiped text are each rejected"
