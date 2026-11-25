#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inventory referenced floor-trigger music, not a reachability claim.

Layout facts: fixed room.h FD trigger walk, OpenLara2333adef; existing source
readers own level parsing. Tutorial remaps28->29 and42->43 are separate rules.
"""
import argparse
import json
from pathlib import Path
import src_pc
import src_psx


def requests(level):
    words = level.data['floor_data']
    rows = set()
    def take(pos):
        if pos >= len(words):
            raise ValueError('truncated floor data')
        return words[pos], pos + 1
    visited = set()
    starts = {sector['floor_index'] for room in level.rooms for sector in room.sectors}
    for start in sorted(starts - {0}):
        pos = start
        while pos not in visited:
            visited.add(pos)
            word, pos = take(pos)
            kind = word & 31
            if kind in (1, 2, 3):
                _, pos = take(pos)
            elif kind == 4:
                _, pos = take(pos)  # trigger timer/mask/once setup
                if (word >> 8) & 127 in (2, 3, 4):
                    _, pos = take(pos)  # switch/key/pickup item
                while True:
                    offset = pos
                    action, pos = take(pos)
                    action_kind, argument = (action >> 10) & 31, action & 1023
                    if action_kind == 8:
                        if argument >= 64:
                            raise ValueError('track outside source track state')
                        rows.add((offset, argument, 'soundtrack'))
                    elif action_kind == 10:
                        rows.add((offset, 13, 'secret'))
                    if action_kind in (1, 12):
                        extra, pos = take(pos)
                        action = (action & 32767) | (extra & 32768)
                    if action & 32768:
                        break
            elif kind not in (0, 5):
                raise ValueError('unknown floor command')
            if word & 32768:
                break
    return [{'word_offset': p, 'track': track, 'kind': kind} for p, track, kind in sorted(rows)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pc', type=Path, default=Path('/tmp/tr64/data/pc'))
    parser.add_argument('--psx', type=Path, default=Path('/tmp/tr64/data/psx/PSXDATA'))
    args = parser.parse_args()
    result = []
    for source, root, suffix, load in [('pc', args.pc, '*.phd', src_pc.load),
                                       ('psx', args.psx, '*.PSX', src_psx.load)]:
        paths = sorted(root.glob(suffix))
        if not paths:
            raise ValueError('missing source level set: ' + str(root))
        for path in paths:
            level = load(path)
            rows = requests(level)
            result.append({'source': source, 'level': level.name, 'source_sha256': level.sha256,
                           'tracks': sorted({row['track'] for row in rows}), 'references': rows})
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
