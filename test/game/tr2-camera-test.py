#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check specified camera checkpoints and refused inputs in the native host."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


TOOL = Path(__file__).resolve().parents[2] / 'tools/tr2-compare.py'
SPEC = importlib.util.spec_from_file_location('tr2_compare', TOOL)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)


def check(exe, pack, fixtures, temp, selected, baseline):
    routes = {
        'stand': None, 'travel': None, 'roll': None, 'grab-attempt': None,
        'slide-jump': None, 'slide-jump-backward': None,
        'look-sidestep': None, 'hard-landing': None, 'fall-damage': None,
        'look': (175, 'firearms'), 'pistols': (45, 'firearms'),
        'flare': None, 'flare-held': None, 'tiger-cave': None,
        'pool-swim': (463, 'water-entry'), 'pool-wade': (567, 'water-entry'),
        'fixed-camera': None, 'fixed-camera-repeat': None,
    }
    if selected and selected not in routes:
        raise ValueError(f'unknown directed control {selected}')
    with tempfile.TemporaryDirectory(prefix='tr2-camera-', dir=temp) as scratch:
        root = Path(scratch)
        out = root / 'scene.jsonl'

        def run(masks, host=exe):
            return subprocess.run(['rtk', str(host), '--game-tr2', str(pack), str(masks), str(out)],
                                  capture_output=True, timeout=20)

        def replay(route, host=exe, end=None):
            fixture = fixtures / route
            wire, masks = compare.fixture_data(fixture)
            retail = list(compare.records(wire, masks, 'retail'))
            proc = run(fixture / 'input.txt', host)
            if proc.returncode:
                raise RuntimeError(f'{route}: host exited {proc.returncode}: {proc.stderr.decode()}')
            wire = out.read_bytes()
            body, _, _, ends = compare.port_body(wire)
            want = {'camera': {'lastTick': end[0], 'reason': end[1]}} if end else {}
            if host == exe and ends != want:
                raise RuntimeError(f'{route}: expected coverage {want}, got {ends}')
            return retail, list(compare.records(body, masks, 'port'))

        for route, end in routes.items():
            if selected and route != selected:
                continue
            expected, got = replay(route, end=end)
            n = end[0] + 1 if end else len(expected)
            rng_diff = None
            for a, b in zip(expected[:n], got[:n]):
                if a['camera'] != b['camera']:
                    diff, _ = compare.first_difference(a['camera'], b['camera'], 'camera', ())
                    raise RuntimeError(f'{route}: camera at {a["tick"]}: {diff}')
                if any(a['lara'][key] != b['lara'][key] for key in ('head', 'torso')):
                    raise RuntimeError(f'{route}: gaze differs at {a["tick"]}')
                if rng_diff is None and a['rng'][0] != b['rng'][0]:
                    rng_diff = a['tick']
            first_tiger_update = {'tiger-cave': 397, 'pool-wade': 397, 'pool-swim': 430}.get(route)
            if rng_diff != first_tiger_update:
                raise RuntimeError(f'{route}: gameplay RNG first differs at {rng_diff}, '
                                   f'expected {first_tiger_update}')
            print(f'PASS {route}: camera {n}/{len(got)} canonical records')
        if selected is not None:
            return

        if baseline is not None:
            expected, got = replay('stand', baseline)
            diff, _ = compare.first_difference(expected[0]['camera'], got[0]['camera'], 'camera', ())
            if not diff or diff['path'] != 'camera.pos[0]':
                raise RuntimeError(f'inherited-rule control did not differ at the opening eye: {diff}')
            print('PASS inherited-rule negative against historical executable')

        stand = fixtures / 'stand' / 'input.txt'
        run(stand).check_returncode()
        normal = out.read_bytes()
        placement = root / 'placed.txt'
        placement.write_bytes(b'place 62976 1536 23040 0 35\t\r\n' + stand.read_bytes())
        run(placement).check_returncode()
        if normal != out.read_bytes():
            raise RuntimeError('placing at the authored start altered the trace')
        out.unlink()
        placement.write_bytes(b'place +62976 1536 23040 0 35\n0\n')
        proc = run(placement)
        if not proc.returncode or b'expected signed placement decimal' not in proc.stderr + proc.stdout or out.exists():
            raise RuntimeError('malformed placement was not refused before tracing')
        print('PASS authored placement identity and invalid-placement control')

        replay('pistols', end=routes['pistols'])
        bounded = out.read_bytes()
        body, producer, profile, ends = compare.port_body(bounded)
        fixture = fixtures / 'pistols'

        def stamp(record):
            return json.dumps(record, separators=(',', ':')).encode()

        def seal(rows, boundary):
            footer = {'schema': 20, 'producer': producer, 'unavailable': profile,
                      'coverageEnds': boundary, 'recordsHash': compare.fnv32(rows)}
            footer['hash'] = compare.fnv32(stamp(footer))
            return rows + stamp(footer) + b'\n'

        test = [sys.executable, str(TOOL), str(fixture), str(out), '--producer', producer,
                '--require', 'camera']
        for accept, status in (([], 1), (['--accept-profile-exclusions'], 0)):
            proc = subprocess.run(test + accept, capture_output=True, timeout=20)
            if proc.returncode != status:
                raise RuntimeError(f'coverage CLI: expected {status}, got {proc.returncode}: {proc.stderr!r}')
        for invalid in ({}, {'camera': {'lastTick': 44, 'reason': 'firearms'}},
                        {'camera': {'lastTick': 46, 'reason': 'firearms'}},
                        {'camera': {'lastTick': 45, 'reason': 'water-entry'}},
                        {'lara': {'lastTick': 45, 'reason': 'firearms'}}):
            try:
                compare.compare(fixture, seal(body, invalid), producer)
            except ValueError as e:
                if 'coverage ends differ' not in str(e):
                    raise
            else:
                raise RuntimeError(f'accepted false coverage: {invalid}')

        rows = body.splitlines(keepends=True)
        row = json.loads(rows[45])
        del row['hash']
        row['camera']['pos'][0] += 1
        row['hash'] = compare.fnv32(stamp(row))
        rows[45] = stamp(row) + b'\n'
        diff = compare.compare(fixture, seal(b''.join(rows), ends), producer)['families']['camera']['first']
        if diff is None or (diff['tick'], diff['path']) != (45, 'camera.pos[0]'):
            raise RuntimeError(f'approved coverage hid a mismatch on its last tick: {diff}')
        print('PASS sealed coverage cut, strict-vs-projected verdict, rehashed false cuts and last-covered-tick mismatch')

        # The Draw bit is recorded but not simulated by this harness. It
        # must change only trace metadata, never the supported rig or gaze.
        replay('look', end=routes['look'])
        armed, _, _, armed_ends = compare.port_body(out.read_bytes())
        _, masks = compare.fixture_data(fixtures / 'look')
        no_draw = [mask & ~256 for mask in masks]
        placement.write_text(' '.join(map(str, no_draw)) + '\n', encoding='ascii')
        run(placement).check_returncode()
        unarmed, _, _, unarmed_ends = compare.port_body(out.read_bytes())
        assert armed_ends and not unarmed_ends
        for a, b in zip(compare.records(armed, masks, 'port'),
                        compare.records(unarmed, no_draw, 'port')):
            del a['lara']['input'], b['lara']['input']
            if a != b:
                raise RuntimeError(f'coverage changed simulation at tick {a["tick"]}')
        print('PASS Draw coverage metadata does not alter camera/gaze or other simulation')

        replay('fixed-camera')
        placed_body, _, _, _ = compare.port_body(out.read_bytes())
        source = fixtures / 'fixed-camera'
        forged = root / 'bad-placement'
        forged.mkdir()
        manifest = json.loads((source / 'manifest.json').read_bytes())
        altered = (source / 'input.txt').read_bytes().replace(b'44980', b'44981', 1)
        manifest['placement'][0] += 1
        manifest['inputSHA256'] = hashlib.sha256(altered).hexdigest()
        (forged / 'input.txt').write_bytes(altered)
        (forged / 'manifest.json').write_bytes(stamp(manifest))
        (forged / 'trace.jsonl').write_bytes((source / 'trace.jsonl').read_bytes())

        def expect_bad_placement(directory, rows, origin):
            try:
                compare.compare(directory, seal(rows, {}), producer)
            except ValueError as e:
                if not str(e).startswith(f'{origin}: tick 0: Lara position, rotation or room'):
                    raise
            else:
                raise RuntimeError(f'{origin} trace accepted a different tick-0 placement')

        expect_bad_placement(forged, placed_body, 'retail')
        records = placed_body.splitlines(keepends=True)
        for field, change in (('pos', [44981, 0, 33280]),
                              ('rot', [1, 16384, 0]), ('room', 1)):
            row = json.loads(placed_body.split(b'\n', 1)[0])
            del row['hash']
            row['lara'][field] = change
            row['hash'] = compare.fnv32(stamp(row))
            records[0] = stamp(row) + b'\n'
            expect_bad_placement(source, b''.join(records), 'port')
        print('PASS manifest/input relocation and rehashed port tick-0 placement mismatches rejected')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('wall', type=Path)
    parser.add_argument('fixture_directory', type=Path)
    parser.add_argument('--route', help='execute one directed control')
    parser.add_argument('--baseline-host', type=Path, help='historical dry executable for the opening-eye negative')
    options = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    check(options.executable, options.wall, options.fixture_directory, os.environ['TMPDIR'],
          options.route, options.baseline_host)
