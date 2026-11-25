/* SPDX-License-Identifier: MIT */
#ifndef TR64_UI_FRONTEND_H
#define TR64_UI_FRONTEND_H
#include "tr64/game.h"
#include "tr64/level.h"
/* Explicit full presentation opt-in, after Game start and while loading. */
void tr64_ui_game_start(const tr64_level_t *level,const char *name);
void tr64_ui_game_stop(void);
/* Zero without queries/drawing when unbound. Otherwise owns exactly one whole
 * frame; caller chunk and snapshots remain borrowed until GPU completion. */
int tr64_ui_game_frame(const tr64_game_view_t *view,tr64_game_drawable_t *chunk,uint32_t capacity);
/* After every game tick that leaves the game running (the play loop; a one-tick-per-frame lane calls it
 * once before each frame): the presentation's per-tick work, with the borrowed draw chunk as scratch. The
 * game replaces its pickup events at each tick, so the notifications are fed, and their 30 Hz timers run,
 * here and never in the frame. Zero cost when unbound or nothing is showing. */
/* The TR2 play adapter may pass a null view; obtain a camera only when a
 * pickup requests presentation work. */
void tr64_ui_game_tick(const tr64_game_view_t *view,tr64_game_drawable_t *chunk,uint32_t capacity);
/* Clock-driven presentation (the rumble pulse's end) while the loop waits for the next tick. */
void tr64_ui_game_poll(void);
void tr64_ui_coop_no_medipack(unsigned slot);
void tr64_ui_coop_warped(unsigned slot);
void tr64_ui_coop_leave(void);
/* Whether the last presented frame had the ring/passport up. The game's LEVEL_END
 * to the title carries no cause: after a menu exit the closing menu was the last
 * picture, after a level completion it was not. */
int tr64_ui_menu_was_shown(void);
/* Per game tick, before game_tick: the slot picker and the rule that no save or
 * load reaches the game unconfirmed (engine/ui/picker.h, picker.c). */
#include "engine/ui/picker.h"
int tr64_ui_backend_rumble(unsigned slot,int active); /* zero when optional hardware absent */
#endif
