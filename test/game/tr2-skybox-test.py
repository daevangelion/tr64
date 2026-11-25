#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for TR2 outdoor skybox (horizon model 254) rendering.

Runs:
1. Synthetic end-to-end renderer + host GPU/rasterizer controls (no retail data):
   - TR2 outdoor room (flags & 0x0008) draws model 254 with authored rest-frame
     orientation (wall/boat frame words 0x0ff7, 0xfdff + offset (0, 414, 0)),
     exact 1.0 full-bright sky colour, and TR64_GPU_NO_DEPTH (closer skybox
     does not occlude farther room geometry).
   - TR2 indoor room (flags == 0) in the same level submits 0 skybox matrices/
     triangles and leaves sky pixels black (negative control).
   - TR1 (game == 0) never binds or draws model 254 and keeps workspace_bytes
     unchanged.
   - Split-screen (top outdoor, bottom indoor) clips skybox strictly to the
     outdoor half without leaking into the indoor half.
2. Optional real-pack host scene checks (--wall-pack / --boat-pack):
   - Great Wall outdoor start & Venice outdoor start render non-black sky.
   - Great Wall deep indoor room (room 49) does not draw skybox.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PPM_HEADER = b'P6\n320 240\n255\n'

SYNTHETIC_C = r'''/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/load.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/mem.h"
#include "plat/host/gpu-platform.h"

static uint8_t mem_arena[2 * 1024 * 1024] __attribute__((aligned(16)));
static uint32_t mem_used;

void sys_error(const char *fmt, ...) { fputs(fmt, stderr); fputc('\n', stderr); abort(); }
uint32_t sys_time_us(void) { return 0; }
uint32_t sys_profile_ticks(void) { return 0; }
uint32_t sys_profile_us(uint32_t t) { return t; }
void sys_present(const uint16_t *p) { (void)p; }
void log_printf(const char *fmt, ...) { (void)fmt; }
void log_flush(void) { }
int mem_is_booted(void) { return 1; }
void *tr64_mem_alloc(int pool, uint32_t bytes)
{
 (void)pool;
 bytes = (bytes + 15u) & ~15u;
 if (bytes > sizeof(mem_arena) - mem_used) sys_error("test arena exhausted");
 void *p = mem_arena + mem_used;
 mem_used += bytes;
 memset(p, 0, bytes);
 return p;
}
uint32_t game_view_room(uint32_t room, uint32_t *cursor, tr64_game_drawable_t *out, uint32_t cap)
{ (void)room; (void)out; (void)cap; *cursor = TR64_GAME_VIEW_END; return 0; }
uint32_t game_rest_item_view(uint32_t item, tr64_game_drawable_t *out, uint32_t cap)
{ (void)item; (void)out; (void)cap; return 0; }

static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void w32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (24 - 8 * i)); }
static void ws16(uint8_t *p, int32_t v) { w16(p, (uint16_t)v); }

static uint16_t rgb5551(unsigned r5, unsigned g5, unsigned b5)
{ return (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | 1u); }

static void pack_vert_pair(uint8_t *dst,
                           int16_t x0, int16_t y0, int16_t z0, uint8_t c0,
                           int16_t x1, int16_t y1, int16_t z1, uint8_t c1)
{
 memset(dst, 0, 32);
 ws16(dst + 0, x0); ws16(dst + 2, y0); ws16(dst + 4, z0);
 ws16(dst + 8, x1); ws16(dst + 10, y1); ws16(dst + 12, z1);
 dst[16] = dst[17] = dst[18] = c0; dst[19] = 255;
 dst[20] = dst[21] = dst[22] = c1; dst[23] = 255;
}

static void write_geo_header(uint8_t *p, const char *magic, uint32_t bytes,
                             uint32_t objs, uint32_t batches, uint32_t runs,
                             uint32_t faces, uint32_t pairs, uint32_t tris)
{
 uint32_t oo = 64, bo = oo + 32 * objs, ro = bo + 32 * batches;
 uint32_t fo = ro + 24 * runs, vo = fo + 8 * faces;
 memcpy(p, magic, 4);
 w32(p + 4, 1); w32(p + 8, bytes); w32(p + 12, objs);
 w32(p + 16, batches); w32(p + 20, runs); w32(p + 24, faces); w32(p + 28, pairs);
 w32(p + 32, oo); w32(p + 36, bo); w32(p + 40, ro); w32(p + 44, fo); w32(p + 48, vo);
 w32(p + 52, tris); w32(p + 56, 1);
}

static void fail(const char *msg, unsigned a, unsigned b)
{
 fprintf(stderr, "FAIL: %s (%u vs %u)\n", msg, a, b);
 exit(1);
}

int main(void)
{
 /* TX64 with 2 flat colours: 0 = red foreground (28,4,4), 1 = blue sky (4,12,26),
  * plus 1 animation word (0 groups) at offset 96. */
 uint8_t tx[98] __attribute__((aligned(16))) = {0};
 memcpy(tx, "TX64", 4);
 w32(tx + 4, 1); w32(tx + 8, sizeof(tx));
 w32(tx + 32, 2); /* 2 flat colours */
 w32(tx + 36, 1); /* 1 animation word */
 w32(tx + 40, 80); w32(tx + 44, 80); w32(tx + 48, 80);
 w32(tx + 52, 80); w32(tx + 56, 80); w32(tx + 60, 80); w32(tx + 64, 96);
 const uint16_t red5551 = rgb5551(28, 4, 4);
 const uint16_t sky5551 = rgb5551(4, 12, 26);
 w16(tx + 80 + 2, red5551);
 w16(tx + 88 + 2, sky5551);

 /* RG64: 2 identical rooms (room 0 outdoor, room 1 indoor), each with 1 red quad at z=512
  * covering centre screen x in [-64..+64], y in [-64..+64] (pixels 128..192, 88..152). */
 uint8_t rg[64 + 64 + 64 + 48 + 16 + 128] __attribute__((aligned(16))) = {0};
 write_geo_header(rg, "RG64", sizeof(rg), 2, 2, 2, 2, 4, 4);
 for (unsigned r = 0; r < 2; ++r) {
  uint8_t *o = rg + 64 + 32 * r;
  w32(o + 0, r); w32(o + 4, r); w16(o + 8, 1);
  ws16(o + 12, -64); ws16(o + 14, -64); ws16(o + 16, 512);
  ws16(o + 18, 64);  ws16(o + 20, 64);  ws16(o + 22, 512);
  uint8_t *b = rg + 128 + 32 * r;
  ws16(b + 0, -64); ws16(b + 2, -64); ws16(b + 4, 512);
  ws16(b + 6, 64);  ws16(b + 8, 64);  ws16(b + 10, 512);
  w16(b + 12, 4); w16(b + 14, r); w32(b + 16, 256 + 64 * r); w16(b + 20, 1);
  uint8_t *run = rg + 192 + 24 * r;
  w16(run + 0, 0); run[2] = 0; run[3] = TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED;
  w16(run + 4, r); w16(run + 6, 1);
  uint8_t *face = rg + 240 + 8 * r;
  face[0] = 0; face[1] = 1; face[2] = 2; face[3] = 3;
  uint8_t *vp = rg + 256 + 64 * r;
  pack_vert_pair(vp + 0,  -64, -64, 512, 171,   64, -64, 512, 171);
  pack_vert_pair(vp + 32,  64,  64, 512, 171,  -64,  64, 512, 171);
 }

 /* MG64: 1 skybox object (source=108540). Place a huge flat blue quad at local y = +256
  * (which under Rx(+90) maps to world z = +256 — CLOSER than room's z = 512! — proving
  * TR64_GPU_NO_DEPTH skips Z-write so the foreground room quad still draws on top!).
  * Local x in [-1000..+1000], local z in [-1000..+1000] maps to world x in [-1000..+1000],
  * world y in [-1000+414..+1000+414] = [-586..+1414], covering the full 320x240 viewport.
  * Vertex shade c0 = 128 (exact cooked MG64 skybox vertex colour). */
 uint8_t mg[64 + 32 + 32 + 24 + 8 + 64] __attribute__((aligned(16))) = {0};
 write_geo_header(mg, "MG64", sizeof(mg), 1, 1, 1, 1, 2, 2);
 {
  uint8_t *o = mg + 64;
  w32(o + 0, 108540); w32(o + 4, 0); w16(o + 8, 1);
  ws16(o + 12, -1000); ws16(o + 14, 256); ws16(o + 16, -1000);
  ws16(o + 18, 1000);  ws16(o + 20, 256); ws16(o + 22, 1000);
  uint8_t *b = mg + 96;
  ws16(b + 0, -1000); ws16(b + 2, 256); ws16(b + 4, -1000);
  ws16(b + 6, 1000);  ws16(b + 8, 256); ws16(b + 10, 1000);
  w16(b + 12, 4); w16(b + 14, 0); w32(b + 16, 160); w16(b + 20, 1);
  uint8_t *run = mg + 128;
  w16(run + 0, 1); run[2] = 0; run[3] = TR64_GPU_FLAT | TR64_GPU_ONE_CYCLE_ELIGIBLE;
  w16(run + 4, 0); w16(run + 6, 1);
  uint8_t *face = mg + 152;
  /* Positive screen area (Y-down): (-1000, -586) -> (+1000, -586) -> (+1000, +1414) -> (-1000, +1414).
   * Since wy = -lz + 414, top (wy = -586) is lz = +1000 and bottom (wy = +1414) is lz = -1000. */
  face[0] = 0; face[1] = 1; face[2] = 2; face[3] = 3;
  uint8_t *vp = mg + 160;
  pack_vert_pair(vp + 0,  -1000, 256,  1000, 128,   1000, 256,  1000, 128);
  pack_vert_pair(vp + 32,  1000, 256, -1000, 128,  -1000, 256, -1000, 128);
 }

 tr64_core_rooms_t rooms_tbl[2];
 memset(rooms_tbl, 0, sizeof(rooms_tbl));
 rooms_tbl[0].alternate_room.raw = tr64_core_be16(65535u);
 rooms_tbl[0].flags.raw = tr64_core_be16(0x0008u); /* outdoor */
 rooms_tbl[1].alternate_room.raw = tr64_core_be16(65535u);
 rooms_tbl[1].flags.raw = tr64_core_be16(0x0000u); /* indoor */

 tr64_core_mesh_pointers_t mptr[1] = {{ tr64_core_be32(108540u) }};
 tr64_core_models_t model[1];
 memset(model, 0, sizeof(model));
 model[0].type.raw = tr64_core_be32(254u);
 model[0].mesh_count.raw = tr64_core_be16(1u);
 model[0].mesh_start.raw = tr64_core_be16(0u);
 model[0].animation.raw = tr64_core_be16(0u);

 tr64_core_animations_t anim[1];
 memset(anim, 0, sizeof(anim));
 anim[0].frame_offset.raw = tr64_core_be32(0u);
 anim[0].frame_rate = 1;
 anim[0].frame_size = 11;

 /* Exact authored WALL.TR2 / BOAT.TR2 model 254 rest frame words. */
 static const uint16_t raw_fw[11] = {
  0xf7bf, 0x0841, 0xf62c, 0x08f1, 0xf7bf, 0x0841,
  0x0000, 0x019e, 0x0000, 0x0ff7, 0xfdff
 };
 tr64_core_frames_t frames_tbl[11];
 for (unsigned i = 0; i < 11; ++i) frames_tbl[i].raw = tr64_core_be16(raw_fw[i]);

 tr64_level_t level;
 memset(&level, 0, sizeof(level));
 level.ready = 1; level.game = 2; level.textures = tx; level.texture_bytes = sizeof(tx);
#define SET_TBL(name, arr, cnt) \
 level.tables[TR64_CORE_##name - 1] = (tr64_level_table_t){ .data = (const void *)(arr), .count = (cnt), .stride = sizeof((arr)[0]) }
 SET_TBL(rooms, rooms_tbl, 2);
 SET_TBL(mesh_pointers, mptr, 1);
 SET_TBL(models, model, 1);
 SET_TBL(animations, anim, 1);
 SET_TBL(frames, frames_tbl, 11);
#undef SET_TBL

 tr64_gpu_init();
 tr64_gpu_span_t tx_span = {tx, sizeof(tx)}, rg_span = {rg, sizeof(rg)}, mg_span = {mg, sizeof(mg)};
 tr64_geo_t rgeo, mgeo;
 tr64_render_error_t err;
 if (tr64_geo_open_load(&rgeo, rg_span, 0, &err) || tr64_geo_open_load(&mgeo, mg_span, 1, &err))
  fail("geo_open_load", err.code, err.section);

 size_t ws_tr2 = tr64_render_workspace_bytes_load(&level, &rgeo, &mgeo);
 uint8_t work[4096] __attribute__((aligned(16)));
 tr64_render_t r;
 if (tr64_render_bind_load(&r, &level, tx_span, rg_span, mg_span, work, sizeof(work), &err))
  fail("bind_load tr2", err.code, err.offset);
 if (!(r.bound & TR64_RENDER_BOUND_SKYBOX))
  fail("skybox not bound on TR2", r.bound, 3);
 const tr64_skybox_t *sb = skybox_state(&r);
 if (sb->offset[0] != 0 || sb->offset[1] != 414 || sb->offset[2] != 0)
  fail("rest-frame offset mismatch", (unsigned)sb->offset[1], 414);
 int32_t zenith_y = (int32_t)(((int64_t)sb->basis[5] * 2930) >> 14) + sb->offset[1];
 if (zenith_y != -2516)
  fail("rest-frame zenith Y mismatch", (unsigned)zenith_y, (unsigned)-2516);

 const int32_t eye[3] = {0, 0, 0}, look[3] = {0, 0, 65536};
 tr64_render_pose_t pose_out, pose_in;
 if (tr64_render_camera(&pose_out, eye, look, 0, 0, &err) ||
     tr64_render_camera(&pose_in, eye, look, 1, 0, &err))
  fail("camera", err.code, 0);

 /* 1. Outdoor TR2 room 0: skybox draws full-bright blue, room quad draws red on top. */
 tr64_render_counts_t c_out, c_in;
 if (tr64_render_frame(&r, &pose_out, &c_out, &err))
  fail("render outdoor", err.code, err.offset);
 const uint16_t *fb = tr64_gpu_host_pixels();
 if (fb[40 * 320 + 40] != sky5551)
  fail("outdoor sky pixel colour mismatch", fb[40 * 320 + 40], sky5551);
 if (fb[120 * 320 + 160] != red5551)
  fail("foreground room occluded by closer skybox", fb[120 * 320 + 160], red5551);
 if (c_out.gpu.matrix_loads != 2 || c_out.gpu.triangles != 4)
  fail("outdoor counts", c_out.gpu.matrix_loads, c_out.gpu.triangles);

 /* 2. Indoor TR2 room 1 (negative control): zero skybox submission, black background. */
 if (tr64_render_frame(&r, &pose_in, &c_in, &err))
  fail("render indoor", err.code, err.offset);
 if (fb[40 * 320 + 40] != 0)
  fail("indoor sky pixel not black", fb[40 * 320 + 40], 0);
 if (fb[120 * 320 + 160] != red5551)
  fail("indoor foreground mismatch", fb[120 * 320 + 160], red5551);
 if (c_in.gpu.matrix_loads != 1 || c_in.gpu.triangles != 2)
  fail("indoor submitted skybox", c_in.gpu.matrix_loads, c_in.gpu.triangles);

 /* 3. Split-screen: top half outdoor (blue sky), bottom half indoor (pure black sky). */
 tr64_render_pose_t top = pose_out, bot = pose_in;
 tr64_render_half(&top, 0);
 tr64_render_half(&bot, 1);
 tr64_render_counts_t c_split;
 if (tr64_render_split_frame(&r, &top, &bot, &c_split, &err))
  fail("render split", err.code, err.offset);
 if (fb[20 * 320 + 40] != sky5551)
  fail("split top outdoor sky missing", fb[20 * 320 + 40], sky5551);
 if (fb[140 * 320 + 40] != 0)
  fail("split bottom indoor sky leaked", fb[140 * 320 + 40], 0);

 /* 3b. Signed 32-bit overflow in eye + skybox offset must return TR64_RENDER_BOUNDS (no UB). */
 tr64_render_t r_ovf = r;
 r_ovf.world_min[1] = INT32_MAX - 200;
 r_ovf.world_max[1] = INT32_MAX;
 tr64_render_pose_t pose_ovf = pose_out;
 pose_ovf.view.eye[1] = INT32_MAX - 100;
 tr64_render_counts_t c_ovf;
 if (!tr64_render_frame(&r_ovf, &pose_ovf, &c_ovf, &err) || err.code != TR64_RENDER_BOUNDS)
  fail("eye+offset overflow not rejected", err.code, TR64_RENDER_BOUNDS);
 tr64_gpu_frame_end(&c_ovf.gpu);

 /* 4. TR1 (game == 0): skybox never bound, workspace_bytes strictly smaller, sky black. */
 level.game = 0;
 size_t ws_tr1 = tr64_render_workspace_bytes_load(&level, &rgeo, &mgeo);
 if (ws_tr1 >= ws_tr2)
  fail("TR1 workspace bloated by skybox", (unsigned)ws_tr1, (unsigned)ws_tr2);
 if (tr64_render_bind_load(&r, &level, tx_span, rg_span, mg_span, work, sizeof(work), &err))
  fail("bind_load tr1", err.code, err.offset);
 if (r.bound != 1)
  fail("TR1 bound skybox bit", r.bound, 1);
 tr64_render_counts_t c_tr1;
 if (tr64_render_frame(&r, &pose_out, &c_tr1, &err))
  fail("render tr1", err.code, err.offset);
 if (fb[40 * 320 + 40] != 0 || c_tr1.gpu.matrix_loads != 1 || c_tr1.gpu.triangles != 2)
  fail("TR1 drew skybox", fb[40 * 320 + 40], c_tr1.gpu.matrix_loads);

 printf("PASS synthetic skybox: outdoor=%04x(sky)+%04x(fg) mats=%u->%u split=ok tr1_ws=%zu<%zu\n",
        sky5551, red5551, (unsigned)c_out.gpu.matrix_loads, (unsigned)c_in.gpu.matrix_loads,
        ws_tr1, ws_tr2);
 return 0;
}
'''


def run_synthetic(work_dir: Path) -> None:
    gen = work_dir / 'gen'
    gen.mkdir(parents=True, exist_ok=True)
    schema = gen / 'core_schema.h'
    subprocess.run(
        [sys.executable, str(ROOT / 'tools/cook/n64_core.py'), 'schema', '--out', str(schema)],
        check=True,
    )
    src = work_dir / 'skybox_unit.c'
    src.write_text(SYNTHETIC_C, encoding='utf-8')
    exe = work_dir / 'skybox_unit'
    sources = [
        str(src),
        str(ROOT / 'engine/render/bind_load.c'),
        str(ROOT / 'engine/render/geo_load.c'),
        str(ROOT / 'engine/render/texture_load.c'),
        str(ROOT / 'engine/render/texture.c'),
        str(ROOT / 'engine/render/render.c'),
        str(ROOT / 'engine/render/sprite.c'),
        str(ROOT / 'engine/render/items.c'),
        str(ROOT / 'engine/res/view.c'),
        str(ROOT / 'plat/host/gpu.c'),
        str(ROOT / 'plat/host/raster.c'),
    ]
    cc = os.environ.get('HOST_CC', 'cc')
    subprocess.run(
        [cc, '-m32', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
         '-DTR64_HOST=1', f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}',
         *sources, '-o', str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)


def parse_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PPM_HEADER) or len(data) != len(PPM_HEADER) + 320 * 240 * 3:
        raise ValueError(f'{path}: expected canonical 320x240 P6 PPM')
    return 320, 240, data[len(PPM_HEADER):]


def sky_band_stats(ppm: Path, top_rows: int = 80) -> tuple[int, int]:
    w, _, rgb = parse_ppm(ppm)
    non_black = 0
    colours = set()
    for y in range(top_rows):
        row = y * w * 3
        for x in range(w):
            p = row + x * 3
            c = (rgb[p], rgb[p + 1], rgb[p + 2])
            if c != (0, 0, 0):
                non_black += 1
                colours.add(c)
    return non_black, len(colours)


def run_host_route(build_dir: Path, pack: Path, route_text: str, name: str, out_dir: Path, rebuild: bool = True) -> tuple[str, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    route_path = out_dir / f'{name}.route'
    rbc_path = out_dir / f'{name}.rbc'
    ppm_path = out_dir / f'{name}.ppm'
    route_path.write_text(route_text, encoding='utf-8')
    sys.path.insert(0, str(ROOT / 'tools/routec'))
    import routec
    _, rbc = routec.parse(route_path)
    rbc_path.write_bytes(rbc)
    if rebuild:
        subprocess.run(
            ['make', '--no-print-directory', '-j4',
             f'BUILD_DIR={build_dir}', 'TR64_GAME=2', f'TR64_LEVEL_PACK={pack}', 'host'],
            cwd=str(ROOT),
            check=True,
        )
    exe = build_dir / 'host/tr64-host'
    res = subprocess.run(
        [str(exe), '--route', str(rbc_path), '--shot', str(ppm_path)],
        capture_output=True, text=True, check=True, timeout=30,
    )
    return res.stdout + res.stderr, ppm_path


def run_real_packs(build_dir: Path, out_dir: Path, wall_pack: Path | None, boat_pack: Path | None) -> None:
    if wall_pack is not None:
        # Great Wall outdoor start (room 35 looks out over the Great Wall & sunset sky).
        wall_out_route = (
            'scene tr2-skybox-wall-out\nstart boot\nseed 1\n'
            'load level wall\ngame\nhash-schema 21\nticks 2\n'
            'observe log "^game-render: .* matrices=[0-9]+"\n'
        )
        log_out, ppm_out = run_host_route(build_dir, wall_pack, wall_out_route, 'tr2-skybox-wall-out', out_dir)
        non_black, distinct = sky_band_stats(ppm_out, top_rows=90)
        if non_black < 5000 or distinct < 4:
            raise AssertionError(f'Great Wall outdoor sky missing: non_black={non_black}, distinct={distinct}')
        # Great Wall open-air courtyard room 1 (flags=0x0060, NOT ROOM_FLAG_OUTSIDE)
        # looking across the valley under sky-cap rooms 6/8: the skybox horizon band
        # below the sky-cap portal (rows 60..110) must be filled, not scissored to black.
        wall_valley_route = (
            'scene tr2-skybox-wall-valley\nstart boot\nseed 1\n'
            'load level wall\ngame\nhash-schema 21\n'
            'place 1 39000 -256 46000 -32768\nticks 2\n'
            'observe log "^game-render: .* matrices=[0-9]+"\n'
        )
        _, ppm_val = run_host_route(build_dir, wall_pack, wall_valley_route, 'tr2-skybox-wall-valley', out_dir, rebuild=False)
        _, _, val_rgb = parse_ppm(ppm_val)
        horizon_non_black = sum(
            1 for y in range(60, 110) for x in range(320)
            if (val_rgb[(y * 320 + x) * 3], val_rgb[(y * 320 + x) * 3 + 1], val_rgb[(y * 320 + x) * 3 + 2]) != (0, 0, 0)
        )
        if horizon_non_black < 12000:
            raise AssertionError(
                f'Great Wall courtyard horizon scissored/black below sky-cap portal: {horizon_non_black}/16000'
            )
        # Great Wall deep underground indoor room 49 (no outdoor rooms visible -> no skybox).
        wall_in_route = (
            'scene tr2-skybox-wall-in\nstart boot\nseed 1\n'
            'load level wall\ngame\nhash-schema 21\n'
            'place 49 21520 4352 57856 0\nticks 2\n'
            'observe log "^game-render: .* matrices=[0-9]+"\n'
        )
        log_in, ppm_in = run_host_route(build_dir, wall_pack, wall_in_route, 'tr2-skybox-wall-in', out_dir, rebuild=False)
        m_out = re.findall(r'game-render: .* matrices=(\d+)', log_out)
        m_in = re.findall(r'game-render: .* matrices=(\d+)', log_in)
        p_in = re.findall(r'render-phases: .* batches=(\d+) rejected=(\d+) runs=(\d+)', log_in)
        if not m_out or not m_in or not p_in:
            raise AssertionError('Missing game-render/render-phases logs in Great Wall runs')
        in_batches, in_rej, in_runs = map(int, p_in[-1])
        # Room 49 is strictly indoor (2 matrices, 18 batches, 282 runs): drawing the
        # 17-batch / 68-run wall.pack skybox would raise matrices >= 3 and batches >= 35.
        if int(m_in[-1]) != 2 or in_batches >= 35 or in_runs >= 350:
            raise AssertionError(
                f'Great Wall indoor room 49 unexpectedly drew skybox: '
                f'matrices={m_in[-1]} batches={in_batches} runs={in_runs}'
            )
        print(f'PASS wall.pack outdoor: sky_band non_black={non_black}/28800 distinct={distinct} matrices={m_out[-1]}; indoor room 49 matrices={m_in[-1]} batches={in_batches} runs={in_runs}')

    if boat_pack is not None:
        # Venice outdoor start (room 0 canal/skyline).
        boat_out_route = (
            'scene tr2-skybox-boat-out\nstart boot\nseed 1\n'
            'load level boat\ngame\nhash-schema 22\nticks 2\n'
            'observe log "^game-render: .* matrices=[0-9]+"\n'
        )
        log_boat, ppm_boat = run_host_route(build_dir, boat_pack, boat_out_route, 'tr2-skybox-boat-out', out_dir)
        non_black, distinct = sky_band_stats(ppm_boat, top_rows=90)
        if non_black < 3000 or distinct < 4:
            raise AssertionError(f'Venice outdoor sky missing: non_black={non_black}, distinct={distinct}')
        m_boat = re.findall(r'game-render: .* matrices=(\d+)', log_boat)
        print(f'PASS boat.pack outdoor: sky_band non_black={non_black}/28800 distinct={distinct} matrices={m_boat[-1] if m_boat else "?"}')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--wall-pack', type=Path, default=None, help='Cooked Great Wall pack (wall.pack)')
    ap.add_argument('--boat-pack', type=Path, default=None, help='Cooked Venice pack (boat.pack)')
    ap.add_argument('--build', type=Path, default=None, help='Scratch BUILD_DIR for real-pack scene runs')
    ap.add_argument('--out', type=Path, default=None, help='Scratch output directory')
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix='tr2-skybox-') as tmp:
        work = args.out if args.out is not None else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        run_synthetic(work / 'synthetic')
        if args.wall_pack is not None or args.boat_pack is not None:
            if args.build is None:
                ap.error('--build is required when --wall-pack or --boat-pack is supplied')
            run_real_packs(args.build, work / 'real', args.wall_pack, args.boat_pack)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
