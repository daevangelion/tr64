/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h and object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_SAVE_WORLD_H
#define TR64_GAME_SAVE_WORLD_H
#include "save.h"
bool game_save_validate_world(const void *data,uint32 bytes,const GameSavePlan *plan);
bool game_save_rebuild_world(void);
#endif
