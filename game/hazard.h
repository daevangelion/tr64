/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_registry.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_HAZARD_H
#define TR64_GAME_HAZARD_H
#include "object.h"
#include "weapon.h"
bool ItemObj_collideBounds(const ItemObj *, Lara *, CollisionInfo *);
bool Hazard_construct(ItemObj *);
bool Hazard_update(ItemObj *);
bool Hazard_collide(ItemObj *, Lara *, CollisionInfo *);
void TrapBoulder_resetOrigin(ItemObj *);
void TrapBoulder_update(ItemObj *);
void TrapBoulder_collide(ItemObj *, Lara *, CollisionInfo *);
void TrapSlam_update(ItemObj *);
void TrapDamocles_update(ItemObj *);
void TrapCeiling_update(ItemObj *);
void ThorHandle_update(ItemObj *);
void LightningEmitter_update(ItemObj *);
void Flame_update(ItemObj *);
void TrapFlameEmitter_update(ItemObj *);
void TrapLavaWedge_update(ItemObj *);
void worldRequestFlipToggle(void);
#endif
