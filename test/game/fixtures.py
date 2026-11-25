#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the production host game against every fixture in one level, fail first."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from compare import compare, first_difference, validate
from stream import Run

REVISION = '2333adef30ae2a5f3b32c5355e6e343d6aa0531a'
ORDER = ['idle', 'run-forward', 'walk', 'turn-left', 'turn-right', 'wall',
         'jump-forward', 'back-hop', 'side-left', 'side-right', 'roll',
         'fall', 'swim', 'underwater-start']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(executable, level, masks, ticks, output):
    process = Run(['rtk', str(executable), '--game-level', level, '--game-input', str(masks),
                   '--game-ticks', str(ticks), '--game-trace-only'], '--game-trace', log_limit=65536)
    trace = process.trace.read().decode('ascii')
    code, log = process.finish()
    output.with_suffix('.log').write_text(log)
    if code:
        raise ValueError(f'game exited {code}: {log[-600:]}')
    return trace


def live(oracle, executable, level, row, schema, output):
    """Schemas 3 and 4 have no stored fixture traces: stream the oracle's own trace of the same
    input through a second pipe and compare line by line (first difference reported by field)."""
    reference = Run([str(oracle), '--level', row['level'], '--input', str(row['input']), '--ticks', str(row['ticks']),
                     '--trace-schema', str(schema)], '--out')
    native = Run(['rtk', str(executable), '--game-level', level, '--game-input', str(row['input']),
                  '--game-ticks', str(row['ticks']), '--game-trace-only', '--game-trace-schema', str(schema)],
                 '--game-trace', log_limit=65536)
    tick = 0
    while True:
        a, b = reference.trace.readline().decode('ascii'), native.trace.readline().decode('ascii')
        if not a or not b:
            break
        if a != b:
            reference.finish(kill=True)
            native.finish(kill=True)
            field, wanted, got = first_difference(json.loads(a), json.loads(b)) or ('serialization', a, b)
            raise ValueError(f'tick={tick} field={field} expected={wanted!r} actual={got!r}')
        validate(json.loads(a), a, tick, 'oracle', schema)
        tick += 1
    ocode, olog = reference.finish(kill=bool(a or b))
    ncode, nlog = native.finish(kill=bool(a or b))
    if a or b or ocode or ncode or tick != row['ticks'] + 1:
        raise ValueError(f'trace length/exit mismatch records={tick} oracle={ocode} native={ncode}: {nlog[-300:]}')
    (output / 'live.log').write_text(olog + nlog)
    return tick


def negative(executable, level, fixture, output):
    words = Path(fixture['input']).read_text(encoding='ascii').split()
    if words[0] == 'start' or len(words) < 31:
        raise ValueError('run-forward negative control requires an unplaced 31+ tick input')
    mask = int(words[30], 16 if words[30].lower().startswith('0x') else 10)
    if mask & 2:
        raise ValueError('negative control tick31 already holds LEFT')
    words[30] = str(mask | 2)
    masks = output / 'negative.masks'
    masks.write_text('\n'.join(words) + '\n', encoding='ascii')
    actual = run(executable, level, masks, fixture['ticks'], output / 'negative')
    try:
        compare(fixture['trace'], io.StringIO(actual))
    except ValueError as error:
        if not str(error).startswith('tick=31 '):
            raise ValueError(f'negative control failed for the wrong reason: {error}') from error
        first = str(error)
    else:
        raise ValueError('negative control was accepted')
    with Path(fixture['trace']).open(encoding='ascii') as source, io.StringIO(actual) as changed:
        expected = [json.loads(line) for line in source]
        rows = []
        for tick, line in enumerate(changed):
            record = json.loads(line)
            validate(record, line, tick, 'native pipe')
            rows.append(record)
    if len(rows) != len(expected) or any(rows[i]['hash'] == expected[i]['hash'] for i in range(31, len(rows))):
        raise ValueError('negative control did not change every subsequent record')
    if rows[-1]['lara']['pos'] == expected[-1]['lara']['pos'] or rows[-1]['lara']['rot'] == expected[-1]['lara']['rot']:
        raise ValueError('negative control did not change final position and rotation')
    print(f'game negative PASS {first}')
    spec = importlib.util.spec_from_file_location('game_scene_checks', Path(__file__).with_name('scene-checks.py'))
    checks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checks)
    context = dict(meta={'ticks': fixture['ticks']}, results={},
                   log=(output / 'negative.log').read_text())
    try:
        checks.hashes(context, [fixture['trace']])
    except ValueError as error:
        if not str(error).startswith('tick=31 field=hash '):
            raise ValueError(f'cart hash predicate failed for the wrong reason: {error}') from error
    else:
        raise ValueError('cart hash predicate accepted the changed production input')
    with Path(fixture['trace']).open(encoding='ascii') as source:
        lines = list(source)
    missing = json.loads(lines[0])
    del missing['lara']['health']
    lines[0] = json.dumps(missing, separators=(',', ':')) + '\n'
    try:
        compare(fixture['trace'], io.StringIO(''.join(lines)))
    except ValueError as error:
        if not str(error).startswith('tick=0 field=lara.keys '):
            raise ValueError(f'missing-field control failed for the wrong reason: {error}') from error
    else:
        raise ValueError('trace comparator accepted a missing Lara field')
    print('game verifier controls PASS changed-production-hash missing-field')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--level', choices=['level1', 'gym'], required=True)
    parser.add_argument('--fixtures', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m2-start'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--schema', type=int, choices=(2, 3, 4), default=2,
                        help='2 compares the stored fixture traces; 3 or 4 streams --oracle for every fixture input')
    parser.add_argument('--oracle', type=Path, help='oracle executable supporting --schema (3 or 4)')
    args = parser.parse_args()
    if args.schema != 2 and (not args.oracle or args.negative):
        parser.error('--schema 3/4 needs --oracle and has no stored-trace negative control')
    try:
        manifest = json.loads((args.fixtures / 'manifest.json').read_text())
        if manifest['schema'] != 2 or manifest['openlara_commit'] != REVISION or manifest['verified'] is not True:
            raise ValueError('oracle manifest schema/revision/verification identity')
        fixtures = [row for row in manifest['fixtures'] if row['name'].split('/')[0].lower() == args.level]
        if len(manifest['fixtures']) != 25 or len(fixtures) != {'level1': 12, 'gym': 13}[args.level]:
            raise ValueError('oracle manifest does not contain the complete 25-fixture M2 set')
        if len({row['name'] for row in manifest['fixtures']}) != 25:
            raise ValueError('oracle manifest contains duplicate fixtures')
        fixtures.sort(key=lambda row: ORDER.index(row['name'].split('/')[1]))
        args.out.mkdir(parents=True, exist_ok=True)
        binary = sha(args.executable)
        for row in fixtures:
            for kind in ['level', 'input', 'trace']:
                if sha(row[kind]) != row[kind + '_sha256']:
                    raise ValueError(f'{row["name"]}: immutable oracle {kind} identity changed')
            name = row['name'].split('/')[1]
            if args.schema != 2:
                (args.out / name).mkdir(exist_ok=True)
                records = live(args.oracle, args.executable, args.level, row, args.schema, args.out / name)
            else:
                actual = run(args.executable, args.level, row['input'], row['ticks'], args.out / name)
                records = compare(row['trace'], io.StringIO(actual))
            print(f'game fixture PASS {row["name"]} records={records}')
        if args.negative:
            row = next(row for row in fixtures if row['name'].endswith('/run-forward'))
            negative(args.executable, args.level, row, args.out)
        if sha(args.executable) != binary:
            raise ValueError('host executable changed during the fixture gate')
        print(f'game fixtures PASS level={args.level} fixtures={len(fixtures)} executable_sha256={binary}')
    except (OSError, ValueError, KeyError, StopIteration, subprocess.SubprocessError) as error:
        parser.exit(1, f'game fixtures FAIL {error}\n')


if __name__ == '__main__':
    main()
