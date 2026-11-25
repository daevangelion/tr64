/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h and item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_OBJECT_H
#define TR64_GAME_OBJECT_H
#include "internal.h"
void ItemObj_animSkip(ItemObj *, int32, int32, bool);
bool ItemObj_moveTo(ItemObj *, const vec3i *, ItemObj *, bool);
bool Object_isActive(ItemObj *);
void Block_collide(ItemObj *,Lara *,CollisionInfo *);
void Block_update(ItemObj *);
void TrapFloor_update(ItemObj *);

/* The hinge animation is independent of ordinary door sector writes. */
static inline bool Door_animate(ItemObj *self)
{
    bool active=Object_isActive(self);
    if (active) { if (self->state == 0) self->goalState=1; }
    else if (self->state == 1) self->goalState=0;
    ItemObj_animProcess(self,true);
    return active;
}

bool Object_checkLimit(const ItemObj *, const Lara *, const int16 *);
void Object_collideDefault(ItemObj *, Lara *, CollisionInfo *);
vec3i getBlockOffset(int16, int32);
bool Object_isPickup(int32);
bool Object_construct(ItemObj *);
void Object_update(ItemObj *);
void Object_collide(ItemObj *, Lara *, CollisionInfo *);
bool Object_activate(ItemObj *);
void Object_deactivate(ItemObj *);
void ScionTarget_hit(ItemObj *,int32);
bool useSwitch(ItemObj *, int32);
bool useKey(ItemObj *, ItemObj *);
bool usePickup(ItemObj *);
void game_door_action(ItemObj *, bool);
bool inventory_setCount(int32, int32);
bool inventory_addCount(int32, int32);
bool inventory_consumeCount(int32, int32);
bool inventory_hasAmmo(Weapon);
bool inventory_canSelectWeapon(Weapon);
bool inventory_consumeAmmo(Weapon, int32 *);
bool inventory_addPickup(ItemType);
bool inventory_applyItem(ItemObj *);
bool inventory_export_counts(int32 *);
bool inventory_import_counts(const int32 *);
bool inventory_apply_level_transition(int32 from,int32 to,const int32 *counts);
void inventory_openUse(Lara *, int32);
void ItemObj_collidePush(ItemObj *, Lara *, CollisionInfo *, bool);
bool ItemObj_updateHitMask(ItemObj *, Lara *, CollisionInfo *);
bool ItemObj_collideBounds(const ItemObj *, Lara *, CollisionInfo *);
void ItemObj_fxBlood(ItemObj *, const vec3i *, int16, int16);
void ItemObj_fxSmoke(ItemObj *, const vec3i *);
void ItemObj_fxRicochet(Room *, const vec3i *, bool);
uint32 ItemObj_collideSpheres(const ItemObj *, Lara *);
#endif
