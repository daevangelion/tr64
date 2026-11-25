#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify the full Lara and camera streams on the six follow-up routes."""
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

ROUTES = ('ladder-hang-steps', 'ledge-edge', 'wade-step', 'wade-wall', 'pool-exit', 'pool-surface')


def sidestep_alias(args, out):
    fixture = args.fixtures / 'wade-wall'
    _, masks, placement = compare._fixture_data(fixture)
    # Continue the existing dedicated right step, switch to Walk+Right
    # and then release Walk while retaining the raw direction. The last
    # tick must lose the step request, not inherit a wading latch.
    steps = [*masks[:39], 32768, 64 | 4, 4]
    script, trace = (out / name for name in ('sidestep-alias.input', 'sidestep-alias.jsonl'))
    script.write_text('place ' + ' '.join(map(str, placement)) + '\n' +
                      '\n'.join(map(str, steps)) + '\n')
    subprocess.run(['rtk', str(args.host), '--game-tr2', str(args.pack), str(script), str(trace)],
                   capture_output=True, check=True, timeout=45)
    body, producer, _, ends = compare.port_body(trace.read_bytes())
    assert producer == args.producer and not ends
    rows = list(compare.records(body, steps, 'sidestep alias control', placement))
    got = [(row['lara']['water'], row['lara']['state'], row['lara']['goal']) for row in rows[40:]]
    expected = [(1, 21, 21), (1, 21, 21), (1, 21, 2)]
    if got != expected:
        raise RuntimeError(f'wading dedicated/alias/released Walk control: {got}, expected {expected}')
    print('PASS wading sidestep: dedicated and Walk+Right continue; releasing Walk ends the alias', flush=True)


def replay(args, out):
    failures = []
    for name in args.route or ROUTES:
        fixture = args.fixtures / name
        trace = out / f'{name}.jsonl'
        proc = subprocess.run(['rtk', str(args.host), '--game-tr2', str(args.pack),
                               str(fixture / 'input.txt'), str(trace)], capture_output=True, timeout=45)
        (out / f'{name}.log').write_bytes(proc.stdout + proc.stderr)
        if proc.returncode:
            failure = proc.stderr.decode(errors='replace').strip()
            print(f'FAIL {name}: {failure}', flush=True)
            failures.append(name)
            continue
        artifact = trace.read_bytes()
        report = compare.compare(fixture, artifact, args.producer)
        assert not report['coverageEnds'], (name, report['coverageEnds'])
        (out / f'{name}.compare.json').write_text(json.dumps(report, indent=2) + '\n')
        retail, masks, placement = compare._fixture_data(fixture)
        body, _, _, _ = compare.port_body(artifact)
        a = compare.records(retail, masks, 'retail', placement)
        b = compare.records(body, masks, 'port', placement)
        mismatches = {}
        for left, right in zip(a, b, strict=True):
            for key in ('lara', 'camera'):
                if left[key] != right[key] and key not in mismatches:
                    diff, _ = compare.first_difference(left[key], right[key], key, set())
                    mismatches[key] = {'tick': left['tick'], **diff}
        rng = report['families']['rng[0]']['first']
        if mismatches:
            failures.append(name)
            print(f'FAIL {name}: {mismatches}; first gameplay RNG difference: {rng}', flush=True)
        else:
            print(f'PASS {name}: {report["records"]} raw Lara and camera records; first gameplay RNG difference: {rng}', flush=True)
    if not args.route or 'wade-wall' in args.route:
        sidestep_alias(args, out)
    return bool(failures)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('host', 'pack', 'fixtures'):
        parser.add_argument(arg, type=Path)
    parser.add_argument('--producer', required=True)
    parser.add_argument('--route', choices=ROUTES, action='append')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir()
        raise SystemExit(replay(args, args.output))
    with tempfile.TemporaryDirectory(prefix='tr2-moves2-', dir=os.environ['TMPDIR']) as work:
        raise SystemExit(replay(args, Path(work)))
