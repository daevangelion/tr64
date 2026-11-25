/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static void Crocodile_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Crocodile_updateState(ItemObj *self);
static void Crocodile_updateBase(ItemObj *self);

static void Crocodile_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, 0);
        if (self->health <= 0) {
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, ((self->type == ITEM_CROCODILE_WATER) ?
                    Crocodile_ANIM_DEATH_WATER : Crocodile_ANIM_DEATH_LAND)), true, 0);
        }
    }

static int32 Crocodile_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        if (self->type == ITEM_CROCODILE_WATER)
        {
            if (gs->tinfo.target)
                (gs->tinfo.waypoint).y = ((gs->tinfo.target)->pos).y;
            switch (self->state)
            {
                case Crocodile_STATE_WATER_SWIM:
                {
                    (self->extraE)->maxTurn = Crocodile_TURN_FAST;
                    if (gs->tinfo.front && self->hitMask)
                        return Crocodile_STATE_WATER_BITE;
                    break;
                }
                case Crocodile_STATE_WATER_BITE:
                {
                    if (self->hitMask)
                    {
                        if (self->nextState)
                            return Crocodile_STATE_WATER_BITE;
                        Enemy_bite(self, Crocodile_DAMAGE_BITE, &(vec3i){5, -21, 467}, 9);
                        self->nextState = Crocodile_STATE_WATER_SWIM;
                    }
                    return Crocodile_STATE_WATER_SWIM;
                }
            }
            return self->goalState;
        }
        switch (self->state)
        {
            case Crocodile_STATE_STOP:
            {
                if (gs->tinfo.canAttack && (gs->tinfo.dist < Crocodile_DIST_BITE))
                    return Crocodile_STATE_BITE;
                switch (self->input)
                {
                    case MOOD_ESCAPE: return Crocodile_STATE_RUN;
                    case MOOD_ATTACK: return (gs->tinfo.front || (gs->tinfo.dist < Crocodile_DIST_TURN)) ? Crocodile_STATE_RUN : Crocodile_STATE_TURN;
                    case MOOD_STALK: return Crocodile_STATE_WALK;
                }
                break;
            }
            case Crocodile_STATE_RUN:
            {
                (self->extraE)->maxTurn = Crocodile_TURN_FAST;
                if (gs->tinfo.front && (self->hitMask & Crocodile_HIT_MASK))
                    return Crocodile_STATE_STOP;
                switch (self->input)
                {
                    case MOOD_SLEEP: return Crocodile_STATE_STOP;
                    case MOOD_STALK: return Crocodile_STATE_WALK;
                    case MOOD_ATTACK:
                        if ((gs->tinfo.dist > Crocodile_DIST_TURN) && !gs->tinfo.front)
                            return Crocodile_STATE_STOP;
                        break;
                }
                break;
            }
            case Crocodile_STATE_WALK:
            {
                (self->extraE)->maxTurn = Crocodile_TURN_FAST;
                if (gs->tinfo.front && (self->hitMask & Crocodile_HIT_MASK))
                    return Crocodile_STATE_STOP;
                switch (self->input)
                {
                    case MOOD_SLEEP: return Crocodile_STATE_STOP;
                    case MOOD_ATTACK:
                    case MOOD_ESCAPE: return Crocodile_STATE_RUN;
                }
                break;
            }
            case Crocodile_STATE_TURN:
            {
                (self->extraE)->maxTurn = Crocodile_TURN_FAST;
                return gs->tinfo.front ? Crocodile_STATE_WALK : Crocodile_STATE_TURN;
            }
            case Crocodile_STATE_BITE:
            {
                if (!self->nextState) {
                    Enemy_bite(self, Crocodile_DAMAGE_BITE, &(vec3i){5, -21, 467}, 9);
                    self->nextState = Crocodile_STATE_STOP;
                }
                break;
            }
        }
        return self->goalState;
    }

static void Crocodile_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Crocodile_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_crocodile(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Crocodile_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Crocodile_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_crocodile operation: %u",(unsigned)call->op);
    }
    return 0;
}
