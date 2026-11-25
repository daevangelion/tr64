#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""LEVEL8B's four drawbridges (items 19 to 22), oracle against port, schema 4.

The reference source never built a controller for the drawbridge, so its switches activated
the item but it stayed in state 0 for ever, and its floor rule could not have found the
deck sectors even in state 1 (oracle host.patch hunks 17 and 18). Cases, each compared tick for
tick through the streaming pipes:

lower-N   Lara placed in front of the bridge's switch (35 -> 19, 45 -> 20, 61 -> 21, 69 -> 22),
          one ACTION press: the bridge goes state 0 -> 1 (animation 231 -> 232 -> 233).
toggle-22 the same switch pressed twice: the bridge follows it back up (1 -> 0), and is solid again.
cross-22  placed at switch 69, pulls it, walks to the east ledge, over the lowered bridge, and picks
          up artifact 15 (item 15, pickup trigger lists door 68, mask 1 of 31): by play from the
          switch, 600 ticks at most. Placed-start evidence, not a playthrough: the other three
          switches are on other floors; no single 600-tick scene can lower all four bridges and so
          open door 68 (masks 24|2|4|1 = 31, the four pickup triggers of room 36).
raised-22 placed on the ledge tile beside the raised bridge, walking at it: the raised bridge is a
          solid (Lara is pushed out of its bounding box) and she does not fall into the pit.

negative  each case again with the previous oracle (`--old-oracle`, m6-worldfix) against the same
          port: it must differ (that oracle has no controller, so the bridge stays raised).

door 68  not opened by play, and not expressible with the directives (`start`, `give`, `item`): the four artifacts are on
          four decks that meet in a pit, each deck lowered by its own switch (items 35, 45, 61, 69 in rooms 36, 41, 49,
          57); `reach` shows no walking route between three of the four switch fronts (45 and 61 only reach each
          other), so one Lara cannot pull them in one script, and no directive can say "this pickup is already
          collected and its trigger's mask already in the door" without new machinery. The tick limit (600; 4096 for
          LEVEL10A) is not the obstacle. Only artifact 15 (mask 1 of the door's 31) is collected by play.

Run with PYTHONPATH=tools/cook:
  drawbridge.py --executable HOST --oracle ORACLE --old-oracle OLD --out DIR [cases|negative|all]
"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
import items
from directed import ACTION, FORWARD, NONE

BRIDGES = {19: 35, 20: 45, 21: 61, 22: 69}      # bridge item -> its switch item
ARTIFACT = 15                                   # on bridge 22's far deck sector
DOWN, UP = 1, 0


def bridge_trace(rows, index):
    """(tick, state, goal, anim, frame) of the bridge, when it is in the active list."""
    out = []
    for r in rows:
        for it in r['active']:
            if it['index'] == index:
                out.append((r['tick'], it['state'], it['goal'], it['anim'], it['frame']))
    return out


class Watch:
    """Observer over records already proven equal."""
    def __init__(self, index):
        self.index = index
        self.seq = []
        self.lara_room_x = []
        self.counts = None
        self.counts_first = None
        self.pairs = set()
        self.min_y = 1 << 30
        self.max_y = -(1 << 30)

    def __call__(self, row):
        for it in row['active']:
            if it['index'] == self.index:
                key = (it['state'], it['goal'], it['anim'])
                if not self.seq or self.seq[-1][1:] != key:
                    self.seq.append((row['tick'],) + key)
        lara = row['lara']
        if lara is not None:
            self.lara_room_x.append((lara['room'], lara['pos'][0], lara['pos'][2]))
            self.min_y = min(self.min_y, lara['pos'][1])
            self.max_y = max(self.max_y, lara['pos'][1])
        counts = tuple(row['inventory']['counts'])
        if self.counts_first is None:
            self.counts_first = counts
        self.counts = counts
        if row.get('world'):
            self.pairs.add(row['world'][3])


def start_at_switch(env, switch):
    return items.hole_starts(env, env.level.data['items'][switch])[0][2]


def lower_case(env, bridge):
    return dict(name=f'lower-{bridge}', bridge=bridge, start=start_at_switch(env, BRIDGES[bridge]),
                masks=[ACTION] * 8 + [NONE] * 120)


def toggle_case(env):
    return dict(name='toggle-22', bridge=22, start=start_at_switch(env, 69),
                masks=[ACTION] * 8 + [NONE] * 100 + [ACTION] * 8 + [NONE] * 100)


def cross_case(env):
    """Generated through the oracle (probe): pull, wait for state 1, walk to the artifact, take it."""
    I = env.level.data['items']
    start = start_at_switch(env, 69)
    s = d.Script(env, start)
    s.hold(ACTION, 8)
    s.hold(NONE, 2)
    s.until(lambda r: any(a['index'] == 22 and a['state'] == DOWN for a in r['active']), NONE, 150, chunk=5)
    if not s.walk_route(lambda r, x, y, z: x >> 10 == 51 and z >> 10 == 55, limit=500):
        return None
    if not s.walk_to(51500, 56832, tol=250, limit=150, run=False):
        return None
    if not d.pickup(s, I[ARTIFACT]):
        return None
    s.hold(NONE, 20)
    return dict(name='cross-22', bridge=22, start=start, masks=list(s.masks))


def raised_case(env):
    """Standing on the ledge tile, facing the raised bridge, walking at it."""
    I = env.level.data['items'][22]
    g = d.ground(env, 16, I['x'], I['z'], None, clear=1024)
    if g is None:
        return None
    return dict(name='raised-22', bridge=22, start=(g[0], I['x'] - 300, g[1], I['z'], -16384),
                masks=[FORWARD] * 80 + [NONE] * 20)


def run_case(env, case):
    watch = Watch(case['bridge'])
    try:
        result = d.compare(env, case['start'], case['masks'], ticks=len(case['masks']), observe=watch)
    except ValueError as error:
        return dict(status='error', why=str(error)[:200])
    result['bridge_sequence'] = watch.seq
    result['inventory_before'] = watch.counts_first
    result['inventory_after'] = watch.counts
    result['world_pairs'] = sorted(watch.pairs)
    result['lara_y_range'] = [watch.min_y, watch.max_y]
    result['lara_last'] = watch.lara_room_x[-1] if watch.lara_room_x else None
    return result


def verdicts(case, result):
    """Per-case expectations on top of tick-for-tick equality; returns a list of failures."""
    bad = []
    if result.get('status') != 'equal':
        return [f"not equal: {result.get('status')} {result.get('field', '')}"]
    seq = [(s, g, a) for _, s, g, a in [(t[0], t[1], t[2], t[3]) for t in result['bridge_sequence']]]
    states = [t[1] for t in result['bridge_sequence']]
    name = case['name']
    if name.startswith('lower-') or name == 'cross-22':
        if not states or states[-1] != DOWN or result['bridge_sequence'][-1][3] != 233:
            bad.append(f'bridge did not end lowered (state 1, anim 233): {result["bridge_sequence"]}')
    if name == 'toggle-22':
        order = [s for s in states]
        if order[-1] != UP or DOWN not in order:
            bad.append(f'bridge did not go down and back up: {result["bridge_sequence"]}')
    if name == 'cross-22':
        if result['inventory_after'] == result['inventory_before']:
            bad.append('artifact not collected')
        if result['world_pairs'] != [1]:
            bad.append(f'world validator verdicts {result["world_pairs"]} (expected only 1)')
        last = result['lara_last']
        if not last or last[1] > 51200:
            bad.append(f'Lara did not reach the deck: {last}')
    if name == 'raised-22':
        if result['lara_y_range'][1] > case['start'][2] + 64:
            bad.append(f'Lara fell: y range {result["lara_y_range"]}')
    return bad


def build_cases(env):
    cases = [lower_case(env, b) for b in BRIDGES]
    cases.append(toggle_case(env))
    for make in (cross_case, raised_case):
        case = make(env)
        cases.append(case if case is not None else dict(name=make.__name__, missing=True))
    return cases


def reach(env):
    """Which switch fronts can Lara walk to from which (the closed-loop driver's route over standable sector centres,
    steps up to 800 units; no jump, no climb)? Door 68 needs all four artifacts, so all four switches pulled by one Lara."""
    starts = {k: start_at_switch(env, k) for k in BRIDGES.values()}
    rows = {}
    for a, sa in starts.items():
        rows[a] = {}
        for b, sb in starts.items():
            if a != b:
                path = d.route(env, sa[0], sa[1], sa[2], sa[3],
                               lambda r, x, y, z, g=sb: abs(x - g[1]) < 900 and abs(z - g[3]) < 900, limit=6000, step=800)
                rows[a][b] = None if path is None else len(path)
    return starts, rows


def regions(env):
    """Walkable regions (steps up of at most 800, any drop, portals followed) from each switch front, first with the decks as
    they are (pits), then with all four lowered (the deck cells of each bridge, specification drawbridge.md 4.1, at the
    bridge's y). Door 68 needs all four artifacts, items 15 to 18, so one Lara walking from front to front and bridge to
    bridge; `artifacts` lists the ones a region reaches."""
    I = env.level.data['items']
    cell = lambda a: a >> 10
    off = {0: (0, -1), 16384: (-1, 0), -32768: (0, 1), -16384: (1, 0)}
    starts = {k: start_at_switch(env, k) for k in BRIDGES.values()}

    def decks(bridges):
        out = {}
        for b in bridges:
            it = I[b]
            dx, dz = off[d.yaw(it['angle'])]
            for k in (1, 2):
                out[(cell(it['x']) + dx * k, cell(it['z']) + dz * k)] = (16, it['y'])
        return out

    def region(start, deckset):
        import collections
        first = d.ground(env, start[0], start[1], start[3], None, clear=1024)
        k0 = (cell(start[1]), cell(start[3]))
        seen = {k0: first}
        queue = collections.deque([k0])
        while queue:
            k = queue.popleft()
            r, y = seen[k]
            for dx, dz, _ in d.DIRS:
                nk = (k[0] + dx, k[1] + dz)
                if nk in seen:
                    continue
                g = deckset.get(nk) or d.ground(env, r, nk[0] * 1024 + 512, nk[1] * 1024 + 512, None, clear=1024)
                if g is None or g[1] < y - 800:
                    continue
                seen[nk] = g
                queue.append(nk)
        return seen

    out = {}
    for name, bridges in (('decks raised', []), ('all four lowered', list(BRIDGES))):
        ds = decks(bridges)
        rows = {}
        for k, st in starts.items():
            c = region(st, ds)
            rows[k] = dict(cells=len(c), fronts=[q for q, t in starts.items() if (cell(t[1]), cell(t[3])) in c],
                           artifacts=[a for a in (15, 16, 17, 18) if (cell(I[a]['x']), cell(I[a]['z'])) in c])
        out[name] = rows
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--old-oracle', type=Path, default=d.ORACLE)
    p.add_argument('--save-masks', type=Path, help='also write each case as an input file')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('action', choices=['cases', 'negative', 'all', 'reach'], nargs='?', default='all')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    env = d.Env('level8b', args.executable, oracle=args.oracle)
    old = d.Env('level8b', args.executable, oracle=args.old_oracle)
    if args.action == 'reach':
        starts, rows = reach(env)
        print('switch fronts', starts)
        print('walking route length in cells, switch -> switch (None: no route):', json.dumps(rows))
        print('regions (any drop, steps up to 800):', json.dumps(regions(env)))
        return 0
    report, failed = {}, False
    for case in build_cases(env):
        name = case['name']
        entry = {}
        if case.get('missing'):
            entry['status'] = 'could not be generated'
            failed = True
        else:
            if args.save_masks:
                args.save_masks.mkdir(parents=True, exist_ok=True)
                (args.save_masks / f'{name}.masks').write_text(d.masks_text(case['start'], case['masks']))
            if args.action in ('cases', 'all'):
                result = run_case(env, case)
                bad = verdicts(case, result)
                entry.update(result, expectations_failed=bad)
                failed |= bool(bad)
            if args.action in ('negative', 'all'):
                neg = d.compare(old, case['start'], case['masks'], ticks=len(case['masks']))
                entry['negative'] = neg
                ok = neg.get('status') == 'different'
                entry['negative_differs'] = ok
                failed |= not ok
        report[name] = entry
        print(name, entry.get('status'), 'expect-fail=%s' % entry.get('expectations_failed'),
              'neg=%s@%s' % (entry.get('negative', {}).get('status'), entry.get('negative', {}).get('tick')))
    (args.out / 'drawbridge.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print('drawbridge', 'FAIL' if failed else 'PASS')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
