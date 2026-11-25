#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Cutscene levels, oracle against port: full length, and the transitions around them.

Run from the repository root with PYTHONPATH=tools/cook. Both programs stream
schema-3 records through separate pipes; nothing but the input and a small JSON
report is written. A cutscene level has no Lara: its records carry the
cinematic camera (`pos`, `room`, `targetAngle`, `angle`, `timer`, `eye`).

  cuts.py --executable HOST --oracle ORACLE --out DIR full [--mode zero|random|both]
      each of cut1..cut4 from its own start for its whole cinematic frame table
      (the table length is the tick count: the level ends on the last tick and
      the last record is already the next campaign level's first tick)
  cuts.py ... enter
      the level before each cutscene, placed on one of its END triggers, for
      600 ticks: the transition into the cutscene and its first ticks
  cuts.py ... larson
      LEVEL3B -> CUT1 -> LEVEL4 played from level3b: Lara is placed under the
      only sector that activates Larson (room 10, below the pool cell
      (43520, 58880)), swims out of the pool, draws the pistols and shoots him
      (`item 30 1`: his health is 1, a test-only directive, so this is placed-state
      evidence); the level ends through his death animation (FX_FINISH_LEVEL),
      the whole 1600-tick cinematic follows and LEVEL4 runs on
  cuts.py ... negative
      the verdict logic rejects: the port one tick short (record count), a changed
      input in the Larson script (first differing record), and the port running
      another cutscene's level (first differing record)
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import sys
import threading
sys.dont_write_bytecode = True
import src_pc
from compare import first_difference
from stream import Run

DATA = Path('/tmp/tr64/data/pc')
CUTS = ['cut1', 'cut2', 'cut3', 'cut4']
# Campaign order (tr1_campaign.h: nextLevel(gLevelID + 1)).
BEFORE = {'cut1': 'level3b', 'cut2': 'level7b', 'cut3': 'level10a', 'cut4': 'level10b'}
AFTER = {'cut1': 'level4', 'cut2': 'level8a', 'cut3': 'level10b', 'cut4': 'level10c'}
CHOICES = [0, 8, 10, 12, 40, 72, 16, 80, 66, 68, 24, 2, 4, 128, 136, 160,
           514, 516, 256, 264, 384, 392, 2048, 32]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def frames(name):
    return len(src_pc.load(DATA / (name + '.phd')).data['cinematic_frames'])


def random_masks(seed, ticks):
    rng = random.Random(seed)
    masks = []
    while len(masks) < ticks:
        mask = rng.choice(CHOICES)
        duration = 1 if mask & (256 | 2048) else rng.randint(1, 45)
        masks.extend([mask] * min(duration, ticks - len(masks)))
    return masks


SCHEMA = 3     # --schema 4 also compares the per-tick `world` field (needs the m6-worldfix oracle)


def longer(run, seconds=600):
    run.timer.cancel()
    run.timer = threading.Timer(seconds, run.kill)
    run.timer.daemon = True
    run.timer.start()


def compare_case(executable, oracle, level_path, masks_path, ticks, name,
                 native_masks=None, native_ticks=None, native_level=None):
    """Run both programs and return a row; the `native_*` arguments make the port run
    something else than the oracle (negative controls only)."""
    reference = Run([str(oracle), '--level', str(level_path), '--input', str(masks_path),
                     '--ticks', str(ticks), '--trace-schema', str(SCHEMA)], '--out')
    native = Run([str(executable), '--game-level', native_level or level_path.stem,
                  '--game-input', str(native_masks or masks_path),
                  '--game-ticks', str(ticks if native_ticks is None else native_ticks),
                  '--game-trace-only', '--game-trace-schema', str(SCHEMA)], '--game-trace')
    longer(reference)
    longer(native)
    tick = 0
    levels = []
    difference = None
    while True:
        a, b = reference.trace.readline(), native.trace.readline()
        if not a or not b:
            break
        level = re.search(rb'"level":(\d+)', a).group(1)
        if not levels or levels[-1][1] != level:
            levels.append((tick, level))
        if a != b:
            try:
                left, right = json.loads(a), json.loads(b)
                field = first_difference(left, right) or ('serialization', 'canonical bytes', 'different bytes')
            except ValueError:
                field = ('malformed record', a[-80:], b[-80:])
            difference = dict(tick=tick, field=field[0], oracle=str(field[1])[:200], native=str(field[2])[:200])
            break
        tick += 1
    if difference is None and (a or b):
        difference = dict(tick=tick, field='record count', oracle=bool(a), native=bool(b))
    oc, olog = reference.finish(kill=difference is not None)
    nc, nlog = native.finish(kill=difference is not None)
    if difference is None and (oc or nc or tick != ticks + 1):
        difference = dict(tick=tick, field='exit', oracle=oc, native=nc,
                          oracle_log=olog[-300:], native_log=nlog[-300:])
    return dict(name=name, records=tick, ticks=ticks, level_changes=[(t, int(v)) for t, v in levels],
                equal=difference is None, difference=difference, native_log=nlog, oracle_log=olog)


# Larson (item 30, type 27) starts hidden; the one trigger that activates him is the sector of room 10
# at (43520, 58880): an ACTIVATE trigger, which has no height condition (room.h checkTrigger), taken
# from the floor lookup of whatever column Lara's centre is in (the room-11 floor opening above it
# and the water room below share it). Phases are (input mask, ticks): surface (BACK+JUMP), swim at
# the surface, climb out onto the rim (FORWARD+ACTION), turn right to face Larson, draw the pistols
# (WEAPON), shoot (ACTION, auto-aim). Larson dies at tick 382, his death animation (230) ends the
# level at tick 530 (FX_FINISH_LEVEL, frame 428), CUT1 runs 1600 ticks, then LEVEL4.
LARSON_HEAD = 'start 10 43520 6500 58880 16384\nitem 30 1\n'
LARSON_PHASES = [(48, 110), (32, 20), (136, 190), (0, 6), (4, 30), (256, 2), (0, 25), (128, 200), (0, 1700)]


def larson_script(phases=LARSON_PHASES):
    return LARSON_HEAD + ''.join(f'{mask}\n' * n for mask, n in phases), sum(n for _, n in phases)


def negative_controls(executable, oracle, out):
    """Each control must be rejected; returns [(label, rejected, detail)]."""
    text, ticks = larson_script()
    base = out / 'larson.masks'
    base.write_text(text)
    # the pistols are never drawn: the same length, Larson survives
    changed = out / 'larson-nodraw.masks'
    changed.write_text(larson_script([(0 if mask == 256 else mask, n) for mask, n in LARSON_PHASES])[0])
    level3b = DATA / 'level3b.phd'
    short = out / 'larson-short.masks'                     # a real one-tick-shorter scene, not a refused file
    short.write_text(text[:text.rstrip('\n').rfind('\n') + 1])
    rows = [
        ('port one tick short', compare_case(executable, oracle, level3b, base, ticks, 'short',
                                             native_masks=short, native_ticks=ticks - 1),
         lambda d: d and d['field'] == 'record count'),
        ('changed input (WEAPON not pressed)', compare_case(executable, oracle, level3b, base, ticks, 'input', native_masks=changed),
         lambda d: d and d['field'] != 'record count'),
    ]
    cut3 = out / 'cut3.masks'
    cut3.write_text('0\n' * frames('cut3'))
    cut3_short = out / 'cut3-short.masks'
    cut3_short.write_text('0\n' * (frames('cut3') - 1))
    rows.append(('port runs cut2 for cut3', compare_case(executable, oracle, DATA / 'cut3.phd', cut3, frames('cut3'), 'level',
                                                          native_level='cut2'), lambda d: d and d['tick'] == 0))
    rows.append(('cut3 port one tick short', compare_case(executable, oracle, DATA / 'cut3.phd', cut3, frames('cut3'), 'short',
                                                           native_masks=cut3_short, native_ticks=frames('cut3') - 1), lambda d: d and d['field'] == 'record count'))
    for path in (base, changed, short, cut3, cut3_short):
        path.unlink()
    return [(label, bool(test(row['difference'])), row['difference']) for label, row, test in rows]


def end_triggers(level):
    """Sectors whose floor data has an END action: (room, x, y, z)."""
    words = level.data['floor_data']
    found = []
    for room_index, room in enumerate(level.rooms):
        for index, sector in enumerate(room.sectors):
            at = sector['floor_index']
            if not at or sector['floor'] == -127:
                continue
            hit = False
            while True:
                command = words[at]
                at += 1
                kind = command & 31
                if kind in (1, 2, 3):
                    at += 1
                elif kind == 4:
                    at += 1
                    while True:
                        action = words[at]
                        at += 1
                        if (action >> 10) & 31 == 7:
                            hit = True
                        if (action >> 10) & 31 in (1, 12):
                            # worldActionEffectiveEnd: the extra word carries the end bit
                            action = (action & 0x7fff) | (words[at] & 0x8000)
                            at += 1
                        if action >> 15:
                            break
                if command >> 15:
                    break
            if hit:
                x = room.info['x'] + (index // room.z_sectors) * 1024 + 512
                z = room.info['z'] + (index % room.z_sectors) * 1024 + 512
                found.append((room_index, x, sector['floor'] * 256 - (sector['room_below'] != 255), z))
    return found


# The Lara special (pickup) animation of these levels carries FX_FINISH_LEVEL; the
# pickup item stands in the source's SCION limit boxes (progression.h LIMIT_SCION*).
FINISH = {'level8c': (143, 'level10a'), 'level10b': (146, 'cut4')}   # LEVEL3B ends through Larson: `larson`
FACINGS = [(0, 0, -1), (16384, -1, 0), (-16384, 1, 0), (-32768, 0, 1)]


def wrap16(v):
    return ((v + 32768) & 0xffff) - 32768


def pickup_placements(level, item_type):
    """Lara placements beside the pickup, facing it, inside the limit box's middle."""
    for item in level.data['items']:
        if item['type'] != item_type:
            continue
        room = level.rooms[item['room']]
        x = wrap16(item['x'] - room.info['x']) + room.info['x']
        z = wrap16(item['z'] - room.info['z']) + room.info['z']
        y = wrap16(item['y'])
        # item local z in [-350, -200] for the pickup, [-862, -200] for the holder pickup
        for back in ([275] if item_type == 143 else [450, 550, 650, 750, 850]):
            for angle, dx, dz in FACINGS:
                yield item['room'], x + dx * back, y + (640 if item_type == 143 else 256), z + dz * back, angle


def finish_input(room, x, y, z, angle, ticks=600):
    # ACTION held for the pickup; after tick 200 it is released every other tick so the
    # statistics page (LEVEL8C -> LEVEL10A) can be dismissed by a fresh press.
    masks = [128] * 200 + [128 if (t // 2) % 2 == 0 else 0 for t in range(ticks - 200)]
    return f'start {room} {x} {y} {z} {angle}\n' + '\n'.join(map(str, masks)) + '\n'


def report_row(row):
    d = row['difference']
    return (f"{row['name']:<28} ticks={row['ticks']:<5} records={row['records']:<5} "
            + ('EQUAL' if row['equal'] else f"FIRST DIFFERENCE tick={d['tick']} field={d['field']}")
            + f" levels={row['level_changes']}"
            + ('' if row['equal'] else '\n  oracle: ' + row['oracle_log'][-200:].strip() + '\n  native: ' + row['native_log'][-300:].strip()))


def keep(path):
    """DIRECTED_SAVE=DIR keeps every script that was run (coverage.py replays them through a gcov build)."""
    target = os.environ.get('DIRECTED_SAVE')
    if target:
        Path(target).mkdir(parents=True, exist_ok=True)
        (Path(target) / path.name).write_bytes(path.read_bytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--executable', required=True, type=Path)
    ap.add_argument('--oracle', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--seed', type=int, default=61)
    ap.add_argument('--cut', action='append', choices=CUTS)
    ap.add_argument('--mode', choices=['zero', 'random', 'both'], default='both')
    ap.add_argument('--schema', type=int, choices=(3, 4), default=3)
    ap.add_argument('action', choices=['full', 'enter', 'finish', 'larson', 'negative'])
    args = ap.parse_args()
    global SCHEMA
    SCHEMA = args.schema
    args.out.mkdir(parents=True, exist_ok=True)
    cuts = args.cut or CUTS
    rows = []
    if args.action == 'larson':
        text, ticks = larson_script()
        path = args.out / 'level3b-larson.masks'
        path.write_text(text)
        row = compare_case(args.executable, args.oracle, DATA / 'level3b.phd', path, ticks, 'level3b>cut1>level4 (Larson)')
        row['input_sha256'] = sha(path)
        keep(path)
        path.unlink()
        rows.append(row)
        print(report_row(row), flush=True)
        cuts = []
    elif args.action == 'negative':
        failed = False
        for label, rejected, detail in negative_controls(args.executable, args.oracle, args.out):
            print(f'negative control {label}: {"REJECTED" if rejected else "ACCEPTED (FAIL)"} {detail}', flush=True)
            failed = failed or not rejected
        return 1 if failed else 0
    elif args.action == 'finish':
        for before, (item_type, after) in FINISH.items():
            level = src_pc.load(DATA / (before + '.phd'))
            done = None
            for placement in pickup_placements(level, item_type):
                path = args.out / f'{before}-finish.masks'
                path.write_text(finish_input(*placement))
                row = compare_case(args.executable, args.oracle, DATA / (before + '.phd'), path, 600,
                                   f'{before}>{after} placement={placement}')
                keep(path)
                path.unlink()
                if row['records'] == 0:
                    continue  # placement rejected by both programs
                if len(row['level_changes']) > 1 or not row['equal']:
                    rows.append(row)
                    print(report_row(row), flush=True)
                    done = row
                    break
            if done is None:
                print(f'{before}: no placement beside the pickup left the level', flush=True)
        cuts = []
    for cut in cuts:
        if args.action == 'full':
            ticks = frames(cut)
            modes = ['zero', 'random'] if args.mode == 'both' else [args.mode]
            for mode in modes:
                masks = [0] * ticks if mode == 'zero' else random_masks(args.seed + ticks, ticks)
                path = args.out / f'{cut}-{mode}.masks'
                path.write_text('\n'.join(map(str, masks)) + '\n')
                row = compare_case(args.executable, args.oracle, DATA / (cut + '.phd'), path, ticks, f'{cut}/{mode}')
                row['input_sha256'] = sha(path)
                rows.append(row)
                print(report_row(row), flush=True)
                keep(path)
                path.unlink()
        else:
            before = BEFORE[cut]
            level = src_pc.load(DATA / (before + '.phd'))
            spots = end_triggers(level)
            if not spots:
                # The fixed game has no other way to end a level (see README).
                print(f'{before}>{cut}: no END trigger sector ({before} ends through an FX_FINISH_LEVEL animation: finish or larson)', flush=True)
                continue
            done = None
            for room, x, y, z in spots:
                path = args.out / f'{before}-end.masks'
                path.write_text(f'start {room} {x} {y} {z} 0\n' + '\n'.join(['0'] * 600) + '\n')
                row = compare_case(args.executable, args.oracle, DATA / (before + '.phd'), path, 600,
                                   f'{before}>{cut} room{room}')
                changed = len(row['level_changes']) > 1
                if changed or not row['equal']:
                    row['input_sha256'] = sha(path)
                    rows.append(row)
                    print(report_row(row), flush=True)
                    done = row
                    keep(path)
                    path.unlink()
                    break
                keep(path)
                path.unlink()
            if done is None:
                raise SystemExit(f'{before}: no END trigger sector placement reached the cutscene')
    (args.out / 'report.json').write_text(json.dumps(
        dict(executable_sha256=sha(args.executable), oracle_sha256=sha(args.oracle),
             rows=[{k: v for k, v in r.items() if k not in ('native_log', 'oracle_log')} for r in rows]), indent=1) + '\n')
    return 0 if all(r['equal'] for r in rows) else 1


if __name__ == '__main__':
    sys.exit(main())
