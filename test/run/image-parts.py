#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Resident image by component, from a build's size.json and linker map.

usage: image-parts.py BUILD_DIR [--top N]
BUILD_DIR holds tr64.map and the size.json that `make rom` writes (test/run/size_gate).
Every resident byte is one of: an object's text, rodata, string literals, data or bss;
an alignment hole between sections (the low page, the 8 KiB hot-data anchor, section
alignment); or fill inside a section. Components follow the object path (COMPONENTS
below, first match wins), so a new file lands in the last row until it is listed.

String literals are one merged input section in the map, owned by the first object that
had a string; each string section's own size is on the line after it ("size before
relaxing"), so the merged bytes are shared out over their objects in that proportion
(duplicates and shared tails make the merged total about 2% smaller).
"""
import argparse
import collections
import json
import pathlib
import re

COMPONENTS = [
    ("video: RSP microcode and SDK decoder (libdragon)", r"^libdragon\.a\(.*(rsph264|rsp_yuv|h264|yuv)"),
    ("video: engine glue", r"^engine/media/|^plat/n64/film\.o$"),
    ("SDK: Tiny3D (with its RSP microcode)", r"^libt3d\.a"),
    ("SDK: libdragon", r"^libdragon\.a|^libdragonsys\.a"),
    ("SDK: newlib, libgcc, crt", r"^lib(c|m|gcc)\.a|^crt"),
    ("audio", r"^engine/snd/|^plat/n64/audio\.o$"),
    ("renderer", r"^engine/render/|^plat/n64/(gpu|surfaces)\.o$"),
    ("front end (UI, menus, save)", r"^engine/ui/|^engine/save/|^plat/n64/ui|^game/common/(frontend|menu_text|save_world)\.o$"),
    ("diagnostics (fatal screen, QR, unwinder, formatter, log)",
     r"^plat/n64/(fatal|fmt-sdk)|^engine/core/(qr|unwind|fmt|log)\.o$"),
    ("scene and test machinery in the build", r"^engine/test/"),
    ("game resident", r"^game/"),
    ("engine core, memory, loader, I/O, input, platform", r""),
]
KINDS = ("text", "rodata", "str", "data", "bss")


def owner(path):
    path = path.strip()
    return path.split("/cart/", 1)[1] if "/cart/" in path else re.sub(r".*/", "", path)


def string_sections(map_path, low, high):
    """Own size of every resident string input section, by owning object."""
    lines = pathlib.Path(map_path).read_text().splitlines()
    sizes = collections.Counter()
    for i, line in enumerate(lines):
        if not re.match(r"^ \.rodata\S*\.str\d\.\d(\s|$)", line):
            continue
        inline = re.search(r"0x[0-9a-f]+\s+0x", line)
        address_line = line if inline else lines[i + 1]
        match = re.search(r"(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$", address_line)
        if not match or not low <= int(match[1], 16) < high:
            continue
        relaxed = re.search(r"0x([0-9a-f]+) \(size before relaxing\)", lines[i + (1 if inline else 2)])
        if relaxed:
            sizes[owner(match[3])] += int(relaxed[1], 16)
    return sizes


def holes(report):
    """Gaps between resident sections (physical order) and what is left inside them."""
    rows = sorted((s for s in report["sections"] if not s["unit"] and (s["bytes"] or s["name"] in (".sbss", ".bss"))),
                  key=lambda s: s["address"] & 0x1FFFFFFF)
    gaps, cursor = [], 0
    for section in rows:
        start = section["address"] & 0x1FFFFFFF
        if start > cursor:
            gaps.append((start - cursor, section["name"]))
        cursor = start + section["bytes"]
    return gaps, cursor


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--top", type=int, default=10)
    ns = parser.parse_args()
    report = json.loads((ns.build / "size.json").read_text())
    rodata = next(s for s in report["sections"] if s["name"] == ".rodata" and not s["unit"])
    strings = string_sections(ns.build / "tr64.map", rodata["address"], rodata["address"] + rodata["bytes"])
    merged = sum(r["bytes"] for r in report["map_records"] if r["merged_strings"])
    scale = merged / sum(strings.values())
    objects = collections.defaultdict(collections.Counter)
    for record in report["map_records"]:
        if not record["merged_strings"]:
            objects[record["owner"]][record["kind"]] += record["bytes"]
    for name, size in strings.items():
        objects[name]["str"] += size * scale
    table = collections.defaultdict(collections.Counter)
    for name, counts in objects.items():
        row = next(label for label, pattern in COMPONENTS if re.search(pattern, name))
        table[row].update(counts)
    print(f"{'component':58s}" + "".join(f"{k:>9s}" for k in KINDS) + f"{'total':>9s}")
    accounted = 0
    for row, counts in sorted(table.items(), key=lambda item: -sum(item[1].values())):
        total = sum(counts.values())
        accounted += total
        print(f"{row:58s}" + "".join(f"{counts[k]:9.0f}" for k in KINDS) + f"{total:9.0f}")
    gaps, end = holes(report)
    gap_bytes = sum(size for size, _ in gaps)
    print(f"{'alignment holes between sections':58s}{gap_bytes:>63d}  " +
          ", ".join(f"{size} before {name}" for size, name in gaps))
    inside = report["resident"] - round(accounted) - gap_bytes
    print(f"{'fill inside sections and linker-made bytes':58s}{inside:>63d}")
    print(f"{'resident image (size_gate: end of .bss)':58s}{report['resident']:>63d}  of {report['budget']}")
    print(f"\nlargest {ns.top} resident objects (string literals shared out as above):")
    sums = sorted(((sum(c.values()), name) for name, c in objects.items()), reverse=True)[:ns.top]
    for total, name in sums:
        c = objects[name]
        print(f"{total:8.0f}  text {c['text']:6.0f}  rodata {c['rodata']:6.0f}  strings {c['str']:6.0f}  "
              f"data {c['data']:6.0f}  bss {c['bss']:6.0f}  {name}")


if __name__ == "__main__":
    main()
