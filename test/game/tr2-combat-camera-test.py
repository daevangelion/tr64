#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replay Combat and check unchanged record streams on the inherited routes."""
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

BASELINE = 'tr64-tr2-wet-camera'
PRODUCER = 'tr64-tr2-combat-camera'
COMBAT = ('pistols', 'look', 'flare-drop')


def check_route(args, name, output):
    fixture = args.fixtures / name
    new = output / f'{name}.jsonl'
    result = subprocess.run(['rtk', str(args.host), '--game-tr2', str(args.pack),
                             str(fixture / 'input.txt'), str(new)], capture_output=True, timeout=45)
    (output / f'{name}.log').write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    artifact = new.read_bytes()
    report = compare.compare(fixture, artifact, args.producer)
    before = (args.baseline / new.name).read_bytes()
    old = compare.compare(fixture, before, BASELINE)
    old_body, _, old_exclusions, old_ends = compare.port_body(before)
    new_body, _, exclusions, ends = compare.port_body(artifact)
    assert old_exclusions == exclusions, name
    count = report['records']
    if name in COMBAT:
        if 'camera' in ends:
            raise RuntimeError(f'{name}: camera still ends at {ends["camera"]}')
        for family, metric in (('camera', 'equal'), ('lara', 'knownEqual'), ('rng[0]', 'equal')):
            family_report = report['families'][family]
            if family_report[metric] != count:
                raise RuntimeError(f'{name}: first {family} difference {family_report["first"]}')
        _, masks, placement = compare._fixture_data(fixture)
        a = compare.records(old_body, masks, 'baseline', placement)
        b = compare.records(new_body, masks, 'port', placement)
        allowed_gaze = ('head', 'torso') if name == 'look' else ()
        for left, right in zip(a, b, strict=True):
            for field in ('rng', 'world', 'active', 'inventory', 'save'):
                assert left[field] == right[field], (name, left['tick'], field)
            for field in left['lara']:
                if field not in allowed_gaze:
                    assert left['lara'][field] == right['lara'][field], (name, left['tick'], field)
            if left['tick'] <= old_ends['camera']['lastTick']:
                assert left['camera'] == right['camera'], (name, left['tick'], 'camera prefix')
        print(f'PASS {name}: {count} complete camera, comparable Lara and gameplay RNG rows', flush=True)
    else:
        assert old_body == new_body and old_ends == ends and old['families'] == report['families'], name
        print(f'PASS {name}: {count} inherited rows byte-identical', flush=True)
    (output / f'{name}.compare.json').write_text(json.dumps(report, indent=2) + '\n')


def check(args, output):
    routes = args.route or sorted(f.name for f in args.fixtures.iterdir()
                                  if (args.baseline / f'{f.name}.jsonl').is_file())
    if not args.route and (len(routes) != 30 or not set(COMBAT).issubset(routes)):
        raise RuntimeError('expected all 30 accepted wet-camera baseline artifacts')
    for name in routes:
        check_route(args, name, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('host', 'pack', 'fixtures', 'baseline'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--route', action='append')
    parser.add_argument('--producer', default=PRODUCER)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir()
        check(args, args.output)
    else:
        with tempfile.TemporaryDirectory(prefix='tr2-combat-camera-', dir=os.environ['TMPDIR']) as work:
            check(args, Path(work))
