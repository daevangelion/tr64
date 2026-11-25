#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for TR2 dynamic flare & muzzle flash room lighting.

Runs:
1. Synthetic end-to-end renderer + host GPU/rasterizer controls compiled with
   UBSan (-fsanitize=undefined -fno-sanitize-recover=all, no retail data):
   - Unlit TR2 room submits 0 TR64_GPU_BATCH_LIT batches (direct pointer path).
   - Lit TR2 room brightens ONLY the overlapping near batch (1 TR64_GPU_BATCH_LIT)
     and leaves the out-of-radius far batch untouched and bit-identical.
   - Flicker-blackout radius=32 core lights vertices <= 32 units and leaves
     farther vertices in the same batch dark.
   - Underwater TR2 room (water=1) preserves the cyan water tint ratio when lit.
   - TR1 (level.game == 0) supports dynamic lights (1 TR64_GPU_BATCH_LIT on overlapping
     near batch, bit-identical far batch, 0 TR64_GPU_BATCH_LIT when unlit).
   - Extreme INT32_MAX / INT32_MIN coordinates cause zero signed overflow under UBSan.
   - Clearing lights resets to 0 TR64_GPU_BATCH_LIT and bit-identical baseline.
2. Optional real-pack Great Wall (wall.pack) dark-room flare test (--wall-pack):
   - Places Lara in dark underground room 49, renders unlit vs steady lit held flare.
   - Verifies outer room wall/floor/ceiling regions (outside Lara's silhouette)
     brighten dramatically in the 320x240 PPM.
   - Verifies unlit frame submits 0 lit room recolouring and is bit-identical to
     a clean baseline run.
   - Verifies zero simulation/RNG trace hash change vs pure non-rendering simulation.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PPM_HEADER = b'P6\n320 240\n255\n'

SYNTHETIC_C = r'''/* SPDX-License-Identifier: MIT */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/load.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/mem.h"
#include "plat/host/gpu-platform.h"

extern void __real_tr64_gpu_batch(const tr64_gpu_batch_t *b);
static unsigned g_total_batches, g_lit_batches;

void __wrap_tr64_gpu_batch(const tr64_gpu_batch_t *b)
{
 ++g_total_batches;
 if (b->batch_id & TR64_GPU_BATCH_LIT) ++g_lit_batches;
 __real_tr64_gpu_batch(b);
}

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

static void unpack5551(uint16_t px, unsigned *r, unsigned *g, unsigned *b)
{
 *r = (px >> 11) & 31u;
 *g = (px >> 6) & 31u;
 *b = (px >> 1) & 31u;
}

static void pack_vert_pair_rgb(uint8_t *dst,
                               int16_t x0, int16_t y0, int16_t z0, uint8_t r0, uint8_t g0, uint8_t b0,
                               int16_t x1, int16_t y1, int16_t z1, uint8_t r1, uint8_t g1, uint8_t b1)
{
 memset(dst, 0, 32);
 ws16(dst + 0, x0); ws16(dst + 2, y0); ws16(dst + 4, z0);
 ws16(dst + 8, x1); ws16(dst + 10, y1); ws16(dst + 12, z1);
 dst[16] = r0; dst[17] = g0; dst[18] = b0; dst[19] = 255;
 dst[20] = r1; dst[21] = g1; dst[22] = b1; dst[23] = 255;
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
 /* TX64: 1 flat white colour (31, 31, 31) so framebuffer RGB directly reflects vertex colour. */
 uint8_t tx[90] __attribute__((aligned(16))) = {0};
 memcpy(tx, "TX64", 4);
 w32(tx + 4, 1); w32(tx + 8, sizeof(tx));
 w32(tx + 32, 1); /* 1 flat colour */
 w32(tx + 36, 1); /* 1 animation word */
 w32(tx + 40, 80); w32(tx + 44, 80); w32(tx + 48, 80);
 w32(tx + 52, 80); w32(tx + 56, 80); w32(tx + 60, 80); w32(tx + 64, 88);
 w16(tx + 80 + 2, rgb5551(31, 31, 31));

 /* RG64: 3 rooms, 4 batches total (4 runs, 4 faces, 8 pairs):
  *  Room 0 (dry):
  *    Batch 0 (near left):  x in [-180..-20], y in [-80..80], z = 512, shade (32,32,32)
  *    Batch 1 (far right):  x in [200..900],  y in [-400..400], z = 3072, shade (32,32,32)
  *  Room 1 (underwater, flags & 1):
  *    Batch 2:              x in [-120..120], y in [-120..120], z = 512, water shade (20,32,32)
  *  Room 2 (dry, radius=32 core test):
  *    Batch 3:              left vert at (-120,-120,512)/( -120,120,512), right near (0,-120,512)/(0,120,512)
  *                          Actually place vert 0 at (-16,-16,512), vert 1 at (120,-16,512),
  *                          vert 2 at (120,120,512), vert 3 at (-16,120,512) so light at (-16,-16,512)
  *                          radius=32 lights ONLY vert 0 (dist=0 <= 32) and NOT verts 1,2,3 (dist >= 136 > 32)! */
 enum { OBJS = 3, BATCHES = 4, RUNS = 4, FACES = 4, PAIRS = 8 };
 uint8_t rg[64 + 32 * OBJS + 32 * BATCHES + 24 * RUNS + 8 * FACES + 32 * PAIRS] __attribute__((aligned(16))) = {0};
 write_geo_header(rg, "RG64", sizeof(rg), OBJS, BATCHES, RUNS, FACES, PAIRS, 8);
 const uint32_t oo = 64;
 const uint32_t bo = oo + 32 * OBJS;
 const uint32_t ro = bo + 32 * BATCHES;
 const uint32_t fo = ro + 24 * RUNS;
 const uint32_t vo = fo + 8 * FACES;

 /* Object 0 (Room 0): batches 0..1 */
 {
  uint8_t *o = rg + oo + 32 * 0;
  w32(o + 0, 0); w32(o + 4, 0); w16(o + 8, 2);
  ws16(o + 12, -180); ws16(o + 14, -400); ws16(o + 16, 512);
  ws16(o + 18, 900);  ws16(o + 20, 400);  ws16(o + 22, 3072);
 }
 /* Object 1 (Room 1): batch 2 */
 {
  uint8_t *o = rg + oo + 32 * 1;
  w32(o + 0, 1); w32(o + 4, 2); w16(o + 8, 1);
  ws16(o + 12, -120); ws16(o + 14, -120); ws16(o + 16, 512);
  ws16(o + 18, 120);  ws16(o + 20, 120);  ws16(o + 22, 512);
 }
 /* Object 2 (Room 2): batch 3 */
 {
  uint8_t *o = rg + oo + 32 * 2;
  w32(o + 0, 2); w32(o + 4, 3); w16(o + 8, 1);
  ws16(o + 12, -120); ws16(o + 14, -120); ws16(o + 16, 512);
  ws16(o + 18, 120);  ws16(o + 20, 120);  ws16(o + 22, 512);
 }

 /* Batches 0..3 */
 {
  uint8_t *b = rg + bo + 32 * 0;
  ws16(b + 0, -180); ws16(b + 2, -80); ws16(b + 4, 512);
  ws16(b + 6, -20);  ws16(b + 8, 80);  ws16(b + 10, 512);
  w16(b + 12, 4); w16(b + 14, 0); w32(b + 16, vo + 0); w16(b + 20, 1);

  b = rg + bo + 32 * 1;
  ws16(b + 0, 200); ws16(b + 2, -400); ws16(b + 4, 3072);
  ws16(b + 6, 900); ws16(b + 8, 400);  ws16(b + 10, 3072);
  w16(b + 12, 4); w16(b + 14, 0); w32(b + 16, vo + 64); w16(b + 20, 1);

  b = rg + bo + 32 * 2;
  ws16(b + 0, -120); ws16(b + 2, -120); ws16(b + 4, 512);
  ws16(b + 6, 120);  ws16(b + 8, 120);  ws16(b + 10, 512);
  w16(b + 12, 4); w16(b + 14, 1); w32(b + 16, vo + 128); w16(b + 20, 1);

  b = rg + bo + 32 * 3;
  ws16(b + 0, -120); ws16(b + 2, -120); ws16(b + 4, 512);
  ws16(b + 6, 120);  ws16(b + 8, 120);  ws16(b + 10, 512);
  w16(b + 12, 4); w16(b + 14, 2); w32(b + 16, vo + 192); w16(b + 20, 1);
 }

 for (unsigned i = 0; i < RUNS; ++i) {
  uint8_t *run = rg + ro + 24 * i;
  w16(run + 0, 0); run[2] = 0; run[3] = TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED;
  w16(run + 4, i); w16(run + 6, 1);
  uint8_t *face = rg + fo + 8 * i;
  face[0] = 0; face[1] = 1; face[2] = 2; face[3] = 3;
 }

 /* Vertices: Batch 0 (near left, screen x in [70..150], y in [80..160]) */
 pack_vert_pair_rgb(rg + vo + 0,   -180, -80, 512, 32, 32, 32,   -20, -80, 512, 32, 32, 32);
 pack_vert_pair_rgb(rg + vo + 32,   -20,  80, 512, 32, 32, 32,  -180,  80, 512, 32, 32, 32);
 /* Batch 1 (far right, screen x in [176..235], y in [86..153]) */
 pack_vert_pair_rgb(rg + vo + 64,   200, -400, 3072, 32, 32, 32,  900, -400, 3072, 32, 32, 32);
 pack_vert_pair_rgb(rg + vo + 96,   900,  400, 3072, 32, 32, 32,  200,  400, 3072, 32, 32, 32);
 /* Batch 2 (underwater room 1, screen x in [100..220], y in [60..180], dark cyan tint 20,32,32) */
 pack_vert_pair_rgb(rg + vo + 128, -120, -120, 512, 20, 32, 32,   120, -120, 512, 20, 32, 32);
 pack_vert_pair_rgb(rg + vo + 160,  120,  120, 512, 20, 32, 32,  -120,  120, 512, 20, 32, 32);
 /* Batch 3 (room 2 radius=32 core test: top-left corner (-120,-120,512) lit, bottom-right (120,120,512) dark) */
 pack_vert_pair_rgb(rg + vo + 192, -120, -120, 512, 32, 32, 32,   120, -120, 512, 32, 32, 32);
 pack_vert_pair_rgb(rg + vo + 224,  120,  120, 512, 32, 32, 32,  -120,  120, 512, 32, 32, 32);

 /* Minimal valid empty MG64. */
 uint8_t mg[64] __attribute__((aligned(16))) = {0};
 write_geo_header(mg, "MG64", sizeof(mg), 0, 0, 0, 0, 0, 0);

 tr64_core_rooms_t rooms_tbl[OBJS];
 memset(rooms_tbl, 0, sizeof(rooms_tbl));
 for (unsigned i = 0; i < OBJS; ++i) rooms_tbl[i].alternate_room.raw = tr64_core_be16(65535u);
 rooms_tbl[0].flags.raw = tr64_core_be16(0u);
 rooms_tbl[1].flags.raw = tr64_core_be16(1u); /* water */
 rooms_tbl[2].flags.raw = tr64_core_be16(0u);

 tr64_level_t level;
 memset(&level, 0, sizeof(level));
 level.ready = 1; level.game = 2; level.textures = tx; level.texture_bytes = sizeof(tx);
 level.tables[TR64_CORE_rooms - 1] = (tr64_level_table_t){
  .data = (const void *)rooms_tbl, .count = OBJS, .stride = sizeof(rooms_tbl[0])
 };

 tr64_gpu_init();
 tr64_gpu_span_t tx_span = {tx, sizeof(tx)}, rg_span = {rg, sizeof(rg)}, mg_span = {mg, sizeof(mg)};
 tr64_geo_t rgeo, mgeo;
 tr64_render_error_t err;
 if (tr64_geo_open_load(&rgeo, rg_span, 0, &err) || tr64_geo_open_load(&mgeo, mg_span, 1, &err))
  fail("geo_open_load", err.code, err.section);

 uint8_t work[4096] __attribute__((aligned(16)));
 tr64_render_t r;
 if (tr64_render_bind_load(&r, &level, tx_span, rg_span, mg_span, work, sizeof(work), &err))
  fail("bind_load", err.code, err.offset);

 const int32_t eye[3] = {0, 0, 0}, look[3] = {0, 0, 65536};
 tr64_render_pose_t p0, p1, p2;
 if (tr64_render_camera(&p0, eye, look, 0, 0, &err) ||
     tr64_render_camera(&p1, eye, look, 1, 0, &err) ||
     tr64_render_camera(&p2, eye, look, 2, 0, &err))
  fail("camera", err.code, 0);

 const uint16_t *fb = tr64_gpu_host_pixels();
 static uint16_t fb_unlit[320 * 240];
 tr64_render_counts_t counts;

 /* 1. Unlit TR2 room 0: 0 lit batches, dark baseline. */
 tr64_render_set_lights(NULL, 0);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p0, &counts, &err)) fail("unlit room 0", err.code, err.offset);
 if (g_total_batches != 2 || g_lit_batches != 0)
  fail("unlit batch counts", g_total_batches, g_lit_batches);
 memcpy(fb_unlit, fb, sizeof(fb_unlit));
 uint16_t near_unlit = fb[120 * 320 + 110];
 uint16_t far_unlit  = fb[120 * 320 + 200];
 unsigned nr0, ng0, nb0, fr0, fg0, fb0;
 unpack5551(near_unlit, &nr0, &ng0, &nb0);
 unpack5551(far_unlit, &fr0, &fg0, &fb0);
 if (nr0 == 0 || fr0 == 0 || nr0 > 8 || fr0 > 8)
  fail("unexpected unlit baseline luma", nr0, fr0);

 /* 2. Lit TR2 room 0: flare at (-100, 0, 512), radius=2048, peak=4096, shift=10.
  *    Must set TR64_GPU_BATCH_LIT on Batch 0 ONLY (1/2 batches) and leave Batch 1 bit-identical! */
 tr64_gpu_light_t flare = { .pos = {-100, 0, 512}, .radius = 2048, .peak = 4096, .shift = 10, .active = 1 };
 tr64_render_set_lights(&flare, 1);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p0, &counts, &err)) fail("lit room 0", err.code, err.offset);
 if (g_total_batches != 2 || g_lit_batches != 1)
  fail("lit room 0 batch culling failed", g_total_batches, g_lit_batches);
 uint16_t near_lit = fb[120 * 320 + 110];
 uint16_t far_lit  = fb[120 * 320 + 200];
 unsigned nr1, ng1, nb1;
 unpack5551(near_lit, &nr1, &ng1, &nb1);
 if (nr1 < nr0 + 18 || ng1 != nr1 || nb1 != nr1)
  fail("near dry batch did not brighten properly", nr0, nr1);
 if (far_lit != far_unlit)
  fail("far out-of-radius batch was altered", far_lit, far_unlit);

 /* 3. Flicker blackout radius=32 core at (-120, -120, 512) in Room 2.
  *    Vertex 0 (-120,-120,512) is distance 0 <= 32 (lit); Vertex 2 (120,120,512) is dist ~339 > 32 (dark). */
 tr64_gpu_light_t core32 = { .pos = {-120, -120, 512}, .radius = 32, .peak = 4096, .shift = 0, .active = 1 };
 tr64_render_set_lights(&core32, 1);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p2, &counts, &err)) fail("core32 room 2", err.code, err.offset);
 if (g_lit_batches != 1) fail("core32 lit batch count", g_lit_batches, 1);
 unsigned c32_near_r, c32_near_g, c32_near_b, c32_far_r, c32_far_g, c32_far_b;
 unpack5551(fb[62 * 320 + 102], &c32_near_r, &c32_near_g, &c32_near_b);
 unpack5551(fb[178 * 320 + 218], &c32_far_r, &c32_far_g, &c32_far_b);
 if (c32_near_r < nr0 + 12 || c32_far_r > nr0 + 2)
  fail("core32 falloff mismatch", c32_near_r, c32_far_r);

 /* 4. Underwater room 1 (water=1): cyan water tint ratio preserved when lit. */
 tr64_render_set_lights(NULL, 0);
 if (tr64_render_frame(&r, &p1, &counts, &err)) fail("unlit water room 1", err.code, err.offset);
 unsigned wr0, wg0, wb0;
 unpack5551(fb[120 * 320 + 160], &wr0, &wg0, &wb0);

 tr64_gpu_light_t wflare = { .pos = {0, 0, 512}, .radius = 2048, .peak = 4096, .shift = 10, .active = 1 };
 tr64_render_set_lights(&wflare, 1);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p1, &counts, &err)) fail("lit water room 1", err.code, err.offset);
 if (g_lit_batches != 1) fail("water lit batch count", g_lit_batches, 1);
 unsigned wr1, wg1, wb1;
 unpack5551(fb[120 * 320 + 160], &wr1, &wg1, &wb1);
 /* Expect dr ~ 85, dg = db ~ 136 -> rgb ~ (105, 168, 168) * 1.5 -> (157, 252, 252) -> 5-bit (~19, 31, 31). */
 if (wr1 <= wr0 + 10 || wg1 <= wg0 + 18 || wg1 != wb1 || wr1 >= wg1)
  fail("water tint not preserved", wr1, wg1);
 /* Ratio check: r/g should be close to 5/8 = 0.625 (allow 0.52..0.72 after 5-bit quantization). */
 if (wr1 * 100 < wg1 * 52 || wr1 * 100 > wg1 * 72)
  fail("water cyan tint ratio out of band", wr1 * 100, wg1 * 62);

 /* 5. TR1 (level.game == 0): dynamic lights illuminate near batch identically to TR2; unlit returns 0 lit. */
 level.game = 0;
 tr64_render_set_lights(&flare, 1);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p0, &counts, &err)) fail("tr1 room 0", err.code, err.offset);
 if (g_lit_batches != 1) fail("TR1 lit batch count mismatch", g_lit_batches, 1);
 if (fb[120 * 320 + 200] != far_unlit) fail("TR1 far batch changed under near light", fb[120 * 320 + 200], far_unlit);
 unsigned tr1_nr, tr1_ng, tr1_nb;
 unpack5551(fb[120 * 320 + 110], &tr1_nr, &tr1_ng, &tr1_nb);
 if (tr1_nr != nr1 || tr1_ng != ng1 || tr1_nb != nb1)
  fail("TR1 near batch lit brightness differed from TR2", tr1_nr, nr1);
 tr64_render_set_lights(NULL, 0);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p0, &counts, &err)) fail("tr1 unlit room 0", err.code, err.offset);
 if (g_lit_batches != 0 || memcmp(fb, fb_unlit, sizeof(fb_unlit)) != 0)
  fail("TR1 unlit pixels differed from baseline", g_lit_batches, 0);

 /* 6. Extreme INT32_MAX / INT32_MIN coordinates under UBSan: zero overflow, zero corruption. */
 level.game = 2;
 tr64_gpu_light_t extremes[2] = {
  { .pos = {INT32_MAX, INT32_MIN, INT32_MAX - 10}, .radius = 2048, .peak = 4096, .shift = 10, .active = 1 },
  { .pos = {INT32_MIN + 1, INT32_MAX, INT32_MIN},  .radius = INT32_MAX, .peak = 4096, .shift = 10, .active = 1 }
 };
 tr64_render_set_lights(extremes, 2);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p0, &counts, &err)) fail("extreme coords room 0", err.code, err.offset);
 /* First light is out of range; second has radius=INT32_MAX which overlaps AABB in render.c
  * and is safely skipped by radius>16384 guard in gpu.c without signed overflow! */
 if (memcmp(fb, fb_unlit, sizeof(fb_unlit)) != 0) fail("extreme coords altered pixels", 1, 0);

 /* 7. Clearing lights returns to exact unlit baseline with 0 TR64_GPU_BATCH_LIT. */
 tr64_render_set_lights(NULL, 0);
 g_total_batches = g_lit_batches = 0;
 if (tr64_render_frame(&r, &p0, &counts, &err)) fail("reset room 0", err.code, err.offset);
 if (g_lit_batches != 0 || memcmp(fb, fb_unlit, sizeof(fb_unlit)) != 0)
  fail("light reset leaked state", g_lit_batches, 0);

 printf("PASS synthetic room-lights (UBSan): dry_near=%u->%u dry_far=%u==%u core32=%u/%u water=(%u,%u,%u)->(%u,%u,%u) tr1=lit(%u->%u) extreme=ok\n",
        nr0, nr1, fr0, fr0, c32_near_r, c32_far_r, wr0, wg0, wb0, wr1, wg1, wb1, nr0, tr1_nr);
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
    src = work_dir / 'room_lights_unit.c'
    src.write_text(SYNTHETIC_C, encoding='utf-8')
    exe = work_dir / 'room_lights_unit'
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
        [cc, '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=undefined', '-fno-sanitize-recover=all',
         '-Wl,--wrap=tr64_gpu_batch',
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


def region_luma(rgb: bytes, x0: int, y0: int, x1: int, y1: int) -> int:
    total = 0
    for y in range(y0, y1):
        row = y * 320 * 3
        for x in range(x0, x1):
            p = row + x * 3
            total += rgb[p] + rgb[p + 1] + rgb[p + 2]
    return total


def run_host_route(build_dir: Path, pack: Path, route_text: str, name: str,
                   out_dir: Path, rebuild: bool = False) -> tuple[str, Path]:
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
        capture_output=True, text=True, check=True, timeout=60,
    )
    return res.stdout + res.stderr, ppm_path


def run_real_wall(build_dir: Path, out_dir: Path, wall_pack: Path) -> None:
    exe = build_dir / 'host/tr64-host'
    # Dark underground Great Wall room 49: Lara placed at (21520, 4352, 57856) facing +Z.
    unlit_route = (
        'scene tr2-room-lights-unlit\nstart boot\nseed 1\n'
        'load level wall\ngame\nhash-schema 21\n'
        'place 49 21520 4352 57856 0\n'
        'ticks 95\n'
        'observe log "^game-render: .* matrices=[0-9]+"\n'
    )
    lit_route = (
        'scene tr2-room-lights-lit\nstart boot\nseed 1\n'
        'load level wall\ngame\nhash-schema 21\n'
        'place 49 21520 4352 57856 0\n'
        'at 1 pad 8192 0 0\n'
        'at 2 pad 0 0 0\n'
        'ticks 95\n'
        'observe log "^game-render: .* matrices=[0-9]+"\n'
    )
    log_u1, ppm_u1 = run_host_route(build_dir, wall_pack, unlit_route, 'wall-r49-unlit-1', out_dir, rebuild=True)
    log_lit, ppm_lit = run_host_route(build_dir, wall_pack, lit_route, 'wall-r49-lit', out_dir, rebuild=False)
    log_u2, ppm_u2 = run_host_route(build_dir, wall_pack, unlit_route, 'wall-r49-unlit-2', out_dir, rebuild=False)

    # Unlit PPM must be 100% bit-identical across runs (no stale light leakage).
    if ppm_u1.read_bytes() != ppm_u2.read_bytes():
        raise AssertionError('Unlit room 49 PPM changed after lit flare run (light leakage!)')

    _, _, rgb_u = parse_ppm(ppm_u1)
    _, _, rgb_l = parse_ppm(ppm_lit)

    # Check OUTER room wall/floor/ceiling bands strictly outside Lara's center silhouette!
    left_u  = region_luma(rgb_u, 0,   40,  90, 200)
    left_l  = region_luma(rgb_l, 0,   40,  90, 200)
    right_u = region_luma(rgb_u, 230, 40, 320, 200)
    right_l = region_luma(rgb_l, 230, 40, 320, 200)
    top_u   = region_luma(rgb_u, 0,    0, 320,  50)
    top_l   = region_luma(rgb_l, 0,    0, 320,  50)
    bot_u   = region_luma(rgb_u, 0,  195, 320, 240)
    bot_l   = region_luma(rgb_l, 0,  195, 320, 240)
    outer_u = left_u + right_u + top_u + bot_u
    outer_l = left_l + right_l + top_l + bot_l
    full_u  = sum(rgb_u)
    full_l  = sum(rgb_l)

    if outer_l < outer_u * 2 or left_l <= left_u + 100000 or right_l <= right_u + 100000:
        raise AssertionError(
            f'Room 49 outer walls/floor did not brighten sufficiently: '
            f'outer {outer_u}->{outer_l} (L {left_u}->{left_l}, R {right_u}->{right_l}, '
            f'T {top_u}->{top_l}, B {bot_u}->{bot_l})'
        )

    # Verify zero simulation/RNG trace hash change vs pure non-rendering --game-tr2-schema21!
    inp_path = out_dir / 'wall-r49-lit.txt'
    tr_path = out_dir / 'wall-r49-lit.jsonl'
    inp_lines = ['place 21520 4352 57856 0 49', '0', '8192'] + ['0'] * 93
    inp_path.write_text('\n'.join(inp_lines) + '\n', encoding='ascii')
    subprocess.run(
        [str(exe), '--game-tr2-schema21', str(wall_pack), str(inp_path), str(tr_path)],
        check=True, capture_output=True, text=True, timeout=60,
    )
    recs = [json.loads(line) for line in tr_path.read_text(encoding='utf-8').splitlines() if line.strip()]
    tick95_rec = [r for r in recs if r.get('tick') == 95][-1]
    pure_hash = tick95_rec['hash']
    m_hash = re.findall(r'game-tick: tick=95 hash=([0-9a-fA-F]+)', log_lit)
    if not m_hash:
        raise AssertionError(f'Could not find tick=95 hash in route log:\n{log_lit[-800:]}')
    route_hash = m_hash[-1].lower().zfill(8)
    if route_hash != pure_hash.lower():
        raise AssertionError(f'Route rendered hash {route_hash} != pure simulation hash {pure_hash}')

    unlit_sha = hashlib.sha256(ppm_u1.read_bytes()).hexdigest()[:16]
    print(
        f'PASS wall.pack room 49 flare: full_luma {full_u}->{full_l} ({full_l/max(full_u,1):.2f}x), '
        f'outer_walls {outer_u}->{outer_l} ({outer_l/max(outer_u,1):.2f}x; '
        f'L={left_u}->{left_l} R={right_u}->{right_l} T={top_u}->{top_l} B={bot_u}->{bot_l}), '
        f'unlit_sha={unlit_sha} (bit-identical), sim_hash={pure_hash} (zero mutation)'
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--wall-pack', type=Path, default=None, help='Cooked Great Wall pack (wall.pack)')
    ap.add_argument('--build', type=Path, default=None, help='Scratch BUILD_DIR containing host/tr64-host')
    ap.add_argument('--out', type=Path, default=None, help='Scratch output directory')
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix='tr2-room-lights-') as tmp:
        work = args.out if args.out is not None else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        run_synthetic(work / 'synthetic')
        if args.wall_pack is not None:
            if args.build is None:
                ap.error('--build is required when --wall-pack is supplied')
            run_real_wall(args.build, work / 'real', args.wall_pack)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
