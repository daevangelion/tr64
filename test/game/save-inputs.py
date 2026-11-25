#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stream real native/source Save/Load schema3 cases; retain inputs/reports only.

One caller-supplied production executable and pack serve the complete matrix.
The explicit test/game/save-io.c provider must be selected in that build.
No builds, JSONL files, save serializer, state copying or alternate simulation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
from compare import first_difference
from stream import Run

LEVELS = ('gym', 'level1', 'level2', 'level3a', 'level3b', 'level4', 'level5',
          'level6', 'level7a', 'level7b', 'level8a', 'level8b', 'level8c',
          'level10a', 'level10b', 'level10c')
ACTIONS = 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 | 512 | 2048


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for part in iter(lambda: source.read(65536), b''):
            digest.update(part)
    return digest.hexdigest()


def masks(path):
    words = Path(path).read_text(encoding='ascii').split()
    placement = []
    if words and words[0] == 'start':
        if len(words) < 6 or any(not re.fullmatch('[+-]?[0-9]+', value) for value in words[1:6]):
            raise ValueError(f'{path}: malformed start directive')
        placement, words = words[:6], words[6:]
    values = []
    for word in words:
        if not re.fullmatch('(?:[0-9]+|0[xX][0-9a-fA-F]+)', word):
            raise ValueError(f'{path}: malformed input {word!r}')
        value = int(word, 16 if word.lower().startswith('0x') else 10)
        if value & ~ACTIONS:
            raise ValueError(f'{path}: unsupported action bits')
        values.append(value)
    if len(values) != 600:
        raise ValueError(f'{path}: expected 600 input ticks, got {len(values)}')
    return placement, values


def check_record(raw, tick, producer):
    try:
        record = json.loads(raw)
    except (ValueError, UnicodeDecodeError) as error:
        raise ValueError(f'{producer}: tick={tick} malformed record: {error}') from error
    if record.get('schema') != 3 or type(record.get('tick')) is not int or record['tick'] != tick:
        raise ValueError(f'{producer}: tick={tick} wrong schema/tick')
    text = json.dumps(record, separators=(',', ':'), ensure_ascii=True).encode('ascii') + b'\n'
    if raw != text:
        raise ValueError(f'{producer}: tick={tick} noncanonical record')
    body = dict(record)
    stored = body.pop('hash', None)
    value = 2166136261
    for byte in json.dumps(body, separators=(',', ':'), ensure_ascii=True).encode('ascii'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    if stored != f'{value:08x}':
        raise ValueError(f'{producer}: tick={tick} hash does not cover schema3 fields')
    return record


def compare_case(executable, oracle, level, case, native_input=None):
    name = case['level'] + '/' + case['name']
    source_command = ['rtk', str(oracle), '--level', str(level), '--input', case['input'],
                      '--ticks', '600', '--trace-schema', '3',
                      '--save-tick', str(case['save_tick']), '--load-tick', str(case['load_tick'])]
    native_command = ['rtk', str(executable), '--game-level', case['level'],
                      '--game-input', str(native_input or case['input']), '--game-ticks', '600',
                      '--game-trace-only', '--game-trace-schema', '3',
                      '--game-save-tick', str(case['save_tick']),
                      '--game-load-tick', str(case['load_tick'])]
    source = Run(source_command, '--out')
    try:
        native = Run(native_command, '--game-trace')
    except BaseException:
        source.finish(kill=True)
        raise
    tick, difference, error, complete = 0, None, None, False
    try:
        while True:
            a, b = source.trace.readline(), native.trace.readline()
            if not a or not b:
                if a or b:
                    error = f'{name}: tick={tick} field=record expected={"record" if a else "EOF"} actual={"record" if b else "EOF"}'
                else:
                    complete = True
                break
            left = check_record(a, tick, 'source')
            right = check_record(b, tick, 'native')
            if a != b:
                field = first_difference(left, right) or ('serialization', 'source bytes', 'native bytes')
                difference = dict(tick=tick, field=field[0], expected=field[1], actual=field[2])
                break
            tick += 1
    except BaseException as failure:
        error = str(failure)
    finally:
        source_code, source_log = source.finish(kill=not complete)
        native_code, native_log = native.finish(kill=not complete)
    if error:
        raise ValueError(f'{error}\nsource: {source_log[-1000:]}\nnative: {native_log[-1000:]}')
    if difference:
        return dict(status='difference', records=tick, difference=difference)
    if source_code or native_code or tick != 601:
        raise ValueError(f'{name}: records={tick} source_exit={source_code} native_exit={native_code}\n'
                         f'source: {source_log[-1200:]}\nnative: {native_log[-1200:]}')
    return dict(status='equal', records=tick)


def run(args):
    matrix_sha = sha(args.matrix)
    cases = json.loads(args.matrix.read_text())
    verification = json.loads(args.oracle_report.read_text())
    oracle = (args.oracle or Path(verification['binary'])).resolve()
    oracle_sha = sha(oracle)
    if oracle_sha != verification['binary_sha256']:
        raise ValueError('source oracle binary differs from the immutable reference report')
    executable = args.executable.resolve()
    executable_sha = sha(executable)
    if not isinstance(cases, list) or {row['level'] for row in cases} != set(LEVELS):
        raise ValueError('matrix must cover exactly the 16 TR1 gameplay levels')
    identities, selected = {}, []
    for row in cases:
        key = row['level'] + '/' + row['name']
        if key in identities:
            raise ValueError(f'duplicate matrix case {key}')
        if row.get('status') != 'source-complete' or row.get('records') != 601:
            raise ValueError(f'{key}: source case is not a complete 600-tick reference')
        if any(type(row.get(field)) is not int or not 0 <= row[field] <= 600
               for field in ('save_tick', 'load_tick')):
            raise ValueError(f'{key}: invalid save/load boundary')
        if row['save_tick'] > row['load_tick']:
            raise ValueError(f'{key}: load precedes the only save')
        masks(row['input'])
        if sha(row['golden']) != row['save_sha256']:
            raise ValueError(f'{key}: immutable source save changed')
        level = args.data / (row['level'] + '.phd')
        identities[key] = dict(input_sha256=sha(row['input']), source_sha256=sha(level),
                               source_save_sha256=row['save_sha256'])
        if not args.case or key in args.case:
            selected.append(row)
    if args.case and set(args.case) != {row['level'] + '/' + row['name'] for row in selected}:
        raise ValueError('requested case missing from the matrix')
    args.out.mkdir(parents=True, exist_ok=True)
    report = dict(matrix_sha256=matrix_sha, source_binary_sha256=oracle_sha,
                  executable_sha256=executable_sha, schema=3, cases=[])
    for row in selected:
        key = row['level'] + '/' + row['name']
        result = compare_case(executable, oracle, args.data / (row['level'] + '.phd'), row)
        result.update(case=key, save_tick=row['save_tick'], load_tick=row['load_tick'], **identities[key])
        report['cases'].append(result)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        if result['status'] != 'equal':
            diff = result['difference']
            raise ValueError(f'{key}: tick={diff["tick"]} field={diff["field"]} expected={diff["expected"]!r} actual={diff["actual"]!r}')
        print(f'game save-input PASS {key} records=601 save={row["save_tick"]} load={row["load_tick"]}', flush=True)
    if args.negative:
        row = next((case for case in selected if case['level'] == 'level1' and case['name'] == 'awkward'), None)
        if row is None:
            raise ValueError('negative requires the real level1/awkward case')
        placement, values = masks(row['input'])
        values[0] ^= 2
        altered = args.out / 'negative-current.masks'
        altered.write_text((' '.join(placement) + '\n' if placement else '') + ''.join(f'{n}\n' for n in values))
        result = compare_case(executable, oracle, args.data / 'level1.phd', row, altered)
        if result['status'] != 'difference' or result['difference']['tick'] != 1:
            raise ValueError(f'changed-production-input negative was not rejected at tick1: {result}')
        report['negative'] = result
        print('game save-input negative PASS tick=1 field=' + result['difference']['field'], flush=True)
    if sha(executable) != executable_sha or sha(oracle) != oracle_sha or sha(args.matrix) != matrix_sha:
        raise ValueError('executable or source reference changed during the streamed gate')
    report['equal'] = len(selected)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'game save-input PASS cases={len(selected)} schema=3 no_jsonl_files=1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--matrix', required=True, type=Path)
    parser.add_argument('--oracle-report', required=True, type=Path)
    parser.add_argument('--oracle', type=Path)
    parser.add_argument('--data', type=Path, default=Path('/tmp/tr64/data/pc'))
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--case', action='append')
    parser.add_argument('--negative', action='store_true')
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'game save-input FAIL {error}\n')


if __name__ == '__main__':
    main()
