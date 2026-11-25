# SPDX-License-Identifier: MIT
"""Actual source-record predicates for awkward V3 saves, not input labels.

Enum/layout facts: OpenLara src/fixed/common.h, lara.h, object.h,
hazard_boulder.h at2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
Pure observation only: no game mutation, process creation or file output.
"""
CATEGORIES = ('midjump', 'hanging', 'water', 'drawn', 'enemy-active',
              'enemy-dead', 'door-half', 'boulder-rolling', 'after-pickup', 'after-switch')
ENEMIES = set(range(6, 25)) | {27, 28, 30, 31, 32, 33, 34}
JUMPS = {3, 25, 26, 27, 28}
HANGS = {10, 30, 31}
AIR = JUMPS | {9, 11, 29}
SLOT = {84: 11, 85: 13, 86: 15, 87: 17, 88: 12, 89: 14,
        90: 16, 91: 18, 93: 20, 94: 19, 110: 8, 111: 7,
        112: 6, 113: 5, 126: 0, 129: 1, 130: 2, 131: 3, 132: 4, 143: 9, 144: 9}
# Eligibility is deliberately broader than SLOT: Source's EXPLOSIVE and
# scripted Scion-holder interaction may need a reference-specific recipe.
COLLECTIBLES = set(range(84, 95)) | set(range(110, 114)) | {126, 129, 130, 131, 132, 143, 144, 146}
ENEMY_SPAWNERS = {161, 163, 181}
# Source enemy.h:990-1004,1169-1181,1368-1375,1452-1477,1624-1653,
# 1758-1771,1985-2009,2119-2136,2236-2258,2770-2807,2903-2958,
# 2963-2974,3063-3070,3425-3434. Runtime type handles land/water conversion.
DEATH_STATE = {7: 11, 8: 9, 9: 5, 10: 7, 11: 3, 12: 5, 13: 5, 14: 5,
               15: 5, 16: 5, 17: 3, 18: 5, 19: 0, 23: 5, 24: 2,
               27: 5, 28: 5, 30: 5, 31: 5, 32: 5}


def validate_cell_metadata(cell, level_id):
    name, category = cell['level'], cell['category']
    candidates = list(cell['candidates'])
    if cell['status'] == 'source-qualified':
        candidate = cell['candidate']
        if not any(candidate['input'] == original['input'] and
                   candidate['input_sha256'] == original['input_sha256']
                   for original in candidates):
            raise ValueError('qualified input is not a planned real candidate')
        candidates.append(candidate)
        save, load = cell.get('save_tick'), cell.get('load_tick')
        if type(save) is not int or type(load) is not int:
            raise ValueError('qualified save/load boundary must be an actual integer')
        if not candidate['min_save'] <= save <= candidate['max_save'] or not save < load <= 600:
            raise ValueError('qualified save/load boundary outside the source search/later-restore window')
        if load != save + 30:
            raise ValueError('qualified artifact changed the frozen thirty-tick restore interval')
        if cell.get('witness', {}).get('tick') != save:
            raise ValueError('qualified artifact does not retain its actual save-tick witness')
    for candidate in candidates:
        if candidate['level_id'] != level_id or candidate.get('level', name) != name \
                or candidate.get('category', category) != category:
            raise ValueError('candidate level/category does not match its matrix cell')
        low, high = candidate.get('min_save'), candidate.get('max_save')
        if type(low) is not int or type(high) is not int or not 1 <= low <= high <= 520:
            raise ValueError('save-boundary search is not bounded')


def excerpt(row, target=None):
    lara = row.get('lara')
    result = {'tick': row['tick'], 'record_hash': row['hash'], 'level': row.get('level'),
              'save': {key: row['save'][key] for key in ('time', 'kills', 'pickups', 'flipped')},
              'lara': None if lara is None else {key: lara[key] for key in
                  ('index', 'room', 'pos', 'rot', 'state', 'anim', 'frame', 'flags',
                   'vSpeed', 'hSpeed', 'health', 'water', 'weapon', 'weaponState')},
              'inventory_counts': row['inventory']['counts']}
    if target is not None:
        result['target'] = {key: target[key] for key in
            ('index', 'type', 'room', 'pos', 'state', 'goal', 'anim', 'frame',
             'flags', 'vSpeed', 'hSpeed', 'health', 'turnSpeed', 'visibleMask')}
        ai = target.get('ai')
        result['target']['ai_present'] = ai is not None
    return result


class Coverage:
    """Keep bounded causal history and certify a category at one actual row.

    A reference manifest may nominate an actor/animation, never a state value to
    write into the game. Positive health/AI, interpolation frame, item motion,
    inventory deltas and actual player interaction are checked independently.
    """
    def __init__(self, spec, animations):
        if spec['category'] not in CATEGORIES:
            raise ValueError('unknown save category ' + spec['category'])
        self.spec = spec
        self.animations = animations
        self.first = None
        self.previous = None
        self.previous_target = None
        self.saw_air = False
        self.saw_pickup = False
        self.saw_switch = False
        self.switch_changed = False
        self.target_motion = False
        self.target_alive = False
        self.target_initial_state = spec.get('initial_state')
        self.hang_rows = 0
        self.witness = None
        self.last_reason = 'no records'
        self.states, self.water_states, self.target_states = set(), set(), set()
        self.target_ticks = 0
        self.target_health_min = None
        self.target_health_max = None
        self.closest_door = None

    def summary(self):
        return dict(states=sorted(self.states), water=sorted(self.water_states),
                    target_states=sorted(self.target_states), target_ticks=self.target_ticks,
                    target_health_min=self.target_health_min, target_health_max=self.target_health_max,
                    saw_air=self.saw_air, saw_pickup=self.saw_pickup, saw_switch=self.saw_switch,
                    switch_changed=self.switch_changed, linked_door_motion=self.target_motion,
                    target_alive=self.target_alive, closest_door=self.closest_door)

    def observe(self, row):
        lara = row.get('lara')
        if self.first is None:
            self.first = {'pickups': row['save']['pickups'], 'kills': row['save']['kills'],
                          'counts': list(row['inventory']['counts'])}
        target = next((item for item in row['active']
                       if item['index'] == self.spec.get('target_index')), None)
        if lara is not None:
            self.states.add(lara['state'])
            self.water_states.add(lara['water'])
            self.saw_air |= lara['state'] in AIR and bool(lara['flags'] & 2)
            self.saw_pickup |= lara['state'] == 39
            self.saw_switch |= lara['state'] in (40, 41)
            self.hang_rows = self.hang_rows + 1 if lara['state'] in HANGS else 0
        if target is not None:
            self.target_ticks += 1
            self.target_states.add(target['state'])
            self.target_health_min = target['health'] if self.target_health_min is None else min(self.target_health_min, target['health'])
            self.target_health_max = target['health'] if self.target_health_max is None else max(self.target_health_max, target['health'])
            if 57 <= target['type'] <= 64:
                animation = self.animation(target['anim'])
                if animation and animation['next_animation'] != target['anim']:
                    midpoint = animation['frame_start'] + (animation['frame_end'] - animation['frame_start']) // 2
                    distance = abs(target['frame'] - midpoint)
                    if self.closest_door is None or distance < self.closest_door['distance']:
                        self.closest_door = dict(tick=row['tick'], animation=target['anim'],
                                                 frame=target['frame'], midpoint=midpoint, distance=distance)
            if self.target_initial_state is None:
                self.target_initial_state = target['state']
            self.switch_changed |= target['state'] != self.target_initial_state
            self.target_alive |= target['health'] > 0
        doors = set(self.spec.get('door_indices', []))
        for door in row['active']:
            if door['index'] in doors and 57 <= door['type'] <= 64:
                animation = self.animation(door['anim'])
                if animation and animation['frame_start'] < door['frame'] < animation['frame_end'] \
                        and animation['next_animation'] != door['anim']:
                    self.target_motion = True
        ok, reason = self.predicate(row, lara, target)
        self.last_reason = reason
        minimum = self.spec.get('min_save', 10)
        maximum = self.spec.get('max_save', 520)
        fixed = self.spec.get('save_tick')
        eligible = minimum <= row['tick'] <= maximum and (fixed is None or row['tick'] == fixed)
        if ok and eligible and self.witness is None:
            self.witness = excerpt(row, target)
        self.previous = None if lara is None else {
            'state': lara['state'], 'pos': list(lara['pos']), 'flags': lara['flags']}
        if target is not None:
            self.previous_target = {'pos': list(target['pos']), 'health': target['health'],
                                    'frame': target['frame'], 'state': target['state']}
        return ok and eligible

    def animation(self, index):
        return self.animations[index] if 0 <= index < len(self.animations) else None

    def predicate(self, row, lara, target):
        category = self.spec['category']
        if row.get('level') != self.spec['level_id']:
            return False, 'save boundary is in a different live source level'
        if lara is None or lara['health'] <= 0:
            return False, 'no living Lara'
        if self.spec.get('target_type') is not None and target is not None \
                and target['type'] != self.spec['target_type']:
            return False, 'target actor type changed'
        if category == 'midjump':
            ok = lara['state'] in JUMPS and bool(lara['flags'] & 2) \
                and -70 <= lara['vSpeed'] <= 70 and lara['water'] == 0
            return ok, 'requires interior airborne jump, gravity and actual vertical speed'
        if category == 'hanging':
            previous = self.previous
            ok = self.saw_air and self.hang_rows >= 3 and lara['state'] in HANGS \
                and not lara['flags'] & 2 and lara['vSpeed'] == 0 \
                and previous is not None and previous['pos'][1] == lara['pos'][1]
            return ok, 'requires actual air-to-grab, hanging state and stable ledge height'
        if category == 'water':
            ok = lara['water'] in (2, 3) and lara['state'] in (13, 17, 18, 33, 34, 35, 47, 48, 49)
            if lara['water'] == 3:
                ok &= lara['room'] in self.spec['water_rooms']
            return bool(ok), 'requires real surface/underwater state and water metadata'
        if category == 'drawn':
            weapon = lara['weapon']
            owned_slot = {0: 11, 1: 15, 2: 17, 3: 13}.get(weapon)
            ok = lara['weaponState'] == 4 and owned_slot is not None \
                and row['inventory']['counts'][owned_slot] == 1 and row['inventory']['state'] == 0
            return ok, 'requires ready owned weapon, not a draw button or menu label'
        if category in ('enemy-active', 'enemy-dead'):
            if target is None or target['type'] not in ENEMIES:
                return False, 'indexed enemy not present in actual active records'
            if category == 'enemy-active':
                return bool(target['flags'] & 4 and target['ai'] is not None and target['health'] > 0), \
                    'requires active AI and positive target health'
            death_state = target['state'] == DEATH_STATE.get(target['type']) \
                and target['health'] != -16384
            if target['type'] in (20, 21, 22):
                # enemy.h:52-73,2512-2514: unused upper bits stay set.
                mask = self.spec.get('model_mask')
                death_state = mask is not None and bool(target['turnSpeed'] & 8) \
                    and target['visibleMask'] & mask == 0
            elif target['type'] == 33:
                # enemy.h:3884-3917,4103-4154: negative faint health is not death.
                death_state = target['state'] == 9 and target['health'] != -8192 \
                    and not target['turnSpeed'] & 16384
            elif target['type'] == 28:
                # enemy.h:3257-3279,3294-3314: retreat is not the kill path.
                death_state &= bool(target['flags'] & 256)
            elif target['type'] == 34:
                # enemy.h:4212-4225,4254-4301: no STATE_DEATH enum.
                base = self.spec.get('model_animation_base')
                death_state = base is not None and target['health'] != -16384 \
                    and target['anim'] == base + 13
            ok = self.target_alive and target['health'] <= 0 and death_state
            return ok, 'requires observed positive-to-dead indexed actor and exact source-family death predicate'
        if category == 'door-half':
            if target is None or not 57 <= target['type'] <= 64:
                return False, 'indexed regular door not active'
            animation = self.animation(target['anim'])
            if not animation:
                return False, 'source door animation outside immutable table'
            lo, hi = animation['frame_start'], animation['frame_end']
            midpoint = lo + (hi - lo) // 2
            ok = hi - lo >= 2 and animation['next_animation'] != target['anim'] \
                and lo < target['frame'] < hi and target['frame'] == midpoint
            return ok, 'requires actual motion animation at exact source midpoint, not state!=goal'
        if category == 'boulder-rolling':
            previous = self.previous_target
            ok = target is not None and target['type'] == 38 and target['state'] == 1 \
                and not target['flags'] & 64 and target['hSpeed'] != 0 and target['vSpeed'] == 0 \
                and previous is not None and target['pos'][1] == previous['pos'][1] \
                and (target['pos'][0], target['pos'][2]) != (previous['pos'][0], previous['pos'][2])
            return bool(ok), 'requires grounded source rolling motion; roomFloor is not a fresh witness'
        free = lara['state'] == 2 and lara['weaponState'] == 0 and row['inventory']['state'] == 0
        if category == 'after-pickup':
            slot = self.spec.get('inventory_slot')
            if slot is None:
                return False, 'no source slot mapping for the authored pickup'
            collected = ((row['save']['pickups'] - self.first['pickups']) & 255) > 0
            changed = row['inventory']['counts'][slot] != self.first['counts'][slot]
            return bool(self.saw_pickup and collected and changed and free), \
                'requires actual pickup animation, counter/typed slot change and settled free hands'
        if category == 'after-switch':
            ok = self.saw_switch and self.switch_changed and free
            if self.spec.get('door_indices'):
                ok &= self.target_motion
            return bool(ok), 'requires real switch interaction/change and linked target motion, then settled Lara'
        raise ValueError('unhandled save category')
