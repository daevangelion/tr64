/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_TR2_ENEMY_URBAN_H
#define TR64_GAME_TR2_ENEMY_URBAN_H

#include "creature.h"

#if TR64_HAS_TR2
enum {
    TR2_URBAN_CONTROL = 0,
    TR2_URBAN_DEATH = 1,
    TR2_URBAN_WINDOW_UPDATE = 2,
    TR2_URBAN_WINDOW_INTERACT = 3,
    TR2_URBAN_WINDOW_RAY = 4
};
typedef struct {
    uint32 op;
    TR2Creature *ai;
    ItemObj *self;
    Lara *lara;
    const Interest *interest;
    const Location *from, *to;
    int16 turn;
} TR2UrbanCall;

void game_tr2_doberman_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_doberman_death(TR2Creature *);
void game_tr2_gunman_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_gunman_death(TR2Creature *);
void game_tr2_boss_control(TR2Creature *, Lara *, const Interest *, int16 turn);
void game_tr2_boss_death(TR2Creature *);
void game_tr2_rat_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_rat_death(TR2Creature *);
void game_tr2_thug_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_thug_death(TR2Creature *);

void game_tr2_window_update(ItemObj *, Lara *);
bool game_tr2_window_interact(ItemObj *, Lara *);
bool game_tr2_window_check_ray(const Location *from, const Location *to);
#endif

#endif
