/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static uint32 Centaur_getJointMask(const ItemObj *self);
static void Centaur_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Centaur_updateState(ItemObj *self);
static void Centaur_update(ItemObj *self);
static void Centaur_updateBase(ItemObj *self);

static uint32 Centaur_getJointMask(const ItemObj *self)
{
        int32 jointCount = (gs->level.models[self->type]).count;
        if (jointCount <= 0)
            return 0;
        return jointCount >= 32 ?
            0xFFFFFFFF : (((uint32)(1)) << jointCount) - 1;
    }

static void Centaur_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, 0);
        if (self->health <= 0)
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Centaur_ANIM_DEATH), true, 0);
    }

static int32 Centaur_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            if (self->state != Centaur_STATE_DEATH)
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Centaur_ANIM_DEATH), true, 0);
            return self->goalState;
        }
        (self->extraE)->maxTurn = self->state == Centaur_STATE_RUN ? Centaur_TURN_FAST : 0;
        if (self->nextState == self->state)
            self->nextState = Centaur_STATE_NONE;
        switch (self->state)
        {
            case Centaur_STATE_STOP:
                if (self->nextState != Centaur_STATE_NONE)
                    return self->nextState;
                if (gs->tinfo.canAttack && gs->tinfo.dist < Centaur_DIST_RUN)
                    return Centaur_STATE_RUN;
                if (Enemy_targetIsVisible(self, Centaur_DIST_SHOT))
                    return Centaur_STATE_AIM;
                return Centaur_STATE_RUN;
            case Centaur_STATE_RUN:
                if (gs->tinfo.canAttack && gs->tinfo.dist < Centaur_DIST_RUN)
                {
                    self->nextState = Centaur_STATE_IDLE;
                    return Centaur_STATE_STOP;
                }
                if (Enemy_targetIsVisible(self, Centaur_DIST_SHOT))
                {
                    self->nextState = Centaur_STATE_AIM;
                    return Centaur_STATE_STOP;
                }
                if (rand_logic() < Centaur_RUN_IDLE_CHANCE)
                {
                    self->nextState = Centaur_STATE_IDLE;
                    return Centaur_STATE_STOP;
                }
                break;
            case Centaur_STATE_AIM:
                if (self->nextState != Centaur_STATE_NONE)
                    return self->nextState;
                return Enemy_targetIsVisible(self, Centaur_DIST_SHOT) ?
                    Centaur_STATE_FIRE : Centaur_STATE_STOP;
            case Centaur_STATE_FIRE:
                if (self->nextState == Centaur_STATE_NONE)
                {
                    self->nextState = Centaur_STATE_AIM;
                    Enemy_shotSpread(self, ITEM_CENTAUR_BULLET, Centaur_JOINT_FIRE, &(vec3i){11, 415, 41}, Centaur_SHOT_SPREAD, Centaur_SHOT_SPREAD);
                }
                break;
            case Centaur_STATE_IDLE:
                if (self->nextState == Centaur_STATE_NONE && (self->hitMask & Centaur_HIT_MASK))
                {
                    Enemy_bite(self, Centaur_DAMAGE_MELEE, &(vec3i){50, 30, 0}, Centaur_JOINT_MELEE);
                    self->nextState = Centaur_STATE_STOP;
                }
                break;
        }
        return self->goalState;
    }

static void Centaur_update(ItemObj *self)
{
        if (self->health == -0x4000 &&
            (self->flags & ((3 << 3))) == ((3 << 3)))
            return;
        uint32 jointMask = Centaur_getJointMask(self);
        bool disintegrating = self->visibleMask != 0xFFFFFFFF;
        if (self->health <= 0 && (disintegrating || ItemObj_animIsEnd(self, 0)))
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
        Centaur_updateBase(self);
    }

static void Centaur_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Centaur_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_centaur(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Centaur_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Centaur_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_centaur operation: %u",(unsigned)call->op);
    }
    return 0;
}
