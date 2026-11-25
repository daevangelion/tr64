#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Switches, underwater switches, and trigger sectors that fire traps and doors.

switch   every SWITCH item (55): Lara on its front side at three distances, pressing
         ACTION once, twice, held, and after a wait (timed switches), so the doors,
         trapdoors, bridges, boulders and ceilings its trigger fires run too.
uwswitch every SWITCH_WATER item (56): Lara placed in the water in front, settling to
         UW_TREAD, then ACTION.
pad      every sector whose floor data carries a trigger that activates a trap or
         door object (boulder, falling ceiling, Thor's hammer, lightning, trapdoor,
         trap floor, drawbridge, door, swing blade, dart emitter, spikes, flames),
         stood on and run through from four sides.
Every script is compared with the oracle record for record (schema 3); no inventory
is supplied, so all of it is play-reached evidence.
"""
import argparse
import collections
import concurrent.futures as futures
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
import items
from directed import ACTION, FORWARD, NONE, WEAPON

SWITCH, SWITCH_WATER = 55, 56
TRAPS = {35: 'trap_floor', 36: 'swing_blade', 37: 'spikes', 38: 'boulder', 39: 'dart', 40: 'dart_emitter', 41: 'drawbridge',
         42: 'slam', 43: 'sword', 44: 'hammer_handle', 45: 'hammer_block', 46: 'lightning', 53: 'ceiling', 54: 'ceiling2',
         57: 'door', 58: 'door', 59: 'door', 60: 'door', 61: 'door', 62: 'door', 63: 'door', 64: 'door',
         65: 'trapdoor', 66: 'trapdoor', 67: 'trapdoor', 178: 'flame_emitter', 177: 'lava_emitter', 180: 'trap_lava',
         179: 'flame_emitter'}

SWITCH_PATTERNS = {
    'once': [(ACTION, 60), (NONE, 150)],
    'twice': [(ACTION, 40), (NONE, 120), (ACTION, 40), (NONE, 160)],
    'held': [(ACTION, 300)],
    'late': [(NONE, 90), (ACTION, 4), (NONE, 300)],
}
UW_PATTERNS = {
    'settle-action': [(NONE, 50), (ACTION, 60), (NONE, 150)],
    'action-twice': [(NONE, 50), (ACTION, 40), (NONE, 80), (ACTION, 40), (NONE, 100)],
}


def expand(schedule):
    return [m for mask, n in schedule for m in [mask] * n]


def triggers(env):
    """Yield (room, sector index, trigger type, [activated item indices]) from the floor data."""
    words = env.level.data['floor_data']
    for ri, room in enumerate(env.level.rooms):
        for index, s in enumerate(room.sectors):
            i = s['floor_index']
            if not i:
                continue
            while True:
                cmd = words[i]
                kind = cmd & 31
                if kind in (1, 2, 3):
                    i += 2
                    if cmd & 0x8000:
                        break
                    continue
                if kind == 4:
                    ttype = (cmd >> 8) & 0x7f
                    j = i + 2
                    acts = []
                    while True:
                        w = words[j]
                        act, arg = (w >> 10) & 15, w & 0x3ff
                        if act == 0:
                            acts.append(arg)
                        if act in (1, 10):
                            j += 1          # camera / flyby carry a second word
                        if w & 0x8000:
                            break
                        j += 1
                    yield ri, index, ttype, acts
                break


def switch_cases(env):
    out = []
    for index, item in env.items(SWITCH):
        for dist, off, start in items.hole_starts(env, item):
            for name, schedule in SWITCH_PATTERNS.items():
                if (off or dist != 420) and name != 'once':
                    continue
                out.append(dict(family='switch', name=name, item=index, start=start, masks=expand(schedule)))
    for index, item in env.items(SWITCH_WATER):
        fx, fz = items.forward(item['angle'] if item['angle'] in (0, 16384, -32768, -16384) else 0)
        for dist in (420, 700):
            x, z = item['x'] + fx * dist, item['z'] + fz * dist
            r = d.resolve(env.level, item['room'], x, z)
            if r is None:
                continue
            ceil, floor = d.height(env.level, r[0], r[1], x, z, True), d.height(env.level, r[0], r[1], x, z)
            if ceil is None or floor is None or not ceil + 200 < item['y'] < floor - 100:
                continue
            for name, schedule in UW_PATTERNS.items():
                out.append(dict(family='uwswitch', name=name, item=index, start=(r[0], x, item['y'], z, d.yaw(item['angle'] + 32768)),
                                masks=expand(schedule)))
    return out


def pad_cases(env, per_target):
    out = []
    taken = collections.Counter()
    for ri, index, ttype, acts in triggers(env):
        kinds = {TRAPS[env.level.data['items'][a]['type']] for a in acts
                 if a < len(env.level.data['items']) and env.level.data['items'][a]['type'] in TRAPS}
        if not kinds or ttype not in (0, 3, 4, 5, 6, 7):
            continue
        key = (ri, tuple(sorted(kinds)))
        if taken[key] >= 1 or sum(taken[k] for k in taken if k[1] == key[1]) >= per_target:
            continue
        taken[key] += 1
        room = env.level.rooms[ri]
        cx = room.info['x'] + (index // room.z_sectors) * 1024 + 512
        cz = room.info['z'] + (index % room.z_sectors) * 1024 + 512
        g = d.ground(env, ri, cx, cz, None, clear=1024)
        if g is None:
            continue
        out.append(dict(family='pad', name='on:' + ','.join(sorted(kinds)), ttype=ttype, start=(g[0], cx, g[1], cz, 0),
                        masks=expand([(NONE, 100)])))
        out.append(dict(family='pad', name='on-weapon:' + ','.join(sorted(kinds)), ttype=ttype, start=(g[0], cx, g[1], cz, 0),
                        masks=expand([(WEAPON, 1), (NONE, 30), (ACTION, 60), (NONE, 80)])))
        for dx, dz, angle in d.DIRS:
            sx, sz = cx - dx * 2048, cz - dz * 2048
            h = d.ground(env, ri, sx, sz, g[1], 300, clear=1024)
            if h is None:
                continue
            out.append(dict(family='pad', name='run:' + ','.join(sorted(kinds)), ttype=ttype, start=(h[0], sx, h[1], sz, angle),
                            masks=expand([(FORWARD, 120), (NONE, 60)])))
    return out


def execute(env, case):
    seen = d.Observer()
    try:
        result = d.compare(env, case['start'], case['masks'], ticks=len(case['masks']), observe=seen)
    except ValueError as error:
        result = dict(status='error', why=str(error)[:200])
    result['lara_states'] = sorted(seen.lara_states)
    result['item_states'] = sorted(t for t, _ in seen.item_states if t in TRAPS or t in (55, 56))
    result['switch_states'] = sorted(st for t, st in seen.item_states if t in (55, 56))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--levels', default='level1,level2,level3a,level3b,level4,level5,level6,level7a,level7b,level8a,level8b,level8c,level10a,level10b,level10c')
    p.add_argument('--families', default='switch,uwswitch,pad')
    p.add_argument('--pads-per-kind', type=int, default=6)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--jobs', type=int, default=3)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    families = set(args.families.split(','))
    report = dict(executable_sha256=d.sha(args.executable), oracle_sha256=d.sha(args.oracle), levels={})
    for name in args.levels.split(','):
        env = d.Env(name, args.executable, args.oracle)
        cases = []
        if families & {'switch', 'uwswitch'}:
            cases += [c for c in switch_cases(env) if c['family'] in families]
        if 'pad' in families:
            cases += pad_cases(env, args.pads_per_kind)
        if not cases:
            continue
        row = dict(cases=len(cases), status=collections.Counter(), by_family=collections.defaultdict(collections.Counter),
                   switched=collections.Counter(), trap_types=collections.Counter(), lara=collections.Counter(), bad=[])
        with futures.ThreadPoolExecutor(args.jobs) as pool:
            for case, res in zip(cases, pool.map(lambda c: execute(env, c), cases)):
                row['status'][res['status']] += 1
                row['by_family'][case['family']][res['status']] += 1
                for t in res['item_states']:
                    row['trap_types'][t] += 1
                for st in res['lara_states']:
                    if st in (40, 41):
                        row['lara'][f"{case['family']}:state{st}"] += 1
                if res['status'] not in ('equal', 'invalid'):
                    row['bad'].append(dict(family=case['family'], name=case['name'], start=case['start'],
                                           **{k: v for k, v in res.items() if k not in ('lara_states', 'item_states', 'switch_states')}))
        for key in ('status', 'trap_types', 'lara', 'switched'):
            row[key] = dict(row[key])
        row['by_family'] = {k: dict(v) for k, v in row['by_family'].items()}
        report['levels'][name] = row
        print(name, json.dumps({k: v for k, v in row.items() if k != 'bad'}), flush=True)
        for bad in row['bad'][:6]:
            print('  ', json.dumps(bad), flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(r['bad'] for r in report['levels'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
