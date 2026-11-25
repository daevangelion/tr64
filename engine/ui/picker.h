/* SPDX-License-Identifier: MIT */
#ifndef TR64_UI_PICKER_H
#define TR64_UI_PICKER_H
#include <stdint.h>
#include "tr64/game.h"
#include "engine/ui/front.h"
/* Called with the level's presentation start/stop (frontend.c). */
void tr64_ui_picker_start(void);
void tr64_ui_picker_stop(void);
/* Switching ring owners discards any pending choice from the old pad. */
void tr64_ui_picker_owner(unsigned slot);
/* The open picker for the frame to draw, or NULL. */
const tr64_ui_picker_t *tr64_ui_picker_view(void);
enum { TR64_UI_PICK_PASS,TR64_UI_PICK_WITHHOLD,TR64_UI_PICK_OPEN };
/* What a tick's ACTION edge may do, from the menu view of the tick before
 * (pure; `edge` is the game input bits newly pressed this tick). */
int tr64_ui_picker_classify(const tr64_game_menu_view_t *menu,int active,uint16_t edge);
/* Per game tick, before game_tick, with the tick's game actions; returns the
 * actions the game receives. See picker.c for the rule. */
uint16_t tr64_ui_picker_filter(uint16_t actions);
#endif
