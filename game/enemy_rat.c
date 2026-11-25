/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static void Rat_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Rat_updateState(ItemObj *self);
static void Rat_updateBase(ItemObj *self);

static void Rat_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, SND_HIT_RAT);
        if (self->health <= 0) {
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, ((self->type == ITEM_RAT_WATER) ?
                    Rat_ANIM_DEATH_WATER : Rat_ANIM_DEATH_LAND)), true, 0);
        }
    }

static int32 Rat_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        if (self->type == ITEM_RAT_WATER)
        {
            switch (self->state)
            {
                case Rat_STATE_WATER_SWIM:
                {
                    (self->extraE)->maxTurn = Rat_TURN_FAST;
                    if (gs->tinfo.front && (self->hitMask & Rat_HIT_MASK))
                        return Rat_STATE_WATER_BITE;
                    break;
                }
                case Rat_STATE_WATER_BITE:
                {
                    if (gs->tinfo.front && (self->hitMask & Rat_HIT_MASK))
                    {
                        if (self->nextState)
                            return Rat_STATE_WATER_BITE;
                        Enemy_bite(self, Rat_DAMAGE_BITE, &(vec3i){0, -11, 108}, 3);
                        self->nextState = Rat_STATE_WATER_SWIM;
                    }
                    return Rat_STATE_WATER_SWIM;
                }
            }
            return self->goalState;
        }
        switch (self->state)
        {
            case Rat_STATE_STOP:
            {
                if (self->nextState)
                    return self->nextState;
                if (gs->tinfo.canAttack && (gs->tinfo.dist < Rat_DIST_BITE))
                    return Rat_STATE_BITE;
                return Rat_STATE_RUN;
            }
            case Rat_STATE_RUN:
            {
                (self->extraE)->maxTurn = Rat_TURN_FAST;
                if (gs->tinfo.front && (self->hitMask & Rat_HIT_MASK))
                    return Rat_STATE_STOP;
                if (gs->tinfo.canAttack && (gs->tinfo.dist < Rat_DIST_ATTACK))
                    return Rat_STATE_ATTACK;
                if (gs->tinfo.front && (rand_logic() < Rat_WAIT_RANDOM))
                {
                    self->nextState = Rat_STATE_WAIT;
                    return Rat_STATE_STOP;
                }
                break;
            }
            case Rat_STATE_ATTACK:
            case Rat_STATE_BITE:
            {
                if (!self->nextState && gs->tinfo.front && (self->hitMask & Rat_HIT_MASK))
                {
                    Enemy_bite(self, Rat_DAMAGE_BITE, &(vec3i){0, -11, 108}, 3);
                    self->nextState = (self->state == Rat_STATE_ATTACK) ? Rat_STATE_RUN : Rat_STATE_STOP;
                }
                break;
            }
            case Rat_STATE_WAIT:
            {
                if (self->input == MOOD_SLEEP || rand_logic() < Rat_WAIT_RANDOM)
                    return Rat_STATE_STOP;
                break;
            }
        }
        return self->goalState;
    }

static void Rat_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Rat_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_rat(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Rat_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Rat_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_rat operation: %u",(unsigned)call->op);
    }
    return 0;
}
