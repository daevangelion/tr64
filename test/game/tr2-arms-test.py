#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check production TR2 arms and inventory with synthetic held input."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile


def expect(condition, message):
    if not condition:
        raise RuntimeError(message)


def capture(host, pack, root, masks):
    keys = root / 'input.txt'
    trace = root / 'trace.jsonl'
    trace.unlink(missing_ok=True)
    keys.write_text(' '.join(map(str, masks)) + '\n', encoding='ascii')
    result = subprocess.run(['rtk', str(host), '--game-tr2', str(pack), str(keys), str(trace)],
                            capture_output=True, text=True, check=False, timeout=15)
    return result, trace


def script(host, pack, root, masks):
    result, trace = capture(host, pack, root, masks)
    expect(result.returncode == 0, result.stdout + result.stderr)
    records = [json.loads(line) for line in trace.read_text().splitlines()]
    expect(len(records) == len(masks) + 2, 'wrong record or footer count')
    return records[:-1]


def stock(record):
    return dict(record['inventory']['items']).get(173, 0)

CONE_MESSAGE = 'target acquisition did not distinguish the visible in-cone target from the hidden and out-of-cone ones'


def targeting(host, pack, root, masks, cone_only=False):
    spec = importlib.util.spec_from_file_location('item_controls',
                                                  Path(__file__).with_name('tr2-item-actions-test.py'))
    item_controls = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(item_controls)
    base = item_controls.n64_pack.inspect(pack.read_bytes())['wall']
    items = item_controls.rows(base, 'items')
    lara = next(item for item in items if item['type'] == 0)
    tiger = next(i for i, item in enumerate(items) if item['type'] == 39)
    room = item_controls.rows(base, 'rooms')[lara['room']]
    sector = (room['sector_start'] + ((lara['x'] - room['x']) >> 10) * room['z_sectors'] +
              ((lara['z'] - room['z']) >> 10))
    candidate = root / 'target.pack'
    # The tiger has a live controller: it moves as soon as it wakes. The wake
    # is therefore a combat trigger (floor function 4, trigger type 7) on Lara's
    # sector, which fires on the tick her pistols become ready, and the target
    # is read in the ticks right after it. The synthetic program replaces the
    # spawn sector's floor data, so terrain and placement differ from the
    # retail route.
    combat_wake = [0x8004 | (7 << 8), 0x3e00, 0x8000 | tiger]

    def place(x, y, z):
        tables = item_controls.replace_row(base, 'items', tiger, items[tiger], room=lara['room'],
                                           x=x, y=y, z=z, angle=0)
        candidate.write_bytes(item_controls.spawn_program(tables, sector, combat_wake))
        records = script(host, candidate, root, masks)
        awake = [i for i, r in enumerate(records) if any(a['index'] == tiger for a in r['active'])]
        expect(awake and awake[0] + 3 < len(records), 'synthetic tiger did not wake with room to read targets')
        expect(records[awake[0]]['lara']['weaponState'] == 4 and
               records[awake[0] - 1]['lara']['weaponState'] != 4, 'combat wake did not fire on the ready tick')
        return records, awake[0]

    # Her recorded position on the wake tick, from a first run with a tiger anywhere.
    probe, wake = place(lara['x'] + 1200, lara['y'] + 3840, lara['z'] + 4096)
    x, y, z = probe[wake]['lara']['pos']
    expect(y > lara['y'], 'Lara did not settle on the floor before the wake')
    # Offsets from her recorded point (the height is the spawn-height offset of 3840 less the 768 she
    # fell on landing; the tiger drops to its floor anyway). Lara stands on a platform facing +z.
    #   in cone:    1200 aside, +3072 down, 4096 ahead, down in the pit, in line of sight, within both cones.
    #   hidden:     1200 aside, +3072 down, 2048 ahead, behind the platform edge: no line of sight.
    #   yaw out:    ±2250 aside, +3072 down, 1000 ahead, in line of sight at ~66 degrees off facing (> 60).
    #   pitch high: 0 aside, -3000 up, 1000 ahead, in open sky room 78 above Lara: in line of sight,
    #               yaw ~0, elevation ~67-70 degrees (outside the 60-degree pitch cone).
    seen = {}
    placements = {
        'in cone': (1200, 3072, 4096),
        'hidden': (1200, 3072, 2048),
        'yaw right': (2250, 3072, 1000),
        'yaw left': (-2250, 3072, 1000),
        'pitch high': (0, -3000, 1000),
    }
    for name, (side, vertical, ahead) in placements.items():
        records, first = place(x + side, y + vertical, z + ahead)
        expect(first == wake, 'the wake tick depends on the tiger placement')
        seen[name] = [(r['lara']['target'], r['lara']['armR']['aim'], r['lara']['armL']['aim'])
                      for r in records[first + 1:first + 4]]
    expect(seen['in cone'] == [(tiger, 1, 1)] * 3 and
           all(rows == [(-1, 0, 0)] * 3 for name, rows in seen.items() if name != 'in cone'),
           CONE_MESSAGE)
    if cone_only:
        return

    failures = []

    def refused(label, tables, message):
        candidate.write_bytes(item_controls.spawn_program(tables, sector, [0x8004, 0, 0x8000 | tiger]))
        result, trace = capture(host, candidate, root, [0])
        if (not result.returncode or (trace.exists() and trace.stat().st_size != 0) or
                message not in result.stdout + result.stderr):
            failures.append(f'{label}: not refused by name before tick 0')

    # The Core accepts this shortened clip structurally, but it cannot
    # supply the full independently indexed pistol aim sequence.
    models = item_controls.rows(base, 'models')
    anims = item_controls.rows(base, 'animations')
    aim = next(m['animation'] for m in models if m['type'] == 1)
    short = item_controls.replace_row(base, 'animations', aim, anims[aim],
                                      frame_end=anims[aim]['frame_end'] - 1)
    refused('shortened pistol clip', short, f'invalid authored arm range {aim}')
    held = next(m['animation'] for m in models if m['type'] == 9)
    # Each clip stops in a different validator: the bank check names no owner, Lara's own check does.
    for clip, text in ((held, f'Tomb Raider II: packed rotations truncated: animation={held} frame=0 joint=0'),
                       (189, f'Tomb Raider II: Lara packed rotations truncated: animation=189 '
                             f'frame={anims[189]["frame_start"]} joint=0')):
        short = item_controls.replace_row(base, 'animations', clip, anims[clip], frame_size=9)
        refused(f'pose header only, clip {clip}', short, text)

    def corrupt_rotations(clip, key, sampled, label):
        ident, _ = item_controls.TABLES['frames']
        raw = bytearray(base[ident])
        a = anims[clip]
        stride = 2 * a['frame_size']
        begin = item_controls.HEADER + a['frame_offset'] + key * stride
        # Fifteen all-axis tags need 30 words, more than this key holds.
        expect(9 <= a['frame_size'] < 39, 'mutation must truncate the packed rotation stream')
        raw[begin + 18:begin + stride] = bytes(stride - 18)
        refused(label, base | {ident: bytes(raw)}, f'animation={clip} frame={sampled}')

    idle, toss = anims[103], anims[189]
    corrupt_rotations(103, 1, idle['frame_start'] + idle['frame_rate'], 'later idle key tags')
    span = toss['frame_end'] - toss['frame_start']
    expect(span % toss['frame_rate'] != 0, 'throw clip should have a fractional tail')
    # With the interaction rules the Lara clip is walked key by key (validateContactClip), so the
    # physical tail key is named by its own sample frame, one interval past frame_end.
    corrupt_rotations(189, span // toss['frame_rate'] + 1,
                      toss['frame_start'] + (span // toss['frame_rate'] + 1) * toss['frame_rate'],
                      'throw physical tail key')

    # The legacy effect 2 can select dry stand. A valid-syntax TR2
    # body command with that numeric value must not enter gameplay.
    foreign = item_controls.replace_commands(base, 189, anims[189], 6, anims[189]['frame_start'] + 1, 2)
    refused('unsupported body effect', foreign, 'Lara animation 189 unsupported effect 2')

    bubbles = item_controls.replace_commands(base, 103, idle, 6, idle['frame_start'], 3)
    candidate.write_bytes(item_controls.spawn_program(bubbles, sector, [0x8004, 0, 0x8000 | tiger]))
    result, trace = capture(host, candidate, root, [0])
    if result.returncode or 'tr2-inert-animation-bubbles: count=1' not in result.stdout + result.stderr:
        failures.append('authored bubbles effect was not admitted and counted inert')
    expect(not failures, '\n'.join(failures))
    print('PASS: short clip, arm/body rotation spans and later keys, unsupported effect, inert bubbles')

def releases(host, pack, root, held, draw, flare):
    release = len(held) + 1

    def loose(record):
        return next((i for i in record['active'] if i['type'] == 152), None)

    tossed = script(host, pack, root, held + [flare] + [0] * 1810)
    expect(tossed[release]['lara']['weaponState'] == 3 and
           tossed[release]['lara']['anim'] == 189 and
           tossed[release]['lara']['flareFrame'] == tossed[release]['lara']['frame'] + 1,
           'standing flare toss did not select the independent body counter')
    spawn = release + 20
    item = loose(tossed[spawn])
    expect(not loose(tossed[spawn - 1]) and item and item['timer'] == 0 and
           (item['hSpeed'], item['vSpeed']) == (50, -50) and
           loose(tossed[spawn + 1])['vSpeed'] == -44,
           'throw was not committed at arm phase 21 before its first world update')
    expiration = spawn + 1800 - tossed[spawn]['lara']['flareAge']
    expect(loose(tossed[expiration]) and not loose(tossed[expiration + 1]),
           'stationary flare did not leave after, rather than upon, reaching age 1800')
    drop = script(host, pack, root, held + [draw | flare] + [0] * 70)
    item = loose(drop[release])
    expect(item and (item['hSpeed'], item['vSpeed']) == (10, 50) and
           drop[release]['lara']['weaponState'] == 2 and drop[release]['lara']['weapon'] == 1,
           'eligible simultaneous Draw/Flare did not immediately drop and draw')
    expect(stock(drop[-1]) == stock(tossed[-1]) == 1, 'release spent an additional flare')

    held_again = script(host, pack, root, [0] * 35 + [flare] * 120)
    expect(held_again[98]['lara']['weaponState'] == 3 and not loose(held_again[117]) and
           loose(held_again[118]) and held_again[129]['lara']['weaponState'] == 0 and
           held_again[130]['lara']['armL']['frame'] == 33 and stock(held_again[-1]) == 0,
           'held Flare required a second press or did not draw the remaining stock after stow')

def control(host, pack, scratch):
    draw, action, flare = 256, 128, 8192
    settle = [0] * 150  # Deliberate neutral input, no spawn override.
    start = len(settle) + 1
    with tempfile.TemporaryDirectory(prefix='tr2-arms-', dir=scratch) as td:
        root = Path(td)
        f = script(host, pack, root, settle + [flare] + [0] * 75)
        expect(f[start]['lara']['weaponState'] == 2 and f[start]['lara']['armL']['frame'] == 33,
               'flare did not start its auxiliary draw sequence')
        expect(stock(f[start + 12]) == 2 and stock(f[start + 13]) == 1,
               'flare charge did not occur on arm frame 46')
        expect(f[start + 38]['lara']['flareAge'] == 0 and f[start + 39]['lara']['flareAge'] == 1,
               'held age did not start on ignition frame 72')
        expect(f[start + 61]['lara']['weaponState'] == 0 and f[start + 61]['lara']['armL']['frame'] == 0,
               'flare draw did not free Lara and select the held pose at 94')
        expect(all(r['save']['ammoUsed'] == 0 and r['rng'][0] == f[0]['rng'][0] for r in f),
               'a handheld flare consumed ammunition or gameplay randomness')
        releases(host, pack, root, settle + [flare] + [0] * 75, draw, flare)
        p = script(host, pack, root, settle + [draw] + [0] * 18 + [action] * 15 + [0] + [draw] + [0] * 26)
        expect(p[start]['lara']['weaponState'] == 2 and p[start]['lara']['armR']['frame'] == 5,
               'pistols did not enter the first authored pickup pose')
        expect(p[start + 18]['lara']['weaponState'] == 4 and p[start + 18]['lara']['armR']['frame'] == 0,
               'pistols did not finish drawing at arm frame 23')
        first = start + 23
        expect(p[first - 1]['save']['ammoUsed'] == 0 and p[first]['save']['ammoUsed'] == 2 and
               p[first + 9]['save']['ammoUsed'] == 2 and p[first + 10]['save']['ammoUsed'] == 4,
               'paired gun raising/recoil did not produce two 10-tick volleys')
        for i in (first, first + 10):
            expect(p[i]['lara']['armR']['flash'] == p[i]['lara']['armL']['flash'] == 3 and
                   p[i]['lara']['armR']['frame'] == p[i]['lara']['armL']['frame'] == 24,
                   'projectile commit did not kick both arms into recoil and flash')
            expect(p[i]['lara']['ammo'][0] == 999,
                   'paired pistol attempts did not restore and spend the unlimited reserve')
            seed = p[i - 1]['rng'][0]
            for _ in range(4):  # Two axes per projectile, left then right.
                seed = (seed * 0x41c64e6d + 0x3039) & 0xffffffff
            expect(p[i]['rng'][0] == seed, 'projectiles did not advance exactly four logic draws')
        overlap = script(host, pack, root, settle + [draw] + [0] * 18 +
                         [action] * 6 + [draw | action])
        last, prior = overlap[-1], overlap[-2]
        expect(last['lara']['weaponState'] == 3 and last['lara']['armL']['frame'] == 4 and
               last['lara']['armL']['flash'] == 1 and last['rng'][0] == prior['rng'][0] and
               last['save']['ammoUsed'] == prior['save']['ammoUsed'] == 2,
               'holster did not take priority over Action during recoil')
        expect(p[-1]['lara']['weaponState'] == 0 and p[-1]['lara']['target'] == -1,
               'paired holster did not clear targeting and return to free hands')
        expect(p[-1]['save']['ammoUsed'] == 4 and p[-1]['inventory'] == p[0]['inventory'],
               'holster changed carried stock or issued a projectile')
        targeting(host, pack, root, settle + [draw] + [0] * 18 + [action] * 4)
    print('PASS: held flare, throw/drop and expiry, paired gun timing and reserve, exact RNG, target acquisition '
          '(line of sight, yaw cone, pitch cone), short-arm admission')


def cone_mutants(build, pack, scratch, game='1'):
    """Prove the target check against mutated scratch copies of lara_weapon.c.

    Uses the ring-latch test's compile and link helpers. The unmodified copy must match
    the build's section sizes and pass the target check; each mutant removing line of sight,
    the yaw cone, the pitch cone, or widening the pitch cone to 90 degrees must fail by name."""
    ring = runpy.run_path(str(Path(__file__).with_name('tr2-ring-latch-test.py')))
    root, weapon = ring['ROOT'], ring['WEAPON']
    source = (root / weapon).read_text()
    cone_test = 'if (pitch > pitchCone || yaw > 10920 || yaw >= bestYaw + 2730)'
    sight_test = 'bool visible=game_tr2_sight(from,&to,true);'
    expect(source.count(cone_test) == 1, 'the cone test is not in lara_weapon.c exactly once')
    expect(source.count(sight_test) == 1, 'the sight test is not in lara_weapon.c exactly once')
    scratch = Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    control = scratch / 'tr2-rex-control.o'
    ring['compile_object'](build, game, root / 'test/game/tr2-rex-control.c', control)
    masks = [0] * 150 + [256] + [0] * 18 + [128] * 4
    cases = (
        ('unmodified copy', cone_test, cone_test, False),
        ('copy with sight forced true', sight_test, 'bool visible=true; (void)from; (void)to;', True),
        ('copy without the yaw cone', cone_test, 'if (pitch > pitchCone || yaw >= bestYaw + 2730)', True),
        ('copy without the pitch cone', cone_test,
         '(void)pitch; (void)pitchCone; if (yaw > 10920 || yaw >= bestYaw + 2730)', True),
        ('copy with pitch cone widened to 90 deg', cone_test,
         '(void)pitchCone; if (pitch > 16384 || yaw > 10920 || yaw >= bestYaw + 2730)', True),
    )
    own_sections = ring['sections'](build / 'host' / ring['OBJECT'])
    for name, old, new, stops in cases:
        path = scratch / 'cone.c'
        path.write_text(source.replace(old, new, 1))
        obj = scratch / 'cone.o'
        ring['compile_object'](build, game, path, obj)
        if not stops:
            expect(ring['sections'](obj) == own_sections,
                   f'unmodified {weapon} section sizes differ from build (wrong --game?)')
        host = ring['link'](build, scratch, 'host-cone', control, obj)
        with tempfile.TemporaryDirectory(prefix='tr2-arms-cone-', dir=scratch) as td:
            try:
                targeting(host, pack, Path(td), masks, cone_only=True)
                stopped = None
            except RuntimeError as error:
                stopped = str(error)
        ok = (stopped == CONE_MESSAGE) if stops else stopped is None
        print(('PASS' if ok else 'FAIL') + f' target check, {name}' + (' (must stop by name)' if stops else ''))
        expect(ok, f'target check, {name}: {stopped!r}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('wall', type=Path)
    parser.add_argument('--cone-mutants', type=Path, metavar='HOST_BUILD_DIR',
                        help='also run the target check against mutated copies of lara_weapon.c')
    parser.add_argument('--game', default='1', choices=('1', '2'),
                        help='TR64_GAME used to compile HOST_BUILD_DIR (default 1)')
    args = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    control(args.host, args.wall, os.environ['TMPDIR'])
    if args.cone_mutants:
        cone_mutants(args.cone_mutants, args.wall, Path(os.environ['TMPDIR']) / 'tr2-arms-cone', args.game)
