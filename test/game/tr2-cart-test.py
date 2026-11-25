#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile existing Great Wall inputs for scene.py and compare every state hash."""
import argparse
import json
import re
from pathlib import Path

ROUTES = ('stand','movement','ledge-grab','pool-swim','flare','pistols',
          'sidestep-blocked','tiger-cave','spider-shoot')
HASH = re.compile(r'game-tick: tick=(\d+) hash=([0-9a-f]{8}) tick_us=\d+ hash_schema=21')
SCHEMA21_INPUT = {'tiger-cave', 'spider-shoot'}
WORK = re.compile(r'scene-work: tick=(\d+) total_us=(\d+) .*')
DRAW = re.compile(r'game-render: room=\d+ .* drawables=(\d+) .* triangles=(\d+) .*')


def observe(ctx, args):
    if args or not ctx.get('shot') or ctx['meta']['hash_schema'] != 21:
        raise ValueError('Great Wall state gate requires schema 21 and a fresh image')
    tick_count = int(ctx['meta']['ticks'], 0)
    lines = ctx['log'].splitlines()
    hashes = [(int(m[1]),m[2]) for s in lines if (m:=HASH.fullmatch(s))]
    if [t for t,_ in hashes] != list(range(tick_count+1)):
        raise ValueError('Great Wall expected contiguous hashes including tick 0')
    draws = [(int(m[1]),int(m[2])) for s in lines if (m:=DRAW.fullmatch(s))]
    if len(draws)!=tick_count+1 or not all(tris for _,tris in draws) or not any(n for n,_ in draws):
        raise ValueError('Great Wall missing a called, nonempty game draw')
    work = [(int(m[1]),int(m[2])) for s in lines if (m:=WORK.fullmatch(s))]
    if [t for t,_ in work] != list(range(1,tick_count+1)):
        raise ValueError('Great Wall expected one work sample per simulated frame')
    if len([s for s in lines if s.startswith('code: load load pages=')]) != 1 or not any(
            'load-scope:' in s and s.endswith('released=1') for s in lines):
        raise ValueError('Great Wall did not use a single balanced outer load lease')
    for unit in ('tr2_wall','tr2_creatures'):
        if len([s for s in lines if s.startswith(f'code: load {unit} pages=')]) != 1:
            raise ValueError(f'Great Wall did not select {unit} exactly once')
    shot=ctx['shot'].read_bytes()
    header=b'P6\n320 240\n255\n'
    if not shot.startswith(header) or len(shot)!=len(header)+320*240*3:
        raise ValueError('Great Wall expected a canonical 320x240 RGB image')
    rgb=shot[len(header):]
    if len(set(zip(rgb[0::3],rgb[1::3],rgb[2::3],strict=True)))<8:
        raise ValueError('Great Wall image is blank')
    (ctx['artifacts']/'hashes.txt').write_text(''.join(f'{tick} {digest}\n' for tick,digest in hashes))
    (ctx['artifacts']/'work.json').write_text(json.dumps(work)+'\n')
    ctx['results']['tr2-ticks']=len(hashes)


CHECKS = {'tr2-world': observe}


def create(fixtures, out):
    out.mkdir(parents=True,exist_ok=True)
    for route in ROUTES:
        source = fixtures / 'schema21' if route in SCHEMA21_INPUT else fixtures
        rows = (source/route/'input.txt').read_text(encoding='ascii').splitlines()
        place = ''
        if rows[0].startswith('place '):
            _, x,y,z,yaw,room = rows.pop(0).split()
            place = f'place {room} {x} {y} {z} {yaw}\n'
        masks = [int(m) for m in rows]
        if route == 'sidestep-blocked' and not any(m & 65536 for m in masks):
            raise ValueError('sidestep-blocked must exercise the high action bit')
        if route == 'movement':
            # Exercise Walk in addition to the fixture's run, turn, jump and roll.
            masks += [0]*20 + [72]*30
        events = ''.join(f'at {i} pad {m} 0 0\n' for i,m in enumerate(masks) if i==0 or m!=masks[i-1])
        (out/f'{route}.route').write_text(f'scene tr2-{route}\nstart boot\nseed 1\n'
                                            f'load level wall\ngame\nhash-schema 21\n'
                                            f'{place}ticks {len(masks)}\n{events}observe tr2-world\n')


def compare(root):
    for route in ROUTES:
        h=root/'host'/f'tr2-{route}'/'hashes.txt'
        c=root/'ares'/f'tr2-{route}'/'hashes.txt'
        a=h.read_text().splitlines(); b=c.read_text().splitlines()
        if len(a)!=len(b) or len(a)<2:
            raise ValueError(f'{route}: different or empty trace lengths')
        for i,(left,right) in enumerate(zip(a,b)):
            if left!=right:
                raise ValueError(f'{route}: first differing tick {i}: host {left}, cart {right}')
        print(f'{route}: {len(a)} of {len(a)} state hashes equal')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='op',required=True)
    gen=sub.add_parser('generate'); gen.add_argument('fixtures',type=Path); gen.add_argument('out',type=Path)
    check=sub.add_parser('compare');check.add_argument('scene_artifacts',type=Path)
    args=p.parse_args()
    if args.op=='generate':create(args.fixtures,args.out)
    else:compare(args.scene_artifacts)
