#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed Venice (BOAT.TR2) schema-22 boot, parity, and refusal test."""
import argparse
import importlib.util
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
SOUND = 0x40000
SOUND_BLOCK = 4096
END = 0x8000
MASK = 0x3e00


def expect(ok, message):
    if not ok:
        raise RuntimeError(message)


def rows(tables, name):
    ident, table = TABLES[name]
    return [dict(zip(table.fields, row)) for row in
            struct.iter_unpack('>' + table.fmt, tables[ident][HEADER:])]


def replace_row(tables, name, index, original, **changes):
    ident, layout = TABLES[name]
    data = bytearray(tables[ident])
    row = struct.Struct('>' + layout.fmt)
    values = original | changes
    row.pack_into(data, HEADER + index * row.size, *(values[k] for k in layout.fields))
    return tables | {ident: bytes(data)}


def replace_commands(tables, clip, original, *words, count=1):
    ident, _ = TABLES['commands']
    data = bytearray(tables[ident])
    offset = (len(data) - HEADER) // 2
    data.extend(struct.pack('>' + 'H' * len(words), *words))
    struct.pack_into('>I', data, 8, offset + len(words))
    struct.pack_into('>I', data, 24, len(data))
    return replace_row(tables | {ident: bytes(data)}, 'animations', clip, original,
                       command_count=count, command_offset=offset)


def sound_section(data):
    count, = struct.unpack_from('>I', data, 16)
    metadata, = struct.unpack_from('>I', data, 12)
    starts = [struct.unpack_from('>I', data, 64 + i * 32)[0] for i in range(count)] + [len(data)]
    cuts = [metadata]
    for begin, end in zip(starts, starts[1:]):
        cuts.extend(range(begin + SOUND_BLOCK, end, SOUND_BLOCK))
        cuts.append(end)
    return n64_pack.Section(SOUND, data, header_bytes=metadata, chunk_bytes=SOUND_BLOCK,
                            compression='none', boundaries=tuple(sorted(set(cuts[:-1]))))


def build_boat_pack(tables):
    sections = [n64_pack.Section(k, data, header_bytes=32 if k < 0x10000 else 0)
                for k, data in tables.items() if k <= 0x10000]
    if SOUND in tables:
        sections.append(sound_section(tables[SOUND]))
    blob = n64_pack.build({'boat': sections}, games={'boat': 2})
    n64_pack.inspect(blob)
    return blob


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
    return build_boat_pack(tables | {floor: bytes(program), sectors: bytes(refs)})


def load_compare():
    repo = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('tr2_compare', repo / 'tools/tr2-compare.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('host', type=Path)
    ap.add_argument('boat', type=Path)
    ap.add_argument('fixtures', type=Path)
    ap.add_argument('--wall', type=Path, default=None)
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')

    c = load_compare()
    fixture = args.fixtures / 'schema22' / 'venice-stand'
    base = n64_pack.inspect(args.boat.read_bytes())['boat']
    items = rows(base, 'items')
    anims = rows(base, 'animations')
    lara = items[0]
    room = rows(base, 'rooms')[lara['room']]
    spawn_sector = (room['sector_start'] + ((lara['x'] - room['x']) >> 10) * room['z_sectors'] +
                    ((lara['z'] - room['z']) >> 10))

    with tempfile.TemporaryDirectory(prefix='tr2-venice-boot-', dir=scratch) as td:
        root = Path(td)
        trace = root / 'venice-stand.jsonl'

        # 1. Positive control: full 181-tick venice-stand parity via tr2-compare.py.
        p = subprocess.run(
            ['rtk', str(args.host), '--game-tr2-schema22', str(args.boat),
             str(fixture / 'input.txt'), str(trace)],
            capture_output=True, text=True, timeout=30)
        expect(p.returncode == 0, f'venice-stand replay failed: {p.stdout}{p.stderr}')
        port_bytes = trace.read_bytes()
        report = c.compare(fixture, port_bytes, 'tr64-tr2-venice-boat')
        expect(report['schema'] == 22 and report['records'] == 181, f'unexpected report header: {report}')
        for fam in ('lara', 'camera', 'active', 'save', 'inventory', 'world',
                    'rng[0]', 'boats', 'race', 'mines', 'effectsUsed'):
            stats = report['families'][fam]
            expect(stats['knownEqual'] == 181 and stats['first'] is None,
                   f'venice-stand mismatch in {fam}: {stats}')

        # Zero-exclusion full field verification on all 181 rows (including lara.timer & lara.vehicle).
        retail_bytes, masks, placement, _ = c._fixture_data(fixture, metadata=True)
        retail_rows = list(c.records(retail_bytes, masks, 'retail', placement, 22))
        port_rows = list(c.records(c.port_body(port_bytes, 22)[0], masks, 'port', placement, 22))
        for tick, (left, right) in enumerate(zip(retail_rows, port_rows, strict=True)):
            for fam in ('lara', 'camera', 'active', 'save', 'inventory', 'world',
                        'boats', 'race', 'mines', 'effectsUsed'):
                diff, _ = c.first_difference(left[fam], right[fam], fam, set())
                expect(diff is None, f'zero-exclusion diff at tick {tick} {fam}: {diff}')
            expect(left['rng'][0] == right['rng'][0], f'gameplay RNG diff at tick {tick}')

        # Check documented tr2-venice-level.md milestones.
        r0, r2, r52, r53, r180 = port_rows[0], port_rows[2], port_rows[52], port_rows[53], port_rows[180]
        expect(r0['lara']['pos'] == [58880, -2304, 22016] and r0['lara']['rot'] == [0, -32768, 0] and
               r0['lara']['room'] == 0 and (r0['lara']['anim'], r0['lara']['frame']) == (11, 185),
               'tick 0 Lara spawn mismatch')
        expect(r0['lara']['armL'] == {'angle': [0, 0, 0], 'anim': 254, 'frame': 0, 'aim': 0, 'flash': 0},
               'tick 0 armL carried inert clip mismatch')
        expect(r0['inventory']['items'] == [[121, 1], [157, 1], [158, 1], [173, 1], [172, 1], [171, 1]],
               'tick 0 carried inventory mismatch')
        expect([a['index'] for a in r0['active']] == [113, 72, 71, 70, 68, 67, 64, 39, 38, 35, 34, 33, 22, 4],
               'tick 0 active order mismatch')
        expect(r0['world'][:2] == [485659008, 433429501] and r2['world'][:2] == [2641629337, 433429501],
               'tick 0/2 world sector/box digest mismatch')
        expect((r52['lara']['anim'], r52['lara']['frame'], r52['lara']['hSpeed']) == (93, 1482, 50) and
               (r53['lara']['anim'], r53['lara']['frame'], r53['lara']['hSpeed']) == (82, 1361, 0),
               'tick 52/53 backward-fall landing mismatch')
        expect(r180['lara']['pos'] == [58880, 0, 23288] and r180['lara']['room'] == 0 and
               r180['lara']['health'] == 1000 and r180['save']['time'] == 180 and
               r180['save']['distance'] == 2949 and r180['effectsUsed'] == 0,
               'tick 180 neutral endpoint mismatch')
        print('PASS venice-stand: 181/181 exact tick-for-tick parity (all 11 logic/schema-22 families, 0 exclusions)')

        # 2. Negative control: tr2-compare.py rejects tampered schema-22 trace.
        lines = port_bytes.splitlines(keepends=True)
        tampered = json.loads(lines[0])
        tampered['effectsUsed'] = 1
        tampered.pop('hash')
        payload = json.dumps(tampered, separators=(',', ':')).encode('ascii')
        tampered_line = payload[:-1] + b',"hash":"' + c.fnv32(payload).encode('ascii') + b'"}\n'
        new_body = tampered_line + b''.join(lines[1:-1])
        footer = json.loads(lines[-1])
        footer['recordsHash'] = c.fnv32(new_body)
        footer.pop('hash')
        fpayload = json.dumps(footer, separators=(',', ':')).encode('ascii')
        new_footer = fpayload[:-1] + b',"hash":"' + c.fnv32(fpayload).encode('ascii') + b'"}\n'
        bad_report = c.compare(fixture, new_body + new_footer, 'tr64-tr2-venice-boat')
        expect(bad_report['families']['effectsUsed']['first'] == {'tick': 0, 'path': 'effectsUsed', 'retail': 0, 'port': 1},
               'tr2-compare.py failed to catch tampered effectsUsed')
        print('PASS comparator negative control: detected tampered schema-22 effectsUsed')

        # 3. Negative control: schema/level mismatch refused before tick 0.
        short_in = root / 'short.txt'
        short_in.write_text('0 0\n', encoding='ascii')
        mismatch = root / 'mismatch.jsonl'
        p21 = subprocess.run(
            ['rtk', str(args.host), '--game-tr2-schema21', str(args.boat), str(short_in), str(mismatch)],
            capture_output=True, text=True, timeout=15)
        expect(p21.returncode != 0 and not mismatch.exists(),
               'schema21 on boat.pack was not refused before tick 0')
        if args.wall is not None:
            p22 = subprocess.run(
                ['rtk', str(args.host), '--game-tr2-schema22', str(args.wall), str(short_in), str(mismatch)],
                capture_output=True, text=True, timeout=15)
            expect(p22.returncode != 0 and not mismatch.exists(),
                   'schema22 on wall.pack was not refused before tick 0')
        print('PASS schema/level mismatch refused before tick 0')

        # 4. Negative controls: malformed Venice tables refused before tick 0.
        case_pack = root / 'case.pack'
        case_trace = root / 'case.jsonl'
        malformed_cases = [
            ('bad-boat-slot', replace_row(base, 'items', 26, items[26], type=243),
             'schema-22 malformed boat 26'),
            ('bad-clock-slot', replace_row(base, 'items', 76, items[76], type=243),
             'schema-22 malformed race clock 76'),
            ('bad-mine-slot', replace_row(base, 'items', 106, items[106], type=243),
             'schema-22 malformed mine slot 106'),
            ('bad-gondola-cmd', replace_commands(base, 360, anims[360], 6, 0, 2),
             'world animation 360 uses unsupported command 6'),
            ('unknown-type', replace_row(base, 'items', 52, items[52], type=250),
             'unsupported placement type 250'),
        ]
        for label, tables, expected_err in malformed_cases:
            case_trace.unlink(missing_ok=True)
            case_pack.write_bytes(build_boat_pack(tables))
            proc = subprocess.run(
                ['rtk', str(args.host), '--game-tr2-schema22', str(case_pack), str(short_in), str(case_trace)],
                capture_output=True, text=True, timeout=15)
            out_text = proc.stdout + proc.stderr
            emitted = case_trace.exists() and case_trace.stat().st_size != 0
            expect(proc.returncode != 0 and not emitted and expected_err in out_text,
                   f'{label}: expected refusal {expected_err!r}, got rc={proc.returncode} trace={emitted} err={out_text[-300:]}')
            print(f'PASS malformed table {label}: refused before tick 0 ({expected_err})')

        # 5. Implemented Venice hostile & window controllers activate cleanly.
        implemented_cases = [
            ('doberman-1', 1),
            ('gunman-3', 3),
            ('rat-36', 36),
            ('thug-10', 10),
            ('window-46', 46),
        ]
        for label, slot in implemented_cases:
            case_trace.unlink(missing_ok=True)
            words = [END | 4, MASK, END | slot]
            case_pack.write_bytes(spawn_program(base, spawn_sector, words))
            proc = subprocess.run(
                ['rtk', str(args.host), '--game-tr2-schema22', str(case_pack), str(short_in), str(case_trace)],
                capture_output=True, text=True, timeout=15)
            expect(proc.returncode == 0 and case_trace.exists(),
                   f'activate {label}: expected clean run, got rc={proc.returncode} err={(proc.stdout + proc.stderr)[-300:]}')
        print(f'PASS implemented Venice hostile/window activation ({len(implemented_cases)}/{len(implemented_cases)} activated cleanly)')

        # 6. Negative control: still-unimplemented waterfall mist emitter stops cleanly by name in range.
        case_trace.unlink(missing_ok=True)
        moved = replace_row(base, 'items', 33, items[33],
                            room=lara['room'], x=lara['x'], y=lara['y'], z=lara['z'])
        case_pack.write_bytes(build_boat_pack(moved))
        proc = subprocess.run(
            ['rtk', str(args.host), '--game-tr2-schema22', str(case_pack), str(short_in), str(case_trace)],
            capture_output=True, text=True, timeout=15)
        out_text = proc.stdout + proc.stderr
        expect(proc.returncode != 0 and 'unsupported waterfall mist emitter' in out_text,
               f'waterfall mist refusal failed: rc={proc.returncode} err={out_text[-300:]}')
        print('PASS still-unimplemented waterfall mist emitter refused cleanly by name')


if __name__ == '__main__':
    main()
