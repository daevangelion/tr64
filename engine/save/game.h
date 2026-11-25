/* SPDX-License-Identifier: MIT */
#ifndef TR64_SAVE_GAME_H
#define TR64_SAVE_GAME_H
#include "engine/save/save.h"
/* After Game start, inside loading: one ordinary 6 KiB state allocation (a region); preload
 * medium, validate availability and apply valid persisted source settings. */
int tr64_save_game_start(void);
void tr64_save_game_stop(void);
/* The slot the player confirmed in the picker (the last one, at start). */
unsigned tr64_save_game_selected(void);
/* The slot picker (engine/ui/picker.c) is the only place a slot is chosen.
 * confirm(slot,load): the player accepted this slot for the save (load=0) or
 * load request that is about to reach the game as one ACTION. A save
 * confirmation lasts until the next picker filter call (one game tick); a load
 * confirmation until taken by the play loop at the game's LOAD transition or
 * until reset (level stop). Strict mode (the play loop turns it on) makes
 * eng_game_save refuse, with a failed save, any save without a confirmation. */
void tr64_save_game_confirm(unsigned slot,int load);
void tr64_save_game_confirm_tick(void);
void tr64_save_game_confirm_reset(void);
int tr64_save_game_take_load_confirmation(void);
void tr64_save_game_require_confirmation(void);
/* Current validated Core must be the selected slot's source level. */
int tr64_save_game_load(void);
int tr64_save_game_status(void);
/* Saves written to the medium since boot; the play loop logs Lara's state
 * after the tick in which it grew (the shipping gate compares it with the load). */
unsigned tr64_save_game_saves(void);
#endif
