#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Seeded placed 600-tick differential: exact records or a named unported stop.

Run from the repository root with PYTHONPATH=tools/cook. Only the current input
and a small result report are written; both JSONL traces use separate pipes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import subprocess
import sys
sys.dont_write_bytecode = True
import src_pc
from compare import first_difference

from stream import Run


def compare_case(executable, oracle, level, masks, name, observe=None, schema=2):
    reference = Run(['rtk', str(oracle), '--level', str(level), '--input', str(masks),
                     '--ticks', '600'] + (['--trace-schema', str(schema)] if schema != 2 else []), '--out')
    native = Run(['rtk', str(executable), '--game-level', level.stem.lower(),
                  '--game-input', str(masks), '--game-ticks', '600', '--game-trace-only']
                 + (['--game-trace-schema', str(schema)] if schema != 2 else []), '--game-trace')
    tick = 0
    while True:
        a, b = reference.trace.readline(), native.trace.readline()
        if not a or not b:
            break
        if a != b:
            problem = None
            try:
                left = json.loads(a)
            except (ValueError, UnicodeDecodeError) as error:
                problem = f'oracle malformed record: {error}'
            if problem is None:
                try:
                    right = json.loads(b)
                except (ValueError, UnicodeDecodeError) as error:
                    problem = f'native malformed record: {error}'
            _, oracle_log = reference.finish(kill=True)
            _, native_log = native.finish(kill=True)
            if problem:
                raise ValueError(f'{name}: tick={tick} {problem}\n'
                                 f'oracle: {oracle_log[-400:]}\nnative: {native_log[-400:]}')
            difference = first_difference(left, right) or (
                'serialization', 'canonical oracle bytes', 'different native bytes')
            raise ValueError(f'{name}: tick={tick} field={difference[0]} expected={difference[1]!r} actual={difference[2]!r}')
        if observe is not None:
            try:
                observe(json.loads(a))
            except BaseException:
                reference.finish(kill=True)
                native.finish(kill=True)
                raise
        tick += 1
    if not b:
        code, log = native.finish()
        if code and re.search(r'tr64 fatal: game .*?(?:not ported|outside M2)', log):
            reference.finish(kill=True)
            reason = re.search(r'tr64 fatal: (game [^\n]+)', log).group(1)
            return 'loud', tick, reason
    else:
        code, log = None, ''
    if a:
        reference.finish(kill=True)
        if code is None:
            native.finish(kill=True)
        raise ValueError(f'{name}: tick={tick} native ended without a named exclusion: {log[-600:]}')
    if b:
        native.finish(kill=True)
        reference.finish()
        raise ValueError(f'{name}: oracle stopped at tick={tick}, native continued')
    oracle_code, oracle_log = reference.finish()
    if code is None:
        code, log = native.finish()
    if tick == 0 and oracle_code and code and 'start' in oracle_log and 'placement' in log:
        return 'invalid-placement', 0, None
    if oracle_code or code or tick != 601:
        raise ValueError(f'{name}: records={tick} oracle_exit={oracle_code} native_exit={code}\n{oracle_log[-300:]}\n{log[-300:]}')
    return 'equal', tick, None


def portal(words, index):
    if not index:
        return False
    command = words[index]
    for kind in (2, 3):
        if command & 31 == kind:
            if command & 32768:
                return False
            index += 2
            command = words[index]
    return command & 31 == 1


def generated(level, ordinal, seed, menus=False):
    rng = random.Random(seed + ordinal * 104729)
    room_index = ordinal % len(level.rooms)
    room = level.rooms[room_index]
    words = level.data['floor_data']
    candidates = [(index, sector) for index, sector in enumerate(room.sectors)
                  if sector['floor'] != -127 and sector['ceiling'] != -127
                  and sector['floor'] - sector['ceiling'] >= 4
                  and not portal(words, sector['floor_index'])]
    if not candidates:
        return None
    index, sector = rng.choice(candidates)
    # Room dimensions are retained explicitly by the source reader.
    z_sectors = room.z_sectors
    x = room.info['x'] + (index // z_sectors) * 1024 + 512
    z = room.info['z'] + (index % z_sectors) * 1024 + 512
    y = sector['floor'] * 256 - (sector['room_below'] != 255)
    falling = ordinal % 4 == 0
    if falling:
        y = sector['ceiling'] * 256 + 1024
    angle = rng.choice([0, 16384, -16384, -32768])
    masks = []
    choices = [0, 8, 10, 12, 40, 72, 16, 80, 66, 68, 24, 2, 4, 128, 136, 160,
               514, 516, 256, 264, 384, 392]
    if menus:
        choices += [2048, 32]  # SELECT edge; JUMP cancels ring/passport pages.
    while len(masks) < 600:
        mask = 0 if falling and (not menus or len(masks)<90) else rng.choice(choices)
        duration = 1 if mask & (256 | 2048) else rng.randint(1, 45)
        masks.extend([mask] * min(duration, 600-len(masks)))
    return f'start {room_index} {x} {y} {z} {angle}\n' + '\n'.join(map(str, masks)) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--level', required=True, type=Path)
    parser.add_argument('--oracle', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m2-start/tr1-oracle'))
    parser.add_argument('--count', type=int, default=200)
    parser.add_argument('--seed', type=int, default=61)
    parser.add_argument('--case', action='append', type=Path, default=[])
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    level = src_pc.load(args.level)
    result = dict(level=level.name, seed=args.seed, equal=0, loud=0, invalid=0,
                  silent_difference=0, loud_reasons={}, targeted=[],
                  source_sha256=level.sha256,
                  executable_sha256=hashlib.sha256(args.executable.read_bytes()).hexdigest())
    try:
        for path in args.case:
            status, records, reason = compare_case(args.executable, args.oracle, args.level, path, path.name)
            if status == 'invalid-placement':
                raise ValueError(f'targeted case rejected by both programs: {path}')
            result['targeted'].append(dict(input=str(path),
                input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                status=status, records=records, reason=reason))
            print(f'game targeted PASS {path.name} {status} records={records} reason={reason!r}', flush=True)
        ordinal = 0
        while result['equal'] + result['loud'] < args.count:
            if ordinal >= args.count * 20:
                raise ValueError('too many rejected random placements')
            text = generated(level, ordinal, args.seed)
            name = f'{level.name}-room{ordinal % len(level.rooms)}-seed{args.seed}-case{ordinal}'
            ordinal += 1
            if text is None:
                continue
            path = args.out / 'current.masks'
            path.write_text(text, encoding='ascii')
            status, records, reason = compare_case(args.executable, args.oracle, args.level, path, name)
            if reason:
                result['loud_reasons'][reason] = result['loud_reasons'].get(reason, 0) + 1
            result['invalid' if status == 'invalid-placement' else status] += 1
            if status != 'invalid-placement':
                print(f'game random PASS {name} {status} records={records}', flush=True)
        if result['executable_sha256'] != hashlib.sha256(args.executable.read_bytes()).hexdigest():
            raise ValueError('native executable changed during regression')
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        result['silent_difference'] += int('tick=' in str(error) and 'field=' in str(error))
        result['error'] = str(error)
        (args.out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
        parser.exit(1, f'game random FAIL {error}\n')
    (args.out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    print('game random PASS ' + json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
