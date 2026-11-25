#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate immutable named scenes and verify production-path behaviour."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from input import assemble, write_masks

HERE = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def rows(path):
    result = [json.loads(line) for line in Path(path).read_text().splitlines()]
    for tick, row in enumerate(result):
        require(row['schema'] == 2 and row['tick'] == tick, f'{path}: invalid schema/tick')
        body = dict(row)
        supplied = body.pop('hash')
        text = json.dumps(body, separators=(',', ':')).encode('ascii')
        value = 2166136261
        for byte in text:
            value = ((value ^ byte) * 16777619) & 0xffffffff
        require(supplied == f'{value:08x}', f'{path}: invalid record hash at tick {tick}')
    return result


def tool_hash():
    digest = hashlib.sha256()
    for path in sorted(HERE.rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            digest.update(path.relative_to(HERE).as_posix().encode() + b'\0')
            digest.update(path.read_bytes())
    return digest.hexdigest()


def run(executable, level, inputs, ticks, trace):
    subprocess.run([str(executable), '--level', str(level), '--input', str(inputs),
                    '--ticks', str(ticks), '--out', str(trace)], check=True)


def yaw_delta(left, right):
    return (right - left + 32768) % 65536 - 32768


def displacement(trace, tick=-1):
    first, last = trace[0]['lara'], trace[tick]['lara']
    dx = last['pos'][0] - first['pos'][0]
    dz = last['pos'][2] - first['pos'][2]
    # Both retail starts are cardinal. These are coordinate definitions, not captures.
    axes = {0: (0, 1), 16384: (1, 0), -16384: (-1, 0), -32768: (0, -1)}
    sx, cz = axes[first['rot'][1]]
    return dx * sx + dz * cz, dx * cz - dz * sx


def check_motion(fixtures):
    traces = {fixture['name']: rows(fixture['trace']) for fixture in fixtures}
    for fixture in fixtures:
        name = fixture['name']
        trace = traces[name]
        require(len(trace) == fixture['ticks'] + 1, f'{name}: wrong trace length')
        first, last = trace[0]['lara'], trace[-1]['lara']
        states = {row['lara']['state'] for row in trace}
        short_name = name.split('/')[1]
        forward, lateral = displacement(trace)
        # State IDs are from src/fixed/lara.h:LARA_STATES at the pinned revision.
        if short_name == 'idle':
            require(all(row['lara']['pos'] == first['pos'] for row in trace), name + ': idle moved')
            require(states == {2}, name + ': idle must remain in STOP')
            require(all(row['lara']['rot'] == first['rot'] for row in trace), name + ': idle rotated')
            require(len({row['lara']['frame'] for row in trace}) > 1, name + ': idle frame frozen')
            print(f"PASS {name} constant position {first['pos']}, breathing frames {first['frame']}..{last['frame']}")
        elif short_name == 'run-forward':
            require(1 in states and forward > 0, name + ': must enter RUN and move forwards')
            require(trace[0]['lara']['state'] == 2, name + ': must start stopped')
            require(len({row['lara']['anim'] for row in trace}) > 1, name + ': run animation did not change')
            changes = []
            for row in trace:
                pair = [row['lara']['anim'], row['lara']['state']]
                if not changes or pair != changes[-1][1:]:
                    changes.append([row['tick']] + pair)
            print(f"PASS {name} positions " + json.dumps({str(t): trace[t]['lara']['pos'] for t in (0,30,150)})
                  + ' anim/state transitions=' + json.dumps(changes))
        elif short_name == 'walk':
            running = traces[name.split('/')[0] + '/run-forward']
            run_forward, _ = displacement(running, fixture['ticks'])
            require(0 in states and 0 < forward < run_forward,
                    name + ': WALK must move forwards less than RUN over equal ticks')
            print(f'PASS {name} WALK reached; walk distance < run at same tick')
        elif short_name in ('turn-left', 'turn-right'):
            turn = sum(yaw_delta(a['lara']['rot'][1], b['lara']['rot'][1]) for a,b in zip(trace,trace[1:]))
            wanted = 7 if short_name == 'turn-left' else 6
            require(wanted in states and (turn < 0 if wanted == 7 else turn > 0),
                    name + ': wrong turn state/sign')
            print(f'PASS {name} turn state/sign correct')
        elif short_name == 'jump-forward':
            require(3 in states and any(row['lara']['flags'] & 2 and row['lara']['vSpeed'] < 0
                                       and row['lara']['pos'][1] < first['pos'][1] for row in trace),
                    name + ': jump must leave ground upwards under gravity')
            require(not last['flags'] & 2 and last['vSpeed'] == 0, name + ': jump must land')
            require(forward > 0, name + ': jump must move forwards')
            print(f'PASS {name} airborne upwards then landed')
        elif short_name == 'back-hop':
            require(5 in states and forward < 0, name + ': BACK_FAST must move against facing')
            print(f'PASS {name} backwards displacement')
        elif short_name in ('side-left', 'side-right'):
            wanted = 22 if short_name == 'side-left' else 21
            require(wanted in states and (lateral < 0 if wanted == 22 else lateral > 0),
                    name + ': wrong step state/lateral sign')
            print(f'PASS {name} lateral step state/sign correct')
        elif short_name == 'roll':
            require({45,23} <= states and (last['rot'][1]-first['rot'][1]) % 65536 == 32768,
                    name + ': ROLL_START/END must reverse facing')
            print(f'PASS {name} roll reversed facing')
        elif short_name == 'wall':
            require(forward > 0 and all(row['lara']['pos'] == last['pos'] for row in trace[-30:]),
                    name + ': forward movement must stop at wall')
            print(f"PASS {name} blocked position={last['pos']}")
        elif short_name == 'fall':
            require(last['health'] < first['health'] and any(row['lara']['vSpeed'] > 100 for row in trace),
                    name + ': fall must accelerate and cause damage')
            print(f"PASS {name} damaging fall health={last['health']}")
        elif short_name == 'swim':
            require({52,17,33} <= states and {0,2,3} <= {row['lara']['water'] for row in trace},
                    name + ': require swan dive, underwater swim, surface tread')
            require(last['state'] == 33 and last['pos'] == trace[-30]['lara']['pos'], name + ': must tread at surface')
            print(f'PASS {name} swan dive -> underwater swim -> surface tread; water states=0,3,2')
        elif short_name == 'underwater-start':
            require(first['water'] == 3 and last['water'] == 2, name + ': underwater setup then surfacing')
            print(f'PASS {name} initial water=3 derived by updateWaterState; surface water=2')
        else:
            raise ValueError('scene lacks a behaviour check: ' + name)


def verify(executable, fixtures, temporary):
    for stage in ('rerun', 'fresh-build'):
        if stage == 'fresh-build':
            subprocess.run([str(HERE / 'build.sh'), '--fresh'], check=True)
        for fixture in fixtures:
            check = temporary / 'check.jsonl'
            run(executable, fixture['level'], fixture['input'], fixture['ticks'], check)
            require(check.read_bytes() == Path(fixture['trace']).read_bytes(),
                    f"nondeterminism: {stage} {fixture['name']}")
        print(f'PASS determinism {stage}: {len(fixtures)}/{len(fixtures)} byte-identical traces', flush=True)
    check_motion(fixtures)
    baseline = next(f for f in fixtures if f['name'] == 'LEVEL1/run-forward')
    masks = [int(line) for line in Path(baseline['input']).read_text().splitlines()]
    masks[30] |= 2
    inputs, trace = temporary / 'negative.masks', temporary / 'negative.jsonl'
    write_masks(inputs, masks)
    run(executable, baseline['level'], inputs, len(masks), trace)
    original, changed = rows(baseline['trace']), rows(trace)
    require(len(original) == len(changed) == len(masks) + 1, 'negative control: wrong trace lengths')
    require(original[:31] == changed[:31], 'negative control changed earlier records')
    require(all(a['hash'] != b['hash'] for a,b in zip(original[31:],changed[31:])), 'negative control failed to change every later hash')
    require(original[31]['lara']['rot'] != changed[31]['lara']['rot'], 'negative control did not change rotation')
    require(original[-1]['lara']['pos'] != changed[-1]['lara']['pos'], 'negative control did not change position')
    print('PASS negative control: tick 31 adds LEFT; records 0..30 equal; hashes 31..150 differ; rotation and final position differ')
    # Exercise the executable reader, not only the script assembler.
    one = temporary / 'protocol.masks'
    result = temporary / 'protocol.jsonl'
    decimal = None
    for literal in ('010', '10', '0x0a'):
        one.write_text(literal + '\n')
        run(executable, baseline['level'], one, 1, result)
        observed = result.read_bytes()
        require(rows(result)[1]['lara']['input'] == 10, 'numeric input was not decimal/hex')
        if decimal is None:
            decimal = observed
        require(observed == decimal, 'decimal and hexadecimal readers disagree')
    for literal, reason in (('1024', 'START/free camera'), ('0b10', 'integer must be'),
                            ('+8', 'integer must be'), ('0x', 'invalid integer')):
        one.write_text(literal + '\n')
        rejected = subprocess.run([str(executable), '--level', baseline['level'], '--input', str(one),
                                   '--ticks', '1', '--out', str(result)], capture_output=True, text=True)
        require(rejected.returncode == 2 and reason in rejected.stderr,
                'production parser failed to reject ' + literal)
    gym = next(f for f in fixtures if f['name'] == 'GYM/swim')
    one.write_text('start 13 40448 4608 60928 0\n0\n')
    rejected = subprocess.run([str(executable), '--level', gym['level'], '--input', str(one),
                               '--ticks', '1', '--out', str(result)], capture_output=True, text=True)
    require(rejected.returncode == 2 and 'different room' in rejected.stderr,
            'placement must reject the room-13 point resolving into water room 14')
    print('PASS production protocol: decimal/hex agreement; START/malformed integers/wrong-room placement rejected')


def publish(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + '.new')
    shutil.copy2(source, temporary)
    os.replace(temporary, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('set', choices=['m2-start'])
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--update', action='store_true', help='deliberately replace changed fixture identities/traces')
    args = parser.parse_args()
    build = Path(os.environ.get('TR64_ORACLE_BUILD', '/tmp/tr64/builds/Oracle'))
    executable = build / 'tr1-oracle'
    data = Path(os.environ.get('TR64_DATA', '/tmp/tr64/data')) / 'pc'
    output = Path('/tmp/tr64/fixtures/oracle') / args.set
    output.mkdir(parents=True, exist_ok=True)
    with (output / 'fixtures.lock').open('w') as lock, tempfile.TemporaryDirectory(dir=build, prefix='fixtures-') as work:
        fcntl.flock(lock, fcntl.LOCK_EX)
        temporary = Path(work)
        manifest_path = output / 'manifest.json'
        old = json.loads(manifest_path.read_text()) if manifest_path.exists() else None
        identity = dict(schema=2, fixture_set=args.set, openlara_commit=(build / 'revision').read_text().strip(),
                        tool_sha256=tool_hash(), build_key=(build / 'key').read_text().strip())
        def unchanged(condition, reason):
            if old is not None and not args.update and not condition:
                raise ValueError('fixture reference changed: ' + reason + '; use --update to replace deliberately')
        for key, value in identity.items():
            unchanged(old is None or old.get(key) == value, key)
        prior = {entry['name']: entry for entry in old['fixtures']} if old else {}
        fixtures = []
        for level_name in ('LEVEL1', 'GYM'):
            candidates = [path for path in data.iterdir() if path.stem.upper() == level_name and path.suffix.lower() == '.phd']
            if len(candidates) != 1:
                raise ValueError(f'expected exactly one {level_name}.phd in {data}')
            level = candidates[0]
            for script in sorted((HERE / 'scripts').glob('*.input')):
                if script.stem.startswith(('gym-', 'level1-')):
                    if not script.stem.startswith(level_name.lower() + '-'):
                        continue
                    short_name = script.stem.split('-', 1)[1]
                else:
                    short_name = script.stem
                name = level_name + '/' + short_name
                target = temporary / level_name
                target.mkdir(exist_ok=True)
                inputs = target / (short_name + '.masks')
                start, masks = assemble(script.read_text())
                write_masks(inputs, masks, start)
                trace = target / (short_name + '.jsonl')
                entry = dict(name=name, level=str(level), level_sha256=sha(level),
                             script=str(script.relative_to(HERE)), input=str(inputs),
                             input_sha256=sha(inputs), ticks=len(masks), trace=str(trace), start=start)
                previous = prior.get(name)
                unchanged(previous is not None, name + ' is a new scene')
                if previous:
                    for key in ('level_sha256','input_sha256','ticks','start'):
                        unchanged(previous.get(key) == entry[key], name + '/' + key)
                    unchanged(Path(previous['trace']).is_file() and sha(previous['trace']) == previous['trace_sha256'], name + '/stored trace')
                    unchanged(Path(previous['input']).is_file() and sha(previous['input']) == previous['input_sha256'], name + '/stored input')
                run(executable, level, inputs, len(masks), trace)
                entry['trace_sha256'] = sha(trace)
                unchanged(previous is None or previous['trace_sha256'] == entry['trace_sha256'], name + '/regenerated trace')
                fixtures.append(entry)
        unchanged(set(prior) == {entry['name'] for entry in fixtures}, 'scene set')
        initial_binary = sha(executable)
        if args.verify:
            verify(executable, fixtures, temporary)
        for entry in fixtures:
            for key in ('input', 'trace'):
                source = Path(entry[key])
                destination = output / entry['name'].split('/')[0] / source.name
                publish(source, destination)
                entry[key] = str(destination)
        publish(executable, output / 'tr1-oracle')
        publish(HERE / 'LICENSE-OPENLARA', output / 'LICENSE-OPENLARA')
        if args.verify:
            for name in ('negative.masks','negative.jsonl'):
                publish(temporary / name, output / name)
        manifest = dict(identity, executable_sha256=sha(executable), executable=str(output / 'tr1-oracle'),
                        initial_executable_sha256=initial_binary, flags=(build / 'flags').read_text().strip(),
                        compiler=(build / 'compiler').read_text(), verified=args.verify, fixtures=fixtures)
        candidate = temporary / 'manifest.json'
        candidate.write_text(json.dumps(manifest, indent=2) + '\n')
        publish(candidate, manifest_path)
        print(f'fixtures={len(fixtures)} manifest={manifest_path}')


if __name__ == '__main__':
    main()
