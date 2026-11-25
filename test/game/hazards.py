#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Does each hazard hurt or kill Lara in the ORACLE? One instance per type, placed starts, oracle only.

`place`: Lara starts on a trigger pad that lists the trap (types 0, 1, 5) or on the item's own cell, stands 60
ticks, runs forward 60, stands 120; `pad-run`: Lara starts 2048 or 3072 units before such a pad, in each of the
four directions, and runs through it for 140 ticks (traps that strike on a crossing). The minimum health over
the scene is reported. Results of the committed run: docs/game-gaps.md section 6d.

  PYTHONPATH=tools/cook:test/game hazards.py ORACLE [place|pad-run]
"""
import os
import sys
sys.dont_write_bytecode = True
os.environ['TR64_TRACE_SCHEMA'] = '4'
import directed as d
import pickups
from directed import FORWARD, NONE

PLACE = {'spikes': (37, ['level3b', 'level5', 'level6']), 'boulder': (38, ['level3b', 'level4', 'level10a']),
         'slam': (42, ['level6', 'level7b', 'level10b']), 'sword': (43, ['level4']),
         'swing_blade': (36, ['level2', 'level7b', 'level10c']), 'dart_emitter': (40, ['level1', 'level2']),
         'flame_emitter': (179, ['level6', 'level10c']), 'lava_wedge': (180, ['level10b', 'level10c'])}
PAD_RUN = {'falling_ceiling': ('level3b', (53, 54)), 'thor_hammer': ('level4', (44,)),
           'lightning': ('level4', (46,))}


def min_health(rows):
    return min(r['lara']['health'] for r in rows if r['lara'])


def place(oracle):
    for name, (typ, levels) in PLACE.items():
        best = None
        for level in levels:
            env = d.Env(level, '/unused', oracle=oracle)
            for idx, it in enumerate(env.level.data['items']):
                if it['type'] != typ:
                    continue
                starts = []
                for ri, _, tt, acts, x, z in pickups.trigger_sectors(env.level):
                    if tt in (0, 1, 5) and any(a == 0 and arg == idx for a, arg in acts):
                        g = d.ground(env, ri, x, z, None, clear=1024)
                        if g:
                            starts.append(('pad', (g[0], x, g[1], z, 0)))
                g = d.ground(env, it['room'], it['x'], it['z'], None, clear=1024)
                if g:
                    starts.append(('item', (g[0], it['x'], g[1], it['z'], d.yaw(it['angle'] - 32768))))
                for kind, start in starts[:2]:
                    try:
                        rows = d.probe(env, start, [NONE] * 60 + [FORWARD] * 60 + [NONE] * 120, want='all')
                    except ValueError:
                        continue
                    res = dict(level=level, item=idx, place=kind, min_health=min_health(rows))
                    if best is None or res['min_health'] < best['min_health']:
                        best = res
                if best and best['min_health'] < 1000:
                    break
            if best and best['min_health'] < 1000:
                break
        print('place', name, best, flush=True)


def pad_run(oracle):
    for name, (level, types) in PAD_RUN.items():
        env = d.Env(level, '/unused', oracle=oracle)
        ids = [i for i, it in enumerate(env.level.data['items']) if it['type'] in types]
        best = None
        for ri, _, tt, acts, x, z in pickups.trigger_sectors(env.level):
            if not any(a == 0 and arg in ids for a, arg in acts):
                continue
            g = d.ground(env, ri, x, z, None, clear=1024)
            if not g:
                continue
            for dx, dz, ang in d.DIRS:
                for back in (2048, 3072):
                    sx, sz = x - dx * back, z - dz * back
                    h = d.ground(env, ri, sx, sz, g[1], 300, clear=1024)
                    if not h:
                        continue
                    try:
                        rows = d.probe(env, (h[0], sx, h[1], sz, ang), [FORWARD] * 140 + [NONE] * 160, want='all')
                    except ValueError:
                        continue
                    mh = min_health(rows)
                    if best is None or mh < best[0]:
                        best = (mh, ri, tt, (sx, sz), ang, back)
        print('pad-run', name, level, ids[:3], 'best (min health, room, trigger type, start, angle, distance)', best,
              flush=True)


if __name__ == '__main__':
    oracle, mode = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else 'all')
    if mode in ('place', 'all'):
        place(oracle)
    if mode in ('pad-run', 'all'):
        pad_run(oracle)
