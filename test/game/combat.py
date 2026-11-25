#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hit and death paths of the bear, the human enemies, Natla, the doppelganger and the scion target.

For each target item Lara is placed on open ground at several distances and
bearings, facing it, and fires with each weapon: pistols (drawn with WEAPON), and
shotgun, magnums and uzis chosen through the inventory ring (SELECT, rotate,
ACTION: Lara_changeWeapon). The weapons are real pickups' worth of inventory
supplied with `give` (placed-state evidence); the pistols variant needs nothing.
Death paths use `item INDEX HEALTH` (placed-state evidence) so the target dies
inside the 600-tick window: the full-health variants are play-reached, the
low-health ones are not. Natla is placed at the health that puts her in phase 1,
at the phase-2 threshold and at the faint/death thresholds.
Lara reaches a target through the level's own standable sector centres (`directed.route`): the
skater stops on a ramp 8,000 units from his pad, the doppelganger exists only while its mirror image of
Lara about the room centre stands on level ground, LEVEL3B's Larson sleeps until Lara swims up into
room 10 (`cuts.py larson`'s route, replayed with the pistols fired and unfired). The doppelganger's `hit`
hurts Lara, not it: `lara_hurt` counts those scripts. Every script is compared with the oracle record
for record (schema 3).
"""
import argparse
import collections
import concurrent.futures as futures
import json
import math
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
import switches
from directed import ACTION, NONE, WEAPON, WALK, LEFT, RIGHT, BACK, FORWARD

TARGETS = {6: 'doppelganger', 7: 'wolf', 8: 'bear', 9: 'bat', 10: 'crocodile', 11: 'crocodile_water', 12: 'lion', 13: 'lioness', 14: 'puma',
           15: 'gorilla', 16: 'rat', 17: 'rat_water', 18: 'rex', 19: 'raptor', 20: 'mutant_1', 21: 'mutant_2', 22: 'mutant_3', 23: 'centaur',
           24: 'mummy', 27: 'larson', 28: 'pierre', 29: 'skateboard', 30: 'skater', 31: 'cowboy', 32: 'mr_t', 33: 'natla', 34: 'adam',
           145: 'scion_target'}
WEAPONS = {'pistols': (None, d.SLOT['pistols']), 'shotgun': (85, d.SLOT['shotgun']),
           'magnums': (86, d.SLOT['magnums']), 'uzis': (87, d.SLOT['uzis'])}
# Health presets per target: full (as constructed), near death, one shot from death.
HEALTH = dict.fromkeys(TARGETS, (None, 40, 3))
HEALTH[33] = (None, 205, 200, 100, 6, 1, 0)
HEALTH[145] = (None, 40, 1)


def bearings(env, item, index):
    """Placements facing the target: on every trigger sector that activates it (the
    enemies start inactive, the pad is what wakes them), with the enemy dead ahead and
    35 degrees off to either side. Targets with no trigger sector (none here) are also
    sampled on rings around them."""
    out = []
    for ri, sector, ttype, acts in switches.triggers(env):
        if index not in acts or ttype not in (0, 3, 4, 5, 6, 7):
            continue
        room = env.level.rooms[ri]
        x = room.info['x'] + (sector // room.z_sectors) * 1024 + 512
        z = room.info['z'] + (sector % room.z_sectors) * 1024 + 512
        g = d.ground(env, ri, x, z, None, clear=1024)
        if g is None:
            continue
        dist = int(math.hypot(item['x'] - x, item['z'] - z))
        aim = d.heading_to(x, z, item['x'], item['z'])
        for off in (0, 6000, -6000):
            out.append((dist, ('pad', ri, sector, off), (g[0], x, g[1], z, d.yaw(aim + off))))
    if not out:
        for dist in (1500, 2500):
            for k in range(8):
                a = k * 8192
                x = item['x'] + int(round(math.sin(a * math.pi / 32768) * dist))
                z = item['z'] + int(round(math.cos(a * math.pi / 32768) * dist))
                g = d.ground(env, item['room'], x, z, None, clear=1536)
                if g is not None:
                    out.append((dist, ('ring', a), (g[0], x, g[1], z, d.heading_to(x, z, item['x'], item['z']))))
    return out


def target_xz(script, index):
    it = script.item(index)
    return (it['pos'][0], it['pos'][2]) if it else None


def fire_script(env, start, give, weapon, pattern, index):
    script = d.Script(env, start, setup=give)
    # Let the pad wake the target and Lara settle (placements on slopes slide first).
    script.until(lambda r: r['lara']['state'] == 2 and any(x['index'] == index for x in r['active']), NONE, 160, chunk=4)
    # Some targets walk to a stand before they stop (the skater on his ramp): steer for where
    # a Lara standing still on the pad sees him stop (same oracle, deterministic), not where
    # he is while he is still skating.
    hint = None
    if script.item(index) and script.item(index)['type'] in SETTLE:
        rec = d.probe(env, start, [NONE] * SETTLE[script.item(index)['type']], setup=give)
        found = [a for a in rec['active'] if a['index'] == index]
        hint = (found[0]['pos'][0], found[0]['pos'][2]) if found else None
    # Close in (the pads are often a whole room away) until the target is in pistol range,
    # steering through the level's standable sector centres; straight-line steering if no path.
    goal, lara = hint or target_xz(script, index), script.lara()
    if script.item(index) and script.item(index)['type'] in MIRROR:
        # The doppelganger stands at the mirror image of Lara about the room centre: Lara must be
        # within about 2000 of the centre for it to be inside pistol range.
        cx, cz = MIRROR[script.item(index)['type']]
        script.walk_route(lambda r, x, y, z: 900 < math.hypot(x - cx, z - cz) < 2100, limit=420)
    elif goal is not None and math.hypot(goal[0] - lara['pos'][0], goal[1] - lara['pos'][2]) >= 4500:
        gx, gz = goal
        script.walk_route(lambda r, x, y, z: math.hypot(x - gx, z - gz) < 4200, limit=420)
    for _ in range(20):
        goal, lara = target_xz(script, index), script.lara()
        if goal is None or math.hypot(goal[0] - lara['pos'][0], goal[1] - lara['pos'][2]) < 4500 or len(script.masks) > 330:
            break
        script.walk_to(goal[0], goal[1], tol=3000, limit=24, chunk=6)
    script.until(lambda r: r['lara']['state'] == 2, NONE, 30, chunk=2)
    give_type, slot = WEAPONS[weapon]
    if weapon == 'pistols':
        script.hold(WEAPON, 1)
        script.hold(NONE, 24)
    else:
        d.open_ring(script)
        d.ring_pick(script, slot, page_keys=False)
        script.hold(NONE, 30)

    def aim():
        goal = target_xz(script, index)
        if goal:
            d.face(script, goal[0], goal[1], tol=2500, limit=30)

    if pattern == 'stand':
        for _ in range(8):
            aim()
            script.hold(ACTION, 30)
    elif pattern == 'strafe':
        aim()
        for k in range(8):
            script.hold(ACTION | WALK | (LEFT if k % 2 else RIGHT), 26)
    elif pattern == 'retreat':
        aim()
        script.hold(ACTION | BACK, 100)
        script.hold(ACTION, 100)
    elif pattern == 'holster':
        aim()
        script.hold(ACTION, 80)
        script.hold(WEAPON, 1)
        script.hold(NONE, 40)
        script.hold(WEAPON, 1)
        script.hold(NONE, 30)
        aim()
        script.hold(ACTION, 120)
    return script


SETTLE = {30: 200}
MIRROR = {6: (36864, 60416)}   # ROOM_CENTER_X, ROOM_CENTER_Z of game/common/enemy.h
PATTERNS = ('stand', 'strafe', 'retreat', 'holster')


def owns_pistols(env):
    return d.probe(env, None, []) ['inventory']['counts'][d.SLOT['pistols']] > 0


def jobs_for(env, index, item, args):
    out = []
    pts = bearings(env, item, index)
    pistols_missing = not owns_pistols(env)
    if len(pts) > args.placements:      # spread the sample over the available pads
        step = len(pts) / args.placements
        pts = [pts[int(k * step)] for k in range(args.placements)]
    for hp in HEALTH[item['type']]:
        for weapon in (args.weapons.split(',')):
            give = [('give', WEAPONS[weapon][0], 1)] if WEAPONS[weapon][0] else []
            if pistols_missing:
                give = [('give', 84, 1)] + give     # LEVEL10A confiscates the pistols on entry
            if hp is not None:
                give = give + [('item', index, hp)]
            for n, (dist, where, start) in enumerate(pts):
                for pattern in PATTERNS:
                    if pattern != 'stand' and n:
                        continue
                    out.append(dict(target=TARGETS[item['type']], item=index, hp=hp, weapon=weapon, dist=dist,
                                    bearing=where, pattern=pattern, start=start, give=give))
    return out


LARSON_ITEM, LARSON_HEALTH, LARSON_ITEM_TYPE = 30, (None, 40, 3, 1), 27


def larson_jobs():
    """LEVEL3B's Larson sleeps hidden until Lara's centre is in room 10's pool column (an ACTIVATE
    trigger no pad placement reaches by walking): the `cuts.py larson` route, 583 ticks, with the
    pistols drawn and fired at him: full health (he shoots back: Larson_fire and Lara_hit), and
    near death through `item`."""
    cuts = d.load_module('combat_cuts', 'cuts.py')
    masks = [m for mask, n in cuts.LARSON_PHASES[:-1] for m in [mask] * n]
    waiting = [m for mask, n in cuts.LARSON_PHASES[:5] + [(0, 240)] for m in [mask] * n]   # never draws: he shoots at her
    return [dict(target='larson', item=LARSON_ITEM, hp=hp, weapon='pistols', dist=0, bearing='cuts-larson', pattern=pattern,
                 start=(10, 43520, 6500, 58880, 16384), masks=script,
                 give=[] if hp is None else [('item', LARSON_ITEM, hp)])
            for pattern, script in (('route', masks), ('wait', waiting)) for hp in LARSON_HEALTH]


def execute(env, job):
    seen = d.Observer()
    try:
        if 'masks' in job:
            masks = job['masks']
        else:
            script = fire_script(env, job['start'], job['give'], job['weapon'], job['pattern'], job['item'])
            script.pad(min(600, len(script.masks)))
            masks = script.masks
        result = d.compare(env, job['start'], masks, ticks=len(masks), observe=seen, setup=job['give'])
    except ValueError as error:
        result = dict(status='invalid' if 'invalid start room or angle' in str(error) or 'Lara is absent' in str(error) else 'error', why=str(error)[:200])
    result['hit'] = seen.min_item_health.get(job['item'], 1 << 30)
    result['start_hp'] = seen.first_item_health.get(job['item'], 1 << 30)
    result['kills'] = seen.save_max['kills']
    result['ammo'] = seen.save_max['ammoUsed']
    result['lara_min'] = seen.min_health
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--levels', default='level10a,level10b,level10c')
    p.add_argument('--targets', default=','.join(map(str, TARGETS)))
    p.add_argument('--items', default='', help='only these item indexes (one level at a time): a sample of the level, not all of its enemies')
    p.add_argument('--weapons', default='pistols,shotgun,magnums,uzis')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--jobs', type=int, default=3)
    p.add_argument('--placements', type=int, default=4)
    p.add_argument('--limit', type=int, default=0)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    wanted = set(map(int, args.targets.split(',')))
    only = {int(i) for i in args.items.split(',') if i}
    report = dict(executable_sha256=d.sha(args.executable), oracle_sha256=d.sha(args.oracle), levels={})
    for name in args.levels.split(','):
        env = d.Env(name, args.executable, args.oracle)
        jobs = []
        for index, item in env.items(*wanted):
            if only and index not in only:
                continue
            jobs += jobs_for(env, index, item, args)
        if name == 'level3b' and LARSON_ITEM_TYPE in wanted:
            jobs += larson_jobs()
        if args.limit:
            jobs = jobs[:args.limit]
        if not jobs:
            continue
        row = dict(cases=len(jobs), status=collections.Counter(), per_target=collections.defaultdict(lambda: collections.Counter()), bad=[])
        with futures.ThreadPoolExecutor(args.jobs) as pool:
            for job, res in zip(jobs, pool.map(lambda j: execute(env, j), jobs)):
                row['status'][res['status']] += 1
                t = row['per_target'][f"{job['target']}/{job['weapon']}"]
                t[res['status']] += 1
                if res['status'] in ('equal', 'different', 'loud'):
                    t['hurt'] += res['hit'] < res['start_hp']
                    t['dead'] += res['hit'] <= 0
                    t['kills'] += bool(res['kills'])
                    t['lara_hurt'] += res['lara_min'] < 1000
                if res['status'] not in ('equal', 'invalid'):
                    row['bad'].append(dict({k: v for k, v in job.items() if k != 'give'}, give=job['give'], **res))
        row['status'] = dict(row['status'])
        row['per_target'] = {k: dict(v) for k, v in row['per_target'].items()}
        report['levels'][name] = row
        print(name, json.dumps({k: v for k, v in row.items() if k != 'bad'}), flush=True)
        for bad in row['bad'][:6]:
            print('  ', json.dumps(bad), flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(r['bad'] for r in report['levels'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
