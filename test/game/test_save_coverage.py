#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Synthetic predicate falsifiers, not evidence of real gameplay coverage."""
import sys
sys.dont_write_bytecode = True
import unittest
from save_coverage import Coverage


def player():
    return dict(index=0, room=0, pos=[0, 0, 0], rot=[0, 0, 0], state=2, anim=0,
                frame=0, flags=0, vSpeed=0, hSpeed=0, health=1000, water=0,
                weapon=0, weaponState=0)


def actor(kind=7):
    return dict(index=1, type=kind, room=0, pos=[0, 0, 0], state=0, goal=0,
                anim=0, frame=0, flags=4, vSpeed=0, hSpeed=0, health=100,
                turnSpeed=0, visibleMask=0xffffffff, ai={'slot': 0})


def row(tick=0):
    return dict(tick=tick, hash='00000000', level=2, lara=player(), active=[],
                save=dict(time=tick, pickups=0, kills=0, flipped=0),
                inventory=dict(counts=[0] * 26, state=0))


def observer(category, **extra):
    spec = dict(category=category, level_id=2, min_save=1, max_save=520,
                water_rooms=[0], target_index=1)
    spec.update(extra)
    return Coverage(spec, [dict(frame_start=0, frame_end=10, next_animation=1),
                           dict(frame_start=0, frame_end=10, next_animation=1)])


class Predicates(unittest.TestCase):
    def test_idle_cannot_be_renamed_midjump(self):
        check = observer('midjump')
        for tick in range(30):
            self.assertFalse(check.observe(row(tick)))
        self.assertIsNone(check.witness)

    def test_jump_requires_actual_gravity_state_and_speed(self):
        check = observer('midjump')
        sample = row(12)
        sample['lara'].update(state=3, flags=2, vSpeed=-4, hSpeed=50)
        self.assertTrue(check.observe(sample))
        sample['tick'] = 13
        sample['lara']['flags'] = 0
        self.assertFalse(check.observe(sample))
        sample['lara']['flags'] = 2
        sample['lara']['vSpeed'] = -94
        self.assertFalse(check.observe(sample))

    def test_hanging_requires_air_to_grab_and_stable_rows(self):
        check = observer('hanging')
        sample = row(0)
        sample['lara'].update(state=11, flags=2)
        check.observe(sample)
        for tick in (1, 2, 3):
            sample = row(tick)
            sample['lara']['state'] = 10
            result = check.observe(sample)
            self.assertEqual(result, tick == 3)
        fresh = observer('hanging')
        self.assertFalse(fresh.observe(sample))

    def test_wrong_live_level_cannot_fill_another_level_cell(self):
        check = observer('drawn')
        sample = row(10)
        sample['level'] = 3
        sample['lara']['weaponState'] = 4
        sample['inventory']['counts'][11] = 1
        self.assertFalse(check.observe(sample))

    def test_owned_ready_weapon_not_input_button(self):
        check = observer('drawn')
        sample = row(10)
        sample['lara']['weaponState'] = 4
        self.assertFalse(check.observe(sample))
        sample['inventory']['counts'][11] = 1
        self.assertTrue(check.observe(sample))

    def test_door_midpoint_does_not_use_state_goal_difference(self):
        check = observer('door-half', target_type=60)
        sample = row(10)
        door = actor(60)
        door.update(state=1, goal=1, frame=5)
        sample['active'] = [door]
        self.assertTrue(check.observe(sample))
        door['anim'] = 1  # An idle loop at the same numeric frame is not motion.
        self.assertFalse(check.observe(sample))

    def test_boulder_uses_motion_not_stale_room_floor(self):
        check = observer('boulder-rolling', target_type=38)
        sample = row(10)
        rock = actor(38)
        rock.update(state=1, hSpeed=16, health=-16384, roomFloor=9999)
        sample['active'] = [rock]
        self.assertFalse(check.observe(sample))
        sample['tick'] += 1
        rock['pos'][0] += 16
        self.assertTrue(check.observe(sample))
        sample['tick'] += 1
        rock['pos'][0] += 16
        rock['pos'][1] += 1
        self.assertFalse(check.observe(sample))

    def test_natla_faint_is_not_death(self):
        check = observer('enemy-dead', death_states=[5])
        sample = row(1)
        target = actor(33)
        sample['active'] = [target]
        check.observe(sample)
        sample['tick'] = 2
        sample['save']['kills'] = 1
        target.update(health=-8192, state=5)
        self.assertFalse(check.observe(sample))
        sample['tick'] = 3
        target.update(health=0, state=9)
        self.assertTrue(check.observe(sample))

    def test_mutant_explosion_not_unused_classical_death_state(self):
        check = observer('enemy-dead', death_states=[5], model_mask=0xffff)
        sample = row(1)
        target = actor(20)
        sample['active'] = [target]
        check.observe(sample)
        sample['tick'] = 2
        target.update(health=0, state=3, turnSpeed=8, visibleMask=0xffff0001)
        self.assertFalse(check.observe(sample))  # Partial explosion is not complete.
        target['visibleMask'] = 0xffff0000  # Source preserves unused high bits.
        self.assertTrue(check.observe(sample))
        target['turnSpeed'] = 0
        self.assertFalse(check.observe(sample))

    def test_pickup_needs_causal_animation_counter_and_slot(self):
        check = observer('after-pickup', inventory_slot=20)
        check.observe(row(0))
        sample = row(1)
        sample['lara']['state'] = 39
        check.observe(sample)
        sample = row(2)
        sample['save']['pickups'] = 1
        self.assertFalse(check.observe(sample))
        sample['inventory']['counts'][20] = 1
        self.assertTrue(check.observe(sample))

    def test_ai_allocation_is_not_positive_health_enemy(self):
        check = observer('enemy-active')
        sample = row(10)
        target = actor()
        sample['active'] = [target]
        target['health'] = 0
        self.assertFalse(check.observe(sample))
        target['health'] = 1
        self.assertTrue(check.observe(sample))
        target['ai'] = None
        self.assertFalse(check.observe(sample))

    def test_pierre_retreat_not_death_without_once_flag(self):
        check = observer('enemy-dead')
        sample = row(1)
        target = actor(28)
        sample['active'] = [target]
        check.observe(sample)
        sample['tick'] = 2
        target.update(health=0, state=5)
        self.assertFalse(check.observe(sample))
        target['flags'] |= 256
        self.assertTrue(check.observe(sample))

    def test_adam_uses_real_model_relative_death_animation(self):
        check = observer('enemy-dead', model_animation_base=100)
        sample = row(1)
        target = actor(34)
        sample['active'] = [target]
        check.observe(sample)
        sample['tick'] = 2
        target.update(health=0, anim=112)
        self.assertFalse(check.observe(sample))
        target['anim'] = 113
        self.assertTrue(check.observe(sample))
        target['health'] = -16384
        self.assertFalse(check.observe(sample))

    def test_raptor_state_zero_needs_actual_lethal_health(self):
        check = observer('enemy-dead')
        sample = row(1)
        target = actor(19)
        sample['active'] = [target]
        self.assertFalse(check.observe(sample))
        sample['tick'] = 2
        target['health'] = 0
        self.assertTrue(check.observe(sample))

    def test_mummy_removed_sentinel_not_collapse_witness(self):
        check = observer('enemy-dead')
        sample = row(1)
        target = actor(24)
        sample['active'] = [target]
        check.observe(sample)
        sample['tick'] = 2
        target.update(state=2, health=-16384)
        self.assertFalse(check.observe(sample))
        target['health'] = 0
        self.assertTrue(check.observe(sample))

    def test_geometry_probe_miss_never_makes_hang_not_applicable(self):
        from save_scenes import na_reason
        counts = dict(enemy=0, door=0, boulder=0, switch=0, pickup=0, water=1, weapon_pickup=0)
        self.assertIsNone(na_reason('gym', 'hanging', counts))
        self.assertIsNone(na_reason('gym', 'midjump', counts))
        self.assertIsNotNone(na_reason('gym', 'drawn', counts))

    def test_special_collectibles_and_encounter_pods_prevent_false_na(self):
        from types import SimpleNamespace
        from save_scenes import static_counts, na_reason
        level = SimpleNamespace(data={'items': [{'type': 143}, {'type': 144}, {'type': 161}]}, rooms=[])
        counts = static_counts(level)
        self.assertEqual(counts['pickup'], 2)
        self.assertEqual(counts['placed_enemy'], 0)
        self.assertEqual(counts['enemy_spawner'], 1)
        self.assertIsNone(na_reason('levelX', 'after-pickup', counts))
        self.assertIsNone(na_reason('levelX', 'enemy-dead', counts))

    def test_qualified_artifact_cannot_restore_at_same_boundary(self):
        from save_coverage import validate_cell_metadata
        original = dict(input='real.masks', input_sha256='a' * 64, level_id=2,
                        min_save=10, max_save=520)
        selected = dict(original, level='level1', category='midjump')
        cell = dict(level='level1', category='midjump', status='source-qualified',
                    candidates=[original], candidate=selected,
                    save_tick=20, load_tick=50, witness={'tick': 20})
        validate_cell_metadata(cell, 2)
        for invalid in (20, 19, 601, True):
            cell['load_tick'] = invalid
            with self.assertRaises(ValueError):
                validate_cell_metadata(cell, 2)
        cell['load_tick'] = 50
        cell['candidate']['category'] = 'drawn'
        with self.assertRaises(ValueError):
            validate_cell_metadata(cell, 2)


if __name__ == '__main__':
    unittest.main()
