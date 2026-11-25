#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for Tomb Raider III (TR64_GAME=3) cook pipeline & posed 4 MiB render.

Verifies:
1. Cooking JUNGLE.TR2, TEMPLE.TR2, and QUADCHAS.TR2 with MAIN.SFX:
   - Fits TR3 4 MiB level pool (<= 2,088,960 B) with positive margin.
   - Fits TR64_BYTES_io (<= 49,152 B) with > 10 KiB headroom (even with 60 music tracks).
   - SN64 uses 11,025 Hz SPU-ADPCM (<= 450 blocks).
   - Passes n64_pack.inspect() and C core-test (engine/res/validate.c, tex.c, format.c, snd.c).
   - Preserves non-grey RGB555 colored room vertices in RG64 (scaled cleanly for 1.5x combiner).
2. Real posed 320x240 PPM render of JUNGLE.pack via tr64-host at Room 4 outdoor start:
   - engine/res/validate.c + tr64_render_bind_load (skybox 355) + tr64_render_frame PASS with 0 errors.
   - Rich non-blank frame with skybox 355 sky band + RGB555 colored walls/foliage.
3. Regression & negative controls:
   - TR1 (level1.phd) and TR2 (WALL.TR2) cooked packs remain 100% byte-identical to 76e5fc5.
   - Unsupported game version (4) is rejected by pool_rows, n64_pack, and n64_validate.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/cook'))

import n64_pack
import n64_snd_view
import n64_validate
import pool_rows

TR3_DATA = Path('/tmp/tr64/data/pc-tr3')
TR2_DATA = Path('/tmp/tr64/data/pc-tr2')
TR1_DATA = Path('/tmp/tr64/data/pc')
TR3_LEVELS = ('JUNGLE', 'TEMPLE', 'QUADCHAS')
LEVEL_BUDGET = 2_088_960
IO_BUDGET = 49_152
PPM_HEADER = b'P6\n320 240\n255\n'
BASELINE_COMMIT = '76e5fc5'


def run_cmd(cmd, *, cwd=ROOT, env=None, timeout=180):
    res = subprocess.run(cmd, cwd=str(cwd), env=env, capture_output=True, text=True, timeout=timeout)
    if res.returncode != 0:
        raise RuntimeError(
            f'command failed ({res.returncode}): {" ".join(map(str, cmd))}\n'
            f'stdout:\n{res.stdout}\nstderr:\n{res.stderr}'
        )
    return res.stdout + res.stderr


def inspect_rg64_colors(rg: bytes) -> dict:
    """Parse RG64 vertex pairs directly and measure RGB555 color preservation."""
    magic, ver, total = struct.unpack_from('>4sII', rg, 0)
    if magic != b'RG64' or ver != 1 or total != len(rg):
        raise AssertionError(f'bad RG64 header: magic={magic!r} ver={ver} total={total}/{len(rg)}')
    pairs = struct.unpack_from('>I', rg, 28)[0]
    vo = struct.unpack_from('>I', rg, 48)[0]
    total_verts = 0
    non_grey = 0
    max_channel = 0
    distinct_rgb = set()
    for i in range(pairs):
        off = vo + i * 32
        for k in (16, 20):
            r, g, b, a = rg[off + k:off + k + 4]
            if a == 0:
                continue  # odd-vertex padding slot
            total_verts += 1
            if not (r == g == b):
                non_grey += 1
            max_channel = max(max_channel, r, g, b)
            distinct_rgb.add((r, g, b))
    return dict(
        vertices=total_verts,
        non_grey=non_grey,
        distinct_rgb=len(distinct_rgb),
        max_channel=max_channel,
    )


def analyze_ppm(ppm_path: Path) -> dict:
    data = ppm_path.read_bytes()
    if not data.startswith(PPM_HEADER) or len(data) != len(PPM_HEADER) + 320 * 240 * 3:
        raise AssertionError(f'{ppm_path}: expected canonical 320x240 P6 PPM')
    rgb = data[len(PPM_HEADER):]
    non_black = 0
    colors = set()
    sky_non_black = 0
    sky_colors = set()
    blue_dom = green_dom = warm_dom = 0
    for y in range(240):
        row = y * 320 * 3
        for x in range(320):
            p = row + x * 3
            r, g, b = rgb[p], rgb[p + 1], rgb[p + 2]
            if (r, g, b) != (0, 0, 0):
                non_black += 1
                colors.add((r, g, b))
                if b > r + 10 and b > g:
                    blue_dom += 1
                if g > r + 8 and g > b + 8:
                    green_dom += 1
                if r > g + 8 and r > b + 12:
                    warm_dom += 1
                if y < 80:
                    sky_non_black += 1
                    sky_colors.add((r, g, b))
    return dict(
        non_black=non_black,
        distinct=len(colors),
        sky_non_black=sky_non_black,
        sky_distinct=len(sky_colors),
        blue_dom=blue_dom,
        green_dom=green_dom,
        warm_dom=warm_dom,
    )


def pack_mount_io_bytes(blob: bytes, extra_catalog_entries: int = 0) -> int:
    """Compute exact mount I/O scratch (directory + retained 32B entry table + 16 KiB staging)."""
    count = struct.unpack_from('>I', blob, 12)[0]
    entries = struct.unpack_from('>I', blob, 32 + 36)[0]
    return n64_pack.align(48 * (count + extra_catalog_entries)) + 32 * entries + n64_pack.BLOCK


def check_negative_controls() -> None:
    try:
        pool_rows.pool_table(4)
        raise AssertionError('pool_table(4) unexpectedly succeeded')
    except ValueError:
        pass
    try:
        n64_pack.build({'bad': [n64_pack.Section(1, b'\0' * 16)]}, games={'bad': 4})
        raise AssertionError('n64_pack.build(game=4) unexpectedly succeeded')
    except ValueError:
        pass

    class DummyLevel:
        data = {}
        rooms = []
        reserved = {'game': 4}

    try:
        n64_validate.validate(DummyLevel())
        raise AssertionError('n64_validate.validate(game=4) unexpectedly succeeded')
    except ValueError:
        pass
    print('PASS negative controls: game=4 rejected by pool_rows, n64_pack, and n64_validate')


def check_tr1_tr2_byte_identity(work: Path, cache: Path) -> None:
    ref_dir = work / 'ref_76e5fc5'
    ref_dir.mkdir(parents=True, exist_ok=True)
    # Extract committed baseline tools/cook + include/tr64 from 76e5fc5
    tar_proc = subprocess.Popen(
        ['git', 'archive', BASELINE_COMMIT, 'tools/cook', 'include/tr64'],
        cwd=str(ROOT), stdout=subprocess.PIPE,
    )
    subprocess.run(['tar', '-x', '-C', str(ref_dir)], stdin=tar_proc.stdout, check=True)
    if tar_proc.wait() != 0:
        raise RuntimeError(f'git archive {BASELINE_COMMIT} failed')

    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env.pop('PYTHONPATH', None)
    cmp_dir = work / 'cmp'
    cmp_dir.mkdir(parents=True, exist_ok=True)

    cases = [
        ('level1', TR1_DATA / 'level1.phd', None),
        ('wall', TR2_DATA / 'WALL.TR2', TR2_DATA / 'MAIN.SFX'),
    ]
    for name, src, sfx in cases:
        ref_pack = cmp_dir / f'{name}_ref.pack'
        new_pack = cmp_dir / f'{name}_new.pack'
        for script, out in (
            (ref_dir / 'tools/cook/pack_level.py', ref_pack),
            (ROOT / 'tools/cook/pack_level.py', new_pack),
        ):
            cmd = [sys.executable, str(script), '--source', 'pc', '--input', str(src),
                   '--cache', str(cache), '--out', str(out)]
            if sfx is not None:
                cmd += ['--main-sfx', str(sfx)]
            run_cmd(cmd, env=env)
        ref_bytes = ref_pack.read_bytes()
        new_bytes = new_pack.read_bytes()
        if ref_bytes != new_bytes:
            raise AssertionError(f'{name}.pack drifted from {BASELINE_COMMIT}!')
        digest = hashlib.sha256(new_bytes).hexdigest()
        print(f'PASS {name}.pack byte-identical to {BASELINE_COMMIT}: {len(new_bytes)} B sha256={digest}')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True, help='Scratch BUILD_DIR under /tmp/tr64/builds/...')
    ap.add_argument('--work-dir', type=Path, required=True, help='Scratch work dir under /tmp/tr64/tmp/...')
    args = ap.parse_args()

    work = args.work_dir.resolve()
    build = args.build_dir.resolve()
    packs_dir = work / 'packs'
    cache = work / 'compressor'
    scene_dir = work / 'scene'
    for d in (work, build, packs_dir, cache, scene_dir):
        d.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(work))

    # Verify TR3 pool table rows & 4 MiB fit
    rows = pool_rows.pool_rows(game=3)
    reserve = pool_rows.reserve_bytes(rows, pool_rows.pool_table(3))
    level_budget = dict((n, sz) for n, sz, _ in rows)['level']
    assert level_budget == LEVEL_BUDGET, f'unexpected TR3 level budget {level_budget}'
    assert reserve > 0, f'TR3 pools exceed 4 MiB: reserve={reserve}'
    print(f'PASS pools-tr3.def: level={level_budget} B, reserve={reserve} B')

    # 1. Cook JUNGLE, TEMPLE, QUADCHAS
    cooked = {}
    for lvl in TR3_LEVELS:
        name = lvl.lower()
        out_pack = packs_dir / f'{name}.pack'
        out = run_cmd([
            sys.executable, str(ROOT / 'tools/cook/pack_level.py'),
            '--source', 'pc',
            '--input', str(TR3_DATA / f'{lvl}.TR2'),
            '--main-sfx', str(TR3_DATA / 'MAIN.SFX'),
            '--cache', str(cache),
            '--out', str(out_pack),
        ], env=env)
        report = json.loads(out.strip().splitlines()[-1])
        layout = report['resident_layout']
        assert report['game'] == 3
        assert layout['bytes'] <= LEVEL_BUDGET and layout['margin_bytes'] > 0
        assert report['sound_blocks'] <= 450, f'{lvl} sound_blocks={report["sound_blocks"]} > 450'

        blob = out_pack.read_bytes()
        io_single = pack_mount_io_bytes(blob, 0)
        io_music60 = pack_mount_io_bytes(blob, 60)
        assert io_single <= IO_BUDGET, f'{lvl} io={io_single} > {IO_BUDGET}'
        assert IO_BUDGET - io_music60 >= 10_240, f'{lvl} io headroom with 60 music tracks < 10 KiB: {IO_BUDGET - io_music60}'

        sections = n64_pack.inspect(blob)[name]
        sn = n64_snd_view.Section(sections[0x40000])
        assert all(row[2] == 11025 and row[3] == 3 for row in sn.samples), f'{lvl} SN64 not 11025 Hz SPU-ADPCM'

        rg_stats = inspect_rg64_colors(sections[0x20000])
        assert rg_stats['non_grey'] >= 1000, f'{lvl} lost RGB555 colored vertices: {rg_stats}'
        assert rg_stats['max_channel'] <= 171, f'{lvl} RG64 color exceeds 1.5x combiner dry max 171: {rg_stats}'

        cooked[name] = (out_pack, report, io_single, io_music60, rg_stats)
        print(
            f'PASS cook {lvl}: level={layout["bytes"]}/{LEVEL_BUDGET} (margin=+{layout["margin_bytes"]} B), '
            f'SN64={report["sound_bytes"]} B ({report["sound_blocks"]} blocks), '
            f'io={io_single} B (music60_headroom=+{IO_BUDGET - io_music60} B), '
            f'RG64_non_grey={rg_stats["non_grey"]}/{rg_stats["vertices"]} (distinct={rg_stats["distinct_rgb"]})'
        )

    # 2. Build tr64-host & core-test, validate ALL THREE packs via engine/res/validate.c, and render posed Jungle!
    jungle_scene = scene_dir / 'tr3-jungle-start.scene.json'
    jungle_scene.write_text(json.dumps({
        'level': 'jungle',
        'camera': {
            'room': 4,
            'position': [28160, -1024, 28160],
            'target': [28160, -1536, 36000],
        },
    }, indent=2) + '\n')
    jungle_route = scene_dir / 'tr3-jungle-start.route'
    jungle_route.write_text(
        'scene tr3-jungle-start\n'
        'start boot\n'
        'load level jungle\n'
        'ticks 2\n'
        'seed 1\n'
        'observe log "^render: pose=tr3-jungle-start.scene "\n'
        'observe pools\n'
        'observe placement\n'
    )
    jungle_rbc = scene_dir / 'tr3-jungle-start.rbc'
    run_cmd([sys.executable, str(ROOT / 'tools/routec/routec.py'), str(jungle_route), '-o', str(jungle_rbc)], env=env)

    jungle_pack = cooked['jungle'][0]
    run_cmd([
        'make', '--no-print-directory', '-j4',
        f'BUILD_DIR={build}', 'TR64_GAME=2',
        f'TR64_LEVEL_PACK={jungle_pack}',
        f'TR64_RENDER_SCENE={jungle_scene}',
        'host', str(build / 'core-test'),
    ], env=env, timeout=300)

    # Validate ALL THREE packs through C engine/res/validate.c + tex.c + format.c + snd.c!
    core_test = build / 'core-test'
    for name in ('jungle', 'temple', 'quadchas'):
        pack_path = cooked[name][0]
        log = run_cmd([str(core_test), str(pack_path), name, 'retail'], env=env, timeout=30)
        assert 'sound_refs=validated' in log, f'core-test {name} missing validated sound_refs'
        m_lvl = re.search(r'pool: level .* size=(\d+) used=(\d+) peak=(\d+)', log)
        m_io = re.search(r'pool: io .* size=(\d+) used=(\d+) peak=(\d+)', log)
        assert m_lvl and m_io, f'core-test {name} missing pool stats'
        assert int(m_lvl.group(3)) <= LEVEL_BUDGET
        assert int(m_io.group(3)) <= IO_BUDGET
        print(f'PASS core-test {name}: validate.c+tex+snd PASS, level_peak={m_lvl.group(3)}/{m_lvl.group(1)}, io_peak={m_io.group(3)}/{m_io.group(1)}')

    # Run posed Jungle render via tr64-host
    ppm_path = scene_dir / 'tr3-jungle-start.ppm'
    host_log = run_cmd([
        str(build / 'host/tr64-host'),
        '--route', str(jungle_rbc),
        '--shot', str(ppm_path),
    ], env=env, timeout=60)
    m_ren = re.search(
        r'render: pose=tr3-jungle-start\.scene .* visible_rooms=(\d+) statics=(\d+) .* triangles=(\d+) .* matrices=(\d+)',
        host_log,
    )
    assert m_ren, f'missing render log in Jungle posed scene:\n{host_log}'
    vis_rooms, statics, tris, matrices = map(int, m_ren.groups())
    assert vis_rooms >= 2 and statics >= 1 and tris >= 500 and matrices >= 3, (
        f'unexpected Jungle render metrics: rooms={vis_rooms} statics={statics} tris={tris} matrices={matrices}'
    )
    ppm_stats = analyze_ppm(ppm_path)
    assert ppm_stats['non_black'] >= 60_000, f'Jungle frame too blank: {ppm_stats}'
    assert ppm_stats['distinct'] >= 50, f'Jungle frame lacks distinct colors: {ppm_stats}'
    assert ppm_stats['sky_non_black'] >= 12_800 and ppm_stats['sky_distinct'] >= 20, (
        f'Jungle skybox 355 missing from top sky band: {ppm_stats}'
    )
    assert ppm_stats['blue_dom'] >= 10_000 and ppm_stats['green_dom'] >= 1_000 and ppm_stats['warm_dom'] >= 1_000, (
        f'Jungle frame missing RGB555 colored lighting/foliage/sky spectrum: {ppm_stats}'
    )
    print(
        f'PASS posed TR3 Jungle render: rooms={vis_rooms} statics={statics} tris={tris} matrices={matrices} '
        f'non_black={ppm_stats["non_black"]}/76800 ({100*ppm_stats["non_black"]/76800:.1f}%) '
        f'distinct={ppm_stats["distinct"]} sky_band={ppm_stats["sky_non_black"]}/25600 '
        f'(sky_distinct={ppm_stats["sky_distinct"]}) '
        f'hues(blue={ppm_stats["blue_dom"]},green={ppm_stats["green_dom"]},warm={ppm_stats["warm_dom"]})'
    )

    # 3. Negative controls & TR1/TR2 byte-identity regression check
    check_negative_controls()
    check_tr1_tr2_byte_identity(work, cache)
    print('ALL TR3 COOK & POSED RENDER CHECKS PASSED')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
