#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run repaired-integrity packs through native TR2 gameplay admission."""
import argparse
import os
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile

import n64_core
import n64_pack

wall_sections = runpy.run_path(str(Path(__file__).with_name('tr2-item-actions-test.py')))['wall_sections']


def kind(name):
    return next(i for i, table in enumerate(n64_core.TABLES, 1) if table.name == name)


def overlapping_sectors(room_data):
    row = struct.Struct('>' + n64_core.TABLES[0].fmt)
    rooms = tuple(row.iter_unpack(room_data[n64_core.PREFIX.size:]))
    fields = n64_core.TABLES[0].fields
    at = {key: fields.index(key) for key in ('x', 'z', 'x_sectors', 'z_sectors', 'sector_start')}
    for a, first in enumerate(rooms):
        for b in range(a + 1, len(rooms)):
            second = rooms[b]
            corners = tuple(max(first[at[k]], second[at[k]]) + 1024 for k in ('x', 'z'))
            overlaps = []
            for coords in (corners, (corners[0] + 1024, corners[1])):
                tiles = []
                for r in (first, second):
                    x, z = ((value - r[at[k]]) // 1024 for value, k in zip(coords, ('x', 'z')))
                    nx, nz = r[at['x_sectors']], r[at['z_sectors']]
                    if not (0 < x < nx - 1 and 0 < z < nz - 1):
                        break
                    tiles.append(r[at['sector_start']] + x * nz + z)
                if len(tiles) == 2 and tiles[0] != tiles[1]:
                    overlaps.append(tiles)
            if len(overlaps) == 2:
                return a, b, *overlaps
    raise RuntimeError('no overlapping interior sectors found')


def control(exe, wall, temp):
    base = n64_pack.inspect(wall.read_bytes())['wall']
    floor, sectors = kind('floor_data'), kind('sectors')
    n = n64_core.PREFIX.unpack_from(base[floor])[2]
    item_count = n64_core.PREFIX.unpack_from(base[kind('items')])[2]
    cameras = kind('cameras')
    models = kind('models')
    camera_count = n64_core.PREFIX.unpack_from(base[cameras])[2]
    room_data = base[kind('rooms')]
    a, b, (sa, sb), (_, sb_other) = overlapping_sectors(room_data)
    row = n64_core.PREFIX.size
    sector_width = struct.calcsize('>' + n64_core.TABLES[sectors - 1].fmt)
    assert struct.unpack_from('>I', room_data, row + 36)[0] == 0
    cases = (
        ('valid', ((0, (0x8004, 0, 0x8000)),), (), None),
        ('disjoint-room-links', ((sa, (0x8001, b)), (sb_other, (0x8001, a))), (), None),
        ('span', ((0, (0x8004,)),), (), 'unterminated/truncated stream'),
        ('terminator', ((0, (0x8004, 0, 0)),), (), 'unterminated action stream'),
        ('camera', ((0, (0x8004, 0, 1024 | camera_count, 0x8000)),), (), 'trigger camera out of range'),
        ('item', ((0, (0x8004, 0, 0x8000 | (6 << 10) | item_count)),), (), 'trigger item out of range'),
        # Unlike execution, renewal reads out to the action word's end bit.
        ('renewal-action-end', ((0, (0x8004, 0, 0x8400, 0x8000)),), (), None),
        ('camera-bit14', ((0, (0x8004, 0, 0xc402, 0x8000)),), (), None),
        ('renewal-suffix', ((0, (0x8004, 0, 0x0400, 0x8000, 0xfd00)),), (), None),
        ('renewal-end', ((0, (0x8004, 0, 0x0400, 0x8000)),), (), 'unterminated camera renewal stream'),
        ('renewal-extra', ((0, (0x8004, 0, 0x0400, 0x8000, 0x8400)),), (), 'unterminated/truncated stream'),
        ('renewal-camera', ((0, (0x8004, 0, 0x0400, 0x8000, 0x8400 | camera_count, 0)),), (),
         'trigger camera out of range'),
        ('renewal-target', ((0, (0x8004, 0, 0x0400, 0x8000, 0x9800 | item_count)),), (),
         'trigger item out of range'),
        ('fixed-camera-room', ((0, (0x8004, 0, 0x8400, 0x8000)),), (), 'fixed camera room out of range'),
        ('self-portal', ((0, (0x8001, 0)),), (), 'horizontal room cycle'),
        ('two-room-portal', ((sa, (0x8001, b)), (sb, (0x8001, a))), (), 'horizontal room cycle'),
        ('above-cycle', (), ((sa, 'room_above', b), (sb, 'room_above', a)), 'room-above cycle'),
        ('below-cycle', (), ((sa, 'room_below', b), (sb, 'room_below', a)), 'room-below cycle'),
        ('model-first-outside-gameplay', (), (), 'unsupported Tomb Raider II gameplay model type 265'),
        ('model-last-native-type', (), (), 'unsupported Tomb Raider II gameplay model type 65535'),
    )
    with tempfile.TemporaryDirectory(prefix='tr2-floors-', dir=temp) as scratch:
        root = Path(scratch)
        masks = root / 'input.txt'
        masks.write_text('0\n', encoding='ascii')
        for name, floor_edits, vertical_edits, error in cases:
            program = bytearray(base[floor])
            refs = bytearray(base[sectors])
            for s, words in floor_edits:
                struct.pack_into('>H', refs, row + sector_width * s, n + (len(program) - len(base[floor])) // 2)
                program += struct.pack('>' + 'H' * len(words), *words)
            struct.pack_into('>I', program, 8, (len(program) - row) // 2)
            struct.pack_into('>I', program, 24, len(program))
            for s, field, target in vertical_edits:
                fields = n64_core.TABLES[sectors - 1].fields
                # The two single-byte room references follow two 16-bit words.
                offset = 4 + fields.index(field) - fields.index('room_below')
                refs[row + sector_width * s + offset] = target
            changed = {floor: bytes(program), sectors: bytes(refs)}
            if name == 'fixed-camera-room':
                views = bytearray(base[cameras])
                struct.pack_into('>h', views, row + 12, -1)  # After the three 32-bit coordinates.
                changed[cameras] = bytes(views)
            if name.startswith('model-'):
                rows = bytearray(base[models])
                struct.pack_into('>I', rows, row, int(error.split()[-1]))
                changed[models] = bytes(rows)
            blob = n64_pack.build({'wall': wall_sections(base | changed)}, games={'wall': 2})
            n64_pack.inspect(blob)  # Verify rebuilt CRCs and hashes before the native run.
            candidate = root / 'case.pack'
            candidate.write_bytes(blob)
            proc = subprocess.run(['rtk', str(exe), '--game-tr2', str(candidate), str(masks), str(root / 'out')],
                                  capture_output=True, text=True, check=False, timeout=10)
            log = proc.stdout + proc.stderr
            if error is None:
                if proc.returncode or 'tr2-inert:' not in log:
                    raise RuntimeError(f'positive pack did not reach gameplay: {log}')
            elif not proc.returncode or f'core {"models" if name.startswith("model-") else "floor_data"}: {error}' not in log:
                raise RuntimeError(f'{name} did not fail for {error}: {log}')
            print(f'PASS {name}: {error or "native gameplay admitted"}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('wall', type=Path)
    args = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    control(args.executable, args.wall, os.environ['TMPDIR'])
