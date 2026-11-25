#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for TR2 Rendering Wave 4 (tr2-blend-shadows-v1):

1. Synthetic UBSan C harness (-fsanitize=undefined -fno-sanitize-recover=all, no retail data):
   - TR64_GPU_ADDITIVE (non-wrapping FOG_ALPHA=0xB0, (src*176 + dst*80) >> 8):
     * Blends blue over red cleanly, bright-over-bright never wraps.
     * Z-compare ON, Z-write OFF (later opaque quad behind additive quad overwrites).
   - TR64_GPU_BLENDED_M1 (50% translucent FOG_ALPHA=0x80, (src + dst) >> 1):
     * Black flat sentinel (material=0xFFFF) over (30,30,30) produces exact (15,15,15).
     * Z-compare ON, Z-write OFF.
   - Underwater camera cyan tint (camera_water=1):
     * Air room + skybox shift to cyan (R*0.5, G*0.8, B*0.8; G > R and B > R).
     * Water room pixels are bit-identical (zero double-tinting).
     * Lit air room viewed from underwater clamps to cyan water range (<= 16, 25, 25 in RGB5).
   - Static mesh dynamic light:
     * Near static mesh brightens under active flare; far static mesh is bit-identical.
   - TR1 (level.game == 0) parity:
     * Renders ground shadows, underwater camera tint on air rooms, and dynamic lights
       without binding a TR2 skybox.

2. Real-pack host verification (--build, --wall-pack, --boat-pack):
   - Great Wall lit flare: held glow sprite submitted with TR64_GPU_ADDITIVE;
     rendered PPM brightens cleanly without dark halo pixels.
   - Great Wall & Venice ground shadows: Lara (+ boat in Venice) emit shadows,
     floor pixels beneath Lara darken vs shadow-disabled negative control,
     100% bit-exact trace parity with plain tr64-host (zero sim/RNG mutation).
   - Submerged camera in Great Wall pool (room 37 looking up at outdoor air/sky):
     sky/air pixels are strongly cyan-tinted (G > R and B > R) vs dry control.
   - Host TR2 animated-texture period fix: period == 10 (step 0 at tick 10, step 1 at tick 11).
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
static unsigned g_total_batches, g_lit_batches, g_shadow_batches;

void __wrap_tr64_gpu_batch(const tr64_gpu_batch_t *b)
{
 ++g_total_batches;
 if (b->batch_id & TR64_GPU_BATCH_LIT) ++g_lit_batches;
 if ((b->batch_id & (TR64_GPU_BATCH_TRANSIENT | 0x10000000u)) ==
     (TR64_GPU_BATCH_TRANSIENT | 0x10000000u))
  ++g_shadow_batches;
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

static int g_emit_shadow = 0;
uint32_t game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity)
{
 if (!g_emit_shadow || !out || !capacity) return 0u;
 out[0] = (tr64_game_shadow_t){
  .x = 0, .y = 0, .z = 512, .rx = 160, .rz = 160, .yaw = 0, .room = 0, .alpha = 128, .pad = 0
 };
 return 1u;
}

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

static void draw_quad_direct(uint32_t instance_id, uint32_t batch_id, int16_t z,
                             uint8_t r, uint8_t g, uint8_t b, uint16_t flat_mat, uint8_t flags)
{
 uint8_t verts[64] __attribute__((aligned(16)));
 pack_vert_pair_rgb(verts + 0,  -100, -100, z, r, g, b,   100, -100, z, r, g, b);
 pack_vert_pair_rgb(verts + 32,  100,  100, z, r, g, b,  -100,  100, z, r, g, b);
 static const uint8_t face[8] = {0, 1, 2, 3, 0, 0, 0, 0};
 static const uint8_t st[16] = {0};
 tr64_gpu_transform_t m = {.basis = {16384,0,0, 0,16384,0, 0,0,16384}, .instance_id = instance_id};
 tr64_gpu_matrix(&m);
 tr64_gpu_material((tr64_gpu_material_t){flat_mat, flat_mat, 0, flags});
 tr64_gpu_batch_t pkt = {verts, face, st, 4, 1, batch_id};
 tr64_gpu_batch(&pkt);
}

int main(void)
{
 /* TX64: 1 flat white colour (31,31,31). */
 uint8_t tx[90] __attribute__((aligned(16))) = {0};
 memcpy(tx, "TX64", 4);
 w32(tx + 4, 1); w32(tx + 8, sizeof(tx));
 w32(tx + 32, 1); w32(tx + 36, 1);
 w32(tx + 40, 80); w32(tx + 44, 80); w32(tx + 48, 80);
 w32(tx + 52, 80); w32(tx + 56, 80); w32(tx + 60, 80); w32(tx + 64, 88);
 w16(tx + 80 + 2, rgb5551(31, 31, 31));

 tr64_gpu_init();
 tr64_gpu_span_t tx_span = {tx, sizeof(tx)};
 tr64_gpu_bind_textures(tx_span);

 const tr64_gpu_view_t view = {.basis = {16384,0,0, 0,16384,0, 0,0,16384}, .eye = {0,0,0}};
 const tr64_gpu_projection_t proj = TR64_GPU_WORLD_PROJECTION;
 const tr64_gpu_rect_t full = {0, 0, 320, 240};
 tr64_gpu_counts_t gcounts;
 const uint16_t *fb = tr64_gpu_host_pixels();
 unsigned r, g, b;

 /* --- Test 1: TR64_GPU_ADDITIVE (FOG_ALPHA=0xB0) + Z-write OFF + zero wrap --- */
 tr64_gpu_frame_begin(&view, &proj);
 tr64_gpu_scissor(full);
 /* Red dst at z=600: shade 88 * 1.5 = 132 -> 132>>3 = 16 in 5-bit red. */
 draw_quad_direct(0, 1, 600, 88, 0, 0, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 /* Blue additive src at z=400: shade 128 * 1.5 = 192 -> 192>>3 = 24 in 5-bit blue. */
 draw_quad_direct(1, 2, 400, 0, 0, 128, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED | TR64_GPU_ADDITIVE);
 tr64_gpu_frame_end(&gcounts);
 unpack5551(fb[120 * 320 + 150], &r, &g, &b);
 /* Expected: R = (0*176 + 16*80)>>8 = 5, G = 0, B = (24*176 + 0*80)>>8 = 16. */
 if (r != 5 || g != 0 || b != 16)
  fail("additive 0xB0 blend mismatch", (r << 10) | (g << 5) | b, (5u << 10) | 16u);

 /* Bright-over-bright never wraps; and Z-write OFF lets z=500 green overwrite z=400 additive! */
 tr64_gpu_frame_begin(&view, &proj);
 tr64_gpu_scissor(full);
 draw_quad_direct(0, 1, 600, 255, 255, 255, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 draw_quad_direct(1, 2, 400, 255, 255, 255, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED | TR64_GPU_ADDITIVE);
 tr64_gpu_frame_end(&gcounts);
 unpack5551(fb[120 * 320 + 150], &r, &g, &b);
 if (r != 31 || g != 31 || b != 31)
  fail("additive bright-over-bright wrapped", r, g);

 tr64_gpu_frame_begin(&view, &proj);
 tr64_gpu_scissor(full);
 draw_quad_direct(0, 1, 600, 88, 0, 0, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 draw_quad_direct(1, 2, 400, 0, 0, 128, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED | TR64_GPU_ADDITIVE);
 draw_quad_direct(2, 3, 500, 0, 108, 0, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 tr64_gpu_frame_end(&gcounts);
 unpack5551(fb[120 * 320 + 150], &r, &g, &b);
 if (r != 0 || g < 18 || b != 0)
  fail("additive overwrote Z-buffer", r, g);

 /* --- Test 2: TR64_GPU_BLENDED_M1 (50% translucent, black sentinel 0xFFFF) + Z-write OFF --- */
 tr64_gpu_frame_begin(&view, &proj);
 tr64_gpu_scissor(full);
 /* Grey dst at z=600: shade 162 -> (255*162+128)>>8 = 161 -> 161+81 = 242 -> 242>>3 = 30. */
 draw_quad_direct(0, 1, 600, 162, 162, 162, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 /* Black flat sentinel 0xFFFF with BLENDED_M1 at z=400 -> (30 + 0) >> 1 = 15! */
 draw_quad_direct(1, 2, 400, 0, 0, 0, UINT16_MAX,
                  TR64_GPU_FLAT | TR64_GPU_BLENDED_M1 | TR64_GPU_DOUBLE_SIDED);
 tr64_gpu_frame_end(&gcounts);
 unpack5551(fb[120 * 320 + 150], &r, &g, &b);
 if (r != 15 || g != 15 || b != 15)
  fail("translucent shadow 50% darken mismatch", r, g);

 /* Z-write OFF proof for BLENDED_M1: z=500 opaque quad overwrites z=400 translucent quad. */
 tr64_gpu_frame_begin(&view, &proj);
 tr64_gpu_scissor(full);
 draw_quad_direct(0, 1, 600, 160, 160, 160, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 draw_quad_direct(1, 2, 400, 0, 0, 0, UINT16_MAX,
                  TR64_GPU_FLAT | TR64_GPU_BLENDED_M1 | TR64_GPU_DOUBLE_SIDED);
 draw_quad_direct(2, 3, 500, 0, 108, 0, 0, TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED);
 tr64_gpu_frame_end(&gcounts);
 unpack5551(fb[120 * 320 + 150], &r, &g, &b);
 if (r != 0 || g < 18 || b != 0)
  fail("translucent overwrote Z-buffer", r, g);

 /* --- Test 3, 4, 5: Full renderer tests (camera_water, static dynamic lights, TR1 isolation) --- */
 /* RG64: 2 rooms:
  *   Room 0 (air, OUTSIDE): left quad [-160..-20, -60..60, z=512], shade (160,160,160)
  *   Room 1 (water):        right quad [20..160, -60..60, z=512], water shade (80,128,128)
  * Portal from Room 1 -> Room 0 covering full view so submerged camera in Room 1 sees Room 0 + Skybox! */
 enum { R_OBJS = 2, R_BATCHES = 2, R_RUNS = 2, R_FACES = 2, R_PAIRS = 4 };
 uint8_t rg[64 + 32 * R_OBJS + 32 * R_BATCHES + 24 * R_RUNS + 8 * R_FACES + 32 * R_PAIRS] __attribute__((aligned(16))) = {0};
 write_geo_header(rg, "RG64", sizeof(rg), R_OBJS, R_BATCHES, R_RUNS, R_FACES, R_PAIRS, 4);
 const uint32_t r_oo = 64, r_bo = r_oo + 32 * R_OBJS, r_ro = r_bo + 32 * R_BATCHES;
 const uint32_t r_fo = r_ro + 24 * R_RUNS, r_vo = r_fo + 8 * R_FACES;
 {
  uint8_t *o0 = rg + r_oo + 0;
  w32(o0, 0); w32(o0 + 4, 0); w16(o0 + 8, 1);
  ws16(o0 + 12, -160); ws16(o0 + 14, -60); ws16(o0 + 16, 512);
  ws16(o0 + 18, -20);  ws16(o0 + 20, 60);  ws16(o0 + 22, 512);
  uint8_t *o1 = rg + r_oo + 32;
  w32(o1, 1); w32(o1 + 4, 1); w16(o1 + 8, 1);
  ws16(o1 + 12, 20);  ws16(o1 + 14, -60); ws16(o1 + 16, 512);
  ws16(o1 + 18, 160); ws16(o1 + 20, 60);  ws16(o1 + 22, 512);

  uint8_t *b0 = rg + r_bo + 0;
  ws16(b0 + 0, -160); ws16(b0 + 2, -60); ws16(b0 + 4, 512);
  ws16(b0 + 6, -20);  ws16(b0 + 8, 60);  ws16(b0 + 10, 512);
  w16(b0 + 12, 4); w16(b0 + 14, 0); w32(b0 + 16, r_vo + 0); w16(b0 + 20, 1);

  uint8_t *b1 = rg + r_bo + 32;
  ws16(b1 + 0, 20);  ws16(b1 + 2, -60); ws16(b1 + 4, 512);
  ws16(b1 + 6, 160); ws16(b1 + 8, 60);  ws16(b1 + 10, 512);
  w16(b1 + 12, 4); w16(b1 + 14, 1); w32(b1 + 16, r_vo + 64); w16(b1 + 20, 1);

  for (unsigned i = 0; i < 2; ++i) {
   uint8_t *run = rg + r_ro + 24 * i;
   w16(run, 0); run[2] = 0; run[3] = TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED;
   w16(run + 4, i); w16(run + 6, 1);
   uint8_t *face = rg + r_fo + 8 * i;
   face[0] = 0; face[1] = 1; face[2] = 2; face[3] = 3;
  }
  pack_vert_pair_rgb(rg + r_vo + 0,  -160, -60, 512, 160, 160, 160,  -20, -60, 512, 160, 160, 160);
  pack_vert_pair_rgb(rg + r_vo + 32,  -20,  60, 512, 160, 160, 160, -160,  60, 512, 160, 160, 160);
  pack_vert_pair_rgb(rg + r_vo + 64,   20, -60, 512,  80, 128, 128,  160, -60, 512,  80, 128, 128);
  pack_vert_pair_rgb(rg + r_vo + 96,  160,  60, 512,  80, 128, 128,   20,  60, 512,  80, 128, 128);
 }

 /* MG64: 2 meshes:
  *   Mesh 0 (source_id=10): small static cube-face quad [-30..30, -30..30, z=0], shade 128 (mid grey)
  *   Mesh 1 (source_id=20): skybox backdrop quad [-600..600, -400..400, z=1024], shade 160 */
 enum { M_OBJS = 2, M_BATCHES = 2, M_RUNS = 2, M_FACES = 2, M_PAIRS = 4 };
 uint8_t mg[64 + 32 * M_OBJS + 32 * M_BATCHES + 24 * M_RUNS + 8 * M_FACES + 32 * M_PAIRS] __attribute__((aligned(16))) = {0};
 write_geo_header(mg, "MG64", sizeof(mg), M_OBJS, M_BATCHES, M_RUNS, M_FACES, M_PAIRS, 4);
 const uint32_t m_oo = 64, m_bo = m_oo + 32 * M_OBJS, m_ro = m_bo + 32 * M_BATCHES;
 const uint32_t m_fo = m_ro + 24 * M_RUNS, m_vo = m_fo + 8 * M_FACES;
 {
  uint8_t *o0 = mg + m_oo + 0;
  w32(o0, 10); w32(o0 + 4, 0); w16(o0 + 8, 1);
  ws16(o0 + 12, -30); ws16(o0 + 14, -30); ws16(o0 + 16, 0);
  ws16(o0 + 18, 30);  ws16(o0 + 20, 30);  ws16(o0 + 22, 0);
  uint8_t *o1 = mg + m_oo + 32;
  w32(o1, 20); w32(o1 + 4, 1); w16(o1 + 8, 1);
  ws16(o1 + 12, -600); ws16(o1 + 14, -400); ws16(o1 + 16, 1024);
  ws16(o1 + 18, 600);  ws16(o1 + 20, 400);  ws16(o1 + 22, 1024);

  uint8_t *b0 = mg + m_bo + 0;
  ws16(b0 + 0, -30); ws16(b0 + 2, -30); ws16(b0 + 4, 0);
  ws16(b0 + 6, 30);  ws16(b0 + 8, 30);  ws16(b0 + 10, 0);
  w16(b0 + 12, 4); w16(b0 + 14, 0); w32(b0 + 16, m_vo + 0); w16(b0 + 20, 1);

  uint8_t *b1 = mg + m_bo + 32;
  ws16(b1 + 0, -600); ws16(b1 + 2, -400); ws16(b1 + 4, 1024);
  ws16(b1 + 6, 600);  ws16(b1 + 8, 400);  ws16(b1 + 10, 1024);
  w16(b1 + 12, 4); w16(b1 + 14, 1); w32(b1 + 16, m_vo + 64); w16(b1 + 20, 1);

  for (unsigned i = 0; i < 2; ++i) {
   uint8_t *run = mg + m_ro + 24 * i;
   w16(run, 0); run[2] = 0; run[3] = TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED;
   w16(run + 4, i); w16(run + 6, 1);
   uint8_t *face = mg + m_fo + 8 * i;
   face[0] = 0; face[1] = 1; face[2] = 2; face[3] = 3;
  }
  pack_vert_pair_rgb(mg + m_vo + 0,   -30, -30, 0, 128, 128, 128,   30, -30, 0, 128, 128, 128);
  pack_vert_pair_rgb(mg + m_vo + 32,   30,  30, 0, 128, 128, 128,  -30,  30, 0, 128, 128, 128);
  pack_vert_pair_rgb(mg + m_vo + 64, -600, -400, 1024, 120, 120, 120, 600, -400, 1024, 120, 120, 120);
  pack_vert_pair_rgb(mg + m_vo + 96,  600,  400, 1024, 120, 120, 120,-600,  400, 1024, 120, 120, 120);
 }

 /* Portals: Room 0 -> Room 1 and Room 1 -> Room 0. */
 tr64_core_portals_t portals[2];
 memset(portals, 0, sizeof(portals));
 for (unsigned i = 0; i < 2; ++i) {
  portals[i].room.raw = tr64_core_be16(1u - i);
  portals[i].normal_z.raw = tr64_core_be16((uint16_t)-1);
  portals[i].x0.raw = tr64_core_be16((uint16_t)-500); portals[i].y0.raw = tr64_core_be16((uint16_t)-500); portals[i].z0.raw = tr64_core_be16(256);
  portals[i].x1.raw = tr64_core_be16(500);            portals[i].y1.raw = tr64_core_be16((uint16_t)-500); portals[i].z1.raw = tr64_core_be16(256);
  portals[i].x2.raw = tr64_core_be16(500);            portals[i].y2.raw = tr64_core_be16(500);            portals[i].z2.raw = tr64_core_be16(256);
  portals[i].x3.raw = tr64_core_be16((uint16_t)-500); portals[i].y3.raw = tr64_core_be16(500);            portals[i].z3.raw = tr64_core_be16(256);
 }

 /* Static instances in Room 0:
  *   Static 0 (near): (-60, 140, 512), shade=7400 (dark: offset=+3304) -> screen (-30+160=130, 70+120=190)
  *   Static 1 (far):  (180, 140, 3072), shade=7400 -> screen (15+160=175, 11+120=131)... wait place at (600, 700, 3072) -> screen (210, 178) */
 tr64_core_static_instances_t sinsts[2];
 memset(sinsts, 0, sizeof(sinsts));
 sinsts[0].x.raw = tr64_core_be32((uint32_t)-60);  sinsts[0].y.raw = tr64_core_be32(140); sinsts[0].z.raw = tr64_core_be32(512);
 sinsts[0].intensity.raw = tr64_core_be16(7400); sinsts[0].reserved.raw = tr64_core_be16(7400); sinsts[0].mesh.raw = tr64_core_be16(0);
 sinsts[1].x.raw = tr64_core_be32(600); sinsts[1].y.raw = tr64_core_be32(700); sinsts[1].z.raw = tr64_core_be32(3072);
 sinsts[1].intensity.raw = tr64_core_be16(7400); sinsts[1].reserved.raw = tr64_core_be16(7400); sinsts[1].mesh.raw = tr64_core_be16(0);

 tr64_core_static_meshes_t smeshes[1];
 memset(smeshes, 0, sizeof(smeshes));
 smeshes[0].id.raw = tr64_core_be32(0);
 smeshes[0].mesh.raw = tr64_core_be16(0);
 smeshes[0].flags.raw = tr64_core_be16(2); /* visible */

 tr64_u32_be_t mesh_ptrs[2];
 mesh_ptrs[0].raw = tr64_core_be32(10);
 mesh_ptrs[1].raw = tr64_core_be32(20);

 tr64_core_models_t models[1];
 memset(models, 0, sizeof(models));
 models[0].type.raw = tr64_core_be32(TR64_SKYBOX_MODEL_ID);
 models[0].mesh_count.raw = tr64_core_be16(1);
 models[0].mesh_start.raw = tr64_core_be16(1);
 models[0].frame_offset.raw = tr64_core_be32(0);

 /* Skybox frame (10 u16 words: 6 bounds, 3 offset, 1 packed rot = identity) */
 tr64_u16_be_t frames[12];
 memset(frames, 0, sizeof(frames));
 frames[9].raw = tr64_core_be16(0x4000u); /* single-axis X=0 */

 tr64_core_rooms_t rooms_tbl[2];
 memset(rooms_tbl, 0, sizeof(rooms_tbl));
 rooms_tbl[0].alternate_room.raw = tr64_core_be16(65535u);
 rooms_tbl[0].flags.raw = tr64_core_be16(TR64_ROOM_FLAG_OUTSIDE); /* air + outdoor */
 rooms_tbl[0].portal_start.raw = tr64_core_be32(0); rooms_tbl[0].portal_count.raw = tr64_core_be32(1);
 rooms_tbl[0].instance_start.raw = tr64_core_be32(0); rooms_tbl[0].instance_count.raw = tr64_core_be32(2);

 rooms_tbl[1].alternate_room.raw = tr64_core_be16(65535u);
 rooms_tbl[1].flags.raw = tr64_core_be16(1u); /* water */
 rooms_tbl[1].portal_start.raw = tr64_core_be32(1); rooms_tbl[1].portal_count.raw = tr64_core_be32(1);

 tr64_level_t level;
 memset(&level, 0, sizeof(level));
 level.ready = 1; level.game = 2; level.textures = tx; level.texture_bytes = sizeof(tx);
 level.tables[TR64_CORE_rooms - 1] = (tr64_level_table_t){.data = (const uint8_t *)rooms_tbl, .count = 2, .stride = sizeof(rooms_tbl[0])};
 level.tables[TR64_CORE_portals - 1] = (tr64_level_table_t){.data = (const uint8_t *)portals, .count = 2, .stride = sizeof(portals[0])};
 level.tables[TR64_CORE_static_instances - 1] = (tr64_level_table_t){.data = (const uint8_t *)sinsts, .count = 2, .stride = sizeof(sinsts[0])};
 level.tables[TR64_CORE_static_meshes - 1] = (tr64_level_table_t){.data = (const uint8_t *)smeshes, .count = 1, .stride = sizeof(smeshes[0])};
 level.tables[TR64_CORE_mesh_pointers - 1] = (tr64_level_table_t){.data = (const uint8_t *)mesh_ptrs, .count = 2, .stride = sizeof(mesh_ptrs[0])};
 level.tables[TR64_CORE_models - 1] = (tr64_level_table_t){.data = (const uint8_t *)models, .count = 1, .stride = sizeof(models[0])};
 level.tables[TR64_CORE_frames - 1] = (tr64_level_table_t){.data = (const uint8_t *)frames, .count = 12, .stride = sizeof(frames[0])};

 tr64_gpu_span_t rg_span = {rg, sizeof(rg)}, mg_span = {mg, sizeof(mg)};
 uint8_t work[4096] __attribute__((aligned(16)));
 tr64_render_t r_tr2;
 tr64_render_error_t err;
 if (tr64_render_bind_load(&r_tr2, &level, tx_span, rg_span, mg_span, work, sizeof(work), &err))
  fail("bind_load tr2", err.code, err.offset);

 const int32_t eye[3] = {0, 0, 0}, look[3] = {0, 0, 65536};
 tr64_render_pose_t p_air, p_water;
 if (tr64_render_camera(&p_air, eye, look, 0, 0, &err) ||
     tr64_render_camera(&p_water, eye, look, 1, 0, &err))
  fail("camera", err.code, 0);

 tr64_render_counts_t rcounts;
 /* 3a. Dry camera in air room 0 (sees air room 0 left, water room 1 right, skybox top/background). */
 tr64_render_set_lights(NULL, 0);
 if (tr64_render_frame(&r_tr2, &p_air, &rcounts, &err)) fail("dry frame", err.code, err.offset);
 uint16_t px_air_dry   = fb[120 * 320 + 110]; /* air room 0 */
 uint16_t px_wat_dry   = fb[120 * 320 + 210]; /* water room 1 */
 uint16_t px_sky_dry   = fb[20  * 320 + 160]; /* skybox */
 uint16_t px_snear_dry = fb[190 * 320 + 130]; /* near static 0 */
 uint16_t px_sfar_dry  = fb[178 * 320 + 210]; /* far static 1 */

 unsigned ar0, ag0, ab0, wr0, wg0, wb0, sr0, sg0, sb0;
 unpack5551(px_air_dry, &ar0, &ag0, &ab0);
 unpack5551(px_wat_dry, &wr0, &wg0, &wb0);
 unpack5551(px_sky_dry, &sr0, &sg0, &sb0);
 if (ar0 < 24 || ar0 != ag0 || ag0 != ab0) fail("air room dry baseline", ar0, ag0);
 if (sr0 < 24 || sr0 != sg0 || sg0 != sb0) fail("skybox dry baseline", sr0, sg0);
 if (wg0 <= wr0 || wb0 <= wr0) fail("water room baseline not cyan", wr0, wg0);

 /* 3b. Submerged camera in water room 1 (sees water room 1 right, air room 0 left, skybox top). */
 if (tr64_render_frame(&r_tr2, &p_water, &rcounts, &err)) fail("submerged frame", err.code, err.offset);
 uint16_t px_air_sub = fb[120 * 320 + 110];
 uint16_t px_wat_sub = fb[120 * 320 + 210];
 uint16_t px_sky_sub = fb[20  * 320 + 160];
 unsigned ar1_u, ag1_u, ab1_u, ar1, ag1, ab1, sr1, sg1, sb1;
 unpack5551(px_air_sub, &ar1_u, &ag1_u, &ab1_u);
 unpack5551(px_sky_sub, &sr1, &sg1, &sb1);
 if (ar1_u < ar0 / 2 || ar1_u > ar0 / 2 + 1 || ag1_u < 20 || ag1_u >= ag0 || ag1_u <= ar1_u || ab1_u != ag1_u)
  fail("air room underwater cyan tint mismatch", (ar1_u << 10) | (ag1_u << 5) | ab1_u, ar0);
 if (sr1 > sr0 / 2 + 2 || sg1 <= sr1 || sb1 != sg1)
  fail("skybox underwater cyan tint mismatch", (sr1 << 10) | (sg1 << 5) | sb1, sr0);
 if (px_wat_sub != px_wat_dry)
  fail("water room was double-tinted!", px_wat_sub, px_wat_dry);

 /* 3c. Lit air room viewed from underwater: stays cyan-clamped (<= 16, 25, 25 in 5-bit). */
 tr64_gpu_light_t flare_air = {.pos = {-60, 0, 512}, .radius = 2048, .peak = 4096, .shift = 10, .active = 1};
 tr64_render_set_lights(&flare_air, 1);
 if (tr64_render_frame(&r_tr2, &p_water, &rcounts, &err)) fail("submerged lit frame", err.code, err.offset);
 unpack5551(fb[120 * 320 + 110], &ar1, &ag1, &ab1);
 if (ar1 > 24 || ag1 <= ar1 || ab1 != ag1)
  fail("submerged lit air room lost cyan clamp", ar1, ag1);

 /* --- Test 4: Static mesh dynamic lighting (near static brightens, far static bit-identical) --- */
 tr64_gpu_light_t flare_st = {.pos = {-60, 140, 512}, .radius = 2048, .peak = 4096, .shift = 10, .active = 1};
 tr64_render_set_lights(&flare_st, 1);
 if (tr64_render_frame(&r_tr2, &p_air, &rcounts, &err)) fail("lit static frame", err.code, err.offset);
 uint16_t px_snear_lit = fb[190 * 320 + 130];
 uint16_t px_sfar_lit  = fb[178 * 320 + 210];
 unsigned snr0, sng0, snb0, snr1, sng1, snb1;
 unpack5551(px_snear_dry, &snr0, &sng0, &snb0);
 unpack5551(px_snear_lit, &snr1, &sng1, &snb1);
 if (snr1 < snr0 + 14) fail("near static did not brighten", snr0, snr1);
 if (px_sfar_lit != px_sfar_dry) fail("far static changed under near flare", px_sfar_lit, px_sfar_dry);

 /* --- Test 5: TR1 parity (level.game == 0: shadows, underwater tint, dynamic lights enabled) --- */
 level.game = 0;
 tr64_render_t r_tr1;
 uint8_t work1[4096] __attribute__((aligned(16)));
 if (tr64_render_bind_load(&r_tr1, &level, tx_span, rg_span, mg_span, work1, sizeof(work1), &err))
  fail("bind_load tr1", err.code, err.offset);
 g_emit_shadow = 1;
 tr64_render_set_lights(NULL, 0);
 tr64_game_drawable_t dummy_buf[4];
 tr64_game_view_t gview;
 memset(&gview, 0, sizeof(gview));
 memcpy(gview.camera_basis, p_water.view.basis, sizeof(gview.camera_basis));
 gview.camera_room = 1; /* water room: TR1 now applies camera_water tint + ground shadows! */
 g_total_batches = g_lit_batches = g_shadow_batches = 0;
 if (tr64_render_game_frame(&r_tr1, &gview, dummy_buf, 4, &rcounts, &err))
  fail("tr1 frame", err.code, err.offset);
 if (g_lit_batches == 0 || g_shadow_batches == 0)
  fail("TR1 missed lit/tint or shadow batches", g_lit_batches, g_shadow_batches);
 if (fb[120 * 320 + 110] != px_air_sub)
  fail("TR1 underwater camera tint on air room did not match TR2", fb[120 * 320 + 110], px_air_sub);
 if (fb[120 * 320 + 210] != px_wat_dry)
  fail("TR1 double-tinted water room", fb[120 * 320 + 210], px_wat_dry);

 printf("PASS synthetic blend-shadows (UBSan): add0xB0=(%u,%u,%u) shadow50=30->15 "
        "cam_water_air=(%u,%u,%u)->(%u,%u,%u) sky=(%u,%u,%u)->(%u,%u,%u) wat_unchanged=ok static_lit=%u->%u tr1=lit(%u)/sh(%u)/tint=ok\n",
        5u, 0u, 16u, ar0, ag0, ab0, ar1_u, ag1_u, ab1_u, sr0, sg0, sb0, sr1, sg1, sb1, snr0, snr1,
        g_lit_batches, g_shadow_batches);
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void tr64_gpu_init(void);
extern void tr64_render_scene_set_live(int live);
extern void tr64_render_scene_load(const tr64_level_t *level, const char *name, uint32_t pack_bytes);
extern void tr64_render_game_scene_prepare(void);
extern void tr64_render_game_scene_frame(const tr64_game_view_t *view,
                                         tr64_game_drawable_t *buffer, uint32_t capacity);

void __real_game_tr2_tick(uint32 mask);
void __real_tr64_level_load(tr64_level_t *out, uint32_t pack_bytes, const char *name);
uint32_t __real_game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity);
void __real_tr64_gpu_material(tr64_gpu_material_t m);

static int render_ready;
static uint32_t saved_pack_bytes;
static char saved_level_name[32];
static tr64_game_drawable_t frame_buf[TR64_GAME_DRAW_CHUNK];
static unsigned g_add_mats, g_blend_mats, g_shadow_count;

void __wrap_tr64_gpu_material(tr64_gpu_material_t m)
{
 if (m.flags & TR64_GPU_ADDITIVE) ++g_add_mats;
 else if ((m.flags & TR64_GPU_CLASS_MASK) == TR64_GPU_BLENDED_M1) ++g_blend_mats;
 __real_tr64_gpu_material(m);
}

uint32_t __wrap_game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity)
{
 if (getenv("TR64_BS_NO_SHADOWS")) return 0u;
 uint32_t n = __real_game_view_shadows(out, capacity);
 g_shadow_count = n;
 return n;
}

void __wrap_tr64_level_load(tr64_level_t *out, uint32_t pack_bytes, const char *name)
{
 saved_pack_bytes = pack_bytes;
 strncpy(saved_level_name, name, sizeof(saved_level_name) - 1);
 saved_level_name[sizeof(saved_level_name) - 1] = 0;
 __real_tr64_level_load(out, pack_bytes, name);
 if (getenv("TR64_BS_SHOT_OUT") || getenv("TR64_BS_RENDER_ALL")) {
  tr64_gpu_init();
  code_loading_begin();
  tr64_load_scope_begin();
  tr64_render_scene_set_live(1);
  tr64_render_scene_load(out, saved_level_name, saved_pack_bytes);
  tr64_load_scope_end();
  code_loading_end();
  render_ready = 1;
 }
}

static void set_custom_camera(tr64_game_view_t *view, const char *spec)
{
 long r, ex, ey, ez, tx, ty, tz;
 if (sscanf(spec, "%ld,%ld,%ld,%ld,%ld,%ld,%ld", &r, &ex, &ey, &ez, &tx, &ty, &tz) != 7)
  sys_error("bs probe: bad TR64_BS_CAM format");
 int16 ax = 0, ay = 0;
 anglesFromVector((int32)(tx - ex), (int32)(ty - ey), (int32)(tz - ez), &ax, &ay);
 uint32 txsc = gSinCosTable[(uint16)ax >> 4], tysc = gSinCosTable[(uint16)ay >> 4];
 int32 sx = wrap_i32(txsc) >> 16, cx = (int16)txsc;
 int32 sy = wrap_i32(tysc) >> 16, cy = (int16)tysc;
 view->camera_basis[0] = cy; view->camera_basis[1] = 0; view->camera_basis[2] = -sy;
 view->camera_basis[3] = (sx * sy) >> 14; view->camera_basis[4] = cx; view->camera_basis[5] = (sx * cy) >> 14;
 view->camera_basis[6] = (cx * sy) >> 14; view->camera_basis[7] = -sx; view->camera_basis[8] = (cx * cy) >> 14;
 view->camera_eye[0] = (int32)ex; view->camera_eye[1] = (int32)ey; view->camera_eye[2] = (int32)ez;
 view->camera_target[0] = (int32)tx; view->camera_target[1] = (int32)ty; view->camera_target[2] = (int32)tz;
 view->camera_room = (uint32)r;
}

void __wrap_game_tr2_tick(uint32 mask)
{
 __real_game_tr2_tick(mask);
 uint32 logicBefore = gs->gRandSeedLogic, drawBefore = gs->gRandSeedDraw;
 uint32 dynFloorBefore = gs->tr2 ? gs->tr2->dynamicFloorCalls : 0u;
 const FloorData *lastFdBefore = gs->gLastFloorData;
 uint16 lastSlantBefore = gs->gLastFloorSlant;

 tr64_game_view_t view;
 game_view_camera(&view);
 if (getenv("TR64_BS_LOG_TEXSTEP")) {
  printf("TEXSTEP %u %u %u\n", gs->tick, gs->textureTicks, view.texture_animation_step);
 }

 const char *shotOut = getenv("TR64_BS_SHOT_OUT");
 const char *shotTickEnv = getenv("TR64_BS_SHOT_TICK");
 bool renderAll = getenv("TR64_BS_RENDER_ALL") != NULL;
 bool takeShot = shotOut && shotTickEnv && gs->tick == (uint32)strtoul(shotTickEnv, NULL, 10);
 if (renderAll || takeShot) {
  if (render_ready == 1) {
   tr64_render_game_scene_prepare();
   render_ready = 2;
  }
  const char *cam = getenv("TR64_BS_CAM");
  if (cam && *cam) set_custom_camera(&view, cam);
  g_add_mats = g_blend_mats = g_shadow_count = 0;
  tr64_render_game_scene_frame(&view, frame_buf, TR64_GAME_DRAW_CHUNK);
  if (takeShot) {
   printf("SHOT tick=%u shadows=%u add_mats=%u blend_mats=%u\n",
          gs->tick, g_shadow_count, g_add_mats, g_blend_mats);
   if (sys_shot(shotOut) < 0) sys_error("bs probe: sys_shot failed");
  }
 }

 if (gs->gRandSeedLogic != logicBefore || gs->gRandSeedDraw != drawBefore ||
     (gs->tr2 && gs->tr2->dynamicFloorCalls != dynFloorBefore) ||
     gs->gLastFloorData != lastFdBefore || gs->gLastFloorSlant != lastSlantBefore)
  sys_error("bs probe: presentation mutated simulation state at tick %u", gs->tick);
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
    cmd = [
        'rtk', os.environ.get('HOST_CC', 'cc'), '-m32', '-std=c99', '-O2', '-g',
        '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-DTR64_HOST=1', '-DTR64_GAME=2', '-D_POSIX_C_SOURCE=200809L',
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
        '-Wl,--wrap=tr64_gpu_batch',
        '-o', str(exe),
    ]
    c = subprocess.run(cmd, capture_output=True, text=True)
    expect(c.returncode == 0, f'synthetic compile failed:\n{c.stdout}{c.stderr}')
    r = subprocess.run(['rtk', str(exe)], capture_output=True, text=True, timeout=30)
    expect(r.returncode == 0, f'synthetic test failed:\n{r.stdout}{r.stderr}')
    print(r.stdout.strip())


def build_probe(build_dir: Path, work: Path) -> Path:
    src = work / 'probe.c'
    obj = work / 'probe.o'
    exe = work / 'bs-host'
    src.write_text(PROBE_C, encoding='utf-8')
    gen = build_dir / 'generated'
    objs = sorted((build_dir / 'host').rglob('*.o'))
    expect(bool(objs), f'no host objects under {build_dir / "host"}')
    c = subprocess.run([
        'rtk', 'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}', f'-I{gen / "host"}',
        '-DTR64_GAME=2', '-DTR64_HOST=1', '-D_POSIX_C_SOURCE=200809L',
        '-c', str(src), '-o', str(obj),
    ], capture_output=True, text=True)
    expect(c.returncode == 0, f'probe compile failed:\n{c.stdout}{c.stderr}')
    l = subprocess.run([
        'rtk', 'cc', '-m32',
        '-Wl,--wrap=game_tr2_tick', '-Wl,--wrap=tr64_level_load',
        '-Wl,--wrap=game_view_shadows', '-Wl,--wrap=tr64_gpu_material',
        '-o', str(exe), str(obj), *(str(p) for p in objs),
    ], capture_output=True, text=True)
    expect(l.returncode == 0, f'probe link failed:\n{l.stdout}{l.stderr}')
    return exe


def run_probe(exe: Path, pack: Path, inp: Path, trace: Path, schema: int, extra_env: dict[str, str]):
    env = dict(os.environ)
    env.update(extra_env)
    p = subprocess.run(
        ['rtk', str(exe), f'--game-tr2-schema{schema}', str(pack), str(inp), str(trace)],
        capture_output=True, text=True, env=env, timeout=120)
    expect(p.returncode == 0, f'probe failed ({inp.name}):\n{p.stdout[-800:]}{p.stderr[-800:]}')
    shot = {}
    texsteps = []
    for line in p.stdout.splitlines():
        if line.startswith('SHOT '):
            for kv in line.split()[1:]:
                k, v = kv.split('=')
                shot[k] = int(v)
        elif line.startswith('TEXSTEP '):
            parts = line.split()
            texsteps.append((int(parts[1]), int(parts[2]), int(parts[3])))
    return shot, texsteps


def run_real_packs(build_dir: Path, wall_pack: Path, boat_pack: Path, work: Path) -> None:
    exe = build_probe(build_dir, work)
    plain_host = build_dir / 'host/tr64-host'

    # 1. Great Wall lit flare: additive material used, room+flare brightens, zero dark halo.
    place = 'place 62976 5504 27136 0 35'
    inp_lit = work / 'flare_lit.txt'
    inp_unlit = work / 'flare_unlit.txt'
    inp_lit.write_text('\n'.join([place, '0', '8192'] + ['0'] * 100) + '\n', encoding='ascii')
    inp_unlit.write_text('\n'.join([place] + ['0'] * 102) + '\n', encoding='ascii')
    ppm_lit, ppm_unlit = work / 'flare_lit.ppm', work / 'flare_unlit.ppm'
    shot_lit, _ = run_probe(exe, wall_pack, inp_lit, work / 'flare_lit.jsonl', 21,
                            {'TR64_BS_SHOT_TICK': '95', 'TR64_BS_SHOT_OUT': str(ppm_lit)})
    run_probe(exe, wall_pack, inp_unlit, work / 'flare_unlit.jsonl', 21,
              {'TR64_BS_SHOT_TICK': '95', 'TR64_BS_SHOT_OUT': str(ppm_unlit)})
    expect(shot_lit.get('add_mats', 0) >= 1, f'expected additive flare glow material, got {shot_lit}')
    px_lit, px_unlit = read_ppm(ppm_lit), read_ppm(ppm_unlit)
    sum_lit, sum_unlit = sum(px_lit), sum(px_unlit)
    expect(sum_lit > sum_unlit * 2, f'lit flare did not brighten room ({sum_unlit} -> {sum_lit})')
    print(f'PASS real 1/4 Great Wall flare additive: add_mats={shot_lit["add_mats"]} luma={sum_unlit}->{sum_lit}')

    # 2. Ground shadows on Great Wall (placed standing in room 35) & Venice start + negative control + trace parity.
    # Place Lara standing on flat stone floor in Great Wall room 35 with camera looking at her feet.
    inp_sh_wall = work / 'sh_wall.txt'
    inp_sh_wall.write_text('\n'.join([place] + ['0'] * 5) + '\n', encoding='ascii')
    ppm_w_on, ppm_w_off = work / 'w_sh_on.ppm', work / 'w_sh_off.ppm'
    tr_w_on, tr_w_plain = work / 'w_sh_on.jsonl', work / 'w_sh_plain.jsonl'
    cam_w = '35,62976,4700,25600,62976,5504,27136'
    sw_on, _ = run_probe(exe, wall_pack, inp_sh_wall, tr_w_on, 21,
                         {'TR64_BS_SHOT_TICK': '2', 'TR64_BS_SHOT_OUT': str(ppm_w_on),
                          'TR64_BS_CAM': cam_w, 'TR64_BS_RENDER_ALL': '1'})
    run_probe(exe, wall_pack, inp_sh_wall, work / 'w_sh_off.jsonl', 21,
              {'TR64_BS_SHOT_TICK': '2', 'TR64_BS_SHOT_OUT': str(ppm_w_off),
               'TR64_BS_CAM': cam_w, 'TR64_BS_RENDER_ALL': '1', 'TR64_BS_NO_SHADOWS': '1'})
    p_plain = subprocess.run(['rtk', str(plain_host), '--game-tr2-schema21',
                              str(wall_pack), str(inp_sh_wall), str(tr_w_plain)],
                             capture_output=True, text=True, timeout=60)
    expect(p_plain.returncode == 0, f'plain wall failed: {p_plain.stderr}')
    expect(tr_w_on.read_bytes() == tr_w_plain.read_bytes(), 'shadow rendering mutated wall trace!')
    expect(sw_on.get('shadows', 0) >= 1, f'expected Lara shadow on Great Wall, got {sw_on}')
    pw_on, pw_off = read_ppm(ppm_w_on), read_ppm(ppm_w_off)
    w_dark = sum(1 for k in range(0, len(pw_on), 3)
                 if (pw_on[k] + pw_on[k + 1] + pw_on[k + 2]) < (pw_off[k] + pw_off[k + 1] + pw_off[k + 2]) - 10)
    w_bright = sum(1 for k in range(0, len(pw_on), 3)
                   if (pw_on[k] + pw_on[k + 1] + pw_on[k + 2]) > (pw_off[k] + pw_off[k + 1] + pw_off[k + 2]))
    expect(w_dark >= 30 and w_bright == 0,
           f'Great Wall shadow pixel check failed: darkened={w_dark} brightened={w_bright}')

    # Venice start (boat.pack): Lara lands on the dock by tick 25; verify Lara + boat shadows and darkened dock floor.
    inp_sh_boat = work / 'sh_boat.txt'
    inp_sh_boat.write_text('\n'.join(['0'] * 26) + '\n', encoding='ascii')
    ppm_b_on, ppm_b_off = work / 'b_sh_on.ppm', work / 'b_sh_off.ppm'
    tr_b_on, tr_b_plain = work / 'b_sh_on.jsonl', work / 'b_sh_plain.jsonl'
    sb_on, texsteps = run_probe(exe, boat_pack, inp_sh_boat, tr_b_on, 22,
                                {'TR64_BS_SHOT_TICK': '25', 'TR64_BS_SHOT_OUT': str(ppm_b_on),
                                 'TR64_BS_RENDER_ALL': '1', 'TR64_BS_LOG_TEXSTEP': '1'})
    run_probe(exe, boat_pack, inp_sh_boat, work / 'b_sh_off.jsonl', 22,
              {'TR64_BS_SHOT_TICK': '25', 'TR64_BS_SHOT_OUT': str(ppm_b_off),
               'TR64_BS_RENDER_ALL': '1', 'TR64_BS_NO_SHADOWS': '1'})
    pb_plain = subprocess.run(['rtk', str(plain_host), '--game-tr2-schema22',
                               str(boat_pack), str(inp_sh_boat), str(tr_b_plain)],
                              capture_output=True, text=True, timeout=60)
    expect(pb_plain.returncode == 0, f'plain boat failed: {pb_plain.stderr}')
    expect(tr_b_on.read_bytes() == tr_b_plain.read_bytes(), 'shadow rendering mutated boat trace!')
    expect(sb_on.get('shadows', 0) >= 2, f'expected Lara + boat shadows on Venice, got {sb_on}')
    pb_on, pb_off = read_ppm(ppm_b_on), read_ppm(ppm_b_off)
    b_dark = sum(1 for k in range(0, len(pb_on), 3)
                 if (pb_on[k] + pb_on[k + 1] + pb_on[k + 2]) < (pb_off[k] + pb_off[k + 1] + pb_off[k + 2]) - 10)
    b_bright = sum(1 for k in range(0, len(pb_on), 3)
                   if (pb_on[k] + pb_on[k + 1] + pb_on[k + 2]) > (pb_off[k] + pb_off[k + 1] + pb_off[k + 2]))
    expect(b_dark >= 20 and b_bright == 0,
           f'Venice shadow pixel check failed: darkened={b_dark} brightened={b_bright}')
    print(f'PASS real 2/4 ground shadows: wall(shadows={sw_on["shadows"]} darkened={w_dark} brightened={w_bright}) '
          f'venice(shadows={sb_on["shadows"]} darkened={b_dark} brightened={b_bright}) trace=100%_identical')

    # 3. Great Wall submerged camera in pool room 37 looking up at outdoor air room & sunset skybox.
    inp_uw = work / 'uw.txt'
    inp_uw.write_text('0\n0\n', encoding='ascii')
    ppm_uw = work / 'uw_sky.ppm'
    cam_uw = '37,68476,5800,36647,68476,3500,37200'
    run_probe(exe, wall_pack, inp_uw, work / 'uw.jsonl', 21,
              {'TR64_BS_SHOT_TICK': '1', 'TR64_BS_SHOT_OUT': str(ppm_uw), 'TR64_BS_CAM': cam_uw})
    px_uw = read_ppm(ppm_uw)
    # Inspect non-black pixels across the upper/middle frame looking up through water into air+sky.
    r_sum = g_sum = b_sum = lit_cnt = 0
    for y in range(0, 180):
        row = y * 320 * 3
        for x in range(320):
            p = row + x * 3
            pr, pg, pb = px_uw[p], px_uw[p + 1], px_uw[p + 2]
            if pr + pg + pb > 30:
                r_sum += pr; g_sum += pg; b_sum += pb; lit_cnt += 1
    expect(lit_cnt >= 5000 and g_sum > r_sum * 1.25 and b_sum > r_sum * 1.25,
           f'submerged camera sky/air not cyan-tinted: lit={lit_cnt} R={r_sum} G={g_sum} B={b_sum}')
    print(f'PASS real 3/4 underwater camera cyan tint (room 37 looking up): '
          f'lit_px={lit_cnt} mean_RGB=({r_sum//lit_cnt},{g_sum//lit_cnt},{b_sum//lit_cnt})')

    # 4. Host TR2 animated-texture period fix: verify period == 10 (NOT 5).
    by_tt = {tt: step for _, tt, step in texsteps}
    expect(10 in by_tt and 11 in by_tt, f'missing textureTicks 10/11 in {texsteps}')
    expect(by_tt[10] == 0 and by_tt[11] == 1,
           f'TR2 host texture period wrong: tt10->{by_tt[10]} tt11->{by_tt[11]} (expected 0, 1)')
    print(f'PASS real 4/4 host TR2 animated-texture period=10: tt10->step={by_tt[10]} tt11->step={by_tt[11]}')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=None, help='TR2 host build directory')
    ap.add_argument('--wall-pack', type=Path, default=None, help='Cooked Great Wall pack')
    ap.add_argument('--boat-pack', type=Path, default=None, help='Cooked Venice pack')
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')
    with tempfile.TemporaryDirectory(prefix='tr2-blend-shadows-', dir=scratch) as td:
        work = Path(td)
        run_synthetic(work)
        if args.build and args.wall_pack and args.boat_pack:
            run_real_packs(args.build, args.wall_pack, args.boat_pack, work)
    print('ALL TR2 BLEND/SHADOWS/WATER-TINT TESTS PASSED')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
