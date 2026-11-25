#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check climb marks and trigger dispatch with admitted floor-program suffixes."""
import argparse
import json
import os
from pathlib import Path
import runpy
import re
import struct
import subprocess
import tempfile

import n64_core
import n64_pack

wall_sections = runpy.run_path(str(Path(__file__).with_name('tr2-item-actions-test.py')))['wall_sections']


def test(host, wall, scratch):
    base = n64_pack.inspect(wall.read_bytes())['wall']
    ids = {table.name: i for i, table in enumerate(n64_core.TABLES, 1)}
    floor, sectors = ids['floor_data'], ids['sectors']
    header = n64_core.PREFIX.size
    width = struct.calcsize('>' + n64_core.TABLES[sectors - 1].fmt)
    start = (len(base[floor]) - header) // 2
    spawn_sector = 3443  # WALL's ordinary tick-one collision sector, facing +Z.
    mark, wrong_mark = 0x0106, 0x0206
    end = 0x8000
    # Unhandled floor-trigger opcodes are counted as inert; observing opcode 11
    # proves the trigger suffix reached the dispatcher rather than merely the mark scan.
    trigger = (end | 4, 0, end | (11 << 10))
    cases = (
        ('matching mark', (end | mark,), 1, False),
        ('wrong face', (end | wrong_mark,), 0, False),
        ('mark before terminal no-op', (mark, end), 1, False),
        ('portal payload', (mark, end | 1, 0), 1, False),
        ('floor slope payload', (mark, end | 2, 0), 1, False),
        ('ceiling slope payload', (mark, end | 3, 0), 1, False),
        ('mark before trigger', (mark, *trigger), 1, True),
        ('review floor sequence', (mark, 5, end | 4, 0, end), 1, False),
        ('mark before lava before trigger', (mark, 5, *trigger), 1, True),
        ('mark before terminal lava', (mark, end | 5), 1, False),
        ('lava before mark before trigger', (5, mark, *trigger), 1, True),
        ('matching second mark', (wrong_mark, end | mark), 1, False),
        ('payload is not a mark', (2, mark, end | wrong_mark), 0, False),
    )
    with tempfile.TemporaryDirectory(prefix='tr2-climb-mark-', dir=scratch) as tmp:
        root = Path(tmp)
        script, pack, trace = (root / name for name in ('input', 'pack', 'trace'))
        script.write_text('0\n')
        for name, words, want_mark, want_trigger in cases:
            data = bytearray(base[floor]) + struct.pack('>' + 'H' * len(words), *words)
            sector_data = bytearray(base[sectors])
            struct.pack_into('>I', data, 8, start + len(words))
            struct.pack_into('>I', data, 24, len(data))
            struct.pack_into('>H', sector_data, header + width * spawn_sector, start)
            replace = {floor: data, sectors: sector_data}
            native = n64_pack.build({'wall': wall_sections(base | replace)}, games={'wall': 2})
            n64_pack.inspect(native)
            pack.write_bytes(native)
            proc = subprocess.run(['rtk', str(host), '--game-tr2', str(pack), str(script), str(trace)],
                                  capture_output=True, text=True, check=True, timeout=30)
            _, tick, _ = [json.loads(line) for line in trace.read_bytes().splitlines()]
            dispatched = bool(re.search(r'tr2-inert-floor-action: opcode=11 count=[1-9]', proc.stdout))
            got = tick['lara']['climbing'], dispatched
            want = want_mark, want_trigger
            if got != want:
                raise RuntimeError(f'{name}: (mark, trigger) was {got}, expected {want}')
            print(f'PASS {name}: mark={got[0]} trigger={got[1]}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('wall', type=Path)
    args = parser.parse_args()
    test(args.host, args.wall, os.environ['TMPDIR'])
