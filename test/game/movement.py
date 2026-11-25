#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Lara's air and water states that random walking does not reach.

families:
  hang   ledge hangs (ledges.py's enumerated edges) with sideways shimmy: HANG_LEFT, HANG_RIGHT
  fall   long falls from the highest drops of a level: screaming start and stop, the
         landing damage and death, falls into water; swan dives that exceed vSpeed 131
         (FAST_DIVE) before the water or the floor
  restore  Lara_restore: dead from tick 0 (`item LARA 0`, placed state), JUMP+WEAPON, dry and in water
  wadein walking from dry ground into a pool whose top slab is 384..730 deep: WATER_STATE_WADE by play
  wade   shallow water cells (floor under the surface within wading depth): WATER_STATE_WADE
         entered by walking, jumping and turning, then out again
Every script is compared with the oracle record for record (schema 3). Placement is by
position only, never inventory: all of it is play-reached evidence.
"""
import argparse
import collections
import concurrent.futures as futures
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from directed import ACTION, FORWARD, JUMP, WALK, LEFT, RIGHT, BACK, NONE, WEAPON

HANG_LEFT, HANG_RIGHT, FAST_DIVE, HANG, SWAN_DIVE, UW_DIVE, FALL = 30, 31, 53, 10, 52, 35, 9

HANG_PATTERNS = {
    # JUMP alone from standing is a jump-up; ACTION held in the air grabs the ledge (HANG),
    # and FORWARD while hanging would pull up (HANG_UP), so the shimmy holds ACTION only.
    'jump-hang-right': [(32, 5), (160, 40), (ACTION | RIGHT, 70), (ACTION, 10), (ACTION | LEFT, 70), (ACTION | FORWARD, 80)],
    'jump-hang-left': [(32, 5), (160, 40), (ACTION | LEFT, 70), (ACTION, 10), (ACTION | RIGHT, 70), (ACTION | FORWARD, 80)],
    'hang-right-pulses': [(32, 5), (160, 40)] + [(ACTION | RIGHT, 3), (ACTION, 5)] * 12 + [(ACTION | FORWARD, 80)],
    'hang-both': [(32, 5), (160, 40), (ACTION | RIGHT | LEFT, 20), (ACTION | RIGHT, 40), (ACTION | LEFT, 40), (ACTION | BACK, 30)],
    'fwd-hang-right': [(40, 5), (168, 12), (160, 40), (ACTION | RIGHT, 70), (ACTION | LEFT, 70), (ACTION | FORWARD, 80)],
    'walk-hang-right': [(FORWARD | ACTION, 60), (ACTION | RIGHT, 70), (ACTION | LEFT, 70), (ACTION | FORWARD, 100)],
}

FALL_PATTERNS = {
    'drop': [(NONE, 200)],
    'drop-forward': [(FORWARD, 30), (FORWARD | JUMP, 5), (FORWARD, 170)],
    'swan': [(FORWARD, 14), (FORWARD | JUMP, 3), (FORWARD | WALK, 160)],
    'swan-standing': [(JUMP, 3), (FORWARD | WALK, 160)],
    'reach-fall': [(FORWARD | JUMP, 5), (ACTION | FORWARD, 120), (NONE, 80)],
    'roll-fall': [(JUMP, 3), (FORWARD | BACK, 40), (NONE, 160)],
}

WADE_PATTERNS = {
    'forward': [(FORWARD, 90), (LEFT, 20), (FORWARD, 60), (RIGHT, 40), (FORWARD, 60)],
    'walk': [(FORWARD | WALK, 120), (BACK | WALK, 60), (LEFT | WALK, 30)],
    'jump': [(FORWARD, 30), (FORWARD | JUMP, 5), (NONE, 60), (FORWARD, 60), (JUMP, 5), (NONE, 80)],
    'back': [(BACK, 60), (NONE, 20), (BACK | JUMP, 5), (NONE, 80)],
    'roll': [(24, 40), (FORWARD, 60), (JUMP | BACK, 5), (NONE, 80)],
    'action': [(FORWARD, 40), (ACTION, 30), (FORWARD, 60), (LEFT | FORWARD, 60)],
}


def expand(schedule):
    return [m for mask, n in schedule for m in [mask] * n]


def hang_cases(env, level_ledges, limit):
    edges = level_ledges.edges(env.level)
    edges = [e for e in edges if 'water-out' not in e['tags']]
    edges.sort(key=lambda e: ('slope' in e['tags'], e['angle'] != -32768))
    unique = {}
    for e in edges:
        unique.setdefault((e['angle'], tuple(e['tags'])), e)
    chosen = list(unique.values())[:limit]
    out = []
    for e in chosen:
        for distance in (96, 256):
            x = e['x'] + e['dx'] * (512 - distance)
            z = e['z'] + e['dz'] * (512 - distance)
            s = d.sector_at(env.level, e['room'], x, z)
            y = d.height(env.level, e['room'], s, x, z) - (s['room_below'] != 255)
            start = (e['room'], x, y, z, d.yaw(e['angle']))
            for name, schedule in HANG_PATTERNS.items():
                out.append(dict(family='hang', name=name, tags=e['tags'], start=start, masks=expand(schedule)))
    return out


def drops(env):
    """Sectors ranked by the height available to fall: top of the room's floor chain to the bottom."""
    found = []
    for ri, room in enumerate(env.level.rooms):
        for index, s in enumerate(room.sectors):
            if s['floor'] == -127 or s['ceiling'] == -127:
                continue
            x = room.info['x'] + (index // room.z_sectors) * 1024 + 512
            z = room.info['z'] + (index % room.z_sectors) * 1024 + 512
            if d.resolve(env.level, ri, x, z) != (ri, s):
                continue
            top = d.height(env.level, ri, s, x, z, True)
            bottom = d.height(env.level, ri, s, x, z)
            if top is None or bottom is None:
                continue
            found.append((bottom - top, ri, x, z, top, bottom, s['room_below'] != 255))
    found.sort(reverse=True)
    return found


def fall_cases(env, per_level):
    out = []
    taken = 0
    last = None
    for height, ri, x, z, top, bottom, stacked in drops(env):
        if height < 2600 or taken >= per_level:
            break
        if last is not None and last[0] == ri and abs(last[1] - x) + abs(last[2] - z) < 3000:
            continue
        last = (ri, x, z)
        taken += 1
        for yoff in (700, 1500):
            y = top + yoff
            if y >= bottom:
                continue
            for angle in (0, 16384):
                for name, schedule in FALL_PATTERNS.items():
                    out.append(dict(family='fall', name=name, drop=height, start=(ri, x, y, z, angle), masks=expand(schedule)))
    return out


def wade_cases(env, per_level):
    out = []
    for ri, room in enumerate(env.level.rooms):
        if not room.flags & 1:
            continue
        for index, s in enumerate(room.sectors):
            if s['floor'] == -127 or s['room_below'] != 255:
                continue
            x = room.info['x'] + (index // room.z_sectors) * 1024 + 512
            z = room.info['z'] + (index % room.z_sectors) * 1024 + 512
            floor = d.height(env.level, ri, s, x, z)
            if floor is None:
                continue
            above = s['room_above']
            if above == 255:
                continue
            ceiling = s['ceiling'] * 256
            depth = floor - ceiling      # water surface is this room's ceiling at the portal
            if not 200 <= depth <= 800:
                continue
            for angle in (0, 16384, -32768, -16384):
                for name, schedule in WADE_PATTERNS.items():
                    out.append(dict(family='wade', name=name, depth=depth, start=(ri, x, floor, z, angle), masks=expand(schedule)))
        if len(out) > per_level * len(WADE_PATTERNS) * 4:
            break
    return out


def cell_sector(env, ri, x, z):
    room = env.level.rooms[ri]
    sx = min(max((x - room.info['x']) >> 10, 0), room.x_sectors - 1)
    sz = min(max((z - room.info['z']) >> 10, 0), room.z_sectors - 1)
    return room.sectors[sx * room.z_sectors + sz]


def water_depth(env, ri, x, z):
    """ItemObj_getWaterDepth of a point in room `ri`: the floor of the sector Room_getWaterSector
    returns (the top slab of the water column) minus its ceiling, or None without water."""
    rooms = env.level.rooms
    r, s = ri, cell_sector(env, ri, x, z)
    if rooms[r].flags & 1:
        while s['room_above'] != 255:
            r = s['room_above']
            if not rooms[r].flags & 1:
                break
            s = cell_sector(env, r, x, z)
            if s['room_above'] == 255:
                break
    else:
        while True:
            if s['room_below'] == 255:
                return None
            r = s['room_below']
            s = cell_sector(env, r, x, z)
            if rooms[r].flags & 1:
                break
    floor = d.height(env.level, r, s, x, z)
    return None if floor is None else floor - s['ceiling'] * 256


def wade_in_cases(env, per_level):
    """Walk into shallow water: Lara's feet end up at least LARA_STEP_HEIGHT (384) under the
    surface in a column whose top slab is at most 730 thick (Lara_updateWaterState), the only
    way into WATER_STATE_WADE. The start is on the neighbouring cell with no wading depth, facing the
    pool, so the water state is entered by play, not by placement."""
    out, seen = [], set()
    for ri, room in enumerate(env.level.rooms):
        if not room.flags & 1:
            continue
        for index, s in enumerate(room.sectors):
            if s['floor'] == -127 or s['room_below'] != 255:
                continue
            x = room.info['x'] + (index // room.z_sectors) * 1024 + 512
            z = room.info['z'] + (index % room.z_sectors) * 1024 + 512
            pool = d.ground(env, ri, x, z, None, clear=1024)
            depth = water_depth(env, ri, x, z)
            if pool is None or depth is None or not 384 <= depth <= 730:
                continue
            for dx, dz, angle in d.DIRS:
                sx, sz = x - dx * 1024, z - dz * 1024
                g = d.ground(env, ri, sx, sz, pool[1], 512, clear=1024)
                if g is None:
                    continue
                near = water_depth(env, g[0], sx, sz)
                if near is not None and 384 <= near <= 730 or (ri, x, z) in seen:
                    continue
                seen.add((ri, x, z))
                for name, schedule in WADE_IN.items():
                    out.append(dict(family='wadein', name=name, depth=depth, start=(g[0], sx, g[1], sz, angle), masks=expand(schedule)))
                break
        if len(seen) >= per_level:
            break
    return out


WADE_IN = {
    'walk-in': [(FORWARD, 90), (NONE, 20), (BACK, 60), (NONE, 20)],
    'run-in-out': [(FORWARD, 40), (BACK, 40), (FORWARD, 30), (NONE, 20)],
    'jump-in': [(FORWARD, 12), (FORWARD | JUMP, 5), (NONE, 60), (FORWARD, 40)],
    'turn-in': [(FORWARD, 50), (LEFT, 20), (FORWARD, 40), (RIGHT, 40), (BACK, 40)],
    'step-in': [(FORWARD | WALK, 70), (LEFT | WALK, 30), (BACK | WALK, 60)],
    'dive-out': [(FORWARD, 50), (JUMP, 3), (FORWARD | WALK, 80)],
}


def restore_cases(env, water_cells):
    """Lara_restore (JUMP+WEAPON while dead, game.c): `item LARA 0` kills her at tick 0, then the
    chord arrives before the death animation ends (3 and 30 ticks) and after the death page has opened
    (90; the restore is then not reachable: the inventory owns the tick). On dry ground and in water
    (the restored animation differs). `item` is placed-state evidence."""
    lara = d.probe(env, None, [0])['lara']['index']
    wet = [c for c in water_cells][:1]
    out = []
    for where, start in [('start', None)] + [('water', c['start']) for c in wet]:
        for k in (0, 3, 30, 90):
            out.append(dict(family='restore', name=f'{where}-dead-{k}', start=start, setup=[('item', lara, 0)],
                            masks=expand([(NONE, k), (JUMP | WEAPON, 3), (NONE, 60)])))
    return out


def execute(env, case):
    seen = d.Observer()
    try:
        result = d.compare(env, case['start'], case['masks'], ticks=len(case['masks']), observe=seen, setup=case.get('setup', ()))
    except ValueError as error:
        result = dict(status='error', why=str(error)[:200])
    result['states'] = sorted(set(seen.lara_states) & {HANG_LEFT, HANG_RIGHT, FAST_DIVE, HANG, SWAN_DIVE, UW_DIVE, FALL})
    result['water'] = sorted(seen.water_states)
    result['min_health'] = seen.min_health
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--levels', default='gym,level1,level2,level3a,level3b,level4,level5,level6,level7a,level7b,level8a,level8b,level8c,level10a,level10b,level10c')
    p.add_argument('--families', default='hang,fall,wade,wadein,restore')
    p.add_argument('--hang-edges', type=int, default=24)
    p.add_argument('--drops', type=int, default=8)
    p.add_argument('--wade-cells', type=int, default=6)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--jobs', type=int, default=3)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    families = set(args.families.split(','))
    report = dict(executable_sha256=d.sha(args.executable), oracle_sha256=d.sha(args.oracle), levels={})
    for name in args.levels.split(','):
        env = d.Env(name, args.executable, args.oracle)
        cases = []
        if 'hang' in families:
            cases += hang_cases(env, d.ledges, args.hang_edges)
        if 'fall' in families:
            cases += fall_cases(env, args.drops)
        if 'wade' in families:
            cases += wade_cases(env, args.wade_cells)
        if 'restore' in families:
            cases += restore_cases(env, wade_cases(env, 1))
        if 'wadein' in families:
            cases += wade_in_cases(env, args.wade_cells)
        if not cases:
            continue
        row = dict(cases=len(cases), status=collections.Counter(), by_family=collections.defaultdict(collections.Counter),
                   reached=collections.Counter(), bad=[])
        with futures.ThreadPoolExecutor(args.jobs) as pool:
            for case, res in zip(cases, pool.map(lambda c: execute(env, c), cases)):
                row['status'][res['status']] += 1
                row['by_family'][case['family']][res['status']] += 1
                for water in res.get('water', ()):
                    row['reached'][f"{case['family']}:water{water}"] += 1
                for state in res['states']:
                    row['reached'][f"{case['family']}:state{state}"] += 1
                if res['status'] not in ('equal', 'invalid'):
                    row['bad'].append(dict(family=case['family'], name=case['name'], start=case['start'],
                                           **{k: v for k, v in res.items() if k not in ('states', 'water')}))
        row['status'] = dict(row['status'])
        row['reached'] = dict(row['reached'])
        row['by_family'] = {k: dict(v) for k, v in row['by_family'].items()}
        report['levels'][name] = row
        print(name, json.dumps({k: v for k, v in row.items() if k != 'bad'}), flush=True)
        for bad in row['bad'][:6]:
            print('  ', json.dumps(bad), flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(r['bad'] for r in report['levels'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
