#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run with PYTHONPATH=tools/cook; compare clean and rejected runtime builds."""
import argparse
from collections import Counter
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(args, case_pack, keys, trace, mode=None):
    trace.unlink(missing_ok=True)
    p = subprocess.run(['rtk', str(args.host), '--game-tr2-schema21', str(case_pack),
                        str(keys), str(trace)], capture_output=True, text=True, timeout=30,
                       env=os.environ if mode is None else os.environ | {'TR64_COMBAT_LIVE_CONTROL': mode})
    rows = trace.read_bytes().splitlines() if trace.exists() else []
    return p, rows


def read_occupancy(log):
    occupancy = {}
    for line in log.splitlines():
        if line.startswith('EFFECTS '):
            _, tick, *counts = line.split()
            occupancy[int(tick)] = {k: int(v) for k, v in (word.split('=') for word in counts)}
    return occupancy


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('host', 'pack', 'fixtures', 'scratch'):
        ap.add_argument(name, type=Path)
    ap.add_argument('--sparks', action='store_true', help='use the separate live diagnostic link')
    ap.add_argument('--shards', action='store_true', help='use the separate effects diagnostic link')
    ap.add_argument('--crows', action='store_true', help='replay both seeded crow fixtures in the normal host')
    ap.add_argument('--producer', default='tr64-tr2-combat', help='pinned producer used by --crows')
    ap.add_argument('--bites', action='store_true', help='check injury allocations and lifetimes via the diagnostic link')
    ap.add_argument('--crow-unit', action='store_true', help='prove crow-only placement selects the creature paging unit')
    args = ap.parse_args()
    here = Path(__file__).resolve().parent
    c = load(here.parents[1] / 'tools/tr2-compare.py', 'tr2_compare')
    item = load(here / 'tr2-item-actions-test.py', 'item_controls')
    with tempfile.TemporaryDirectory(prefix='tr2-combat-review-', dir=args.scratch) as td:
        scratch = Path(td)
        trace, keys, pack = [scratch / n for n in ('trace.jsonl', 'keys', 'case.pack')]
        if args.bites:
            cases = {'crow-bite': (38, (338, 427, 568)), 'tiger-bite': (39, (123, 159, 195, 231)),
                     'spider-bite': (36, (299, 335, 371, 407, 443, 479))}
            for name, (species, ticks) in cases.items():
                _, masks, place = c._fixture_data(args.fixtures / 'schema21' / name)
                keys.write_text('place ' + ' '.join(map(str, place)) + '\n' + ' '.join(map(str, masks)) + '\n')
                p, rows = run(args, args.pack, keys, trace, 'effects')
                bites = [tuple(map(int, line.split()[1:])) for line in p.stdout.splitlines()
                         if line.startswith('BITE ')]
                if p.returncode or len(rows) != len(masks)+2 or bites != [(t, species, 1) for t in ticks]:
                    raise RuntimeError(f'{name}: injury allocation mismatch: {bites}, exit={p.returncode}')
                occupancy = read_occupancy(p.stdout)
                for t in range(1, len(masks)+1):
                    expected = sum(b <= t < b+23 for b in ticks)
                    if occupancy[t]['blood'] != expected:
                        raise RuntimeError(f"{name}: tick {t}, expected {expected} live blood, got {occupancy[t]}")
                print(f'PASS {name}: {len(ticks)} injury allocations, retirement at bite+23 where sampled')
                if name=='crow-bite':
                    p, rows = run(args, args.pack, keys, trace, 'bite-full')
                    if p.returncode or not rows or 'producer' in json.loads(rows[-1]) or \
                            'PASS full bite pool refuses blood but retains damage and latch; unsealed' not in p.stdout:
                        raise RuntimeError(f'{name}: full injury pool control failed; {p.stderr}')
                    print('PASS full injury pool preserves damage and latch')
            return
        if args.crows:
            for name, count in (('crow-bite', 641), ('crow-shoot', 561)):
                fixture = args.fixtures / 'schema21' / name
                retail, masks, place, manifest = c._fixture_data(fixture, metadata=True)
                if manifest['preLevelSeed'] != 3547461959 or len(masks)+1 != count:
                    raise RuntimeError(f'{name}: wrong published pre-level seed or sample count')
                keys.write_text('place ' + ' '.join(map(str, place)) + '\n' + ' '.join(map(str, masks)) + '\n')
                p, rows = run(args, args.pack, keys, trace)
                if p.returncode:
                    raise RuntimeError(f'{name}: host exited {p.returncode}, {len(rows)} rows\n{p.stderr[-800:]}')
                report = c.compare(fixture, trace.read_bytes(), args.producer)
                for field in ('lara', 'camera', 'active', 'inventory', 'world', 'rng[0]'):
                    family = report['families'][field]
                    if family['knownEqual'] != count or family['first']:
                        raise RuntimeError(f'{name}: {field}: {family}')
                # The placed-start distance remains the interactions slice's gap.
                for expected, line in zip(c.records(retail, masks, 'retail', place, 21), rows):
                    got = json.loads(line)['save']
                    if any(got[k]!=v for k,v in expected['save'].items() if k!='distance'):
                        raise RuntimeError(f"{name}: save counters differ at {expected['tick']}")
                first = json.loads(rows[1])
                crows = [(x['index'], x['ai']['slot'], x['rot'][1]) for x in first['active'] if x['type']==38]
                if crows != [(2, 2, 3230), (12, 1, -4159), (13, 0, 2705)] or first['camera']['pos'] != [48127, 324, 33957]:
                    raise RuntimeError(f'{name}: wrong wake order/heading or first fixed eye')
                print(f'PASS {name}: {count}/{count} Lara, camera, active creatures and gameplay RNG')
            return
        if args.shards:
            _, masks, place = c._fixture_data(args.fixtures / 'schema21/spider-shoot')
            keys.write_text('place ' + ' '.join(map(str, place)) + '\n' + ' '.join(map(str, masks)) + '\n')
            p, _ = run(args, args.pack, keys, trace, 'effects')
            occupancy = read_occupancy(p.stdout)
            got = {t: occupancy[t]['shard'] for t in (300, 317, 350)}
            last = occupancy[350]
            if p.returncode or got != {300: 11, 317: 0, 350: 0} or last != {
                    'shard': 0, 'splash': 0, 'flame': 1, 'blood': 2, 'ricochet': 1}:
                raise RuntimeError(f'fragment retirement differs: exit={p.returncode}, shards={got}, tick350={last}')
            print(f'PASS Q-T2K-11: shards {got}; four effects before second breakup')
            return
        if args.sparks:
            checks = {'pistols': {76: 2, 86: 2},
                      'spider-shoot': {290: 1, 300: 1, 310: 2, 320: 1, 330: 2, 340: 1, 350: 1}}
            for name, want in checks.items():
                fixture = args.fixtures / ('schema21/spider-shoot' if name == 'spider-shoot' else name)
                _, masks, placement = c._fixture_data(fixture)
                keys.write_text(('place ' + ' '.join(map(str, placement)) + '\n' if placement else '') +
                                ' '.join(map(str, masks)) + '\n')
                p, _ = run(args, args.pack, keys, trace)
                sparks = [tuple(map(int, row.split()[1:])) for row in p.stdout.splitlines() if row.startswith('SPARK ')]
                # The wall measurement gives spider counts only through the second breakup.
                measured = [row for row in sparks if name=='pistols' or row[0]<=350]
                if p.returncode or Counter(row[0] for row in measured) != want:
                    raise RuntimeError(f'{name}: {sparks!r}\n{p.stdout[-1600:]}\n{p.stderr}')
                expected_points = { 'pistols': [(76, 35, 65535, 3924, 25224), (76, 35, 65535, 3926, 25171)],
                                    'spider-shoot': [(290, 4, 25600, -646, 35352)] }
                for hit in expected_points[name]:
                    if hit not in sparks:
                        raise RuntimeError(f'{name}: did not find expected linked impact {hit!r}; got {sparks!r}')
                print(f'PASS {name}: exact spark counts, linked impact and presentation-only draw', flush=True)
            return

        base = item.n64_pack.inspect(args.pack.read_bytes())['wall']
        items, rooms, sectors = (item.rows(base, n) for n in ('items', 'rooms', 'sectors'))
        def sector(obj):
            r = rooms[obj['room']]
            return r['sector_start'] + ((obj['x'] - r['x']) >> 10) * r['z_sectors'] + ((obj['z'] - r['z']) >> 10)
        spawn = sector(next(p for p in items if p['type'] == 0))
        tiger, rex, crow = (next(i for i, p in enumerate(items) if p['type'] == t) for t in (39, 214, 38))
        if args.crow_unit:
            tables = base
            for slot, actor in enumerate(items):
                if actor['type'] in (36, 39):
                    tables = item.replace_row(tables, 'items', slot, actor, type=243)
            pack.write_bytes(item.spawn_program(tables, spawn, [0x8004, 0x3e00, 0x8000 | crow]))
            keys.write_text('0 0 0\n')
            p, rows = run(args, pack, keys, trace)
            loads = [line for line in p.stdout.splitlines() if line.startswith('code: load tr2_creatures pages=')]
            if p.returncode or len(loads)!=1 or not rows or 'producer' not in json.loads(rows[-1]):
                raise RuntimeError(f'crow-only paging selection failed: exit={p.returncode}, loads={loads}, {p.stderr}')
            crow_samples = [o for row in rows[1:-1] for o in json.loads(row)['active'] if o['index']==crow]
            if len(crow_samples)!=3 or not all(o.get('ai') for o in crow_samples):
                raise RuntimeError('crow-only paging control did not wake and run a crow')
            print('PASS crow-only placement selects, wakes and ticks through tr2_creatures')
            return
        no_box = sector(items[tiger])
        if sectors[no_box]['box_index'] == 65535:
            raise RuntimeError('expected authored tiger box before removing it')
        nobox = item.replace_row(base, 'sectors', no_box, sectors[no_box], box_index=65535)
        # The damage clip's ordinary hand-free command would run much later;
        # this case requires refusal at load, before tick 0 is written.
        death = 297
        clip = item.rows(base, 'animations')[death]
        bad_cmd = item.replace_commands(base, death, clip, 3)
        # Reconstruct the reviewer's clipped-crow-bank.pack with no stored game data:
        # the foreign bank begins at the hardwired perch; short local links do
        # not leave the crow bank, and the unvalidated perch has a Lara effect.
        models = item.rows(base, 'models')
        animations = item.rows(base, 'animations')
        crow_base = next(m['animation'] for m in models if m['type']==38)
        unused = next(i for i, m in enumerate(models) if m['type']==2 and m['animation']==65535)
        def clipped_crow(offset):
            first_foreign = crow_base + offset
            tables = item.replace_commands(base, first_foreign, animations[first_foreign], 6,
                                           animations[first_foreign]['frame_start'], 0)
            tables = item.replace_row(tables, 'models', unused, models[unused], animation=first_foreign)
            for index in range(crow_base, first_foreign):
                anim = animations[index]
                tables = item.replace_row(tables, 'animations', index, anim, state_count=0,
                                          next_animation=index, next_frame=anim['frame_start'])
            return tables
        giant = item.replace_row(base, 'items', rex, items[rex], type=37)
        cases = [('giant spider wake', rex, giant, 1, 'unsupported giant spider controller'),
                 ('missing navigation box', tiger, nobox, 1, 'no navigation box at wake'),
                 ('Lara command in tiger bank', tiger, bad_cmd, 0, 'creature animation 297 incompatible command 3'),
                 ('clipped crow perch bank', crow, clipped_crow(14), 0, 'crow perch animation outside its body bank'),
                 ('clipped crow fall bank', crow, clipped_crow(1), 0, 'crow fall animation outside its body bank')]
        for actor, species, joint in (('tiger', 39, 26), ('crow', 38, 14), ('small spider', 36, 1)):
            model_id, model = next((i, m) for i, m in enumerate(models) if m['type']==species)
            if model['mesh_count']<=joint:
                raise RuntimeError(f'{actor}: authored positive control lacks joint {joint}')
            tables = item.replace_row(base, 'models', model_id, model, mesh_count=joint)
            slot = next(i for i, p in enumerate(items) if p['type']==species)
            cases.append((f'{actor} missing bite joint', slot, tables, 0,
                          f'{actor} bite joint {joint} outside its model meshes'))
        def old_shotgun_accepted(tables):
            """v2 load check: rate, literal endpoints and in-bank links.
            It does not read clip states or require a state change to the named clip."""
            anims = item.rows(tables, 'animations')
            states = item.rows(tables, 'states')
            ranges = item.rows(tables, 'ranges')
            bank = next(m['animation'] for m in item.rows(tables, 'models') if m['type'] == 3)
            starts = (0, 13, 47, 80, 114, 127)
            end = bank + 5
            if end > len(anims):
                return False
            for clip in range(bank, end):
                anim = anims[clip]
                k = clip - bank
                if (anim['frame_rate'] != 1 or anim['frame_start'] != starts[k] or
                        anim['frame_end'] != starts[k + 1] - 1):
                    return False
                if not bank <= anim['next_animation'] < end:
                    return False
                for change in states[anim['state_offset']:anim['state_offset'] + anim['state_count']]:
                    for window in ranges[change['range_offset']:change['range_offset'] + change['range_count']]:
                        if not bank <= window['next_animation'] < end:
                            return False
            return True

        def literal_walk(tables, clip, frame, goal):
            anims = item.rows(tables, 'animations')
            states = item.rows(tables, 'states')
            ranges = item.rows(tables, 'ranges')
            anim = anims[clip]
            if goal == anim['state'] or not anim['state_count']:
                return clip
            for change in states[anim['state_offset']:anim['state_offset'] + anim['state_count']]:
                if change['state'] != goal:
                    continue
                for window in ranges[change['range_offset']:change['range_offset'] + change['range_count']]:
                    if window['low'] <= frame <= window['high']:
                        return window['next_animation']
            return clip

        aim = next(m['animation'] for m in models if m['type'] == 3)
        aim_anim = animations[aim]
        recoil = aim + 2
        recoil_anim = animations[recoil]
        states = item.rows(base, 'states')
        gap = next(recoil_anim['state_offset'] + i
                   for i in range(recoil_anim['state_count'])
                   if states[recoil_anim['state_offset'] + i]['state'] == aim_anim['state'])
        no_range = item.replace_row(base, 'states', gap, states[gap], state=99)
        if not old_shotgun_accepted(no_range):
            raise RuntimeError('old literal shotgun check rejected a bank that only retargets a range')
        def v3_window(tables, current, target):
            anims = item.rows(tables, 'animations')
            goal = anims[target]['state']
            first, last = anims[current]['frame_start'], anims[current]['frame_end']
            return any(literal_walk(tables, current, frame + 1, goal) == target
                       for frame in range(first, last + 1))
        def v3_chain(tables, current, target):
            anims = item.rows(tables, 'animations')
            goal = anims[target]['state']
            frame = anims[current]['frame_start']
            for _ in range(64):
                landed = literal_walk(tables, current, frame + 1, goal)
                if landed == target:
                    return True
                if landed != current:
                    current, frame = landed, anims[landed]['frame_start']
                    continue
                if frame + 1 > anims[current]['frame_end']:
                    end = anims[current]['next_animation']
                    if end == target:
                        return True
                    current, frame = end, anims[end]['frame_start']
                    continue
                frame += 1
            return False
        if not all((v3_window if w else v3_chain)(no_range, aim + a, aim + b)
                   for a, b, w in ((0, 2, False), (0, 4, True), (2, 4, True), (0, 3, False), (2, 3, False))):
            raise RuntimeError('replaced walk refused the no-range bank; the negative is vacuous')
        def chain_from(tables, current, frame, target):
            anims = item.rows(tables, 'animations')
            goal = anims[target]['state']
            for _ in range(64):
                landed = literal_walk(tables, current, frame + 1, goal)
                if landed == target:
                    return True
                if landed != current:
                    current, frame = landed, anims[landed]['frame_start']
                    continue
                if frame + 1 > anims[current]['frame_end']:
                    end = anims[current]['next_animation']
                    frame = anims[current]['next_frame']
                    if end == target:
                        return True
                    current = end
                    continue
                frame += 1
            return False
        def append_counted(tables, name, fmt, values):
            ident, _ = item.TABLES[name]
            data = bytearray(tables[ident])
            width = struct.calcsize('>' + fmt)
            count = (len(data) - item.HEADER) // width
            data.extend(struct.pack('>' + fmt, *values))
            struct.pack_into('>I', data, 8, count + 1)
            struct.pack_into('>I', data, 24, len(data))
            return tables | {ident: bytes(data)}, count
        # Early off-clip range at the known start, later range landing on
        # recoil. v4 accepts the later range; the known-start walk refuses
        # because the first range that fires leaves the recoil clip.
        draw = aim + 1
        offclip, early_at = append_counted(
            base, 'ranges', '4H',
            (aim_anim['frame_start'], aim_anim['frame_start'] + 1, draw,
             animations[draw]['frame_start']))
        offclip, late_at = append_counted(
            offclip, 'ranges', '4H',
            (aim_anim['frame_start'] + 10, aim_anim['frame_start'] + 11, recoil,
             recoil_anim['frame_start']))
        if late_at != early_at + 1:
            raise RuntimeError('off-clip ranges are not contiguous')
        copied = offclip
        copy_at = None
        for entry in item.rows(base, 'states')[aim_anim['state_offset']:
                                               aim_anim['state_offset'] + aim_anim['state_count']]:
            copied, at = append_counted(
                copied, 'states', '3H',
                (entry['state'], entry['range_count'], entry['range_offset']))
            copy_at = at if copy_at is None else copy_at
        copied, added = append_counted(
            copied, 'states', '3H',
            (recoil_anim['state'], 2, early_at))
        offclip = item.replace_row(
            copied, 'animations', aim, aim_anim,
            state_count=aim_anim['state_count'] + 1, state_offset=copy_at)
        if not chain_from(offclip, aim, aim_anim['frame_start'] + 10, recoil):
            raise RuntimeError('v4 any-frame walk refused the later correct range')
        if chain_from(offclip, aim, aim_anim['frame_start'], recoil):
            raise RuntimeError('early off-clip range did not divert the known start')
        lower = aim + 4
        lower_anim = animations[lower]
        early_only, range_at = append_counted(
            base, 'ranges', '4H',
            (lower_anim['frame_start'], lower_anim['frame_start'] + 1, aim + 3,
             animations[aim + 3]['frame_start']))
        early_only, state_at = append_counted(
            early_only, 'states', '3H', (animations[aim + 3]['state'], 1, range_at))
        early_only = item.replace_row(
            early_only, 'animations', lower, lower_anim, state_count=1, state_offset=state_at,
            next_animation=lower, next_frame=lower_anim['frame_start'] + 2)
        if not chain_from(early_only, lower, lower_anim['frame_start'], aim + 3):
            raise RuntimeError('v4 early-frame stow walk refused the one-frame bank')
        if chain_from(early_only, lower, lower_anim['frame_start'] + 2, aim + 3):
            raise RuntimeError('late lower frame still reached stow; the negative is vacuous')
        broken_rate = item.replace_row(base, 'animations', aim, aim_anim, frame_rate=2)
        if old_shotgun_accepted(broken_rate):
            raise RuntimeError('old literal shotgun check accepted a bad rate')
        ranges = item.rows(base, 'ranges')
        release_at = next(st['range_offset'] + ri
                          for st in states[recoil_anim['state_offset']:
                                           recoil_anim['state_offset'] + recoil_anim['state_count']]
                          for ri in range(st['range_count'])
                          if ranges[st['range_offset'] + ri]['next_animation'] == aim and
                          ranges[st['range_offset'] + ri]['low'] > recoil_anim['frame_start'] + 2)
        diverted = item.replace_row(base, 'ranges', release_at, ranges[release_at],
                                    next_animation=recoil, next_frame=recoil_anim['frame_start'])
        frame = recoil_anim['frame_start'] + 1
        if not any(change['state'] == aim_anim['state'] and
                   window['low'] <= frame <= window['high']
                   for change in states[recoil_anim['state_offset']:
                                        recoil_anim['state_offset'] + recoil_anim['state_count']]
                   for window in ranges[change['range_offset']:
                                        change['range_offset'] + change['range_count']]):
            raise RuntimeError('aim-state range no longer fires; the negative is vacuous')
        cases.append(('shotgun recoil release without a range to aim', tiger, diverted, 0,
                      'shotgun recoil release has no range to aim'))
        cases.append(('shotgun recoil without a range for aim', tiger, no_range, 0,
                      f'shotgun recoil has no range for state {aim_anim["state"]}'))
        cases.append(('shotgun early off-clip recoil range', tiger, offclip, 0,
                      f'shotgun aim has no range for state {animations[recoil]["state"]}'))
        cases.append(('shotgun stow range only on an early frame', tiger, early_only, 0,
                      f'shotgun lower has no range for state {animations[aim + 3]["state"]}'))
        rex_clip = next(m['animation'] for m in models if m['type'] == 214)
        wide = item.replace_row(base, 'animations', rex_clip, animations[rex_clip], state=256)
        cases.append(('Tyrannosaurus state wider than the item', rex, wide, 0,
                      f'animation {rex_clip} state 256 does not fit'))
        failures = []
        keys.write_text('0 0 0\n')
        for name, slot, tables, samples, error in cases:
            pack.write_bytes(item.spawn_program(tables, spawn, [0x8004, 0x3e00, 0x8000 | slot]))
            p, rows = run(args, pack, keys, trace)
            log = p.stdout + p.stderr
            sealed = bool(rows and 'producer' in json.loads(rows[-1]))
            if not p.returncode or sealed or len(rows) != samples or error not in log:
                failures.append(f'{name}: exit={p.returncode} sealed={sealed} rows={len(rows)} {log[-225:]}')
            else:
                print('PASS', name, error, flush=True)
        if failures:
            raise RuntimeError('\n'.join(failures))
        pack.write_bytes(item.spawn_program(base, spawn, [0x8004, 0x3e00, 0x8000 | tiger]))
        p, rows = run(args, pack, keys, trace)
        if p.returncode or not rows:
            raise RuntimeError(f'unmutated pack refused: {(p.stdout + p.stderr)[-225:]}')
        print('PASS unmutated pack loads', flush=True)


if __name__ == '__main__':
    main()
