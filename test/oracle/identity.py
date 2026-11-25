#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare two oracle executables on the same inputs, streaming both traces through pipes.

Run from the repository root with PYTHONPATH=test/game:tools/cook. Nothing is written but a small
JSON report. `same` requires the stdout and every trace byte to be identical; `different` runs the
inputs of a directory and requires the first difference to be at the stated field family.

  identity.py --old OLD --new NEW --out DIR fixtures [--fixtures m2-start dir]
  identity.py --old OLD --new NEW --out DIR campaign --levels level1 --count 200
  identity.py --old OLD --new NEW --out DIR cutscenes
  identity.py --old OLD --new NEW --out DIR different --inputs DIR --level level3b
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import sys
sys.dont_write_bytecode = True
import src_pc
from stream import Run

DATA = Path('/tmp/tr64/data/pc')
CUTS = ['cut1', 'cut2', 'cut3', 'cut4']
CHOICES = [0, 8, 10, 12, 40, 72, 16, 80, 66, 68, 24, 2, 4, 128, 136, 160, 514, 516, 256, 264, 384, 392, 2048, 32]


def module(filename):
    spec = importlib.util.spec_from_file_location(filename[:-3].replace('-', '_'),
                                                  Path(__file__).resolve().parents[1] / 'game' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def run_pair(old, new, level, masks, ticks, schema):
    """First differing record (tick, old line, new line) or None; also compares exit code and stdout."""
    runs = []
    for exe in (old, new):
        runs.append(Run([str(exe), '--level', str(level), '--input', str(masks), '--ticks', str(ticks),
                         '--trace-schema', str(schema)], '--out'))
    tick = 0
    first = None
    while True:
        a, b = runs[0].trace.readline(), runs[1].trace.readline()
        if not a or not b:
            if a or b:
                first = (tick, 'record count', a[:80], b[:80])
            break
        if a != b and first is None:
            first = (tick, 'record', a, b)
            break
        tick += 1
    results = [r.finish(kill=first is not None) for r in runs]
    if first is None and (results[0][0] != results[1][0] or results[0][1] != results[1][1]):
        first = (tick, 'exit/stdout', str(results[0]), str(results[1]))
    return tick, first


def field_of(a, b):
    from compare import first_difference
    return first_difference(json.loads(a), json.loads(b))


def check(rows, label, tick, first):
    rows.append(dict(name=label, records=tick, equal=first is None,
                     first=None if first is None else dict(tick=first[0], what=first[1], field=field_of(first[2], first[3])
                                                           if first[1] == 'record' else None)))
    print(f'{label}: ' + ('IDENTICAL records=%d' % tick if first is None else f'DIFFERENT at tick {first[0]} ({first[1]})'), flush=True)
    return first is None


def fixtures(args):
    manifest = json.loads((args.fixtures / 'manifest.json').read_text())
    rows, ok = [], True
    for row in manifest['fixtures']:
        tick, first = run_pair(args.old, args.new, row['level'], row['input'], row['ticks'], args.schema or 2)
        ok &= check(rows, row['name'], tick, first)
    return rows, ok


def campaign(args):
    random_inputs = module('random-inputs.py')
    rows, ok = [], True
    for name in args.levels.split(','):
        level = src_pc.load(DATA / f'{name}.phd')
        done = ordinal = 0
        while done < args.count and ordinal < args.count * 20:
            text = random_inputs.generated(level, ordinal, args.seed, menus=True)
            ordinal += 1
            if text is None:
                continue
            path = args.out / 'current.masks'
            path.write_text(text)
            tick, first = run_pair(args.old, args.new, DATA / f'{name}.phd', path, 600, 3)
            if tick == 0 and first is not None and first[1] == 'exit/stdout':
                continue            # both programs reject the placement
            done += 1
            ok &= check(rows, f'{name}/case{ordinal - 1}', tick, first)
        if done != args.count:
            raise ValueError(f'{name}: only {done} placements of {args.count} accepted')
    return rows, ok


def cutscenes(args):
    rows, ok = [], True
    for cut in CUTS:
        count = len(src_pc.load(DATA / f'{cut}.phd').data['cinematic_frames'])
        for mode in ('zero', 'random'):
            rng = random.Random(61)
            masks, spent = [], 0
            while spent < count:
                mask = 0 if mode == 'zero' else rng.choice(CHOICES)
                n = min(1 if mask & (256 | 2048) else rng.randint(1, 45) if mode == 'random' else count, count - spent)
                masks += [mask] * n
                spent += n
            path = args.out / 'current.masks'
            path.write_text(''.join(f'{m}\n' for m in masks))
            tick, first = run_pair(args.old, args.new, DATA / f'{cut}.phd', path, count, args.schema or 3)
            ok &= check(rows, f'{cut}/{mode}', tick, first)
    return rows, ok


def mask_count(text):
    """Number of masks in an input file: the words left after its start/give/item directives."""
    words, i = text.split(), 0
    while i < len(words) and words[i] in ('start', 'give', 'item', 'activate'):
        i += {'start': 6, 'give': 3, 'item': 3, 'activate': 3}[words[i]]
    return len(words) - i


def different(args):
    rows, ok = [], True
    for path in sorted(args.inputs.glob('*.masks')):
        ticks = mask_count(path.read_text())
        tick, first = run_pair(args.old, args.new, DATA / f'{args.level}.phd', path, ticks, 3)
        differs = first is not None and first[1] == 'record'
        rows.append(dict(name=path.name, tick=tick, differs=differs,
                         field=field_of(first[2], first[3])[0] if differs else None))
        print(f'{path.name}: ' + (f'DIFFERENT at tick {tick}, first field {rows[-1]["field"]}' if differs else 'IDENTICAL'), flush=True)
        ok &= differs
    return rows, ok


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--old', required=True, type=Path)
    p.add_argument('--new', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--fixtures', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m2-start'))
    p.add_argument('--schema', type=int, choices=(2,3,4),
                   help='Override schema for fixtures (default 2) or cutscenes (default 3)')
    p.add_argument('--levels', default='level1')
    p.add_argument('--count', type=int, default=200)
    p.add_argument('--seed', type=int, default=61)
    p.add_argument('--inputs', type=Path)
    p.add_argument('--level')
    p.add_argument('action', choices=['fixtures', 'campaign', 'cutscenes', 'different'])
    args = p.parse_args()
    if args.schema and args.action not in ('fixtures','cutscenes'):
        p.error('--schema applies only to fixtures and cutscenes')
    args.out.mkdir(parents=True, exist_ok=True)
    rows, ok = dict(fixtures=fixtures, campaign=campaign, cutscenes=cutscenes, different=different)[args.action](args)
    (args.out / f'identity-{args.action}.json').write_text(json.dumps(
        dict(old=str(args.old), old_sha256=hashlib.sha256(args.old.read_bytes()).hexdigest(), new=str(args.new),
             new_sha256=hashlib.sha256(args.new.read_bytes()).hexdigest(), rows=rows), indent=1) + '\n')
    print(f'identity {args.action} ' + ('PASS' if ok else 'FAIL') + f' cases={len(rows)}')
    return int(not ok)


if __name__ == '__main__':
    raise SystemExit(main())
