#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replay directed TR2 climb and blocked-movement fixtures against the pinned producer."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

TOOL = Path(__file__).resolve().parents[2] / 'tools/tr2-compare.py'
SPEC = importlib.util.spec_from_file_location('tr2_compare', TOOL)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)

DIRECTED = ('ledge-grab', 'ladder', 'sidestep-blocked', 'grab-attempt', 'look-sidestep')
BASELINE_DIFF = {
    'ledge-grab': (121, 'pos', [67685, 5143, 39376], [67685, 5226, 39376]),
    'ladder': (1, 'climbing', 1, 0),
    'sidestep-blocked': (4, 'anim', 103, 11),
}

def marked_hang_back(args, root):
    """Keep the published approach; replace the first settled-hang request."""
    placement, *masks = (args.fixtures / 'ladder/input.txt').read_text().splitlines()
    tick, back_action = 52, '144'
    script = root / 'hang-back.txt'
    script.write_text('\n'.join([placement, *masks[:tick - 1], back_action]) + '\n')
    trace = root / 'hang-back.jsonl'
    proc = subprocess.run(
        ['rtk', str(args.host), '--game-tr2', str(args.pack), str(script), str(trace)],
        capture_output=True, text=True, timeout=30)
    data = trace.read_bytes()
    try:
        compare.port_body(data)
    except ValueError:
        pass
    else:
        raise RuntimeError('marked hang Back: unsupported request produced a sealed trace')
    assert proc.returncode, proc.stdout
    assert 'TR2 marked hang step unavailable (Q-T2M-04) (count=1)' in proc.stdout + proc.stderr, proc.stderr
    rows = [json.loads(line) for line in data.splitlines()]
    assert len(rows) == tick, rows[-1]
    assert (rows[-1]['lara']['anim'], rows[-1]['lara']['frame'], rows[-1]['lara']['climbing']) == (96, 1513, 1)
    print(f'PASS marked hang Back: tick {tick} refused by name, count 1, unsealed')


def test(args):
    with tempfile.TemporaryDirectory(prefix='tr2-climb-', dir=os.environ['TMPDIR']) as root:
        port = Path(root) / 'trace.jsonl'
        for route in BASELINE_DIFF if args.baseline_control else DIRECTED:
            fixture = args.fixtures / route
            subprocess.run(['rtk', str(args.host), '--game-tr2', str(args.pack), str(fixture / 'input.txt'), str(port)],
                           capture_output=True, check=True, timeout=30)
            data = port.read_bytes()
            if args.baseline_control:
                body, producer, _, ends = compare.port_body(data)
                assert producer == 'tr64-tr2-camera' and not ends
                retail, inputs, placement = compare._fixture_data(fixture)
                a = list(compare.records(retail, inputs, 'retail', placement))
                b = list(compare.records(body, inputs, 'port', placement))
                tick, field, want, old = BASELINE_DIFF[route]
                pair = (a[tick]['lara'][field], b[tick]['lara'][field])
                if pair != (want, old):
                    raise RuntimeError(f'{route} tick {tick}: wrong baseline contrast {field} {pair}')
                print(f'RED {route} tick {tick} lara.{field}: retail {want} baseline {old}')
                continue
            report = compare.compare(fixture, data, args.producer)
            assert not report['coverageEnds'], (route, report['coverageEnds'])
            camera, lara, rng = (report['families'][f] for f in ('camera', 'lara', 'rng[0]'))
            assert 'lara.climbing' not in lara['unavailable']
            n = report['records']
            if (lara['knownEqual'], camera['equal'], rng['equal']) != (n, n, n):
                raise RuntimeError(f'{route}: first differences {[f["first"] for f in (lara, camera, rng)]}')
            print(f'PASS {route}: {n} camera, comparable Lara, live climb mark and gameplay RNG records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('host', 'pack', 'fixtures'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--baseline-control', action='store_true')
    parser.add_argument('--hang-refusal-only', action='store_true')
    parser.add_argument('--producer', default='tr64-tr2-combat-camera')
    args = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to assigned tr64 scratch')
    if args.hang_refusal_only:
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as root:
            marked_hang_back(args, Path(root))
    else:
        test(args)
