#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for Graphics Options (m10-graphics-options-v1):

1. Synthetic UBSan C harness (-fsanitize=undefined -fno-sanitize-recover=all, no retail data):
   - tr64_render_fog_range(0..16): exact endpoints/default (0->4096/3276, 8->10240/8192,
     16->14336/11468), clamping, strict monotonicity.
   - tr64_render_auto_dist_step: EMA smoothing, 4-frame cooldown, floor 4608 under high
     load (40000 us), hysteresis hold at 28000 us, smooth expansion back to max_far at
     15000 us, pause spike clamp (5,000,000 us) without overflow.
   - tr64_gpu_projection_valid: accepts [4096, 14336] with fog_near in [2048, far-1]
     and fog_far == far; rejects out-of-range/invalid projections.
   - tr64_gpu_set_filter: defaults to 0 (Point), toggles cleanly to 1 (Bilinear) and 0.
   - Synthetic host GPU + rasterizer draw-distance test:
     * Default 10240/8192 draws near (z=1000) and far (z=7000) batches unfogged.
     * Lowered 4096/3276 culls the z=7000 batch (triangles drop, far quad absent).
     * Mid 8000/6400 draws z=7000 partially fogged (0 < mid_luma < default_luma).
     * Extended 14336/11468 draws z=12000 partially fogged without clipping/rejection.
   - Settings persistence (engine/save/game.c + medium.c over RAM SRAM bank):
     * Legacy 32 B (8-word) record loads TR64_SAVE_OK with video_filter=0, video_auto_dist=0.
     * eng_game_save_settings() writes 40 B (10 words) and round-trips video_filter=1,
       video_auto_dist=1 cleanly.
     * Out-of-range words (video_filter=2 / video_auto_dist=2) and invalid size (36 B)
       rejected as TR64_SAVE_CORRUPT.

2. Real-pack host verification (--build, --wall-pack, --boat-pack, optional --tr1-pack):
   - Great Wall outdoor start: video_fog=8 (default) vs video_fog=0 (far_z=4096) culls
     distant batches (triangles strictly lower), keeps skybox visible, toggles GPU filter
     via video_filter, and preserves 100% bit-identical simulation trace vs plain tr64-host.
   - TR2 Options ring (Great Wall + Venice): Start -> Down -> Action opens Graphics Options
     (6 entries: Gamma, Draw Distance, Show FPS, VSync, Filtering, Auto Distance), toggles
     Filtering & Auto Distance and adjusts Draw Distance, closes cleanly with choice==0
     and zero carried[] mutation.
   - TR1 Options ring (when --tr1-pack provided): SLOT_DETAIL preserves indices 0..3 and
     appends Filtering (4) & Auto Distance (5), toggling both and invoking save-settings.
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
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/load.h"
#include "engine/save/game.h"
#include "engine/save/save.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "plat/host/gpu-platform.h"

extern int tr64_gpu_host_filter(void);
extern uint32_t tr64_unit_medium(uint32_t argument);

static uint8_t mem_arena[2 * 1024 * 1024] __attribute__((aligned(16)));
static uint32_t mem_used;

void sys_error(const char *fmt, ...)
{
 va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
 fputc('\n', stderr); abort();
}
uint32_t sys_time_us(void) { static uint32_t t = 1000; return t += 1000; }
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

/* Save medium RAM bank & minimal stubs for engine/save/game.c */
static uint8_t sram_bank[TR64_SAVE_BYTES];
static int medium_loaded;
void code_load(const char *name) { assert(!strcmp(name, "medium")); medium_loaded = 1; }
uint32_t code_call(const char *name, uint32_t arg)
{
 assert(medium_loaded && !strcmp(name, "medium"));
 return tr64_unit_medium(arg);
}
int tr64_save_platform_init(void) { return TR64_SAVE_OK; }
int tr64_save_platform_read(uint32_t off, void *data, uint32_t bytes)
{
 assert(off <= sizeof(sram_bank) && bytes <= sizeof(sram_bank) - off);
 memcpy(data, sram_bank + off, bytes);
 return 0;
}
int tr64_save_platform_write(uint32_t off, const void *data, uint32_t bytes)
{
 assert(off <= sizeof(sram_bank) && bytes <= sizeof(sram_bank) - off);
 memcpy(sram_bank + off, data, bytes);
 return 0;
}
uint32_t tr64_save_level_identity(int32_t level_id) { return 0x12340000u + (uint32_t)level_id; }

/* Minimal game stubs */
static tr64_game_settings_t g_settings = {
 .video_fog = 8, .video_fps = 1, .audio_sfx = 1, .audio_music = 1, .controls_vibration = 1
};
const tr64_game_settings_t *game_settings_view(void) { return &g_settings; }
int game_settings_apply(const tr64_game_settings_t *s)
{
 if (!s || (uint32_t)s->video_gamma > 31u || (uint32_t)s->video_fog > 31u ||
     (uint32_t)s->video_fps > 1u || (uint32_t)s->video_vsync > 1u ||
     (uint32_t)s->audio_sfx > 1u || (uint32_t)s->audio_music > 1u ||
     (uint32_t)s->controls_vibration > 1u || (uint32_t)s->controls_swap > 1u ||
     (uint32_t)s->video_filter > 1u || (uint32_t)s->video_auto_dist > 1u ||
     (uint32_t)s->controls_abilities > 2u)
  return 0;
 g_settings = *s;
 return 1;
}
int32_t game_level_id(void) { return 1; }
int game_save_blocked(void) { return 0; }
uint32_t game_save(void *b, uint32_t c) { (void)b; (void)c; return 0; }
int game_load(const void *b, uint32_t c) { (void)b; (void)c; return 0; }
int game_stats_view(tr64_game_level_stats_t *o) { memset(o, 0, sizeof(*o)); return 1; }
int game_cutscene_active(void) { return 0; }
uint32_t game_view_room(uint32_t room, uint32_t *cursor, tr64_game_drawable_t *out, uint32_t cap)
{ (void)room; (void)out; (void)cap; *cursor = TR64_GAME_VIEW_END; return 0; }
uint32_t game_rest_item_view(uint32_t item, tr64_game_drawable_t *out, uint32_t cap)
{ (void)item; (void)out; (void)cap; return 0; }
void tr64_pack_mount(tr64_pack_t *p, uint32_t b, const char *n) { (void)p; (void)b; (void)n; abort(); }
void tr64_pack_begin_kind(tr64_pack_t *p, uint32_t k, int pool) { (void)p; (void)k; (void)pool; abort(); }
int tr64_pack_poll(tr64_pack_t *p) { (void)p; return -1; }
const void *tr64_pack_data(const tr64_pack_t *p, uint32_t *b) { (void)p; (void)b; return NULL; }
void tr64_pack_unmount(tr64_pack_t *p) { (void)p; }
void tr64_io_frame(void) { }
void tr64_io_pump(void) { }
void tr64_mem_log(void) { }
int tr64_geo_open(tr64_geo_t *o, tr64_gpu_span_t s, int m, tr64_render_error_t *e)
{ return tr64_geo_open_load(o, s, m, e); }
size_t tr64_render_workspace_bytes(const tr64_level_t *c, const tr64_geo_t *r, const tr64_geo_t *m)
{ return tr64_render_workspace_bytes_load(c, r, m); }
int tr64_render_bind(tr64_render_t *ren, const tr64_level_t *c, tr64_gpu_span_t t,
                     tr64_gpu_span_t r, tr64_gpu_span_t m, void *ws, size_t b, tr64_render_error_t *e)
{ return tr64_render_bind_load(ren, c, t, r, m, ws, b, e); }

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

static void pack_vert_pair(uint8_t *dst,
                           int16_t x0, int16_t y0, int16_t z0,
                           int16_t x1, int16_t y1, int16_t z1)
{
 memset(dst, 0, 32);
 ws16(dst + 0, x0); ws16(dst + 2, y0); ws16(dst + 4, z0);
 ws16(dst + 8, x1); ws16(dst + 10, y1); ws16(dst + 12, z1);
 dst[16] = 171; dst[17] = 171; dst[18] = 171; dst[19] = 255;
 dst[20] = 171; dst[21] = 171; dst[22] = 171; dst[23] = 255;
}

static size_t build_tx64(uint8_t *buf)
{
 /* 1 flat white material (index 0) */
 memset(buf, 0, 96);
 memcpy(buf, "TX64", 4);
 w32(buf + 4, 1); w32(buf + 8, 90);
 w32(buf + 32, 1); w32(buf + 36, 1);
 w32(buf + 40, 80); w32(buf + 44, 80); w32(buf + 48, 80);
 w32(buf + 52, 80); w32(buf + 56, 80); w32(buf + 60, 80); w32(buf + 64, 88);
 w16(buf + 80, 0); w16(buf + 82, rgb5551(31, 31, 31));
 return 90;
}

static size_t build_empty_mg64(uint8_t *buf)
{
 memset(buf, 0, 64);
 memcpy(buf, "MG64", 4);
 w32(buf + 4, 1); w32(buf + 8, 64);
 w32(buf + 32, 64); w32(buf + 36, 64); w32(buf + 40, 64);
 w32(buf + 44, 64); w32(buf + 48, 64); w32(buf + 56, 1);
 return 64;
}

/* Build 1-room RG64 with 2 separate batches:
 * batch 0: left half quad at z_near
 * batch 1: right half quad at z_far */
static size_t build_two_batch_rg64(uint8_t *buf, int16_t z0, int16_t z1)
{
 /* header=64, 1 object=32, 2 batches=64, 2 runs=48, 2 faces=16, 4 pairs=128 -> total 352 */
 const uint32_t obj_off = 64, bat_off = 96, run_off = 160, fac_off = 208, vtx_off = 224, total = 352;
 memset(buf, 0, total);
 memcpy(buf, "RG64", 4);
 w32(buf + 4, 1); w32(buf + 8, total);
 w32(buf + 12, 1); w32(buf + 16, 2); w32(buf + 20, 2);
 w32(buf + 24, 2); w32(buf + 28, 4);
 w32(buf + 32, obj_off); w32(buf + 36, bat_off); w32(buf + 40, run_off);
 w32(buf + 44, fac_off); w32(buf + 48, vtx_off);
 w32(buf + 52, 4); w32(buf + 56, 1);
 /* Object 0 */
 uint8_t *o = buf + obj_off;
 w32(o + 0, 0); w32(o + 4, 0); w16(o + 8, 2);
 ws16(o + 12, -2000); ws16(o + 14, -2000); ws16(o + 16, z0);
 ws16(o + 18, 2000);  ws16(o + 20, 2000);  ws16(o + 22, z1);
 /* Batch 0: x in [-400, -50], y in [-200, 200], z = z0 */
 uint8_t *b0 = buf + bat_off;
 ws16(b0 + 0, -400); ws16(b0 + 2, -200); ws16(b0 + 4, z0);
 ws16(b0 + 6, -50);  ws16(b0 + 8, 200);  ws16(b0 + 10, z0);
 w16(b0 + 12, 4); w16(b0 + 14, 0); w32(b0 + 16, vtx_off); w16(b0 + 20, 1);
 /* Batch 1: x in [50, 2000], y in [-2000, 2000], z = z1 */
 uint8_t *b1 = buf + bat_off + 32;
 ws16(b1 + 0, 50);   ws16(b1 + 2, -2000); ws16(b1 + 4, z1);
 ws16(b1 + 6, 2000); ws16(b1 + 8, 2000);  ws16(b1 + 10, z1);
 w16(b1 + 12, 4); w16(b1 + 14, 0); w32(b1 + 16, vtx_off + 64); w16(b1 + 20, 1);
 /* Run 0 (flat double-sided, batch 0, 1 face) */
 uint8_t *r0 = buf + run_off;
 w16(r0 + 0, 0); r0[2] = 0; r0[3] = TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED;
 w16(r0 + 4, 0); w16(r0 + 6, 1);
 /* Run 1 (flat double-sided, batch 1, 1 face) */
 uint8_t *r1 = buf + run_off + 24;
 w16(r1 + 0, 0); r1[2] = 0; r1[3] = TR64_GPU_FLAT | TR64_GPU_DOUBLE_SIDED;
 w16(r1 + 4, 1); w16(r1 + 6, 1);
 /* Faces 0 & 1 (quads 0,1,2,3) */
 uint8_t *f0 = buf + fac_off;
 f0[0] = 0; f0[1] = 1; f0[2] = 2; f0[3] = 3;
 uint8_t *f1 = buf + fac_off + 8;
 f1[0] = 0; f1[1] = 1; f1[2] = 2; f1[3] = 3; w16(f1 + 6, 1);
 /* Vertices: batch 0 */
 pack_vert_pair(buf + vtx_off + 0,  -400, -200, z0,  -50, -200, z0);
 pack_vert_pair(buf + vtx_off + 32,  -50,  200, z0, -400,  200, z0);
 /* Vertices: batch 1 */
 pack_vert_pair(buf + vtx_off + 64,   50, -2000, z1, 2000, -2000, z1);
 pack_vert_pair(buf + vtx_off + 96, 2000,  2000, z1,   50,  2000, z1);
 return total;
}

static void init_minimal_core(tr64_level_t *core, uint8_t *room_rec,
                              const uint8_t *tx, uint32_t tx_bytes)
{
 memset(core, 0, sizeof(*core));
 core->ready = 1;
 core->game = 2;
 core->textures = tx;
 core->texture_bytes = tx_bytes;
 memset(room_rec, 0, sizeof(tr64_core_rooms_t));
 tr64_core_rooms_t *rm = (tr64_core_rooms_t *)room_rec;
 w16((uint8_t *)&rm->alternate_room, 0xffffu);
 core->tables[TR64_CORE_rooms - 1] = (tr64_level_table_t){
  .base = room_rec, .data = room_rec, .bytes = sizeof(tr64_core_rooms_t),
  .count = 1, .stride = sizeof(tr64_core_rooms_t)
 };
}

static void test_fog_range(void)
{
 uint16_t f, n, prev_f = 0, prev_n = 0;
 tr64_render_fog_range(0, &f, &n);
 assert(f == 4096 && n == 3276);
 tr64_render_fog_range(8, &f, &n);
 assert(f == 10240 && n == 8192);
 tr64_render_fog_range(16, &f, &n);
 assert(f == 14336 && n == 11468);
 /* Clamping out-of-range inputs */
 tr64_render_fog_range(-5, &f, &n);
 assert(f == 4096 && n == 3276);
 tr64_render_fog_range(99, &f, &n);
 assert(f == 14336 && n == 11468);
 for (int32_t v = 0; v <= 16; ++v) {
  tr64_render_fog_range(v, &f, &n);
  assert(f > prev_f && n > prev_n && n < f);
  prev_f = f; prev_n = n;
 }
 printf("PASS synth 1/6 tr64_render_fog_range(0..16): 0->4096/3276, 8->10240/8192, 16->14336/11468\n");
}

static void test_auto_dist(void)
{
 tr64_render_auto_dist_t ctrl;
 tr64_render_auto_dist_reset(&ctrl, 10240);
 assert(ctrl.cur_far == 10240);
 /* Mid load (36500 us): holds steady in hysteresis band [35000, 38000] */
 for (int i = 0; i < 40; ++i) {
  uint16_t f = tr64_render_auto_dist_step(&ctrl, 36500u, 10240);
  assert(f == 10240);
 }
 /* High load (50000 us = 20 fps): steps down smoothly to floor 4608, never below */
 uint16_t prev = ctrl.cur_far;
 int drops = 0;
 for (int i = 0; i < 200; ++i) {
  uint16_t f = tr64_render_auto_dist_step(&ctrl, 50000u, 10240);
  assert(f <= prev && f >= 4608u);
  if (f < prev) {
   assert((unsigned)(prev - f) <= 256u);
   ++drops;
  }
  prev = f;
 }
 assert(ctrl.cur_far == 4608u && drops == 22);
 /* Mid load at floor: holds steady at 4608 with zero oscillation */
 for (int i = 0; i < 40; ++i) (void)tr64_render_auto_dist_step(&ctrl, 36500u, 10240);
 uint16_t held = ctrl.cur_far;
 for (int i = 0; i < 40; ++i) {
  assert(tr64_render_auto_dist_step(&ctrl, 36500u, 10240) == held);
 }
 /* Low load (33333 us = locked 30 fps): smoothly expands back to max_far (10240), never exceeds */
 prev = ctrl.cur_far;
 int rises = 0;
 for (int i = 0; i < 300; ++i) {
  uint16_t f = tr64_render_auto_dist_step(&ctrl, 33333u, 10240);
  assert(f >= prev && f <= 10240u);
  if (f > prev) {
   assert((unsigned)(f - prev) <= 128u);
   ++rises;
  }
  prev = f;
 }
 assert(ctrl.cur_far == 10240u && rises > 0);
 /* Pause spike (5,000,000 us): clamped cleanly to 100000 us without overflow */
 tr64_render_auto_dist_reset(&ctrl, 10240);
 uint16_t after_spike = tr64_render_auto_dist_step(&ctrl, 5000000u, 10240);
 assert(ctrl.ema_us <= 100000u && after_spike >= 4608u && after_spike <= 10240u);
 printf("PASS synth 2/6 tr64_render_auto_dist_step: floor=4608 drops=%d rises=%d spike_ema=%u\n",
        drops, rises, (unsigned)ctrl.ema_us);
}

static void test_projection_and_filter(void)
{
 tr64_gpu_projection_t p = TR64_GPU_WORLD_PROJECTION;
 assert(tr64_gpu_projection_valid(&p));
 p.far_z = p.fog_far_z = 4096; p.fog_near_z = 3276;
 assert(tr64_gpu_projection_valid(&p));
 p.far_z = p.fog_far_z = 14336; p.fog_near_z = 11468;
 assert(tr64_gpu_projection_valid(&p));
 /* Invalid projections */
 p.far_z = p.fog_far_z = 4095; p.fog_near_z = 3000;
 assert(!tr64_gpu_projection_valid(&p));
 p.far_z = p.fog_far_z = 14337; p.fog_near_z = 11000;
 assert(!tr64_gpu_projection_valid(&p));
 p.far_z = p.fog_far_z = 8000; p.fog_near_z = 2047;
 assert(!tr64_gpu_projection_valid(&p));
 p.far_z = p.fog_far_z = 8000; p.fog_near_z = 8000;
 assert(!tr64_gpu_projection_valid(&p));
 p.far_z = 8000; p.fog_far_z = 8001; p.fog_near_z = 6400;
 assert(!tr64_gpu_projection_valid(&p));
 /* Filter toggle */
 assert(tr64_gpu_host_filter() == 0);
 tr64_gpu_set_filter(1);
 assert(tr64_gpu_host_filter() == 1);
 tr64_gpu_set_filter(0);
 assert(tr64_gpu_host_filter() == 0);
 printf("PASS synth 3/6 tr64_gpu_projection_valid + tr64_gpu_set_filter\n");
}

static unsigned sample_pixel_luma(int x, int y)
{
 uint16_t px = tr64_gpu_host_pixels()[y * 320 + x];
 unsigned r, g, b;
 unpack5551(px, &r, &g, &b);
 return r + g + b;
}

static void test_synthetic_draw_distance(void)
{
 uint8_t tx[128] __attribute__((aligned(16)));
 uint8_t mg[128] __attribute__((aligned(16)));
 uint8_t rg[512] __attribute__((aligned(16)));
 uint8_t room_rec[sizeof(tr64_core_rooms_t)] __attribute__((aligned(16)));
 size_t tx_n = build_tx64(tx);
 size_t mg_n = build_empty_mg64(mg);
 size_t rg_n = build_two_batch_rg64(rg, 1000, 7000);
 tr64_level_t core;
 init_minimal_core(&core, room_rec, tx, (uint32_t)tx_n);

 tr64_geo_t rgeo, mgeo;
 tr64_render_error_t err;
 assert(tr64_geo_open(&rgeo, (tr64_gpu_span_t){rg, (uint32_t)rg_n}, 0, &err) == 0);
 assert(tr64_geo_open(&mgeo, (tr64_gpu_span_t){mg, (uint32_t)mg_n}, 1, &err) == 0);
 size_t ws_bytes = tr64_render_workspace_bytes(&core, &rgeo, &mgeo);
 void *ws = tr64_mem_alloc(TR64_MEM_state, (uint32_t)ws_bytes);
 tr64_render_t ren;
 assert(tr64_render_bind(&ren, &core, (tr64_gpu_span_t){tx, (uint32_t)tx_n},
                         (tr64_gpu_span_t){rg, (uint32_t)rg_n},
                         (tr64_gpu_span_t){mg, (uint32_t)mg_n}, ws, ws_bytes, &err) == 0);

 tr64_render_pose_t pose;
 const int32_t eye[3] = {0, 0, 0}, look[3] = {0, 0, 65536};
 assert(tr64_render_camera(&pose, eye, look, 0, 0, &err) == 0);

 tr64_render_counts_t c_def, c_low, c_mid;
 /* 1. Default 10240/8192: both z=1000 and z=7000 drawn unfogged */
 assert(tr64_render_frame(&ren, &pose, &c_def, &err) == 0);
 unsigned near_def = sample_pixel_luma(110, 120);
 unsigned far_def  = sample_pixel_luma(200, 120);
 assert(c_def.gpu.triangles == 4 && near_def > 70 && far_def > 70);

 /* 2. Lowered 4096/3276: z=7000 batch culled -> triangles drops 4 -> 2, far pixels black */
 pose.projection.far_z = pose.projection.fog_far_z = 4096;
 pose.projection.fog_near_z = 3276;
 assert(tr64_render_frame(&ren, &pose, &c_low, &err) == 0);
 unsigned near_low = sample_pixel_luma(110, 120);
 unsigned far_low  = sample_pixel_luma(200, 120);
 assert(c_low.gpu.triangles == 2 && near_low == near_def && far_low == 0);

 /* 3. Mid 8000/6400: z=7000 batch drawn AND fogged between 6400..8000 */
 pose.projection.far_z = pose.projection.fog_far_z = 8000;
 pose.projection.fog_near_z = 6400;
 assert(tr64_render_frame(&ren, &pose, &c_mid, &err) == 0);
 unsigned far_mid = sample_pixel_luma(200, 120);
 assert(c_mid.gpu.triangles == 4 && far_mid > 0 && far_mid < far_def);

 /* 4. Extended 14336/11468: quad at z=12000 drawn and partially fogged */
 rg_n = build_two_batch_rg64(rg, 1000, 12000);
 assert(tr64_geo_open(&rgeo, (tr64_gpu_span_t){rg, (uint32_t)rg_n}, 0, &err) == 0);
 assert(tr64_render_bind(&ren, &core, (tr64_gpu_span_t){tx, (uint32_t)tx_n},
                         (tr64_gpu_span_t){rg, (uint32_t)rg_n},
                         (tr64_gpu_span_t){mg, (uint32_t)mg_n}, ws, ws_bytes, &err) == 0);
 pose.projection.far_z = pose.projection.fog_far_z = 14336;
 pose.projection.fog_near_z = 11468;
 tr64_render_counts_t c_ext;
 assert(tr64_render_frame(&ren, &pose, &c_ext, &err) == 0);
 unsigned far_ext = sample_pixel_luma(200, 120);
 assert(c_ext.gpu.triangles == 4 && far_ext > 0 && far_ext < near_def);

 printf("PASS synth 4/6 draw-distance cull+fog: def(tri=%u,luma=%u) low(tri=%u,luma=%u) mid(tri=%u,luma=%u) ext12k(tri=%u,luma=%u)\n",
        c_def.gpu.triangles, far_def, c_low.gpu.triangles, far_low,
        c_mid.gpu.triangles, far_mid, c_ext.gpu.triangles, far_ext);
}

static void test_settings_persistence(void)
{
 memset(sram_bank, 0xff, sizeof(sram_bank));
 int st = tr64_save_game_start();
 assert(st == TR64_SAVE_OK || st == TR64_SAVE_EMPTY);

 /* 1. Legacy 32-byte (8-word) settings load cleanly with filter=0, auto_dist=0 */
 uint8_t legacy[32] __attribute__((aligned(16)));
 const uint32_t leg_words[8] = {12, 6, 0, 1, 1, 0, 1, 1};
 for (unsigned i = 0; i < 8; ++i) w32(legacy + 4 * i, leg_words[i]);
 assert(tr64_save_settings_write(legacy, 32) == TR64_SAVE_OK);
 tr64_save_game_stop();
 g_settings = (tr64_game_settings_t){.video_filter = 1, .video_auto_dist = 1};
 assert(tr64_save_game_start() == TR64_SAVE_OK);
 assert(g_settings.video_gamma == 12 && g_settings.video_fog == 6 &&
        g_settings.video_fps == 0 && g_settings.video_vsync == 1 &&
        g_settings.audio_sfx == 1 && g_settings.audio_music == 0 &&
        g_settings.controls_vibration == 1 && g_settings.controls_swap == 1 &&
        g_settings.video_filter == 0 && g_settings.video_auto_dist == 0);

 /* 2. 44-byte (11-word) round-trip via eng_game_save_settings() */
 g_settings.video_fog = 4;
 g_settings.video_filter = 1;
 g_settings.video_auto_dist = 1;
 g_settings.controls_abilities = 2;
 eng_game_save_settings();
 assert(tr64_save_game_status() == TR64_SAVE_OK);
 uint8_t raw[64] __attribute__((aligned(16))); uint32_t got = 0;
 assert(tr64_save_settings_read(raw, sizeof(raw), &got) == TR64_SAVE_OK && got == 44);
 tr64_save_game_stop();
 memset(&g_settings, 0, sizeof(g_settings));
 assert(tr64_save_game_start() == TR64_SAVE_OK);
 assert(g_settings.video_fog == 4 && g_settings.video_filter == 1 &&
        g_settings.video_auto_dist == 1 && g_settings.controls_abilities == 2);

 /* 3. Out-of-range video_filter=2 / video_auto_dist=2 / controls_abilities=3 / invalid length 36 rejected as CORRUPT */
 uint8_t bad[44] __attribute__((aligned(16)));
 memcpy(bad, raw, 44);
 w32(bad + 32, 2); /* video_filter = 2 */
 assert(tr64_save_settings_write(bad, 44) == TR64_SAVE_OK);
 tr64_save_game_stop();
 assert(tr64_save_game_start() == TR64_SAVE_CORRUPT);

 memcpy(bad, raw, 44);
 w32(bad + 36, 2); /* video_auto_dist = 2 */
 assert(tr64_save_settings_write(bad, 44) == TR64_SAVE_OK);
 tr64_save_game_stop();
 assert(tr64_save_game_start() == TR64_SAVE_CORRUPT);

 memcpy(bad, raw, 44);
 w32(bad + 40, 3); /* controls_abilities = 3 */
 assert(tr64_save_settings_write(bad, 44) == TR64_SAVE_OK);
 tr64_save_game_stop();
 assert(tr64_save_game_start() == TR64_SAVE_CORRUPT);

 assert(tr64_save_settings_write(bad, 36) == TR64_SAVE_OK);
 tr64_save_game_stop();
 assert(tr64_save_game_start() == TR64_SAVE_CORRUPT);
 tr64_save_game_stop();
 printf("PASS synth 5/6 settings persistence: legacy 32B->filter=0,auto=0; 44B round-trip; 4 corrupt cases rejected\n");
}

static void test_scene_options_apply(void)
{
 /* Verify tr64_render_apply_scene_options reflects live settings (filter + manual fog) */
 g_settings = (tr64_game_settings_t){
  .video_fog = 0, .video_filter = 1, .video_auto_dist = 1
 };
 tr64_gpu_projection_t p = TR64_GPU_WORLD_PROJECTION;
 tr64_render_apply_scene_options(&p);
 assert(tr64_gpu_host_filter() == 1);
 assert(p.far_z == 4096 && p.fog_far_z == 4096 && p.fog_near_z == 3276);

 g_settings.video_fog = 8;
 g_settings.video_filter = 0;
 tr64_render_apply_scene_options(&p);
 assert(tr64_gpu_host_filter() == 0);
 assert(p.far_z == 10240 && p.fog_far_z == 10240 && p.fog_near_z == 8192);
 printf("PASS synth 6/6 tr64_render_apply_scene_options: filter+fog wired cleanly\n");
}

int main(void)
{
 tr64_gpu_init();
 test_fog_range();
 test_auto_dist();
 test_projection_and_filter();
 test_synthetic_draw_distance();
 test_settings_persistence();
 test_scene_options_apply();
 return 0;
}
'''

PROBE_C = r'''/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/common/internal.h"
#include "game/common/inventory-private.h"
#include "tr64/game.h"
#include "tr64/code.h"
#include "tr64/level.h"
#include "tr64/render.h"
#include "tr64/mem.h"
#include "engine/render/internal.h"
#include "engine/core/sys.h"
#include "plat/host/gpu-platform.h"
#include "ui_string_ids.h"

extern int tr64_gpu_host_filter(void);
extern void tr64_render_scene_load(const tr64_level_t *level, const char *name, uint32_t pack_bytes);
extern void __real_game_tr2_tick(uint32_t mask);
extern void __real_tr64_level_load(tr64_level_t *out, uint32_t pack_bytes, const char *name);

static int g_bound;
static int g_tested_ring;
static int g_tested_render;
static tr64_game_drawable_t g_chunk[TR64_GAME_DRAW_CHUNK];

static void write_ppm(const char *path, const uint16_t *px)
{
 FILE *f = fopen(path, "wb");
 if (!f) sys_error("go probe: cannot open %s", path);
 fprintf(f, "P6\n320 240\n255\n");
 uint8_t row[320 * 3];
 for (int y = 0; y < 240; ++y) {
  for (int x = 0; x < 320; ++x) {
   uint16_t p = px[y * 320 + x];
   unsigned r5 = (p >> 11) & 31u, g5 = (p >> 6) & 31u, b5 = (p >> 1) & 31u;
   row[x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
   row[x * 3 + 1] = (uint8_t)((g5 << 3) | (g5 >> 2));
   row[x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
  }
  fwrite(row, 1, sizeof(row), f);
 }
 fclose(f);
}

void __wrap_tr64_level_load(tr64_level_t *out, uint32_t pack_bytes, const char *name)
{
 __real_tr64_level_load(out, pack_bytes, name);
 if (!out || out->game != 2 || g_bound) return;
 if (getenv("TR64_GO_TEST_RENDER")) {
  tr64_gpu_init();
  code_loading_begin();
  tr64_load_scope_begin();
  tr64_render_scene_set_live(1);
  tr64_render_scene_load(out, name, pack_bytes);
  tr64_load_scope_end();
  code_loading_end();
 }
 g_bound = 1;
}

static void step_ring_until(uint16_t mask, int target_state, int max_ticks)
{
 for (int i = 0; i < max_ticks; ++i) {
  tr64_game_menu_view_t v;
  if (game_menu_view(&v) && v.state == target_state && v.frameIndex == v.frameTarget)
   return;
  if (!game_tr2_ring_update(mask))
   sys_error("go probe: ring closed unexpectedly waiting for state %d", target_state);
  mask = 0;
 }
 sys_error("go probe: timed out waiting for ring state %d", target_state);
}

static void test_tr2_graphics_ring(void)
{
 const uint32_t carried_before = gs->tr2->carriedCount;
 TR2RingEntry carried_snap[TR2_CARRIED_CAPACITY];
 memcpy(carried_snap, gs->tr2->carried, sizeof(carried_snap));
 const tr64_game_settings_t orig = *game_settings_view();
 assert(orig.video_fog == 8 && orig.video_filter == 0 && orig.video_auto_dist == 0);

 assert(game_tr2_ring_open(false));
 step_ring_until(0, TR64_GAME_MENU_STATE_READY, 32);
 tr64_game_menu_view_t mv;
 assert(game_menu_view(&mv) && mv.page == TR64_GAME_MENU_PAGE_MAIN && mv.showDown == 1);

 /* Press Down -> Options page */
 assert(game_tr2_ring_update(TR64_GAME_BACK));
 step_ring_until(0, TR64_GAME_MENU_STATE_READY, 40);
 assert(game_menu_view(&mv) && mv.page == TR64_GAME_MENU_PAGE_OPTIONS && mv.showUp == 1);
 assert(mv.itemsCount >= 1 && mv.items[mv.itemIndex].type == TR2_RING_DETAIL);

 /* Press Action -> Graphics Options (6 options: Gamma, Fog, FPS, VSync, Filter, AutoDist) */
 assert(game_tr2_ring_update(TR64_GAME_ACTION));
 step_ring_until(0, TR64_GAME_MENU_STATE_SHOW, 40);
 assert(game_menu_view(&mv) && mv.optionsCount == 6);
 assert(mv.options[0].str == STR_OPT_DETAIL_GAMMA);
 assert(mv.options[1].str == STR_OPT_DETAIL_FOG);
 assert(mv.options[2].str == STR_OPT_DETAIL_FPS);
 assert(mv.options[3].str == STR_OPT_DETAIL_VSYNC);
 assert(mv.options[4].str == STR_OPT_DETAIL_FILTER);
 assert(mv.options[5].str == TR64_STR_OPT_DETAIL_AUTO_DIST);

 /* Move down to Draw Distance (idx 1) and press Left -> video_fog 8 -> 7 */
 assert(game_tr2_ring_update(TR64_GAME_BACK));
 assert(game_tr2_ring_update(TR64_GAME_LEFT));
 assert(game_settings_view()->video_fog == 7);

 /* Move down to Filtering (idx 4) and toggle -> video_filter 0 -> 1 */
 for (int i = 0; i < 3; ++i) {
  assert(game_tr2_ring_update(0));
  assert(game_tr2_ring_update(TR64_GAME_BACK));
 }
 assert(game_tr2_ring_update(TR64_GAME_RIGHT));
 assert(game_settings_view()->video_filter == 1);

 /* Move down to Auto Distance (idx 5) and toggle -> video_auto_dist 0 -> 1 */
 assert(game_tr2_ring_update(0));
 assert(game_tr2_ring_update(TR64_GAME_BACK));
 assert(game_tr2_ring_update(TR64_GAME_RIGHT));
 assert(game_settings_view()->video_auto_dist == 1);

 /* Exit Graphics Options -> Options ring -> Close ring */
 assert(game_tr2_ring_update(TR64_GAME_JUMP));
 step_ring_until(0, TR64_GAME_MENU_STATE_READY, 40);
 assert(game_tr2_ring_update(0));
 assert(game_tr2_ring_update(TR64_GAME_JUMP));
 int closed = 0;
 for (int i = 0; i < 40; ++i) {
  if (!game_tr2_ring_update(0)) { closed = 1; break; }
 }
 assert(closed && game_tr2_ring_choice() == 0);
 assert(gs->tr2->carriedCount == carried_before);
 assert(memcmp(gs->tr2->carried, carried_snap, sizeof(carried_snap)) == 0);

 /* Restore defaults before trace continues */
 assert(game_settings_apply(&orig));
 printf("TR2_RING_PASS fog=8->7 filter=0->1 auto_dist=0->1 options=6 choice=0\n");
 fflush(stdout);
}

void __wrap_game_tr2_tick(uint32_t mask)
{
 __real_game_tr2_tick(mask);
 if (!g_bound) return;

 const char *tick_s = getenv("TR64_GO_TICK");
 int want_tick = tick_s ? atoi(tick_s) : 2;
 if ((int)gs->tick != want_tick) return;

 const uint32_t logicBefore = gs->gRandSeedLogic;
 const uint32_t drawBefore = gs->gRandSeedDraw;

 if (!g_tested_ring && getenv("TR64_GO_TEST_RING")) {
  g_tested_ring = 1;
  test_tr2_graphics_ring();
 }

 if (!g_tested_render && getenv("TR64_GO_TEST_RENDER")) {
  g_tested_render = 1;
  tr64_game_view_t view;
  game_view_camera(&view);
  const tr64_game_settings_t orig = *game_settings_view();
  tr64_render_counts_t c8, c0;

  /* Render default video_fog=8, video_filter=0 through real scene pipeline */
  tr64_game_settings_t s = orig;
  s.video_fog = 8; s.video_filter = 0; s.video_auto_dist = 0;
  assert(game_settings_apply(&s));
  tr64_render_game_scene_frame(&view, g_chunk, TR64_GAME_DRAW_CHUNK);
  c8 = *tr64_render_scene_counts();
  int filt8 = tr64_gpu_host_filter();
  const char *ppm8 = getenv("TR64_GO_PPM8");
  if (ppm8) write_ppm(ppm8, tr64_gpu_host_pixels());

  /* Render video_fog=0 (far_z=4096), video_filter=1 (Bilinear) */
  s.video_fog = 0; s.video_filter = 1; s.video_auto_dist = 0;
  assert(game_settings_apply(&s));
  tr64_render_game_scene_frame(&view, g_chunk, TR64_GAME_DRAW_CHUNK);
  c0 = *tr64_render_scene_counts();
  int filt0 = tr64_gpu_host_filter();
  const char *ppm0 = getenv("TR64_GO_PPM0");
  if (ppm0) write_ppm(ppm0, tr64_gpu_host_pixels());

  /* Also verify skybox looking up in outdoor room 78 stays bright & unclipped at far_z=4096 */
  tr64_game_view_t sky_view = view;
  /* Pitch camera upward ~45 deg to face open skybox in room 78 */
  sky_view.camera_basis[0] = 16384; sky_view.camera_basis[1] = 0;      sky_view.camera_basis[2] = 0;
  sky_view.camera_basis[3] = 0;     sky_view.camera_basis[4] = 11585;  sky_view.camera_basis[5] = 11585;
  sky_view.camera_basis[6] = 0;     sky_view.camera_basis[7] = -11585; sky_view.camera_basis[8] = 11585;
  s.video_fog = 0; s.video_filter = 0; s.video_auto_dist = 0;
  assert(game_settings_apply(&s));
  tr64_render_game_scene_frame(&sky_view, g_chunk, TR64_GAME_DRAW_CHUNK);
  const char *ppm_sky = getenv("TR64_GO_PPM_SKY");
  if (ppm_sky) write_ppm(ppm_sky, tr64_gpu_host_pixels());

  assert(game_settings_apply(&orig));
  printf("RENDER_COMPARE tri8=%u tri0=%u rej8=%u rej0=%u filt8=%d filt0=%d\n",
         (unsigned)c8.gpu.triangles, (unsigned)c0.gpu.triangles,
         (unsigned)c8.batches_rejected, (unsigned)c0.batches_rejected,
         filt8, filt0);
  fflush(stdout);
 }

 if (gs->gRandSeedLogic != logicBefore || gs->gRandSeedDraw != drawBefore)
  sys_error("go probe: RNG mutated!");
}
'''

TR1_MENU_PROBE_C = r'''/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/common/internal.h"
#include "game/common/inventory-private.h"
#include "tr64/game.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "engine/core/sys.h"
#include "ui_string_ids.h"

extern void __real_game_tick_players(const uint16_t mask[TR64_GAME_PLAYERS]);
static tr64_level_t g_tr1_level;
void __wrap_tr64_scene_load(const char *name)
{
 const char *path = getenv("TR64_LEVEL_PACK");
 int n = sys_cart_open(path);
 if (n <= 0) sys_error("tr1 probe: open pack failed");
 tr64_io_init((uint32_t)n);
 tr64_io_frame();
 tr64_load_scope_begin();
 tr64_level_load(&g_tr1_level, (uint32_t)n, name);
 tr64_load_scope_end();
}
const tr64_level_t *__wrap_tr64_level_current(void) { return &g_tr1_level; }
static int g_done;
static int g_save_calls;
static tr64_game_settings_t g_saved;

void eng_game_save_settings(void)
{
 const tr64_game_settings_t *s = game_settings_view();
 assert(s);
 g_saved = *s;
 ++g_save_calls;
}
int eng_game_has_save(void) { return 0; }
int eng_game_save(void) { return 1; }
void eng_game_vibrate(void) { }

static void tick1(uint16_t mask)
{
 uint16_t m[TR64_GAME_PLAYERS] = {mask, 0};
 __real_game_tick_players(m);
}

static void step_until(uint16_t mask, int state, int max_ticks)
{
 for (int i = 0; i < max_ticks; ++i) {
  tr64_game_menu_view_t v;
  if (game_menu_view(&v) && v.state == state && v.frameIndex == v.frameTarget)
   return;
  tick1(mask);
  mask = 0;
 }
 sys_error("tr1 probe: timeout waiting for state %d", state);
}

void __wrap_game_tick_players(const uint16_t mask[TR64_GAME_PLAYERS])
{
 __real_game_tick_players(mask);
 if (g_done || gs->tick != 1) return;
 g_done = 1;
 /* Open TR1 inventory -> Down to Options -> spin to SLOT_DETAIL -> open */
 tick1(TR64_GAME_SELECT);
 step_until(0, TR64_GAME_MENU_STATE_READY, 40);
 tick1(TR64_GAME_BACK);
 step_until(0, TR64_GAME_MENU_STATE_READY, 40);
 tr64_game_menu_view_t mv;
 assert(game_menu_view(&mv) && mv.page == TR64_GAME_MENU_PAGE_OPTIONS);
 for (int tries = 0; tries < 8; ++tries) {
  assert(game_menu_view(&mv));
  if (mv.items[mv.itemIndex].slot == SLOT_DETAIL) break;
  tick1(TR64_GAME_LEFT);
  step_until(0, TR64_GAME_MENU_STATE_READY, 32);
 }
 assert(game_menu_view(&mv) && mv.items[mv.itemIndex].slot == SLOT_DETAIL);
 tick1(TR64_GAME_ACTION);
 step_until(0, TR64_GAME_MENU_STATE_SHOW, 40);
 assert(game_menu_view(&mv) && mv.optionsCount == 6);
 assert(mv.options[0].str == STR_OPT_DETAIL_GAMMA);
 assert(mv.options[1].str == STR_OPT_DETAIL_FOG);
 assert(mv.options[2].str == STR_OPT_DETAIL_FPS);
 assert(mv.options[3].str == STR_OPT_DETAIL_VSYNC);
 assert(mv.options[4].str == STR_OPT_DETAIL_FILTER);
 assert(mv.options[5].str == TR64_STR_OPT_DETAIL_AUTO_DIST);
 /* Navigate to Filtering (idx 4) and toggle */
 for (int i = 0; i < 4; ++i) {
  tick1(0);
  tick1(TR64_GAME_BACK);
 }
 tick1(TR64_GAME_RIGHT);
 assert(game_settings_view()->video_filter == 1 && g_saved.video_filter == 1);
 /* Navigate to Auto Distance (idx 5) and toggle */
 tick1(0);
 tick1(TR64_GAME_BACK);
 tick1(TR64_GAME_RIGHT);
 assert(game_settings_view()->video_auto_dist == 1 && g_saved.video_auto_dist == 1);
 assert(g_save_calls >= 2);
 printf("TR1_DETAIL_PASS options=6 filter=0->1 auto_dist=0->1 saves=%d\n", g_save_calls);
 fflush(stdout);
 exit(0);
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
    (gen / 'render_pose.h').write_text(
        '#define TR64_RENDER_LEVEL ""\n'
        '#define TR64_RENDER_POSE ""\n'
        '#define TR64_RENDER_POSE_SHA ""\n'
        '#define TR64_RENDER_EYE {0,0,0}\n'
        '#define TR64_RENDER_LOOK {0,0,65536}\n'
        '#define TR64_RENDER_ROOM 0u\n'
        '#define TR64_RENDER_FLIPPED 0u\n'
        '#define TR64_RENDER_SPLIT 0\n'
        '#define TR64_RENDER_HALF_VIEW 0\n'
        '#define TR64_RENDER_POSED_GAME_CAMERA 0\n'
        '#define TR64_RENDER_MEASURE 0\n'
        '#define TR64_RENDER_HUD 0\n'
        '#define TR64_RENDER_HUD_RECT_PATH 0\n'
        '#define TR64_RENDER_REST_MODEL_COUNT 0u\n'
        '#define TR64_RENDER_REST_MODEL_IDS {0u}\n'
        '#define TR64_RENDER_REST_ITEMS 0u\n'
        '#define TR64_RENDER_LIVE_ITEM UINT32_MAX\n'
        '#define TR64_RENDER_TEXTURE_STEP 0u\n',
        encoding='utf-8',
    )
    src = work / 'synth.c'
    exe = work / 'synth'
    src.write_text(SYNTHETIC_C, encoding='utf-8')
    cmd = [
        'rtk', os.environ.get('HOST_CC', 'cc'), '-m32', '-std=c99', '-O2', '-g',
        '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-DTR64_HOST=1', '-DTR64_GAME=2', '-DTR64_PLAY=0', '-DTR64_TEST=1',
        '-D_POSIX_C_SOURCE=200809L',
        f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}',
        str(src),
        str(ROOT / 'engine/render/bind_load.c'),
        str(ROOT / 'engine/render/geo_load.c'),
        str(ROOT / 'engine/render/texture_load.c'),
        str(ROOT / 'engine/render/texture.c'),
        str(ROOT / 'engine/render/render.c'),
        str(ROOT / 'engine/render/scene.c'),
        str(ROOT / 'engine/render/sprite.c'),
        str(ROOT / 'engine/render/items.c'),
        str(ROOT / 'engine/res/view.c'),
        str(ROOT / 'engine/res/filetable.c'),
        str(ROOT / 'engine/save/game.c'),
        str(ROOT / 'engine/save/dispatch.c'),
        str(ROOT / 'engine/save/medium.c'),
        str(ROOT / 'plat/host/gpu.c'),
        str(ROOT / 'plat/host/raster.c'),
        '-o', str(exe),
    ]
    c = subprocess.run(cmd, capture_output=True, text=True)
    expect(c.returncode == 0, f'synthetic compile failed:\n{c.stdout}{c.stderr}')
    r = subprocess.run(['rtk', str(exe)], capture_output=True, text=True, timeout=30)
    expect(r.returncode == 0, f'synthetic test failed:\n{r.stdout}{r.stderr}')
    print(r.stdout.strip())


def build_tr2_probe(build_dir: Path, work: Path) -> Path:
    src = work / 'probe.c'
    obj = work / 'probe.o'
    exe = work / 'go-host'
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
    expect(c.returncode == 0, f'TR2 probe compile failed:\n{c.stdout}{c.stderr}')
    l = subprocess.run([
        'rtk', 'cc', '-m32',
        '-Wl,--wrap=game_tr2_tick', '-Wl,--wrap=tr64_level_load',
        '-o', str(exe), str(obj), *(str(p) for p in objs),
    ], capture_output=True, text=True)
    expect(l.returncode == 0, f'TR2 probe link failed:\n{l.stdout}{l.stderr}')
    return exe


def run_tr2_real_packs(build_dir: Path, wall_pack: Path, boat_pack: Path, work: Path) -> None:
    exe = build_tr2_probe(build_dir, work)
    plain_host = build_dir / 'host/tr64-host'

    # 1. Great Wall outdoor vista (run forward to open canyon): fog=8 vs fog=0 culling +
    #    skybox visibility + filter toggle + trace parity
    inp = work / 'wall_start.txt'
    inp.write_text('\n'.join(['0', '0'] + ['8'] * 60) + '\n', encoding='ascii')
    ppm8, ppm0, ppm_sky = work / 'wall_fog8.ppm', work / 'wall_fog0.ppm', work / 'wall_sky0.ppm'
    tr_probe, tr_plain = work / 'wall_probe.jsonl', work / 'wall_plain.jsonl'
    env = dict(os.environ)
    env.update({
        'TR64_GO_TICK': '60',
        'TR64_GO_TEST_RING': '1',
        'TR64_GO_TEST_RENDER': '1',
        'TR64_GO_PPM8': str(ppm8),
        'TR64_GO_PPM0': str(ppm0),
        'TR64_GO_PPM_SKY': str(ppm_sky),
    })
    p = subprocess.run(['rtk', str(exe), '--game-tr2-schema21', str(wall_pack), str(inp), str(tr_probe)],
                       capture_output=True, text=True, env=env, timeout=120)
    expect(p.returncode == 0, f'wall probe failed:\n{p.stdout[-800:]}{p.stderr[-800:]}')
    expect('TR2_RING_PASS' in p.stdout, f'missing TR2_RING_PASS in wall output:\n{p.stdout}')
    cmp_line = [ln for ln in p.stdout.splitlines() if ln.startswith('RENDER_COMPARE ')]
    expect(len(cmp_line) == 1, f'missing RENDER_COMPARE in:\n{p.stdout}')
    kv = dict(tok.split('=') for tok in cmp_line[0].split()[1:])
    tri8, tri0 = int(kv['tri8']), int(kv['tri0'])
    filt8, filt0 = int(kv['filt8']), int(kv['filt0'])
    expect(tri0 < tri8, f'expected fog=0 to cull distant batches: tri8={tri8} tri0={tri0}')
    expect(filt8 == 0 and filt0 == 1, f'filter toggle failed: filt8={filt8} filt0={filt0}')

    # Verify distant canyon geometry fades into fog (lower luma at fog=0) AND skybox stays visible at far_z=4096
    px8, px0, px_sky = read_ppm(ppm8), read_ppm(ppm0), read_ppm(ppm_sky)
    luma8, luma0 = sum(px8), sum(px0)
    expect(luma0 < luma8, f'expected fog=0 frame to darken distant walls into fog: luma8={luma8} luma0={luma0}')
    sky_lit = sum(1 for y in range(0, 120) for x in range(320)
                  if px_sky[(y * 320 + x) * 3] + px_sky[(y * 320 + x) * 3 + 1] + px_sky[(y * 320 + x) * 3 + 2] > 60)
    expect(sky_lit >= 5000, f'skybox clipped at far_z=4096: sky_lit={sky_lit}')

    # Verify 100% bit-identical trace parity vs plain tr64-host
    p_plain = subprocess.run(['rtk', str(plain_host), '--game-tr2-schema21',
                              str(wall_pack), str(inp), str(tr_plain)],
                             capture_output=True, text=True, timeout=60)
    expect(p_plain.returncode == 0, f'plain wall failed: {p_plain.stderr}')
    expect(tr_probe.read_bytes() == tr_plain.read_bytes(), 'graphics options probe mutated wall trace!')
    print(f'PASS real 1/2 Great Wall fog=8 vs fog=0 + ring + trace: '
          f'tri={tri8}->{tri0} filt={filt8}->{filt0} sky_lit={sky_lit} trace=100%_identical')

    # 2. Venice (boat.pack): verify TR2 Options ring + Graphics Options work identically
    inp_b = work / 'boat_start.txt'
    inp_b.write_text('0\n0\n0\n', encoding='ascii')
    tr_b = work / 'boat_probe.jsonl'
    env_b = dict(os.environ)
    env_b.update({'TR64_GO_TICK': '2', 'TR64_GO_TEST_RING': '1'})
    pb = subprocess.run(['rtk', str(exe), '--game-tr2-schema22', str(boat_pack), str(inp_b), str(tr_b)],
                        capture_output=True, text=True, env=env_b, timeout=120)
    expect(pb.returncode == 0, f'boat probe failed:\n{pb.stdout[-800:]}{pb.stderr[-800:]}')
    expect('TR2_RING_PASS' in pb.stdout, f'missing TR2_RING_PASS in boat output:\n{pb.stdout}')
    print('PASS real 2/2 Venice TR2 Options/Graphics ring: 6 options, Filtering + Auto Distance toggled cleanly')


def run_tr1_menu_test(build_dir: Path, tr1_pack: Path, work: Path) -> None:
    src = work / 'tr1_probe.c'
    obj = work / 'tr1_probe.o'
    exe = work / 'tr1-menu-host'
    src.write_text(TR1_MENU_PROBE_C, encoding='utf-8')
    gen = build_dir / 'generated'
    objs = sorted((build_dir / 'host').rglob('*.o'))
    expect(bool(objs), f'no host objects under {build_dir / "host"}')
    c = subprocess.run([
        'rtk', 'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}', f'-I{gen / "host"}',
        '-DTR64_GAME=1', '-DTR64_HOST=1', '-D_POSIX_C_SOURCE=200809L',
        '-c', str(src), '-o', str(obj),
    ], capture_output=True, text=True)
    expect(c.returncode == 0, f'TR1 probe compile failed:\n{c.stdout}{c.stderr}')
    l = subprocess.run([
        'rtk', 'cc', '-m32',
        '-Wl,--wrap=game_tick_players', '-Wl,--wrap=tr64_scene_load', '-Wl,--wrap=tr64_level_current',
        '-o', str(exe), str(obj), *(str(p) for p in objs),
    ], capture_output=True, text=True)
    expect(l.returncode == 0, f'TR1 probe link failed:\n{l.stdout}{l.stderr}')
    inp = work / 'tr1_in.txt'
    inp.write_text('0\n0\n0\n', encoding='ascii')
    env = dict(os.environ)
    env['TR64_LEVEL_PACK'] = str(tr1_pack)
    p = subprocess.run([
        'rtk', str(exe), '--game-input', str(inp), '--game-level', 'level1',
        '--game-ticks', '3', '--game-trace-only',
    ], capture_output=True, text=True, env=env, timeout=60)
    expect(p.returncode == 0, f'TR1 menu probe failed:\n{p.stdout}{p.stderr}')
    pass_lines = [ln for ln in p.stdout.splitlines() if ln.startswith('TR1_DETAIL_PASS')]
    expect(len(pass_lines) == 1, f'missing TR1_DETAIL_PASS:\n{p.stdout}')
    print(f'PASS real TR1 SLOT_DETAIL menu: {pass_lines[0]}')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=None, help='Host build directory')
    ap.add_argument('--wall-pack', type=Path, default=None, help='Cooked Great Wall pack')
    ap.add_argument('--boat-pack', type=Path, default=None, help='Cooked Venice pack')
    ap.add_argument('--tr1-pack', type=Path, default=None, help='Optional cooked TR1 pack (level1)')
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')
    with tempfile.TemporaryDirectory(prefix='graphics-options-', dir=scratch) as td:
        work = Path(td)
        run_synthetic(work)
        if args.build and args.wall_pack and args.boat_pack:
            run_tr2_real_packs(args.build, args.wall_pack, args.boat_pack, work)
        if args.build and args.tr1_pack:
            run_tr1_menu_test(args.build, args.tr1_pack, work)
    print('ALL GRAPHICS OPTIONS TESTS PASSED')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
