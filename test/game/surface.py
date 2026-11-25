#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Surfacing from inside a water room: pitch and roll are zero on the first SURFACE tick and afterwards.

Oracle host.patch hunk 29. The fork added a branch to `updateWaterState` (UNDER) that put Lara on the surface while her
room was still a water room, up to 256 units before she crossed into the air room above, and left the swimming pitch
and roll set (the original, and the branch for the air room, zero both); it also did it under a solid water-room
ceiling (the water level of a water room with no room above is its ceiling), so she could "surface" inside the rock.
Hunk 29 restores the original rule: she leaves UNDER only once her room is no longer a water room
(docs/specs/wall/lara-and-enemies.md 1.3.1).

For each level with water, open surface cells (a water room sector whose room above is an air room, room below none,
at least 1,500 deep) are chosen over the rooms. Lara is placed 450 above the floor of the cell, dives (the placed start
enters UNDER), holds BACK+JUMP until her pitch has risen to one of the targets and then JUMP alone, so she swims up
at that pitch; for the roll cases LEFT or RIGHT is added for the last fourteen ticks before the surface (roll builds to about 20 degrees; the control surfaces earlier). The script is built
closed-loop on the oracle and cut at its first SURFACE tick, followed by 30 ticks of nothing, 20 of FORWARD (surface
swim) and 30 of nothing. Checks, per case:
  equal      oracle and port: every schema-3 record equal;
  zero       every record of the oracle's trace with water state SURFACE has pitch (rot x) and roll (rot z) 0;
  meaningful the tick before the first SURFACE tick has a non-zero pitch or roll (so the zeroing is observable);
  control    the oracle without hunk 29 (`test/oracle/control.sh no-hunk-29`) on the same inputs: its first SURFACE tick
             has a non-zero pitch (the defect), and the port differs from it.
`--ps-executable` (a host built with the PlayStation pack, same sources) runs the same inputs: the zero check on its own
trace and equality with the PC port (differences are reported, not failed: the PC and PlayStation level tables differ,
see README `editions.py`).

Run with PYTHONPATH=tools/cook:test/game:
  surface.py --executable HOST --oracle ORACLE --control-oracle CONTROL --out DIR [--ps-executable HOSTPS]
             [--levels gym level2 ...] [--cells N] [--jobs N]
"""
import argparse
import collections
import concurrent.futures
import functools
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from stream import Run

BACK, JUMP, LEFT, RIGHT, FORWARD = d.BACK, d.JUMP, d.LEFT, d.RIGHT, d.FORWARD
LEVELS = ('gym', 'level2', 'level3a', 'level5', 'level6', 'level8b', 'level10a')
PITCHES = (5460, 10920, 15470)               # 30, 60 and 85 degrees (180 units a degree)
ROLLS = (('none', 0), ('left', LEFT), ('right', RIGHT))
WATER_SURFACE = 2


def open_cells(env, per_room=2):
    """Centres of water-room sectors with an air room above, nothing below, 1,500 or more deep."""
    level, rooms = env.level, env.level.rooms
    found = []
    for ri, room in enumerate(rooms):
        if not room.flags & 1:
            continue
        mine = []
        for index, s in enumerate(room.sectors):
            above = s['room_above']
            if above == 255 or rooms[above].flags & 1 or s['floor'] == -127 or s['room_below'] != 255:
                continue
            x = room.info['x'] + (index // room.z_sectors) * 1024 + 512
            z = room.info['z'] + (index % room.z_sectors) * 1024 + 512
            floor = d.height(level, ri, s, x, z)
            if floor is None or floor - s['ceiling'] * 256 < 1500:
                continue
            mine.append(dict(room=ri, x=x, z=z, floor=floor, surface=s['ceiling'] * 256))
        step = max(1, len(mine) // per_room)
        found.extend(mine[::step][:per_room])
    return found


def pick(cells, count):
    step = max(1, len(cells) // count)
    return cells[::step][:count]


def build(env, cell, target, roll, rollticks):
    """(start, masks) cut at the oracle's first SURFACE tick, or None."""
    start = (cell['room'], cell['x'], cell['floor'] - 450, cell['z'], 0)
    try:
        rows = d.probe(env, start, [BACK | JUMP] * 120, want='all')
    except ValueError:
        return None
    rise = next((r['tick'] for r in rows if r['lara']['rot'][0] >= target), None)
    if rise is None:
        return None
    total = 330
    masks = [BACK | JUMP] * rise + [JUMP] * (total - rise)
    first = surface_tick(d.probe(env, start, masks, want='all'))
    if first is None:
        return None
    if roll:
        for t in range(max(1, first - rollticks), first):
            masks[t - 1] |= roll
        first = surface_tick(d.probe(env, start, masks, want='all'))
        if first is None:
            return None
    return start, masks[:first] + [0] * 30 + [FORWARD] * 20 + [0] * 30


def surface_tick(rows):
    return next((r['tick'] for r in rows if r['lara']['water'] == WATER_SURFACE), None)


def zero_violations(rows):
    return [(r['tick'], r['lara']['rot']) for r in rows if r['lara']['water'] == WATER_SURFACE and
            (r['lara']['rot'][0] or r['lara']['rot'][2])]


def host_rows(executable, level, start, masks):
    path = d.scratch('ps.masks')
    path.write_text(d.masks_text(start, masks))
    run = Run([str(executable), '--game-level', level, '--game-input', str(path), '--game-ticks', str(len(masks)),
               '--game-trace-only', '--game-trace-schema', str(d.SCHEMA)], '--game-trace')
    lines = list(run.trace)
    code, log = run.finish()
    if code:
        raise ValueError(f'host failed: {log[-300:]}')
    return lines


@functools.lru_cache(maxsize=None)
def environment(level, executable, oracle):
    return d.Env(level, executable, oracle=oracle)


def one(args, level, cell, target, rname, roll, ps):
    env = environment(level, args.executable, args.oracle)
    control = environment(level, args.executable, args.control_oracle)
    name = f'{level}-room{cell["room"]}-{cell["x"]}-{cell["z"]}-p{target}-{rname}'
    built = build(env, cell, target, roll, 14 if roll else 0)
    if built is None:
        return dict(name=name, status='no-script')
    start, masks = built
    rows = d.probe(env, start, masks, want='all')
    first = surface_tick(rows)
    before = rows[first - 1]['lara']['rot']
    out = dict(name=name, status='ok', first=first, before=before, start=list(start), ticks=len(masks),
               equal=d.compare(env, start, masks)['status'])
    out['zero_violations'] = zero_violations(rows)
    out['meaningful'] = bool(before[0] or before[2])
    try:
        crows = d.probe(control, start, masks, want='all')
        cfirst = surface_tick(crows)
        out['control_first'] = cfirst
        out['control_rot'] = crows[cfirst]['lara']['rot'] if cfirst is not None else None
        out['control_pos_y'] = crows[cfirst]['lara']['pos'][1] if cfirst is not None else None
        out['control_differs'] = d.compare(control, start, masks)['status']
    except ValueError as error:
        out['control_error'] = str(error)[:200]
    if ps is not None:
        lines = host_rows(ps, level, start, masks)
        prows = [json.loads(line) for line in lines]
        out['ps_zero_violations'] = zero_violations(prows)
        out['ps_first'] = surface_tick(prows)
        pc = host_rows(args.executable, level, start, masks)
        out['ps_equal_pc'] = pc == lines
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--control-oracle', type=Path, required=True, help='the oracle without hunk 29 (control.sh no-hunk-29)')
    p.add_argument('--ps-executable', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--levels', nargs='*', default=list(LEVELS))
    p.add_argument('--cells', type=int, default=4, help='surface cells per level')
    p.add_argument('--jobs', type=int, default=3)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    jobs = []
    for level in args.levels:
        env = environment(level, args.executable, args.oracle)
        for cell in pick(open_cells(env), args.cells):
            for target in PITCHES:
                for rname, roll in ROLLS:
                    jobs.append((level, cell, target, rname, roll))
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for res in pool.map(lambda j: one(args, *j, args.ps_executable), jobs):
            results.append(res)
    (args.out / 'report.json').write_text(json.dumps(results, indent=1))
    table = collections.defaultdict(collections.Counter)
    failed = []
    for r in results:
        level = r['name'].split('-')[0]
        t = table[level]
        t['cases'] += 1
        if r['status'] != 'ok':
            t['no-script'] += 1
            continue
        t['equal'] += r['equal'] == 'equal'
        t['zero'] += not r['zero_violations']
        t['meaningful'] += r['meaningful']
        t['control-surfaces'] += r.get('control_first') is not None
        t['control-pitch-nonzero'] += bool(r.get('control_rot') and r['control_rot'][0])
        t['control-roll-nonzero'] += bool(r.get('control_rot') and r['control_rot'][2])
        t['control-differs'] += r.get('control_differs') == 'different'
        if args.ps_executable:
            t['ps-zero'] += not r['ps_zero_violations']
            t['ps-equal-pc'] += bool(r['ps_equal_pc'])
        if r['equal'] != 'equal' or r['zero_violations']:
            failed.append(r)
    for level, t in table.items():
        print(level, dict(t))
    ok = [r for r in results if r['status'] == 'ok']
    # the control must show the defect: a non-zero pitch on its first SURFACE tick in most meaningful cases, on every level
    weak = [lv for lv, t in table.items() if t['cases'] - t['no-script'] and not t['control-pitch-nonzero']]
    for r in failed:
        print('FAIL', r['name'], r['equal'], r['zero_violations'][:3])
    for lv in weak:
        print('FAIL control shows no defect on', lv)
    if not ok:
        print('FAIL no script reached the surface')
    if failed or weak or not ok or any(r.get('control_differs') not in (None, 'different') for r in ok):
        sys.exit(1)


if __name__ == '__main__':
    main()
