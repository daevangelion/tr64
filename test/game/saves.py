#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Save and load in the middle of the states the directed scripts reach.

The other directed scripts keep every script they compare when `DIRECTED_SAVE=DIR` is set
(`DIRECTED_SAVE_ONLY=1` writes them without comparing). This one replays a sample of such
scripts with the real source `gameSave`/`gameLoad` at two boundaries (oracle `--save-tick N
--load-tick M`, native `--game-save-tick/--game-load-tick`, test/oracle/README.md) and compares
every schema-3 record: a block half pushed, a ledge hang, wading, a fight, a Midas hand, the
restored Lara. Save and load tick are chosen from the script itself: just after Lara's state or
the set of moved/active items first changes, and near the end, so the saved state is the interesting
one and not an idle start.

PYTHONPATH=tools/cook:test/game. One production host with test/game/save-io.c and directives.c linked.
"""
import argparse
import collections
import concurrent.futures as futures
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d


def parse(path):
    start, setup, masks = None, [], []
    for line in path.read_text().split('\n'):
        words = line.split()
        if not words:
            continue
        if words[0] == 'start':
            start = tuple(int(w) for w in words[1:])
        elif words[0] in ('give', 'item', 'activate'):
            setup.append((words[0], int(words[1]), int(words[2])))
        else:
            masks.append(int(words[0]))
    return start, setup, masks


def boundaries(env, start, setup, masks):
    """(save, load) pairs: after each of the first few Lara state changes, and at the end."""
    rows = d.probe(env, start, masks, want='all', setup=setup)
    changes, last = [], None
    for row in rows:
        lara = row['lara']
        key = (lara['state'], lara['waterState'] if 'waterState' in lara else lara['water'], lara['weaponState'], row['inventory']['state'])
        if key != last and row['tick'] > 2:
            changes.append(row['tick'])
        last = key
    n = len(masks)
    picks = [changes[len(changes) * k // 4] for k in (1, 2, 3)] if len(changes) >= 4 else changes[:3]
    pairs = [(min(t, n - 2), min(t, n - 2) + 1) for t in picks]
    pairs.append((max(n - 30, 1), max(n - 30, 1) + 20))
    return sorted(set(pairs))


def execute(env, job):
    out = []
    for pair in job['pairs']:
        try:
            result = d.compare(env, job['start'], job['masks'], ticks=len(job['masks']), setup=job['setup'], save=pair)
        except ValueError as error:
            result = dict(status='error', why=str(error)[:200])
        result['pair'] = pair
        out.append(result)
    return out


HANGS = {10, 30, 31}
ENEMIES = set(range(6, 25)) | {27, 28, 30, 31, 32, 33, 34}


def holds(category, row, previous, alive):
    """The save-matrix categories (save_coverage.py), reduced to what the oracle record shows; `alive` collects the
    enemies seen with positive health, so a dead one was killed during the script."""
    lara = row['lara']
    for item in row['active']:
        if item['type'] in ENEMIES and item['health'] > 0:
            alive.add(item['index'])
    if lara is None:
        return None
    if category == 'hanging':
        return lara['state'] in HANGS and not lara['flags'] & 2 and lara['vSpeed'] == 0 \
            and previous is not None and previous['lara']['pos'][1] == lara['pos'][1] and previous['lara']['state'] in HANGS
    if category == 'boulder-rolling':
        for item in row['active']:
            if item['type'] == 38 and item['state'] == 1 and item['hSpeed'] != 0 and item['vSpeed'] == 0:
                return item['index']
        return None
    if category == 'enemy-dead':
        for item in row['active']:
            if item['type'] in ENEMIES and item['index'] in alive and item['health'] <= 0:
                return item['index']
        return None
    raise ValueError('category ' + category)


def cells(args):
    """Matrix cells the earlier gate left uncovered: hanging, boulder-rolling, enemy-dead."""
    report = {}
    for cell in args.cells.split(','):
        level, category = cell.split(':')
        env = d.Env(level, args.executable, args.oracle)
        files = sorted(f for folder in args.scripts for f in folder.glob(level + '-*.masks'))
        found = dict(status='no kept script reaches it', tried=0)
        for path in files[::max(len(files) // args.tries, 1)][:args.tries]:
            start, setup, masks = parse(path)
            try:
                rows = d.probe(env, start, masks, want='all', setup=setup)
            except ValueError:
                continue
            found['tried'] += 1
            previous, alive, tick = None, set(), None
            for row in rows:
                if row['tick'] > 2 and holds(category, row, previous, alive):
                    tick = row['tick']
                    break
                previous = row if row['lara'] else previous
            if tick is None or tick >= len(masks) - 2:
                continue
            results = {}
            for load in (tick + 1, tick + 30):
                load = min(load, len(masks))
                results[f'save {tick} load {load}'] = d.compare(env, start, masks, ticks=len(masks), setup=setup, save=(tick, load))['status']
            found = dict(script=path.name, save_tick=tick, results=results, tried=found['tried'],
                         status='equal' if set(results.values()) == {'equal'} else 'NOT EQUAL')
            if found['status'] == 'equal' or any(v not in ('equal', 'oracle_boundary') for v in results.values()):
                break
        report[cell] = found
        print(cell, json.dumps(found), flush=True)
    (args.out / 'cells.json').write_text(json.dumps(report, indent=1) + '\n')
    return int(any(r['status'] != 'equal' for r in report.values()))

def sweep(args):
    """One script, a save and a load at every STEP-th tick: oracle and port records equal for every pair."""
    level = args.sweep.name.split('-')[0]
    env = d.Env(level, args.executable, args.oracle)
    start, setup, masks = parse(args.sweep)
    ticks = range(args.first, len(masks) - 1, args.step)
    counts = collections.Counter()
    bad = []

    retried = []

    def one(t):
        result = d.compare(env, start, masks, ticks=len(masks), setup=setup, save=(t, t + 1))
        if result['status'] == 'different' and result.get('field') == 'malformed':
            retried.append(t)      # a record cut short by a killed process on a loaded host, not a field difference
            result = d.compare(env, start, masks, ticks=len(masks), setup=setup, save=(t, t + 1))
        return t, result
    with futures.ThreadPoolExecutor(args.jobs) as pool:
        for t, result in pool.map(one, ticks):
            counts[result['status']] += 1
            if result['status'] != 'equal':
                bad.append(dict(save_tick=t, **result))
    print(args.sweep.name, dict(counts), 'retried after a malformed record:', sorted(retried), 'save ticks', ticks.start, '..', ticks[-1], 'step', args.step, flush=True)
    for b in bad[:10]:
        print('  ', json.dumps(b), flush=True)
    (args.out / 'sweep.json').write_text(json.dumps(dict(script=args.sweep.name, status=dict(counts), retried=sorted(retried), bad=bad), indent=1) + '\n')
    return int(bool(bad))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--scripts', type=Path, action='append', help='directory of kept scripts (a family)')
    p.add_argument('--sweep', type=Path, help='one script: save at every STEP-th tick (load on the next) from FIRST to the end, all compared')
    p.add_argument('--cells', help='LEVEL:CATEGORY,...: for each, find a kept script and the first tick where the category holds, and save/load there')
    p.add_argument('--tries', type=int, default=40, help='cells: scripts tried per cell')
    p.add_argument('--step', type=int, default=10)
    p.add_argument('--first', type=int, default=10)
    p.add_argument('--sample', type=int, default=12, help='scripts per directory and level, evenly spread')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--jobs', type=int, default=3)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.sweep:
        return sweep(args)
    if args.cells:
        return cells(args)
    if not args.scripts:
        p.error('--scripts or --sweep')
    report = dict(executable_sha256=d.sha(args.executable), oracle_sha256=d.sha(args.oracle), families={})
    bad_total = 0
    for folder in args.scripts:
        files = collections.defaultdict(list)
        for path in sorted(folder.glob('*.masks')):
            files[path.name.split('-')[0]].append(path)
        family = collections.Counter()
        bad = []
        for level, paths in sorted(files.items()):
            step = max(len(paths) / args.sample, 1)
            chosen = [paths[int(i * step)] for i in range(min(args.sample, len(paths)))]
            env = d.Env(level, args.executable, args.oracle)

            def job(path):
                start, setup, masks = parse(path)
                if not masks:
                    return None
                try:
                    pairs = boundaries(env, start, setup, masks)
                except ValueError:      # an invalid placement the sweep itself reports as such
                    return None
                return dict(path=path.name, start=start, setup=setup, masks=masks, pairs=pairs)
            with futures.ThreadPoolExecutor(args.jobs) as pool:
                jobs = [j for j in pool.map(job, chosen) if j]
                for j, results in zip(jobs, pool.map(lambda j: execute(env, j), jobs)):
                    for r in results:
                        family[r['status']] += 1
                        if r['status'] not in ('equal', 'invalid'):
                            bad.append(dict(level=level, script=j['path'], **r))
            print(folder.name, level, dict(family), flush=True)
        report['families'][folder.name] = dict(status=dict(family), bad=bad)
        bad_total += len(bad)
        for b in bad[:6]:
            print('  ', json.dumps(b), flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(bad_total > 0)


if __name__ == '__main__':
    raise SystemExit(main())
