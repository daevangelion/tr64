#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise stop/query/release and unchanged fatal trace policy using real inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
from compare import first_difference, validate
from stream import Run

KINDS = {'DEATH': 1, 'LEVEL_END': 2, 'NOT_PORTED': 3}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def case(executable, oracle, level, masks, kind, next_level):
    words = masks.read_text().split()
    ticks = len(words) - (6 if words and words[0] == 'start' else 0)
    if not 1 <= ticks <= 600: raise ValueError('stop input must contain 1..600 ticks')
    base = ['rtk', str(executable), '--game-level', level.stem.lower(), '--game-input', str(masks),
            '--game-ticks', str(ticks), '--game-trace-only']
    reference = Run(['rtk', str(oracle), '--level', str(level), '--input', str(masks), '--ticks', str(ticks)], '--out')
    native = Run(base + ['--game-stop-report'], '--game-trace')
    count = 0
    prefix = hashlib.sha256()
    try:
        for line in native.trace:
            expected = reference.trace.readline()
            if expected != line:
                difference = first_difference(json.loads(expected), json.loads(line)) if expected else ('record', 'EOF', 'record')
                raise ValueError(f'tick={count} field={difference[0]} expected={difference[1]!r} actual={difference[2]!r}')
            validate(json.loads(line), line.decode('ascii'), count, 'native stop prefix')
            prefix.update(line)
            count += 1
    except Exception:
        native.finish(kill=True)
        reference.finish(kill=True)
        raise
    code, log = native.finish()
    reference.finish(kill=True)
    marker = re.search(r'^game-stop: kind=(\d+) next_level_id=(-?\d+) death_ready=(\d+) name=(.+)$', log, re.M)
    if code or not marker or not count or count > ticks:
        raise ValueError(f'missing clean structured stop: exit={code} records={count}\n{log[-1500:]}')
    if (int(marker[1]), int(marker[2])) != (KINDS[kind], next_level):
        raise ValueError(f'stop kind/next expected={(KINDS[kind], next_level)} actual={(marker[1], marker[2])}')
    if kind == 'DEATH' and marker[3] != '1':
        raise ValueError('death stop did not expose the ready source death page')
    if not re.search(r'^game-stop-release: code_bytes=0$', log, re.M):
        raise ValueError('release did not return every game unit frame')
    default = Run(base, '--game-trace')
    default_prefix = hashlib.sha256()
    default_count = 0
    for line in default.trace:
        default_prefix.update(line)
        default_count += 1
    default_code, default_log = default.finish()
    if not default_code or ('tr64 fatal: ' + marker[4] + '\n') not in default_log:
        raise ValueError('default test lane did not retain its named fatal boundary')
    if default_count != count or default_prefix.digest() != prefix.digest():
        raise ValueError('structured/default paths emitted different or additional trace records')
    return dict(input=str(masks), input_sha256=sha(masks), kind=kind, next_level_id=next_level,
                name=marker[4], death_ready=int(marker[3]), matched_prefix_records=count, default_exit=default_code,
                prefix_sha256=prefix.hexdigest(), released_code_bytes=0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--executable', required=True, type=Path)
    ap.add_argument('--level', required=True, type=Path)
    ap.add_argument('--oracle', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m2-start/tr1-oracle'))
    ap.add_argument('--case', action='append', required=True, help='KIND:NEXT_LEVEL_ID:INPUT_MASKS')
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    identity = sha(args.executable)
    report = dict(executable_sha256=identity, oracle_sha256=sha(args.oracle), cases=[])
    try:
        for value in args.case:
            kind, next_level, path = value.split(':', 2)
            if kind not in KINDS: raise ValueError(f'unknown stop kind: {kind}')
            result = case(args.executable, args.oracle, args.level, Path(path), kind, int(next_level))
            report['cases'].append(result)
            print('game stop PASS ' + json.dumps(result, sort_keys=True), flush=True)
        if identity != sha(args.executable): raise ValueError('stop executable changed during gate')
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + '\n')
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        ap.exit(1, f'game stop FAIL {error}\n')


if __name__ == '__main__':
    main()
