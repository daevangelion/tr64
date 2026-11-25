#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Enumerate source sector ledges and compare production controllers through pipes."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import src_pc
from stream import Run
from compare import first_difference

DIRECTIONS = [(0, 1, 0), (1, 0, 16384), (0, -1, -32768), (-1, 0, -16384)]
PATTERNS = {
    'forward-action': [(136, 240)],
    'action-forward': [(128, 15), (136, 225)],
    'jump-action': [(40, 5), (168, 100), (136, 135)],
    'handstand': [(136, 20), (128, 60), (200, 240)],
    'jump-handstand': [(32, 5), (160, 75), (200, 240)],
    'diagonal-left': [(138, 100), (136, 140)],
    'diagonal-right': [(140, 100), (136, 140)],
    'roll-action': [(152, 80), (136, 160)],
}
WATER_PATTERN = {'surface-action': [(48, 110), (32, 20), (136, 190)]}


def sector_at(level, ri, x, z):
    r = level.rooms[ri]
    ix, iz = (x-r.info['x'])//1024, (z-r.info['z'])//1024
    if not (0 <= ix < r.x_sectors and 0 <= iz < r.z_sectors):
        return None
    return r.sectors[ix*r.z_sectors+iz]


def commands(level, sector):
    index = sector['floor_index']
    result = {}
    if index:
        words = level.data['floor_data']
        while True:
            cmd = words[index]
            kind = cmd & 31
            if kind not in (1, 2, 3):
                break
            result[kind] = words[index+1]
            index += 2
            if cmd & 32768:
                break
    return result


def resolve(level, ri, x, z):
    seen = set()
    while ri not in seen:
        seen.add(ri)
        s = sector_at(level, ri, x, z)
        if s is None:
            return None
        portal = commands(level, s).get(1)
        if portal is None:
            return ri, s
        ri = portal
    return None


def height(level, ri, sector, x, z, ceiling=False):
    seen = set()
    key = 'room_above' if ceiling else 'room_below'
    while sector[key] != 255:
        ri = sector[key]
        if ri in seen:
            return None
        seen.add(ri)
        sector = sector_at(level, ri, x, z)
        if sector is None:
            return None
    base = sector['ceiling' if ceiling else 'floor']
    if base == -127:
        return None
    value = base*256
    slope = commands(level, sector).get(3 if ceiling else 2, 0)
    sx, sz = slope & 255, slope >> 8
    sx = sx-256 if sx >= 128 else sx
    sz = sz-256 if sz >= 128 else sz
    dx, dz = x & 1023, z & 1023
    if ceiling:
        value -= sx*((dx-1023) if sx < 0 else dx) >> 2
        value += sz*(dz if sz < 0 else (dz-1023)) >> 2
    else:
        value -= sx*(dx if sx < 0 else (dx-1023)) >> 2
        value -= sz*(dz if sz < 0 else (dz-1023)) >> 2
    return value


def edges(level):
    found = []
    for ri, room in enumerate(level.rooms):
        for index, s in enumerate(room.sectors):
            if s['floor'] == -127 or s['ceiling'] == -127 or 1 in commands(level, s):
                continue
            x = room.info['x']+(index//room.z_sectors)*1024+512
            z = room.info['z']+(index%room.z_sectors)*1024+512
            floor = height(level, ri, s, x, z)
            ceiling = height(level, ri, s, x, z, True)
            if floor is None or ceiling is None or floor-ceiling < 768:
                continue
            for dx, dz, angle in DIRECTIONS:
                # A floor belonging to a lower room is not a placement in this room.
                if s['room_below'] != 255:
                    continue
                nx, nz = x+dx*1024, z+dz*1024
                other = resolve(level, ri, nx, nz)
                if other is None:
                    continue
                nri, ns = other
                top = height(level, nri, ns, nx, nz)
                roof = height(level, nri, ns, nx, nz, True)
                if top is None or roof is None or not 384 <= floor-top <= 1920:
                    continue
                tags = [f'clicks-{round((floor-top)/256)}']
                tags += ['low-ceiling' if top-roof < 1024 else 'clear-pullup']
                if 2 in commands(level, s) or 2 in commands(level, ns):
                    tags += ['slope']
                if room.flags & 1 or level.rooms[nri].flags & 1:
                    tags += ['water']
                found.append(dict(room=ri, x=x, z=z, y=floor, top=top,
                                  roof=roof, dx=dx, dz=dz, angle=angle, tags=tags))
            if not room.flags & 1 or s['room_above'] == 255:
                continue
            surface = s['ceiling'] * 256
            for dx, dz, angle in DIRECTIONS:
                nx, nz = x+dx*1024, z+dz*1024
                other = resolve(level, s['room_above'], nx, nz)
                if other is None:
                    continue
                nri, ns = other
                if level.rooms[nri].flags & 1 or ns['room_below'] != 255:
                    continue
                top = height(level, nri, ns, nx, nz)
                roof = height(level, nri, ns, nx, nz, True)
                if top is None or roof is None or not -512 < top-surface <= 316:
                    continue
                found.append(dict(room=ri, x=x, z=z, y=surface+512, top=top,
                                  roof=roof, dx=dx, dz=dz, angle=angle,
                                  tags=['water-out', 'low-ceiling' if top-roof < 1024 else 'clear-pullup']))
    return found


def run_case(args, path, edge, name, ticks=240):
    ref = Run(['rtk', str(args.oracle), '--level', str(args.level), '--input', str(path), '--ticks', str(ticks)], '--out')
    native = Run(['rtk', str(args.executable), '--game-level', args.level.stem.lower(), '--game-input', str(path), '--game-ticks', str(ticks), '--game-trace-only'], '--game-trace')
    previous = None
    anomalies = []
    states = collections.Counter()
    first = None
    count = 0
    while True:
        a, b = ref.trace.readline(), native.trace.readline()
        if not a or not b:
            break
        row = json.loads(a)
        lara = row['lara']
        states[str(lara['state'])] += 1
        if previous is not None:
            dy = lara['pos'][1]-previous['pos'][1]
            # End-of-climb commands legitimately translate the root by 512/768.
            near_edge = abs(lara['pos'][0]-edge['x']) < 1024 and abs(lara['pos'][2]-edge['z']) < 1024
            if abs(dy) > 1024 or (near_edge and lara['pos'][1] < edge['top']-762 and previous['pos'][1] >= edge['top']-762):
                anomalies.append(dict(tick=row['tick'], before=previous['pos'], after=lara['pos'],
                                      anim=lara['anim'], frame=lara['frame'], state=lara['state'], dy=dy,
                                      kind='vertical-step' if abs(dy) > 1024 else 'above-sector-top'))
        previous = lara
        if a != b and first is None:
            diff = first_difference(row, json.loads(b))
            first = dict(tick=count, field=diff[0], expected=diff[1], actual=diff[2])
            # Stop both lanes at the first difference, retaining no trace.
            ref.finish(kill=True)
            native.finish(kill=True)
            return dict(status='different', records=count+1, difference=first, anomalies=anomalies, states=dict(states))
        count += 1
    rc, rlog = ref.finish(kill=bool(b))
    nc, nlog = native.finish(kill=bool(a))
    if count == 0 and rc and nc and 'start' in rlog and 'placement' in nlog:
        return dict(status='invalid', records=0)
    if (not a and not b and count >= 201 and rc == 2 and nc == 1 and
            'cross-level transition is outside this oracle scene' in rlog and
            'tr64 fatal: game tutorial level transition outside M2' in nlog):
        return dict(status='loud', records=count, reason='game tutorial level transition outside M2',
                    anomalies=anomalies, states=dict(states))
    if rc or nc or count != ticks+1:
        return dict(status='error', records=count, oracle_exit=rc, native_exit=nc, oracle_log=rlog[-400:], native_log=nlog[-400:])
    return dict(status='equal', records=count, anomalies=anomalies, states=dict(states))


def failed(report, counts):
    # Sector heights omit item surfaces such as the room-14 bridges. Preserve
    # those height-only flags for inspection, not as claims of a source defect.
    return bool(report['failures'] or not counts['equal'] or
                any(a['dy'] < -1024 or a['dy'] > 1024
                    for case in report['anomalies'] for a in case['anomalies']))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--level', required=True, type=Path)
    p.add_argument('--executable', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m2-start/tr1-oracle'))
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--distances', default='96,256,512')
    p.add_argument('--angles', default='0,-1024,1024')
    p.add_argument('--limit', type=int, default=0)
    p.add_argument('--rooms', help='Comma-separated source room indices')
    p.add_argument('--patterns', help='Comma-separated input schedule names')
    p.add_argument('--representative', action='store_true',
                   help='First edge per facing/height/clearance/slope/water class')
    p.add_argument('--stop-difference', action='store_true')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    level = src_pc.load(args.level)
    situations = edges(level)
    if args.rooms:
        rooms = set(map(int, args.rooms.split(',')))
        situations = [e for e in situations if e['room'] in rooms]
    situations.sort(key=lambda e: ('slope' in e['tags'], e['angle'] != -32768))
    enumerated = len(situations)
    if args.representative:
        unique = {}
        for edge in situations:
            unique.setdefault((edge['angle'], tuple(edge['tags'])), edge)
        situations = list(unique.values())
    if not situations:
        p.error('no matching source ledges')
    report = dict(level=level.name, level_sha256=level.sha256,
                  executable_sha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                  oracle_sha256=hashlib.sha256(args.oracle.read_bytes()).hexdigest(),
                  enumerated_edges=enumerated, edges=len(situations),
                  tags=dict(collections.Counter(t for e in situations for t in e['tags'])),
                  patterns=PATTERNS, water_patterns=WATER_PATTERN,
                  distances=args.distances, angles=args.angles,
                  counts={}, states={}, failures=[], anomalies=[], boundaries=[])
    counts, states = collections.Counter(), collections.Counter()
    n = 0
    try:
        for ei, edge in enumerate(situations):
            for distance in map(int, args.distances.split(',')):
                for angle in map(int, args.angles.split(',')):
                    schedules = WATER_PATTERN if 'water-out' in edge['tags'] else PATTERNS
                    if args.patterns:
                        selected = set(args.patterns.split(','))
                        schedules = {k: v for k, v in schedules.items() if k in selected}
                    for pattern, schedule in schedules.items():
                        x = edge['x']+edge['dx']*(512-distance)
                        z = edge['z']+edge['dz']*(512-distance)
                        s = sector_at(level, edge['room'], x, z)
                        y = edge['y'] if 'water-out' in edge['tags'] else height(level, edge['room'], s, x, z)-(s['room_below'] != 255)
                        yaw = (edge['angle']+angle+32768)%65536-32768
                        text = f"start {edge['room']} {x} {y} {z} {yaw}\n"+''.join(f'{mask}\n'*duration for mask, duration in schedule)
                        path = args.out/'current.masks'
                        path.write_text(text)
                        name = f'edge{ei}-d{distance}-a{angle}-{pattern}'
                        result = run_case(args, path, edge, name, sum(duration for _, duration in schedule))
                        counts[result['status']] += 1
                        states.update(result.get('states', {}))
                        if result['status'] not in ('equal', 'invalid') or result.get('anomalies'):
                            detail = dict(name=name, edge=edge, placement=text.splitlines()[0], **result)
                            if result['status'] == 'loud':
                                report['boundaries'].append(detail)
                            elif result['status'] not in ('equal', 'invalid'):
                                report['failures'].append(detail)
                                (args.out/f'{name}.masks').write_text(text)
                            if result.get('anomalies'):
                                report['anomalies'].append(detail)
                                (args.out/f'{name}.masks').write_text(text)
                            print(json.dumps(detail), flush=True)
                        n += 1
                        if (args.stop_difference and result['status']=='different') or (args.limit and n >= args.limit):
                            return int(failed(report, counts))
            print(f'{level.name} edge={ei+1}/{len(situations)} cases={n} counts={dict(counts)}', flush=True)
    finally:
        report['counts'] = dict(counts)
        report['states'] = dict(states)
        report['cases'] = n
        for binary, key in ((args.executable, 'executable_sha256'), (args.oracle, 'oracle_sha256')):
            if hashlib.sha256(binary.read_bytes()).hexdigest() != report[key]:
                report['failures'].append(dict(error=f'input binary changed: {binary}'))
        (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k not in ('failures', 'anomalies')}), flush=True)
    return int(failed(report, counts))

if __name__ == '__main__':
    raise SystemExit(main())
