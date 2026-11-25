#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the port to a published PC TR2 schema-20, schema-21, or schema-22 fixture."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import sys

FAMILIES = ('lara', 'camera', 'active', 'save', 'inventory', 'world', 'rng')
FAMILIES22 = (*FAMILIES, 'boats', 'race', 'mines', 'effectsUsed')
REQUIRED = (*FAMILIES, 'rng[0]')
REQUIRED22 = (*FAMILIES22, 'rng[0]')
EXE = 'fd9c91d14d54540b3e00c9bedafeef9e4ddb1e9542adc9f29170c7f19b1a657b'
LEVEL = 'a6e0acef3c28a0ca37b1262fb1040279ba4ef8f0efc95731065f181ebc3d1d3b'
VENICE_LEVEL = 'a061b05a6dcf584a471da375bb3ce11fc819d520cb7c9e943aa461f0f12602db'
VENICE_FINISH_INPUT = '8fbf23152e625e3b126296684304646d80219604af91949e5f42b0a7d6d547a5'
VENICE_ENTRY = {
    'finishInputSHA256': VENICE_FINISH_INPUT,
    'cutActionPoll': 3,
    'statisticsConfirmPoll': 3,
    'weapon': 1,
    'shotgunPellets': 12,
    'spareFlares': 1,
    'smallMedikits': 1,
    'largeMedikits': 1,
}
MASK = 123902
PROFILES = Path(__file__).with_name('tr2-producer-profiles.json')
ACTIVE_SELECTOR = re.compile(r'active\[type=(0|[1-9][0-9]{0,2})(,afterWake)?\]\.([A-Za-z]+)')


def layout(names, default=int, **nested):
    return {name: nested.get(name, default) for name in names.split()}


I16 = range(-32768, 32768)
I32 = range(-2**31, 2**31)
U8 = range(256)
U16 = range(65536)
U32 = range(2**32)
BIT = range(2)
SLOT = range(-1, 256)
ROOM = range(255)
ANGLE = (I16,) * 3
POSITION = (I32,) * 3
ARM = layout('angle anim frame aim flash', I16, angle=ANGLE, aim=BIT)
ITEM = layout('index type pos rot room anim frame state goal next flags health hSpeed vSpeed '
              'timer roomFloor hitMask visibleMask status gravity wasHit collidable', I16,
              index=U8, type=range(265), pos=POSITION, rot=ANGLE, room=ROOM, flags=U16,
              roomFloor=I32, hitMask=U32, visibleMask=U32, status=range(4),
              gravity=BIT, wasHit=BIT, collidable=BIT)
BOX = range(-1, 1078)
BOX22 = range(-1, 625)
BRAIN = layout('slot enemy mood jointYaw maxTurn flags box targetBox requiredBox nextBox '
               'headBox tailBox search boxSearch step drop fly blockMask zoneCount destination waypoint',
               BOX, slot=range(5), enemy=SLOT, mood=range(4), jointYaw=(I16, I16),
               maxTurn=range(32768), flags=U16, search=U16, boxSearch=U16, step=range(32768),
               drop=range(-32768, 1), fly=range(32768), blockMask=U16, zoneCount=range(1079),
               destination=POSITION, waypoint=POSITION)
BRAIN22 = layout('slot enemy mood jointYaw maxTurn flags box targetBox requiredBox nextBox '
                 'headBox tailBox search boxSearch step drop fly blockMask zoneCount destination waypoint',
                 BOX22, slot=range(5), enemy=SLOT, mood=range(4), jointYaw=(I16, I16),
                 maxTurn=range(32768), flags=U16, search=U16, boxSearch=U16, step=range(32768),
                 drop=range(-32768, 1), fly=range(32768), blockMask=U16, zoneCount=range(626),
                 destination=POSITION, waypoint=POSITION)
ITEM21 = {**ITEM, 'ai': BRAIN}
ITEM22 = {**ITEM, 'ai': BRAIN22}
HULL = layout('turn bowVSpeed bank terrainYaw supportY', I32,
              bowVSpeed=(I32, I32), bank=I16, terrainYaw=I16)
BOAT_PRESENT = layout('index present item hull', index=U8, present=range(1, 2), item=ITEM, hull=HULL)
BOAT_ABSENT = layout('index present item hull', index=U8, present=range(0, 1),
                     item=type(None), hull=type(None))
RACE_LEAF = layout('index timer latch active', index=U8, timer=I16, latch=range(32), active=BIT)
RACE = layout('timer latch active leaves', timer=I16, latch=range(32), active=BIT,
              leaves=(RACE_LEAF, RACE_LEAF))
MINE = layout('index spent active', index=U8, spent=BIT, active=BIT)
MINES = layout('chain items', chain=BIT, items=(MINE,) * 6)
RECORD = layout(
    'schema tick level lara camera active save inventory world rng hash',
    lara={**ITEM, **layout('water oxygen weaponState weapon goalWeapon lastWeapon input swimTimer '
                           'moveAngle turnSpeed hitFrame hitTimer hitQuadrant head torso flareAge '
                           'flareFrame flareLeft flareRight burning waterDistance climbing vehicle '
                           'weaponItem backWeapon target armR armL ammo', I16,
                           water=frozenset((0, 1, 2, 3, 5)), weaponState=range(6), weapon=range(10),
                           goalWeapon=range(10), lastWeapon=range(10), input=U32, head=ANGLE,
                           hitQuadrant=range(-1, 4),
                           torso=ANGLE, flareLeft=BIT, flareRight=BIT, burning=BIT,
                           waterDistance=I32, climbing=BIT, vehicle=SLOT, weaponItem=SLOT,
                           backWeapon=range(265), target=SLOT, armR=ARM, armL=ARM, ammo=(U32,) * 7)},
    camera=layout('pos target room targetRoom targetDist targetAngle angle targetElevation lookAtItem '
                  'lastItem speed timer index lastIndex bounce mode lastFixed', I16,
                  pos=POSITION, target=POSITION, room=ROOM, targetRoom=ROOM, targetDist=I32,
                  lookAtItem=SLOT, lastItem=SLOT, bounce=I32, mode=range(6), lastFixed=BIT),
    active=[ITEM], save=layout('secrets time distance kills ammoUsed ammoHits mediUsed', U32,
                              secrets=U8, kills=U16, mediUsed=U8),
    inventory=layout('items mainSelection keySelection', I16, items=[(U16, U16)]),
    world=(U32,) * 4, rng=(U32,) * 2, hash=str)
RECORD22 = layout(
    'schema tick level lara camera active save inventory world rng boats race mines effectsUsed hash',
    **{k: v for k, v in RECORD.items() if k != 'hash'},
    boats=list, race=RACE, mines=MINES, effectsUsed=range(101), hash=str)
MANIFEST = layout('schema ticks identicalRuns date wine exeSHA256 levelSHA256 inputSHA256 traceSHA256',
                  date=str, wine=str, exeSHA256=str, levelSHA256=str, inputSHA256=str,
                  traceSHA256=str)
VENICE_ENTRY_FORM = layout(
    'finishInputSHA256 cutActionPoll statisticsConfirmPoll weapon shotgunPellets '
    'spareFlares smallMedikits largeMedikits',
    int, finishInputSHA256=str)


def validate(value, form, path):
    if isinstance(form, dict):
        if type(value) is not dict or tuple(value) != tuple(form):
            raise ValueError(f'{path}: wrong fields or field order')
        for key, subform in form.items():
            validate(value[key], subform, f'{path}.{key}')
    elif isinstance(form, (tuple, list)):
        if type(value) is not list or (isinstance(form, tuple) and len(value) != len(form)):
            raise ValueError(f'{path}: wrong array shape')
        for index, entry in enumerate(value):
            validate(entry, form[index] if isinstance(form, tuple) else form[0], f'{path}[{index}]')
    elif isinstance(form, range):
        if type(value) is not int or value not in form:
            raise ValueError(f'{path}: expected integer in [{form.start}, {form.stop - 1}]')
    elif isinstance(form, frozenset):
        if type(value) is not int or value not in form:
            raise ValueError(f'{path}: expected one of {sorted(form)}')
    elif type(value) is not form:
        raise ValueError(f'{path}: expected {form.__name__}')


def fnv32(data):
    state = 2166136261
    for byte in data:
        state = ((state ^ byte) * 16777619) & 0xffffffff
    return f'{state:08x}'


def masks(data, markers=()):
    if not re.fullmatch(rb'[ \t\r\n]*[0-9]+(?:[ \t\r\n]+[0-9]+)*[ \t\r\n]*', data):
        raise ValueError('input.txt: expected a nonempty decimal mask script')
    result = [int(token.lstrip(b'0') or b'0') for token in data.split()]
    allowed = set(markers)
    if any((mask != 2048) if tick in allowed else (mask & ~MASK or mask == 2048)
           for tick, mask in enumerate(result, 1)):
        raise ValueError('input.txt: unsupported input mask or shotgun menu marker')
    return result


def unique_object(pairs):
    result = dict(pairs)
    if len(result) != len(pairs):
        raise ValueError('duplicate JSON key')
    return result


def _fixture_data(directory, *, metadata=False):
    manifest = json.loads((directory / 'manifest.json').read_bytes(), object_pairs_hook=unique_object)
    if type(manifest) is not dict:
        raise ValueError('manifest: expected object')
    schema = manifest.get('schema')
    if schema == 22:
        allowed_extra = {'placement', 'level', 'levelFile', 'entry'}
    else:
        allowed_extra = {
            'placement', 'schema20SHA256', 'preLevelSeed', 'keyConfirmation', 'shotgunSelection',
            'medipackSelection',
        }
    if set(manifest) - allowed_extra != MANIFEST.keys():
        raise ValueError('manifest: wrong field set')
    validate({key: manifest[key] for key in MANIFEST}, MANIFEST, 'manifest')
    expected_level_sha = VENICE_LEVEL if schema == 22 else LEVEL
    if (schema not in (20, 21, 22) or manifest['identicalRuns'] < 3 or manifest['wine'] != '11.22' or
            manifest['exeSHA256'] != EXE or manifest['levelSHA256'] != expected_level_sha or
            not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}', manifest['date'])):
        raise ValueError('manifest: wrong schema or capture identity')
    datetime.date.fromisoformat(manifest['date'])
    if schema == 22:
        validate(manifest.get('level'), range(2, 3), 'manifest.level')
        validate(manifest.get('levelFile'), str, 'manifest.levelFile')
        entry = manifest.get('entry')
        if type(entry) is not dict or set(entry) != VENICE_ENTRY_FORM.keys():
            raise ValueError('manifest: invalid schema-22 Venice entry fields')
        validate({key: entry[key] for key in VENICE_ENTRY_FORM}, VENICE_ENTRY_FORM, 'manifest.entry')
        if manifest['levelFile'] != 'BOAT.TR2' or entry != VENICE_ENTRY:
            raise ValueError('manifest: invalid schema-22 Venice level or entry metadata')
    if 'schema20SHA256' in manifest:
        digest = manifest['schema20SHA256']
        if schema != 21 or type(digest) is not str or not re.fullmatch(r'[0-9a-f]{64}', digest):
            raise ValueError('manifest: invalid schema-20 projection digest')
    if 'preLevelSeed' in manifest and (schema != 21 or
            type(manifest['preLevelSeed']) is not int or manifest['preLevelSeed'] != 3547461959):
        raise ValueError('manifest: invalid pre-level seed')
    shotgun = manifest.get('shotgunSelection')
    if 'shotgunSelection' in manifest:
        if (type(shotgun) is not dict or set(shotgun) != {'tick', 'type', 'pausedTicks'} or
                any(type(value) is not int for value in shotgun.values())):
            raise ValueError('manifest: invalid shotgun selection fields')
        if (schema != 21 or shotgun['type'] != 158 or shotgun['pausedTicks'] != 87 or
                not 2 <= shotgun['tick'] <= manifest['ticks']):
            raise ValueError('manifest: invalid shotgun selection')
    placement = manifest.get('placement')
    if 'placement' in manifest:
        validate(placement, (I32, I32, I32, I16, ROOM), 'manifest.placement')
    for name, key in (('input.txt', 'inputSHA256'), ('trace.jsonl', 'traceSHA256')):
        data = (directory / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != manifest[key]:
            raise ValueError(f'manifest: {name} SHA-256 mismatch')
        if name == 'input.txt':
            if placement is not None:
                line, lf, data = data.partition(b'\n')
                fields = re.fullmatch(rb'place(?:[ \t]+-?[0-9]+){5}[ \t]*\r?', line)
                if not lf or len(line) > 127 or not fields or list(map(int, line[5:].split())) != placement:
                    raise ValueError('manifest: placement line does not match the declared placement')
            markers = []
            if shotgun:
                markers.append(shotgun['tick'] - 1)
            medi = manifest.get('medipackSelection')
            if isinstance(medi, list):
                markers.extend(event['tick'] - 1 for event in medi
                               if isinstance(event, dict) and type(event.get('tick')) is int)
            inputs = masks(data, markers)
    if manifest['ticks'] != len(inputs):
        raise ValueError('manifest: input tick count mismatch')
    if 'keyConfirmation' in manifest:
        if shotgun is not None:
            raise ValueError('manifest: only one inventory confirmation is supported')
        key = manifest['keyConfirmation']
        if type(key) is not dict or set(key) != {'tick', 'type', 'pausedTicks'}:
            raise ValueError('manifest: invalid keyConfirmation fields')
        validate(key['tick'], range(1, len(inputs) + 1), 'manifest.keyConfirmation.tick')
        validate(key['type'], range(197, 201), 'manifest.keyConfirmation.type')
        validate(key['pausedTicks'], U32, 'manifest.keyConfirmation.pausedTicks')
        tick = key['tick']
        if not inputs[tick - 1] & 128:
            raise ValueError('manifest: keyConfirmation needs an Action mask')
        # Verify the event is a pause and a removal, not extra gameplay
        # samples, injected stock or a change at placement.
        lines = data.splitlines()
        if len(lines) != len(inputs) + 1:
            raise ValueError('manifest: keyConfirmation record count mismatch')
        prior = json.loads(lines[tick - 1], object_pairs_hook=unique_object)
        used = json.loads(lines[tick], object_pairs_hook=unique_object)
        try:
            if (prior['save']['time'] != tick - 1 or
                    used['save']['time'] != ((tick + key['pausedTicks']) & 0xffffffff) or
                    [e for e in prior['inventory']['items'] if 197 <= e[0] <= 200] != [[key['type'], 1]] or
                    any(e[0] == key['type'] for e in used['inventory']['items'])):
                raise ValueError('manifest: keyConfirmation disagrees with the recorded ring or clock')
        except (IndexError, KeyError, TypeError) as exc:
            raise ValueError('manifest: malformed keyConfirmation record') from exc
    if 'medipackSelection' in manifest:
        if shotgun is not None or 'keyConfirmation' in manifest:
            raise ValueError('manifest: only one inventory confirmation is supported')
        events = manifest['medipackSelection']
        expected = [
            {'pausedTicks': 87, 'presentationAdvances': 88, 'tick': 169, 'type': 171},
            {'pausedTicks': 81, 'presentationAdvances': 82, 'tick': 191, 'type': 172},
        ]
        if events != expected or manifest['ticks'] < 191:
            raise ValueError('manifest: invalid medipack selection')

    return (data, inputs, placement, manifest) if metadata else (data, inputs, placement)


def fixture_data(directory):
    data, inputs, _ = _fixture_data(directory)
    return data, inputs

def records(data, inputs, source, placement=None, schema=20, *, key_confirmation=None, shotgun=None, medipacks=None):
    lines = data.split(b'\n')
    if len(lines) != len(inputs) + 2 or lines[-1] != b'':
        raise ValueError(f'{source}: wrong record count or missing final LF')
    for tick, (line, mask) in enumerate(zip(lines, (0, *inputs))):
        path = f'{source}: tick {tick}'
        try:
            record = json.loads(line.decode('ascii'), object_pairs_hook=unique_object)
            if schema in (21, 22):
                base_record = RECORD22 if schema == 22 else RECORD
                item_with_brain = ITEM22 if schema == 22 else ITEM21
                validate(record, base_record | {'active': list}, path)
                brains = set()
                for i, item in enumerate(record['active']):
                    child = f'{path}.active[{i}]'
                    has_brain = type(item) is dict and 'ai' in item
                    validate(item, item_with_brain if has_brain else ITEM, child)
                    if has_brain:
                        brain = item['ai']
                        if brain['slot'] in brains:
                            raise ValueError(f'{child}: duplicate navigation slot')
                        brains.add(brain['slot'])
                        if brain['box'] == -1 and (brain['nextBox'] != -1 or brain['boxSearch'] != 0):
                            raise ValueError(f'{child}: route link without a stored box')
                        if brain['targetBox'] == -1 and any(brain['destination'] + brain['waypoint']):
                            raise ValueError(f'{child}: route points without a target box')
                if schema == 22:
                    boats = record['boats']
                    if len(boats) != 2 or [b.get('index') if type(b) is dict else None for b in boats] != [26, 91]:
                        raise ValueError(f'{path}.boats: expected permanent slots [26, 91]')
                    for i, boat in enumerate(boats):
                        bpath = f'{path}.boats[{i}]'
                        validate(boat, BOAT_PRESENT if boat.get('present') == 1 else BOAT_ABSENT, bpath)
                        if boat['present'] == 1:
                            if boat['item']['index'] != boat['index'] or boat['item']['type'] != 14:
                                raise ValueError(f'{bpath}: item index/type mismatch')
                    if [leaf['index'] for leaf in record['race']['leaves']] != [73, 74]:
                        raise ValueError(f'{path}.race.leaves: expected slots [73, 74]')
                    if [mine['index'] for mine in record['mines']['items']] != [106, 107, 108, 109, 110, 111]:
                        raise ValueError(f'{path}.mines.items: expected slots 106..111')
            else:
                validate(record, RECORD, path)
        except (ValueError, RecursionError) as exc:
            raise ValueError(f'{path}: {exc}') from exc
        canonical = json.dumps(record, separators=(',', ':')).encode('ascii')
        if canonical != line:
            raise ValueError(f'{path}: noncanonical wire bytes')
        digest = record.pop('hash')
        payload = json.dumps(record, separators=(',', ':')).encode('ascii')
        if fnv32(payload) != digest:
            raise ValueError(f'{path}: FNV-1a hash mismatch')
        expected_level = 2 if schema == 22 else 1
        if (record['schema'], record['tick'], record['level'], record['lara']['input']) != (schema, tick, expected_level, mask):
            raise ValueError(f'{path}: wrong schema, tick, level or input')
        # tr2-oracle-trace.md: a native menu pause adds pausedTicks to the
        # saved clock and emits no extra world row. Two medipack pauses accumulate.
        extra = 0
        if medipacks:
            extra = sum(event['pausedTicks'] for event in medipacks if tick >= event['tick'])
        else:
            menu = key_confirmation if key_confirmation is not None else shotgun
            if menu and tick >= menu['tick']:
                extra = menu['pausedTicks']
        time = (tick + extra) & 0xffffffff
        if source == 'retail' and (record['save']['time'] != time or
                                   (tick == 0 and record['rng'] != [3547461959] * 2)):
            raise ValueError(f'{path}: wrong retail clock or initial seed')
        if tick == 0 and placement is not None:
            lara = record['lara']
            if (lara['pos'], lara['rot'], lara['room']) != (
                    placement[:3], [0, placement[3], 0], placement[4]):
                raise ValueError(f'{path}: Lara position, rotation or room does not match placement')
        slots = {item['index'] for item in record['active']}
        if len(slots) != len(record['active']) or record['lara']['index'] in slots:
            raise ValueError(f'{path}: active identities are not unique world item slots')
        if schema == 22:
            active_by_slot = {item['index']: {k: v for k, v in item.items() if k != 'ai'} for item in record['active']}
            for boat in record['boats']:
                idx = boat['index']
                if boat['present'] == 0 and idx in active_by_slot:
                    raise ValueError(f'{path}: removed boat {idx} is on active list')
                if boat['present'] == 1 and idx in active_by_slot and boat['item'] != active_by_slot[idx]:
                    raise ValueError(f'{path}: active boat {idx} disagrees with boats entry')
            if record['race']['active'] != int(76 in slots):
                raise ValueError(f'{path}: race clock active disagrees with active list')
            for leaf in record['race']['leaves']:
                if leaf['active'] != int(leaf['index'] in slots):
                    raise ValueError(f"{path}: race leaf {leaf['index']} active disagrees with active list")
            for mine in record['mines']['items']:
                if mine['active'] != int(mine['index'] in slots):
                    raise ValueError(f"{path}: mine {mine['index']} active disagrees with active list")
        yield record


def project_schema20(data, inputs, placement=None, *, key_confirmation=None):
    """Project checked schema-21 rows, preserving the surviving field order."""
    for record in records(data, inputs, 'retail', placement, 21, key_confirmation=key_confirmation):
        record['schema'] = 20
        for item in record['active']:
            item.pop('ai', None)
        payload = json.dumps(record, separators=(',', ':')).encode('ascii')
        yield payload[:-1] + b',"hash":"' + fnv32(payload).encode('ascii') + b'"}\n'


def first_difference(a, b, path, missing):
    """Return (first difference, whether any part of this value was compared)."""
    if path in missing:
        return None, False
    if type(a) is not type(b):
        return {'path': path, 'retail': a, 'port': b}, True
    known = False
    if isinstance(a, dict):
        # Schema 21 permits a missing brain, not a null on the wire.
        pairs = ((a.get(key), b.get(key), f'{path}.{key}')
                 for key in (a if a.keys() == b.keys() else dict.fromkeys((*a, *b))))
    elif isinstance(a, list):
        if len(a) != len(b):
            return {'path': f'{path}.length', 'retail': len(a), 'port': len(b)}, True
        if not a:
            return None, True  # Empty known list; its length was compared.
        pairs = ((x, y, f'{path}[{index}]') for index, (x, y) in enumerate(zip(a, b)))
    else:
        return (None if a == b else {'path': path, 'retail': a, 'port': b}), True
    for left, right, child in pairs:
        diff, compared = first_difference(left, right, child, missing)
        if diff:
            return diff, True
        known |= compared
    return None, known


def shape_paths(form, path):
    yield path
    if isinstance(form, dict):
        for key, subform in form.items():
            yield from shape_paths(subform, f'{path}.{key}')
    elif isinstance(form, tuple):
        for index, subform in enumerate(form):
            yield from shape_paths(subform, f'{path}[{index}]')


def is_under(path, parent):
    return path == parent or path.startswith((parent + '.', parent + '['))


def active_missing(left, right, missing, previous):
    """Resolve reviewed per-object omissions without hiding list order or a wake."""
    known = set()
    for path in missing:
        selector = ACTIVE_SELECTOR.fullmatch(path)
        if not selector:
            known.add(path)
            continue
        kind, after_wake, field = selector.groups()
        for i, (a, b) in enumerate(zip(left, right)):
            slot = a['index'], a['type']
            if (slot == (b['index'], b['type']) and slot[1] == int(kind) and
                    (not after_wake or slot in previous)):
                known.add(f'active[{i}].{field}')
    return known


def valid_active_selector(path, schema=20):
    selector = ACTIVE_SELECTOR.fullmatch(path)
    fields = ITEM22 if schema == 22 else ITEM21 if schema == 21 else ITEM
    return (selector and int(selector[1]) in ITEM['type'] and
            selector[3] in fields and selector[3] not in ('index', 'type'))

def availability(missing, last_tick, schema=20):
    rec = RECORD22 if schema == 22 else RECORD
    fams = FAMILIES22 if schema == 22 else FAMILIES
    domains = {path for family in fams for path in shape_paths(rec[family], family)}
    if type(missing) is not dict:
        raise ValueError('availability: expected object {field: [start, end]}')
    for path, interval in missing.items():
        if ((path not in domains and not valid_active_selector(path, schema)) or
                type(interval) is not list or len(interval) != 2):
            raise ValueError(f'availability: invalid path or interval for {path}')
        begin, end = interval
        if end is None:
            end = last_tick
        if type(begin) is not int or type(end) is not int or not 0 <= begin <= end <= last_tick:
            raise ValueError(f'availability: invalid tick range for {path}')
    return missing


def port_body(port, schema=None):
    # The footer is part of the artifact, separate from its canonical
    # schema-20/21/22 records. Hash it and bind it to the exact preceding bytes.
    if not port.endswith(b'\n'):
        raise ValueError('port: missing availability footer or final LF')
    boundary = port.rfind(b'\n', 0, len(port) - 1) + 1
    if boundary == 0:
        raise ValueError('port: missing availability footer')
    body, line = port[:boundary], port[boundary:-1]
    footer = json.loads(line.decode('ascii'), object_pairs_hook=unique_object)
    layouts = (('schema', 'producer', 'unavailable', 'recordsHash', 'hash'),
               ('schema', 'producer', 'unavailable', 'coverageEnds', 'recordsHash', 'hash'))
    if (type(footer) is not dict or tuple(footer) not in layouts or
            footer['schema'] not in (20, 21, 22) or (schema is not None and footer['schema'] != schema) or
            type(footer['schema']) is not int or type(footer['producer']) is not str):
        raise ValueError('port: malformed availability footer')
    if json.dumps(footer, separators=(',', ':')).encode('ascii') != line:
        raise ValueError('port: noncanonical availability footer')
    hash_value = footer.pop('hash')
    if fnv32(json.dumps(footer, separators=(',', ':')).encode('ascii')) != hash_value:
        raise ValueError('port: availability footer hash mismatch')
    if fnv32(body) != footer['recordsHash']:
        raise ValueError('port: availability footer does not bind these trace records')
    ends = footer.get('coverageEnds')
    if 'coverageEnds' in footer and type(ends) is not dict:
        raise ValueError('port: coverageEnds must be an object')
    return body, footer['producer'], footer['unavailable'], ends


def producer_profile(name):
    profiles = json.loads(PROFILES.read_bytes(), object_pairs_hook=unique_object)
    if name not in profiles['producerExtras']:
        raise ValueError(f'port: unknown trace producer {name!r}')
    def resolve(producer, seen):
        if producer in seen or producer not in profiles['producerExtras']:
            raise ValueError(f'producer profile: unknown parent or cycle at {producer!r}')
        parent = profiles.get('producerParent', {}).get(producer)
        result = (resolve(parent, seen | {producer}) if parent is not None
                  else dict(profiles['common']))
        result.update(profiles['producerExtras'][producer])
        for group in profiles.get('producerNowAvailable', {}).get(producer, ()):
            covered = [path for path in result if is_under(path, group)]
            if not covered:
                raise ValueError(f'producer profile: no inherited exclusion for {group!r}')
            for path in covered:
                del result[path]
        return result

    return resolve(name, set())


def reviewed_ends(producer, retail, ticks, ends):
    """A coverage cut is approved for the exact retail stream, not its path."""
    registry = json.loads(PROFILES.read_bytes(), object_pairs_hook=unique_object)
    chain = []
    while producer is not None:
        chain.append(producer)
        producer = registry.get('producerParent', {}).get(producer)
    producer = chain[0]
    routes = {}
    for ancestor in reversed(chain):
        for name, rule in registry.get('routeCoverage', {}).get(ancestor, {}).items():
            if name not in routes:
                routes[name] = rule
                continue
            inherited = routes[name]
            if inherited['traceSHA256'] != rule['traceSHA256']:
                raise ValueError(f'port: inherited coverage changed trace identity for {name!r}')
            for family, new in rule.items():
                if family == 'traceSHA256':
                    continue
                old = inherited.get(family)
                if new[1] is not None and (old is None or old[1] is None or
                    new[0] < old[0] or (new[0] == old[0] and new != old)):
                    raise ValueError(f'port: inherited coverage shortened or changed {family} for {name!r}')
            routes[name] = inherited | rule
    if not routes:
        if ends is not None:
            raise ValueError(f'port: producer {producer} does not permit coverage ends')
        return {}
    digest = hashlib.sha256(retail).hexdigest()
    approved = [rule for rule in routes.values() if rule['traceSHA256'] == digest]
    if len(approved) != 1:
        raise ValueError(f'port: route has no unique reviewed coverage for {producer}')
    if ends is None:
        raise ValueError('port: missing coverageEnds')
    for family, bound in ends.items():
        if (family not in REQUIRED22 or type(bound) is not dict or
                tuple(bound) != ('lastTick', 'reason') or type(bound['lastTick']) is not int or
                not 0 <= bound['lastTick'] < ticks or type(bound['reason']) is not str):
            raise ValueError(f'port: invalid coverage end for {family}')
    expected = {}
    for family, span in approved[0].items():
        if family == 'traceSHA256':
            continue
        last, reason = span
        if family not in REQUIRED22 or not 0 <= last <= ticks or (last == ticks) != (reason is None):
            raise ValueError(f'port: invalid reviewed coverage for {family}')
        if reason is not None:
            expected[family] = {'lastTick': last, 'reason': reason}
    if ends != expected:
        raise ValueError(f'port: coverage ends differ from reviewed route: expected {expected}, got {ends}')
    return ends


def producer_descends(producer, ancestor):
    """A child producer publishes a field its parent left out. The parent's
    reviewed routes still apply; the footer must match the child."""
    profiles = json.loads(PROFILES.read_bytes(), object_pairs_hook=unique_object)
    seen = set()
    while producer != ancestor:
        if producer in seen:
            return False
        seen.add(producer)
        producer = profiles.get('producerParent', {}).get(producer)
        if producer is None:
            return False
    return True


def compare(directory, port, expected_producer):
    retail, inputs, placement, manifest = _fixture_data(directory, metadata=True)
    schema = manifest['schema']
    port, producer, coverage, ends = port_body(port, schema)
    if producer != expected_producer and not producer_descends(producer, expected_producer):
        raise ValueError(f'port: expected producer {expected_producer!r}, got {producer!r}')
    profile = producer_profile(producer)
    n = len(inputs)
    unavailable = availability(coverage, n, schema)
    if unavailable != profile:
        raise ValueError(f'port: footer coverage differs from reviewed producer profile {producer}')
    ends = reviewed_ends(expected_producer, retail, n, ends)
    unavailable = unavailable | {family: [bound['lastTick'] + 1, None] for family, bound in ends.items()}
    required_keys = REQUIRED22 if schema == 22 else REQUIRED
    result = {
        key: {'equal': 0, 'prefix': 0, 'knownEqual': 0, 'knownPrefix': 0, 'first': None,
              'unavailable': {path: ticks for path, ticks in unavailable.items()
                              if is_under(path, key) or is_under(key, path)}}
        for key in required_keys
    }
    previous_active = set()
    for tick, (left, right) in enumerate(zip(
            records(retail, inputs, 'retail', placement, schema,
                    key_confirmation=manifest.get('keyConfirmation'), shotgun=manifest.get('shotgunSelection'),
                    medipacks=manifest.get('medipackSelection')),
            records(port, inputs, 'port', placement, schema))):
        for key, stats in result.items():
            a, b = (left['rng'][0], right['rng'][0]) if key == 'rng[0]' else (left[key], right[key])
            missing = {path for path, (begin, end) in stats['unavailable'].items()
                       if begin <= tick <= (n if end is None else end)}
            if missing and any(is_under(key, path) for path in missing):
                continue  # No comparable values in this family at this tick.
            if key == 'active' and missing:
                missing = active_missing(a, b, missing, previous_active)
            diff, compared = first_difference(a, b, key, missing)
            if not compared:
                continue
            if diff:
                if stats['first'] is None:
                    stats['first'] = {'tick': tick, **diff}
                continue
            stats['knownEqual'] += 1
            if stats['knownPrefix'] == tick:
                stats['knownPrefix'] += 1
            if not missing:
                stats['equal'] += 1
                if stats['prefix'] == tick:
                    stats['prefix'] += 1
        previous_active = ({(item['index'], item['type']) for item in left['active']} &
                           {(item['index'], item['type']) for item in right['active']})
    return {'schema': schema, 'records': len(inputs) + 1, 'producer': producer, 'coverageEnds': ends, 'families': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path, help='directory containing the retail trace, input and manifest')
    parser.add_argument('port', type=Path, help='port JSONL trace')
    parser.add_argument('--producer', required=True, help='expected producer of this run, pinned by the caller')
    parser.add_argument('--accept-profile-exclusions', action='store_true',
                        help='explicitly gate only fields produced by the reviewed producer profile')
    parser.add_argument('--require', choices=REQUIRED22, action='append',
                        help='require equality in this family (repeatable; default: all schema families)')
    args = parser.parse_args()
    try:
        report = compare(args.fixture, args.port.read_bytes(), args.producer)
        default_families = FAMILIES22 if report['schema'] == 22 else FAMILIES
        required = args.require or default_families
        for key in required:
            if key not in report['families']:
                raise ValueError(f'family {key!r} is not present in schema {report["schema"]}')
        uncovered = {path for key in required for path in report['families'][key]['unavailable']}
        accepted = uncovered if args.accept_profile_exclusions else set()
    except (OSError, ValueError, RecursionError) as exc:
        print(f'{parser.prog}: {exc}', file=sys.stderr)
        return 2
    report['acceptedExclusions'] = sorted(accepted)
    print(json.dumps(report, indent=2))
    return int(bool(uncovered - accepted) or
               any(report['families'][key]['knownEqual'] !=
                   report['coverageEnds'].get(key, {}).get('lastTick', report['records'] - 1) + 1
                   for key in required))


if __name__ == '__main__':
    sys.exit(main())
