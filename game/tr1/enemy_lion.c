/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static void Lion_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Lion_updateState(ItemObj *self);
static void Lion_updateBase(ItemObj *self);

static void Lion_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, (self->type == ITEM_PUMA) ? 0 : SND_HIT_LION);
        if (self->health <= 0)
        {
            int32 deathAnim = (self->type == ITEM_PUMA) ? Lion_ANIM_DEATH_PUMA : Lion_ANIM_DEATH_LION;
            ItemObj_animSet(self, i32_add(i32_add((gs->level.models[self->type]).animIndex, deathAnim), (i32_mul(rand_logic(), (2)) >> 15)), true, 0);
        }
    }

static int32 Lion_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        if (self->nextState == self->state)
            self->nextState = Lion_STATE_NONE;
        switch (self->state)
        {
            case Lion_STATE_STOP:
            {
                if (self->nextState)
                    return self->nextState;
                if (self->input == MOOD_SLEEP)
                    return Lion_STATE_WALK;
                if (gs->tinfo.front && (self->hitMask & Lion_HIT_MASK))
                    return Lion_STATE_BITE;
                if (gs->tinfo.front && (gs->tinfo.dist < Lion_DIST_ATTACK))
                    return Lion_STATE_ATTACK;
                return Lion_STATE_RUN;
            }
            case Lion_STATE_WALK:
            {
                (self->extraE)->maxTurn = Lion_TURN_SLOW;
                gs->tinfo.tilt = gs->tinfo.turn >> 1;
                if (self->input != MOOD_SLEEP)
                    return Lion_STATE_STOP;
                if (rand_logic() < 0x0D)
                {
                    self->nextState = Lion_STATE_ROAR;
                    return Lion_STATE_STOP;
                }
                break;
            }
            case Lion_STATE_RUN:
            {
                (self->extraE)->maxTurn = Lion_TURN_FAST;
                gs->tinfo.tilt = gs->tinfo.turn >> 1;
                if ((self->input == MOOD_SLEEP) ||
                    (gs->tinfo.front && (gs->tinfo.dist < Lion_DIST_ATTACK)) ||
                    (gs->tinfo.front && (self->hitMask & Lion_HIT_MASK)))
                    return Lion_STATE_STOP;
                if ((self->input == MOOD_ESCAPE) && (rand_logic() < 0x0D))
                {
                    self->nextState = Lion_STATE_ROAR;
                    return Lion_STATE_STOP;
                }
                break;
            }
            case Lion_STATE_ROAR:
            {
                (self->extraE)->maxTurn = Lion_TURN_SLOW;
                gs->tinfo.tilt = gs->tinfo.turn >> 1;
                break;
            }
            case Lion_STATE_ATTACK:
            case Lion_STATE_BITE:
            {
                if (!self->nextState && (self->hitMask & Lion_HIT_MASK))
                {
                    Enemy_bite(self, (self->state == Lion_STATE_ATTACK) ? Lion_DAMAGE_ATTACK : Lion_DAMAGE_BITE, &(vec3i){-2, -10, 132}, 21);
                    self->nextState = Lion_STATE_STOP;
                }
                break;
            }
        }
        return self->goalState;
    }

static void Lion_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Lion_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_lion(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Lion_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Lion_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_lion operation: %u",(unsigned)call->op);
    }
    return 0;
}
