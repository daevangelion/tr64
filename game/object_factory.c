/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h, object.h, hazard_registry.h and hazard_boulder.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "hazard.h"
bool Hazard_construct(ItemObj *self)
{
    switch(self->type) {
    case ITEM_TRAP_SWORD:
        self->state=0;
        self->nextState=self->goalState=0;
        self->timer=0;
        self->hSpeed=(rand_logic()-0x4000)*ANGLE_180/(0x4000*30);
        self->flags=(self->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
        if (!(self->flags & ITEM_FLAG_ACTIVE)) ItemObj_activate(self);
        return true;
    case ITEM_TRAP_CEILING: case ITEM_TRAP_CEILING_2:
        if (self->flags & ITEM_FLAG_ACTIVE) ItemObj_deactivate(self);
        self->state=self->goalState=0;
        self->timer=self->vSpeed=0;
        return true;
    case ITEM_HAMMER_HANDLE: self->headOffset=65535; return true;
    case ITEM_FLAME_EMITTER:
        if (self->headOffset >= MAX_ITEMS || gs->items[self->headOffset].type != ITEM_FLAME)
            self->headOffset=65535;
        return true;
    case ITEM_LIGHTNING:
        self->timer=0;
        self->state=self->goalState=self->nextState=0;
        return true;
    case ITEM_TRAP_SPIKES: case ITEM_TRAP_BOULDER: case ITEM_TRAP_SLAM:
    case ITEM_HAMMER_BLOCK: case ITEM_FLAME: case ITEM_TRAP_LAVA:
        return true;
    default: return false;
    }
}
bool Object_construct(ItemObj *self)
{
    if (Hazard_construct(self)) return true;
    if (Object_isPickup(self->type)) { self->frameIndex=0; return true; }
    switch(self->type) {
    case ITEM_SCION_PICKUP_QUALOPEC: self->frameIndex=0; return true;
    case ITEM_SCION_TARGET: self->health=5; self->timer=0; return true;
    case ITEM_LAVA_PARTICLE: /* oracle hunk 22: LavaParticle */
        self->animIndex=0; self->timer=0; self->hSpeed=0; self->vSpeed=0; self->frameIndex=0;
        ItemObj_activate(self);
        return true;
    case ITEM_MIDAS_HAND: case ITEM_SCION_PICKUP_HOLDER: case ITEM_SCION_HOLDER: return true;
    case ITEM_CUT_1: case ITEM_CUT_2: case ITEM_CUT_3: case ITEM_CUT_4: Cut_construct(self); return true;
    case ITEM_CRYSTAL: self->flags=(self->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INVISIBLE; return true;
    case ITEM_TRAP_SWING_BLADE: case ITEM_DART: self->flags |= ITEM_FLAG_SHADOW; return true;
    case ITEM_TRAP_FLOOR: case ITEM_TRAP_DART_EMITTER:
    case ITEM_SWITCH: case ITEM_SWITCH_WATER:
    case ITEM_DOOR_1: case ITEM_DOOR_2: case ITEM_DOOR_3: case ITEM_DOOR_4:
    case ITEM_DOOR_5: case ITEM_DOOR_6: case ITEM_DOOR_7: case ITEM_DOOR_8:
    case ITEM_TRAP_DOOR_1: case ITEM_TRAP_DOOR_2:
    case ITEM_GEARS_1: case ITEM_GEARS_2: case ITEM_GEARS_3:
    case ITEM_BLOCK_1: case ITEM_BLOCK_2: case ITEM_BLOCK_3: case ITEM_BLOCK_4:
    case ITEM_PUZZLEHOLE_1: case ITEM_PUZZLEHOLE_2: case ITEM_PUZZLEHOLE_3: case ITEM_PUZZLEHOLE_4:
    case ITEM_KEYHOLE_1: case ITEM_KEYHOLE_2: case ITEM_KEYHOLE_3: case ITEM_KEYHOLE_4:
    case ITEM_VIEW_TARGET: case ITEM_WATERFALL: case ITEM_LAVA_EMITTER:
    /* These factory entries deliberately use plain ItemObj, including update/collide no-ops. */
    case ITEM_SKATEBOARD:
    case ITEM_DRAWBRIDGE: case ITEM_MOVING_OBJECT: case ITEM_MOVING_BLOCK:
    case ITEM_BRIDGE_FLAT: case ITEM_BRIDGE_TILT_1: case ITEM_BRIDGE_TILT_2:
    case ITEM_CABIN: case ITEM_BOAT: case ITEM_EARTHQUAKE:
        return true;
    default: return false;
    }
}
bool Object_activate(ItemObj *self)
{
    if (self->type != ITEM_TRAP_BOULDER) return false;
    if ((self->flags & ITEM_FLAG_INJURED) || self->state != 0) TrapBoulder_resetOrigin(self);
    return true; /* Caller performs the source base activation after this override. */
}
void Object_deactivate(ItemObj *self)
{
    /* Called after source base unlink. */
    if (self->type == ITEM_TRAP_BOULDER && ((self->flags & ITEM_FLAG_INJURED) || self->state != 0))
        TrapBoulder_resetOrigin(self);
}
void TrapBoulder_resetOrigin(ItemObj *self)
{
    uint32 index=(uint32)(self-gs->items);
    ASSERT(index < gs->level.itemsCount);
    const tr64_core_items_t *info=tr64_level_items(gs->core,index);
    Room *origin=gs->rooms+tr64_u16(info->room);
    if (origin != self->room) {
        Room_remove(self->room,self);
        Room_add(origin,self);
    }
    self->pos=vec3i_create(wrap_i16(i32_sub(tr64_i32(info->x),tr64_i32(origin->info->x)))+tr64_i32(origin->info->x),
        wrap_i16(tr64_i32(info->y)),
        wrap_i16(i32_sub(tr64_i32(info->z),tr64_i32(origin->info->z)))+tr64_i32(origin->info->z));
    self->angle.y=((int32)tr64_level_quadrant(info->angle)-2)*ANGLE_90;
    self->vSpeed=self->hSpeed=0;
    self->flags &= ~ITEM_FLAG_INJURED;
    ItemObj_animSet(self,gs->level.models[self->type].animIndex,true,0);
    ItemObj_updateRoom(self,0);
}
