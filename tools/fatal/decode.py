#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Decode the QR code on the tr64 fatal screen and resolve its addresses.

    decode.py [--elf tr64.elf | --archive DIR] [--image shot.png] [TEXT]

TEXT is what a phone's QR reader shows ("TR64.1.<base45>"), given as an
argument or on standard input. --image decodes the symbol from a photograph or
screenshot instead (needs zxing-cpp or OpenCV; neither is required for TEXT).
The ELF is --elf, else the build id's archive entry under DIR (default
/tmp/tr64/evidence/ship/<build id>/, a tr64.elf or the delivered
*.tar.zst that contains it). Functions and lines come from
mips64-elf-addr2line (N64_GCCPREFIX, default /opt/mips64-toolchain,
or --addr2line); without an ELF the facts are still printed.

The payload format is documented in docs/architecture.md (Diagnostics). RFC
9285 base-45 carries a big-endian binary record; format 1 is parsed here.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"
PREFIX = "TR64."
HEADER = 38
KINDS = {1: "exception", 2: "assert", 3: "error"}
# VR4300 CP0 Cause.ExcCode values (MIPS III exception codes).
EXCCODE = {0: "Int interrupt", 1: "Mod TLB modification", 2: "TLBL TLB miss (load or fetch)",
           3: "TLBS TLB miss (store)", 4: "AdEL address error (load or fetch)",
           5: "AdES address error (store)", 6: "IBE bus error (fetch)", 7: "DBE bus error (data)",
           8: "Sys syscall", 9: "Bp breakpoint", 10: "RI reserved instruction",
           11: "CpU coprocessor unusable", 12: "Ov arithmetic overflow", 13: "Tr trap",
           15: "FPE floating point", 23: "Watch"}
UNIT_BASE = 0xC1000000
UNIT_STRIDE = 0x80000
DEFAULT_ARCHIVE = Path("/tmp/tr64/evidence/ship")
DEFAULT_PREFIX = "/opt/mips64-toolchain"


class FatalDecodeError(ValueError):
    pass


def base45_decode(text):
    """RFC 9285 section 4.3, strict: any invalid character or group is an error."""
    try:
        values = [ALPHABET.index(c) for c in text]
    except ValueError as error:
        raise FatalDecodeError(f"character outside the base-45 alphabet: {error}") from None
    if len(values) % 3 == 1:
        raise FatalDecodeError("base-45 text has a dangling character")
    out = bytearray()
    for i in range(0, len(values), 3):
        group = values[i:i + 3]
        if len(group) == 3:
            n = group[0] + group[1] * 45 + group[2] * 2025
            if n > 0xFFFF:
                raise FatalDecodeError("base-45 group above 65535")
            out += n.to_bytes(2, "big")
        else:
            n = group[0] + group[1] * 45
            if n > 0xFF:
                raise FatalDecodeError("base-45 final group above 255")
            out.append(n)
    return bytes(out)


def frame_address(code):
    selector, word = code >> 22, code & 0x3FFFFF
    base = {0: 0x80000000, 1: 0xC0000000, 2: 0xC1000000}.get(selector)
    if base is None:
        raise FatalDecodeError("frame with reserved region selector")
    return base + word * 4


FLAGS = [(0, "stash"), (1, "nested"), (2, "pc-mapped"), (3, "unit-mapped"), (4, "epc-words"), (5, "entry-words"),
         (6, "mapped-words"), (7, "exl-already-set")]
HEADER2 = 273
GPRS = [i for i in range(1, 32) if i not in (26, 27)]


def split_text(text):
    """(format version string, base-45 body bytes) of one "TR64.<version>.<body>." text."""
    text = text.strip("\r\n")
    start = text.find(PREFIX)
    if start < 0:
        raise FatalDecodeError("no TR64 prefix: this is not a tr64 fatal QR text")
    match = re.match(r"TR64\.([0-9A-Z]+)\.", text[start:])
    if not match:
        raise FatalDecodeError("malformed TR64 prefix")
    body = text[start + match.end():]
    if body.endswith("."):
        body = body[:-1]
    return match.group(1), base45_decode(body)


def parse(text):
    """Return the crash facts of one "TR64.<version>.<body>" text as a dict (format 1 or 2)."""
    version, body = split_text(text)
    if version == "1":
        return parse1(body)
    if version == "2":
        return parse2(body)
    raise FatalDecodeError(f"unsupported payload format version {version}; this tool reads 1 and 2")


def parse1(body):
    if len(body) < HEADER:
        raise FatalDecodeError(f"record has {len(body)} bytes, need at least {HEADER}")
    kind, packed = body[0], body[1]
    frames = packed >> 5
    word = lambda at, n=4: int.from_bytes(body[at:at + n], "big")
    if len(body) < HEADER + 3 * frames:
        raise FatalDecodeError("record shorter than its frame count")
    level = word(28, 2)
    return {
        "format": 1,
        "kind": KINDS.get(kind, f"unknown({kind})"),
        "kind_code": kind,
        "exccode": packed & 31,
        "build": body[2:8].hex().upper(),
        "pc": word(8), "ra": word(12), "badvaddr": word(16), "sr": word(20), "cause": word(24),
        "level": None if level == 0xFFFF else level,
        "tick": word(30),
        "units_mask": word(34),
        "frames": [frame_address(word(HEADER + 3 * i, 3)) for i in range(frames)],
        "message": body[HEADER + 3 * frames:].decode("latin-1"),
    }


def parse2(body):
    """Format 2: the record of qr.c (layout in docs/architecture.md section 3.1)."""
    if len(body) < HEADER2:
        raise FatalDecodeError(f"record has {len(body)} bytes, need at least {HEADER2}")
    kind, packed = body[0], body[1]
    nframes = packed >> 5
    at = [2]

    def take(n):
        value = int.from_bytes(body[at[0]:at[0] + n], "big")
        at[0] += n
        return value

    facts = {"format": 2, "kind": KINDS.get(kind, f"unknown({kind})"), "kind_code": kind, "exccode": packed & 31,
             "build": body[2:8].hex().upper()}
    at[0] = 8
    for key, size in (("pc", 4), ("ra", 4), ("badvaddr", 8), ("sr", 4), ("cause", 4), ("epc", 4), ("entryhi", 8),
                      ("context", 8), ("level", 2), ("tick", 4), ("units_mask", 4), ("flags", 1), ("vec", 1),
                      ("exc_count", 1), ("tlb_valid", 1), ("unit_slot", 1), ("gpr_wide", 4)):
        facts[key] = take(size)
    facts["level"] = None if facts["level"] == 0xFFFF else facts["level"]
    facts["flag_names"] = [name for bit, name in FLAGS if facts["flags"] >> bit & 1]
    facts["gpr"] = {i: take(4) for i in GPRS}
    for key in ("epc_words", "entry_words", "mapped_words"):
        facts[key] = [take(4) for _ in range(4)]
    for key in ("pc_entry", "unit_entry"):
        index, lo, up = take(1), take(4), take(2)
        facts[key] = {"index": None if index == 0xFF else index, "hi": (up & 0xC000) << 48 | (up & 0xFF) << 32 | lo,
                      "lo0": take(4), "lo1": take(4), "mask": take(2) << 13}
    facts["frame"] = take(4)
    if at[0] != HEADER2:
        raise FatalDecodeError("internal: format 2 fixed part is not 273 bytes")
    if len(body) < HEADER2 + 3 * nframes:
        raise FatalDecodeError("record shorter than its frame count")
    facts["frames"] = [frame_address(int.from_bytes(body[HEADER2 + 3 * i:HEADER2 + 3 * i + 3], "big")) for i in range(nframes)]
    facts["message"] = body[HEADER2 + 3 * nframes:].decode("latin-1")
    return facts


def decode_image(path):
    """Text of the first QR symbol in an image, with zxing-cpp or OpenCV."""
    errors = []
    try:
        import zxingcpp
        from PIL import Image
        results = zxingcpp.read_barcodes(Image.open(path).convert("L"), formats=zxingcpp.BarcodeFormat.QRCode)
        if results:
            return results[0].text
        errors.append("zxing-cpp found no symbol")
    except ImportError as error:
        errors.append(f"zxing-cpp unavailable ({error})")
    try:
        import cv2
        image = cv2.imread(str(path))
        if image is None:
            raise FatalDecodeError(f"cannot read image {path}")
        text, _, _ = cv2.QRCodeDetector().detectAndDecode(image)
        if text:
            return text
        errors.append("OpenCV found no symbol")
    except ImportError as error:
        errors.append(f"OpenCV unavailable ({error})")
    raise FatalDecodeError("; ".join(errors))


def elf_for(build, elf, archive):
    """Path of the ELF, extracting it from the delivered archive when needed."""
    if elf:
        return Path(elf), None
    folder = Path(archive) / build
    loose = folder / "tr64.elf"
    if loose.is_file():
        return loose, None
    for tarball in sorted(folder.glob("*.tar.zst")) + sorted(folder.glob("*.tar.gz")):
        scratch = Path(tempfile.mkdtemp(prefix="tr64-fatal-", dir=os.environ.get("TMPDIR")))
        try:
            with open(scratch / "tr64.elf", "wb") as out:
                flag = "--zstd" if tarball.suffix == ".zst" else "-z"
                subprocess.run(["tar", flag, "-xOf", str(tarball), "tr64.elf"], check=True, stdout=out)
        except (OSError, subprocess.CalledProcessError) as error:
            shutil.rmtree(scratch, ignore_errors=True)
            raise FatalDecodeError(f"cannot extract tr64.elf from {tarball}: {error}") from None
        return scratch / "tr64.elf", scratch
    raise FatalDecodeError(f"no ELF for build {build}: nothing under {folder}; pass --elf")


class Symbols:
    def __init__(self, elf, prefix, addr2line=None):
        self.elf = str(elf)
        bin_dir = Path(prefix) / "bin"
        self.addr2line = addr2line or str(bin_dir / "mips64-elf-addr2line")
        self.readelf = str(bin_dir / "mips64-elf-readelf")
        self.units = self.unit_sections()

    def unit_sections(self):
        text = subprocess.run([self.readelf, "-S", "-W", self.elf], check=True,
                              capture_output=True, text=True).stdout
        units = {}
        for line in text.splitlines():
            m = re.search(r"\]\s+\.tlbcode\.(\S+)\s+PROGBITS\s+([0-9a-f]{8})\s+\S+\s+([0-9a-f]+)", line)
            if m:
                base, size = int(m.group(2), 16), int(m.group(3), 16)
                if base >= UNIT_BASE and (base - UNIT_BASE) % UNIT_STRIDE == 0:
                    units[(base - UNIT_BASE) // UNIT_STRIDE] = (m.group(1), base, size)
        return units

    def contains_build(self, build):
        return build.encode() in Path(self.elf).read_bytes()

    def unit_of(self, address):
        for slot, (name, base, size) in self.units.items():
            if base <= address < base + size:
                return slot, name
        return None

    def resolve(self, address):
        """[(function, 'file:line')] for address, innermost inline level first."""
        lines = subprocess.run([self.addr2line, "-f", "-C", "-i", "-e", self.elf, f"0x{address:08x}"],
                               check=True, capture_output=True, text=True).stdout.splitlines()
        return [(lines[i], lines[i + 1]) for i in range(0, len(lines) - 1, 2)]

    def words(self, address, count=4):
        """The ELF's big-endian words at address (a section's contents), or None."""
        objdump = str(Path(self.readelf).with_name("mips64-elf-objdump"))
        run = subprocess.run([objdump, "-s", f"--start-address=0x{address:08x}", f"--stop-address=0x{address + 4 * count:08x}",
                              self.elf], capture_output=True, text=True)
        data = bytearray()
        for line in run.stdout.splitlines():
            m = re.match(r"\s+([0-9a-f]{4,8}) ((?:[0-9a-f]{8} ?){1,4})", line)
            if m:
                data += bytes.fromhex(m.group(2).replace(" ", ""))
        if len(data) != 4 * count:
            return None
        return [int.from_bytes(data[i:i + 4], "big") for i in range(0, len(data), 4)]


def place(symbols, address):
    """One printable resolution of address: 'function (file:line)'."""
    if symbols is None:
        return ""
    rows = symbols.resolve(address)
    shown = []
    for function, location in rows:
        shown.append(function if location.startswith("??") else f"{function} ({location})")
    where = symbols.unit_of(address)
    note = f"  [code unit {where[1]}]" if where else ""
    return (" <- ".join(shown) or "unknown") + note


def report(facts, symbols):
    lines = []
    out = lines.append
    out(f"TR64 crash report, payload format {facts['format']}")
    out(f"build     {facts['build']}")
    kind = facts["kind"]
    if kind == "exception":
        code = facts["exccode"]
        kind += f", Cause.ExcCode {code} = {EXCCODE.get(code, 'unnamed')}"
    out(f"kind      {kind}")
    if symbols and not symbols.contains_build(facts["build"]):
        out(f"WARNING   the ELF does not contain build id {facts['build']}: addresses may not resolve")
    out(f"PC        {facts['pc']:08X}  {place(symbols, facts['pc'])}")
    ra = facts["ra"]
    out(f"RA        {ra:08X}  " + (f"call site {ra - 8:08X}: {place(symbols, ra - 8)}" if ra >= 8 else ""))
    wide = facts["format"] == 2
    out(f"BadVAddr  {facts['badvaddr']:0{16 if wide else 8}X}")
    out(f"SR        {facts['sr']:08X}")
    out(f"Cause     {facts['cause']:08X}" + ("  (branch delay slot)" if facts["cause"] & 0x80000000 else ""))
    if wide:
        out(f"EPC       {facts['epc']:08X}  {place(symbols, facts['epc'])}")
        out(f"EntryHi   {facts['entryhi']:016X}")
        out(f"Context   {facts['context']:016X}")
        out(f"flags     {', '.join(facts['flag_names']) or 'none'}   vector {facts['vec']}   "
            f"non-interrupt exceptions {facts['exc_count']}   TLB entries valid {facts['tlb_valid']}")
    level = facts["level"]
    out(f"level     {'none' if level is None else level}   tick {facts['tick']}")
    mask = facts["units_mask"]
    names = []
    for slot in range(32):
        if mask >> slot & 1:
            names.append(symbols.units[slot][0] if symbols and slot in symbols.units else f"slot{slot}")
    out("units     mapped: " + (", ".join(names) or "none"))
    out("frames    return-address walk after PC (call sites, nearest caller first):")
    for i, address in enumerate(facts["frames"], 1):
        out(f"  #{i:<2}     {address:08X}  {place(symbols, address)}")
    if not facts["frames"]:
        out("  none")
    out(f"message   {facts['message']!r}" if facts["message"] else "message   none")
    if wide:
        lines.extend(report2(facts, symbols))
    return "\n".join(lines)


def tlb_line(name, entry, address):
    if entry["index"] is None:
        return f"{name:9} no TLB entry maps {address:08X}"
    mask = entry["mask"]
    page = ((mask | 0x1FFF) >> 1) + 1
    parts = []
    for lo, label in ((entry["lo0"], "even"), (entry["lo1"], "odd")):
        parts.append(f"{label} pfn {(lo & 0x3FFFFFC0) << 6:08X} C{(lo >> 3) & 7} {'D' if lo & 4 else '-'}{'V' if lo & 2 else '-'}{'G' if lo & 1 else '-'}")
    return (f"{name:9} entry {entry['index']}: hi {entry['hi']:016X} page {page // 1024} KiB  " + "  ".join(parts))


def words_line(name, words, expected):
    shown = " ".join(f"{w:08X}" for w in words)
    if expected is None:
        return f"{name:9} {shown}"
    verdict = "equal to the ELF" if list(words) == expected else "DIFFERENT from the ELF: " + " ".join(f"{w:08X}" for w in expected)
    return f"{name:9} {shown}   {verdict}"


def report2(facts, symbols):
    lines = []
    out = lines.append
    flags = facts["flag_names"]
    unit_base = UNIT_BASE + facts["unit_slot"] * UNIT_STRIDE
    out("registers (low 32 bits; * = upper half is not the sign extension; $26 and $27 are not saved by the SDK)")
    row = []
    for i in GPRS:
        row.append(f"${i:<2}{'*' if facts['gpr_wide'] >> i & 1 else ' '}{facts['gpr'][i]:08X}")
        if len(row) == 4:
            out("  " + "  ".join(row))
            row = []
    if row:
        out("  " + "  ".join(row))
    exp = (lambda address: symbols.words(address)) if symbols else (lambda address: None)
    if "epc-words" in flags:
        out(words_line("EPC-4..+8", facts["epc_words"], exp(facts["epc"] - 4)))
    if "entry-words" in flags:
        out(words_line(f"entry@{unit_base:08X}", facts["entry_words"], exp(unit_base)))
    if "mapped-words" in flags:
        out(words_line("mapped", facts["mapped_words"], exp(unit_base)))
        if "entry-words" in flags and facts["mapped_words"] != facts["entry_words"]:
            out("          the words read through the mapped address differ from RDRAM read through kseg1")
    out(tlb_line("PC map", facts["pc_entry"], facts["pc"]))
    out(tlb_line("unit map", facts["unit_entry"], unit_base) + (f"   first frame {facts['frame']:08X}" if facts["frame"] else ""))
    return lines


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("text", nargs="?", help="decoded QR text (default: standard input)")
    parser.add_argument("--image", type=Path, help="decode the QR symbol from this image")
    parser.add_argument("--elf", type=Path, help="tr64.elf of the crashed build")
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--prefix", default=os.environ.get("N64_GCCPREFIX", DEFAULT_PREFIX))
    parser.add_argument("--addr2line")
    parser.add_argument("--json", action="store_true", help="print the parsed facts as JSON, no symbols")
    args = parser.parse_args(argv)
    try:
        if args.image:
            text = decode_image(args.image)
        else:
            text = args.text if args.text is not None else sys.stdin.read()
        facts = parse(text)
        if args.json:
            print(json.dumps(facts, indent=1))
            return 0
        scratch = None
        try:
            elf, scratch = elf_for(facts["build"], args.elf, args.archive)
            symbols = Symbols(elf, args.prefix, args.addr2line)
            print(report(facts, symbols))
        except FatalDecodeError as error:
            print(report(facts, None))
            print(f"symbols   unavailable: {error}", file=sys.stderr)
            return 2
        finally:
            if scratch:
                shutil.rmtree(scratch, ignore_errors=True)
    except FatalDecodeError as error:
        print(f"decode.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
