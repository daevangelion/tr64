#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replay the wet transitions and test effect admission with small pack mutations."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile


def load_item_controls():
    spec = importlib.util.spec_from_file_location('item_controls', Path(__file__).with_name('tr2-item-actions-test.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def traces(host, pack, fixtures, scratch):
    trace = scratch / 'trace.jsonl'
    directed = {
        'pool-wade': (568, 619, 729, 730, 775, 839),
        'pool-swim': (464, 583, 584, 599, 600, 608, 609, 610, 633, 640, 644, 645, 658),
    }
    for route, samples in directed.items():
        fixture = fixtures / route
        trace.unlink(missing_ok=True)
        p = subprocess.run(['rtk', str(host), '--game-tr2', str(pack), str(fixture/'input.txt'), str(trace)],
                           capture_output=True, text=True, timeout=30)
        if p.returncode:
            raise RuntimeError(p.stdout + p.stderr)
        a = [json.loads(row) for row in (fixture/'trace.jsonl').read_text().splitlines()]
        b = [json.loads(row) for row in trace.read_text().splitlines()[:-1]]
        if len(a) != len(b):
            raise RuntimeError(f'{route}: wrong number of records')
        for tick in samples:
            for field in a[tick]['lara']:
                want, got = a[tick]['lara'][field], b[tick]['lara'][field]
                if want != got:
                    raise RuntimeError(f'{route} tick {tick}: lara.{field}: expected {want}, got {got}')
        if route == 'pool-swim' and 'tr2-inert-animation-bubbles: count=4' not in p.stdout:
            raise RuntimeError('the four encountered bubbles cues were not counted')
    print('PASS: dry/wade boundary, wet bank corrections, surface travel before dive, underwater pitch and floor, air')


def cues(host, pack, fixtures, scratch):
    support = load_item_controls()
    base = support.n64_pack.inspect(pack.read_bytes())['wall']
    clips = support.rows(base, 'animations')
    dry = scratch / 'one-tick.txt'
    dry.write_text('0\n', encoding='ascii')
    candidate, trace = scratch / 'cue.pack', scratch / 'cue.jsonl'

    def run(word=None, actions=dry, clip=103, offset=0):
        if word is None:
            source = pack
        else:
            anim = clips[clip]
            sound, sounds = support.sound_commands(base, clip)
            tables = support.replace_commands(base, clip, anim, *sound, 6, anim['frame_start'] + offset, word,
                                              count=sounds + 1)
            candidate.write_bytes(support.n64_pack.build({'wall': support.wall_sections(tables)}, games={'wall': 2}))
            source = candidate
        trace.unlink(missing_ok=True)
        p = subprocess.run(['rtk', str(host), '--game-tr2', str(source), str(actions), str(trace)],
                           capture_output=True, text=True, timeout=20)
        return p, trace.read_bytes() if trace.exists() else b''

    def gameplay_records(blob):
        recs = [json.loads(row) for row in blob.decode('ascii').splitlines()[:-1]]
        for r in recs:
            r['rng'] = [r['rng'][0], None]
            r.pop('hash', None)
        return recs

    def draw_rngs(blob):
        return [json.loads(row)['rng'][1] for row in blob.decode('ascii').splitlines()[:-1]]

    p, wire = run()
    if p.returncode or b'"tick":1' not in wire:
        raise RuntimeError(f'baseline cue control could not run: {p.stdout + p.stderr}')
    wire_game, wire_draw = gameplay_records(wire), draw_rngs(wire)
    for flags, expected in ((0,1), (0x4000,1), (0x8000,0), (0xc000,0)):
        p, actual = run(flags | 3)
        if p.returncode or f'tr2-inert-animation-bubbles: count={expected}' not in p.stdout:
            raise RuntimeError(f'bubbles flag {flags:#06x}: wrong admission or count')
        if expected == 0:
            if actual != wire:
                raise RuntimeError(f'bubbles flag {flags:#06x}: filtered cue altered trace or RNG')
        else:
            if gameplay_records(actual) != wire_game:
                raise RuntimeError(f'bubbles flag {flags:#06x}: live bubble emission altered gameplay state or rng[0]')
            if draw_rngs(actual) == wire_draw:
                raise RuntimeError(f'bubbles flag {flags:#06x}: live bubble emission did not advance presentation rng[1]')
    for effect in (16,17):
        p, actual = run(effect)
        if p.returncode or actual != wire:
            raise RuntimeError(f'numbered inert effect {effect} was not inert or was refused')
    for word in (2, 0x4000 | 32):
        p, actual = run(word)
        if not p.returncode or actual or f'unsupported effect {word & 0x3fff}' not in p.stdout + p.stderr:
            raise RuntimeError(f'unsupported effect {word & 0x3fff} was not refused by name before tick 0')
    # Check the same cue through the opposite medium: rng[0] and gameplay state
    # stay identical while admitted cues emit live bubbles on the presentation stream.
    wet = fixtures / 'pool-swim' / 'input.txt'
    p, wire = run(actions=wet)
    if p.returncode or 'tr2-inert-animation-bubbles: count=4' not in p.stdout:
        raise RuntimeError('baseline submerged cue control did not reach its four authored cues')
    wire_game, wire_draw = gameplay_records(wire), draw_rngs(wire)
    for flags, expected in ((0,2), (0x4000,1), (0x8000,2), (0xc000,1)):
        # Replace the three cues of the dive clip only, at its first cue.
        # The later tread cue is kept in all runs.
        p, actual = run(flags | 3, actions=wet, clip=119, offset=7)
        if p.returncode or f'tr2-inert-animation-bubbles: count={expected}' not in p.stdout:
            raise RuntimeError(f'underwater cue flag {flags:#06x}: wrong count')
        if gameplay_records(actual) != wire_game:
            raise RuntimeError(f'underwater cue flag {flags:#06x}: altered gameplay state or rng[0]')
        if draw_rngs(actual) == wire_draw:
            raise RuntimeError(f'underwater cue flag {flags:#06x}: presentation rng[1] did not reflect changed bubble count')
    print('PASS: cue filters, rng[0] unchanged, live bubble presentation draws, valid inert 16/17, unsupported number refusal')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('pack', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('case', choices=('trace', 'cue'))
    args = parser.parse_args()
    if not (tmp := os.environ.get('TMPDIR')):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    with tempfile.TemporaryDirectory(prefix='tr2-water-', dir=tmp) as path:
        if args.case == 'trace':
            traces(args.host, args.pack, args.fixtures, Path(path))
        else:
            cues(args.host, args.pack, args.fixtures, Path(path))


if __name__ == '__main__':
    main()
