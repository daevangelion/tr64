/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static void Bear_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Bear_updateState(ItemObj *self);
static void Bear_updateBase(ItemObj *self);

static void Bear_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, SND_HIT_BEAR);
    }

static int32 Bear_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            switch (self->state)
            {
                case Bear_STATE_HIND:
                    return Bear_STATE_HOWL;
                case Bear_STATE_WALK:
                case Bear_STATE_RUN:
                    return Bear_STATE_STOP;
                case Bear_STATE_STOP:
                    self->flags &= ~((1 << 14));
                    return Bear_STATE_DEATH;
                case Bear_STATE_HOWL:
                    self->flags |= ((1 << 14));
                    return Bear_STATE_DEATH;
                case Bear_STATE_DEATH:
                    if ((self->flags & ((1 << 14))) && (self->hitMask & Bear_HIT_MASK) && gs->tinfo.target)
                    {
                        ItemObj_hit(gs->tinfo.target, Bear_DAMAGE_FALL, &(vec3i){0, 0, 0}, 0);
                        self->flags &= ~((1 << 14));
                    }
                    return Bear_STATE_DEATH;
                default:
                    return self->goalState;
            }
        }
        if (self->flags & ((1 << 6))) {
            self->flags |= ((1 << 14));
        } else {
            if (self->flags & ((1 << 14))) {
                self->flags |= ((1 << 6));
            }
        }
        switch (self->state)
        {
            case Bear_STATE_WALK:
            {
                (self->extraE)->maxTurn = (i32_mul((2), (0x10000 / 360)));
                if ((gs->tinfo.target)->health <= 0 && gs->tinfo.front && (self->hitMask & Bear_HIT_MASK))
                    return self->nextState = Bear_STATE_STOP;
                if (self->input != MOOD_SLEEP)
                {
                    if (self->input == MOOD_ESCAPE) {
                        self->nextState = Bear_STATE_NONE;
                    }
                    return Bear_STATE_STOP;
                }
                if (rand_logic() < 0x50)
                {
                    self->nextState = Bear_STATE_GROWL;
                    return Bear_STATE_STOP;
                }
                break;
            }
            case Bear_STATE_STOP:
            {
                if ((gs->tinfo.target)->health <= 0)
                    return (gs->tinfo.canAttack && gs->tinfo.dist < 768) ? Bear_STATE_EAT : Bear_STATE_WALK;
                if (self->nextState)
                    return self->nextState;
                return (self->input == MOOD_SLEEP) ? Bear_STATE_WALK : Bear_STATE_RUN;
            }
            case Bear_STATE_HIND:
            {
                if (self->flags & ((1 << 6))) {
                    self->nextState = Bear_STATE_NONE;
                    return Bear_STATE_HOWL;
                }
                if (gs->tinfo.front && (self->hitMask & Bear_HIT_MASK))
                    return Bear_STATE_HOWL;
                if (self->input == MOOD_ESCAPE) {
                    self->nextState = Bear_STATE_NONE;
                    return Bear_STATE_HOWL;
                }
                if (self->input == MOOD_SLEEP || rand_logic() < 0x50) {
                    self->nextState = Bear_STATE_GROWL;
                    return Bear_STATE_HOWL;
                }
                if (gs->tinfo.dist > 2048 || rand_logic() < 0x600) {
                    self->nextState = Bear_STATE_STOP;
                    return Bear_STATE_HOWL;
                }
                return Bear_STATE_HOWL;
            }
            case Bear_STATE_RUN:
            {
                (self->extraE)->maxTurn = (i32_mul((5), (0x10000 / 360)));
                if (self->hitMask & Bear_HIT_MASK) {
                    ItemObj_hit(gs->tinfo.target, 3, &(self->pos), 0);
                    if (self->health >= 0) {
                        self->nextState = Bear_STATE_NONE;
                    }
                }
                if ((gs->tinfo.target)->health <= 0 || self->input == MOOD_SLEEP)
                    return Bear_STATE_STOP;
                if (!self->nextState && gs->tinfo.front)
                {
                    if (gs->tinfo.dist < 2048)
                    {
                        if (!(self->flags & ((1 << 6))) && (rand_logic() < 0x300))
                        {
                            self->nextState = Bear_STATE_HOWL;
                            return Bear_STATE_STOP;
                        }
                        if (gs->tinfo.dist < 1024)
                            return Bear_STATE_BITE;
                    }
                }
                break;
            }
            case Bear_STATE_HOWL:
            {
                if (self->flags & ((1 << 6)))
                {
                    self->nextState = Bear_STATE_NONE;
                    return Bear_STATE_STOP;
                }
                if (self->nextState)
                    return self->nextState;
                if (self->input == MOOD_SLEEP || self->input == MOOD_ESCAPE)
                    return Bear_STATE_STOP;
                if (gs->tinfo.canAttack && gs->tinfo.dist < 600)
                    return Bear_STATE_ATTACK;
                return Bear_STATE_HIND;
            }
            case Bear_STATE_BITE:
            {
                if (!self->nextState && (self->hitMask & Bear_HIT_MASK)) {
                    Enemy_bite(self, Bear_DAMAGE_BITE, &(vec3i){0, 96, 335}, 14);
                    self->nextState = Bear_STATE_STOP;
                }
                break;
            }
            case Bear_STATE_ATTACK:
            {
                if (!self->nextState && (self->hitMask & Bear_HIT_MASK) && gs->tinfo.target) {
                    ItemObj_hit(gs->tinfo.target, Bear_DAMAGE_ATTACK, &(vec3i){0, 0, 0}, 0);
                    self->nextState = Bear_STATE_HOWL;
                }
                break;
            }
        }
        return self->goalState;
    }

static void Bear_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Bear_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_bear(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Bear_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Bear_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_bear operation: %u",(unsigned)call->op);
    }
    return 0;
}
