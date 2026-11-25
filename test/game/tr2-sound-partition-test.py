#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Variant Great Wall packs must carry the sound section exactly as the cook wrote it.

usage: PYTHONPATH=tools/cook tr2-sound-partition-test.py WALL_PACK

`wall_sections` (tr2-item-actions-test.py) re-derives the sound section's block
partition from its SN64 sample table. This test rebuilds WALL_PACK from its own
decoded sections with it and requires every sound entry of the rebuilt pack to equal
the shipped one (kind, destination, stored size, raw size, alignment, codec, CRC,
SHA-256), so a change of the cooker's cut rule cannot go unnoticed.
Control: a rebuild cut every 4096 bytes without the sample boundaries must differ."""
from pathlib import Path
import runpy
import struct
import sys

import n64_pack

helpers = runpy.run_path(str(Path(__file__).with_name('tr2-item-actions-test.py')))
wall_sections, SOUND = helpers['wall_sections'], helpers['SOUND']


def sound_entries(blob):
    count, = struct.unpack_from('>I', blob, 12)
    entries = []
    for i in range(count):
        _, offset, size, _, _ = n64_pack.LEVEL.unpack_from(blob, 32 + i * 48)
        entries += [row for row in (n64_pack.ENTRY.unpack_from(blob, offset + 64 * j) for j in range(size))
                    if row[0] == SOUND]
    return [(r[0], r[1], r[3], r[4], r[5], r[6], r[7], r[8]) for r in entries]


def main():
    blob = Path(sys.argv[1]).read_bytes()
    tables = n64_pack.inspect(blob)['wall']
    shipped = sound_entries(blob)
    rebuilt = sound_entries(n64_pack.build({'wall': wall_sections(tables)}, games={'wall': 2}))
    if not shipped or rebuilt != shipped:
        print(f'FAIL sound partition: {len(shipped)} shipped entries, {len(rebuilt)} rebuilt, '
              f'equal={rebuilt == shipped}')
        return 1
    print(f'PASS sound partition: all {len(shipped)} sound entries of the rebuilt pack equal the shipped ones')
    section = wall_sections(tables)[-1]
    uniform = n64_pack.Section(SOUND, section.data, header_bytes=section.header_bytes,
                               chunk_bytes=section.chunk_bytes, compression='none')
    other = [s for s in wall_sections(tables) if s.kind != SOUND] + [uniform]
    if sound_entries(n64_pack.build({'wall': other}, games={'wall': 2})) == shipped:
        print('FAIL control: a uniform 4096-byte partition compared equal')
        return 1
    print('PASS control: a uniform 4096-byte partition is detected')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
