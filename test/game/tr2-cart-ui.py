#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build v4 menu scenes from published inputs, and compare every native hash."""
import argparse
import json
from pathlib import Path
import re

HASH = re.compile(r'^game-tick: tick=(\d+) hash=([0-9a-f]{8}) tick_us=\d+ hash_schema=21$', re.M)
ROUTES = ('ring-close', 'ring-medi', 'rusty-key', 'shotgun-tiger')


def fixture(directory):
    source = (directory / 'input.txt').read_text().splitlines()
    placement = ''
    if source[0].startswith('place '):
        _, x, y, z, yaw, room = source.pop(0).split()
        placement = f'place {room} {x} {y} {z} {yaw}\n'
    meta = json.loads((directory / 'manifest.json').read_text())
    return list(map(int, source)), placement, meta


def generate(root, out):
    out.mkdir(parents=True, exist_ok=True)
    rusty, placement, key = fixture(root / 'rusty-key')
    shotgun, shot_place, gun = fixture(root / 'schema21' / 'shotgun-tiger')
    def choice(kind, event):
        # Published ticks number the first mask 1; scene pads number it 0.
        return 'at %d %s %d %d\n' % (event['tick'] - 1, kind, event['type'], event['pausedTicks'])
    # The 250 HP fall lands on scene tick 219. The marker is the next tick so
    # the fall is already applied, and the small pack is the tick after that.
    medi = list(rusty[:232])
    medi[220] = 2048
    for name, masks, place, choices in (
        ('ring-close', [0, 2048, 0, 0] + [8]*40, '', 'at 2 ring 0 34\n'),
        ('ring-medi', medi, placement, 'at 221 ring 171 34\n'),
        ('rusty-key', rusty, placement, choice('key', key['keyConfirmation'])),
        ('shotgun-tiger', shotgun, shot_place, choice('ring', gun['shotgunSelection'])),
    ):
        pads = ''.join(f'at {i} pad {m} 0 0\n' for i, m in enumerate(masks)
                       if i == 0 or m != masks[i-1])
        (out / f'{name}.route').write_text(f'scene {name}\nstart boot\nseed 1\nload level wall\n'
                                         f'game\nhash-schema 21\n{place}ticks {len(masks)}\n'
                                         f'{pads}{choices}observe tr2-world\n')


def compare(root, scenes, names):
    for name in names:
        expected = int(re.search(r'^ticks (\d+)$', (scenes / f'{name}.route').read_text(), re.M)[1]) + 1
        rows = [HASH.findall((root / platform / name / 'engine.log').read_text())
                for platform in ('host', 'ares')]
        for platform, trace in zip(('host', 'ares'), rows):
            if [int(t) for t, _ in trace] != list(range(expected)):
                raise ValueError(f'{name}: {platform} missing contiguous hashes (expected {expected})')
        for (tick, host), (_, cart) in zip(*rows):
            if host != cart:
                raise ValueError(f'{name}: tick {tick} differs: host {host}, cart {cart}')
        print(f'{name}: {expected}/{expected} equal host/cart state hashes')


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    modes = cli.add_subparsers(dest='mode', required=True)
    gen = modes.add_parser('generate')
    gen.add_argument('fixtures', type=Path)
    gen.add_argument('scenes', type=Path)
    check = modes.add_parser('compare')
    check.add_argument('evidence', type=Path)
    check.add_argument('scenes', type=Path)
    check.add_argument('routes', nargs='*', choices=ROUTES, default=ROUTES)
    args = cli.parse_args()
    if args.mode == 'generate':
        generate(args.fixtures, args.scenes)
    else:
        compare(args.evidence, args.scenes, args.routes)
