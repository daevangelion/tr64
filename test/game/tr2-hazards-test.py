#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Great Wall hazard traces, saturated effects, portal order and finish boundary."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

import n64_pack

ROUTES = ('spike-walk', 'spike-jump', 'blade-hit', 'blade-pass',
          'boulder-hit', 'zip-release', 'zip-stop', 'level-finish')
PRODUCER = 'tr64-tr2-great-wall20'
FINISH = b'Tomb Raider II: The Great Wall finish boundary'


def load(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(host, pack, inputs, trace, **env):
    trace.unlink(missing_ok=True)
    result = subprocess.run(['rtk', 'proxy', str(host), '--game-tr2', str(pack), str(inputs), str(trace)],
                            env=os.environ | env, capture_output=True, check=False, timeout=120)
    return result.returncode, result.stdout + result.stderr, trace.read_bytes() if trace.exists() else b''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('host', 'pack', 'fixtures', 'pool_control', 'contact_control'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--prior-contact-pass', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    compare = load(here.parents[1] / 'tools/tr2-compare.py')
    cooked = load(here / 'tr2-item-actions-test.py')
    failures = []
    native = {}
    with tempfile.TemporaryDirectory(prefix='tr2-hazards-', dir=os.environ['TMPDIR']) as tmp:
        path = Path(tmp)
        trace = path / 'trace.jsonl'
        code, log, _ = run(args.pool_control, args.pack, args.fixtures / 'spike-jump/input.txt', trace,
                           TR64_HAZARD_POOL_CONTROL='compress-goal')
        if code or b'PASS compression goal stays without an eligible direction, including wade' not in log:
            failures.append(f'compression goal: {log.decode().splitlines()[-1]}')
        else:
            print('PASS compression goal keeps 15 dry and in wade; descent over 131 leaves it')
        for route in ROUTES:
            fixture = args.fixtures / route
            code, log, rows = run(args.host, args.pack, fixture / 'input.txt', trace)
            if code:
                failures.append(f'{route}: {log.decode().splitlines()[-1]}')
                continue
            report = compare.compare(fixture, rows, PRODUCER)
            # The raw presentation stream remains in the comparator report;
            # this logic-only harness gates the gameplay stream separately.
            differences = {key: stat['first'] for key, stat in report['families'].items()
                           if key != 'rng' and stat['first']}
            if differences:
                failures.append(f'{route}: {differences}')
            else:
                print(f'PASS {route}: {report["records"]} rows, all compared families')
            native[route] = rows
            if route == 'level-finish' and FINISH not in log:
                failures.append('level-finish: terminal update lacked the named boundary')

        for route, attempts, tick in (('spike-jump', 20, 43), ('boulder-hit', 15, 84)):
            for capacity in ('normal', 'zero'):
                code, log, rows = run(args.pool_control, args.pack, args.fixtures / route / 'input.txt', trace,
                                      TR64_HAZARD_POOL_CONTROL=route, TR64_HAZARD_POOL_CAPACITY=capacity)
                marker = f'PASS {route} {capacity}: {attempts} blood attempts and lethal contact at {tick}'.encode()
                if code or marker not in log or rows != native.get(route):
                    failures.append(f'{route}: {capacity} pool lost damage, draws or trace identity')
                else:
                    print(marker.decode())

        for route in ('spike-29', 'spike-30'):
            code, log, rows = run(args.pool_control, args.pack, args.fixtures / 'spike-walk/input.txt', trace,
                                  TR64_HAZARD_POOL_CONTROL=route, TR64_HAZARD_POOL_CAPACITY='normal')
            marker = f'PASS {route}: grounded contact and exact gameplay roll'.encode()
            if code or marker not in log or b'\"recordsHash\"' in rows:
                failures.append(f'{route}: {log.decode().splitlines()[-1]}')
            else:
                print(marker.decode())

        for route, count in (('blade-idle', 0), ('blade-pedestal', 0), ('blade-cut', 2)):
            code, log, rows = run(args.pool_control, args.pack, args.fixtures / 'blade-hit/input.txt', trace,
                                  TR64_HAZARD_POOL_CONTROL=route, TR64_HAZARD_POOL_CAPACITY='normal')
            marker = f'PASS {route}: {count} blood attempts in the item phase'.encode()
            if code or marker not in log or b'\"recordsHash\"' in rows:
                failures.append(f'{route}: {log.decode().splitlines()[-1]}')
            else:
                print(marker.decode())

        inputs = path / 'extra.txt'
        inputs.write_bytes((args.fixtures / 'level-finish/input.txt').read_bytes() + b'\n0\n')
        code, log, rows = run(args.host, args.pack, inputs, trace)
        final_rows = native.get('level-finish', b'').splitlines(keepends=True)[:-1]
        if code != 1 or FINISH not in log or rows != b''.join(final_rows):
            failures.append('level-finish: extra input did not stop after the final gameplay row without a seal')
        else:
            print('PASS finish boundary rejects tick 123 but retains the tick-122 sample')

        # The room-3 portals are 0, 33, 31 in WALL.TR2. Make an independent
        # test pack with [33, 0, 33], so both sort and dedup would be visible.
        tables = n64_pack.inspect(args.pack.read_bytes())['wall']
        room = cooked.rows(tables, 'rooms')[3]
        first = room['portal_start']
        portals = cooked.rows(tables, 'portals')
        if [p['room'] for p in portals[first:first+room['portal_count']]] != [0, 33, 31]:
            raise RuntimeError('portal control needs the authored room-3 sequence')
        for index, destination in enumerate((33, 0, 33), first):
            tables = cooked.replace_row(tables, 'portals', index, portals[index], room=destination)
        variant = path / 'portals.pack'
        variant.write_bytes(n64_pack.build({'wall': cooked.wall_sections(tables)}, games={'wall': 2}))
        code, log, _ = run(args.contact_control, variant, args.fixtures / 'door-closed/input.txt', trace)
        marker = b'PASS authored duplicate portal order, push keeps the entry rooms, strict per-axis reach and entry-only health gate'
        if code or marker not in log:
            failures.append(f'contact pass: {log.decode().splitlines()[-1]}')
        else:
            print(marker.decode())
        if args.prior_contact_pass:
            code, log, _ = run(args.prior_contact_pass, variant,
                               args.fixtures / 'door-closed/input.txt', trace)
            if code != 1 or b'wrong portal contact visits=3 order=0,4,5,0' not in log:
                failures.append(f'prior contact pass control: {log.decode().splitlines()[-1]}')
            else:
                print('PASS prior deduplicating pass loses the second visit to room 33')
        models = cooked.rows(n64_pack.inspect(args.pack.read_bytes())['wall'], 'models')
        donor = next(i for i, m in enumerate(models) if m['type'] == 2 and m['animation'] == 65535)
        blade = next(m for m in models if m['type'] == 81)
        short_tables = cooked.replace_row(
            n64_pack.inspect(args.pack.read_bytes())['wall'], 'models', donor, models[donor],
            animation=blade['animation'] + 2)
        short = path / 'short-blade.pack'
        short.write_bytes(n64_pack.build({'wall': cooked.wall_sections(short_tables)}, games={'wall': 2}))
        code, log, _ = run(args.host, short, args.fixtures / 'blade-hit/input.txt', trace)
        if code == 0 or b'guillotine model has fewer than three clips' not in log:
            failures.append(f'short blade bank: {log.decode().splitlines()[-1]}')
        else:
            print('PASS guillotine model has fewer than three clips')
    for failure in failures:
        print(f'FAIL {failure}')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
