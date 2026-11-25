#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Integer scene compiler; ported from MIT ac3n64 04134cc.
`load level NAME` selects a required level loader; omission means no level.
Native version 3 stores dedicated game/placement words covered by the CRC.
Version 4 is for TR2 schemas 20 and 21, with a wider event mask.
"""
import argparse
import hashlib
import pathlib
import shlex
import struct
import zlib
import re

TR2_INPUT_MASK = 123902  # include/tr64/game.h
TR2_RING_EVENT, TR2_RING_KEY, TR2_RING_PAUSE_SHIFT = 512, 1024, 11


def parse(path):
    p = pathlib.Path(path)
    if p.stat().st_size > 65536:
        raise ValueError("scene exceeds 64 KiB")
    meta, events, observables = {}, [], []
    choices = {}
    level = ""
    game, place = False, None
    hash_schema = None
    for number, line in enumerate(p.read_text(encoding="ascii").splitlines(), 1):
        words = shlex.split(line, comments=True)
        if not words:
            continue
        key, *args = words
        try:
            if key == "at" and len(args) == 5 and args[1] == "pad":
                tick, buttons, x, y = (int(args[i], 0) for i in (0, 2, 3, 4))
                if not 0 <= buttons <= 0xffffffff or not -80 <= x <= 80 or not -80 <= y <= 80:
                    raise ValueError("pad range")
                if events and tick <= events[-1][0]:
                    raise ValueError("events must strictly increase")
                events.append((tick, buttons, x, y))
            elif key == "at" and len(args) == 4 and args[1] in ("ring", "key"):
                tick, item, pause = (int(args[i], 0) for i in (0, 2, 3))
                is_key = args[1] == "key"
                if tick in choices or not 0 <= item < 265 or not 0 <= pause < (1 << 21):
                    raise ValueError("duplicate or out-of-range ring event")
                if is_key and item not in (0, 197, 198, 199, 200):
                    raise ValueError("key choice must name a public key ring type")
                choices[tick] = (item | TR2_RING_EVENT | (TR2_RING_KEY if is_key else 0)
                                 | (pause << TR2_RING_PAUSE_SHIFT))
            elif key == "observe" and args:
                name, *operands = args
                if not re.fullmatch(r"[a-z][a-z0-9-]*", name):
                    raise ValueError("invalid observable name")
                if any(item[0] == name for item in observables):
                    raise ValueError("duplicate observable")
                observables.append((name, operands))
            elif key == "load" and len(args) == 2 and args[0] == "level":
                if level or not re.fullmatch(r"[a-z][a-z0-9_-]{0,30}", args[1]):
                    raise ValueError("duplicate/invalid level load identity")
                level = args[1]
            elif key == "game" and not args:
                if game:
                    raise ValueError("duplicate game request")
                game = True
            elif key == "hash-schema" and len(args) == 1:
                if hash_schema is not None:
                    raise ValueError("duplicate hash schema")
                hash_schema = int(args[0], 0)
                if hash_schema not in (2, 3, 20, 21, 22):
                    raise ValueError("hash schema must be 2, 3, 20, 21 or 22")
            elif key == "place" and len(args) == 5:
                if place is not None:
                    raise ValueError("duplicate placement")
                place = tuple(int(item, 0) for item in args)
                room, x, y, z, angle = place
                if not 0 <= room <= 0xffffffff or any(not -0x80000000 <= v <= 0x7fffffff for v in (x, y, z)) or not -32768 <= angle <= 32767:
                    raise ValueError("placement range")
            elif key in ("scene", "start", "ticks", "seed", "expect-state", "expect-shot", "probe") and len(args) == 1:
                if key in meta:
                    raise ValueError("duplicate directive")
                meta[key] = args[0]
            else:
                raise ValueError("unknown directive or operands")
        except ValueError as e:
            raise ValueError(f"{p}:{number}: {e}") from e
    probes = {"none": 0, "unloaded": 1, "pool": 2, "highmem": 3, "heap": 4, "clock-wrap": 5, "reload": 6, "video": 7}
    probe = meta.pop("probe", "none")
    if probe not in probes:
        raise ValueError("unknown production-path probe")
    required = {"scene", "start", "ticks", "seed"}
    if not required <= set(meta) or set(meta) - required - {"expect-state", "expect-shot"} or not observables:
        raise ValueError("scene requires scene/start/ticks/seed and at least one observe")
    if not meta["scene"].replace("-", "").replace("_", "").isalnum() or len(meta["scene"]) > 48:
        raise ValueError("scene name")
    if meta["start"] != "boot":
        raise ValueError("only the boot diagnostic start state exists")
    ticks, seed = int(meta["ticks"], 0), int(meta["seed"], 0)
    if not 1 <= ticks <= (900 if hash_schema in (20, 21, 22) else 600) or not 0 <= seed <= 0xffffffff or len(events) > 64:
        raise ValueError("scene limits")
    if any(not 0 <= e[0] < ticks for e in events):
        raise ValueError("input outside window")
    if game and not level:
        raise ValueError("game requires a loaded level")
    if probe in ("reload", "video") and (not level or game):
        raise ValueError(probe + " probe requires a non-game level")
    if place is not None and not game:
        raise ValueError("placement requires game")
    if game and any(x or y for _, _, x, y in events):
        raise ValueError("game scene requires raw action masks and zero axes")
    if hash_schema is not None and not game:
        raise ValueError("hash schema requires game")
    if hash_schema in (20, 21, 22):
        changes = {t: b for t, b, _, _ in events}
        ticks_with_event = sorted(changes.keys() | choices.keys())
        if len(ticks_with_event) > 64 or any(t < 0 or t >= ticks for t in ticks_with_event):
            raise ValueError("ring event exceeds scene limits")
        mask = 0
        records = []
        for t in ticks_with_event:
            mask = changes.get(t, mask)
            choice = choices.get(t, 0)
            if (mask & ~TR2_INPUT_MASK) and mask != 2048:
                raise ValueError("Tomb Raider II raw action bits")
            if mask == 2048 and (choice or changes.get(t + 1, 2048) == 2048 or
                                 choices.get(t + 1, 0) & (TR2_RING_EVENT | TR2_RING_KEY) != TR2_RING_EVENT):
                raise ValueError("inventory marker needs one main-ring choice on the next update")
            if choice & TR2_RING_KEY and not mask & 128:
                raise ValueError("key confirmation needs Action")
            records.append(struct.pack(">III", t, mask, choice))
        version = 4
        body = b"".join(records)
        event_count = len(records)
    else:
        if choices:
            raise ValueError("ring choices require a Tomb Raider II route")
        event_count = len(events)
        if any(buttons > 65535 for _, buttons, _, _ in events):
            raise ValueError("pad range")
        version = 3
        body = b"".join(struct.pack(">IHbbI", t, b, x, y, 0) for t, b, x, y in events)
    hash_schema = hash_schema or 2
    for key, length in (("expect-state", 8), ("expect-shot", 64)):
        if key not in meta:
            continue
        if len(meta[key]) != length or any(c not in "0123456789abcdef" for c in meta[key]):
            raise ValueError(f"{key}: lowercase hex required")
    settings = struct.pack(">IIIiiiiI", int(game), int(place is not None),
                           *(place or (0, 0, 0, 0, 0)), hash_schema if hash_schema != 2 else 0)
    payload = level.encode("ascii").ljust(32, b"\0") + settings + body
    data = struct.pack(">4s7I", b"TR6R", version, 32+len(payload), ticks, seed,
                       event_count, zlib.crc32(payload), probes[probe]) + payload
    meta["observables"] = observables
    meta["level"] = level or None
    meta["game"] = game
    meta["place"] = place
    meta["hash_schema"] = hash_schema
    return meta, data


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scene", nargs="?")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--header", action="store_true")
    ns = ap.parse_args()
    try:
        data = parse(ns.scene)[1] if ns.scene else b""
        if ns.header:
            data = ("/* generated; do not edit */\n#include <stdint.h>\n"
                    "static const uint8_t tr64_route_blob[] = {" +
                    ",".join(str(b) for b in (data or b"\0")) + "};\n"
                    f"static const uint32_t tr64_route_blob_size = {len(data)}u;\n").encode()
        out = pathlib.Path(ns.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        if not out.exists() or out.read_bytes() != data:
            out.write_bytes(data)
    except (OSError, ValueError, UnicodeError) as e:
        ap.exit(1, f"routec: {e}\n")

if __name__ == "__main__":
    main()
