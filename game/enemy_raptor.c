/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static void Raptor_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Raptor_updateState(ItemObj *self);
static void Raptor_updateBase(ItemObj *self);

static void Raptor_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, 0);
        if (self->health <= 0) {
            ItemObj_animSet(self, i32_add(i32_add((gs->level.models[self->type]).animIndex, Raptor_ANIM_DEATH_1), (i32_mul(rand_logic(), (2)) >> 15)), true, 0);
        }
    }

static int32 Raptor_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        switch (self->state)
        {
            case Raptor_STATE_STOP: {
                if (self->nextState)
                    return self->nextState;
                if ((self->hitMask & Raptor_HIT_MASK) || (gs->tinfo.canAttack && gs->tinfo.dist < Raptor_DIST_BITE))
                    return Raptor_STATE_BITE;
                if (gs->tinfo.canAttack && gs->tinfo.dist < Raptor_DIST_ATTACK)
                    return Raptor_STATE_ATTACK_1;
                if (self->input == MOOD_SLEEP)
                    return Raptor_STATE_WALK;
                return Raptor_STATE_RUN;
            }
            case Raptor_STATE_WALK: {
                (self->extraE)->maxTurn = (i32_mul((1), (0x10000 / 360)));
                gs->tinfo.tilt = gs->tinfo.turn >> 1;
                if (self->input != MOOD_SLEEP)
                    return Raptor_STATE_STOP;
                if (rand_logic() < 0x100) {
                    self->nextState = Raptor_STATE_ROAR;
                    return Raptor_STATE_STOP;
                }
                break;
            }
            case Raptor_STATE_RUN: {
                (self->extraE)->maxTurn = (i32_mul((4), (0x10000 / 360)));
                gs->tinfo.tilt = gs->tinfo.turn;
                if (self->hitMask & Raptor_HIT_MASK)
                    return Raptor_STATE_STOP;
                if (gs->tinfo.canAttack && gs->tinfo.dist < Raptor_DIST_ATTACK)
                {
                    if (self->goalState != Raptor_STATE_RUN)
                        break;
                    return (rand_logic() < 0x2000) ? Raptor_STATE_STOP : Raptor_STATE_ATTACK_2;
                }
                if (gs->tinfo.front && (self->input != MOOD_ESCAPE) && (rand_logic() < 0x100))
                {
                    self->nextState = Raptor_STATE_ROAR;
                    return Raptor_STATE_STOP;
                }
                if (self->input == MOOD_SLEEP)
                    return Raptor_STATE_STOP;
                break;
            }
            case Raptor_STATE_ATTACK_1:
            case Raptor_STATE_ATTACK_2:
            case Raptor_STATE_BITE:
            {
                gs->tinfo.tilt = gs->tinfo.turn;
                if (!self->nextState && gs->tinfo.front && (self->hitMask & Raptor_HIT_MASK)) {
                    Enemy_bite(self, Raptor_DAMAGE_ATTACK, &(vec3i){0, 66, 318}, 22);
                    self->nextState = (self->state == Raptor_STATE_ATTACK_2) ? Raptor_STATE_RUN : Raptor_STATE_STOP;
                }
                break;
            }
        }
        return self->goalState;
    }

static void Raptor_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Raptor_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_raptor(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Raptor_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Raptor_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_raptor operation: %u",(unsigned)call->op);
    }
    return 0;
}
