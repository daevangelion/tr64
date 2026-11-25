#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Water admission and unsupported-path regression controls."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile


def load_item_controls():
    spec = importlib.util.spec_from_file_location('item_controls', Path(__file__).with_name('tr2-item-actions-test.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cook_pack(support, tables):
    return support.n64_pack.build({'wall': support.wall_sections(tables)}, games={'wall': 2})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('pack', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('--contact-host', type=Path, help='host linked with the wet-contact control wrapper')
    parser.add_argument('--pre-water-host', type=Path, help='historical pre-water executable for admission control')
    args = parser.parse_args()
    if not (tmp := os.environ.get('TMPDIR')):
        parser.error('set TMPDIR to assigned scratch')
    with tempfile.TemporaryDirectory(prefix='tr2-water-review-', dir=tmp) as td:
        control(args, Path(td))


def control(args, root):
    support = load_item_controls()
    base = support.n64_pack.inspect(args.pack.read_bytes())['wall']
    rooms, items, sectors, clips = (support.rows(base, k) for k in ('rooms','items','sectors','animations'))
    swim, wade = [(args.fixtures / route / 'input.txt').read_text().split() for route in ('pool-swim','pool-wade')]
    actions, trace, candidate = (root / path for path in ('inputs.txt','result.jsonl','changed.pack'))
    failures = []

    def check(ok, message):
        if not ok:
            failures.append(message)
        else:
            print('PASS:', message, flush=True)

    def run(values, source=None, host=None, env=None):
        actions.write_text(' '.join(map(str, values)) + '\n', encoding='ascii')
        trace.unlink(missing_ok=True)
        p = subprocess.run(['rtk',str(host or args.host),'--game-tr2',str(source or args.pack),str(actions),str(trace)],
                           stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=25,env=env)
        wire = [json.loads(x) for x in trace.read_bytes().splitlines()] if trace.exists() else []
        return p, wire

    def refuse(reason, values, minimum, source=None, host=None, env=None):
        p, rows = run(values, source, host=host, env=env)
        check(p.returncode and len(rows) >= minimum and all('tick' in row for row in rows) and
              f'unsupported water movement: {reason}' in p.stdout,
              f'{reason}: named refusal without a footer (exit={p.returncode}, rows={len(rows)})')

    # Add a sink to the authored sector reached on the first surface tick.
    def sector_at(room_id, x, z):
        room = rooms[room_id]
        ix, iz = (x-room['x']) >> 10, (z-room['z']) >> 10
        return room['sector_start'] + ix * room['z_sectors'] + iz, ix, iz

    with (args.fixtures/'pool-swim'/'trace.jsonl').open() as rows:
        swimmer = next(row['lara'] for line in rows if (row := json.loads(line))['tick'] == 584)
    x, _, z = swimmer['pos']
    pool_sector, _, _ = sector_at(swimmer['room'], x, z)
    candidate.write_bytes(support.spawn_program(base, pool_sector, [0x8004, 0x3e00, 0x8800]))
    refuse('current sink', swim, 460, source=candidate)

    if args.contact_host:
        for contact, expected in (('step-up', (55, 830, 65)), ('splat', (11, 185, 12))):
            p, rows = run(swim[:502], host=args.contact_host,
                          env=os.environ | {'TR64_WATER_TEST_CONTACT': contact})
            pose = rows[501]['lara'] if len(rows) > 501 else {}
            check(not p.returncode and all(k in rows[-1] for k in ('recordsHash', 'hash'))
                  and tuple(pose.get(k) for k in ('anim', 'frame', 'state')) == expected,
                  f'wade {contact}: admitted and sealed the expected contact pose {expected}')

    spawn = next(item for item in items if item['type'] == 0)
    room = spawn['room']
    tile, ix, iz = sector_at(room, spawn['x'], spawn['z'])
    target = rooms[0]
    assert rooms[room]['flags'] & 1 == target['flags'] & 1 == 0
    assert not target['x'] <= spawn['x'] < target['x'] + 1024 * target['x_sectors']
    for direction in ('above','below'):
        tables = support.replace_row(base, 'sectors', tile, sectors[tile], **{f'room_{direction}': 0})
        candidate.write_bytes(cook_pack(support, tables))
        if args.pre_water_host:
            p, rows = run([0], candidate, host=args.pre_water_host)
            check(p.returncode == 0 and any(row.get('tick') == 1 for row in rows),
                  f'{direction}: disjoint control still admitted by the pre-water executable')
        p, rows = run([0], candidate)
        detail = f'water vertical link outside target grid: room={room} sector={ix},{iz} {direction}=0'
        check(p.returncode and not rows and detail in p.stdout,
              f'{direction}: disjoint control refused by name before tick 0')

    # Effect 0 in a dive clip is outside the retired pose whitelist.
    dive = clips[119]
    # The clip's own sound cues stay (sound_commands): a sound draws the presentation random
    # stream, so a pack without them differs from the first cue's tick, four ticks before the effect.
    sound, sounds = support.sound_commands(base, 119)
    candidate.write_bytes(cook_pack(support, support.replace_commands(
        base, 119, dive, *sound, 6, dive['frame_start'] + 7, 0, count=sounds + 1)))
    p, original = run(swim[:616])
    q, turned = run(swim[:616], candidate)
    if p.returncode or q.returncode or len(original) != 618 or len(turned) != 618:
        failures.append(f'effect 0 did not run through its synthetic cue: {q.stdout[-500:]}')
    else:
        check(turned[:616] == original[:616], 'effect route unchanged before the cue')
        a, b = original[616]['lara'], turned[616]['lara']
        pitch, yaw, bank = a['rot']
        expected = [-pitch, (yaw & 65535) - 32768, bank]
        check(pitch and a['anim'] == b['anim'] == 119 and b['rot'] == expected,
              f'effect 0 on dive 119: expected {expected}, got {b["rot"]}')

    if failures:
        raise RuntimeError('\n'.join(failures))


if __name__ == '__main__':
    main()
