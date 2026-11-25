#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Lava floors (floor function 5), oracle against port, schema 4 (oracle host.patch hunk 21).

The reference left the lava floor entry as a TODO (src/fixed/room.h `checkTrigger`), so Lara walked over lava in LEVEL10A
(225 sectors), 10B (386) and 10C (200). With hunk 21 she is burnt when she stands on the floor of such a sector
(docs/specs/wall/objects-missing.md 15.3, triggers-and-effects.md 2.1; lara-and-enemies.md 1.4): hit points -1, cause
lava, the burning state set, and the death follows as for any hit points of 0 or below.

Per level: a lava sector with a flat, standable, non-lava neighbour is found in the cooked floor data; Lara is placed
on the neighbour facing the lava and runs onto it (lava case), and, from the same cell, onto another non-lava
neighbour (control case: nothing happens). Both compared tick for tick, and the lava run on the oracle without hunk 21
(`--old-oracle`), where Lara must survive it.

Run with PYTHONPATH=tools/cook:test/game:
  lava.py --executable HOST --oracle ORACLE --old-oracle OLD --out DIR
"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from directed import FORWARD, NONE

LEVELS = ('level10a', 'level10b', 'level10c')
RUN = [FORWARD] * 40 + [NONE] * 60
CLEAR = 768       # headroom the placement needs (the default 1,536 rejects the low lava caves)


def has_lava(words, i):
    """Does the floor-data entry chain at `i` hold a lava entry (function 5), anywhere in the chain?"""
    while i < len(words):
        w = words[i]
        t, end = w & 31, w & 0x8000
        i += 1
        if t == 5:
            return True
        if t in (1, 2, 3):
            i += 1
        elif t == 4:
            flags_word = words[i]
            i += 1
            if ((w >> 8) & 31) in (2, 3, 4):       # switch, key, pickup: the controlling item
                i += 1
            while i < len(words):
                a = words[i]
                i += 1
                last = a & 0x8000
                if (a >> 10) & 15 == 1:             # camera: one more word, which carries the end bit
                    last = words[i] & 0x8000
                    i += 1
                if last:
                    break
        if end:
            return False
    return False


def lava_cells(env):
    """{(room, sx, sz)}: sectors whose floor-data chain holds a lava entry (function 5)."""
    words = env.level.data['floor_data']
    found = set()
    for ri, room in enumerate(env.level.rooms):
        for index, sector in enumerate(room.sectors):
            fi = sector['floor_index']
            if fi and fi < len(words) and has_lava(words, fi):
                found.add((ri, index // room.z_sectors, index % room.z_sectors))
    return found


def centre(env, ri, sx, sz):
    room = env.level.rooms[ri]
    return room.info['x'] + sx * 1024 + 512, room.info['z'] + sz * 1024 + 512


def safe(env, start):
    """Lara placed here stays alive for three ticks (lava entries need not be a sector's first entry, so the cooked
    set `lava` under-reports; the oracle decides)."""
    try:
        rows = d.probe(env, start, [NONE] * 3, want='all')
    except ValueError:
        return False
    return all(r['lara'] is not None and r['lara']['health'] == 1000 for r in rows)


def cases(env, lava, limit=12):
    """(lava start, control start) pairs: the same cell, running toward lava and toward a non-lava neighbour."""
    out = []
    for ri, sx, sz in sorted(lava):
        lx, lz = centre(env, ri, sx, sz)
        g = d.ground(env, ri, lx, lz, None, clear=CLEAR)
        if g is None:
            continue
        room = env.level.rooms[ri]

        def is_lava(r, x, z):
            rr = env.level.rooms[r]
            return (r, (x - rr.info['x']) >> 10, (z - rr.info['z']) >> 10) in lava
        for dx, dz, angle in d.DIRS:
            sx0, sz0 = lx - dx * 1024, lz - dz * 1024
            h = d.ground(env, ri, sx0, sz0, g[1], 1100, clear=CLEAR)
            if h is None or h[1] > g[1] or is_lava(h[0], sx0, sz0) or not safe(env, (h[0], sx0, h[1], sz0, angle)):
                continue
            for cx, cz, cangle in d.DIRS:
                if (cx, cz) == (dx, dz):
                    continue
                tx, tz = sx0 + cx * 1024, sz0 + cz * 1024
                t = d.ground(env, ri, tx, tz, h[1], 300, clear=CLEAR)
                if t is not None and not is_lava(t[0], tx, tz) and safe(env, (t[0], tx, t[1], tz, cangle)):
                    out.append(dict(room=ri, cell=(sx, sz), lava=(h[0], sx0, h[1], sz0, angle),
                                    control=(h[0], sx0, h[1], sz0, cangle)))
                    break
            if len(out) and out[-1]['cell'] == (sx, sz) and out[-1]['room'] == ri:
                break
        if len(out) >= limit:
            break
    return out


class Watch:
    def __init__(self):
        self.health = []
        self.burning = []
        self.cause = []

    def __call__(self, row):
        lara = row['lara']
        if lara is not None:
            self.health.append(lara['health'])
            self.burning.append(lara.get('hazard', {}).get('flags') if isinstance(lara.get('hazard'), dict) else None)


def run(env, start):
    w = Watch()
    result = d.compare(env, start, RUN, ticks=len(RUN), observe=w)
    return result, w


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--old-oracle', type=Path, default=d.ORACLE)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--levels', default=','.join(LEVELS))
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    report, ok = {}, True
    for name in args.levels.split(','):
        env = d.Env(name, args.executable, oracle=args.oracle)
        old = d.Env(name, args.executable, oracle=args.old_oracle)
        lava = lava_cells(env)
        row = dict(lava_sectors=len(lava))
        done = None
        for c in cases(env, lava):
            # lava entries need not be a sector's first floor-data entry, so a neighbour that is not in `lava` can be one:
            # the control must leave Lara alive, and she must be alive when she is placed in the lava case
            ctl, wc = run(env, c['control'])
            if ctl.get('status') != 'equal' or not wc.health or wc.health[-1] != 1000:
                continue
            res, w = run(env, c['lava'])
            if res.get('status') != 'equal' or not w.health or w.health[-1] > 0 or w.health[0] != 1000 or w.health[1] != 1000:
                row.setdefault('rejected', []).append(dict(case=c['cell'], status=res.get('status'), health=w.health[-1:] ))
                continue
            # negative control: without hunk 21 Lara survives the same run (the other differences between the two oracles,
            # LEVEL10A's active lava emitters spawning particles from tick 1, are not what this checks)
            survived = [r['lara']['health'] for r in d.probe(old, c['lava'], RUN, want='all') if r['lara']]
            neg = dict(status='survives' if survived and min(survived) == 1000 else 'dies', min_health=min(survived or [0]))
            done = dict(case=c, lava=dict(status=res['status'], health_first=w.health[0], health_last=w.health[-1],
                                          first_dead_tick=next(i for i, h in enumerate(w.health) if h <= 0)),
                        control=dict(status=ctl.get('status'), health_last=wc.health[-1] if wc.health else None),
                        negative=neg)
            good = (ctl.get('status') == 'equal' and wc.health and wc.health[-1] == w.health[0]
                    and neg.get('status') == 'survives')
            done['ok'] = bool(good)
            if good:
                break
        row['case'] = done
        ok &= bool(done and done['ok'])
        report[name] = row
        print(name, json.dumps(row, default=str), flush=True)
    (args.out / 'lava.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print('lava', 'PASS' if ok else 'FAIL')
    return int(not ok)


if __name__ == '__main__':
    raise SystemExit(main())
