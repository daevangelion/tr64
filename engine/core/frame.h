/* SPDX-License-Identifier: MIT */
#ifndef TR64_FRAME_H
#define TR64_FRAME_H
#include <stdint.h>
#include "tr64/limits.h"
#define TR64_FRAME_US (1000000u/TR64_TICK_HZ)
/* Initialise phase to now-TR64_FRAME_US for an immediate first frame.
 * Only unsigned elapsed intervals are compared, never absolute timestamps.
 * A delayed caller catches up one bounded frame per call, preserving phase. */
static inline int tr64_frame_due(uint32_t *phase,uint32_t now)
{
 if((uint32_t)(now-*phase)<TR64_FRAME_US)return 0;
 *phase+=TR64_FRAME_US;return 1;
}
#endif
