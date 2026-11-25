/* SPDX-License-Identifier: MIT */
#ifndef TR64_PERF_H
#define TR64_PERF_H
#include <stdint.h>
#include "engine/core/sys.h"
#include "tr64/render.h"
/* On-cart profiler overlay (docs/perf-overlay.md). Everything here costs one
 * load and a branch of tr64_perf_state per instrumented site while it is off (a frame reads it a handful
 * of times); nothing is allocated, ever. Time comes from the CPU's Count register (sys_profile_ticks).
 *
 * tr64_perf_state: bit 0 the overlay is on (what every instrumented site tests), bit 1 the player's
 * request. A toggle only takes effect at the start of the next drawn frame, so no frame is measured
 * half with the overlay and half without. */
extern uint8_t tr64_perf_state;
#define TR64_PERF_ON() (tr64_perf_state&1u)
/* The pad chord's effect. */
static inline void tr64_perf_toggle(void) { tr64_perf_state^=2u; }
/* A pause is not part of a running sample; retain the user's request. */
static inline void tr64_perf_restart_window(void) { tr64_perf_state&=2u; }
/* play.c: a frame has ticks due (call as soon as the loop commits to the frame). Cheap when off. */
void tr64_perf_begin(uint32_t due);
static inline void tr64_perf_frame_begin(uint32_t due) { if(tr64_perf_state)tr64_perf_begin(due); }
/* Time spent in an instrumented stretch: stamp before (0 when off), add after. */
static inline uint32_t tr64_perf_stamp(void) { return TR64_PERF_ON() ? sys_profile_ticks() : 0u; }
extern uint32_t tr64_perf_sim_ticks,tr64_perf_io_ticks,tr64_perf_snd_ticks; /* game ticks; I/O pumps; the sound tick */
#define TR64_PERF_ADD(counter,stamp) do { if(TR64_PERF_ON())(counter)+=sys_profile_ticks()-(stamp); } while(0)
typedef struct {
 uint32_t ticks;        /* ticks the frame ran */
 uint32_t dropped;      /* the pacer's dropped-tick total for this level */
 uint32_t room;         /* camera room */
 const char *level;
} tr64_perf_frame_t;
/* After the frame has been drawn and presented (the GPU has completed it); only while on. */
void tr64_perf_end(const tr64_perf_frame_t *frame);
/* The overlay, drawn by engine/ui/draw.c inside the frame's 2D pass: its own cost is taken out of the
 * render figure and shown as OVL. */
void tr64_perf_draw(void);
#endif
