/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_TR2_BOAT_H
#define TR64_GAME_TR2_BOAT_H

#include "objects.h"

#if TR64_HAS_TR2
enum {
    TR2_BOAT_INIT = 0,
    TR2_BOAT_CAMERA = 1,
    TR2_BOAT_INTERACT = 2,
    TR2_BOAT_UPDATE = 3,
    TR2_BOAT_UPDATE_POST = 4,
    TR2_BOAT_MINE_POST = 5
};
enum {
    TR2_BOAT_RES_DONE = 0,
    TR2_BOAT_RES_BUMP = 1,
    TR2_BOAT_RES_TRIGGER = 2,
    TR2_BOAT_RES_MINE_TRIGGER = 3
};
typedef struct {
    uint32 op;
    ItemObj *self;
    Lara *lara;
    const FloorData *trigger;
    int32 waterH;
    bool killedRider;
} TR2BoatCall;

void game_tr2_wake_effect(struct Room *, vec3i pos, int16 yaw, int16 hullSpeed);
void game_tr2_splash_effect(struct Room *, vec3i pos);
void game_tr2_explosion_effect(struct Room *, vec3i pos);
void game_tr2_bubbles(const Lara *);
void game_tr2_shatter(ItemObj *, uint32 mask);

void game_tr2_boat_init(void);
void game_tr2_race_clock_update(ItemObj *, const Lara *);
uint32 game_tr2_mine_update_phase1(ItemObj *, Lara *, TR2BoatCall *);
void game_tr2_mine_update_post(ItemObj *, Lara *, bool killedRider);
void game_tr2_gondola_update(ItemObj *);
bool game_tr2_boat_interact(ItemObj *, Lara *);
uint32 game_tr2_boat_update_phase1(ItemObj *, Lara *, TR2BoatCall *);
void game_tr2_boat_update_post(ItemObj *, Lara *, int32 waterH);
void game_tr2_boat_camera(Lara *);
#endif

#endif
