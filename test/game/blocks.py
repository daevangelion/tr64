#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Push and pull of movable blocks: every block of a level, every open side.

Lara is placed on the ground cell beside the block (same floor height, as the
source's `lara->pos.y == pos.y` test needs), facing it, at three distances, and
drives hold-ACTION / push / pull / release schedules. Each script is compared
with the oracle record for record (schema 3). The report counts how often the
scripts really latched the block (BLOCK_READY), pushed (BLOCK_PUSH) and pulled
(BLOCK_PULL), and how many blocks moved, from the oracle's own records.
"""
import argparse
import collections
import concurrent.futures as futures
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from directed import ACTION, FORWARD, BACK, NONE

BLOCK_TYPES = (48, 49, 50, 51)
BLOCK_READY, BLOCK_PUSH, BLOCK_PULL = 38, 36, 37

PATTERNS = {
    'latch': [(ACTION, 60)],
    'push1': [(ACTION, 45), (ACTION | FORWARD, 150), (NONE, 20)],
    'push-long': [(ACTION, 45), (ACTION | FORWARD, 520)],
    'push-release': [(ACTION, 45), (ACTION | FORWARD, 60), (NONE, 30), (ACTION, 30), (ACTION | FORWARD, 200)],
    'pull1': [(ACTION, 45), (ACTION | BACK, 150), (NONE, 20)],
    'pull-long': [(ACTION, 45), (ACTION | BACK, 520)],
    'push-pull': [(ACTION, 45), (ACTION | FORWARD, 150), (ACTION, 20), (ACTION | BACK, 300)],
    'pull-push': [(ACTION, 45), (ACTION | BACK, 150), (ACTION, 20), (ACTION | FORWARD, 300)],
    'tap': [(ACTION, 45), (ACTION | FORWARD, 1), (NONE, 3), (ACTION | BACK, 1), (NONE, 3), (ACTION | FORWARD, 200)],
    'run-in': [(FORWARD | ACTION, 70), (ACTION, 40), (ACTION | FORWARD, 200)],
    'walk-in-push': [(FORWARD, 30), (NONE, 12), (ACTION, 45), (ACTION | FORWARD, 200)],
    'walk-in-pull': [(FORWARD, 30), (NONE, 12), (ACTION, 45), (ACTION | BACK, 200)],
}
DISTANCES = (640, 600, 690, 1024)


def has_trigger(env, sector):
    index = sector['floor_index']
    if not index:
        return False
    words = env.level.data['floor_data']
    while True:
        cmd = words[index]
        kind = cmd & 31
        if kind == 4:
            return True
        if cmd & 32768 or kind not in (1, 2, 3):
            return False
        index += 2


def cells(env, item, step, count=4):
    """Cells a block would slide through along (dx, dz), tagged trigger/plain."""
    out = []
    for k in range(1, count + 1):
        x, z = item['x'] + step[0] * 1024 * k, item['z'] + step[1] * 1024 * k
        if d.ground(env, item['room'], x, z, item['y'], clear=1024) is None:
            break
        out.append('trigger' if has_trigger(env, env.sector(item['room'], x, z)) else 'plain')
    return out


def situations(env):
    found = []
    for index, item in env.items(*BLOCK_TYPES):
        for dx, dz, angle in d.DIRS:
            # Lara stands on the (dx, dz) side of the block and faces it: angle of -(dx, dz).
            facing = d.yaw(angle + 32768)
            for distance in DISTANCES:
                x, z = item['x'] + dx * distance, item['z'] + dz * distance
                g = d.ground(env, item['room'], x, z, item['y'], clear=1024)
                if g is None:
                    continue
                room, y = g
                push_cells = cells(env, item, (-dx, -dz))
                pull_cells = cells(env, item, (dx, dz))
                found.append(dict(item=index, room=item['room'], start=(room, x, y, z, facing),
                                  side=(dx, dz), distance=distance, push=push_cells, pull=pull_cells))
    return found


def run_one(env, sit, pattern):
    schedule = PATTERNS[pattern]
    masks = [m for mask, n in schedule for m in [mask] * n]
    seen = d.Observer()
    result = d.compare(env, sit['start'], masks, observe=seen)
    result['reach'] = dict(ready=seen.lara_states[BLOCK_READY], push=seen.lara_states[BLOCK_PUSH],
                           pull=seen.lara_states[BLOCK_PULL],
                           moved=any(seen.item_states[(t, st)] for t in BLOCK_TYPES for st in (2, 3)))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--levels', default='level2,level3b,level4,level5,level6,level7a,level7b,level8a,level8b,level8c,level10a,level10b,level10c')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--patterns')
    p.add_argument('--jobs', type=int, default=3)
    p.add_argument('--limit', type=int, default=0)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    patterns = args.patterns.split(',') if args.patterns else list(PATTERNS)
    report = dict(executable_sha256=d.sha(args.executable), oracle_sha256=d.sha(args.oracle), levels={})
    for name in args.levels.split(','):
        env = d.Env(name, args.executable, args.oracle)
        sits = situations(env)
        row = dict(blocks=len(env.items(*BLOCK_TYPES)), situations=len(sits), cases=0, status=collections.Counter(),
                   ready=0, push=0, pull=0, block_active_cases=0, blocks_active=set(), differences=[], errors=[])
        jobs = [(s, pat) for s in sits for pat in patterns]
        if args.limit:
            jobs = jobs[:args.limit]
        with futures.ThreadPoolExecutor(args.jobs) as pool:
            for (sit, pat), res in zip(jobs, pool.map(lambda j: run_one(env, *j), jobs)):
                row['cases'] += 1
                row['status'][res['status']] += 1
                reach = res['reach']
                row['ready'] += bool(reach['ready'])
                row['push'] += bool(reach['push'])
                row['pull'] += bool(reach['pull'])
                if reach['moved']:
                    row['block_active_cases'] += 1
                    row['blocks_active'].add(sit['item'])
                if res['status'] not in ('equal', 'invalid'):
                    (row['differences'] if res['status'] == 'different' else row['errors']).append(
                        dict(item=sit['item'], start=sit['start'], pattern=pat, **res))
        row['status'] = dict(row['status'])
        row['blocks_active'] = sorted(row['blocks_active'])
        report['levels'][name] = row
        print(json.dumps({k: v for k, v in row.items() if k not in ('differences', 'errors')}), flush=True)
        for bad in row['differences'][:5] + row['errors'][:5]:
            print('  ', json.dumps(bad), flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(r['differences'] or r['errors'] for r in report['levels'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
