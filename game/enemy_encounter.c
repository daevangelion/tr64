/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/encounter_spawners.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

enum { ENCOUNTER_CHILD_NONE=0xffff, CENTAUR_STATUE_VERTICAL_RANGE=1024,
    CENTAUR_STATUE_HORIZONTAL_DISTANCE_Q=0x00c40000, CENTAUR_STATUE_SOUND=0x68 };

static ItemObj *Encounter_child(const ItemObj *self)
{
    return self->headOffset<MAX_ITEMS?gs->items+self->headOffset:NULL;
}

static bool Encounter_beginPodHatch(ItemObj *self)
{
    if(self->flags&ITEM_FLAG_REVERSE)return false;
    ItemType expectedType=ITEM_LARA;
    uint16 expectedIndex=ENCOUNTER_CHILD_NONE;
    if(!encounterResolvePreparedChild(self,&expectedType,&expectedIndex) ||
       self->headOffset!=expectedIndex)return false;
    ItemObj *preparedChild=Encounter_child(self);
    if(!preparedChild || preparedChild->type!=expectedType)return false;
    self->flags|=ITEM_FLAG_REVERSE;
    preparedChild->flags=(preparedChild->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
    game_item_activate(preparedChild);
    return true;
}

static void Encounter_finishPodHatch(ItemObj *self)
{
    ItemObj_animProcess(self,true);
    if(!ItemObj_animIsEnd(self,0))return;
    self->visibleMask=0;
    self->flags=(self->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INVISIBLE;
    ItemObj_deactivate(self);
}

static bool encounterStatueShouldHatch(const vec3i *sourcePos,const vec3i *laraPos)
{
    int32 dx=i32_sub(laraPos->x,sourcePos->x);
    int32 dy=i32_sub(laraPos->y,sourcePos->y);
    int32 dz=i32_sub(laraPos->z,sourcePos->z);
    if(abs(dy)>=CENTAUR_STATUE_VERTICAL_RANGE)return false;
    return (int64_t)dx*dx+(int64_t)dz*dz<CENTAUR_STATUE_HORIZONTAL_DISTANCE_Q;
}

static void Encounter_updateStatue(ItemObj *self)
{
    if (self->flags&ITEM_FLAG_REVERSE)return;
    const Lara *lara=Enemy_getLara(&self->pos);
    if (!lara || !encounterStatueShouldHatch(&self->pos,&lara->pos))return;
    ItemType expectedType=ITEM_LARA;
    uint16 expectedIndex=ENCOUNTER_CHILD_NONE;
    if(!encounterResolvePreparedChild(self,&expectedType,&expectedIndex) ||
       expectedType!=ITEM_CENTAUR || self->headOffset!=expectedIndex)return;
    ItemObj *preparedChild=Encounter_child(self);
    if(!preparedChild || preparedChild->type!=ITEM_CENTAUR)return;
    self->flags|=ITEM_FLAG_REVERSE;
    self->visibleMask=0;
    self->flags=(self->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INVISIBLE;
    ItemObj_deactivate(self);
    preparedChild->flags=(preparedChild->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
    game_item_activate(preparedChild);
    soundPlay(CENTAUR_STATUE_SOUND,&self->pos);
}

static void Encounter_update(ItemObj *self)
{
    if(self->type==ITEM_CENTAUR_STATUE) {
        Encounter_updateStatue(self);
        return;
    }
    if(!(self->flags&ITEM_FLAG_REVERSE)) {
        if(self->type==ITEM_MUTANT_EGG_SMALL) {
            if (!(self->flags&ITEM_FLAG_ONCE)) {
                const Lara *lara=Enemy_getLara(&self->pos);
                if (!lara)return;
                vec3i d=vec3i_sub(lara->pos,self->pos);
                if (!(abs(d.x)<4096 && abs(d.y)<4096 && abs(d.z)<4096))return;
            }
        }
        if(!Encounter_beginPodHatch(self))return;
        self->goalState=1;
        self->visibleMask=0x00fffe00u;
    }
    Encounter_finishPodHatch(self);
}

uint32 tr64_unit_enemy_encounter(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Encounter_construct(call->self);break;
    case GAME_ITEM_ACTIVATE:Encounter_activate(call->self);break;
    case GAME_ITEM_UPDATE:Encounter_update(call->self);break;
    case GAME_ITEM_COLLIDE:
        /* EncounterSource inherits ItemObj::collide, which is empty
         * (src/fixed/item.h:1438-1441 at the pinned revision). */
        break;
    default:sys_error("game encounter operation: %u",(unsigned)call->op);
    }
    return 0;
}
