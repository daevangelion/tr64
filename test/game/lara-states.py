#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Which Lara controller states have any animation in the Lara models of the PC levels.

A state can only be entered through an animation whose `state` field names it (or
a state-change record dispatching to it from such an animation). The animations of
Lara's own models (types 0-5, each running to the next model's first animation) are
scanned in every gameplay level. A state with no record in any level is not
reachable by any input in Tomb Raider 1, whatever the controller code says.
Run with PYTHONPATH=tools/cook from the repository root.
"""
import bisect
import re
import sys
sys.dont_write_bytecode = True
import src_pc

LEVELS = 'gym level1 level2 level3a level3b level4 level5 level6 level7a level7b level8a level8b level8c level10a level10b level10c'.split()


def main():
    names = dict((int(v), k) for k, v in re.findall(r'Lara_STATE_([A-Z_0-9]+) = (\d+)', open('game/common/internal.h').read()))
    seen = {}
    for level in LEVELS:
        lv = src_pc.load(f'/tmp/tr64/data/pc/{level}.phd')
        anims, states = lv.data['animations'], lv.data['states']
        models = sorted(lv.data['models'], key=lambda m: m['animation'])
        starts = [m['animation'] for m in models]
        for i, a in enumerate(anims):
            j = bisect.bisect_right(starts, i) - 1
            if j < 0 or models[j]['type'] not in range(6):
                continue
            seen.setdefault(a['state'], set()).add(level)
            for k in range(a['state_count']):
                seen.setdefault(states[a['state_offset'] + k]['state'], set()).add(level)
    for number in sorted(names):
        where = seen.get(number)
        print(f'{number:3d} {names[number]:14s}', f'{len(where)} levels' if where else 'NO ANIMATION IN ANY LEVEL: unreachable')


if __name__ == '__main__':
    main()
