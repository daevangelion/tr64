/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_ROUTE_H
#define TR64_ROUTE_H
#include <stdint.h>
#include "tr64/in.h"
#define TR64_ROUTE_OPS 64u
#define TR64_ROUTE_VERSION 3u
#define TR64_ROUTE_VERSION_TR2 4u
#define TR64_ROUTE_HEADER 96u
/* Wire v3: 32 control bytes, 32 NUL-padded level bytes, then BE32 words
 * game/place/room/x/y/z/angle/hash-schema. Signed fields use two's complement;
 * disabled placement words are zero. CRC covers bytes 32..end.
 * A game route forwards buttons as raw action bits and requires zero axes.
 * Each event: BE32 tick, BE16 buttons, s8 x/y, BE32 zero.
 * Wire v4 is for TR2 and uses BE32 buttons, BE32 ring event (zero if absent):
 * bit 9 marks a choice; bit 10 marks an in-update key confirmation; bits
 * 0..8 are the public type (0 cancels), bits 11..31 are paused clock ticks. */
/* Hash-schema wire word: 0 for legacy schema2, 3 for source-global
 * schema3, or 20/21 for the Great Wall (21 includes creature brains). */
/* VIDEO: `load level <name>` names a video (lower case); seed is the tick limit (0 = to the end). */
enum { TR64_PROBE_NONE, TR64_PROBE_UNLOADED, TR64_PROBE_POOL, TR64_PROBE_HIGHMEM, TR64_PROBE_HEAP, TR64_PROBE_CLOCK_WRAP, TR64_PROBE_RELOAD, TR64_PROBE_VIDEO };
typedef struct {
 const uint8_t *data;
 const char *level; /* NULL is the explicit no-level/card state. */
 uint32_t ticks, seed, count, next, probe;
 uint32_t game_hash_schema;
 uint32_t place_room;
 int32_t place_x,place_y,place_z;
 int16_t place_angle;
 uint8_t game,place;
 tr64_input_t input;
#if defined(TR64_HOST) || TR64_GAME == 2
 uint32_t ring_event;
#endif
} tr64_route_t;
int tr64_route_open(tr64_route_t *route, const uint8_t *data, uint32_t size);
tr64_input_t tr64_route_tick(tr64_route_t *route, uint32_t tick);
/* A requested level requires this module-provided strong hook. The common
 * dispatcher checks for a linked loader before entering level residency. */
void tr64_scene_load(const char *level);
void tr64_scene_begin(const tr64_route_t *route);
/* Called by the common 30 Hz engine tick; inactive for ordinary scenes. */
void tr64_scene_tick(tr64_input_t input);
/* Optional explicit scene fixture boundary, never a simulation substitute.
 * Tick0 follows placement/sound start outside the loading scope; later calls
 * follow game_tick, before sound/stop/hash/render. Return0 when complete or1
 * after actual Core release/load/start. Engine rebinds its drawable chunk and
 * retries the same input once at later ticks; tick0 never advances simulation.
 * The fixture may save/load RAM or call normal lifecycle APIs, not tick,
 * render or directly pump sound. Other return values fail closed. */
int tr64_scene_game_boundary(const tr64_route_t *,uint32_t tick,uint16_t input);
/* Finish the explicit clock-wrap test; ordinary scenes have no clock rebase. */
void tr64_scene_clock_report(void);
#endif
