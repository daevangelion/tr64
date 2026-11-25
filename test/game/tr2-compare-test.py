#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the comparator CLI using a published, immutable retail fixture."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

TOOL = Path(__file__).resolve().parents[2] / 'tools/tr2-compare.py'
spec = importlib.util.spec_from_file_location('tr2_compare', TOOL)
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


def check(fixture, tiger):
    retail = fixture / 'trace.jsonl'
    body = retail.read_bytes()
    producer = 'tr64-tr2-dry'
    profile = compare.producer_profile(producer)

    def seal(record, digest=None):
        payload = json.dumps(record, separators=(',', ':')).encode('ascii')
        if digest is None:
            digest = compare.fnv32(payload)
        return payload[:-1] + f',"hash":"{digest}"}}\n'.encode('ascii')

    with tempfile.TemporaryDirectory(prefix='tr2-compare-') as directory:
        scratch = Path(directory)
        port = scratch / 'port.jsonl'

        def artifact(data, missing=None, *, bind=None, name=producer):
            port.write_bytes(data + seal({'schema': 20, 'producer': name,
                                          'unavailable': profile if missing is None else missing,
                                          'recordsHash': compare.fnv32(data if bind is None else bind)}))

        def run(status, *options, source=fixture, expected=producer):
            proc = subprocess.run([sys.executable, str(TOOL), str(source), str(port),
                                   '--producer', expected, *options], capture_output=True,
                                  text=True, check=False)
            if proc.returncode != status:
                raise RuntimeError(f'expected exit {status}, got {proc.returncode}: {proc.stderr}\n{proc.stdout}')
            return proc.stderr if status == 2 else json.loads(proc.stdout)

        artifact(body)
        equal = run(0, '--require', 'rng[0]')
        n = equal['records']
        if equal['families']['rng[0]']['equal'] != n:
            raise RuntimeError('identical gameplay RNG did not compare equal')
        missing = run(1, '--require', 'lara')
        if (missing['families']['lara']['equal'] != 0 or
                missing['families']['lara']['knownEqual'] != n or
                missing['families']['save']['equal'] != 0 or
                missing['families']['save']['knownEqual'] != n):
            raise RuntimeError(f'absent state counted as an equal tick: {missing}')
        projected = run(0, '--require', 'lara', '--require', 'rng[0]', '--accept-profile-exclusions')
        if projected['acceptedExclusions'] != sorted(p for p in profile if p.startswith('lara.')):
            raise RuntimeError('projection did not use the producer profile')
        absent = run(1, '--require', 'active', '--accept-profile-exclusions')
        if absent['families']['active']['knownEqual']:
            raise RuntimeError('a family with no compared fields counted as known-equal')
        port.write_bytes(port.read_bytes().replace(b'"save.ammoHits"', b'"save.ammoUsed"'))
        if 'availability footer hash mismatch' not in run(2):
            raise RuntimeError('replaced field coverage accepted')
        artifact(body, {})
        if 'coverage differs from reviewed producer' not in run(2, '--require', 'save'):
            raise RuntimeError('rehashed false completeness accepted')
        artifact(body, name='unknown')
        if 'unknown trace producer' not in run(2, expected='unknown'):
            raise RuntimeError('unregistered trace producer accepted')
        baseline = compare.producer_profile('tr64-tr2-baseline')
        artifact(body, baseline, name='tr64-tr2-baseline')
        run(0, '--require', 'lara', '--accept-profile-exclusions', expected='tr64-tr2-baseline')

        lines = body.splitlines(keepends=True)
        tick = 12
        record = json.loads(lines[tick])
        orig = record['lara']['pos'][2]
        record['lara']['pos'][2] += 1
        old_hash = record.pop('hash')
        lines[tick] = seal(record)
        mutated = b''.join(lines)
        artifact(mutated, bind=body)
        if 'does not bind these trace records' not in run(2):
            raise RuntimeError('footer accepted with a different record stream')
        artifact(mutated)
        neg = run(1, '--require', 'lara', '--require', 'rng[0]', '--accept-profile-exclusions')
        if neg['families']['lara'] != {
                'equal': 0, 'prefix': 0, 'knownEqual': n - 1, 'knownPrefix': tick,
                'first': {'tick': tick, 'path': 'lara.pos[2]', 'retail': orig, 'port': orig + 1},
                'unavailable': {p: v for p, v in profile.items() if p.startswith('lara.')}}:
            raise RuntimeError(f'wrong tick, field or counts: {neg}')
        if run(0, '--require', 'rng[0]')['families'] != neg['families']:
            raise RuntimeError('required family should affect the verdict only')

        lines[tick] = seal(record, old_hash)
        artifact(b''.join(lines))
        if 'tick 12: FNV-1a hash mismatch' not in run(2):
            raise RuntimeError('corrupted record hash not reported')
        for family, field, path in (('lara', 'ammo', 'lara.ammo[0]'),
                                    ('camera', 'lastItem', 'camera.lastItem')):
            changed = json.loads(body.splitlines()[tick])
            del changed['hash']
            if field == 'ammo':
                origin = changed[family][field][0]
                changed[family][field][0] = origin + 1
            else:
                origin = changed[family][field]
                changed[family][field] = origin + 1
            lines[tick] = seal(changed)
            mutated = b''.join(lines)
            artifact(mutated)
            options = ('--require', family, '--accept-profile-exclusions')
            difference = run(1, *options)['families'][family]['first']
            if difference != {'tick': tick, 'path': path, 'retail': origin, 'port': origin + 1}:
                raise RuntimeError(f'wrong first difference for {path}: {difference}')
            artifact(mutated, baseline, name='tr64-tr2-baseline')
            if "expected producer 'tr64-tr2-dry', got 'tr64-tr2-baseline'" not in run(2, *options):
                raise RuntimeError(f'baseline producer relabelling concealed {path}')

        for family, field, value, path in (('world', 1, -1, 'world[1]'),
                                            ('lara', 'collidable', 2, 'lara.collidable'),
                                            ('lara', 'water', 4, 'lara.water'),
                                            ('lara', 'hitQuadrant', 32767, 'lara.hitQuadrant')):
            invalid = json.loads(body.splitlines()[tick])
            del invalid['hash']
            invalid[family][field] = value
            lines[tick] = seal(invalid)
            artifact(b''.join(lines))
            if f'{path}: expected' not in run(2):
                raise RuntimeError(f'out-of-range {path} not rejected')
        if tiger is not None:
            name = 'tr64-tr2-items'
            item_profile = compare.producer_profile(name)
            tiger_body = (tiger / 'trace.jsonl').read_bytes()
            tiger_lines = tiger_body.splitlines(keepends=True)
            wake = next(i for i, line in enumerate(tiger_lines)
                        if any(item['type'] == 39 for item in json.loads(line)['active']))
            opts = ('--require', 'active', '--accept-profile-exclusions')

            def item_mutation(tick, change, expected_path=None):
                rows = tiger_lines.copy()
                row = json.loads(rows[tick])
                del row['hash']
                change(row['active'])
                rows[tick] = seal(row)
                artifact(b''.join(rows), item_profile, name=name)
                result = run(int(expected_path is not None), *opts,
                             source=tiger, expected=name)['families']['active']['first']
                if expected_path is not None and (result['tick'], result['path']) != (tick, expected_path):
                    raise RuntimeError(f'wrong item difference: {result}')

            def move_tiger(items):
                next(item for item in items if item['type'] == 39)['pos'][0] += 1

            item_mutation(wake + 1, move_tiger)
            item_mutation(wake, move_tiger, 'active[0].pos[0]')
            item_mutation(wake + 1, lambda items: items.reverse(), 'active[0].index')
            item_mutation(wake + 1, lambda items: items.pop(), 'active.length')

            def move_campfire(items):
                next(item for item in items if item['type'] == 253)['pos'][0] += 1

            item_mutation(wake + 1, move_campfire, 'active[2].pos[0]')
            artifact(tiger_body, {**item_profile, 'active[type=39].index': [0, None]}, name=name)
            if 'invalid path' not in run(2, *opts, source=tiger, expected=name):
                raise RuntimeError('an item identity exclusion was accepted')
            print('PASS: item projection cannot hide wake pose, item order, length or another type')

        for name in ('trace.jsonl', 'input.txt', 'manifest.json'):
            shutil.copyfile(fixture / name, scratch / name)
        with (scratch / 'input.txt').open('ab') as file:
            file.write(b' ')
        artifact(body)
        if 'input.txt SHA-256 mismatch' not in run(2, source=scratch):
            raise RuntimeError('manifest integrity failure not reported')

    print('PASS: identity, perturbation tick/path, bound availability, explicit exclusions, empty coverage and domains')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--tiger', type=Path, help='published route exercising tiger wake projection')
    opts = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    check(opts.fixture, opts.tiger)
