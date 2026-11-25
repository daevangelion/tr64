/* SPDX-License-Identifier: MIT */
#ifndef TR64_UI_FORMAT_H
#define TR64_UI_FORMAT_H
#include <stdint.h>
#include "tr64/game.h"
/* Cold presentation-unit helpers. Caller-owned bounded buffers; no printf,
 * locale, heap, game pointers, unit dispatch or writable static state. */
/* suffix is a raw source glyph ID, or zero; digits use source control bytes. */
void tr64_ui_count_text(char out[16],uint32_t value,uint8_t suffix);
void tr64_ui_time_text(char out[16],uint32_t ticks);
/* Original fixed-game level_stats.h format contract: title, four unsigned
 * counts, time; '@' retains source line separation. Zero on invalid language
 * placeholders, invalid title ID or 192-byte capacity exhaustion. */
int tr64_ui_stats_text(char out[192],const tr64_game_level_stats_t *stats);
#endif
