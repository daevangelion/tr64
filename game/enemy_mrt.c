/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static int32 MrT_getGunJoint(const ItemObj *self);
static vec3i MrT_getMuzzleOffset(const ItemObj *self);
static int32 MrT_getShotDamage(const ItemObj *self);
static int32 MrT_getDeathAnimation(const ItemObj *self);
static int32 MrT_getHitSound(const ItemObj *self);
static void MrT_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 MrT_updateHumanState(ItemObj *self);
static void MrT_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 MrT_updateState(ItemObj *self);
static bool MrT_fire(ItemObj *self);
static void MrT_updateBase(ItemObj *self);

static int32 MrT_getGunJoint(const ItemObj *self)
{
    (void)self;
        return MrT_JOINT_GUN;
    }

static vec3i MrT_getMuzzleOffset(const ItemObj *self)
{
    (void)self;
        return vec3i_create(-20, -20, 300);
    }

static int32 MrT_getShotDamage(const ItemObj *self)
{
    (void)self;
        return MrT_DAMAGE;
    }

static int32 MrT_getDeathAnimation(const ItemObj *self)
{
    (void)self;
        return MrT_ANIM_DEATH;
    }

static int32 MrT_getHitSound(const ItemObj *self)
{
    (void)self;
        return 0;
    }

static void MrT_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
        if (self->health <= 0)
            return;
        MrT_humanHit(self, damage, point, soundId);
        if (self->health <= 0)
            ItemObj_add(ITEM_SHOTGUN, self->room, &(self->pos), 0);
    }

static int32 MrT_updateHumanState(ItemObj *self)
{
        if (self->nextState == self->state)
            self->nextState = Human_STATE_NONE;
        switch (self->state)
        {
            case Human_STATE_STOP:
                if (self->nextState != Human_STATE_NONE)
                    return self->nextState;
                if (Enemy_targetIsVisible(self, Human_DIST_SHOT))
                    return Human_STATE_AIM;
                if (self->input == MOOD_SLEEP)
                    return Human_STATE_WALK;
                return Human_STATE_RUN;
            case Human_STATE_WALK:
                if (self->input == MOOD_ESCAPE || !gs->tinfo.front)
                    self->nextState = Human_STATE_RUN;
                else if (Enemy_targetIsVisible(self, Human_DIST_SHOT))
                    self->nextState = Human_STATE_AIM;
                else if (gs->tinfo.dist > MrT_DIST_WALK)
                    self->nextState = Human_STATE_RUN;
                else
                    break;
                return Human_STATE_STOP;
            case Human_STATE_RUN:
                if (self->input == MOOD_ESCAPE || !gs->tinfo.front)
                    break;
                if (Enemy_targetIsVisible(self, Human_DIST_SHOT))
                    self->nextState = Human_STATE_AIM;
                else if (gs->tinfo.dist < MrT_DIST_WALK && gs->tinfo.front)
                    self->nextState = Human_STATE_WALK;
                else
                    break;
                return Human_STATE_STOP;
            case Human_STATE_AIM:
                if (self->nextState != Human_STATE_NONE)
                    return Human_STATE_STOP;
                return Enemy_targetIsVisible(self, Human_DIST_SHOT) ?
                    Human_STATE_WAIT : Human_STATE_STOP;
            case Human_STATE_WAIT:
                if (self->nextState == Human_STATE_NONE)
                {
                    MrT_fire(self);
                    self->nextState = Human_STATE_AIM;
                }
                if (self->input == MOOD_ESCAPE)
                    self->nextState = Human_STATE_RUN;
                break;
        }
        return self->goalState;
    }

static void MrT_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, MrT_getHitSound(self));
        if (self->health <= 0)
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, MrT_getDeathAnimation(self)), true, 0);
    }

static int32 MrT_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            if (self->state != Human_STATE_DEATH)
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, MrT_getDeathAnimation(self)), true, 0);
            return self->goalState;
        }
        (self->extraE)->maxTurn =
            self->state == Human_STATE_RUN ? Human_TURN_RUN :
            self->state == Human_STATE_WALK ? Human_TURN_WALK : 0;
        return MrT_updateHumanState(self);
    }

static bool MrT_fire(ItemObj *self)
{
    vec3i offset=MrT_getMuzzleOffset(self);
    return Enemy_humanFire(self,MrT_getGunJoint(self),&offset,MrT_getShotDamage(self));
}

static void MrT_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=MrT_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_mrt(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:MrT_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:MrT_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_mrt operation: %u",(unsigned)call->op);
    }
    return 0;
}
