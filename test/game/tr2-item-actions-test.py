#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test live trigger masks, active order and flip words on native host gameplay."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import n64_core
import n64_pack

TABLES = {table.name: (i, table) for i, table in enumerate(n64_core.TABLES, 1)}
HEADER = n64_core.PREFIX.size
END = 0x8000
MASK = 0x3e00
ONCE = 0x100
REVERSE = 0x4000

def replace_row(tables, name, index, original, **changes):
    ident, layout = TABLES[name]
    data = bytearray(tables[ident])
    row = struct.Struct('>' + layout.fmt)
    values = original | changes
    row.pack_into(data, HEADER + index * row.size, *(values[k] for k in layout.fields))
    return tables | {ident: bytes(data)}


def replace_commands(tables, clip, original, *words, count=1):
    """Replace the clip's command list by WORDS, COUNT commands long."""
    ident, _ = TABLES['commands']
    data = bytearray(tables[ident])
    offset = (len(data) - HEADER) // 2
    data.extend(struct.pack('>' + 'H' * len(words), *words))
    struct.pack_into('>I', data, 8, offset + len(words))
    struct.pack_into('>I', data, 24, len(data))
    return replace_row(tables | {ident: bytes(data)}, 'animations', clip, original,
                       command_count=count, command_offset=offset)


COMMAND_ARGS = {0: 0, 1: 3, 2: 2, 3: 0, 4: 0, 5: 2, 6: 2}  # words after the opcode; 5 is sound, 6 is effect


def sound_commands(tables, clip):
    """The sound commands (opcode 5: frame, sound) of a clip, copied from its own list.

    Returns (words, count). A synthetic command list that replaces a clip's own must keep
    them: a sound draws the presentation random stream, so a record without it differs
    from the cue's tick on, whatever the synthetic command does."""
    ident, _ = TABLES['commands']
    anim = rows(tables, 'animations')[clip]
    data = tables[ident]
    at = HEADER + 2 * anim['command_offset']
    words, count = [], 0
    for _ in range(anim['command_count']):
        opcode, = struct.unpack_from('>H', data, at)
        size = 1 + COMMAND_ARGS[opcode]
        if opcode == 5:
            words += struct.unpack_from('>' + 'H' * size, data, at)
            count += 1
        at += 2 * size
    return words, count



def rows(tables, name):
    ident, table = TABLES[name]
    return [dict(zip(table.fields, row)) for row in
            struct.iter_unpack('>' + table.fmt, tables[ident][HEADER:])]


SOUND = 0x40000
SOUND_BLOCK = 4096


def sound_section(data):
    """Re-create the cooked sound section's partition from its own SN64 table.

    A level's sound section is never optional on a cart or in the host (the
    sound system must be bound before a level's audio starts), so a variant
    pack keeps it unchanged. n64_snd.cook cuts after the metadata prefix, at
    every sample start, and every 4096 bytes inside a sample; the sample
    table names each start and the next sample's start is its end.
    tr2-sound-partition-test.py compares the result with the cooked pack's own
    entry table, so a change of the cooker's rule is caught.
    """
    count, = struct.unpack_from('>I', data, 16)
    metadata, = struct.unpack_from('>I', data, 12)
    starts = [struct.unpack_from('>I', data, 64 + i * 32)[0] for i in range(count)] + [len(data)]
    cuts = [metadata]
    for begin, end in zip(starts, starts[1:]):
        cuts.extend(range(begin + SOUND_BLOCK, end, SOUND_BLOCK))
        cuts.append(end)
    return n64_pack.Section(SOUND, data, header_bytes=metadata, chunk_bytes=SOUND_BLOCK,
                            compression='none', boundaries=tuple(sorted(set(cuts[:-1]))))


def wall_sections(tables):
    """Sections of a variant Great Wall pack: Core tables, texture and sound."""
    sections = [n64_pack.Section(k, data, header_bytes=32 if k < 0x10000 else 0)
                for k, data in tables.items() if k <= 0x10000]
    if SOUND in tables:
        sections.append(sound_section(tables[SOUND]))
    return sections


def spawn_program(tables, sector, words):
    floor, _ = TABLES['floor_data']
    sectors, layout = TABLES['sectors']
    program = bytearray(tables[floor])
    refs = bytearray(tables[sectors])
    count = (len(program) - HEADER) // 2
    struct.pack_into('>H', refs, HEADER + sector * struct.calcsize('>' + layout.fmt), count)
    program.extend(struct.pack('>' + 'H' * len(words), *words))
    struct.pack_into('>I', program, 8, count + len(words))
    struct.pack_into('>I', program, 24, len(program))
    changed = {floor: bytes(program), sectors: bytes(refs)}
    blob = n64_pack.build({'wall': wall_sections(tables | changed)}, games={'wall': 2})
    n64_pack.inspect(blob)
    return blob


def flip_digest(slot, flags):
    words = [0] * 11
    words[1 + slot] = flags
    h = 2166136261
    for byte in struct.pack('<11I', *words):
        h = ((h ^ byte) * 16777619) & 0xffffffff
    return h


def expect(ok, message):
    if not ok:
        raise RuntimeError(message)


def control(host, pack, scratch):
    base = n64_pack.inspect(pack.read_bytes())['wall']
    placements = rows(base, 'items')
    lara_index = next(i for i, item in enumerate(placements) if item['type'] == 0)
    lara = placements[lara_index]
    camp = next(i for i, item in enumerate(placements) if item['type'] == 253)
    reverse = next(i for i, item in enumerate(placements) if item['type'] == 107)
    heli = next(i for i, item in enumerate(placements) if item['type'] == 259)
    tiger = next(i for i, item in enumerate(placements) if item['type'] == 39)
    room = rows(base, 'rooms')[lara['room']]
    sector = (room['sector_start'] + ((lara['x'] - room['x']) >> 10) * room['z_sectors'] +
              ((lara['z'] - room['z']) >> 10))
    with tempfile.TemporaryDirectory(prefix='tr2-item-actions-', dir=scratch) as td:
        root = Path(td)
        actions = root / 'input.txt'
        actions.write_text('0 0 0\n', encoding='ascii')
        candidate = root / 'case.pack'
        trace = root / 'case.jsonl'

        def probe(trigger, data, *targets, tables=base):
            trace.unlink(missing_ok=True)
            words = [END | (trigger << 8) | 4, data, *targets[:-1], targets[-1] | END]
            candidate.write_bytes(spawn_program(tables, sector, words))
            proc = subprocess.run(['rtk', str(host), '--game-tr2', str(candidate), str(actions), str(trace)],
                                  capture_output=True, text=True, timeout=10)
            return proc

        def run(trigger, data, *targets, tables=base):
            proc = probe(trigger, data, *targets, tables=tables)
            expect(proc.returncode == 0, proc.stdout + proc.stderr)
            return [json.loads(line) for line in trace.read_text().splitlines()[:-1]]

        def door(record):
            return next(item for item in record['active'] if item['index'] == reverse)

        # Tick one is airborne: a pad cannot act there, but an ordinary action can.
        pad = run(1, MASK, heli)
        expect(all([i['index'] for i in row['active']] == [camp, reverse] for row in pad),
               'an airborne pad activated the helicopter')
        wake = run(0, MASK | ONCE | 1, heli, tiger, reverse)
        expect([i['index'] for i in wake[1]['active']] == [tiger, heli, camp, reverse],
               'startup or same-tick trigger list order differs')
        expect(wake[1]['active'][1]['pos'][2] == placements[heli]['z'] and
               wake[2]['active'][1]['pos'][2] == placements[heli]['z'] + 100,
               'helicopter did not defer its first update until after the wake tick')
        expect(door(wake[1])['flags'] == REVERSE | MASK | ONCE and
               door(wake[1])['timer'] == 1 and door(wake[2])['timer'] == -1 and
               door(wake[3])['timer'] == -1,
               'complete mask / one-tick timer / one-shot refresh differs')

        # Put the helicopter in a valid, distant room so it removes itself
        # on its first update, while the reverse door is still animating.
        far = placements[camp]
        distant = replace_row(base, 'items', heli, placements[heli],
                              **{key: far[key] for key in ('room', 'x', 'y', 'z')})
        retirement = run(0, MASK | ONCE, heli, tables=distant)
        expect([i['index'] for i in retirement[1]['active']] == [heli, camp, reverse] and
               [i['index'] for i in retirement[2]['active']] == [camp, reverse] and
               door(retirement[2])['frame'] == door(retirement[1])['frame'] + 1,
               'self-retirement dropped or skipped the next active items')

        bit = 1 << 9
        partial = run(0, ONCE | bit | 2, reverse, (3 << 10) | 7)
        expect(door(partial[1])['flags'] == REVERSE | bit and door(partial[1])['timer'] == 60,
               'incomplete positive mask locked the item or did not refresh its timer')
        expect(partial[0]['world'][2] == flip_digest(7, 0) and
               partial[1]['world'][2] == flip_digest(7, bit),
               'world[2] did not serialize the live partial flip slot')
        anti = run(9, ONCE | 1, reverse)
        expect(door(anti[1])['flags'] == REVERSE | ONCE and door(anti[1])['timer'] == 1,
               'zero-mask airborne antitrigger did not lock an incomplete target')

        models = rows(base, 'models')
        animations = rows(base, 'animations')
        door_model = next(i for i, m in enumerate(models) if m['type'] == 107)
        heli_anim = next(m['animation'] for m in models if m['type'] == 259)
        open_clip = 332  # WALL.TR2 door 107's authored open-state closing clip.
        opened = replace_row(base, 'models', door_model, models[door_model], animation=open_clip)
        expect(door(run(0, 0, camp, tables=opened)[0])['state'] == 1,
               'valid open-state model did not enter a trace')
        a = animations[heli_anim]
        linked_clip = next(m['animation'] for m in models if m['type'] == 253)
        linked = replace_row(base, 'animations', heli_anim, a, next_animation=linked_clip,
                             next_frame=animations[linked_clip]['frame_start'])
        # Exercise both the first clip and an end successor; neither may
        # reach an effect in the incompatible first-game namespace.
        direct = replace_commands(base, heli_anim, a, 6, a['frame_start'] + 1, 2)
        via_link = replace_commands(linked, linked_clip, animations[linked_clip],
                                    6, animations[linked_clip]['frame_start'], 2)
        door_room = rows(base, 'rooms')[placements[reverse]['room']]
        expect(run(0, bit, (3 << 10) | 9)[1]['world'][2] == flip_digest(9, bit),
               'last valid flip slot did not execute')
        checks = [
            ('open-door-yaw', (0, 0, camp), replace_row(opened, 'items', reverse, placements[reverse], angle=1),
             'game derived door rotation'),
            ('open-door-sector', (0, 0, camp), replace_row(opened, 'items', reverse, placements[reverse],
                                                            x=door_room['x'] - 1024),
             'game derived sector outside room'),
            ('helicopter-command', (0, MASK, heli), direct, f'world animation {heli_anim} uses unsupported command 6'),
            ('helicopter-linked-command', (0, MASK, heli), via_link,
             f'world animation {linked_clip} uses unsupported command 6'),
        ]
        checks.extend((f'flip-{op}-slot-10', (0, bit, (op << 10) | 10), base, 'flip-map index out of range')
                      for op in (3, 4, 5))
        failures = []
        for name, args, tables, error in checks:
            proc = probe(*args, tables=tables)
            emitted = trace.exists() and trace.stat().st_size != 0
            if proc.returncode and not emitted and error in proc.stdout + proc.stderr:
                print(f'PASS {name}: refused before tick 0')
            else:
                failures.append(f'{name}: exit={proc.returncode}, trace={bool(emitted)}')

        action = probe(0, MASK | 2, lara_index)
        lara_rows = [json.loads(line)['lara'] for line in trace.read_text().splitlines()[:-1]]
        if (action.returncode or not lara_rows or
                any((r['timer'], r['oxygen']) != (0, 1800) for r in lara_rows) or
                'tr2-inert-lara-item-actions: count=3' not in action.stdout + action.stderr):
            failures.append('Lara item action: timer/air not preserved or action not counted')
        else:
            print('PASS Lara item action: inert and counted, air preserved')

        cleanup = replace_row(base, 'items', camp, placements[camp], flags=placements[camp]['flags'] | 0x8000)
        if all(next(item for item in row['active'] if item['index'] == camp)['flags'] == REVERSE
               for row in run(0, 0, camp, tables=cleanup)):
            print('PASS cleanup: authored bit consumed and emitter still active')
        else:
            failures.append('cleanup: authored bit remained in active source flags')
        expect(not failures, '\n'.join(failures))
    print('PASS: pad veto, wake order, deferred update, continuing updates after retirement, timeout/lock, flip digest')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('wall', type=Path)
    args = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    control(args.host, args.wall, os.environ['TMPDIR'])
