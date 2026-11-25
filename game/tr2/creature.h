/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_TR2_CREATURE_H
#define TR64_GAME_TR2_CREATURE_H

#include "../internal.h"

enum { BORED, ATTACK, ESCAPE, STALK };
typedef struct { uint64_t distance; int16 yaw; int16 box; bool sameZone, ahead, facing; } Interest;

enum {
    TR2_AI_ACTIVATE = 0,
    TR2_AI_PREPARE = 1,
    TR2_AI_STRIDE = 2,
    TR2_AI_DEATH_STRIDE = 3,
    TR2_AI_COLLIDE = 4
};
enum { TR2_AI_RES_SKIP = 0, TR2_AI_RES_ALIVE = 1, TR2_AI_RES_DEAD = 2 };
typedef struct {
    uint32 op;
    ItemObj *self;
    Lara *lara;
    TR2Creature *ai;
    Interest interest;
    int16 turn;
} TR2AiCall;

enum { TR2_SPECIES_CONTROL = 0, TR2_SPECIES_DEATH = 1 };
enum { TR2_DEATH_DONE = 0, TR2_DEATH_STRIDE = 1, TR2_DEATH_STRIDE_ORDINAL = 2 };
typedef struct {
    uint32 op;
    TR2Creature *ai;
    Lara *lara;
    const Interest *interest;
} TR2SpeciesCall;

uint32 game_tr2_ai_prepare(TR2AiCall *);
void game_tr2_ai_stride(TR2Creature *, int16 turn);
void game_tr2_ai_death_stride(TR2Creature *, int16 turn);

void game_tr2_creature_strike(TR2Creature *, Lara *, int damage, uint32 mask);
void game_tr2_tiger_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_tiger_death(TR2Creature *);
void game_tr2_spider_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_spider_death(TR2Creature *);
void game_tr2_crow_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_crow_death(TR2Creature *);
void game_tr2_rex_control(TR2Creature *, Lara *, const Interest *);
void game_tr2_rex_death(TR2Creature *);

#endif
