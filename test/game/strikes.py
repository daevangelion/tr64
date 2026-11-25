#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Thor's hammer (LEVEL4 item 63) and the lightning emitters (LEVEL4 item 62, LEVEL10C item 107) hurting Lara, oracle
against port, schema 4 (docs/specs/wall/objects-missing.md sections 12 and 13; hazards.py had not shown them).

thor-stand    LEVEL4: Lara placed on the pad cell (56, 39) that triggers the hammer; the landing point 3072 ahead of the
              handle (angle 180 degrees: -z, hunk 27) is that cell's centre. The handle goes tease (1) then swing (2); once the
              swing is more than 30 frames in she is killed (hit points -1, special animation).
thor-leave    the control: the same placement, but she runs off the landing zone (more than 520 away) as soon as the
              handle is swinging: she survives, the hammer finishes (state 3).
lightning-4-N LEVEL4: Lara on a pad of the emitter's room. The emitter has 6 meshes (a targeting one): within 2560 of her it
              strikes her, 400 hit points per bolt (one bolt about every 65 ticks). `near` is 1,450 away, `far` 4,300 away
              (still on a pad: the bolts strike the emitter's joints, not her).
lightning-10c LEVEL10C: the heavy trigger of the Scion target fires lightning 107 (a 1-mesh, "no target" emitter), door 116 and the
              earthquake 108. The target (item 105) is preset to one hit (`item 105 1`, placed state); Lara shoots it with the
              pistols from the cell south of the pedestal. The emitter hangs 1,792 above the room's floor
              (1,280 above the pedestal): standing, neither the fixed game's rule (her origin within a sphere of 1024 of the
              emitter) nor the specification's near test (the emitter's height between the top of her box and its bottom + 100)
              can hold, so the bolt does not hurt her (lightning-10c-south).
lightning-10c-jump  the same emitter woken by the placed-state `activate 107 0` directive, Lara standing on the pedestal cell
              (floor 512 above the room's) 512 west of it and jumping straight up: a jump rises 954, so at the top her origin is
              838 below the emitter (inside the source's sphere of 1024) and the top of her box is 25 above it (inside the
              specification's band for the two ticks at the top). The jump is timed (started at tick 91, found closed-loop on
              the oracle) so that the bolt switches on (tick 125) while she is at the top: she loses 400. So the original's rule
              can reach her, but only in the air, near the top of a jump, at the moment the bolt switches on.

Run with PYTHONPATH=tools/cook:test/game:
  strikes.py --executable HOST --oracle ORACLE --old-oracle OLD --out DIR [thor|lightning|all]
"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from directed import ACTION, FORWARD, JUMP, NONE, WEAPON

C = lambda cell: cell * 1024 + 512


class Watch:
    def __init__(self, indices):
        self.indices = set(indices)
        self.rows = []

    def __call__(self, row):
        lara = row['lara']
        self.rows.append(dict(tick=row['tick'], health=lara['health'] if lara else None, lara=tuple(lara['pos']) if lara else None,
                              state=lara['state'] if lara else None,
                              items={a['index']: (a['state'], a['goal'], a['anim'], a['frame']) for a in row['active'] if a['index'] in self.indices}))

    def feed(self, rows):
        for r in rows:
            self(r)
        return self

    def steps(self):
        h = [r['health'] for r in self.rows]
        return [a - b for a, b in zip(h, h[1:]) if a != b]

    def dead_tick(self):
        return next((r['tick'] for r in self.rows if r['health'] is not None and r['health'] <= 0), None)

    def seq(self, index):
        out = []
        for r in self.rows:
            it = r['items'].get(index)
            if it and (not out or out[-1][1:] != it[:3]):
                out.append((r['tick'],) + it[:3])
        return out


def compare(env, case, indices):
    watch = Watch(indices)
    try:
        result = d.compare(env, case['start'], case['masks'], ticks=len(case['masks']), observe=watch, setup=case.get('setup', ()))
    except ValueError as error:
        return dict(status='error', why=str(error)[:200]), watch
    return result, watch


def thor_cases(env):
    I = env.level.data['items']
    h = I[63]
    x, z = h['x'], h['z'] - 3072          # facing 180 degrees: the landing point is 3072 toward -z
    g = d.ground(env, 25, x, z, None, clear=512)
    start = (g[0], x, g[1], z, -32768)
    stand = dict(name='thor-stand', start=start, masks=[NONE] * 300)
    s = d.Script(env, start)
    s.hold(NONE, 1)
    if not s.until(lambda r: any(a['index'] == 63 and a['state'] == 2 for a in r['active']), NONE, 200, chunk=1):
        raise ValueError('the handle never swung')
    s.hold(FORWARD, 40)
    s.hold(NONE, 260 - 40)
    s.pad(300)
    leave = dict(name='thor-leave', start=start, masks=list(s.masks))
    return [stand, leave]


def lightning4_cases(env):
    em = env.level.data['items'][62]
    out = []
    for name, (cx, cz) in (('near', (49, 41)), ('far', (47, 38))):
        g = d.ground(env, 24, C(cx), C(cz), None, clear=512)
        out.append(dict(name=f'lightning-4-{name}', start=(g[0], C(cx), g[1], C(cz), 0), masks=[NONE] * 400))
    return out


def lightning10c_cases(env):
    """Start on the pad (50, 8) that wakes the target (item 105), walk to the cell south of the Scion's pedestal (52, 12), face the target, draw the pistols and fire until it is down."""
    out = []
    g0 = d.ground(env, 50, 51712, 8704, None, clear=512)
    for name, cell in (('south', (52, 12)),):
        s = d.Script(env, (g0[0], 51712, g0[1], 8704, 0), setup=[('item', 105, 1)])
        s.hold(NONE, 2)
        if not s.walk_route(lambda r, x, y, z: (x >> 10, z >> 10) == cell, limit=400):
            raise ValueError(f'walk to {name} failed')
        s.hold(NONE, 10)
        d.face(s, 53760, 11776, tol=1000, limit=60)
        s.hold(WEAPON, 2)
        s.hold(NONE, 40)
        s.hold(ACTION, 90)
        s.hold(NONE, 60)
        out.append(dict(name=f'lightning-10c-{name}', start=s.start, setup=[('item', 105, 1)], masks=list(s.masks)))
    g = d.ground(env, 50, 53760 - 512, 11776, None, clear=1024)
    out.append(dict(name='lightning-10c-jump', start=(g[0], 53760 - 512, g[1], 11776, 16384), setup=[('activate', 107, 0)],
                    masks=[NONE] * 91 + [JUMP] * 3 + [NONE] * 66))
    return out


def verdict(case, result, watch):
    if result.get('status') != 'equal':
        return [f"not equal: {result.get('status')} {result.get('field', '')} {result.get('expected', '')} {result.get('actual', '')}"]
    bad = []
    n = case['name']
    steps, dead = watch.steps(), watch.dead_tick()
    if n == 'thor-stand':
        if dead is None:
            bad.append('Lara survived the hammer')
        seq = [s[1:] for s in watch.seq(63)]
        if not {0, 1, 2} <= {x[0] for x in seq}:
            bad.append(f'hammer sequence {seq}')
    if n == 'thor-leave':
        if dead is not None or watch.rows[-1]['health'] != 1000:
            bad.append(f'Lara hurt: dead={dead}, health {watch.rows[-1]["health"]}')
        if 3 not in [s[1] for s in watch.seq(63)]:
            bad.append(f'the hammer did not finish: {watch.seq(63)}')
    if n == 'lightning-4-near':
        if not steps or steps[0] != 400:
            bad.append(f'no 400 strike: {steps}')
    if n == 'lightning-4-far':
        if steps:
            bad.append(f'Lara hurt at 4,300: {steps}')
        if not watch.seq(62):
            bad.append('the emitter never ran')
    if n.startswith('lightning-10c'):
        if not watch.seq(107):
            bad.append('lightning 107 never ran')
        if n == 'lightning-10c-jump':
            if steps != [400]:
                bad.append(f'expected one 400 strike at the top of the jump: {steps}')
        elif steps:
            bad.append(f'Lara hurt: {steps}')
    return bad


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--old-oracle', type=Path, default=d.ORACLE, help='the oracle without hunk 27: the negative control of thor-stand')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('action', choices=['thor', 'lightning', 'all'], nargs='?', default='all')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    d.Script.LIMIT = 600
    jobs = []
    if args.action in ('thor', 'all'):
        env = d.Env('level4', args.executable, oracle=args.oracle)
        jobs += [(env, c, (63, 62)) for c in thor_cases(env)]
        jobs += [(env, c, (62,)) for c in lightning4_cases(env)] if args.action == 'all' else []
    if args.action in ('lightning', 'all'):
        if args.action == 'lightning':
            env4 = d.Env('level4', args.executable, oracle=args.oracle)
            jobs += [(env4, c, (62,)) for c in lightning4_cases(env4)]
        env = d.Env('level10c', args.executable, oracle=args.oracle)
        jobs += [(env, c, (107,)) for c in lightning10c_cases(env)]
    report, ok = [], True
    for env, case, indices in jobs:
        result, watch = compare(env, case, indices)
        bad = verdict(case, result, watch)
        row = dict(name=case['name'], status=result.get('status'), ticks=len(case['masks']), failures=bad,
                   health_steps=watch.steps(), dead_tick=watch.dead_tick(),
                   items={i: watch.seq(i) for i in indices})
        if case['name'] == 'thor-stand' and not bad:
            # negative control: the oracle without hunk 27 never strikes the pad cell (the strike point is inside the rock)
            old = Watch((63,)).feed(d.probe(d.Env(env.name, args.executable, oracle=args.old_oracle), case['start'], case['masks'], want='all'))
            row['previous_oracle'] = dict(dead_tick=old.dead_tick(), health_steps=old.steps(), final_health=old.rows[-1]['health'],
                                          hammer=[x[1:] for x in old.seq(63)])
            if old.dead_tick() is not None or old.steps():
                bad.append('the oracle without hunk 27 also hurts her')
                row['failures'] = bad
        report.append(row)
        ok &= not bad
        print(json.dumps(row, default=str), flush=True)
    (args.out / 'strikes.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print('strikes', 'PASS' if ok else 'FAIL')
    return int(not ok)


if __name__ == '__main__':
    sys.exit(main())
