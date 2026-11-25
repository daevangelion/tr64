#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Lava particles, burning without a timeout and the falling ceiling's damage, oracle against port, schema 4
(oracle host.patch hunks 22, 23, 24; docs/specs/wall/objects-missing.md 12, 15.2, 16.1, 16.2).

particles  LEVEL10A/10B/10C: Lara placed on or next to a lava emitter (item type 177) and standing; the emitter makes one
           particle a frame, it flies (fall speed rising 6 a frame), is removed at floor or ceiling, or when it is
           within 200 of Lara it takes 10 hit points. Reported: the particles alive at once (most seen), the pool's free
           slots, whether Lara lost 10 a time. The oracle without hunk 22 (`--old-oracle`) shows no particle at all.
burn       LEVEL6 flame emitter 35: Lara on its pad catches fire and stands (no water): she burns until she dies
           (hunk 23); on the oracle without it the burning stops after 90 ticks and she survives.
ceiling    LEVEL3B falling ceilings 24, 25, 26: activated by the placed-state directive `activate` (the only trigger is the
           Scion's pickup trigger and Lara is held in the pickup animation until every slab has landed), Lara placed under
           the slab and standing: 300 hit points on each tick of contact (hunk 24), not 1,000 at the first contact;
           `--control-oracle` (test/oracle/control.sh no-hunk-24) is the negative control.

Run with PYTHONPATH=tools/cook:test/game:
  burn.py --executable HOST --oracle ORACLE --old-oracle OLD --control-oracle CONTROL --out DIR [particles|burn|ceiling|all]
"""
import argparse
import itertools
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
import pickups
from directed import FORWARD, NONE

PARTICLE, EMITTER = 176, 177
LIMIT_TRIES = 24


def series(env, start, masks):
    rows = d.probe(env, start, masks, want='all')
    health = [r['lara']['health'] for r in rows if r['lara']]
    particles = [sum(1 for a in r['active'] if a['type'] == PARTICLE) for r in rows]
    return health, particles


def particles(args, report):
    ok = True
    for level in ('level10a', 'level10b', 'level10c'):
        env = d.Env(level, args.executable, oracle=args.oracle)
        old = d.Env(level, args.executable, oracle=args.old_oracle)
        items = env.level.data['items']
        row = dict(emitters=sum(1 for it in items if it['type'] == EMITTER),
                   free_slots=256 - len(items), best=None)
        for index, it in enumerate(items):
            if it['type'] != EMITTER:
                continue
            for dx, dz in ((0, 0), (1024, 0), (-1024, 0), (0, 1024), (0, -1024)):
                x, z = it['x'] + dx, it['z'] + dz
                g = d.ground(env, it['room'], x, z, None, clear=768)
                if g is None:
                    continue
                start = (g[0], x, g[1], z, 0)
                masks = [NONE] * 160
                try:
                    health, alive = series(env, start, masks)
                except ValueError:
                    continue
                if max(alive) == 0:
                    continue
                cand = dict(emitter=index, start=start, most_particles=max(alive), first_particle_tick=alive.index(max(alive)),
                            damage=[1000 - h for h in health if h < 1000][:1], min_health=min(health))
                if row['best'] is None or (cand['damage'], cand['most_particles']) > (row['best']['damage'], row['best']['most_particles']):
                    row['best'] = cand
                if cand['damage']:
                    break
            if row['best'] and row['best']['damage']:
                break
        if row['best']:
            res = d.compare(env, row['best']['start'], [NONE] * 160, ticks=160)
            _, none_old = series(old, row['best']['start'], [NONE] * 160)
            row.update(compare=res.get('status'), particles_without_hunk=max(none_old))
            ok &= res.get('status') == 'equal' and max(none_old) == 0
        else:
            row['note'] = 'no emitter of this level made a particle in the placements tried'
        report['particles-' + level] = row
        print('particles', level, json.dumps(row, default=str), flush=True)
    return ok


def pad_starts(env, ids, backs=(2048, 3072)):
    for ri, _, tt, acts, x, z in pickups.trigger_sectors(env.level):
        if tt not in (0, 1, 5) or not any(a == 0 and arg in ids for a, arg in acts):
            continue
        g = d.ground(env, ri, x, z, None, clear=1024)
        if not g:
            continue
        yield 'pad', (g[0], x, g[1], z, 0), [NONE] * 3
        for dx, dz, ang in d.DIRS:
            for back in backs:
                h = d.ground(env, ri, x - dx * back, z - dz * back, g[1], 300, clear=1024)
                if h:
                    yield 'run', (h[0], x - dx * back, h[1], z - dz * back, ang), [FORWARD] * 140 + [NONE] * 160


def burn(args, report, level, ids, stand, want):
    """Lara starts on the pad that triggers the item, then walks to the item's cell and stands there."""
    env = d.Env(level, args.executable, oracle=args.oracle)
    old = d.Env(level, args.executable, oracle=args.old_oracle)
    d.Script.LIMIT = 600
    tried = 0
    for kind, start, lead in pad_starts(env, ids, backs=(2048,)):
        if tried >= LIMIT_TRIES:
            break
        for idx in sorted(ids):
            it = env.level.data['items'][idx]
            s = d.Script(env, start)
            s.hold(NONE, 1)
            s.masks = list(lead)
            try:
                s.walk_to(it['x'], it['z'], tol=200, limit=200, run=False)
            except ValueError:
                continue
            s.hold(NONE, 600 - len(s.masks))
            masks = list(s.masks)
            tried += 1
            try:
                health, _ = series(env, start, masks)
            except ValueError:
                continue
            if not want(health):
                continue
            res = d.compare(env, start, masks, ticks=len(masks))
            oh, _ = series(old, start, masks)
            steps = [a - b for a, b in zip(health, health[1:]) if a != b]
            osteps = [a - b for a, b in zip(oh, oh[1:]) if a != b]
            row = dict(item=idx, placement=kind, start=start, ticks=len(masks), compare=res.get('status'),
                       dead_tick=next((i for i, h in enumerate(health) if h <= 0), None), steps_first=steps[:6],
                       count=len(steps), without_hunk=dict(min_health=min(oh), dead_tick=next((i for i, h in enumerate(oh) if h <= 0), None),
                                                           steps_first=osteps[:6], count=len(osteps)))
            report['burn'] = row
            print('burn', json.dumps(row, default=str), flush=True)
            return res.get('status') == 'equal' and (steps != osteps)
    report['burn'] = dict(note='no placement showed it', tried=tried)
    print('burn', 'no placement showed it, tried', tried, flush=True)
    return False


def ceiling(args, report):
    """LEVEL3B falling ceilings 24, 25 and 26 (room 5). Their only trigger is the Scion's pickup trigger, and Lara is held
    in the pickup animation for 85 ticks after it, by which time every slab has landed (tick 149..165 against collection at
    113): no play reaches a slab as it falls. So the slab is activated by the placed-state directive `activate INDEX 0`
    (what the ACTIVATE_OBJECT trigger action does) with Lara placed on the floor under it, standing. The slab hangs
    shaking from tick 3 (animation 249, 'fall' state 1), falls, and each tick it touches her costs 300 hit points (hunk 24);
    the control oracle (`test/oracle/control.sh no-hunk-24`) kills her at the first contact."""
    env = d.Env('level3b', args.executable, oracle=args.oracle)
    control = d.Env('level3b', args.executable, oracle=args.control_oracle)
    I = env.level.data['items']
    rows_out, ok = [], True
    for idx in (24, 25, 26):
        it = I[idx]
        picked = None
        # Lara under the slab: its box (first frame) is x -574..893, z -650..248 in the item's frame, 100 added all round
        for dx, dz in itertools.product((0, 150, -150, 300, -300), repeat=2):
            x, z = it['x'] + dx, it['z'] + dz
            g = d.ground(env, it['room'], x, z, None, clear=1024)
            if g is None:
                continue
            start = (g[0], x, g[1], z, 0)
            setup = [('activate', idx, 0)]
            masks = [NONE] * 120
            try:
                health, _ = series_setup(env, start, masks, setup)
            except ValueError:
                continue
            steps = [a - b for a, b in zip(health, health[1:]) if a != b]
            if steps:
                picked = (start, setup, masks, health, steps)
                break
        if picked is None:
            rows_out.append(dict(item=idx, note='no placement under the slab was hit'))
            ok = False
            continue
        start, setup, masks, health, steps = picked
        res = d.compare(env, start, masks, ticks=len(masks), setup=setup)
        ch, _ = series_setup(control, start, masks, setup)
        csteps = [a - b for a, b in zip(ch, ch[1:]) if a != b]
        rows = d.probe(env, start, masks, setup=setup, want='all')
        fall = [r['tick'] for r in rows if any(a['index'] == idx and a['state'] == 1 for a in r['active'])]
        row = dict(item=idx, placement=start, compare=res.get('status'), fall_state_ticks=(fall[0], fall[-1]) if fall else None,
                   hit_ticks=[i for i, (a, b) in enumerate(zip(health, health[1:]), 1) if a != b], health_steps=steps,
                   dead_tick=next((i for i, h in enumerate(health) if h <= 0), None),
                   control=dict(health_steps=csteps, dead_tick=next((i for i, h in enumerate(ch) if h <= 0), None)))
        rows_out.append(row)
        print('ceiling', json.dumps(row, default=str), flush=True)
        ok &= res.get('status') == 'equal' and steps[:3] == [300, 300, 300] and csteps[:1] == [1000] and csteps != steps
    report['ceiling'] = rows_out
    return ok


def series_setup(env, start, masks, setup):
    rows = d.probe(env, start, masks, want='all', setup=setup)
    return [r['lara']['health'] for r in rows if r['lara']], rows


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--old-oracle', type=Path, default=d.ORACLE)
    p.add_argument('--control-oracle', type=Path, help='the oracle with hunk 24 taken out (test/oracle/control.sh no-hunk-24), for the ceiling run')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('action', choices=['particles', 'burn', 'ceiling', 'all'], nargs='?', default='all')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    report, ok = {}, True
    if args.action in ('particles', 'all'):
        ok &= particles(args, report)
    if args.action in ('burn', 'all'):
        env = d.Env('level6', args.executable, oracle=args.oracle)
        ids = {35}
        ok &= burn(args, report, 'level6', ids, 400, lambda h: sum(1 for a, b in zip(h, h[1:]) if a - b == 5) > 90)
    if args.action in ('ceiling', 'all'):
        ok &= ceiling(args, report)
    (args.out / 'burn.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print('burn', 'PASS' if ok else 'FAIL')
    return int(not ok)


if __name__ == '__main__':
    raise SystemExit(main())
