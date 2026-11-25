/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static uint32 Mummy_getJointMask(const ItemObj *self);
static int32 Mummy_updateState(ItemObj *self);
static void Mummy_update(ItemObj *self);
static void Mummy_updateBase(ItemObj *self);

static uint32 Mummy_getJointMask(const ItemObj *self)
{
        int32 jointCount = (gs->level.models[self->type]).count;
        if (jointCount <= 0)
            return 0;
        return jointCount >= 32 ?
            0xFFFFFFFF : (((uint32)(1)) << jointCount) - 1;
    }

static int32 Mummy_updateState(ItemObj *self)
{
        if (self->state == Mummy_STATE_IDLE && (self->health <= 0 || self->hitMask))
        {
            self->health = 0;
            return Mummy_STATE_FALL;
        }
        return self->goalState;
    }

static void Mummy_update(ItemObj *self)
{
        if (self->health == -0x4000 &&
            (self->flags & ((3 << 3))) == ((3 << 3)))
            return;
        uint32 jointMask = Mummy_getJointMask(self);
        bool disintegrating = self->visibleMask != 0xFFFFFFFF;
        if (self->health <= 0 &&
            (disintegrating ||
             (self->state == Mummy_STATE_FALL && ItemObj_animIsEnd(self, 0))))
        {
            Enemy_explode(self, jointMask);
            if (!(self->visibleMask & jointMask))
            {
                ItemObj_deactivate(self);
                self->flags &= ~(((3 << 3)) | ((1 << 5)));
                self->flags |= ((3 << 3));
                self->health = -0x4000;
                if (self->extraE)
                    Enemy_disable(self);
            }
            return;
        }
        Mummy_updateBase(self);
    }

static void Mummy_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Mummy_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_mummy(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Mummy_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Enemy_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_mummy operation: %u",(unsigned)call->op);
    }
    return 0;
}
