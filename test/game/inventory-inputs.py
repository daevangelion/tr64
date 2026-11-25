#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stream real SELECT/ring/passport/options scenes against the pinned oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from compare import first_difference
from stream import Run


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def masks(parts):
    result = [mask for mask, ticks in parts for _ in range(ticks)]
    if len(result) > 600:
        raise ValueError('inventory scene exceeds 600 ticks')
    return result + [0] * (600 - len(result))


def compare_scene(executable, oracle, level, path, required_states, required_pages):
    reference = Run(['rtk', str(oracle), '--level', str(level), '--input', str(path),
                     '--ticks', '600', '--trace-schema', '3'], '--out')
    native = Run(['rtk', str(executable), '--game-level', level.stem.lower(), '--game-input', str(path),
                  '--game-ticks', '600', '--game-trace-only', '--game-trace-schema', '3'], '--game-trace')
    records = 0
    states, pages = set(), set()
    try:
        while True:
            expected, actual = reference.trace.readline(), native.trace.readline()
            if not expected or not actual:
                break
            if expected != actual:
                difference = first_difference(json.loads(expected), json.loads(actual))
                raise ValueError(f'{path.name}: tick={records} difference={difference}')
            row = json.loads(expected)
            states.add(row['inventory']['state'])
            pages.add(row['inventory']['page'])
            records += 1
        if expected or actual:
            raise ValueError(f'{path.name}: unmatched record at tick={records}')
        oracle_code, oracle_log = reference.finish()
        native_code, native_log = native.finish()
        if oracle_code or native_code or records != 601:
            raise ValueError(f'{path.name}: records={records} oracle={oracle_code} native={native_code}\n'
                             f'{oracle_log[-500:]}\n{native_log[-500:]}')
        if not required_states <= states or not required_pages <= pages:
            raise ValueError(f'{path.name}: interaction not exercised: states={sorted(states)} pages={sorted(pages)}')
        return dict(input=str(path), input_sha256=sha(path), records=records,
                    states=sorted(states), pages=sorted(pages))
    finally:
        if reference in __import__('stream').RUNNING:
            reference.finish(kill=True)
        if native in __import__('stream').RUNNING:
            native.finish(kill=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--oracle', required=True, type=Path)
    parser.add_argument('--level', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    # InputState values: SELECT=2048, LEFT=2, RIGHT=4, DOWN=16, ACTION=128, JUMP=32.
    scenes = [
        ('open-close', [(2048, 1), (0, 32), (2048, 1)], {0, 1, 2, 4}, {5}),
        ('ring-spin', [(2048, 1), (0, 20), (2, 24), (0, 16), (4, 24), (0, 16), (32, 1)], {0, 1, 4, 6}, {5}),
        ('passport-pages', [(2048, 1), (0, 20), (16, 1), (0, 32), (128, 1), (0, 40),
                            (2, 1), (0, 12), (4, 1), (0, 12), (4, 1), (0, 12),
                            (2, 1), (0, 12), (32, 1), (0, 40), (2048, 1)], {1, 4, 7, 8, 9}, {5, 7}),
        ('detail-options', [(2048, 1), (0, 20), (16, 1), (0, 32), (2, 24), (0, 16),
                           (128, 1), (0, 60), (4, 1), (0, 1), (4, 1), (0, 1),
                           (16, 1), (0, 1), (2, 1), (0, 1), (32, 1), (0, 40), (2048, 1)],
                           {1, 4, 6, 7, 8, 9}, {5, 7}),
    ]
    report = dict(executable=str(args.executable), executable_sha256=sha(args.executable),
                  oracle=str(args.oracle), oracle_sha256=sha(args.oracle),
                  level=str(args.level), level_sha256=sha(args.level), scenes=[])
    try:
        for name, parts, states, pages in scenes:
            path = args.out / (name + '.masks')
            path.write_text('\n'.join(map(str, masks(parts))) + '\n', encoding='ascii')
            result = compare_scene(args.executable, args.oracle, args.level, path, states, pages)
            report['scenes'].append(result)
            print('inventory scene PASS ' + name, flush=True)
        if sha(args.executable) != report['executable_sha256'] or sha(args.oracle) != report['oracle_sha256']:
            raise ValueError('inventory comparison producer changed during run')
    except (ValueError, OSError) as error:
        report['error'] = str(error)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        parser.exit(1, 'inventory scene FAIL ' + str(error) + '\n')
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('inventory focused PASS ' + json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
