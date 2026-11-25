/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_flame.h, hazard_lava_wedge.h and hazard_oracles/flame.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/hazard.h"
#include "game/common/enemy.h"
static bool flameWithinRadius(const vec3i *from,const vec3i *to,int32 radius)
{
    int64_t dx=(int64_t)from->x-to->x, dy=(int64_t)from->y-to->y, dz=(int64_t)from->z-to->z;
    if (dx <= -radius || dx >= radius || dy <= -radius || dz <= -radius || dz >= radius) return false;
    return dx*dx+dy*dy+dz*dz < (int64_t)radius*radius;
}
static ItemObj *flameOwner(const ItemObj *flame)
{
    if (!flame || flame < gs->items || flame >= gs->items+MAX_ITEMS || flame->headOffset >= gs->level.itemsCount) return NULL;
    ItemObj *owner=gs->items+flame->headOffset;
    if (owner->type != ITEM_FLAME_EMITTER || owner->headOffset != (uint16)(flame-gs->items)) return NULL;
    return owner;
}
static bool flameEmitterWillBeActive(const ItemObj *emitter)
{
    if ((emitter->flags & ITEM_FLAG_MASK) != ITEM_FLAG_MASK || emitter->timer == -1)
        return (emitter->flags & ITEM_FLAG_REVERSE) != 0;
    return (emitter->flags & ITEM_FLAG_REVERSE) == 0;
}
static void flameStopSound(const ItemObj *stopping)
{
    for (ItemObj *item=gs->firstActive;item;item=item->nextActive) {
        if (item == stopping || item->type != ITEM_FLAME) continue;
        ItemObj *owner=flameOwner(item);
        if (owner && flameEmitterWillBeActive(owner)) return;
    }
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
        if (hazardLaraIsBurning(gs->players[slot]))return;
    if (gs->level.soundMap) soundStop(150);
}
void Flame_update(ItemObj *self)
{
    ItemObj *owner=flameOwner(self);
    if (!owner || !flameEmitterWillBeActive(owner)) {
        if (self->flags & ITEM_FLAG_ACTIVE) flameStopSound(self);
        ItemObj_deactivate(self);
        return;
    }
    self->pos=owner->pos;
    self->angle=owner->angle;
    soundPlay(150,&self->pos);
    if (self->timer > 0) { self->timer--; return; }
    Lara *lara=Enemy_getLara(&self->pos);
    if (!lara || lara->health<=0 || hazardLaraIsBurning(lara) ||
        !flameWithinRadius(&self->pos,&lara->pos,600))return;
    hazardDamageLara(lara,3,HAZARD_HIT_FLAME,&self->pos);
    if (flameWithinRadius(&self->pos,&lara->pos,300)) {
        hazardIgniteLara(lara,(uint16)(self-gs->items));
        self->timer=90;
    }
}
void TrapFlameEmitter_update(ItemObj *self)
{
    ItemObj *flame=self->headOffset < MAX_ITEMS ? gs->items+self->headOffset : NULL;
    if (flame && flame->type != ITEM_FLAME) flame=NULL;
    if (!flame || !Object_isActive(self)) {
        if (flame) {
            bool wasActive=(flame->flags & ITEM_FLAG_ACTIVE) != 0;
            flame->timer=0;
            if (wasActive) {
                ItemObj_deactivate(flame);
                flameStopSound(flame);
            }
        }
        return;
    }
    flame->pos=self->pos;
    flame->angle=self->angle;
    if (!(flame->flags & ITEM_FLAG_ACTIVE)) ItemObj_activate(flame);
}
void TrapLavaWedge_update(ItemObj *self)
{
    int32 s,c;
    sincos(self->angle.y,s,c);
    int32 x=i32_add(self->pos.x,(s*2048)>>FIXED_SHIFT), z=i32_add(self->pos.z,(c*2048)>>FIXED_SHIFT);
    const Sector *forward=Room_getSector(self->room,x,z);
    if (forward->floor*256 != self->pos.y) return;
    self->pos.x=i32_add(self->pos.x,(s*25)>>FIXED_SHIFT);
    self->pos.z=i32_add(self->pos.z,(c*25)>>FIXED_SHIFT);
    ItemObj_updateRoom(self,0);
}
