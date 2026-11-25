/* SPDX-License-Identifier: MIT */
#ifndef TR64_ENGINE_WALL_H
#define TR64_ENGINE_WALL_H
#include "tr64/game.h"
#if TR64_HAS_TR2
/* Fresh level with rendered world and optional fixture-only Lara placement.
 * Caller starts with empty level/renderer residency. */
tr64_game_drawable_t *tr64_wall_load(const char *name,const int32_t *placement,unsigned schema);
#endif
#endif
