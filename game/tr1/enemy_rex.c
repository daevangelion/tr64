/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static int32 Rex_updateState(ItemObj *self);
static void Rex_updateBase(ItemObj *self);

static int32 Rex_updateState(ItemObj *self)
{
        if (self->health <= 0) {
            return (self->state == Rex_STATE_STOP) ? Rex_STATE_DEATH : Rex_STATE_STOP;
        }
        (self->extraE)->rotNeck = ((self->extraE)->rotHead >>= 1);
        if (self->hitMask)
        {
            ItemObj_hit(gs->tinfo.target, (self->state == Rex_STATE_RUN) ? Rex_DAMAGE_RUN : Rex_DAMAGE_WALK, &(vec3i){0, 0, 0}, 0);
        }
        bool walk = (gs->tinfo.canAttack && (gs->tinfo.dist > Rex_DIST_BITE) && (gs->tinfo.dist < Rex_DIST_WALK)) ||
                    (gs->tinfo.behind && !gs->tinfo.front && (self->input != MOOD_ESCAPE));
        switch (self->state)
        {
            case Rex_STATE_STOP:
            {
                if (self->nextState)
                    return self->nextState;
                if (gs->tinfo.canAttack && (gs->tinfo.dist < Rex_DIST_BITE))
                    return Rex_STATE_BITE;
                if ((self->input == MOOD_SLEEP) || walk)
                    return Rex_STATE_WALK;
                return Rex_STATE_RUN;
            }
            case Rex_STATE_WALK:
            {
                (self->extraE)->maxTurn = (i32_mul((2), (0x10000 / 360)));
                if ((self->input != MOOD_SLEEP) && !walk)
                    return Rex_STATE_STOP;
                if (gs->tinfo.front && (rand_logic() < 0x200))
                {
                    self->nextState = Rex_STATE_ROAR;
                    return Rex_STATE_STOP;
                }
                break;
            }
            case Rex_STATE_RUN:
            {
                (self->extraE)->maxTurn = (i32_mul((4), (0x10000 / 360)));
                if (((gs->tinfo.dist < Rex_DIST_RUN) && gs->tinfo.canAttack) || walk)
                    return Rex_STATE_STOP;
                if ((self->input != MOOD_ESCAPE) && gs->tinfo.front && (rand_logic() < 0x200))
                {
                    self->nextState = Rex_STATE_ROAR;
                    return Rex_STATE_STOP;
                }
                if (self->input == MOOD_SLEEP)
                    return Rex_STATE_STOP;
                break;
            }
            case Rex_STATE_BITE:
            {
                self->nextState = Rex_STATE_WALK;
                if (self->hitMask & Rex_HIT_MASK)
                {
                    self->angle.x = 0;
                    self->angle.z = 0;
                    ((gs->tinfo.target)->pos = self->pos);
                    ((gs->tinfo.target)->angle = self->angle);
                    (gs->tinfo.target)->flags &= ~((1 << 1));
                    ItemObj_hit(gs->tinfo.target, Rex_DAMAGE_FATAL, &(vec3i){0, 0, 0}, 0);
                    ItemObj_meshSwap(gs->tinfo.target, ITEM_LARA_SPEC, 0xFFFFFFFF);
                    ItemObj_animSet(gs->tinfo.target, i32_add((gs->level.models[ITEM_LARA_SPEC]).animIndex, 1), true, 0);
                    if ((gs->tinfo.target)->room != self->room)
                    {
                        Room_remove((gs->tinfo.target)->room, gs->tinfo.target);
                        Room_add(self->room, gs->tinfo.target);
                    }
                    return Rex_STATE_FATAL;
                }
                break;
            }
        }
        return self->goalState;
    }

static void Rex_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Rex_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_rex(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Rex_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Enemy_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_rex operation: %u",(unsigned)call->op);
    }
    return 0;
}
