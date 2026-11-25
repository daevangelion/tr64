#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for TR1 Rendering Backport (m10-tr1-render-backport-v1):
1. Synthetic UBSan C harness (-fsanitize=undefined -fno-sanitize-recover=all, no retail data):
   - Deferred post-opaque VFX ordering (blended sprite & additive mesh submitted
     after opaque room and shadow pass).
   - Underwater camera cyan tint (camera_water=1): air room shifts to cyan
     (R*0.5, G*0.8, B*0.8), water room is bit-identical (zero double-tinting),
     and tr64_gpu_menu_begin() clears camera_water so menu pass is untinted.
   - Dynamic light on TR1 room geometry brightens overlapping near batch only
     and resets cleanly to 0 TR64_GPU_BATCH_LIT when cleared.
   (Non-wrapping FOG_ALPHA=0xB0/0x80 blending math is verified in tr2-blend-shadows-test.py.)
2. Real TR1 pack host verification (--build, --tr1-pack):
   - Caves (level1) start: Lara's octagonal ground shadow darkens floor pixels
     beneath her feet (vs shadow-off negative control), with 100% identical TR1
     state hash (zero simulation/RNG/tmpBox/floorData mutation), plus co-op P2
     shadow emission.
   - Underwater room in TR1 (auto-discovered water room with air room above in
     level2/campaign pack): air-room ceiling viewed from underwater is strongly
     cyan-tinted (G > R and B > R), dry control is untinted, and menu pass
     clears camera_water.
   - TR1 gunfire muzzle flash & VFX sprites: muzzle flash tagged ADDITIVE and
     deferred post-opaque, dynamic muzzle light illuminates nearby cave wall
     (TR64_GPU_BATCH_LIT + brighter wall pixels vs non-firing control), and
     present TR1 VFX sprites (splash/bubble/explosion/flame/etc.) tag
     TRANSLUCENT/ADDITIVE and defer post-opaque.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PPM_HEADER = b'P6\n320 240\n255\n'

SYNTHETIC_C = r'''/* SPDX-License-Identifier: MIT */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/load.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/mem.h"
#include "plat/host/gpu-platform.h"

extern uint16_t *tr64_gpu_host_pixels(void);
extern int tr64_gpu_host_camera_water(void);
extern void __real_tr64_gpu_batch(const tr64_gpu_batch_t *b);
extern void __real_tr64_gpu_material(tr64_gpu_material_t m);

static uint8_t mem_arena[4 * 1024 * 1024];
static size_t mem_used;
int mem_is_booted(void) { return 1; }
void *tr64_mem_alloc(int pool, uint32_t bytes)
{
 (void)pool;
 size_t aligned = (bytes + 15u) & ~(size_t)15u;
 if (mem_used + aligned > sizeof(mem_arena)) abort();
 void *p = mem_arena + mem_used;
 mem_used += aligned;
 return p;
}
uint32_t sys_time_us(void) { return 0; }
uint32_t sys_profile_ticks(void) { return 0; }
uint32_t sys_profile_us(uint32_t ticks) { return ticks; }
void sys_present(const uint16_t *pixels) { (void)pixels; }
void log_printf(const char *fmt, ...) { (void)fmt; }
void log_flush(void) {}
void sys_error(const char *fmt, ...)
{
 fprintf(stderr, "sys_error: %s\n", fmt ? fmt : "(null)");
 exit(2);
}

static unsigned g_total_batches, g_lit_batches, g_shadow_batches, g_step;
static unsigned g_opaque_last_step, g_shadow_step, g_trans_step, g_add_step;
static unsigned g_emit_shadow, g_emit_vfx;

void __wrap_tr64_gpu_material(tr64_gpu_material_t m)
{
 ++g_step;
 if (m.flags & TR64_GPU_ADDITIVE) g_add_step = g_step;
 else if ((m.flags & TR64_GPU_FLAT) && m.material == UINT16_MAX) g_shadow_step = g_step;
 else if ((m.flags & TR64_GPU_CLASS_MASK) == TR64_GPU_BLENDED_M1) g_trans_step = g_step;
 else g_opaque_last_step = g_step;
 __real_tr64_gpu_material(m);
}

void __wrap_tr64_gpu_batch(const tr64_gpu_batch_t *b)
{
 ++g_total_batches;
 if (b->batch_id & TR64_GPU_BATCH_LIT) ++g_lit_batches;
 if ((b->batch_id & (TR64_GPU_BATCH_TRANSIENT | 0x10000000u)) ==
     (TR64_GPU_BATCH_TRANSIENT | 0x10000000u))
  ++g_shadow_batches;
 __real_tr64_gpu_batch(b);
}

uint32_t game_view_room(uint32_t room, uint32_t *cursor, tr64_game_drawable_t *out, uint32_t cap)
{
 *cursor = TR64_GAME_VIEW_END;
 if (!g_emit_vfx || room != 0 || cap < 2) return 0u;
 memset(out, 0, 2 * sizeof(*out));
 /* 0: deferred translucent sprite (descriptor 1 = sprite 0) */
 out[0].mesh_slot = 1;
 out[0].instance_id = 0x80000001u;
 out[0].room = 0;
 out[0].basis[0] = out[0].basis[4] = out[0].basis[8] = 16384;
 out[0].translation[0] = -40; out[0].translation[1] = 0; out[0].translation[2] = 400;
 out[0].light_offset = -4096;
 out[0].kind = TR64_GAME_DRAW_SPRITE;
 out[0].blend = TR64_GAME_DRAW_TRANSLUCENT;
 /* 1: deferred additive mesh (mesh_slot 0) */
 out[1].mesh_slot = 0;
 out[1].instance_id = 0x80000002u;
 out[1].room = 0;
 out[1].basis[0] = out[1].basis[4] = out[1].basis[8] = 16384;
 out[1].translation[0] = 40; out[1].translation[1] = 0; out[1].translation[2] = 400;
 out[1].light_offset = -4096;
 out[1].kind = TR64_GAME_DRAW_MESH;
 out[1].blend = TR64_GAME_DRAW_ADDITIVE;
 return 2u;
}
uint32_t game_rest_item_view(uint32_t index, tr64_game_drawable_t *out, uint32_t cap)
{ (void)index; (void)out; (void)cap; return 0; }
uint32_t game_view_lights(const int32_t eye[3], tr64_gpu_light_t *out, uint32_t max_lights)
{ (void)eye; (void)out; (void)max_lights; return 0; }
uint32_t game_view_shadows(tr64_game_shadow_t *out, uint32_t cap)
{
 if (!g_emit_shadow || !out || !cap) return 0u;
 out[0] = (tr64_game_shadow_t){
  .x = 0, .y = 100, .z = 450, .rx = 180, .rz = 180, .yaw = 0, .room = 0, .alpha = 128, .pad = 0
 };
 return 1u;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }

static void pack_quad_verts(uint8_t *dst, const int16_t xyz[4][3], uint32_t rgba)
{
 memset(dst, 0, 64);
 for (unsigned i = 0; i < 4; ++i) {
  uint8_t *pair = dst + (i / 2) * 32;
  uint8_t *vp = pair + (i % 2) * 8;
  uint8_t *vc = pair + 16 + (i % 2) * 4;
  put16(vp + 0, (uint16_t)xyz[i][0]);
  put16(vp + 2, (uint16_t)xyz[i][1]);
  put16(vp + 4, (uint16_t)xyz[i][2]);
  put32(vc, rgba);
 }
}

static size_t build_tx64(uint8_t *buf)
{
 memset(buf, 0, 512);
 memcpy(buf, "TX64", 4);
 put32(buf + 4, 1);
 put32(buf + 12, 1); /* objects = 1 */
 put32(buf + 16, 1); /* sprites = 1 */
 put32(buf + 20, 1); /* clusters = 1 */
 put32(buf + 24, 1); /* palettes = 1 */
 put32(buf + 28, 0); /* split lines = 0 */
 put32(buf + 32, 2); /* flats = 2 */
 put32(buf + 36, 1); /* animated texture words */

 uint32_t off_cl = 80;
 uint32_t off_pal = off_cl + 16;
 uint32_t off_obj = off_pal + 8;
 uint32_t off_spr = off_obj + 24;
 uint32_t off_lines = off_spr + 22;
 uint32_t off_flats = (off_lines + 7u) & ~7u;
 uint32_t off_anim = off_flats + 16;
 uint32_t off_pal_data = (off_anim + 2u + 7u) & ~7u;
 uint32_t off_img_data = off_pal_data + 32;
 uint32_t total = off_img_data + 32;

 put32(buf + 8, total);
 put32(buf + 40, off_cl);
 put32(buf + 44, off_pal);
 put32(buf + 48, off_obj);
 put32(buf + 52, off_spr);
 put32(buf + 56, off_lines);
 put32(buf + 60, off_flats);
 put32(buf + 64, off_anim);

 /* Cluster 0: 8x8 CI4 (row_bytes=4, 8 rows = 32 bytes) */
 put32(buf + off_cl + 0, off_img_data);
 put16(buf + off_cl + 4, 8);
 put16(buf + off_cl + 6, 8);
 put16(buf + off_cl + 8, 4);
 /* Palette 0 */
 put32(buf + off_pal + 0, off_pal_data);
 /* Object descriptor 0: cluster 0, pal 0, x=0, y=0, w-1=7, h-1=7 */
 put16(buf + off_obj + 0, 0);
 put16(buf + off_obj + 2, 0);
 buf[off_obj + 4] = 0; buf[off_obj + 5] = 0;
 buf[off_obj + 6] = 7; buf[off_obj + 7] = 7;
 /* Sprite descriptor 0 (material id 1): cluster 0, pal 0, 8x8, bounds [-32..32] */
 put16(buf + off_spr + 0, 0);
 put16(buf + off_spr + 2, 0);
 buf[off_spr + 4] = 0; buf[off_spr + 5] = 0;
 buf[off_spr + 6] = 7; buf[off_spr + 7] = 7;
 put16(buf + off_spr + 14, (uint16_t)(int16_t)-32);
 put16(buf + off_spr + 16, (uint16_t)(int16_t)-32);
 put16(buf + off_spr + 18, (uint16_t)(int16_t)32);
 put16(buf + off_spr + 20, (uint16_t)(int16_t)32);
 /* Flats: 0=red (30,0,0), 1=blue (0,0,30) */
 put16(buf + off_flats + 2, (uint16_t)((30u << 11) | 1u));
 put16(buf + off_flats + 10, (uint16_t)((30u << 1) | 1u));
 /* Palette 0: all opaque white (0xFFFF) */
 for (unsigned i = 0; i < 16; ++i) put16(buf + off_pal_data + 2 * i, 0xffffu);
 memset(buf + off_img_data, 0x11, 32);
 return total;
}

typedef struct {
 uint32_t id;
 int16_t lo[3], hi[3];
 const int16_t (*verts)[3];
 uint32_t rgba;
 uint8_t flags;
 uint16_t mat;
} geo_quad_t;

static size_t build_geo(uint8_t *buf, const char magic[4], const geo_quad_t *qs, unsigned n)
{
 memset(buf, 0, 4096);
 uint32_t off_obj = 64;
 uint32_t off_bat = off_obj + 32 * n;
 uint32_t off_run = off_bat + 32 * n;
 uint32_t off_fac = off_run + 24 * n;
 uint32_t off_vtx = off_fac + 8 * n;
 uint32_t total = off_vtx + 64 * n;
 memcpy(buf, magic, 4);
 put32(buf + 4, 1); put32(buf + 8, total); put32(buf + 12, n);
 put32(buf + 16, n); put32(buf + 20, n); put32(buf + 24, n); put32(buf + 28, 2 * n);
 put32(buf + 32, off_obj); put32(buf + 36, off_bat); put32(buf + 40, off_run);
 put32(buf + 44, off_fac); put32(buf + 48, off_vtx);
 put32(buf + 52, 2 * n); put32(buf + 56, 1);
 for (unsigned i = 0; i < n; ++i) {
  uint8_t *o = buf + off_obj + 32 * i;
  put32(o + 0, qs[i].id);
  put32(o + 4, i); put16(o + 8, 1);
  put16(o + 12, (uint16_t)qs[i].lo[0]); put16(o + 14, (uint16_t)qs[i].lo[1]); put16(o + 16, (uint16_t)qs[i].lo[2]);
  put16(o + 18, (uint16_t)qs[i].hi[0]); put16(o + 20, (uint16_t)qs[i].hi[1]); put16(o + 22, (uint16_t)qs[i].hi[2]);
  uint8_t *b = buf + off_bat + 32 * i;
  put16(b + 0, (uint16_t)qs[i].lo[0]); put16(b + 2, (uint16_t)qs[i].lo[1]); put16(b + 4, (uint16_t)qs[i].lo[2]);
  put16(b + 6, (uint16_t)qs[i].hi[0]); put16(b + 8, (uint16_t)qs[i].hi[1]); put16(b + 10, (uint16_t)qs[i].hi[2]);
  put16(b + 12, 4); put16(b + 14, (uint16_t)i); put32(b + 16, off_vtx + 64 * i); put16(b + 20, 1);
  uint8_t *r = buf + off_run + 24 * i;
  put16(r + 0, qs[i].mat);
  r[2] = 0; r[3] = qs[i].flags;
  put16(r + 4, (uint16_t)i); put16(r + 6, 1);
  put32(r + 8, (16u << 16) | 16u);
  put32(r + 12, ((32u * 7 + 16u) << 16) | 16u);
  put32(r + 16, ((32u * 7 + 16u) << 16) | (32u * 7 + 16u));
  put32(r + 20, (16u << 16) | (32u * 7 + 16u));
  uint8_t *f = buf + off_fac + 8 * i;
  f[0] = 0; f[1] = 1; f[2] = 2; f[3] = 3; f[4] = 0xe4; f[5] = 0xe4;
  pack_quad_verts(buf + off_vtx + 64 * i, qs[i].verts, qs[i].rgba);
 }
 return total;
}

static void unpack5551(uint16_t px, unsigned *r, unsigned *g, unsigned *b)
{
 *r = (px >> 11) & 31u; *g = (px >> 6) & 31u; *b = (px >> 1) & 31u;
}

static void fail(const char *msg, unsigned a, unsigned b)
{
 fprintf(stderr, "FAIL: %s (a=%u b=%u)\n", msg, a, b);
 exit(1);
}

int main(void)
{
 tr64_gpu_init();
 uint8_t tx[512] __attribute__((aligned(16)));
 size_t tx_sz = build_tx64(tx);
 tr64_gpu_span_t tx_span = {tx, (uint32_t)tx_sz};
 tr64_gpu_bind_textures(tx_span);

 static const int16_t v_air[4][3] = {
  {-260, -180, 600}, {0, -180, 600}, {0, 180, 600}, {-260, 180, 600}
 };
 static const int16_t v_wat[4][3] = {
  {0, -180, 600}, {260, -180, 600}, {260, 180, 600}, {0, 180, 600}
 };
 static const int16_t v_mesh[4][3] = {
  {-32, -32, 0}, {32, -32, 0}, {32, 32, 0}, {-32, 32, 0}
 };
 geo_quad_t rquads[2] = {
  {0, {-260, -180, 590}, {0, 180, 610}, v_air, 0xa0a0a0ffu, TR64_GPU_OPAQUE | TR64_GPU_DOUBLE_SIDED, 0},
  {1, {0, -180, 590}, {260, 180, 610}, v_wat, 0x508080ffu, TR64_GPU_OPAQUE | TR64_GPU_DOUBLE_SIDED, 0}
 };
 geo_quad_t mquads[1] = {
  {0, {-32, -32, -10}, {32, 32, 10}, v_mesh, 0x808080ffu, TR64_GPU_CUTOUT | TR64_GPU_DOUBLE_SIDED, 0}
 };
 uint8_t rg[4096] __attribute__((aligned(16))), mg[4096] __attribute__((aligned(16)));
 size_t rg_sz = build_geo(rg, "RG64", rquads, 2);
 size_t mg_sz = build_geo(mg, "MG64", mquads, 1);

 tr64_core_rooms_t rooms[2];
 memset(rooms, 0, sizeof(rooms));
 rooms[0].alternate_room.raw = tr64_core_be16(65535u); rooms[0].flags.raw = tr64_core_be16(0);
 rooms[0].portal_start.raw = tr64_core_be32(0); rooms[0].portal_count.raw = tr64_core_be32(1);
 rooms[1].alternate_room.raw = tr64_core_be16(65535u); rooms[1].flags.raw = tr64_core_be16(1u);
 rooms[1].portal_start.raw = tr64_core_be32(1); rooms[1].portal_count.raw = tr64_core_be32(1);

 tr64_core_portals_t portals[2];
 memset(portals, 0, sizeof(portals));
 for (unsigned i = 0; i < 2; ++i) {
  portals[i].room.raw = tr64_core_be16((uint16_t)(1u - i));
  portals[i].normal_z.raw = tr64_core_be16((uint16_t)-1);
  portals[i].x0.raw = tr64_core_be16((uint16_t)-300); portals[i].y0.raw = tr64_core_be16((uint16_t)-240); portals[i].z0.raw = tr64_core_be16(300);
  portals[i].x1.raw = tr64_core_be16(300);            portals[i].y1.raw = tr64_core_be16((uint16_t)-240); portals[i].z1.raw = tr64_core_be16(300);
  portals[i].x2.raw = tr64_core_be16(300);            portals[i].y2.raw = tr64_core_be16(240);            portals[i].z2.raw = tr64_core_be16(300);
  portals[i].x3.raw = tr64_core_be16((uint16_t)-300); portals[i].y3.raw = tr64_core_be16(240);            portals[i].z3.raw = tr64_core_be16(300);
 }
 tr64_u32_be_t mesh_ptrs[1];
 mesh_ptrs[0].raw = tr64_core_be32(0);

 tr64_level_t level;
 memset(&level, 0, sizeof(level));
 level.ready = 1;
 level.game = 0; /* TR1! */
 level.textures = tx;
 level.texture_bytes = (uint32_t)tx_sz;
 level.tables[TR64_CORE_rooms - 1] = (tr64_level_table_t){.data = (const uint8_t *)rooms, .count = 2, .stride = sizeof(rooms[0])};
 level.tables[TR64_CORE_portals - 1] = (tr64_level_table_t){.data = (const uint8_t *)portals, .count = 2, .stride = sizeof(portals[0])};
 level.tables[TR64_CORE_mesh_pointers - 1] = (tr64_level_table_t){.data = (const uint8_t *)mesh_ptrs, .count = 1, .stride = sizeof(mesh_ptrs[0])};

 tr64_render_t ren;
 tr64_render_error_t err;
 tr64_render_counts_t counts;
 uint8_t work[4096] __attribute__((aligned(16)));
 if (tr64_render_bind_load(&ren, &level, tx_span, (tr64_gpu_span_t){rg, (uint32_t)rg_sz},
                           (tr64_gpu_span_t){mg, (uint32_t)mg_sz}, work, sizeof(work), &err))
  fail("bind_load", err.code, err.offset);

 const tr64_gpu_projection_t proj = TR64_GPU_WORLD_PROJECTION;
 tr64_render_pose_t p_dry = {
  .view = {.basis = {16384, 0, 0, 0, 16384, 0, 0, 0, 16384}, .eye = {0, 0, 0}},
  .projection = proj, .camera_room = 0, .flipped = 0
 };
 tr64_render_pose_t p_uw = p_dry;
 p_uw.camera_room = 1;
 const uint16_t *fb = tr64_gpu_host_pixels();

 /* 1. TR1 dry baseline + underwater camera cyan tint + zero double-tinting + menu clear. */
 if (tr64_render_frame(&ren, &p_dry, &counts, &err)) fail("dry frame", err.code, err.offset);
 uint16_t air_dry = fb[120 * 320 + 110];
 uint16_t wat_dry = fb[120 * 320 + 210];
 unsigned ar0, ag0, ab0, ar1, ag1, ab1;
 unpack5551(air_dry, &ar0, &ag0, &ab0);
 if (ar0 < 20 || ar0 != ag0 || ag0 != ab0) fail("dry air baseline", ar0, ag0);

 if (tr64_render_frame(&ren, &p_uw, &counts, &err)) fail("uw frame", err.code, err.offset);
 uint16_t air_uw = fb[120 * 320 + 110];
 uint16_t wat_uw = fb[120 * 320 + 210];
 unpack5551(air_uw, &ar1, &ag1, &ab1);
 if (ar1 > ar0 / 2 + 1 || ag1 <= ar1 || ab1 != ag1) fail("TR1 air room not cyan-tinted", ar1, ag1);
 if (wat_uw != wat_dry) fail("TR1 water room double-tinted", wat_uw, wat_dry);
 if (tr64_render_cam_water != 0) fail("tr64_render_cam_water leaked after frame", tr64_render_cam_water, 0);

 /* Verify menu pass clears GPU camera_water cleanly. */
 tr64_gpu_frame_begin(&p_uw.view, &proj);
 tr64_gpu_set_camera_water(1);
 if (tr64_gpu_host_camera_water() != 1) fail("camera_water did not arm", 0, 1);
 tr64_gpu_menu_begin(&p_dry.view, 0);
 if (tr64_gpu_host_camera_water() != 0) fail("menu_begin did not clear camera_water", 1, 0);
 tr64_gpu_counts_t gcounts;
 tr64_gpu_frame_end(&gcounts);

 /* 2. TR1 live game frame: ground shadow + deferred translucent sprite + deferred additive mesh ordering. */
 g_emit_shadow = 1;
 g_emit_vfx = 1;
 g_step = g_opaque_last_step = g_shadow_step = g_trans_step = g_add_step = 0;
 g_shadow_batches = 0;
 tr64_game_view_t gview;
 memset(&gview, 0, sizeof(gview));
 memcpy(gview.camera_basis, p_dry.view.basis, sizeof(gview.camera_basis));
 gview.camera_room = 0;
 tr64_game_drawable_t dbuf[8];
 if (tr64_render_game_frame(&ren, &gview, dbuf, 8, &counts, &err)) fail("game frame", err.code, err.offset);
 if (g_shadow_batches != 1) fail("TR1 shadow batch count", g_shadow_batches, 1);
 if (!(g_opaque_last_step > 0 && g_shadow_step > g_opaque_last_step &&
       g_trans_step > g_shadow_step && g_add_step > g_trans_step))
  fail("TR1 deferred post-opaque ordering wrong", g_opaque_last_step, g_add_step);
 unsigned ord_op = g_opaque_last_step, ord_sh = g_shadow_step;
 unsigned ord_tr = g_trans_step, ord_ad = g_add_step;

 /* 3. TR1 dynamic room light: brightens near air wall (room 0), resets cleanly. */
 g_emit_shadow = g_emit_vfx = 0;
 tr64_gpu_light_t muzzle = {.pos = {-120, 0, 500}, .radius = 2048, .peak = 4096, .shift = 10, .active = 1};
 tr64_render_set_lights(&muzzle, 1);
 g_lit_batches = 0;
 if (tr64_render_frame(&ren, &p_dry, &counts, &err)) fail("lit frame", err.code, err.offset);
 unsigned lr, lg, lb;
 unpack5551(fb[120 * 320 + 110], &lr, &lg, &lb);
 if (g_lit_batches == 0 || lr <= ar0) fail("TR1 dynamic light did not brighten room", ar0, lr);
 tr64_render_set_lights(NULL, 0);
 g_lit_batches = 0;
 if (tr64_render_frame(&ren, &p_dry, &counts, &err)) fail("unlit reset", err.code, err.offset);
 if (g_lit_batches != 0 || fb[120 * 320 + 110] != air_dry) fail("TR1 light reset leaked", g_lit_batches, 0);

 printf("PASS synth TR1 backport (UBSan): air_uw=(%u,%u,%u)->(%u,%u,%u) wat_unchanged=ok "
        "menu_clear=ok order=%u<%u<%u<%u muzzle_lit=%u->%u\n",
        ar0, ag0, ab0, ar1, ag1, ab1, ord_op, ord_sh, ord_tr, ord_ad, ar0, lr);
 return 0;
}
'''

PROBE_C = r'''/* SPDX-License-Identifier: MIT */
#include "game/common/internal.h"
#include "tr64/game.h"
#include "tr64/gpu.h"
#include "tr64/code.h"
#include "tr64/level.h"
#include "engine/core/sys.h"
#include "tr64/io.h"
#include "engine/render/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void tr64_gpu_init(void);
extern int tr64_gpu_host_camera_water(void);
extern void tr64_render_scene_set_live(int live);
extern void tr64_render_scene_load(const tr64_level_t *level, const char *name, uint32_t pack_bytes);
extern void tr64_render_game_scene_prepare(void);
extern void tr64_render_game_scene_frame(const tr64_game_view_t *view,
                                         tr64_game_drawable_t *buffer, uint32_t capacity);

void __real_game_tick_players(const uint16_t input_mask[TR64_GAME_PLAYERS]);
uint32_t __real_game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity);
void __real_tr64_gpu_material(tr64_gpu_material_t m);
void __real_tr64_gpu_batch(const tr64_gpu_batch_t *b);

static tr64_level_t g_tr1_level;
static int render_ready;
static tr64_game_drawable_t frame_buf[TR64_GAME_DRAW_CHUNK];
static unsigned g_add_mats, g_blend_mats, g_shadow_count, g_lit_batches;
static unsigned g_step, g_opaque_last, g_add_first, g_blend_first;

void __wrap_tr64_scene_load(const char *name)
{
 const char *path = getenv("TR64_LEVEL_PACK");
 int sz = sys_cart_open(path);
 if (sz <= 0) sys_error("tr1r: cannot open TR64_LEVEL_PACK");
 tr64_io_init((uint32_t)sz);
 tr64_io_frame();
 const char *mode = getenv("TR64_TR1R_MODE");
 int want_render = (!mode || strcmp(mode, "plain") != 0);
 if (want_render) {
  tr64_gpu_init();
  tr64_render_scene_set_live(1);
 }
 tr64_load_scope_begin();
 tr64_level_load(&g_tr1_level, (uint32_t)sz, name);
 if (want_render) tr64_render_scene_load(&g_tr1_level, name, (uint32_t)sz);
 tr64_load_scope_end();
 if (want_render) render_ready = 1;
}

const tr64_level_t *__wrap_tr64_level_current(void) { return &g_tr1_level; }

void __wrap_tr64_gpu_material(tr64_gpu_material_t m)
{
 ++g_step;
 if (m.flags & TR64_GPU_ADDITIVE) {
  ++g_add_mats;
  if (!g_add_first) g_add_first = g_step;
 } else if ((m.flags & TR64_GPU_CLASS_MASK) == TR64_GPU_BLENDED_M1) {
  ++g_blend_mats;
  if (!(m.flags & TR64_GPU_FLAT) && !g_blend_first) g_blend_first = g_step;
 } else {
  g_opaque_last = g_step;
 }
 __real_tr64_gpu_material(m);
}

void __wrap_tr64_gpu_batch(const tr64_gpu_batch_t *b)
{
 if (b->batch_id & TR64_GPU_BATCH_LIT) ++g_lit_batches;
 __real_tr64_gpu_batch(b);
}

uint32_t __wrap_game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity)
{
 if (getenv("TR64_TR1R_NO_SHADOWS")) return 0u;
 uint32_t n = __real_game_view_shadows(out, capacity);
 g_shadow_count = n;
 return n;
}

static void make_camera(tr64_game_view_t *view, uint32_t room,
                        int32 ex, int32 ey, int32 ez, int32 tx, int32 ty, int32 tz)
{
 int16 ax = 0, ay = 0;
 anglesFromVector(tx - ex, ty - ey, tz - ez, &ax, &ay);
 uint32 txsc = gSinCosTable[(uint16)ax >> 4], tysc = gSinCosTable[(uint16)ay >> 4];
 int32 sx = wrap_i32(txsc) >> 16, cx = (int16)txsc;
 int32 sy = wrap_i32(tysc) >> 16, cy = (int16)tysc;
 view->camera_basis[0] = cy; view->camera_basis[1] = 0; view->camera_basis[2] = -sy;
 view->camera_basis[3] = (sx * sy) >> 14; view->camera_basis[4] = cx; view->camera_basis[5] = (sx * cy) >> 14;
 view->camera_basis[6] = (cx * sy) >> 14; view->camera_basis[7] = -sx; view->camera_basis[8] = (cx * cy) >> 14;
 view->camera_eye[0] = ex; view->camera_eye[1] = ey; view->camera_eye[2] = ez;
 view->camera_target[0] = tx; view->camera_target[1] = ty; view->camera_target[2] = tz;
 view->camera_room = room;
}

static void run_uw_check(const char *uw_out, const char *dry_out)
{
 /* Auto-discover a water room with a vertical portal up to a dry air room. */
 for (uint32 ri = 0; ri < gs->level.roomsCount; ++ri) {
  const Room *r = gs->rooms + ri;
  if (!(tr64_u16(r->info->flags) & ROOM_FLAG_WATER_MASK)) continue;
  int32 nx = tr64_u16(r->info->x_sectors), nz = tr64_u16(r->info->z_sectors);
  int32 rx = tr64_i32(r->info->x), rz = tr64_i32(r->info->z);
  for (int32 sx = 1; sx + 1 < nx; ++sx) {
   for (int32 sz = 1; sz + 1 < nz; ++sz) {
    const Sector *s = r->sectors + sx * nz + sz;
    if (s->roomAbove == NO_ROOM || s->floor == NO_FLOOR || s->ceiling == NO_FLOOR) continue;
    const Room *above = gs->rooms + s->roomAbove;
    if (tr64_u16(above->info->flags) & ROOM_FLAG_WATER_MASK) continue;
    int32 floorY = s->floor * 256, ceilY = s->ceiling * 256;
    if (floorY - ceilY < 512) continue;
    int32 wx = rx + sx * 1024 + 512, wz = rz + sz * 1024 + 512;
    const Sector *as = Room_getSector(above, wx, wz);
    if (as->ceiling == NO_FLOOR) continue;
    int32 aboveCeilY = as->ceiling * 256;
    if (ceilY - aboveCeilY < 512) continue;
    tr64_game_view_t v;
    game_view_camera(&v);
    /* Underwater camera looking up through portal at dry room ceiling. */
    make_camera(&v, ri, wx, ceilY + 256, wz, wx + 256, aboveCeilY, wz + 256);
    g_add_mats = g_blend_mats = g_shadow_count = g_lit_batches = 0;
    tr64_render_game_scene_frame(&v, frame_buf, TR64_GAME_DRAW_CHUNK);
    int cam_after = tr64_render_cam_water;
    if (sys_shot(uw_out) < 0) sys_error("tr1r: sys_shot uw failed");
    /* Dry control: camera just above water surface looking at same ceiling. */
    make_camera(&v, s->roomAbove, wx, ceilY - 256, wz, wx + 256, aboveCeilY, wz + 256);
    tr64_render_game_scene_frame(&v, frame_buf, TR64_GAME_DRAW_CHUNK);
    if (sys_shot(dry_out) < 0) sys_error("tr1r: sys_shot dry failed");
    /* Verify menu pass clears camera_water. */
    tr64_gpu_projection_t proj = TR64_GPU_WORLD_PROJECTION;
    tr64_gpu_view_t gv;
    memcpy(gv.basis, v.camera_basis, sizeof(gv.basis));
    memcpy(gv.eye, v.camera_eye, sizeof(gv.eye));
    tr64_gpu_frame_begin(&gv, &proj);
    tr64_gpu_set_camera_water(1);
    int armed = tr64_gpu_host_camera_water();
    tr64_gpu_menu_begin(&gv, 0);
    int after_menu = tr64_gpu_host_camera_water();
    tr64_gpu_counts_t gc;
    tr64_gpu_frame_end(&gc);
    printf("UW_CHECK water_room=%u air_room=%u lit_batches=%u cam_after=%d menu_armed=%d menu_after=%d\n",
           ri, (unsigned)s->roomAbove, g_lit_batches, cam_after, armed, after_menu);
    return;
   }
  }
 }
 sys_error("tr1r: no water->air room pair found in level");
}

static void run_vfx_tag_check(void)
{
 /* Verify TR1 sprite VFX tagging & deferred post-opaque submission for every
  * VFX sprite type authored in the current level. */
 static const struct { uint32 type; uint8 want; const char *name; } specs[] = {
  {ITEM_FLAME, TR64_GAME_DRAW_ADDITIVE, "flame"},
  {ITEM_EXPLOSION, TR64_GAME_DRAW_ADDITIVE, "explosion"},
  {ITEM_SPARKLES, TR64_GAME_DRAW_ADDITIVE, "sparkles"},
  {ITEM_LAVA_PARTICLE, TR64_GAME_DRAW_ADDITIVE, "lava"},
  {ITEM_SPLASH, TR64_GAME_DRAW_TRANSLUCENT, "splash"},
  {ITEM_BUBBLE, TR64_GAME_DRAW_TRANSLUCENT, "bubble"},
  {ITEM_SMOKE, TR64_GAME_DRAW_TRANSLUCENT, "smoke"}
 };
 const Lara *lara = gs->players[0];
 if (!lara || !lara->room) sys_error("tr1r: no lara for vfx check");
 uint32 room = (uint32)(lara->room - gs->rooms);
 unsigned tested = 0;
 for (unsigned i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i) {
  if (gs->level.models[specs[i].type].count >= 0) continue;
  vec3i p = {lara->pos.x, i32_sub(lara->pos.y, 512), i32_add(lara->pos.z, 512)};
  ItemObj *fx = ItemObj_add(specs[i].type, lara->room, &p, 0);
  if (!fx) sys_error("tr1r: ItemObj_add failed for %s", specs[i].name);
  uint32 cursor = 0, found = 0;
  uint8 got_blend = 0;
  while (cursor != TR64_GAME_VIEW_END) {
   uint32 n = game_view_room(room, &cursor, frame_buf, TR64_GAME_DRAW_CHUNK);
   for (uint32 j = 0; j < n; ++j) {
    if (frame_buf[j].instance_id == 0x80000000u + (uint32)(fx - gs->items)) {
     found = 1;
     got_blend = frame_buf[j].blend;
    }
   }
  }
  if (!found || got_blend != specs[i].want)
   sys_error("tr1r: vfx %s blend mismatch found=%u got=%u want=%u",
             specs[i].name, found, got_blend, specs[i].want);
  tr64_game_view_t v;
  game_view_camera(&v);
  make_camera(&v, room, lara->pos.x, i32_sub(lara->pos.y, 512), i32_sub(lara->pos.z, 512),
              p.x, p.y, p.z);
  g_step = g_opaque_last = g_add_first = g_blend_first = g_add_mats = g_blend_mats = 0;
  tr64_render_game_scene_frame(&v, frame_buf, TR64_GAME_DRAW_CHUNK);
  unsigned vfx_step = (specs[i].want == TR64_GAME_DRAW_ADDITIVE) ? g_add_first : g_blend_first;
  if (!vfx_step || vfx_step <= g_opaque_last)
   sys_error("tr1r: vfx %s not deferred post-opaque (vfx=%u opaque=%u)",
             specs[i].name, vfx_step, g_opaque_last);
  ItemObj_remove(fx);
  ++tested;
 }
 printf("VFX_TAGS tested=%u\n", tested);
}

void __wrap_game_tick_players(const uint16_t input_mask[TR64_GAME_PLAYERS])
{
 __real_game_tick_players(input_mask);
 uint32 logicBefore = gs->gRandSeedLogic, drawBefore = gs->gRandSeedDraw;
 const FloorData *lastFdBefore = gs->gLastFloorData;
 uint16 lastSlantBefore = gs->gLastFloorSlant;
 AABBs tmpBoxBefore = gs->tmpBox;
 uint32 h2Before = game_state_hash_schema(2);
 uint32 h4Before = game_state_hash_schema(4);

 const char *mode = getenv("TR64_TR1R_MODE");
 const char *shotTickEnv = getenv("TR64_TR1R_TICK");
 uint32 targetTick = shotTickEnv ? (uint32)strtoul(shotTickEnv, NULL, 10) : 0u;
 if (render_ready == 1) {
  tr64_render_game_scene_prepare();
  render_ready = 2;
 }

 if (mode && !strcmp(mode, "shadow") && gs->tick == targetTick) {
  const Lara *lara = gs->players[0];
  tr64_game_view_t v;
  game_view_camera(&v);
  uint32 room = (uint32)(lara->room - gs->rooms);
  make_camera(&v, room, lara->pos.x, i32_sub(lara->pos.y, 600), i32_sub(lara->pos.z, 900),
              lara->pos.x, lara->pos.y, lara->pos.z);
  g_add_mats = g_blend_mats = g_shadow_count = g_lit_batches = 0;
  tr64_render_game_scene_frame(&v, frame_buf, TR64_GAME_DRAW_CHUNK);
  const char *out = getenv("TR64_TR1R_OUT");
  if (out && sys_shot(out) < 0) sys_error("tr1r: sys_shot shadow failed");
  printf("SHADOW tick=%u shadows=%u blend_mats=%u hash2=%08x hash4=%08x\n",
         gs->tick, g_shadow_count, g_blend_mats, game_state_hash_schema(2), game_state_hash_schema(4));
 } else if (mode && !strcmp(mode, "muzzle") &&
            ((targetTick && gs->tick == targetTick) ||
             (!targetTick && gs->players[0] && gs->players[0]->extraL &&
              (gs->players[0]->extraL->armR.flash.timer || gs->players[0]->extraL->armL.flash.timer)))) {
  static int muzzle_done;
  if (!muzzle_done) {
   muzzle_done = 1;
   const Lara *lara = gs->players[0];
   uint32 room = (uint32)(lara->room - gs->rooms);
   /* Inspect Lara's muzzle flash drawable blend tag + dynamic light count. */
   uint32 cursor = 0, add_drawables = 0;
   while (cursor != TR64_GAME_VIEW_END) {
    uint32 n = game_view_room(room, &cursor, frame_buf, TR64_GAME_DRAW_CHUNK);
    for (uint32 j = 0; j < n; ++j)
     if (frame_buf[j].blend == TR64_GAME_DRAW_ADDITIVE) ++add_drawables;
   }
   tr64_gpu_light_t lights[TR64_GPU_MAX_LIGHTS];
   uint32 nlights = game_view_lights(NULL, lights, TR64_GPU_MAX_LIGHTS);
   tr64_game_view_t v;
   game_view_camera(&v);
   make_camera(&v, room, lara->pos.x - 1024, i32_sub(lara->pos.y, 700), i32_sub(lara->pos.z, 1024),
               lara->pos.x + 1024, i32_sub(lara->pos.y, 400), i32_add(lara->pos.z, 1024));
   g_step = g_opaque_last = g_add_first = g_add_mats = g_lit_batches = 0;
   tr64_render_game_scene_frame(&v, frame_buf, TR64_GAME_DRAW_CHUNK);
   const char *out = getenv("TR64_TR1R_OUT");
   if (out && sys_shot(out) < 0) sys_error("tr1r: sys_shot muzzle failed");
   printf("MUZZLE tick=%u flash_r=%u flash_l=%u add_drawables=%u nlights=%u add_mats=%u lit_batches=%u deferred=%d\n",
          gs->tick,
          lara->extraL ? (unsigned)lara->extraL->armR.flash.timer : 0u,
          lara->extraL ? (unsigned)lara->extraL->armL.flash.timer : 0u,
          add_drawables, nlights, g_add_mats, g_lit_batches,
          (g_add_first > g_opaque_last && g_opaque_last > 0) ? 1 : 0);
  }
 } else if (mode && !strcmp(mode, "uw") && gs->tick == targetTick) {
  run_uw_check(getenv("TR64_TR1R_OUT"), getenv("TR64_TR1R_OUT2"));
 }

 /* Strictly verify zero simulation / RNG / floor / tmpBox mutation from rendering! */
 if (gs->gRandSeedLogic != logicBefore || gs->gRandSeedDraw != drawBefore ||
     gs->gLastFloorData != lastFdBefore || gs->gLastFloorSlant != lastSlantBefore ||
     memcmp(&gs->tmpBox, &tmpBoxBefore, sizeof(tmpBoxBefore)) != 0 ||
     game_state_hash_schema(2) != h2Before || game_state_hash_schema(4) != h4Before)
  sys_error("tr1r: presentation mutated TR1 simulation state at tick %u", gs->tick);

 /* Probe-only post-parity checks (co-op P2 shadow & VFX sprite tagging) at final tick. */
 if (mode && !strcmp(mode, "coop_vfx") && gs->tick == targetTick) {
  if (game_player_join() != 0) sys_error("tr1r: game_player_join failed");
  tr64_game_shadow_t sh[16];
  uint32_t nsh = __real_game_view_shadows(sh, 16);
  printf("COOP_SHADOWS count=%u\n", nsh);
  run_vfx_tag_check();
 }
 fflush(stdout);
}
'''


def expect(ok: bool, msg: str) -> None:
    if not ok:
        raise AssertionError(msg)


def read_ppm(path: Path) -> bytes:
    raw = path.read_bytes()
    expect(raw.startswith(PPM_HEADER) and len(raw) == len(PPM_HEADER) + 320 * 240 * 3,
           f'{path}: expected canonical 320x240 P6 PPM')
    return raw[len(PPM_HEADER):]


def parse_kv(stdout: str, prefix: str) -> dict[str, str]:
    for line in stdout.splitlines():
        if line.startswith(prefix + ' '):
            out = {}
            for tok in line.split()[1:]:
                k, v = tok.split('=', 1)
                out[k] = v
            return out
    return {}


def run_synthetic(work: Path) -> None:
    gen = work / 'gen'
    gen.mkdir(parents=True, exist_ok=True)
    schema = gen / 'core_schema.h'
    subprocess.run(
        ['rtk', sys.executable, str(ROOT / 'tools/cook/n64_core.py'), 'schema', '--out', str(schema)],
        check=True,
    )
    src = work / 'synth.c'
    exe = work / 'synth'
    src.write_text(SYNTHETIC_C, encoding='utf-8')
    c = subprocess.run([
        'rtk', os.environ.get('HOST_CC', 'cc'), '-m32', '-std=c99', '-O2', '-g',
        '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-DTR64_HOST=1', '-DTR64_GAME=1', '-D_POSIX_C_SOURCE=200809L',
        f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}',
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
        '-Wl,--wrap=tr64_gpu_batch', '-Wl,--wrap=tr64_gpu_material',
        '-o', str(exe),
    ], capture_output=True, text=True)
    expect(c.returncode == 0, f'synthetic compile failed:\n{c.stdout}{c.stderr}')
    r = subprocess.run(['rtk', str(exe)], capture_output=True, text=True, timeout=30)
    expect(r.returncode == 0, f'synthetic test failed:\n{r.stdout}{r.stderr}')
    print(r.stdout.strip())


def build_probe(build_dir: Path, work: Path) -> Path:
    src = work / 'probe.c'
    obj = work / 'probe.o'
    exe = work / 'tr1r-host'
    src.write_text(PROBE_C, encoding='utf-8')
    gen = build_dir / 'generated'
    objs = sorted((build_dir / 'host').rglob('*.o'))
    expect(bool(objs), f'no host objects under {build_dir / "host"}')
    c = subprocess.run([
        'rtk', 'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}', f'-I{gen / "host"}',
        '-DTR64_GAME=1', '-DTR64_HOST=1', '-D_POSIX_C_SOURCE=200809L',
        '-c', str(src), '-o', str(obj),
    ], capture_output=True, text=True)
    expect(c.returncode == 0, f'probe compile failed:\n{c.stdout}{c.stderr}')
    l = subprocess.run([
        'rtk', 'cc', '-m32',
        '-Wl,--wrap=tr64_scene_load', '-Wl,--wrap=tr64_level_current',
        '-Wl,--wrap=game_tick_players', '-Wl,--wrap=game_view_shadows',
        '-Wl,--wrap=tr64_gpu_material', '-Wl,--wrap=tr64_gpu_batch',
        '-o', str(exe), str(obj), *(str(p) for p in objs),
    ], capture_output=True, text=True)
    expect(l.returncode == 0, f'probe link failed:\n{l.stdout}{l.stderr}')
    return exe


def run_probe(exe: Path, pack: Path, level: str, inp: Path, ticks: int,
              extra_env: dict[str, str], trace_out: Path | None = None) -> str:
    env = dict(os.environ)
    env['TR64_LEVEL_PACK'] = str(pack)
    env.update(extra_env)
    cmd = [
        'rtk', str(exe), '--game-input', str(inp), '--game-level', level,
        '--game-ticks', str(ticks), '--game-trace-only',
    ]
    if trace_out is not None:
        cmd += ['--game-trace', str(trace_out)]
    p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=120)
    expect(p.returncode == 0, f'probe failed ({level}/{inp.name}):\n{p.stdout[-800:]}{p.stderr[-800:]}')
    return p.stdout


def run_real_pack(build_dir: Path, tr1_pack: Path, uw_level: str | None, work: Path) -> None:
    exe = build_probe(build_dir, work)

    # 1. Caves (level1) ground shadow + negative control + 100% identical state hash.
    inp_idle = work / 'idle.txt'
    inp_idle.write_text('0\n0\n0\n', encoding='ascii')
    ppm_on, ppm_off = work / 'l1_sh_on.ppm', work / 'l1_sh_off.ppm'
    tr_on, tr_plain = work / 'l1_on.trace', work / 'l1_plain.trace'
    out_on = run_probe(exe, tr1_pack, 'level1', inp_idle, 3,
                       {'TR64_TR1R_MODE': 'shadow', 'TR64_TR1R_TICK': '2', 'TR64_TR1R_OUT': str(ppm_on)},
                       trace_out=tr_on)
    out_off = run_probe(exe, tr1_pack, 'level1', inp_idle, 3,
                        {'TR64_TR1R_MODE': 'shadow', 'TR64_TR1R_TICK': '2',
                         'TR64_TR1R_OUT': str(ppm_off), 'TR64_TR1R_NO_SHADOWS': '1'})
    run_probe(exe, tr1_pack, 'level1', inp_idle, 3, {'TR64_TR1R_MODE': 'plain'}, trace_out=tr_plain)
    sh_on = parse_kv(out_on, 'SHADOW')
    sh_off = parse_kv(out_off, 'SHADOW')
    expect(int(sh_on.get('shadows', '0')) >= 1, f'expected Lara shadow on Caves, got {sh_on}')
    expect(sh_on.get('hash2') == sh_off.get('hash2') and sh_on.get('hash4') == sh_off.get('hash4'),
           f'shadow mutated TR1 hash: on={sh_on} off={sh_off}')
    expect(tr_on.read_bytes() == tr_plain.read_bytes(),
           'TR1 rendered trace differed from plain non-rendering trace!')
    px_on, px_off = read_ppm(ppm_on), read_ppm(ppm_off)
    darkened = sum(1 for k in range(0, len(px_on), 3)
                   if (px_on[k] + px_on[k + 1] + px_on[k + 2]) < (px_off[k] + px_off[k + 1] + px_off[k + 2]) - 10)
    brightened = sum(1 for k in range(0, len(px_on), 3)
                     if (px_on[k] + px_on[k + 1] + px_on[k + 2]) > (px_off[k] + px_off[k + 1] + px_off[k + 2]))
    expect(darkened >= 25 and brightened == 0,
           f'Caves shadow pixel check failed: darkened={darkened} brightened={brightened}')
    print(f'PASS real 1/4 Caves ground shadow: shadows={sh_on["shadows"]} darkened={darkened} '
          f'brightened={brightened} hash2={sh_on["hash2"]} (100% identical)')

    # 2. Caves co-op P2 shadow + VFX sprite tagging & post-opaque deferral.
    out_cv = run_probe(exe, tr1_pack, 'level1', inp_idle, 3,
                       {'TR64_TR1R_MODE': 'coop_vfx', 'TR64_TR1R_TICK': '2'})
    coop = parse_kv(out_cv, 'COOP_SHADOWS')
    vfx = parse_kv(out_cv, 'VFX_TAGS')
    expect(int(coop.get('count', '0')) >= 2, f'expected >=2 co-op shadows (P1+P2), got {coop}')
    expect(int(vfx.get('tested', '0')) >= 2, f'expected >=2 TR1 VFX sprite types tested, got {vfx}')
    print(f'PASS real 2/4 Caves co-op shadows & VFX tags: coop_shadows={coop["count"]} vfx_types_tested={vfx["tested"]}')

    # 3. Caves gunfire muzzle flash: tagged ADDITIVE, deferred post-opaque, illuminates cave wall.
    # Draw pistols (256), wait until ready, then hold ACTION (128) to fire; compare against same tick without ACTION.
    inp_fire = work / 'fire.txt'
    inp_nofire = work / 'nofire.txt'
    draw_seq = ['256'] + ['0'] * 24
    inp_fire.write_text('\n'.join(draw_seq + ['128'] * 15) + '\n', encoding='ascii')
    inp_nofire.write_text('\n'.join(draw_seq + ['0'] * 15) + '\n', encoding='ascii')
    ppm_fire, ppm_nofire = work / 'l1_fire.ppm', work / 'l1_nofire.ppm'
    out_f = run_probe(exe, tr1_pack, 'level1', inp_fire, 40,
                      {'TR64_TR1R_MODE': 'muzzle', 'TR64_TR1R_TICK': '0',
                       'TR64_TR1R_OUT': str(ppm_fire)})
    fire_info = parse_kv(out_f, 'MUZZLE')
    expect(bool(fire_info), f'Lara never produced a muzzle flash while firing pistols:\n{out_f}')
    fire_tick = int(fire_info['tick'])
    out_nf = run_probe(exe, tr1_pack, 'level1', inp_nofire, 40,
                       {'TR64_TR1R_MODE': 'muzzle', 'TR64_TR1R_TICK': str(fire_tick),
                        'TR64_TR1R_OUT': str(ppm_nofire)})
    nf_info = parse_kv(out_nf, 'MUZZLE')
    expect(int(fire_info.get('add_drawables', '0')) >= 1, f'muzzle flash not tagged ADDITIVE: {fire_info}')
    expect(int(fire_info.get('nlights', '0')) >= 1, f'muzzle flash did not emit dynamic light: {fire_info}')
    expect(int(fire_info.get('add_mats', '0')) >= 1 and int(fire_info.get('deferred', '0')) == 1,
           f'muzzle flash not deferred post-opaque with TR64_GPU_ADDITIVE: {fire_info}')
    expect(int(fire_info.get('lit_batches', '0')) >= 1,
           f'muzzle flash dynamic light did not light cave wall: {fire_info}')
    expect(int(nf_info.get('lit_batches', '0')) == 0 and int(nf_info.get('nlights', '0')) == 0,
           f'non-firing control leaked dynamic lights: {nf_info}')
    px_fire, px_nofire = read_ppm(ppm_fire), read_ppm(ppm_nofire)
    sum_fire, sum_nofire = sum(px_fire), sum(px_nofire)
    expect(sum_fire > sum_nofire + 150000,
           f'muzzle flash did not noticeably brighten cave wall ({sum_nofire} -> {sum_fire})')
    print(f'PASS real 3/4 Caves gunfire muzzle flash: tick={fire_tick} add_mats={fire_info["add_mats"]} '
          f'deferred=1 lit_batches={fire_info["lit_batches"]} luma={sum_nofire}->{sum_fire}')

    # 4. Underwater room cyan tint + zero double-tint + menu clear (on uw_level, default level2).
    if uw_level:
        ppm_uw, ppm_dry = work / 'uw.ppm', work / 'dry.ppm'
        out_uw = run_probe(exe, tr1_pack, uw_level, inp_idle, 3,
                           {'TR64_TR1R_MODE': 'uw', 'TR64_TR1R_TICK': '2',
                            'TR64_TR1R_OUT': str(ppm_uw), 'TR64_TR1R_OUT2': str(ppm_dry)})
        uw = parse_kv(out_uw, 'UW_CHECK')
        expect(int(uw.get('lit_batches', '0')) >= 1, f'underwater air ceiling not lit/tinted: {uw}')
        expect(int(uw.get('cam_after', '1')) == 0, f'tr64_render_cam_water leaked after frame: {uw}')
        expect(int(uw.get('menu_armed', '0')) == 1 and int(uw.get('menu_after', '1')) == 0,
               f'menu pass did not clear camera_water: {uw}')
        px_uw, px_dry = read_ppm(ppm_uw), read_ppm(ppm_dry)
        r_u = g_u = b_u = cnt_u = 0
        r_d = g_d = b_d = cnt_d = 0
        for k in range(0, 320 * 200 * 3, 3):
            if px_uw[k] + px_uw[k + 1] + px_uw[k + 2] > 24:
                r_u += px_uw[k]; g_u += px_uw[k + 1]; b_u += px_uw[k + 2]; cnt_u += 1
            if px_dry[k] + px_dry[k + 1] + px_dry[k + 2] > 24:
                r_d += px_dry[k]; g_d += px_dry[k + 1]; b_d += px_dry[k + 2]; cnt_d += 1
        expect(cnt_u >= 2000 and g_u > r_u * 1.25 and b_u > r_u * 1.25,
               f'underwater ceiling not cyan-tinted: cnt={cnt_u} RGB=({r_u},{g_u},{b_u})')
        expect(cnt_d >= 2000 and (r_d / max(1, g_d)) > (r_u / max(1, g_u)) + 0.15,
               f'dry control not more neutral than underwater: dry=({r_d},{g_d},{b_d}) uw=({r_u},{g_u},{b_u})')
        print(f'PASS real 4/4 {uw_level} underwater cyan tint: water_room={uw["water_room"]}->air_room={uw["air_room"]} '
              f'uw_RGB=({r_u//cnt_u},{g_u//cnt_u},{b_u//cnt_u}) dry_RGB=({r_d//cnt_d},{g_d//cnt_d},{b_d//cnt_d}) '
              f'menu_clear=ok')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=None, help='TR1 host build directory')
    ap.add_argument('--tr1-pack', type=Path, default=None, help='Cooked TR1 pack (campaign or level1)')
    ap.add_argument('--uw-level', type=str, default='level2',
                    help='TR1 level name with underwater->air portal (default: level2; empty to skip)')
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')
    with tempfile.TemporaryDirectory(prefix='tr1-render-backport-', dir=scratch) as td:
        work = Path(td)
        run_synthetic(work)
        if args.build and args.tr1_pack:
            run_real_pack(args.build, args.tr1_pack, args.uw_level or None, work)
    print('ALL TR1 RENDER BACKPORT TESTS PASSED')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
