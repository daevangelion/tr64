/* SPDX-License-Identifier: MIT */
#ifndef TR64_UI_FRONT_H
#define TR64_UI_FRONT_H
#include "tr64/game.h"
#include "tr64/gpu.h"
#include "tr64/level.h"
enum { TR64_UI_PICKUPS=12 };
/* The notifications advance one 30 Hz tick per game tick. The play loop does it in tr64_ui_game_tick
 * (UPDATE_TICK, no drawing); a caller that draws once per tick without that hook (the hash scenes) gets
 * the same single step ahead of the picture (UPDATE_FRAME). */
enum { TR64_UI_UPDATE_NONE,TR64_UI_UPDATE_TICK,TR64_UI_UPDATE_FRAME };
/* Source UI::PickupItem. Position is Q8 screen pixels, ticks counts the
 * remaining 30 Hz game ticks of PICKUP_SHOW_TIME (5 s); they run once per tick, not per drawn frame. */
typedef struct { int32_t type,x,y;uint32_t ticks; } tr64_ui_pickup_t;
/* Writable presentation state lives in the resident image (units hold none). */
typedef struct { uint32_t count,last_tick,seen;tr64_ui_pickup_t item[TR64_UI_PICKUPS]; } tr64_ui_pickups_t;
/* Passport slot picker (three SRAM slots). Presentation only: the game still
 * sees one ordinary ACTION edge after the slot is selected. A slot is `valid`
 * (shows its level title, can be loaded), empty, or `unusable` (intact or not,
 * but this pack cannot load it). */
typedef struct { uint32_t open,load,index,valid[3],unusable[3],title[3]; } tr64_ui_picker_t;
typedef struct {
 const tr64_level_t *level;
 const tr64_ui_picker_t *picker;
 const tr64_game_view_t *view;
 tr64_ui_pickups_t *pickups;
 const tr64_game_menu_view_t *menu;
 const tr64_gpu_view_t *camera;
 const tr64_game_menu_text_t *hud;
 const tr64_game_menu_text_t *hud_second;
 const uint8_t *warp_notice; /* Non-NULL only for a two-player gameplay picture. */
 tr64_game_drawable_t *chunk;
 uint32_t capacity,title; /* Explicit loaded title context, never room count. */
 uint32_t update;         /* TR64_UI_UPDATE_*: who runs the pickup notifications' tick. */
 uint8_t split;
} tr64_ui_front_call_t;
/* Resident pointer to caller stack, valid only during a synchronous frame. */
extern const tr64_ui_front_call_t *tr64_ui_front_frame;
#endif
