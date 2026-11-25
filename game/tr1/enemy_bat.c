/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static int32 Bat_updateState(ItemObj *self);
static void Bat_updateBase(ItemObj *self);

static int32 Bat_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            self->hSpeed = 0;
            if (self->pos.y < self->roomFloor) {
                self->flags |= ((1 << 1));
            } else {
                self->flags &= ~((1 << 1));
            }
            if (self->flags & ((1 << 1)))
                return Bat_STATE_CIRCLING;
            self->pos.y = self->roomFloor;
            return Bat_STATE_DEATH;
        }
        switch (self->state)
        {
            case Bat_STATE_AWAKE:
            {
                return Bat_STATE_FLY;
            }
            case Bat_STATE_FLY:
            {
                if (self->hitMask)
                    return Bat_STATE_ATTACK;
                break;
            }
            case Bat_STATE_ATTACK:
            {
                if (!self->hitMask) {
                    self->input = MOOD_SLEEP;
                    return Bat_STATE_FLY;
                }
                Enemy_bite(self, Bat_DAMAGE_ATTACK, &(vec3i){0, 16, 45}, 4);
                break;
            }
        }
        return self->goalState;
    }

static void Bat_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Bat_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_bat(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Bat_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Enemy_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_bat operation: %u",(unsigned)call->op);
    }
    return 0;
}
