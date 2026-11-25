#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check pistol rules given recorded poses, never claim route equality."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def camera_free_rows(c, path, masks, placement):
    data = path.read_bytes()
    last = json.loads(data.splitlines()[-1])
    if 'producer' in last or 'recordsHash' in last:
        raise RuntimeError('diagnostic host sealed replay inputs as a live trace')
    return c.records(data,masks,'arm diagnostic',placement,21)


def check(c, host, pack, fixture, limit, scratch, perturb=False):
    wire, masks, place = c._fixture_data(fixture)
    want = list(c.records(wire,masks,'retail',place,21))[:limit+1]
    masks=masks[:limit]
    tape, keys, out = [scratch/name for name in ('poses.bin','input.txt','unsealed.jsonl')]
    with tape.open('wb') as f:
        def write(values):
            f.write(struct.pack(f'<{len(values)}i',*values))
        write([0x43313254])
        for row in want[1:]:
            lara=row['lara']
            pos=lara['pos']
            if perturb and row['tick'] == 247:
                pos=[pos[0],pos[1]+256,pos[2]]
            roster=[item for item in row['active'] if item['type'] in (36,39)]
            write([row['tick'],*pos,*lara['rot'],lara['room'],lara['anim'],lara['frame'],
                   lara['health'],lara['water'],len(roster)])
            for item in roster:
                write([item['index'],item.get('ai',{}).get('slot',-1),*item['pos'],*item['rot'],
                       item['room'],item['anim'],item['frame']])
    keys.write_text('place '+' '.join(map(str,place))+'\n'+' '.join(map(str,masks))+'\n',encoding='ascii')
    result=subprocess.run(['rtk',str(host),'--game-tr2-schema21',str(pack),str(keys),str(out)],
                          capture_output=True,text=True,timeout=40,env=os.environ|{'TR64_COMBAT_TEST_TAPE':str(tape)})
    if result.returncode:
        raise RuntimeError(result.stdout+result.stderr)
    fields=('armR','armL','weaponState','weapon','goalWeapon','lastWeapon','target','ammo')
    for a,b in zip(want,camera_free_rows(c,out,masks,place)):
        tick=a['tick']
        for field in fields:
            if a['lara'][field]!=b['lara'][field]:
                return (tick,field,a['lara'][field],b['lara'][field])
        if a['save']['ammoUsed']!=b['save']['ammoUsed']:
            return (tick,'ammoUsed',a['save']['ammoUsed'],b['save']['ammoUsed'])
    return None


def main():
    host,pack,root=map(Path,sys.argv[1:])
    path=Path(__file__).resolve().parents[2]/'tools/tr2-compare.py'
    spec=importlib.util.spec_from_file_location('tr2_compare',path)
    c=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    with tempfile.TemporaryDirectory(prefix='tr2-combat-tape-',dir=os.environ['TMPDIR']) as td:
        scratch=Path(td)
        for name,limit in (('tiger-shoot',300),('spider-shoot',380)):
            diff=check(c,host,pack,root/name,limit,scratch)
            if diff is not None:
                raise RuntimeError(f'{name}: {diff}')
            print(f'PASS {name}: {limit} updates; weapon inputs supplied, output unsealed',flush=True)
        diff=check(c,host,pack,root/'tiger-shoot',300,scratch,True)
        if diff is None or diff[0]!=247 or diff[1] not in ('armR','armL','target'):
            raise RuntimeError(f'pose input was not consumed at the first changed tick: {diff}')
        print('PASS: changed pre-weapon origin fails the aim check at tick 247:',diff)


if __name__=='__main__':
    main()
