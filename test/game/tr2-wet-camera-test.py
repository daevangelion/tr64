#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify the complete pool cameras and unchanged noncamera state."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

TOOL = Path(__file__).resolve().parents[2] / 'tools/tr2-compare.py'
SPEC = importlib.util.spec_from_file_location('tr2_compare', TOOL)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)

ROUTES = ('pool-wade', 'pool-swim')


def run(host, pack, fixture, output):
    subprocess.run(['rtk', str(host), '--game-tr2', str(pack), str(fixture / 'input.txt'), str(output)],
                   capture_output=True, check=True, timeout=40)
    return output.read_bytes()


def check(args):
    with tempfile.TemporaryDirectory(prefix='tr2-wet-camera-', dir=os.environ['TMPDIR']) as root:
        trace = Path(root) / 'trace.jsonl'
        for name in args.routes or ROUTES:
            fixture = args.fixtures / name
            artifact = run(args.host, args.pack, fixture, trace)
            report = compare.compare(fixture, artifact, args.producer)
            boundary = report['coverageEnds'].get('camera')
            if boundary:
                raise RuntimeError(f'{name}: camera unavailable from tick {boundary["lastTick"] + 1}: {boundary["reason"]}')
            camera = report['families']['camera']
            total = report['records']
            if camera['equal'] != total:
                raise RuntimeError(f'{name}: first camera difference {camera["first"]}')
            lara = report['families']['lara']
            if lara['knownEqual'] != total:
                raise RuntimeError(f'{name}: first Lara difference {lara["first"]}')
            previous = run(args.baseline_host, args.pack, fixture, trace)
            _, inputs, placement = compare._fixture_data(fixture)
            old_body, old_producer, _, _ = compare.port_body(previous)
            assert old_producer == 'tr64-tr2-water'
            current_body, _, _, _ = compare.port_body(artifact)
            old = compare.records(old_body, inputs, 'baseline', placement)
            current = compare.records(current_body, inputs, 'port', placement)
            for tick, (a, b) in enumerate(zip(old, current, strict=True)):
                if a['rng'][0] != b['rng'][0]:
                    raise RuntimeError(f'{name}: gameplay RNG changed at tick {tick}')
                for family in ('lara', 'active', 'inventory', 'save', 'world'):
                    if a[family] != b[family]:
                        raise RuntimeError(f'{name}: {family} changed at tick {tick}')
            print(f'PASS {name}: all camera fields equal in {total} records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('host', 'pack', 'fixtures'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--route', dest='routes', choices=ROUTES, action='append')
    parser.add_argument('--producer', default='tr64-tr2-wet-camera')
    parser.add_argument('--baseline-host', type=Path, required=True)
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to assigned tr64 scratch')
    check(parser.parse_args())
