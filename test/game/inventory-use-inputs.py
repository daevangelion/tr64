#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate 600-tick real-pickup inventory-use windows; no injected loadout.
Run with PYTHONPATH=tools/cook. With --executable, reuse random-inputs.py's
streamed comparison and verify that the requested use/firing actually occurs.
The binary must be bound to the supplied level. Item positions and
sector bounds come from the player's PC level, never a stored asset fixture.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import src_pc

CASES = {
    'shotgun': (85, 3),
    'magnums': (86, 1),
    'uzis': (87, 2),
    'big-medikit-after-damage': (94, None),
    'small-medikit-after-damage': (93, None),
    'big-medikit-full-health': (94, None),
    'small-medikit-full-health': (93, None),
}


def candidate(level, item_type, fall_height):
    choices = []
    for index, item in enumerate(level.data['items']):
        if item['type'] != item_type or item['flags'] & 0x100:
            continue
        room = level.rooms[item['room']]
        sx = (item['x'] - room.info['x']) >> 10
        sz = (item['z'] - room.info['z']) >> 10
        sector = room.sectors[sx * room.z_sectors + sz]
        if sector['room_below'] != 255:
            continue
        ceiling, floor = sector['ceiling'] * 256, sector['floor'] * 256
        if floor != item['y'] or floor - ceiling < fall_height + 1024:
            continue
        choices.append((floor - ceiling, index, item, ceiling))
    if not choices:
        raise ValueError('level has no qualifying pickup/fall sector for this case')
    return max(choices, key=lambda row: row[0]) if fall_height else choices[0]


def inputs(weapon):
    masks = []
    def hold(mask, ticks):
        masks.extend([mask] * ticks)
    if weapon is None:
        hold(0, 100)
        hold(128, 120)  # pick up after landing
        hold(2048, 1)
        hold(0, 30)
        hold(4, 1)  # main ring: compass -> last owned medikit
        hold(0, 14)
        hold(128, 1)
    else:
        hold(128, 120)
        hold(2048, 1)
        hold(0, 30)
        for _ in range(2):  # compass -> pistols -> newly picked up weapon
            hold(2, 1)
            hold(0, 14)
        hold(128, 1)
        hold(0, 119)
        hold(128, 60)
        hold(2048, 1)
        hold(0, 30)
        hold(2, 1)  # compass -> pistols
        hold(0, 14)
        hold(128, 1)
        hold(0, 113)
        hold(128, 60)
    hold(0, 600 - len(masks))
    return masks

class UseObservation:
    """Bounded observations from records already proven equal by the comparator."""
    def __init__(self, case, weapon):
        self.case = case
        self.weapon = weapon
        self.slot = {3: 13, 1: 15, 2: 17}.get(weapon, 19 if case.startswith('big-') else 20)
        self.first = None
        self.last = None
        self.min_health = 1000
        self.peak_count = 0
        self.target_ready = False
        self.target_ammo_used = 0
        self.pistols_ready_after_target = False
        self.pistol_ammo_used_after_target = 0
        self.heals = 0
        self.heal_before = self.heal_after = None

    def __call__(self, row):
        lara, save = row['lara'], row['save']
        current = dict(health=lara['health'], medi_used=save['mediUsed'],
            ammo_used=save['ammoUsed'], weapon=lara['weapon'],
            inventory_state=row['inventory']['state'],
            count=row['inventory']['counts'][self.slot])
        self.min_health = min(self.min_health, current['health'])
        self.peak_count = max(self.peak_count, current['count'])
        if self.first is None:
            self.first = current
        if self.weapon is not None:
            if lara['weapon'] == self.weapon and lara['weaponState'] == 4:
                self.target_ready = True
            if self.target_ready and lara['weapon'] == 0 and lara['weaponState'] == 4:
                self.pistols_ready_after_target = True
        if self.last is not None:
            ammo_delta = (current['ammo_used'] - self.last['ammo_used']) & 65535
            if self.weapon is not None and lara['weapon'] == self.weapon:
                self.target_ammo_used += ammo_delta
            if self.target_ready and lara['weapon'] == 0:
                self.pistol_ammo_used_after_target += ammo_delta
            if current['health'] > self.last['health'] and current['medi_used'] != self.last['medi_used']:
                self.heals += 1
                self.heal_before, self.heal_after = self.last['health'], current['health']
        self.last = current

    def verify(self):
        def require(condition, message):
            if not condition:
                raise ValueError(self.case + ': ' + message)
        require(self.first is not None and self.last is not None, 'no observed records')
        require(self.peak_count == 1, 'requested pickup was not acquired exactly once')
        require(self.last['inventory_state'] == 0, 'inventory did not close')
        if self.weapon is not None:
            require(self.target_ready, 'picked-up weapon never became ready')
            require(self.target_ammo_used > 0, 'picked-up weapon never fired')
            require(self.pistols_ready_after_target and self.pistol_ammo_used_after_target > 0,
                'pistols were not selected and fired after the new weapon')
            require(self.last['weapon'] == 0, 'final weapon is not pistols')
        elif self.case.endswith('-after-damage'):
            large = self.case.startswith('big-')
            units, recovery = (2, 1000) if large else (1, 500)
            require(0 < self.min_health < 1000, 'no surviving damage before using the medikit')
            require(self.heals == 1, 'requested medikit did not heal exactly once')
            require(self.heal_after == min(1000, self.heal_before + recovery),
                'medikit recovery does not match its source size')
            require(self.last['health'] == self.heal_after, 'unexpected damage after healing')
            require(((self.last['medi_used'] - self.first['medi_used']) & 65535) == units,
                'medikit half-unit counter does not match its source size')
            require(self.last['count'] == 0, 'medikit was not consumed')
            if not large:
                require(self.heal_after < 1000, 'small-medikit scene did not distinguish partial healing')
        else:
            require(self.min_health == 1000 and self.last['health'] == 1000,
                'full-health rejection scenario was damaged')
            require(self.heals == 0 and self.last['medi_used'] == self.first['medi_used'],
                'medikit was used at full health')
            require(self.last['count'] == 1, 'full-health medikit was consumed')
        return dict(min_health=self.min_health, peak_count=self.peak_count,
            heals=self.heals, heal_before=self.heal_before, heal_after=self.heal_after,
            target_ammo_used=self.target_ammo_used,
            pistol_ammo_used_after_target=self.pistol_ammo_used_after_target, final=self.last)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--level', type=Path, required=True)
    parser.add_argument('--case', choices=CASES, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--oracle', type=Path,
        help='immutable schema-3 oracle; required with --executable')
    args = parser.parse_args()
    if args.executable and args.oracle is None:
        parser.error('--oracle is required with --executable')
    level = src_pc.load(args.level)
    item_type, weapon = CASES[args.case]
    fall_height = 4096 if args.case.endswith('-after-damage') else 0
    _, index, item, _ = candidate(level, item_type, fall_height)
    y = item['y'] - fall_height
    text = f"start {item['room']} {item['x']} {y} {item['z']} 0\n"
    text += '\n'.join(map(str, inputs(weapon))) + '\n'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    result = dict(case=args.case, level=level.name, source_sha256=level.sha256,
        pickup_index=index, pickup_type=item_type, weapon=weapon, ticks=600,
        input_sha256=hashlib.sha256(text.encode()).hexdigest(), path=str(args.out))
    if args.executable:
        executable_sha = hashlib.sha256(args.executable.read_bytes()).hexdigest()
        oracle_sha = hashlib.sha256(args.oracle.read_bytes()).hexdigest()
        spec = importlib.util.spec_from_file_location('tr64_random_inputs',
            Path(__file__).with_name('random-inputs.py'))
        comparison = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(comparison)
        observation = UseObservation(args.case, weapon)
        status, records, reason = comparison.compare_case(args.executable, args.oracle,
            args.level, args.out, args.case, observe=observation, schema=3)
        if executable_sha != hashlib.sha256(args.executable.read_bytes()).hexdigest() or \
                oracle_sha != hashlib.sha256(args.oracle.read_bytes()).hexdigest():
            raise ValueError('comparison executable changed during the directed scene')
        if status != 'equal' or records != 601:
            raise ValueError(f'{args.case}: comparison={status} records={records} reason={reason}')
        result.update(status=status, records=records, schema=3, observations=observation.verify(),
            executable_sha256=executable_sha, oracle_sha256=oracle_sha)
    args.out.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
