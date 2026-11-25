#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise pickups, authored lever settle, item actions and door topology on the native host."""
import argparse
import json
import math
import os
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile

import n64_pack

helpers = runpy.run_path(str(Path(__file__).with_name('tr2-item-actions-test.py')))
END, MASK, ONCE = (helpers[name] for name in ('END', 'MASK', 'ONCE'))
rows, replace_row, replace_commands, spawn_program, wall_sections = (helpers[name] for name in (
    'rows', 'replace_row', 'replace_commands', 'spawn_program', 'wall_sections'))
ACTION = 128


def active(row, slot):
    return next((x for x in row['active'] if x['index'] == slot), None)


def check(ok, message):
    if not ok:
        raise AssertionError(message)


def fixtures(host, wall, retail, tmp):
    base = n64_pack.inspect(wall.read_bytes())['wall']
    placed = rows(base, 'items')
    rooms = rows(base, 'rooms')
    lever = next(i for i, p in enumerate(placed) if p['type'] == 104)
    silver = next(i for i, p in enumerate(placed) if p['type'] == 192)
    door = 3  # tr2-great-wall-objects, Authored checkpoints: lever 4 drives door 3.
    reverse = next(i for i, p in enumerate(placed) if p['type'] == 107)
    nav_door = 6  # tr2-oracle-trace, Great Wall startup: only this door blocks box 703.
    in_file, trace, pack = (tmp / file for file in ('input.txt', 'trace.jsonl', 'variant.pack'))

    def location(name):
        line = (retail / name / 'input.txt').read_text().splitlines()[0]
        return list(map(int, line.split()[1:]))

    def sector(pos):
        x, _, z, _, room = pos
        r = rooms[room]
        return (r['sector_start'] + ((x-r['x']) >> 10)*r['z_sectors'] + ((z-r['z']) >> 10))

    def simulate(pos, masks, *, words=None, at_sector=None, tables=base, refusal=None,
                 last=None, confirmation=None, after_place='', ammo_bundle=False):
        if words:
            pack.write_bytes(spawn_program(tables, sector(pos) if at_sector is None else at_sector, words))
        elif tables is not base:
            pack.write_bytes(n64_pack.build({'wall': wall_sections(tables)}, games={'wall': 2}))
        else:
            pack.unlink(missing_ok=True)
        initial = 'place ' + ' '.join(map(str, pos)) + '\n' if pos is not None else ''
        in_file.write_text(initial + after_place + ' '.join(map(str, masks)) + '\n')
        trace.unlink(missing_ok=True)
        command = ['rtk', str(host), '--game-tr2', str(pack if pack.exists() else wall),
                   str(in_file), str(trace)]
        if confirmation:
            events = confirmation if isinstance(confirmation[0], tuple) else [confirmation]
            for event in events:
                command += ['--key-confirmation', *map(str, event)]
        if ammo_bundle:
            command.append('--ammo-bundle-confirm')
        result = subprocess.run(command,capture_output=True,text=True,timeout=15)
        if refusal is not None:
            partial = trace.read_bytes() if trace.exists() else b''
            log = result.stdout + result.stderr
            check(result.returncode != 0 and refusal in log and b'\"recordsHash\"' not in partial,
                  f'expected unsealed refusal {refusal!r}; got rc={result.returncode}: {log.splitlines()[-1]}')
            partial_rows = [json.loads(line) for line in partial.splitlines()]
            check((partial_rows[-1]['tick'] if partial_rows else None) == last,
                  f'{refusal}: refusal at wrong tick')
            return partial_rows, log
        if result.returncode:
            raise AssertionError((result.stdout + result.stderr).splitlines()[-1])
        data = [json.loads(line) for line in trace.read_text().splitlines()]
        check(len(data) == len(masks) + 2 and 'recordsHash' in data[-1], 'trace did not seal')
        return data[:-1], result.stdout + result.stderr

    def published(name, **kwargs):
        inputs = list(map(int,(retail/name/'input.txt').read_text().splitlines()[1:]))
        return simulate(location(name),inputs,**kwargs)

    def run_co_located_pickups():
        got, log = published('shotgun-ammo')
        before, commit = got[42], got[43]
        check(commit['lara']['frame'] == 3443 and not commit['lara']['input'] and
              before['lara']['ammo'][3] == 12 and commit['lara']['ammo'][3] == 24 and
              dict(before['inventory']['items'])[172] == 1 and
              dict(commit['inventory']['items'])[172] == 2 and
              [k for k,v in before['inventory']['items']] == [k for k,v in commit['inventory']['items']] and
              'pickup-commits=2' in log, 'commit frame must visit both items and preserve ring order')
        medikit = placed[28]
        moved = replace_row(base,'items',28,medikit,x=medikit['x']+257)
        one, log = published('shotgun-ammo',tables=moved)
        check(one[43]['lara']['ammo'][3] == 24 and dict(one[43]['inventory']['items'])[172] == 1 and
              'pickup-commits=1' in log, 'each candidate must pass its own reach test')

    def run_pickup():
        start = location('pickup')
        at = placed[silver]
        # In II the far Z edge is +256, not I's +100; hold once only,
        # and try again after the authored hands-free return.
        start[2] = at['z'] + 256
        press = [ACTION] + [0]*109 + [ACTION] + [0]*10
        got, log = simulate(start, press)
        check(got[1]['lara']['anim'] == 135 and got[1]['lara']['frame'] == 3401 and
              got[1]['lara']['weaponState'] == 1 and got[1]['lara']['pos'] ==
              [at['x'], at['y'], at['z']-100], 'dry pickup reach, exact snap or busy entry')
        check(got[42]['save']['secrets'] == 0 and got[43]['save']['secrets'] == 4 and
              got[43]['lara']['frame'] == 3443 and got[67]['lara']['weaponState'] == 0,
              'pickup did not commit at the authored frame without held Action')
        check(got[43]['inventory'] == got[0]['inventory'] and 'pickup-commits=1' in log,
              'single dragon changed stock or was counted twice')
        start[2] += 1
        rejected, _ = simulate(start, [ACTION, 0])
        check(rejected[1]['lara']['state'] == 2, 'Z +257 was accepted')
        start = location('pickup')
        inert, _ = simulate(start, [0]*44)
        check(inert[-1]['save']['secrets'] == 0 and inert[1]['lara']['state'] == 2,
              'ordinary proximity alone collected a dragon')
        hands, _ = simulate(start, [256, ACTION])
        check(hands[1]['lara']['weaponState'] == 2 and hands[2]['lara']['state'] == 2,
              'Action while drawing was accepted as free-handed')
        anim = rows(base, 'animations')[135]
        shifted = replace_row(base, 'animations', 135, anim, speed=20 << 16)
        out, _ = simulate(start, [ACTION]+[0]*42, tables=shifted)
        check(out[1]['lara']['anim'] == 135 and out[43]['lara']['frame'] == 3443 and
              abs(out[43]['lara']['pos'][2]-at['z']) > 256 and out[43]['save']['secrets'] == 0,
              'the acquisition frame must check reach again after authored motion')

    def run_pickup_trigger():
        start = location('pickup')
        words = [END | (4 << 8) | 4, MASK | 1, silver, END | door]
        got, _ = simulate(start, [ACTION] + [0]*70, words=words)
        check(active(got[43], door) is not None, 'the completed pickup did not wake its target')
        check(not active(got[42], door) and active(got[43], door)['timer'] == 1 and
              active(got[44], door)['timer'] == -1 and active(got[46], door)['timer'] == -1,
              'pickup owner must fire once after removal, not while present or on every revisit')
        hidden = replace_row(base, 'items', silver, placed[silver], flags=ONCE)
        unborn, _ = simulate(start, [ACTION]+[0]*70, words=words, tables=hidden)
        check(not active(unborn[-1], door) and unborn[-1]['save']['secrets'] == 0,
              'an authored invisible pickup fired an acquisition action before collection')

    def run_stock():
        start = location('pickup')
        authored = placed[silver]
        # Replace only the world item under the isolated, dry fixture. Neither
        # new placement nor modified floor action is used in retail parity.
        for kind in (143, 149, 150, 151):
            variant = replace_row(base, 'items', silver, authored, type=kind)
            got, _ = simulate(start, [ACTION]+[0]*45, tables=variant)
            rings = dict(got[43]['inventory']['items'])
            before = got[42]
            check(got[43]['save']['secrets'] == 0, f'pickup type {kind} created secret progress')
            if kind == 143:
                check(got[43]['lara']['ammo'][3] == before['lara']['ammo'][3]+12 and
                      got[43]['inventory'] == before['inventory'], 'owned shotgun ammo did not credit 12 pellets')
            else:
                ring = {149: 171, 150: 172, 151: 173}[kind]
                check(rings[ring] == dict(before['inventory']['items'])[ring] + (6 if kind == 151 else 1),
                      f'pickup type {kind} did not credit the ring at commitment')

    def run_ring_order():
        start = location('pickup')
        origin = {k: placed[silver][k] for k in ('room', 'x', 'y', 'z')}
        key = next(i for i, p in enumerate(placed) if p['type'] == 193)
        shot = [ACTION]+[0]*66
        types = lambda row: [t for t, _ in row['inventory']['items']]
        ammo = replace_row(base, 'items', silver, placed[silver], type=144)
        got, _ = simulate(start, shot, tables=ammo)
        check(types(got[43]) == [121,157,158,166,173,172,171] and
              dict(got[43]['inventory']['items'])[166] == 1 and got[43]['lara']['ammo'][1] == 0,
              'unowned automatic ammo must insert one bundle at priority 3, not credit rounds')
        # The room sees the higher slot (Rusty Key) first; order is still
        # by display priority, and the key ring follows the main ring.
        keys = replace_row(base, 'items', silver, placed[silver], type=194)
        keys = replace_row(keys, 'items', key, placed[key], **origin)
        got, _ = simulate(start, shot, tables=keys)
        check(types(got[43]) == [121,157,158,173,172,171,197,198] and
              got[43]['inventory']['mainSelection'] == got[43]['inventory']['keySelection'] == 0,
              'key insertion must preserve both selections and sort ahead of a larger priority')
        dragons = base
        for i, p in enumerate(placed):
            if p['type'] in (190,191):
                dragons = replace_row(dragons, 'items', i, p, **origin)
        got, log = simulate(start, shot+[0]*43+[ACTION], tables=dragons)
        check(got[42]['save']['secrets'] == 0 and got[43]['save']['secrets'] == 7 and
              types(got[43]) == [121,157,158,163,173,172,171] and
              got[43]['lara']['ammo'][5] == 6 and dict(got[43]['inventory']['items'])[171] == 2 and
              got[-1]['inventory'] == got[43]['inventory'] and 'pickup-commits=3' in log,
              'third dragon must credit the ordered reward once on the acquisition tick')

    def run_saved_stats():
        lara = next(p for p in placed if p['type'] == 0)
        spawn = [lara[k] for k in ('x', 'y', 'z')]
        for route in ('pickup', 'switch-door'):
            masks = list(map(int, (retail / route / 'input.txt').read_text().splitlines()[1:]))
            got, _ = simulate(location(route), masks)
            check(got[0]['save']['distance'] == got[0]['save']['time'] == 0,
                  'placed start changed statistics at tick zero')
            previous, total = spawn, 0
            for tick, row in enumerate(got[1:], 1):
                pos = row['lara']['pos']
                total = (total + math.isqrt(sum((x-y)**2 for x,y in zip(pos,previous)) & 0xffffffff)) & 0xffffffff
                check(row['save']['distance'] == total and row['save']['time'] == tick,
                      f'{route} tick {tick} did not credit net displacement from the retained sample')
                previous = pos

    def run_switch():
        start = location('switch-door')
        masks = [ACTION]+[0]*124+[ACTION]+[0]*60
        got, log = simulate(start, masks)
        check(got[1]['lara']['anim'] == 63 and got[1]['lara']['frame'] == 961 and
              got[1]['lara']['pos'] == start[:3] and active(got[1], lever)['flags'] == 0 and
              active(got[2], lever)['flags'] == MASK, 'first lever direction, stationary entry or deferred mask')
        check(not active(got[45], door) and not active(got[46], lever) and
              active(got[46], door)['flags'] == MASK and active(got[46], door)['state'] == 0 and
              got[46]['camera']['lastIndex'] == 0 and got[46]['camera']['timer'] == 59 and
              got[46]['camera']['lastItem'] == 5, 'lever settlement must arm both target and view, in their own phases')
        check(active(got[47], door)['state'] == 1 and got[47]['world'] == got[46]['world'] and
              got[48]['world'] != got[47]['world'] and got[105]['world'] == got[106]['world'],
              'opening changes sectors one door update after entering open; fixed-view expiry is independent')
        check(got[126]['lara']['anim'] == 64 and got[126]['lara']['frame'] == 1006 and
              active(got[126], lever)['flags'] == MASK and active(got[171], door)['flags'] == MASK and
              active(got[172], door)['flags'] == 0 and got[172]['camera']['timer'] == 59 and
              active(got[173], door)['state'] == 0 and got[173]['world'] == got[172]['world'] and
              got[174]['world'] == got[46]['world'] and 'switch-uses=2' in log,
              'second lever direction did not settle, re-arm and close independently')
        start[0] = placed[lever]['x'] + 292
        yes, _ = simulate(start, [ACTION])
        start[0] -= 1
        no, _ = simulate(start, [ACTION])
        check(active(yes[1], lever) and not active(no[1], lever), 'lever Z +292 edge must be inclusive, +291 outside')

    def run_timed():
        start = location('switch-door')
        header = END | (2 << 8) | 4
        # The synthetic stream ends on the camera in both its readers.
        words = [header, MASK | 2, lever, door, (6 << 10) | 5, END | (1 << 10), END | 1]
        got, _ = simulate(start, [ACTION]+[0]*170, words=words)
        check(active(got[46], lever) and active(got[46], door), 'timed lever did not wake its target')
        check(active(got[46], lever)['timer'] == active(got[46], door)['timer'] == 60 and
              got[46]['camera']['timer'] == 29, 'object seconds and one-second camera timer have different scales')
        check(active(got[106], door)['timer'] == -1 and active(got[106], door)['state'] == 1 and
              active(got[107], door)['state'] == 0 and got[107]['world'] == got[106]['world'] and
              got[108]['world'] != got[107]['world'] and
              active(got[107], lever)['state'] == 1, 'last positive timer counts as triggered, then closes and returns')
        check(active(got[152], lever) and not active(got[153], lever) and
              active(got[153], door)['flags'] == 0 and got[153]['camera']['timer'] == -1,
              'automatic return must settle, toggle back and not re-arm the view')
        full, _ = simulate(start, [ACTION]+[0]*170, words=[header, MASK | ONCE | 1, lever, END | reverse])
        check(active(full[46], reverse)['timer'] == 1 and active(full[47], reverse)['timer'] == -1 and
              active(full[-1], reverse)['flags'] == 0x4000 | MASK | ONCE,
              'one-tick timer or one-shot lock on completed switch mask')
        partial, _ = simulate(start, [ACTION]+[0]*47, words=[header, 0x200 | ONCE | 1, lever, END | reverse])
        check(active(partial[46], reverse)['timer'] == active(partial[47], reverse)['timer'] == 1 and
              active(partial[46], reverse)['flags'] == 0x4000 | 0x200,
              'partial switch mask must refresh timer, but cannot one-shot lock or count down')

    def run_box():
        start = location('switch-door')
        words = [END | (2 << 8) | 4, MASK, lever, door, END | nav_door]
        got, _ = simulate(start, [ACTION]+[0]*124+[ACTION]+[0]*52, words=words)
        check([i['index'] for i in got[46]['active'][:2]] == [nav_door, door],
              'one lever list must wake both doors in authored action order')
        blocked = got[0]['world'][1]
        check(got[47]['world'][1] == blocked and got[48]['world'][1] != blocked and
              got[173]['world'][1] != blocked and got[174]['world'][1] == blocked,
              'live navigation box must unblock and re-block with its owning door')

    def run_submerged_pickup():
        # Use the real swim inputs to reach submerged tread, then append
        # a stationary approach to an isolated small medikit in room 37.
        inp = list(map(int, (retail / 'pool-swim' / 'input.txt').read_text().split()))
        variant = replace_row(base, 'items', silver, placed[silver],
                              type=149, room=37, x=69450, y=6420, z=36662)
        reach = len(inp) + 10
        got, _ = simulate(None, inp+[0]*10+[ACTION]*26+[0]*56, tables=variant)
        check(got[reach]['lara']['state'] == 13 and got[reach]['lara']['water'] == 3,
              'submerged control did not start in tread')
        check(got[reach+1]['lara']['state'] == 13 and
              got[reach+1]['lara']['pos'] != got[reach]['lara']['pos'],
              'submerged pickup must approach before its animation starts')
        first = next((i for i in range(reach+2, reach+27) if got[i]['lara']['anim'] == 130), None)
        check(first is not None, 'submerged approach never arrived at the authored pickup animation')
        check(got[first]['lara']['frame'] == 2952 and got[first]['lara']['weaponState'] == 1 and
              got[first+17]['inventory'] == got[first]['inventory'] and
              dict(got[first+18]['inventory']['items'])[171] == 2 and
              got[first+33]['lara']['state'] == 13,
              'submerged pickup did not commit at +18 and then return to tread')
        cancel, _ = simulate(None, inp+[0]*10+[ACTION]+[0]*44, tables=variant)
        check(all(row['lara']['anim'] != 130 for row in cancel[reach+1:]),
              'releasing Action did not cancel incomplete alignment')

    def run_block():
        cycle, _ = published('block-cycle')
        block = next(i for i,p in enumerate(placed) if p['type'] == 67)
        check(cycle[1]['lara']['pos'][2] == 35740 and
              cycle[20]['lara']['frame'] == cycle[22]['lara']['frame'] == 2091 and
              active(cycle[23],block)['pos'][2] == placed[block]['z'] and
              cycle[23]['world'] != cycle[22]['world'], 'centred grasp and vacancy precede motion')
        check(not active(cycle[178],block) and cycle[178]['world'] != cycle[177]['world'] and
              cycle[203]['lara']['pos'][2] == 36763 and not cycle[203]['lara']['weaponState'] and
              active(cycle[286],block)['state'] == 3 and not active(cycle[458],block) and
              cycle[458]['world'] == cycle[22]['world'] and cycle[495]['lara']['pos'][2] == 35739,
              'both end strides must settle topology before Lara independently follows')
        refused, _ = published('block-refused')
        check(all(not active(row,block) for row in refused) and
              all(row['world'] == cycle[22]['world'] for row in refused[2:]) and
              refused[46]['lara']['frame'] == 2091 and
              refused[47]['lara']['weaponState'] == 1 and not refused[48]['lara']['weaponState'],
              'no retreat floor must not vacate the block or skip the hands-free cue')

    def run_block_destination():
        block = next(p for p in placed if p['type'] == 67)
        target = sector([block['x'],block['y'],block['z']+1024,0,block['room']])
        tiger = 18
        moved, _ = published('block-cycle',at_sector=target,
                             words=[END | (5 << 8) | 4,MASK,END | tiger])
        check(not active(moved[177],tiger) and active(moved[178],tiger) and
              not active(moved[178],30) and moved[178]['world'] != moved[177]['world'],
              'block must vacate the unit and settle before a heavy-trigger creature wake')
        start = location('block-cycle')
        blocked, _ = simulate(start,[ACTION]+[ACTION]*21+[ACTION | 8 | 16],
                              at_sector=target,words=[END | 2,1])
        check(blocked[23]['lara']['frame'] == 2091 and not active(blocked[23],30) and
              blocked[22]['world'] == blocked[23]['world'],
              'a sloped push with both directions held must not fall through to pull')

    def run_key_refusals():
        hole = next(i for i,p in enumerate(placed) if p['type'] == 201)
        p = placed[hole]
        at_key = [p['x']-362, p['y'], p['z'], -16384, p['room']]
        keys = replace_row(base,'items',silver,placed[silver],
                           type=193,x=at_key[0]-100,y=p['y'],z=p['z'],room=p['room'])
        words = [END | (3 << 8) | 4, MASK, hole, END]
        got, _ = simulate(at_key,[ACTION]+[0]*100,tables=keys,words=words)
        check(got[101]['inventory']['items'][-1] == [197,1] and
              got[101]['lara']['weaponState'] == 0, 'control must carry the matching key with hands free')
        simulate(at_key,[ACTION]+[0]*100+[ACTION],tables=keys,words=words,
                 refusal='key-ring confirmation required',last=101)
        masks=[ACTION]+[0]*100+[ACTION]
        other = next(i for i,p in enumerate(placed) if p['type'] == 194)
        multiple = replace_row(keys,'items',other,placed[other],x=at_key[0]-100,
                               y=p['y'],z=p['z'],room=p['room'])
        wrong,_=simulate(at_key,masks,tables=multiple,words=words,confirmation=(102,198,4))
        check(wrong[-1]['inventory']['items']==wrong[-2]['inventory']['items'] and
              wrong[-1]['inventory']['keySelection']==1 and wrong[-1]['lara']['anim']!=131 and
              wrong[-1]['save']['time']==106,'wrong key consumed stock, opened lock or lost pause time')
        used,_=simulate(at_key,masks+[0]*140+[ACTION],tables=multiple,words=words,
                        confirmation=((102,197,0),(243,197,0)))
        check(used[102]['lara']['anim']==131 and used[102]['inventory']['items'][-1]==[198,1] and
              used[-1]['inventory']['items']==used[102]['inventory']['items'] and
              used[-1]['lara']['anim']!=131,
              'correct selection must use one key; a later route choice on the spent lock must not reopen or debit')
        simulate(at_key,[0]*101+[ACTION],words=words,
                 confirmation=(102,197,0),
                 refusal='selected ring item 197 is not carried',last=101)

    def run_rusty_key():
        name = 'rusty-key'
        manifest = json.loads((retail/name/'manifest.json').read_text())
        inputs = list(map(int,(retail/name/'input.txt').read_text().splitlines()[1:]))
        event = manifest['keyConfirmation']
        tick = event['tick']
        got, _ = simulate(location(name),inputs,
                          confirmation=(tick,event['type'],event['pausedTicks']))
        check(got[43]['inventory']['items'][-1] == [198,1] and
              got[76]['inventory'] == got[609]['inventory'] and
              got[tick]['inventory']['items'][-1][0] == 171 and
              got[tick]['lara']['anim'] == 131 and got[tick]['lara']['frame'] == 2985 and
              got[tick]['save']['time'] == 660 and got[tick]['rng'][0] == got[tick-1]['rng'][0],
              'the sole carried Rusty Key must be confirmed on the same gameplay tick')
        check(got[744]['lara']['frame'] == 3119 and got[744]['lara']['weaponState'] == 1 and
              not active(got[744],nav_door) and active(got[745],nav_door)['state'] == 0 and
              active(got[746],nav_door)['state'] == 1 and got[746]['world'] == got[745]['world'] and
              got[747]['world'] != got[746]['world'],
              'key consumption, hands-free wake and passable sectors are separate events')

    def run_key_manifest():
        source = retail/'rusty-key'
        scratch = tmp/'key-manifest'
        scratch.mkdir()
        for name in ('input.txt','trace.jsonl'):
            (scratch/name).symlink_to((source/name).resolve())
        manifest = json.loads((source/'manifest.json').read_text())
        manifest_path = scratch/'manifest.json'
        comparator = runpy.run_path(str(Path(__file__).resolve().parents[2]/'tools/tr2-compare.py'))
        manifest_path.write_text(json.dumps(manifest))
        comparator['fixture_data'](scratch)
        for field, value in (('tick', 609), ('type', 197), ('pausedTicks', 49)):
            changed = manifest | {'keyConfirmation': manifest['keyConfirmation'] | {field:value}}
            manifest_path.write_text(json.dumps(changed))
            try:
                comparator['fixture_data'](scratch)
            except ValueError as ex:
                check('keyConfirmation' in str(ex), f'malformed {field} rejected for wrong reason: {ex}')
            else:
                raise AssertionError(f'accepted a key confirmation with invalid {field}')

    def run_zip_mount():
        handle = next(p for p in placed if p['type'] == 102)
        # Move only a zip handle's placement into a known safe test room.
        start = location('pickup')
        zip_slot = placed.index(handle)
        inactive = replace_row(base,'items',zip_slot,handle,room=start[4],x=start[0],
                               y=start[1],z=start[2]-371,angle=0)
        got, _ = simulate(start,[ACTION],tables=inactive)
        grip = active(got[1],zip_slot)
        check(got[1]['lara']['anim'] == 215 and got[1]['lara']['goal'] == 70 and
              got[1]['lara']['weaponState'] == 1 and grip and grip['flags'] & 256,
              'mount must enter the authored pose, occupy hands and mark the handle')
        outside = start.copy()
        outside[2] += 512-371+1
        missed, _ = simulate(outside,[ACTION],tables=inactive)
        check(not active(missed[1],zip_slot) and missed[1]['lara']['anim'] != 215,
              'a handle more than 512 forward in its local reach is not mountable')

    def run_slabs():
        hinged, _ = published('trapdoor-fall')
        check(active(hinged[1],14)['state'] == 0 and hinged[1]['lara']['roomFloor'] == -3072 and
              active(hinged[2],14)['state'] == 1 and hinged[2]['lara']['roomFloor'] == 1536 and
              hinged[39]['lara']['state'] == 35 and hinged[39]['lara']['goal'] == 17,
              'hinge must lose support on opening, before the deep-water dive')
        cross, _ = published('collapse-cross')
        check(active(cross[14],84)['state'] == 0 and active(cross[15],84)['state'] == 1 and
              active(cross[16],84)['goal'] == 2 and active(cross[51],84)['vSpeed'] == 0 and
              active(cross[52],84)['vSpeed'] == 6 and active(cross[74],84)['goal'] == 3 and
              active(cross[102],84)['frame'] == 127 and not active(cross[103],84),
              'wake, shake, loss of support, motion and deactivation are separate phases')
        air, _ = published('collapse-air')
        check(active(air[1],51)['state'] == 0 and active(air[14],51)['state'] == 0 and
              active(air[15],51)['state'] == 1, 'the slab must not shake before the exact landing height')
        published('collapse-stand')

        # Type 57 is not placed in WALL. Bind the same authored animation
        # bank in a directed pack to isolate the different slab height.
        models = rows(base,'models')
        normal = next(model for model in models if model['type'] == 55)
        spare = next(i for i,model in enumerate(models)
                     if model['type'] == 2 and model['animation'] == 65535)
        high = replace_row(base,'models',spare,models[spare],**(normal | {'type':57}))
        high = replace_row(high,'items',46,placed[46],type=57)
        top = placed[46]['y']-1024
        start = [placed[46]['x'],top,placed[46]['z'],0,placed[46]['room']]
        tall, _ = simulate(start,[0]*2,tables=high)
        check(tall[1]['lara']['roomFloor'] == top and active(tall[1],46)['state'] == 0 and
              active(tall[2],46)['state'] == 1, 'type 57 must supply and test the same higher slab')

    def run_hinge_closes():
        # Trigger the authored hinge from a separate dry position so a
        # one-shot one-update timer can expire without another request.
        start = location('pickup')
        got, _ = simulate(start,[0]*85,words=[END | 4, MASK | ONCE | 1, END | 14])
        check(active(got[1],14)['state'] == 0 and active(got[2],14)['state'] == 1 and
              active(got[28],14)['state'] == 1 and active(got[32],14)['state'] == 1 and
              active(got[-1],14)['state'] == 0, 'closing pose must stay open until it settles')

    def run_deferred_surface():
        # These three no longer refuse on activation. A sealed run with the
        # item active is the replacement for the old named stop.
        start = location('pickup')
        for kind, name in ((62, 'flying disc/launcher'), (80, 'rolling blade'),
                           (85, 'spiked wall')):
            slot = next(i for i, p in enumerate(placed) if p['type'] == kind)
            got, _ = simulate(start, [0] * 3, words=[END | 4, MASK, END | slot])
            item = active(got[2], slot)
            check(item is not None and item['status'] == 1,
                  f'{name} must activate instead of the named stop')

    def run_invalid_owner():
        start = location('switch-door')
        simulate(start,[ACTION]+[0]*124+[ACTION]+[0]*60,words=[END],
                 refusal='wall switch has no controller trigger')

    def run_unsupported_pickup_owner():
        start = location('pickup')
        invalid = replace_row(base,'items',silver,placed[silver],type=145)
        simulate(start,[ACTION]+[0]*43,tables=invalid,
                 words=[END | (4 << 8) | 4,MASK,silver,END | door],
                 refusal='unsupported pickup trigger owner')

    def run_door_contact():
        def door_trace(name):
            inputs = list(map(int,(retail / name / 'input.txt').read_text().splitlines()[1:]))
            return simulate(location(name),inputs)[0]
        closed = door_trace('door-closed')
        check(closed[13]['lara']['pos'][0] == 49318 and closed[14]['lara']['pos'][0] == 49310 and
              closed[-1]['lara']['pos'][0] == 49310 and not closed[14]['lara']['hitFrame'],
              'the closed leaf must supply the one-unit active-body correction, without a bump')
        model = next(m for m in rows(base,'models') if m['type'] == placed[door]['type'])
        mesh = model['mesh_start']
        disabled = replace_row(base,'mesh_spheres',mesh,rows(base,'mesh_spheres')[mesh],radius=0)
        no_sphere, _ = simulate(location('door-closed'),[72]*14,tables=disabled)
        check(no_sphere[13]['lara']['pos'] == closed[13]['lara']['pos'] and
              no_sphere[14]['lara']['pos'][0] == 49309,
              'overlapping widened bounds alone must not push Lara')
        opening = door_trace('door-opening')
        check(all(active(opening[i],reverse)['hitMask'] == 1 for i in range(1,25)) and
              not active(opening[25],reverse)['hitMask'] and not opening[24]['lara']['hitFrame'] and
              opening[27]['lara']['pos'][2] == 56421 and opening[27]['lara']['anim'] == 11,
              'opening leaf contact must clear before the Walk stop beside the intact slab')
        name = 'fixed-camera-repeat'
        fixed = door_trace(name)
        check(fixed[70]['camera']['pos'] ==
              json.loads((retail/name/'trace.jsonl').read_bytes().splitlines()[70])['camera']['pos'],
              'a linked intact tile must not refuse the camera sight probe')

    def run_intact_tile():
        slot = next(i for i,p in enumerate(placed) if p['type'] == 55)
        p = placed[slot]
        y = p['y']-512
        start = [p['x'], y, p['z'], 0, p['room']]
        # A dummy trigger refers to the slab as a surface without
        # activating the unsupported shake/collapse.
        words = [END | (8 << 8) | 4,MASK,END | slot]
        got, _ = simulate(start,[0,0],words=words)
        check(got[0]['lara']['roomFloor'] != y and
              got[1]['lara']['roomFloor'] == y and got[1]['lara']['pos'][1] == y and
              not got[1]['lara']['gravity'] and not active(got[1],slot),
              'intact referenced tile must supply live support without firing its controller')

    def run_surface_action_grammar():
        slot = next(i for i,p in enumerate(placed) if p['type'] == 55)
        p = placed[slot]
        y = p['y']-512
        start = [p['x'],y,p['z'],0,p['room']]
        key = next(i for i,p in enumerate(placed) if p['type'] == 201)
        owner = END | (3 << 8) | 4
        # Same slot, same mask: a terminal controller is not an action.
        owner_only, _ = simulate(start,[0],words=[owner,MASK,END | slot])
        # A real tile action after a camera and its extension supplies
        # support. The owner is a keyhole Lara has not operated.
        camera = 1 << 10
        object_actions = [owner,MASK,key,camera,0,END | slot]
        after_camera, _ = simulate(start,[0],words=object_actions)
        different, _ = simulate(start,[0],words=object_actions[:-1]+[END | door])
        # Put the same low bits in a camera parameter. Its actual action
        # list ends at that parameter and does not name the tile.
        extension, _ = simulate(start,[0],words=[owner,MASK,key,camera,END | slot,END | door])
        check(owner_only[1]['lara']['roomFloor'] == different[1]['lara']['roomFloor'] ==
              extension[1]['lara']['roomFloor'] == owner_only[0]['lara']['roomFloor'] != y,
              'controller, camera parameter or action on another object supplied slab support')
        check(after_camera[1]['lara']['roomFloor'] == y and not active(after_camera[1],slot),
              'the object action after a camera extension must supply intact support')

    def run_contact_pose_admission():
        anims = rows(base,'animations')
        models = rows(base,'models')
        model_slot = next(i for i,m in enumerate(models) if m['type'] == placed[door]['type'])
        model = models[model_slot]
        clip = model['animation']
        variant = replace_row(base,'animations',clip,anims[clip],frame_size=9)
        simulate(None,[0],tables=variant,refusal=f'door packed rotations truncated: type=106 animation={clip}')
        simulate(location('door-closed'),[72]*14,tables=variant,
                 refusal=f'door packed rotations truncated: type=106 animation={clip}')
        # Reverse door 72 reaches clip 334 from its model's root; damage
        # only the last packed key, not its first or the root.
        moving = 334
        a = anims[moving]
        last = (a['frame_end']-a['frame_start'])//a['frame_rate']
        ident = helpers['TABLES']['frames'][0]
        frame_data = bytearray(base[ident])
        at = helpers['HEADER'] + a['frame_offset'] + 2*(last*a['frame_size']+9)
        # A multi-axis word needs a second word, but this stride has
        # room for exactly the one-axis key's single rotation word.
        check(a['frame_size'] == 10, 'expected one-word reverse-leaf key')
        struct.pack_into('>H',frame_data,at,0)
        simulate(None,[0],tables=base | {ident:bytes(frame_data)},
                 refusal=f'door packed rotations truncated: type=107 animation={moving}')
        # Admission binds the decoded joint count to this model.
        extra_mesh = replace_row(base,'models',model_slot,model,mesh_count=model['mesh_count']+1)
        simulate(None,[0],tables=extra_mesh,refusal=f'door packed rotations truncated: type=106 animation={clip}')

    def sphere_tables(flags):
        """Give the door valid rotations for a synthetic skeleton."""
        anims = rows(base,'animations')
        states = rows(base,'states')
        ranges = rows(base,'ranges')
        models = rows(base,'models')
        idx = next(i for i,m in enumerate(models) if m['type'] == 106)
        model = models[idx]
        pending = [model['animation']]
        seen = set()
        while pending:
            i = pending.pop()
            if i in seen:
                continue
            seen.add(i)
            a = anims[i]
            pending.append(a['next_animation'])
            for s in states[a['state_offset']:a['state_offset']+a['state_count']]:
                pending += [r['next_animation'] for r in
                            ranges[s['range_offset']:s['range_offset']+s['range_count']]]
        header = helpers['HEADER']
        bones, nodes, frames = (helpers['TABLES'][k][0] for k in ('bones','node_flags','frames'))

        def append(data, suffix, words):
            out = bytearray(data) + suffix
            struct.pack_into('>I',out,8,struct.unpack_from('>I',out,8)[0]+words)
            struct.pack_into('>I',out,24,len(out))
            return bytes(out)

        bone_at = (len(base[bones])-header)//4  # offset in 32-bit bone words
        changed = replace_row(base,'models',idx,model,mesh_count=len(flags)+1,bone_offset=bone_at)
        new_frames = bytearray()
        added = 0
        for i in sorted(seen):
            a = anims[i]
            n = 1 + (a['frame_end']-a['frame_start'])//a['frame_rate']
            stride = a['frame_size']
            check(stride == 10 and a['frame_rate'] == 1,'door skeleton control needs one rotation per key')
            changed = replace_row(changed,'animations',i,a,frame_size=stride+len(flags),
                                  frame_offset=len(base[frames])-header+len(new_frames))
            for j in range(n):
                start = header+a['frame_offset']+j*stride*2
                new_frames.extend(base[frames][start:start+stride*2])
                new_frames.extend(b'\x40\x00'*len(flags))
            added += n*(stride+len(flags))
        return changed | {
            frames: append(base[frames],new_frames,added),
            bones: append(base[bones],b''.join(struct.pack('>4I',f,0,0,0) for f in flags),4*len(flags)),
            nodes: append(base[nodes],b''.join(struct.pack('>I',f) for f in flags),len(flags)),
        }

    def run_sphere_stack_underflow():
        start = location('door-closed')
        # Five nested pushes fill every slot except the sphere temporary.
        # Pop-then-push on the next bone uses the slot again, not another.
        positive = sphere_tables([2]*5+[3,1])
        got, _ = simulate(start,[72]*14,tables=positive)
        check(got[14]['lara']['pos'][0] == 49310,'valid packed skeleton did not reach real door contact')
        for pos in (None,start):
            simulate(pos,[72]*14,tables=sphere_tables([1]),
                     refusal='door sphere bone stack underflow: type=106 animation=327')

    def run_sphere_stack_overflow():
        over = sphere_tables([2]*6)
        for pos in (None,location('door-closed')):
            simulate(pos,[72]*14,tables=over,
                     refusal='door sphere bone stack overflow: type=106 animation=327')

    def run_sphere_model_domain():
        models = rows(base,'models')
        slot = next(i for i,m in enumerate(models) if m['type'] == 106)
        model = models[slot]
        for changes, why in (
            ({'mesh_count':33},'door sphere mesh count outside 1..32'),
            ({'mesh_start':65535},'core mesh_pointers: referenced offset/count out of range'),
            ({'bone_offset':0xfffffffc},'core node_flags: referenced offset/count out of range'),
        ):
            simulate(None,[0],tables=replace_row(base,'models',slot,model,**changes),refusal=why)
        ident = helpers['TABLES']['mesh_spheres'][0]
        data = bytearray(base[ident][:-8])
        struct.pack_into('>I',data,8,struct.unpack_from('>I',data,8)[0]-1)
        struct.pack_into('>I',data,24,len(data))
        simulate(None,[0],tables=base | {ident:bytes(data)},
                 refusal='core mesh_spheres: count differs from mesh_pointers')
        clip = model['animation']
        anim = rows(base,'animations')[clip]
        bad = replace_row(base,'animations',clip,anim,next_frame=65535)
        simulate(None,[0],tables=bad,refusal=f'door contact pose link outside clip: type=106 animation={clip}')
        # Header plus the near key exactly fits the global frame storage;
        # the reachable fractional interval requires another physical key.
        ident = helpers['TABLES']['frames'][0]
        offset = len(base[ident])-helpers['HEADER']-2*anim['frame_size']
        bad = replace_row(base,'animations',clip,anim,frame_offset=offset,frame_end=1,frame_rate=2)
        simulate(None,[0],tables=bad,refusal=f'door packed rotations truncated: type=106 animation={clip} key storage')

    def run_reversing_leaf_and_boulders():
        name = 'door-reverse-contact'
        inputs = list(map(int,(retail/name/'input.txt').read_text().splitlines()[1:]))
        got, _ = simulate(location(name),inputs)
        ids = [i for i,p in enumerate(placed) if p['type'] == 60]
        check([i['index'] for i in got[1]['active'][:2]] == ids[::-1] and
              all(active(got[2],i)['hSpeed'] == 60 and active(got[2],i)['state'] == 1 for i in ids) and
              all(active(got[80],i)['pos'][0] - placed[i]['x'] == 79*60 for i in ids),
              'both boulders must enter in action order and follow the authored motion')
        check(got[2]['camera']['pos'][1] == 1190 and got[2]['camera']['bounce'] == 1 and
              got[2]['rng'][0] == 290753298,
              'shake needs the previous sound listener and the last settling boulder')
        check(got[4]['lara']['hitFrame'] == 0 and
              all(active(got[i],reverse)['hitMask'] == 1 for i in range(5,41)) and
              got[5]['lara']['hitQuadrant'] == 0 and got[5]['lara']['hitTimer'] == 0 and
              got[5]['lara']['hitFrame'] == 1 and got[40]['lara']['hitFrame'] == 34 and
              not active(got[41],reverse)['hitMask'] and got[41]['lara']['hitFrame'] == 0 and
              got[41]['lara']['hitQuadrant'] == -1 and got[42]['lara']['anim'] == 2 and
              got[42]['lara']['state'] == 2 and got[43]['lara']['goal'] == 0 and
              active(got[62],reverse)['goal'] == 0 and got[-1]['world'] == got[0]['world'],
              'opposed opening bumps without a countdown, then Walk stops when the leaf clears')

    def run_boulder_heavy_wake():
        # The reviewer found a nested unit call here. Leave Lara's wake
        # program intact and replace only the boulder's linked FloorData.
        name = 'door-reverse-contact'
        inputs = list(map(int, (retail/name/'input.txt').read_text().splitlines()[1:]))
        tiger = 18
        boulder_floor = 4349
        spawn = placed[tiger]
        check(spawn['type'] == 39, 'heavy control requires the placed tiger')
        got, _ = simulate(location(name), inputs[:3], at_sector=boulder_floor,
                          words=[END | (5 << 8) | 4, MASK, END | tiger])
        woken = active(got[2], tiger)
        check(active(got[1], tiger) is None and woken and woken['status'] == 1 and
              got[2]['active'][0] == woken and
              woken['pos'] == [spawn['x'], spawn['y'], spawn['z']] and active(got[3], tiger),
              'heavy floor action must wake and prepend the tiger at tick 2')

    def run_slab_admission():
        anims = rows(base, 'animations')
        # A resting clip must not carry the moving clip's end offset.
        moved = replace_row(base, 'animations', 310, anims[310], state=1)
        simulate(None, [0], tables=moved,
                 refusal='push block animation 310 end move is not one tile')
        short = replace_commands(base, 310, anims[310], 1, 512, 0, 0)
        simulate(None, [0], tables=short,
                 refusal='push block animation 310 end move is not one tile')
        opened = replace_row(base, 'animations', 335, anims[335], state=2)
        simulate(None, [0], tables=opened,
                 refusal='item type 114 animation 335 has unsupported state')
        high = replace_row(base, 'animations', 299, anims[299], state=4)
        simulate(None, [0], tables=high,
                 refusal='item type 55 animation 299 has unsupported state')
        early = replace_row(base, 'animations', 338, anims[338], next_frame=1)
        simulate(None, [0], tables=early,
                 refusal='hinged trapdoor animation 338 frame link outside clip')
    def run_block_floor_opened():
        start = location('block-cycle')
        inputs = list(map(int, (retail / 'block-cycle' / 'input.txt').read_text().splitlines()[1:]))
        block = next(p for p in placed if p['type'] == 67)
        # Push commits at tick 23 and the end command is tick 178. Drop the
        # vacated cell on tick 30, while the block is still active.
        simulate(start, inputs[:40],
                 after_place=f'pit 30 {block["room"]} {block["x"]} {block["z"]} 1\n',
                 refusal='moving block left its supporting floor', last=29)
    def run_block_creature():
        block = next(p for p in placed if p['type'] == 67)
        tiger = 18
        # Full trigger mask activates the tiger at placement. It must still
        # occupy the push destination when the grasp commits.
        armed = replace_row(base, 'items', tiger, placed[tiger],
                            x=block['x'], y=block['y'], z=block['z'] + 1024,
                            room=block['room'], flags=31 << 9)
        inputs = list(map(int, (retail / 'block-cycle' / 'input.txt').read_text().splitlines()[1:]))
        simulate(location('block-cycle'), inputs[:24], tables=armed,
                 refusal='creature in destination', last=22)

    def run_ammo_bundle():
        simulate(location('pickup'), [0, 0], ammo_bundle=True,
                 refusal='ammunition bundle has no admitted use', last=0)


    return (run_pickup, run_co_located_pickups, run_pickup_trigger, run_stock, run_ring_order, run_saved_stats,
            run_switch, run_timed, run_box, run_submerged_pickup, run_door_contact,
            run_reversing_leaf_and_boulders, run_boulder_heavy_wake, run_block, run_block_destination,
            run_block_creature, run_block_floor_opened,
            run_slabs, run_hinge_closes, run_key_refusals, run_rusty_key,
            run_key_manifest, run_ammo_bundle, run_intact_tile, run_surface_action_grammar,
            run_contact_pose_admission,
            run_sphere_stack_underflow, run_sphere_stack_overflow, run_sphere_model_domain,
            run_zip_mount, run_deferred_surface, run_invalid_owner, run_unsupported_pickup_owner,
            run_slab_admission)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('wall', type=Path)
    parser.add_argument('retail', type=Path)
    args = parser.parse_args()
    failures = 0
    with tempfile.TemporaryDirectory(prefix='tr2-interact-', dir=os.environ['TMPDIR']) as directory:
        for run in fixtures(args.host, args.wall, args.retail, Path(directory)):
            try:
                run()
            except AssertionError as error:
                failures += 1
                print(f'FAIL {run.__name__}: {error}', flush=True)
            else:
                print(f'PASS {run.__name__}', flush=True)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
