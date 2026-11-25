#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Items that start invisible and are activated by a trigger (oracle host.patch hunk 28; docs/specs/wall/
triggers-and-effects.md section 4, object action: a non-enemy item that is activated becomes status active).

An item with bit 8 of its level flags starts with status "invisible". The source's trigger code turned only a status of 0 into
active, so such an item, once triggered, stayed invisible: not drawn, skipped by the collision loop. Enemies take their own
rule (Enemy::update: take a creature slot, then become active), which is the specification's; every other item is repaired
by the trigger.

  invisible.py scan  --out DIR                   every item with the flag in the 16 PC and 16 PlayStation levels, its type,
                                                  whether it is an enemy, the trigger sectors that name it
  invisible.py run   --executable HOST --oracle ORACLE --control-oracle NO28 --out DIR
                                                  every non-enemy instance of the PC levels: activated by the placed-state
                                                  directive `activate` with Lara standing beside (or under) it; oracle against
                                                  port (all records), and the status field of the item in the oracle's records
                                                  against the oracle without hunk 28 (`test/oracle/control.sh no-hunk-28`)

Run with PYTHONPATH=tools/cook:test/game.
"""
import argparse
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
import directed as d
import src_psx
import switches
from directed import NONE

PC = ['level1', 'level2', 'level3a', 'level3b', 'level4', 'level5', 'level6', 'level7a', 'level7b', 'level8a', 'level8b',
      'level8c', 'level10a', 'level10b', 'level10c']
HEADER = Path(__file__).resolve().parents[2] / 'game' / 'internal.h'
STATUS = {0: 'none', 1: 'active', 2: 'inactive', 3: 'invisible'}
# What each non-enemy type does once it is visible and active (docs/specs/wall/objects-missing.md).
DOES = {
    48: 'pushable block: solid, Lara pushes or pulls it (section 2)',
    53: 'falling ceiling: hangs 40 frames, falls, 300 hit points a tick of contact (section 7.2)',
    84: 'pistols pickup: drawn, Lara can take it (section 25)',
    110: 'puzzle item 1 pickup: drawn, Lara can take it (section 25)',
    182: 'boat: drawn, Lara can board it (section 19)',
}


def type_names():
    names = {}
    for m in re.finditer(r'\bITEM_([A-Z0-9_]+)\s*=\s*(\d+)', HEADER.read_text()):
        names.setdefault(int(m.group(2)), m.group(1))
    return names


NAMES = type_names()


def is_enemy(t):
    """Intelligent objects: Lara's doppelganger and creatures (wolf 7 .. natla 28, the 29/30 skater and cowboy line, Mr T,
    the mutants, Adam), as the specification lists them (lara-and-enemies.md)."""
    return 7 <= t <= 28 or t in (29, 30, 31, 32, 33, 34)


class Stub:
    def __init__(self, level):
        self.level = level


def levels():
    for n in PC:
        yield 'pc', n, d.src_pc.load(Path(d.DATA) / f'{n}.phd')
    for p in sorted(Path('/tmp/tr64/data/psx/PSXDATA').glob('LEVEL*.PSX')):
        yield 'psx', p.stem.lower(), src_psx.load(p)


def scan():
    rows = []
    for source, name, level in levels():
        items = level.data['items']
        names = {}
        for ri, index, ttype, acts in switches.triggers(Stub(level)):
            room = level.rooms[ri]
            cell = ((room.info['x'] + (index // room.z_sectors) * 1024) // 1024, (room.info['z'] + (index % room.z_sectors) * 1024) // 1024)
            for a in acts:
                names.setdefault(a, []).append((ri, cell, ttype))
        for i, it in enumerate(items):
            if it['flags'] & 0x100:
                rows.append(dict(source=source, level=name, item=i, type=it['type'], name=NAMES.get(it['type'], '?'),
                                 enemy=is_enemy(it['type']), room=it['room'], triggers=names.get(i, [])[:4],
                                 triggered=i in names))
    return rows


def status_of(rows, index):
    out = []
    for r in rows:
        for a in r['active']:
            if a['index'] == index:
                out.append((r['tick'], (a['flags'] >> 3) & 3, a['state'], a['anim']))
    return out


# Items whose nearest standable cells all fail the scene (the block stands on a ledge of its room): the
# offset from the item of the first cell tried, measured to work.
PREFERRED = {('level4', 19): (5120, 0)}


def placements(env, it, key=None):
    """Standable ground in the item's room, nearest cells first (with 1,024, 512, then 256 of headroom)."""
    room = env.level.rooms[it['room']]
    cells = [(room.info['x'] + i * 1024 + 512, room.info['z'] + j * 1024 + 512)
             for i in range(room.x_sectors) for j in range(room.z_sectors)]
    cells.sort(key=lambda c: abs(c[0] - it['x']) + abs(c[1] - it['z']))
    if key in PREFERRED:
        dx, dz = PREFERRED[key]
        cells.insert(0, (it['x'] + dx, it['z'] + dz))
    for clear in (1024, 512, 256):
        for x, z in cells:
            g = d.ground(env, it['room'], x, z, None, clear=clear)
            if g is not None:
                yield (g[0], x, g[1], z, 0), (x - it['x'], z - it['z'])


def run_instances(args):
    out, ok = [], True
    rows = [r for r in scan() if r['source'] == 'pc' and not r['enemy'] and r['triggered']]
    cache = {}
    for r in rows:
        name, idx = r['level'], r['item']
        env = cache.setdefault(name, d.Env(name, args.executable, oracle=args.oracle))
        ctl = d.Env(name, args.executable, oracle=args.control_oracle)
        it = env.level.data['items'][idx]
        row = dict(level=name, item=idx, type=r['type'], name=r['name'], does=DOES.get(r['type'], '?'))
        done = False
        for start, off in placements(env, it, (name, idx)):
            setup = [('activate', idx, 0)]
            masks = [NONE] * 150
            try:
                new = d.probe(env, start, masks, want='all', setup=setup)
                old = d.probe(ctl, start, masks, want='all', setup=setup)
            except ValueError:
                continue
            s_new, s_old = status_of(new, idx), status_of(old, idx)
            res = d.compare(env, start, masks, ticks=len(masks), setup=setup)
            health = [x['lara']['health'] for x in new if x['lara']]
            ohealth = [x['lara']['health'] for x in old if x['lara']]
            row.update(placement=start, offset=off, compare=res.get('status'),
                       status_first=STATUS[s_new[0][1]] if s_new else 'not in the active list',
                       control_status_first=STATUS[s_old[0][1]] if s_old else 'not in the active list',
                       status_sequence=[STATUS[v] for v in dict.fromkeys(x[1] for x in s_new)],
                       lara_health_min=min(health), control_lara_health_min=min(ohealth),
                       hashes_differ=new[-1]['hash'] != old[-1]['hash'])
            done = True
            break
        row['ok'] = done and row['compare'] == 'equal' and row['status_first'] == 'active' and row['control_status_first'] == 'invisible'
        ok &= row['ok']
        out.append(row)
        print(json.dumps(row, default=str), flush=True)
    return out, ok


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('action', choices=['scan', 'run'])
    p.add_argument('--executable', type=Path)
    p.add_argument('--oracle', type=Path)
    p.add_argument('--control-oracle', type=Path)
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    if args.action == 'scan':
        rows = scan()
        (args.out / 'invisible-scan.json').write_text(json.dumps(rows, indent=1) + '\n')
        for r in rows:
            print(json.dumps(r))
        print('scan', len(rows), 'items,', sum(1 for r in rows if not r['enemy']), 'non-enemy,',
              sum(1 for r in rows if not r['enemy'] and r['triggered']), 'non-enemy with a trigger')
        return 0
    rows, ok = run_instances(args)
    (args.out / 'invisible-run.json').write_text(json.dumps(rows, indent=1, default=str) + '\n')
    print('invisible', 'PASS' if ok else 'FAIL')
    return int(not ok)


if __name__ == '__main__':
    sys.exit(main())
