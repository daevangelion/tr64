/* SPDX-License-Identifier: MIT */
#ifndef TR64_PACE_H
#define TR64_PACE_H
#include <stdint.h>
#include "tr64/limits.h"
/* The play loop's clock policy: game time follows the hardware clock, never the frame rate.
 *
 * An accumulator counts owed ticks. One tick is 1,000,000 units and every elapsed microsecond adds
 * TR64_TICK_HZ units, so 30 ticks are due in exactly one second (no rounding of a 33.333 us period).
 * tr64_pace_due says how many ticks a frame must run before it draws: none (wait, pumping audio and
 * I/O), one, or several after a slow frame. At most TR64_PACE_MAX_TICKS run in one frame; what is owed
 * beyond that is dropped and counted, so an unforeseen stall costs time, never a burst of ticks.
 * Stalls the loop knows of (a level load, a video, a save) call tr64_pace_start afterwards.
 *
 * Only the interactive loop (engine/in/play.c) uses this. Routes, hash scenes and oracle lanes run one
 * tick per frame by construction and never see it. Only unsigned elapsed intervals are compared. */
enum { TR64_PACE_MAX_TICKS=4 };
#define TR64_PACE_TICK_UNITS 1000000u
/* Longest interval charged at once: keeps credit+elapsed*TICK_HZ inside 32 bits. */
#define TR64_PACE_MAX_ELAPSED_US ((0xffffffffu-TR64_PACE_TICK_UNITS)/TR64_TICK_HZ)
typedef struct {
 uint32_t last,credit;                      /* clock at the last call; owed time in microseconds*TR64_TICK_HZ */
 uint32_t frames,ticks,dropped,restarts;    /* frames that ran >=1 tick, ticks run, ticks dropped, accumulator restarts */
 uint32_t histogram[TR64_PACE_MAX_TICKS+1]; /* frames by ticks run (index 0 stays zero: no tick, no frame) */
} tr64_pace_t;
/* Zero the statistics and restart at `now`, with `first` ticks already owed (1: tick at once). */
static inline void tr64_pace_reset(tr64_pace_t *p,uint32_t now,uint32_t first)
{
 uint32_t i;
 p->last=now;p->credit=first*TR64_PACE_TICK_UNITS;p->frames=p->ticks=p->dropped=p->restarts=0;
 for(i=0;i<=TR64_PACE_MAX_TICKS;++i)p->histogram[i]=0;
}
/* After a stall the loop knows of: nothing owed, statistics kept. */
static inline void tr64_pace_restart(tr64_pace_t *p,uint32_t now)
{
 p->last=now;p->credit=0;++p->restarts;
}
/* Ticks to run before drawing a frame now (0: wait). Call as often as the loop spins. */
static inline uint32_t tr64_pace_due(tr64_pace_t *p,uint32_t now)
{
 uint32_t elapsed=now-p->last,n;
 if(elapsed>TR64_PACE_MAX_ELAPSED_US)elapsed=TR64_PACE_MAX_ELAPSED_US;
 p->last=now;p->credit+=elapsed*TR64_TICK_HZ;
 if(p->credit<TR64_PACE_TICK_UNITS)return 0;
 n=p->credit/TR64_PACE_TICK_UNITS;p->credit-=n*TR64_PACE_TICK_UNITS;
 if(n>TR64_PACE_MAX_TICKS) { p->dropped+=n-TR64_PACE_MAX_TICKS;n=TR64_PACE_MAX_TICKS; }
 ++p->frames;p->ticks+=n;++p->histogram[n];
 return n;
}
#endif
