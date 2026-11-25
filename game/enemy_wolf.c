/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static void Wolf_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Wolf_updateState(ItemObj *self);
static void Wolf_updateBase(ItemObj *self);

static void Wolf_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, SND_HIT_WOLF);
        if (self->health <= 0) {
            ItemObj_animSet(self, i32_add(i32_add((gs->level.models[self->type]).animIndex, Wolf_ANIM_DEATH), (i32_mul(rand_logic(), (3)) >> 15)), true, 0);
        }
    }

static int32 Wolf_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        switch (self->state)
        {
            case Wolf_STATE_STOP:
            {
                if (self->nextState)
                    return self->nextState;
                return Wolf_STATE_WALK;
            }
            case Wolf_STATE_WALK:
            {
                (self->extraE)->maxTurn = (i32_mul((2), (0x10000 / 360)));
                gs->tinfo.tilt = gs->tinfo.turn >> 1;
                if (self->input != MOOD_SLEEP) {
                    self->nextState = Wolf_STATE_NONE;
                    return Wolf_STATE_STALK;
                }
                if (rand_logic() < 0x20) {
                    self->nextState = Wolf_STATE_SLEEP;
                    return Wolf_STATE_STOP;
                }
                break;
            }
            case Wolf_STATE_RUN:
            {
                (self->extraE)->maxTurn = (i32_mul((5), (0x10000 / 360)));
                gs->tinfo.tilt = gs->tinfo.turn;
                if (gs->tinfo.front && gs->tinfo.dist < Wolf_DIST_ATTACK)
                {
                    if (gs->tinfo.dist <= (Wolf_DIST_ATTACK >> 1) && !gs->tinfo.behind)
                    {
                        self->nextState = Wolf_STATE_NONE;
                        return Wolf_STATE_ATTACK;
                    }
                    self->nextState = Wolf_STATE_STALK;
                    return Wolf_STATE_GROWL;
                }
                if (self->input == MOOD_STALK && gs->tinfo.dist < Wolf_DIST_STALK)
                {
                    self->nextState = Wolf_STATE_STALK;
                    return Wolf_STATE_GROWL;
                }
                if (self->input == MOOD_SLEEP)
                    return Wolf_STATE_GROWL;
                break;
            }
            case Wolf_STATE_STALK:
            {
                (self->extraE)->maxTurn = (i32_mul((2), (0x10000 / 360)));
                if (self->input == MOOD_ESCAPE)
                    return Wolf_STATE_RUN;
                if ((gs->tinfo.dist < Wolf_DIST_BITE) && gs->tinfo.canAttack && ((gs->tinfo.target)->health > 0))
                    return Wolf_STATE_BITE;
                if (gs->tinfo.dist > Wolf_DIST_STALK)
                    return Wolf_STATE_RUN;
                if (self->input == MOOD_ATTACK)
                {
                    if (!gs->tinfo.front || (gs->tinfo.dist > Wolf_DIST_ATTACK) || gs->tinfo.behind)
                        return Wolf_STATE_RUN;
                    break;
                }
                if (rand_logic() < 0x180)
                {
                    self->nextState = Wolf_STATE_HOWL;
                    return Wolf_STATE_GROWL;
                }
                if (self->input == MOOD_SLEEP)
                    return Wolf_STATE_GROWL;
                break;
            }
            case Wolf_STATE_SLEEP:
            {
                if ((self->input == MOOD_ESCAPE) || Enemy_checkZone(self))
                    self->nextState = Wolf_STATE_GROWL;
                else if (rand_logic() < 0x20)
                    self->nextState = Wolf_STATE_WALK;
                else
                    break;
                return Wolf_STATE_STOP;
            }
            case Wolf_STATE_GROWL:
            {
                if (self->nextState)
                    return self->nextState;
                if (self->input == MOOD_ESCAPE)
                    return Wolf_STATE_RUN;
                if (gs->tinfo.dist < Wolf_DIST_BITE && gs->tinfo.canAttack && ((gs->tinfo.target)->health > 0))
                    return Wolf_STATE_BITE;
                if (self->input == MOOD_STALK)
                    return Wolf_STATE_STALK;
                if (self->input == MOOD_SLEEP)
                    return Wolf_STATE_STOP;
                return Wolf_STATE_RUN;
            }
            case Wolf_STATE_ATTACK:
            {
                gs->tinfo.tilt = gs->tinfo.turn;
                if (!self->nextState && (self->hitMask & Wolf_HIT_MASK)) {
                    Enemy_bite(self, Wolf_DAMAGE_ATTACK, &(vec3i){0, -14, 174}, 6);
                    self->nextState = Wolf_STATE_RUN;
                }
                return Wolf_STATE_RUN;
            }
            case Wolf_STATE_BITE:
            {
                if (!self->nextState && (self->hitMask & Wolf_HIT_MASK)) {
                    Enemy_bite(self, Wolf_DAMAGE_BITE, &(vec3i){0, -14, 174}, 6);
                    self->nextState = Wolf_STATE_GROWL;
                }
                return Wolf_STATE_RUN;
            }
        }
        return self->goalState;
    }

static void Wolf_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Wolf_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_wolf(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Wolf_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Wolf_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_wolf operation: %u",(unsigned)call->op);
    }
    return 0;
}
