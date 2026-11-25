# SPDX-License-Identifier: MIT
"""Bounded real-data scene recipes for save_coverage; no simulation setters.

Run with PYTHONPATH=tools/cook. Coordinates are read from the user's PC data;
all generated inputs/manifests belong outside the repository. Geometry seeds
are candidates, never proof that Lara reached a requested state.
"""
import hashlib
import json
from pathlib import Path
from save_coverage import CATEGORIES, ENEMIES, SLOT, COLLECTIBLES, ENEMY_SPAWNERS

LEVELS = ('gym', 'level1', 'level2', 'level3a', 'level3b', 'level4', 'level5',
          'level6', 'level7a', 'level7b', 'level8a', 'level8b', 'level8c',
          'level10a', 'level10b', 'level10c')
ACTIONS = 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 | 512 | 2048
LEVEL_IDS = dict(zip(LEVELS, (1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 13, 14, 15, 16, 18, 20)))


def static_counts(level):
    items = level.data['items']
    placed = sum(item['type'] in ENEMIES for item in items)
    spawners = sum(item['type'] in ENEMY_SPAWNERS for item in items)
    return dict(enemy=placed + spawners, placed_enemy=placed, enemy_spawner=spawners,
                door=sum(57 <= item['type'] <= 64 for item in items),
                boulder=sum(item['type'] == 38 for item in items),
                switch=sum(item['type'] in (55, 56) for item in items),
                pickup=sum(item['type'] in COLLECTIBLES for item in items),
                weapon_pickup=sum(84 <= item['type'] <= 87 for item in items),
                water=sum(bool(room.flags & 1) for room in level.rooms))


def na_reason(name, category, counts):
    resource = {'enemy-active': 'enemy', 'enemy-dead': 'enemy', 'door-half': 'door',
                'boulder-rolling': 'boulder', 'after-switch': 'switch',
                'after-pickup': 'pickup', 'water': 'water'}.get(category)
    if resource is not None and counts[resource] == 0:
        if resource == 'enemy':
            return 'enemy count=0: placed_enemy=0 and authored_enemy_spawner=0'
        return resource + ' count=0 in immutable source item/room metadata'
    if category == 'drawn' and name == 'gym' and counts['weapon_pickup'] == 0:
        return 'weapon_pickup count=0 and Source GYM default loadout owns no weapon'
    return None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_input(path):
    words = Path(path).read_text(encoding='ascii').split()
    start = None
    if words and words[0] == 'start':
        if len(words) < 6:
            raise ValueError('short start directive: ' + str(path))
        start = [int(value, 10) for value in words[1:6]]
        words = words[6:]
    masks = [int(word, 16 if word.lower().startswith('0x') else 10) for word in words]
    if not masks or len(masks) > 600 or any(mask < 0 or mask & ~ACTIONS for mask in masks):
        raise ValueError('invalid source input: ' + str(path))
    return start, masks + [0] * (600 - len(masks))


def program(*parts):
    values = [mask for mask, count in parts for _ in range(count)]
    if len(values) > 600:
        raise ValueError('recipe exceeds 600 ticks')
    return values + [0] * (600 - len(values))


def trigger(words, offset):
    """Read admitted TR1 floor commands/actions, preserving camera extra words."""
    if not offset:
        return None
    for _ in range(16):
        if not 0 <= offset < len(words):
            raise ValueError('floor command outside source words')
        command = words[offset]
        offset += 1
        kind = command & 31
        if kind in (1, 2, 3):
            offset += 1
        elif kind == 4:
            if offset >= len(words):
                raise ValueError('missing trigger parameters')
            parameters = words[offset]
            offset += 1
            trigger_type = (command >> 8) & 127
            source_item = None
            if trigger_type in (2, 3, 4):
                if offset >= len(words):
                    raise ValueError('missing trigger source item')
                source_item = words[offset] & 1023
                offset += 1
            targets = []
            for _ in range(128):
                if offset >= len(words):
                    raise ValueError('unterminated trigger')
                action = words[offset]
                offset += 1
                action_type, argument = (action >> 10) & 31, action & 1023
                if action_type == 0:
                    targets.append(argument)
                if action_type == 1:
                    if offset >= len(words):
                        raise ValueError('missing camera extra word')
                    action = words[offset]
                    offset += 1
                if action & 32768:
                    return dict(type=trigger_type, source=source_item, targets=targets,
                                parameters=parameters)
            raise ValueError('trigger action bound exceeded')
        elif kind != 5:
            return None
        if command & 32768:
            return None
    raise ValueError('floor command bound exceeded')


def flat(level, sector):
    if sector['floor'] == -127 or sector['ceiling'] == -127 \
            or sector['room_below'] != 255 or sector['room_above'] != 255:
        return False
    index = sector['floor_index']
    if index and level.data['floor_data'][index] & 31 in (1, 2, 3):
        return False
    return True


def centers(level, wet=False, minimum=1280):
    for room_index, room in enumerate(level.rooms):
        if bool(room.flags & 1) != wet:
            continue
        for x in range(1, room.x_sectors - 1):
            for z in range(1, room.z_sectors - 1):
                sector = room.sectors[x * room.z_sectors + z]
                if wet:
                    valid = sector['floor'] != -127 and sector['ceiling'] != -127
                else:
                    valid = flat(level, sector)
                if valid and (sector['floor'] - sector['ceiling']) * 256 >= minimum:
                    yield room_index, room, x, z, sector


def trigger_sectors(level):
    # Trigger references may live in water/portal/slanted sectors. Do not
    # misclassify their objects N/A merely because a flat-jump filter misses.
    for room_index, room in enumerate(level.rooms):
        for x in range(1, room.x_sectors - 1):
            for z in range(1, room.z_sectors - 1):
                sector = room.sectors[x * room.z_sectors + z]
                if sector['floor'] != -127 and sector['ceiling'] != -127 \
                        and sector['floor_index']:
                    yield room_index, room, x, z, sector


def standing(room_index, room, x, z, sector, angle=0):
    return [room_index, room.info['x'] + x * 1024 + 512, sector['floor'] * 256,
            room.info['z'] + z * 1024 + 512, angle]


def ledges(level):
    for room_index, room, x, z, lower in centers(level):
        for dx, dz, angle in ((0, 1, 0), (1, 0, 16384), (0, -1, -32768), (-1, 0, -16384)):
            nx, nz = x + dx, z + dz
            upper = room.sectors[nx * room.z_sectors + nz]
            if not flat(level, upper):
                continue
            height = (lower['floor'] - upper['floor']) * 256
            if not 640 <= height <= 1536:
                continue
            if (upper['floor'] - upper['ceiling']) * 256 < 768:
                continue
            start = standing(room_index, room, x, z, lower, angle)
            start[1] += dx * 384
            start[3] += dz * 384
            yield start, dict(room=room_index, lower_sector=[x, z], upper_sector=[nx, nz],
                              ledge_height=height)


def pickup_starts(level, types=None):
    for index, item in enumerate(level.data['items']):
        if item['type'] not in SLOT or types is not None and item['type'] not in types or item['flags'] & 256:
            continue
        room = level.rooms[item['room']]
        start = [item['room'], item['x'], item['y'], item['z'], 0]
        if item['type'] == 143:
            # progression.h:81-137: actual Qualopec pickup limit/moveTo vector.
            start[2] += 640
            start[3] -= 310
        x, z = (start[1] - room.info['x']) >> 10, (start[3] - room.info['z']) >> 10
        if not 0 <= x < room.x_sectors or not 0 <= z < room.z_sectors:
            continue
        sector = room.sectors[x * room.z_sectors + z]
        if flat(level, sector) and sector['floor'] * 256 == start[2] \
                and (sector['floor'] - sector['ceiling']) * 256 >= 1024:
            yield index, item, start


def switch_start(item):
    angle = ((item['angle'] + 32768) & 65535) - 32768
    direction = {0: (0, 1), 16384: (1, 0), -32768: (0, -1), -16384: (-1, 0)}
    if angle not in direction:
        return None
    dx, dz = direction[angle]
    return [item['room'], item['x'] + dx * 412, item['y'], item['z'] + dz * 412, angle]


def plan(data, out, max_candidates, enemy_manifest=None, object_manifest=None,
         weapons=None, movement=None):
    import src_pc
    if not 1 <= max_candidates <= 8:
        raise ValueError('candidate bound must be 1..8')
    out.mkdir(parents=True, exist_ok=True)
    enemies = []
    for path in enemy_manifest or []:
        rows = json.loads(path.read_text())
        enemies.extend(rows['scenes'] if isinstance(rows, dict) else rows)
    object_data = json.loads(object_manifest.read_text()) if object_manifest else {}
    objects = object_data.get('existing_scenes', [])
    movement_rows = json.loads(movement.read_text()) if movement else []
    weapon_rows = json.loads((weapons / 'verification.json').read_text())['cases'] if weapons else []
    result = dict(version=1, levels=list(LEVELS), max_candidates=max_candidates,
                  scope='Static real-input candidates; no state or Save/Load result is certified by planning.', cells=[])
    for name in LEVELS:
        level = src_pc.load(data / (name + '.phd'))
        items = level.data['items']
        water_rooms = [i for i, room in enumerate(level.rooms) if room.flags & 1]
        by_category = {category: [] for category in CATEGORIES}

        def add(category, start, values, origin, **extra):
            candidates = by_category[category]
            if len(candidates) >= max_candidates:
                return
            index = extra.get('target_index')
            if category == 'after-switch' and index is not None:
                models = [model for model in level.data['models'] if model['type'] == items[index]['type']]
                if models:
                    extra['initial_state'] = level.data['animations'][models[0]['animation']]['state']
            if index is not None:
                kind = items[index]['type'] if index < len(items) else extra.get('source_type')
                if kind in (20, 21, 22, 34):
                    models = {model['type']: model for model in level.data['models']}
                    effective = models.get(kind)
                    if kind in (21, 22) and (effective is None or effective['mesh_count'] <= 1):
                        effective = models.get(20)
                    if effective is not None and effective['mesh_count'] > 0:
                        count = effective['mesh_count']
                        extra['model_count'] = count
                        extra['model_mask'] = 0xffffffff if count >= 32 else (1 << count) - 1
                        extra['model_animation_base'] = effective['animation']
            text = ('' if start is None else 'start ' + ' '.join(map(str, start)) + '\n')
            text += ''.join(f'{value}\n' for value in values)
            path = out / f'{name}-{category}-{len(candidates)}.masks'
            path.write_text(text, encoding='ascii')
            candidates.append(dict(input=str(path.resolve()), input_sha256=sha(path), origin=origin,
                                   level_id=LEVEL_IDS[name], min_save=10, max_save=520,
                                   water_rooms=water_rooms, **extra))

        # Proven movement inputs are copied/padded, not replaced by idle cases.
        for row in movement_rows:
            if row['name'].split('/')[0].lower() != name:
                continue
            for witness in row['witnesses']:
                category = witness['category']
                if category not in by_category:
                    continue
                start, values = source_input(row['input'])
                add(category, start, values, 'retained source movement trace',
                    save_tick=witness['proposed_save'], source_witness=witness)
        for row in weapon_rows:
            if row['level'] != name:
                continue
            original = weapons / Path(row['path']).name
            start, values = source_input(original)
            if row.get('pickup_type') in SLOT:
                add('after-pickup', start, values, 'proven actual pickup/menu-use input (no retained event tick)',
                    target_index=row['pickup_index'], inventory_slot=SLOT[row['pickup_type']])
            if row.get('weapon') is not None:
                add('drawn', start, values, 'proven actual weapon menu/fire input (no retained event tick)')
        rock_design = object_data.get('boulder_design_only')
        if rock_design and rock_design['level'] == name:
            add('boulder-rolling', rock_design['suggested_start'], program((0, 600)),
                'authored source boulder trigger metadata; placement/motion unexecuted',
                target_index=rock_design['item_index'], target_type=38,
                floor_index=rock_design['retail_trigger']['floor_index'])
        for row in objects:
            if name != 'level1':
                continue
            start, values = source_input(row['input'])
            if 'switch_index' in row:
                add('after-switch', start, values, 'authored real switch interaction input',
                    target_index=row['switch_index'], target_type=55, door_indices=row['door_indices'])
                for door in row['door_indices']:
                    add('door-half', start, values, 'authored real switch-to-door input',
                        target_index=door, target_type=items[door]['type'])
            if 'pickup_index' in row:
                add('after-pickup', start, values, 'authored real pickup input',
                    target_index=row['pickup_index'], inventory_slot=row['inventory_slot'])
        for row in sorted((row for row in enemies if row['level'] == name),
                          key=lambda row: ('death_state' not in row,
                                           row['type'] not in (7, 8, 9, 16, 17), row['type'])):
            if row['type'] not in ENEMIES:
                continue
            path = Path(row['input'])
            if not path.is_file():
                continue
            start, values = source_input(path)
            death_states = row.get('death_states', [row['death_state']] if 'death_state' in row else [])
            for target in [row['target_index']] + row.get('alternative_target_indices', []):
                for category in ('enemy-active', 'enemy-dead'):
                    add(category, start, values, 'retained real enemy-trigger input; boundary unqualified',
                        target_index=target, source_type=row['type'], death_states=death_states,
                        source_rule=row.get('source_rule'))

        dry = list(centers(level))
        for room_index, room, x, z, sector in dry[:max_candidates]:
            add('midjump', standing(room_index, room, x, z, sector),
                program((40, 60)), 'real dry flat sector with measured jump headroom')
            if name not in ('gym', 'level10a'):
                add('drawn', standing(room_index, room, x, z, sector),
                    program((256, 1), (0, 599)), 'normal source loadout; draw via input only')
        hang_rooms = set()
        for start, geometry in ledges(level):
            if start[0] in hang_rooms:
                continue
            hang_rooms.add(start[0])
            mask = 160 if len(hang_rooms) & 1 else 168
            add('hanging', start, program((mask, 90), (128, 90), (0, 420)),
                'real flat adjacent ledge geometry; hanging not yet observed', geometry=geometry)
            if len(by_category['hanging']) >= max_candidates:
                break
        for room_index, room, x, z, sector in centers(level, wet=True, minimum=1024):
            start = standing(room_index, room, x, z, sector)
            start[2] -= 512
            add('water', start, program((8, 120), (0, 480)),
                'real water-room sector and movement input, not a water-state setter')
            if len(by_category['water']) >= max_candidates:
                break
        for index, item, start in pickup_starts(level):
            add('after-pickup', start, program((128, 120)), 'real visible source pickup and ACTION',
                target_index=index, inventory_slot=SLOT[item['type']])
            if name == 'level10a' and item['type'] in (84, 85):
                # No pistols granted: choose the actually acquired ring weapon.
                add('drawn', start, program((128, 120), (2048, 1), (0, 30), (2, 1), (0, 14),
                                            (128, 1), (0, 120)), 'real LEVEL10A pickup and normal ring selection')

        seen = set()
        for room_index, room, x, z, sector in trigger_sectors(level):
            chain = trigger(level.data['floor_data'], sector['floor_index'])
            if chain is None:
                continue
            if chain['type'] == 2 and chain['source'] is not None:
                switch = chain['source']
                if switch >= len(items) or items[switch]['type'] not in (55, 56):
                    continue
                doors = [i for i in chain['targets'] if i < len(items) and 57 <= items[i]['type'] <= 64]
                key = ('switch', switch)
                if key not in seen:
                    seen.add(key)
                    start = switch_start(items[switch])
                    if start:
                        add('after-switch', start, program((128, 120)), 'real authored switch trigger',
                            target_index=switch, target_type=items[switch]['type'], door_indices=doors)
                        for door in doors:
                            add('door-half', start, program((128, 120)), 'real authored switch-to-door trigger',
                                target_index=door, target_type=items[door]['type'])
                        for target in chain['targets']:
                            if target < len(items) and items[target]['type'] == 38:
                                add('boulder-rolling', start, program((128, 120)),
                                    'actual authored switch activates indexed boulder',
                                    target_index=target, target_type=38)
            if chain['type'] not in (0, 1):
                continue
            for target in chain['targets']:
                if target >= len(items):
                    raise ValueError('authored trigger target outside original source items')
                kind = items[target]['type']
                key = ('trigger', target)
                if key in seen:
                    continue
                seen.add(key)
                category = 'boulder-rolling' if kind == 38 else 'door-half' if 57 <= kind <= 64 else None
                if category:
                    start = standing(room_index, room, x, z, sector)
                    if room.flags & 1:
                        start[2] -= 512
                    add(category, start, program((0, 600)),
                        'actual activation/pad floor and indexed authored object',
                        target_index=target, target_type=kind, floor_index=sector['floor_index'])

        counts = static_counts(level)
        for category in CATEGORIES:
            reason = na_reason(name, category, counts)
            candidates = by_category[category]
            result['cells'].append(dict(level=name, category=category, source_sha256=level.sha256,
                status='not-applicable' if reason else 'pending' if candidates else 'reference-required',
                reason=reason or ('no bounded real input candidate; not a proof of absence' if not candidates else None),
                asset_counts=counts, candidates=[] if reason else candidates))
    return result
