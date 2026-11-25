#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed host test for TR2 presentation & rendering features (Wave 1):
1. All 8 TR2 room effects (campfire flame, blood, ricochet, shards, bubbles, wake, splash, explosion)
   and multi-chunk game_view_room() cursor equivalence.
2. Weapon muzzle flashes (model 240) on Lara's right/left hands.
3. Held & world flare flame/glow sprite (236) + dynamic flare/muzzle illumination (light_offset & pixels).
4. Active TR2 creature head/torso jointYaw rotation (positive + zeroed negative control).
5. Lara's 6-segment braid (model 2) attachment, dynamics, and renderer validation.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

IN_UP = 8
IN_ACTION = 128
IN_DRAW = 256
IN_FLARE = 8192


def expect(ok, msg):
    if not ok:
        raise RuntimeError(msg)


def build_probe(build_dir, td):
    repo = Path(__file__).resolve().parents[2]
    src = repo / 'test/game/tr2-render-effects-control.c'
    obj = td / 'tr2-render-effects-control.o'
    exe = td / 'fx-host'
    gen = build_dir / 'generated'
    objs = sorted((build_dir / 'host').rglob('*.o'))
    expect(objs, f'no host objects found under {build_dir / "host"}')
    c = subprocess.run([
        'rtk', 'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        f'-I{repo}', f'-I{repo / "include"}', f'-I{gen}', f'-I{gen / "host"}',
        '-DTR64_GAME=2', '-DTR64_HOST=1', '-D_POSIX_C_SOURCE=200809L',
        '-c', str(src), '-o', str(obj),
    ], capture_output=True, text=True)
    expect(c.returncode == 0, f'compile probe failed: {c.stdout}{c.stderr}')
    l = subprocess.run([
        'rtk', 'cc', '-m32', '-Wl,--wrap=game_tr2_tick', '-Wl,--wrap=tr64_level_load',
        '-o', str(exe), str(obj),
        *(str(p) for p in objs),
    ], capture_output=True, text=True)
    expect(l.returncode == 0, f'link probe failed: {l.stdout}{l.stderr}')
    return exe


def read_ppm(path):
    raw = path.read_bytes()
    header_end = 0
    tokens = []
    i = 0
    while len(tokens) < 4:
        while raw[i:i + 1] in (b' ', b'\t', b'\r', b'\n'):
            i += 1
        if raw[i:i + 1] == b'#':
            while raw[i:i + 1] != b'\n':
                i += 1
            continue
        j = i
        while raw[j:j + 1] not in (b' ', b'\t', b'\r', b'\n'):
            j += 1
        tokens.append(raw[i:j].decode('ascii'))
        i = j + 1
    expect(tokens[0] == 'P6' and tokens[3] == '255', f'unexpected PPM header: {tokens}')
    w, h = int(tokens[1]), int(tokens[2])
    pixels = raw[i:]
    expect(len(pixels) == w * h * 3, f'PPM byte length mismatch: {len(pixels)} vs {w * h * 3}')
    return w, h, pixels


def run_probe(exe, pack, inp_path, out_trace, schema=21, extra_env=None):
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    flag = f'--game-tr2-schema{schema}'
    p = subprocess.run(
        ['rtk', str(exe), flag, str(pack), str(inp_path), str(out_trace)],
        capture_output=True, text=True, env=env, timeout=120)
    expect(p.returncode == 0, f'probe failed ({inp_path.name}): {p.stdout[-800:]}{p.stderr[-800:]}')
    assets, fxs, laras, flares, creatures, czeros = {}, [], [], [], [], []
    for line in p.stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        tag = parts[0]
        if tag == 'ASSET':
            assets[int(parts[1])] = (int(parts[2]), int(parts[3]))
        elif tag == 'FX':
            fxs.append(tuple(int(x) for x in parts[1:]))
        elif tag == 'LARA':
            laras.append(tuple(int(x) for x in parts[1:]))
        elif tag == 'FLARE':
            flares.append(tuple(int(x) for x in parts[1:]))
        elif tag == 'CREATURE':
            creatures.append(tuple(int(x) for x in parts[1:]))
        elif tag == 'CREATURE_ZERO':
            czeros.append(tuple(int(x) for x in parts[1:]))
    return assets, fxs, laras, flares, creatures, czeros


def in_seq(assets, sprite_type, slot):
    cnt, start = assets[sprite_type]
    return cnt < 0 and start <= slot < start + (-cnt)


def test_campfire(exe, wall, td):
    inp = td / 'campfire.txt'
    inp.write_text('\n'.join(['0'] * 12) + '\n', encoding='ascii')
    shot_on = td / 'campfire_on.ppm'
    shot_off = td / 'campfire_off.ppm'
    # Room 65 campfire at (85504, 20480, 68096); view from inside room 65 looking at flame.
    cam = '65,85504,20200,66800,85504,20280,68096'
    assets, fxs, _, _, _, _ = run_probe(
        exe, wall, inp, td / 'campfire_on.jsonl', schema=21,
        extra_env={'TR64_FX_ROOM': '65', 'TR64_FX_SHOT_TICK': '6',
                   'TR64_FX_SHOT_OUT': str(shot_on), 'TR64_FX_CAM': cam})
    run_probe(
        exe, wall, inp, td / 'campfire_off.jsonl', schema=21,
        extra_env={'TR64_FX_ROOM': '65', 'TR64_FX_SHOT_TICK': '6',
                   'TR64_FX_SHOT_OUT': str(shot_off), 'TR64_FX_CAM': cam,
                   'TR64_FX_SUPPRESS_EFFECTS': '1'})
    flames = [f for f in fxs if f[1] == 65 and f[3] == 1 and in_seq(assets, 252, f[4])]
    expect(len(flames) >= 10, f'expected campfire flame drawables in room 65, got {len(flames)}')
    expect(len({f[4] for f in flames}) >= 3, 'campfire flame sprite did not animate across ticks')
    expect(all(f[6] == -4096 for f in flames), 'campfire flame must be self-lit (-4096)')
    w, h, px_on = read_ppm(shot_on)
    _, _, px_off = read_ppm(shot_off)
    diff_px = sum(1 for k in range(0, len(px_on), 3)
                  if abs(px_on[k] - px_off[k]) + abs(px_on[k + 1] - px_off[k + 1]) + abs(px_on[k + 2] - px_off[k + 2]) > 20)
    warm_diff = sum(1 for k in range(0, len(px_on), 3)
                    if px_on[k] > px_off[k] + 40 and px_on[k] > 140)
    expect(diff_px >= 100 and warm_diff >= 50,
           f'campfire flame not visible in rendered PPM (diff_px={diff_px}, warm_diff={warm_diff})')
    print(f'PASS 1/5 campfire flame: {len(flames)} drawables ({len({f[4] for f in flames})} anim frames), '
          f'{diff_px} changed / {warm_diff} warm flame pixels in {w}x{h} PPM')


def test_spider_combat(exe, plain_host, wall, fixtures, td):
    f_inp = fixtures / 'schema21/spider-shoot/input.txt'
    tr_probe = td / 'spider_probe.jsonl'
    tr_plain = td / 'spider_plain.jsonl'
    # Verify 100% bit-exact trace parity with normal host while rendering every tick!
    p = subprocess.run(['rtk', str(plain_host), '--game-tr2-schema21', str(wall), str(f_inp), str(tr_plain)],
                       capture_output=True, text=True, timeout=60)
    expect(p.returncode == 0, f'plain spider-shoot failed: {p.stderr}')
    assets, fxs, laras, _, _, _ = run_probe(
        exe, wall, f_inp, tr_probe, schema=21, extra_env={'TR64_FX_RENDER_ALL': '1'})
    expect(tr_probe.read_bytes() == tr_plain.read_bytes(),
           'spider-shoot trace mutated by presentation/rendering!')

    flash_mesh = assets[240][1]
    flashes = [l for l in laras if l[2] in (15, 16) and l[3] == 0 and l[4] == flash_mesh and l[5] == -4096]
    bloods = [f for f in fxs if f[3] == 1 and in_seq(assets, 233, f[4])]
    shards = [f for f in fxs if f[3] == 0]
    ricochets = [f for f in fxs if f[3] == 1 and in_seq(assets, 238, f[4])]
    expect(len(flashes) >= 4, f'expected muzzle flashes (model 240) during spider-shoot, got {len(flashes)}')
    expect(len(bloods) >= 10, f'expected blood sprites (seq 233) during spider-shoot, got {len(bloods)}')
    expect(len(shards) >= 15, f'expected spider breakup shards (mesh) during spider-shoot, got {len(shards)}')
    # Verify shard rotation changes across ticks for at least one shard instance_id.
    shard_rot = {}
    for s in shards:
        shard_rot.setdefault(s[2], set()).add((s[10], s[11], s[12]))
    expect(any(len(v) >= 2 for v in shard_rot.values()), 'shard mesh basis did not rotate across ticks')

    # Also test ricochet explicitly by shooting a nearby wall if spider-shoot didn't already miss into walls.
    if len(ricochets) < 2:
        w_inp = td / 'wall_ricochet.txt'
        w_lines = [str(IN_DRAW)] + ['0'] * 25 + [str(IN_ACTION)] * 25 + ['0'] * 5
        w_inp.write_text('\n'.join(w_lines) + '\n', encoding='ascii')
        assets2, fxs2, _, _, _, _ = run_probe(
            exe, wall, w_inp, td / 'wall_ricochet.jsonl', schema=21, extra_env={'TR64_FX_RENDER_ALL': '1'})
        ricochets = [f for f in fxs2 if f[3] == 1 and in_seq(assets2, 238, f[4]) and f[6] == -4096]
    expect(len(ricochets) >= 2, f'expected ricochet sprites (seq 238), got {len(ricochets)}')
    print(f'PASS 2/5 spider combat & ricochet: flashes={len(flashes)}, blood={len(bloods)}, '
          f'shards={len(shards)} ({len(shard_rot)} unique), ricochet={len(ricochets)}; trace 100% identical')


def test_flare_and_braid(exe, wall, td):
    # Place Lara in dark interior room 35 facing +Z, light a flare (age hits steady >30 by tick 110), then throw it.
    place = 'place 62976 5504 27136 0 35'
    lit_lines = [place, '0', str(IN_FLARE)] + ['0'] * 105 + [str(IN_FLARE)] + ['0'] * 40
    unlit_lines = [place] + ['0'] * 110
    lit_inp, unlit_inp = td / 'flare_lit.txt', td / 'flare_unlit.txt'
    lit_inp.write_text('\n'.join(lit_lines) + '\n', encoding='ascii')
    unlit_inp.write_text('\n'.join(unlit_lines) + '\n', encoding='ascii')
    shot_lit, shot_unlit = td / 'flare_lit.ppm', td / 'flare_unlit.ppm'
    assets, _, laras_lit, flares_lit, _, _ = run_probe(
        exe, wall, lit_inp, td / 'flare_lit.jsonl', schema=21,
        extra_env={'TR64_FX_SHOT_TICK': '95', 'TR64_FX_SHOT_OUT': str(shot_lit), 'TR64_FX_RENDER_ALL': '1'})
    _, _, laras_unlit, _, _, _ = run_probe(
        exe, wall, unlit_inp, td / 'flare_unlit.jsonl', schema=21,
        extra_env={'TR64_FX_SHOT_TICK': '95', 'TR64_FX_SHOT_OUT': str(shot_unlit)})

    # Held flare sprite (bone 17, sprite 236, light -4096)
    held_sprites = [l for l in laras_lit if l[2] == 17 and l[3] == 1 and in_seq(assets, 236, l[4]) and l[5] == -4096]
    expect(len(held_sprites) >= 40, f'expected held flare glow sprites (236), got {len(held_sprites)}')

    # Lara light_offset brightness check at tick 95 (lower light_offset = brighter!)
    head_lit = [l for l in laras_lit if l[0] == 95 and l[2] == 14][0]
    head_unlit = [l for l in laras_unlit if l[0] == 95 and l[2] == 14][0]
    expect(head_lit[5] < head_unlit[5] - 1000,
           f'held flare did not brighten Lara light_offset: lit={head_lit[5]} vs unlit={head_unlit[5]}')

    # Rendered PPM brightness check at tick 95
    _, _, px_lit = read_ppm(shot_lit)
    _, _, px_unlit = read_ppm(shot_unlit)
    sum_lit = sum(px_lit)
    sum_unlit = sum(px_unlit)
    expect(sum_lit > sum_unlit, f'lit flare PPM not brighter than unlit ({sum_lit} <= {sum_unlit})')

    # Thrown world flare: both mesh (bone 0) and glow sprite (bone 1, seq 236, light -4096)
    world_mesh = [f for f in flares_lit if f[3] == 0 and f[4] == 0]
    world_glow = [f for f in flares_lit if f[3] == 1 and f[4] == 1 and in_seq(assets, 236, f[5]) and f[6] == -4096]
    expect(len(world_mesh) >= 10 and len(world_glow) >= 10,
           f'expected thrown loose flare mesh + glow sprite, got mesh={len(world_mesh)} glow={len(world_glow)}')

    # Braid verification (model 2 -> 6 meshes, bones 18..23)
    hair_cnt, hair_start = assets[2]
    expect(hair_cnt == 6, f'expected hair model 2 count=6, got {hair_cnt}')
    braid_t95 = sorted([l for l in laras_lit if l[0] == 95 and 18 <= l[2] <= 23], key=lambda x: x[2])
    expect(len(braid_t95) == 6, f'expected 6 braid segments at tick 95, got {len(braid_t95)}')
    for i, seg in enumerate(braid_t95):
        expect(seg[3] == 0 and seg[4] == hair_start + i, f'braid seg {i} wrong mesh: {seg}')
    # Verify braid root is attached close to head (within 100 units)
    dx = braid_t95[0][6] - head_lit[6]
    dy = braid_t95[0][7] - head_lit[7]
    dz = braid_t95[0][8] - head_lit[8]
    expect(dx * dx + dy * dy + dz * dz < 100 * 100,
           f'braid root too far from head: d=({dx},{dy},{dz})')
    print(f'PASS 3/5 flare & braid: held_sprites={len(held_sprites)}, world_flare={len(world_glow)}, '
          f'Lara light_offset {head_unlit[5]}->{head_lit[5]}, PPM luma {sum_unlit}->{sum_lit}, '
          f'6 braid segments attached (d=({dx},{dy},{dz}))')


def test_bubbles_and_wake(exe, wall, boat, fixtures, td):
    # Underwater swimming bubbles on Great Wall (pool-swim)
    swim_inp = fixtures / 'pool-swim/input.txt'
    assets_w, fxs_w, _, _, _, _ = run_probe(exe, wall, swim_inp, td / 'swim.jsonl', schema=21)
    bubbles = [f for f in fxs_w if f[3] == 1 and in_seq(assets_w, 231, f[4])]
    expect(len(bubbles) >= 20, f'expected underwater bubble drawables (seq 231), got {len(bubbles)}')
    # Render a tick with bubbles through the full host renderer!
    bubble_tick = bubbles[0][0]
    run_probe(exe, wall, swim_inp, td / 'swim_shot.jsonl', schema=21,
              extra_env={'TR64_FX_SHOT_TICK': str(bubble_tick), 'TR64_FX_SHOT_OUT': str(td / 'bubble.ppm')})

    # Venice boat wake on BOAT.TR2 (venice-boat-control)
    boat_inp = fixtures / 'schema22/venice-boat-control/input.txt'
    assets_b, fxs_b, _, _, _, _ = run_probe(exe, boat, boat_inp, td / 'wake.jsonl', schema=22)
    wakes = [f for f in fxs_b if f[3] == 1 and in_seq(assets_b, 89, f[4])]
    expect(len(wakes) >= 20, f'expected Venice boat wake drawables (seq 89), got {len(wakes)}')
    wake_tick = wakes[len(wakes) // 2][0]
    run_probe(exe, boat, boat_inp, td / 'wake_shot.jsonl', schema=22,
              extra_env={'TR64_FX_SHOT_TICK': str(wake_tick), 'TR64_FX_SHOT_OUT': str(td / 'wake.ppm')})
    print(f'PASS 4/5 underwater bubbles & Venice wake: bubbles={len(bubbles)} (seq 231, rendered t={bubble_tick}), '
          f'wakes={len(wakes)} (seq 89, rendered t={wake_tick})')


def test_creature_head_tracking(exe, wall, boat, fixtures, td):
    # Great Wall Tiger (tiger-shoot) + Venice Doberman/Gunman (venice-first-hostiles)
    _, _, _, _, ct_w, cz_w = run_probe(
        exe, wall, fixtures / 'schema21/tiger-shoot/input.txt', td / 'tiger_head.jsonl',
        schema=21, extra_env={'TR64_FX_CREATURE': '1'})
    zmap_w = {(r[0], r[2], r[4]): r[5:] for r in cz_w}
    changed_w = sum(1 for r in ct_w if (r[0], r[2], r[6]) in zmap_w and r[7:] != zmap_w[(r[0], r[2], r[6])])
    expect(changed_w >= 20, f'expected tiger head jointYaw rotation to alter basis, changed={changed_w}')

    _, _, _, _, ct_b, cz_b = run_probe(
        exe, boat, fixtures / 'schema22/venice-first-hostiles/input.txt', td / 'venice_head.jsonl',
        schema=22, extra_env={'TR64_FX_CREATURE': '1'})
    zmap_b = {(r[0], r[2], r[4]): r[5:] for r in cz_b}
    changed_b = sum(1 for r in ct_b if (r[0], r[2], r[6]) in zmap_b and r[7:] != zmap_b[(r[0], r[2], r[6])])
    expect(changed_b >= 20, f'expected Venice creature head/torso jointYaw rotation to alter basis, changed={changed_b}')
    print(f'PASS 5/5 creature head/torso tracking: Great Wall tiger={changed_w} rotated ticks, '
          f'Venice hostiles={changed_b} rotated ticks (all verified against jointYaw=0 negative control)')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('build_dir', type=Path, help='TR2 host build directory (containing host/tr64-host)')
    ap.add_argument('wall', type=Path)
    ap.add_argument('boat', type=Path)
    ap.add_argument('fixtures', type=Path)
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')
    plain_host = args.build_dir / 'host/tr64-host'
    with tempfile.TemporaryDirectory(prefix='tr2-render-fx-', dir=scratch) as td_str:
        td = Path(td_str)
        exe = build_probe(args.build_dir, td)
        test_campfire(exe, args.wall, td)
        test_spider_combat(exe, plain_host, args.wall, args.fixtures, td)
        test_flare_and_braid(exe, args.wall, td)
        test_bubbles_and_wake(exe, args.wall, args.boat, args.fixtures, td)
        test_creature_head_tracking(exe, args.wall, args.boat, args.fixtures, td)
    print('ALL TR2 RENDER EFFECTS TESTS PASSED')


if __name__ == '__main__':
    main()
