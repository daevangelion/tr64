#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Trigger-derived campaign inputs: `campaign.py --triggers DIR` reads `<level>-*.masks`.

Every floor-data trigger of a level (types ACTIVATE, PAD, OBJECT, ANTIPAD, COMBAT; no switch,
key or pickup, which need an item interaction) names the items it activates. For each activated
item *type* up to `--per-type` trigger sectors are chosen, spread over the level, and one input
is written per sector: Lara is placed on standable ground 3072, 2048 or 1024 units before the
sector, facing it, runs across it, draws her weapons and fires into whatever the trigger
woke, then idles until tick 600. Names are `<level>-type<ITEM_TYPE>-<n>.masks`.

The inputs hold only the placement and masks (no give/item directive): play-reached evidence.
Run with PYTHONPATH=tools/cook:test/game. Writes only the small input files.
"""
import argparse
import collections
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
import switches
from directed import ACTION, FORWARD, NONE, WEAPON

TICKS = 600
TRIGGER_TYPES = (0, 1, 5, 6, 7)
SCRIPT = [(FORWARD, 100), (NONE, 10), (WEAPON, 1), (NONE, 30), (ACTION, 300), (NONE, 1)]


def masks():
    out = [m for mask, n in SCRIPT for m in [mask] * n]
    return (out + [NONE] * TICKS)[:TICKS]


def sectors(env):
    """{item type: [(room, sector index, trigger type)]} in room order."""
    items = env.level.data['items']
    found = collections.defaultdict(list)
    for ri, index, ttype, acts in switches.triggers(env):
        if ttype not in TRIGGER_TYPES:
            continue
        for kind in sorted({items[a]['type'] for a in acts if a < len(items)}):
            found[kind].append((ri, index, ttype))
    return found


def start_for(env, ri, index):
    room = env.level.rooms[ri]
    cx = room.info['x'] + (index // room.z_sectors) * 1024 + 512
    cz = room.info['z'] + (index % room.z_sectors) * 1024 + 512
    g = d.ground(env, ri, cx, cz, None, clear=1024)
    if g is None:
        return None
    for distance in (3072, 2048, 1024):
        for dx, dz, angle in d.DIRS:
            sx, sz = cx - dx * distance, cz - dz * distance
            h = d.ground(env, ri, sx, sz, g[1], 300, clear=1024)
            if h is not None:
                return (h[0], sx, h[1], sz, angle)
    return None


def spread(rows, count):
    if len(rows) <= count:
        return rows
    step = (len(rows) - 1) / (count - 1) if count > 1 else 0
    return [rows[round(i * step)] for i in range(count)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--levels', default='level1,level2,level3a,level3b,level4,level5,level6,level7a,level7b,level8a,level8b,level8c,level10a,level10b,level10c')
    p.add_argument('--per-type', type=int, default=3)
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    total = 0
    for name in args.levels.split(','):
        env = d.Env(name, '/nonexistent', d.ORACLE)
        counts = {}
        for kind, rows in sorted(sectors(env).items()):
            usable = [(r, start) for r in rows if (start := start_for(env, r[0], r[1])) is not None]
            for n, (row, start) in enumerate(spread(usable, args.per_type)):
                (args.out / f'{name}-type{kind}-{n}.masks').write_text(d.masks_text(start, masks()))
                counts[kind] = counts.get(kind, 0) + 1
                total += 1
        print(name, counts, flush=True)
    print('inputs', total)


if __name__ == '__main__':
    main()
