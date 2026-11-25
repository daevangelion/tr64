#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Append a video catalog (n64_video.py manifest) to an already composed campaign
pack, keeping every existing block, boundary and compression choice.

The base pack is the campaign plus its music (`pc-campaign-spu.pack`); the result is
the shipping pack with the VIDEO_/FILM_ streams. The base is first re-built from its
own decoded sections and must reproduce byte for byte, so no old section is
repartitioned or re-encoded; the output must then decode to the same sections.

  pack_add_video.py --base pc-campaign-spu.pack --video-catalog <dir>/manifest.json \\
    --cache /tmp/tr64/cache --out pc-campaign-video.pack
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import n64_pack as pack
import n64_music
import n64_snd
from pack_level import add_video_catalog

RAW_KINDS = (n64_snd.KIND, n64_music.KIND)
CART_BYTES = 64 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', type=Path, required=True, help='composed campaign pack without videos')
    parser.add_argument('--video-catalog', type=Path, required=True, help='n64_video.py manifest.json')
    parser.add_argument('--cache', type=Path, required=True, help='version-keyed host compressor cache')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    base = args.base.read_bytes()
    decoded = pack.inspect(base)
    codec = pack.compressor(args.cache)
    entries, games = {}, {}
    count = struct.unpack_from('>I', base, 12)[0]
    for i in range(count):
        name, offset, size, _, game = pack.LEVEL.unpack_from(base, 32 + i * 48)
        name = name.split(b'\0', 1)[0].decode()
        if game:
            games[name] = game
        groups = {}
        for j in range(size):
            row = pack.ENTRY.unpack_from(base, offset + 64 * j)
            groups.setdefault(row[0], []).append(row)
        rows = []
        for kind, wire in sorted(groups.items()):
            wire.sort(key=lambda row: row[1])
            raw = kind in RAW_KINDS and all(row[6] == 0 and row[3] == row[4] for row in wire)
            rows.append(pack.Section(kind, decoded[name][kind], alignment=wire[0][5],
                                     compression='none' if raw else 'auto',
                                     boundaries=tuple(row[1] for row in wire[1:])))
        entries[name] = rows
    if pack.build(entries, codec, games=games) != base:
        raise SystemExit('base pack does not reproduce byte for byte')
    if any(name.startswith(('VIDEO_', 'FILM_')) for name in entries):
        raise SystemExit('base pack already holds videos')
    identity = add_video_catalog(entries, args.video_catalog)
    blob = pack.build(entries, codec, games=games)
    after = pack.inspect(blob)
    for name, kinds in decoded.items():
        if after[name] != kinds:
            raise SystemExit('base payload changed: ' + name)
    for name, rows in entries.items():
        if any(after[name][row.kind] != row.data for row in rows):
            raise SystemExit('independent decode mismatch: ' + name)
    if len(blob) >= CART_BYTES:
        raise SystemExit('pack does not fit a 64 MiB cart')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    report = dict(pack_bytes=len(blob), pack_sha256=sha(blob), base_bytes=len(base), base_sha256=sha(base),
                  entries=len(entries), video_catalog_sha256=identity,
                  video_streams=sum(1 for n in entries if n.startswith('VIDEO_')),
                  video_bytes=sum(len(r.data) for n, rows in entries.items() if n.startswith(('VIDEO_', 'FILM_')) for r in rows),
                  compressor_sha256=codec.identity, headroom_to_64MiB=CART_BYTES - len(blob))
    args.out.with_suffix('.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
