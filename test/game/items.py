#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Key and puzzle-item use at keyholes and slots, and the Midas hand.

Every KEYHOLE_1..4 / PUZZLEHOLE_1..4 item of a level gets Lara placed on the
hole's front side (the side its angle faces, where the source's LIMIT_HOLE window
is), standing at three distances and two small yaw offsets. The inventory item is
supplied with `give` (placed-state evidence: the real addPickup path, not a walk
from the pickup, which for most pairs is 11k..100k units away and does not fit
the 600-tick script limit), then used through the ring exactly as a player does:
SELECT, keys page, rotate, ACTION. Variants: the matching item chosen from the
main ring, the ring opened by ACTION at the hole (USE page), a wrong item, the
item used twice, and no item at all. The Midas hand: standing on it (Midas
death), and the lead bar chosen from the USE page opened by ACTION (conversion).
Scripts are built closed-loop from the oracle, then compared record for record.
"""
import argparse
import collections
import concurrent.futures as futures
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from directed import ACTION, NONE, FORWARD

KEYHOLES = {137: 129, 138: 130, 139: 131, 140: 132}     # hole type -> matching pickup type
PUZZLEHOLES = {118: 110, 119: 111, 120: 112, 121: 113}
PICKUP_SLOT = {129: 1, 130: 2, 131: 3, 132: 4, 113: 5, 112: 6, 111: 7, 110: 8, 126: 0}
USE_KEY, USE_PUZZLE, USE_MIDAS, DEATH_MIDAS = 42, 43, 50, 51
MIDAS_HAND, LEADBAR = 128, 126


def forward(angle):
    return {0: (0, 1), 16384: (1, 0), -32768: (0, -1), -16384: (-1, 0)}[angle]


def hole_starts(env, item):
    """Placements on the hole's front side, facing it."""
    fx, fz = forward(item['angle'] if item['angle'] in (0, 16384, -32768, -16384) else (item['angle'] + 32768) % 65536 - 32768)
    out = []
    for dist in (330, 420, 500):
        x, z = item['x'] + fx * dist, item['z'] + fz * dist
        g = d.ground(env, item['room'], x, z, item['y'], 300, clear=1024)
        if g is None:
            continue
        base = d.yaw(item['angle'] + 32768)
        for off in (0, 1500, -1500):
            out.append((dist, off, (g[0], x, g[1], z, d.yaw(base + off))))
    return out


def variants(match, wrong):
    """(name, give list, procedure) for one hole; procedure drives the Script."""
    slot = PICKUP_SLOT[match]
    keys = slot in (1, 2, 3, 4) or slot in (5, 6, 7, 8)

    def ring(script, which, page):
        script.hold(NONE, 3)
        d.open_ring(script)
        d.ring_pick(script, which, page_keys=page)
        script.hold(NONE, 120)

    def action_open(script):
        script.hold(NONE, 3)
        script.until(lambda r: r['inventory']['state'] != 0, ACTION, 20)
        script.until(lambda r: r['inventory']['state'] == 4, NONE, 60, chunk=2)
        d.ring_pick(script, slot, page_keys=None)
        script.hold(NONE, 120)

    return [
        ('keys-page', [('give', match, 1)], lambda s: ring(s, slot, True)),
        ('action-open', [('give', match, 1)], action_open),
        ('twice', [('give', match, 2)], lambda s: (ring(s, slot, True), ring(s, slot, True))),
        ('wrong-item', [('give', wrong, 1)], lambda s: ring(s, PICKUP_SLOT[wrong], True)),
        ('action-no-item', [], lambda s: s.hold(ACTION, 60)),
    ]


def run_one(env, start, give, procedure, observer):
    script = d.Script(env, start, setup=give)
    procedure(script)
    script.pad(min(600, max(len(script.masks), 200)))
    return d.compare(env, start, script.masks, ticks=len(script.masks), observe=observer, setup=give)


def hole_jobs(env):
    jobs = []
    for table, base_wrong in ((KEYHOLES, 129), (PUZZLEHOLES, 110)):
        for index, item in env.items(*table):
            match = table[item['type']]
            others = [t for t in table.values() if t != match]
            wrong = others[0]
            for dist, off, start in hole_starts(env, item):
                if off and dist != 420:
                    continue
                for name, give, procedure in variants(match, wrong):
                    if name in ('twice', 'wrong-item', 'action-no-item') and (dist != 420 or off):
                        continue
                    jobs.append(dict(family='hole', item=index, type=item['type'], variant=name, dist=dist, yaw=off,
                                     start=start, give=give, procedure=procedure))
    return jobs


def midas_jobs(env):
    jobs = []
    for index, hand in env.items(MIDAS_HAND):
        # Standing on the hand (Midas death): cells around it, all four facings.
        for dx, dz in ((0, 0), (300, 0), (-300, 0), (0, 300), (0, -300), (400, 400)):
            g = d.ground(env, hand['room'], hand['x'] + dx, hand['z'] + dz, None, clear=1024)
            if g is None:
                continue
            for facing in (0, 16384, -32768, -16384):
                jobs.append(dict(family='midas-death', item=index, type=MIDAS_HAND, variant=f'{dx},{dz}@{facing}', dist=0, yaw=0,
                                 start=(g[0], hand['x'] + dx, g[1], hand['z'] + dz, facing), give=[],
                                 procedure=lambda s: s.hold(NONE, 120)))
        # Standing 520..690 away, facing the hand, lead bar from the USE page.
        for fx, fz, angle in d.DIRS:
            for dist in (540, 600, 680):
                x, z = hand['x'] + fx * dist, hand['z'] + fz * dist
                g = d.ground(env, hand['room'], x, z, None, clear=1024)
                if g is None:
                    continue
                facing = d.yaw(angle + 32768)
                for off in (0, 1500):
                    start = (g[0], x, g[1], z, d.yaw(facing + off))

                    def convert(s):
                        s.hold(NONE, 3)
                        s.until(lambda r: r['inventory']['state'] != 0, ACTION, 20)
                        s.until(lambda r: r['inventory']['state'] == 4, NONE, 60, chunk=2)
                        d.ring_pick(s, 0, page_keys=None)
                        s.hold(NONE, 200)
                    jobs.append(dict(family='midas-use', item=index, type=MIDAS_HAND, variant=f'{fx},{fz}', dist=dist, yaw=off,
                                     start=start, give=[('give', LEADBAR, 2)], procedure=convert))
                    jobs.append(dict(family='midas-nobar', item=index, type=MIDAS_HAND, variant=f'{fx},{fz}', dist=dist, yaw=off,
                                     start=start, give=[], procedure=lambda s: s.hold(ACTION, 80)))
    return jobs


def execute(env, job):
    seen = d.Observer()
    try:
        result = run_one(env, job['start'], job['give'], job['procedure'], seen)
    except ValueError as error:
        result = dict(status='error', why=str(error)[:200])
    result['states'] = {k: v for k, v in seen.lara_states.items() if k in (USE_KEY, USE_PUZZLE, USE_MIDAS, DEATH_MIDAS)}
    result['types'] = sorted({t for t, _ in seen.item_states if t in range(122, 126)})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--levels', default='level2,level3a,level4,level5,level6,level7a,level7b,level8a,level8b,level8c,level10a')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--jobs', type=int, default=3)
    p.add_argument('--limit', type=int, default=0)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = dict(executable_sha256=d.sha(args.executable), oracle_sha256=d.sha(args.oracle), levels={})
    for name in args.levels.split(','):
        env = d.Env(name, args.executable, args.oracle)
        jobs = hole_jobs(env) + midas_jobs(env)
        if args.limit:
            jobs = jobs[:args.limit]
        row = dict(cases=len(jobs), status=collections.Counter(), reached=collections.Counter(), by_family=collections.defaultdict(collections.Counter), bad=[])
        with futures.ThreadPoolExecutor(args.jobs) as pool:
            for job, res in zip(jobs, pool.map(lambda j: execute(env, j), jobs)):
                row['status'][res['status']] += 1
                row['by_family'][job['family']][res['status']] += 1
                for state in res['states']:
                    row['reached'][f"{job['family']}:state{state}"] += 1
                if res.get('types'):
                    row['reached'][f"{job['family']}:done-type"] += 1
                if res['status'] not in ('equal', 'invalid'):
                    row['bad'].append(dict(family=job['family'], item=job['item'], variant=job['variant'], dist=job['dist'],
                                           yaw=job['yaw'], start=job['start'], **{k: v for k, v in res.items() if k not in ('states', 'types')}))
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
