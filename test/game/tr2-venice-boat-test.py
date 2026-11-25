#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed Venice (BOAT.TR2) speedboat, mines, race, and lock-flip tests."""
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
FD_ACT_FLIP = 3 << 10
TR2_DOOR_TYPES = set(range(106, 114))


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


def sector_digest(room_sectors):
    h = 2166136261
    for secs in room_sectors:
        for s in secs:
            raw = struct.pack('<HHBbBb',
                              s['floor_index'], s['box_index'],
                              s['room_below'], s['floor'],
                              s['room_above'], s['ceiling'])
            for b in raw:
                h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def door_refs(rooms, room_sectors, floor_data, item, flipped):
    """Mirror game/common/world.c tr2DoorRefs for a Venice ordinary door."""
    n = len(rooms)
    mate = [255] * n
    alternate = [False] * n
    for i, r in enumerate(rooms):
        alt = r['alternate_room']
        if alt >= 0:
            mate[i] = alt
            mate[alt] = i
            alternate[alt] = True

    yaw = item['angle']
    dx = {0: 0, 16384: -1024, -32768: 0, -16384: 1024}[yaw]
    dz = {0: -1024, 16384: 0, -32768: 1024, -16384: 0}[yaw]
    out = []

    def sec_idx(r_idx, x, z):
        r = rooms[r_idx]
        sx = max(0, min(r['x_sectors'] - 1, (x - r['x']) >> 10))
        sz = max(0, min(r['z_sectors'] - 1, (z - r['z']) >> 10))
        return sx * r['z_sectors'] + sz

    def portal_room(r_idx, s_idx):
        fd = room_sectors[r_idx][s_idx]['floor_index']
        while fd:
            w = floor_data[fd]
            kind = w & 0x1f
            if kind == 1:
                return floor_data[fd + 1]
            if w & 0x8000:
                break
            if kind in (2, 3):
                fd += 2
            elif kind == 4:
                fd += 2
                while True:
                    a = floor_data[fd]
                    fd += 1
                    if ((a >> 10) & 0x1f) in (1, 12):
                        fd += 1
                    if a & 0x8000:
                        break
            else:
                fd += 1
        return None

    def add_face(r_idx, x, z):
        s_idx = sec_idx(r_idx, x, z)
        slot = mate[r_idx] if (flipped and mate[r_idx] != 255) else r_idx
        out.append((slot, s_idx))
        return portal_room(r_idx, s_idx)

    rid = item['room']
    if alternate[rid]:
        rid = mate[rid]
    nx, nz = item['x'] + dx, item['z'] + dz
    behind = add_face(rid, nx, nz)
    if mate[rid] != 255:
        add_face(mate[rid], nx, nz)
    if behind is not None and behind < n:
        if alternate[behind]:
            behind = mate[behind]
        add_face(behind, item['x'], item['z'])
        if mate[behind] != 255:
            add_face(mate[behind], item['x'], item['z'])
    return out


def close_sector(s):
    return s | {'floor_index': 0, 'box_index': 65535,
                'room_below': 255, 'floor': -127,
                'room_above': 255, 'ceiling': -127}


def verify_lock_flip_apply_refs(host, base, spawn_sector, root):
    """Positive + negative control for game/common/world.c applyRefs room->source fix."""
    rooms = rows(base, 'rooms')
    secs = rows(base, 'sectors')
    items = rows(base, 'items')
    fd_ident, _ = TABLES['floor_data']
    floor_data = [r[0] for r in struct.iter_unpack('>H', base[fd_ident][HEADER:])]

    # Build a pack whose spawn sector flips slot 0 (swapping rooms 49/51, 50/63, 62/64)
    # and toggles lock doors 38, 39 (close) and 53, 54 (open into flipped room 49).
    words = [END | 4, MASK, FD_ACT_FLIP | 0, 38, 39, 53, END | 54]
    pack_path = root / 'lock-flip.pack'
    pack_path.write_bytes(spawn_program(base, spawn_sector, words))
    inp = root / 'lock-flip.txt'
    inp.write_text('0\n' * 140, encoding='ascii')
    trace = root / 'lock-flip.jsonl'
    proc = subprocess.run(
        ['rtk', str(host), '--game-tr2-schema22', str(pack_path), str(inp), str(trace)],
        capture_output=True, text=True, timeout=30)
    expect(proc.returncode == 0, f'lock-flip replay failed: {proc.stdout}{proc.stderr}')
    tr = [json.loads(line) for line in trace.read_text(encoding='ascii').splitlines()[:-1]]

    # Independently model live room sectors with the fix vs the pre-fix bug.
    orig_rooms = [secs[r['sector_start']:r['sector_start'] + r['sector_count']] for r in rooms]
    orig_rooms[0][spawn_sector] = orig_rooms[0][spawn_sector] | {
        'floor_index': (len(base[fd_ident]) - HEADER) // 2
    }
    ext_fd = list(floor_data) + words

    def simulate_sectors(use_source, stage):
        live = [[dict(s) for s in rs] for rs in orig_rooms]
        source = list(range(len(rooms)))
        for idx, it in enumerate(items):
            if it['type'] in TR2_DOOR_TYPES:
                for r_idx, s_idx in door_refs(rooms, orig_rooms, ext_fd, it, False):
                    live[r_idx][s_idx] = close_sector(live[r_idx][s_idx])
        if stage == 'tick0':
            return sector_digest(live)
        # Startup reverse-start leaves 38, 39, 64 open on tick 2 (before flip).
        for idx in (38, 39, 64):
            for r_idx, s_idx in door_refs(rooms, orig_rooms, ext_fd, items[idx], False):
                live[r_idx][s_idx] = dict(orig_rooms[source[r_idx]][s_idx])
        # Flip slot 0 swaps (49,51), (50,63), (62,64).
        for a, b in ((49, 51), (50, 63), (62, 64)):
            live[a], live[b] = live[b], live[a]
            source[a], source[b] = source[b], source[a]
        # Lock doors 38, 39 close; 53, 54 open into flipped rooms 49/51.
        for idx in (38, 39):
            for r_idx, s_idx in door_refs(rooms, orig_rooms, ext_fd, items[idx], True):
                live[r_idx][s_idx] = close_sector(live[r_idx][s_idx])
        for idx in (53, 54):
            for r_idx, s_idx in door_refs(rooms, orig_rooms, ext_fd, items[idx], True):
                src = source[r_idx] if use_source else r_idx
                live[r_idx][s_idx] = dict(orig_rooms[src][s_idx])
        return sector_digest(live)

    expect(tr[0]['world'][0] == simulate_sectors(True, 'tick0'),
           f'lock-flip model tick-0 mismatch: got={tr[0]["world"][0]} exp={simulate_sectors(True, "tick0")}')
    good_world0 = simulate_sectors(use_source=True, stage='final')
    buggy_world0 = simulate_sectors(use_source=False, stage='final')
    expect(good_world0 != buggy_world0,
           'lock-flip negative control degenerate: good and buggy world[0] collided')

    # Verify flip fired (world[2] & world[3] changed) and doors 38/39 closed + 53/54 opened with good_world0.
    expect(tr[140]['world'][2] != tr[0]['world'][2] and tr[140]['world'][3] != tr[0]['world'][3],
           'flip slot 0 did not change world[2]/world[3]')
    got_world0 = tr[140]['world'][0]
    expect(got_world0 == good_world0 and got_world0 != buggy_world0,
           f'applyRefs lock-flip world[0] mismatch: got={got_world0} good={good_world0} buggy={buggy_world0}')
    print(f'PASS applyRefs lock-flip fix: world[0]={got_world0} matches source-room model (buggy={buggy_world0} rejected)')


def verify_schema22_routes(host, boat, fixtures, c, root):
    # 1. Exact 100% parity routes via tr2-compare.py (producer tr64-tr2-venice-boat).
    full_routes = [
        ('venice-stand', 181, True),
        ('race-threshold', 76, True),
        ('race-countdown', 1177, False),
        ('venice-boat-crash', 401, False),
        ('venice-mine-pass', 1161, False),
    ]
    logic_fams = ('lara', 'camera', 'active', 'save', 'inventory',
                  'world', 'boats', 'race', 'mines', 'rng[0]')
    for name, expected_n, check_fx in full_routes:
        fdir = fixtures / 'schema22' / name
        out = root / f'{name}.jsonl'
        p = subprocess.run(
            ['rtk', str(host), '--game-tr2-schema22', str(boat), str(fdir / 'input.txt'), str(out)],
            capture_output=True, text=True, timeout=60)
        expect(p.returncode == 0, f'{name} replay failed: {p.stdout}{p.stderr}')
        rep = c.compare(fdir, out.read_bytes(), 'tr64-tr2-venice-boat')
        expect(rep['schema'] == 22 and rep['records'] == expected_n,
               f'{name}: unexpected report header {rep}')
        fams = logic_fams + (('effectsUsed',) if check_fx else ())
        for fam in fams:
            st = rep['families'][fam]
            expect(st['knownEqual'] == expected_n and st['first'] is None,
                   f'{name}: mismatch in {fam}: {st}')
        tag = 'all 11 families' if check_fx else 'all 10 gameplay families'
        print(f'PASS {name}: {expected_n}/{expected_n} exact parity ({tag})')

    # 2. Partial-spec routes: verify every established family matches and key Venice milestones hold.
    # venice-boat-control: 621/621 on active, inventory, world, boats, race, mines, rng[0] (Q-VBOAT-01 at t=205 lara.rot[0]).
    for name, expected_n, exact_fams in (
        ('venice-boat-control', 621, ('active', 'inventory', 'world', 'boats', 'race', 'mines', 'rng[0]')),
        ('venice-mine-hit', 1161, ('camera', 'save', 'inventory', 'world', 'boats', 'race')),
    ):
        fdir = fixtures / 'schema22' / name
        out = root / f'{name}.jsonl'
        p = subprocess.run(
            ['rtk', str(host), '--game-tr2-schema22', str(boat), str(fdir / 'input.txt'), str(out)],
            capture_output=True, text=True, timeout=60)
        expect(p.returncode == 0, f'{name} replay failed: {p.stdout}{p.stderr}')
        rbytes, masks, placement, _ = c._fixture_data(fdir, metadata=True)
        rrows = list(c.records(rbytes, masks, 'retail', placement, 22))
        prows = list(c.records(c.port_body(out.read_bytes(), 22)[0], masks, 'port', placement, 22))
        expect(len(prows) == expected_n, f'{name}: expected {expected_n} rows, got {len(prows)}')
        for fam in exact_fams:
            for t in range(expected_n):
                if fam == 'rng[0]':
                    expect(rrows[t]['rng'][0] == prows[t]['rng'][0], f'{name}: rng[0] diff at tick {t}')
                else:
                    diff, _ = c.first_difference(rrows[t][fam], prows[t][fam], fam, set())
                    expect(diff is None, f'{name}: {fam} diff at tick {t}: {diff}')
        if name == 'venice-mine-hit':
            # Pre-blast ticks 0..1131 match on all 10 gameplay families; t=1132 detonates mine 106, removes boat 91, sets chain=1.
            for t in range(1132):
                expect(rrows[t]['rng'][0] == prows[t]['rng'][0], f'venice-mine-hit: pre-blast rng[0] diff at {t}')
                for fam in ('lara', 'active', 'mines'):
                    diff, _ = c.first_difference(rrows[t][fam], prows[t][fam], fam, set())
                    expect(diff is None, f'venice-mine-hit: pre-blast {fam} diff at {t}: {diff}')
            expect(prows[1132]['boats'][1]['present'] == 0 and
                   prows[1132]['mines']['chain'] == 1 and
                   prows[1132]['mines']['items'][0]['spent'] == 1 and
                   prows[1132]['lara']['visibleMask'] == 0,
                   'venice-mine-hit: mine 106 blast state mismatch at tick 1132')
        print(f'PASS {name}: {expected_n}/{expected_n} on {",".join(exact_fams)}')


def verify_refusals(host, base, fixtures, c, root):
    models = rows(base, 'models')
    short_in = root / 'short.txt'
    short_in.write_text('0 0\n', encoding='ascii')
    case_pack = root / 'case.pack'
    case_trace = root / 'case.jsonl'

    # 1. Missing mine animation (model 222 animation = 65535) refused before tick 0.
    mine_idx = next(i for i, m in enumerate(models) if m['type'] == 222)
    bad_mine = replace_row(base, 'models', mine_idx, models[mine_idx], animation=65535)
    case_pack.write_bytes(build_boat_pack(bad_mine))
    p = subprocess.run(
        ['rtk', str(host), '--game-tr2-schema22', str(case_pack), str(short_in), str(case_trace)],
        capture_output=True, text=True, timeout=15)
    out = p.stdout + p.stderr
    expect(p.returncode != 0 and not case_trace.exists() and
           ('no animation for placed type 222' in out or 'schema-22 mine slot 106 has no animation' in out),
           f'missing mine animation not refused before tick 0: rc={p.returncode} out={out[-240:]}')

    # 2. Invalid Lara boat model 11 bank refused before tick 0.
    lboat_idx = next(i for i, m in enumerate(models) if m['type'] == 11)
    bad_lboat = replace_row(base, 'models', lboat_idx, models[lboat_idx], animation=65535)
    case_pack.write_bytes(build_boat_pack(bad_lboat))
    p = subprocess.run(
        ['rtk', str(host), '--game-tr2-schema22', str(case_pack), str(short_in), str(case_trace)],
        capture_output=True, text=True, timeout=15)
    out = p.stdout + p.stderr
    expect(p.returncode != 0 and not case_trace.exists() and
           'invalid Lara boat animation bank' in out,
           f'missing Lara boat bank not refused before tick 0: rc={p.returncode} out={out[-240:]}')
    print('PASS malformed mine/Lara-boat tables refused before tick 0')

    # 3. tools/tr2-compare.py VENICE_ENTRY exact-int validation rejects bools & bad keys.
    stand_dir = fixtures / 'schema22' / 'venice-stand'
    base_manifest = json.loads((stand_dir / 'manifest.json').read_text(encoding='ascii'))
    m_dir = root / 'manifest-check'
    m_dir.mkdir()
    (m_dir / 'input.txt').symlink_to(stand_dir / 'input.txt')
    (m_dir / 'trace.jsonl').symlink_to(stand_dir / 'trace.jsonl')
    (m_dir / 'manifest.json').write_text(json.dumps(base_manifest), encoding='ascii')
    c._fixture_data(m_dir)
    for label, bad in (
        ('bool-weapon', base_manifest | {'entry': base_manifest['entry'] | {'weapon': True}}),
        ('bool-flares', base_manifest | {'entry': base_manifest['entry'] | {'spareFlares': True}}),
        ('bool-smallMedi', base_manifest | {'entry': base_manifest['entry'] | {'smallMedikits': True}}),
        ('bool-level', base_manifest | {'level': True}),
        ('extra-entry-key', base_manifest | {'entry': base_manifest['entry'] | {'extra': 1}}),
        ('missing-entry-key', base_manifest | {'entry': {k: v for k, v in base_manifest['entry'].items() if k != 'weapon'}}),
    ):
        (m_dir / 'manifest.json').write_text(json.dumps(bad), encoding='ascii')
        failed = False
        try:
            c._fixture_data(m_dir)
        except ValueError:
            failed = True
        expect(failed, f'tr2-compare.py accepted invalid VENICE_ENTRY ({label})')
    print('PASS tr2-compare.py VENICE_ENTRY exact-int validation negative controls')


def verify_generic_boat_slot(host, base, root):
    """Prove speedboat physics works at an arbitrary item slot under a non-'boat' pack name."""
    items = rows(base, 'items')
    boat91 = items[91]
    # Move canal boat from slot 91 to slot 52 (originally a decorative camera target) and
    # replace slot 91 with a camera target; pack under level name 'wall' (not 'boat').
    swapped = replace_row(base, 'items', 52, items[52],
                          type=boat91['type'], room=boat91['room'],
                          x=boat91['x'], y=boat91['y'], z=boat91['z'],
                          angle=boat91['angle'], intensity=boat91['intensity'],
                          flags=boat91['flags'])
    swapped = replace_row(swapped, 'items', 91, boat91, type=243)
    sections = [n64_pack.Section(k, data, header_bytes=32 if k < 0x10000 else 0)
                for k, data in swapped.items() if k <= 0x10000]
    if SOUND in swapped:
        sections.append(sound_section(swapped[SOUND]))
    blob = n64_pack.build({'wall': sections}, games={'wall': 2})
    pack_path = root / 'moved-boat-wall.pack'
    pack_path.write_bytes(blob)
    # Place Lara directly at the moved boat's origin (dry boarding) and drive forward (IN_UP = 8).
    inp = root / 'moved-boat-drive.txt'
    lines = [f"place {boat91['x']} {boat91['y']} {boat91['z']} {boat91['angle']} {boat91['room']}\n"]
    lines.extend('8\n' for _ in range(80))
    inp.write_text(''.join(lines), encoding='ascii')
    trace_path = root / 'moved-boat-drive.jsonl'
    p = subprocess.run(
        ['rtk', str(host), '--game-tr2-schema21', str(pack_path), str(inp), str(trace_path)],
        capture_output=True, text=True, timeout=15)
    expect(p.returncode == 0, f'generic boat slot replay failed: {p.stdout}{p.stderr}')
    recs = [json.loads(line) for line in trace_path.read_text(encoding='ascii').splitlines()[:-1]]
    expect(len(recs) == 81, f'expected 81 records, got {len(recs)}')
    start_pos = recs[0]['lara']['pos']
    end_pos = recs[-1]['lara']['pos']
    moved = abs(end_pos[0] - start_pos[0]) + abs(end_pos[2] - start_pos[2])
    expect(moved > 500, f'boat at non-hardcoded slot 52 under level "wall" did not move: {start_pos} -> {end_pos}')
    print(f'PASS generic non-hardcoded boat (slot=52, pack="wall"): drove {moved} units without levelId/slot hardcoding')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('host', type=Path)
    ap.add_argument('boat', type=Path)
    ap.add_argument('fixtures', type=Path)
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')

    c = load_compare()
    base = n64_pack.inspect(args.boat.read_bytes())['boat']
    items = rows(base, 'items')
    lara = items[0]
    room = rows(base, 'rooms')[lara['room']]
    spawn_sector = (room['sector_start'] + ((lara['x'] - room['x']) >> 10) * room['z_sectors'] +
                    ((lara['z'] - room['z']) >> 10))

    with tempfile.TemporaryDirectory(prefix='tr2-venice-boat-', dir=scratch) as td:
        root = Path(td)
        verify_lock_flip_apply_refs(args.host, base, spawn_sector, root)
        verify_schema22_routes(args.host, args.boat, args.fixtures, c, root)
        verify_refusals(args.host, base, args.fixtures, c, root)
        verify_generic_boat_slot(args.host, base, root)


if __name__ == '__main__':
    main()
