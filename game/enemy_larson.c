/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static int32 Larson_getGunJoint(const ItemObj *self);
static vec3i Larson_getMuzzleOffset(const ItemObj *self);
static int32 Larson_getShotDamage(const ItemObj *self);
static int32 Larson_getDeathAnimation(const ItemObj *self);
static int32 Larson_getHitSound(const ItemObj *self);
static int32 Larson_updateHumanState(ItemObj *self);
static void Larson_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Larson_updateState(ItemObj *self);
static bool Larson_fire(ItemObj *self);
static void Larson_updateBase(ItemObj *self);

static int32 Larson_getGunJoint(const ItemObj *self)
{
    (void)self;
        return Larson_JOINT_GUN;
    }

static vec3i Larson_getMuzzleOffset(const ItemObj *self)
{
    (void)self;
        return vec3i_create(-50, 0, 20);
    }

static int32 Larson_getShotDamage(const ItemObj *self)
{
    (void)self;
        return Larson_DAMAGE;
    }

static int32 Larson_getDeathAnimation(const ItemObj *self)
{
    (void)self;
        return Larson_ANIM_DEATH;
    }

static int32 Larson_getHitSound(const ItemObj *self)
{
    (void)self;
        return 0;
    }

static int32 Larson_updateHumanState(ItemObj *self)
{
        if (self->nextState == self->state)
            self->nextState = Human_STATE_NONE;
        switch (self->state)
        {
            case Human_STATE_STOP:
                if (self->nextState != Human_STATE_NONE)
                    return self->nextState;
                if (self->input == MOOD_SLEEP)
                    return Enemy_humanWaitSelected(self) ?
                        Human_STATE_WAIT : Human_STATE_WALK;
                if (self->input == MOOD_ESCAPE)
                    return Human_STATE_RUN;
                return Human_STATE_WALK;
            case Human_STATE_WAIT:
                if (self->input != MOOD_SLEEP)
                    return Human_STATE_STOP;
                if (Enemy_humanWaitSelected(self))
                {
                    self->nextState = Human_STATE_WALK;
                    return Human_STATE_STOP;
                }
                break;
            case Human_STATE_WALK:
                if (self->input == MOOD_SLEEP &&
                    Enemy_humanWaitSelected(self))
                    self->nextState = Human_STATE_WAIT;
                else if (self->input == MOOD_ESCAPE)
                    self->nextState = Human_STATE_RUN;
                else if (Enemy_targetIsVisible(self, Human_DIST_SHOT))
                    self->nextState = Human_STATE_AIM;
                else if (!gs->tinfo.front || gs->tinfo.dist > Human_DIST_WALK)
                    self->nextState = Human_STATE_RUN;
                else
                    break;
                return Human_STATE_STOP;
            case Human_STATE_RUN:
                if (self->input == MOOD_SLEEP &&
                    Enemy_humanWaitSelected(self))
                    self->nextState = Human_STATE_WAIT;
                else if (Enemy_targetIsVisible(self, Human_DIST_SHOT))
                    self->nextState = Human_STATE_AIM;
                else if (gs->tinfo.front && gs->tinfo.dist < Human_DIST_WALK)
                    self->nextState = Human_STATE_WALK;
                else
                    break;
                return Human_STATE_STOP;
            case Human_STATE_AIM:
                if (self->nextState != Human_STATE_NONE)
                    return self->nextState;
                return Enemy_targetIsVisible(self, Human_DIST_SHOT) ?
                    Human_STATE_FIRE : Human_STATE_STOP;
            case Human_STATE_FIRE:
                if (self->nextState == Human_STATE_NONE)
                {
                    Larson_fire(self);
                    self->nextState = Human_STATE_AIM;
                }
                if (self->input == MOOD_ESCAPE ||
                    !gs->tinfo.target || (gs->tinfo.target)->health <= 0)
                    self->nextState = Human_STATE_STOP;
                break;
        }
        return self->goalState;
    }

static void Larson_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, Larson_getHitSound(self));
        if (self->health <= 0)
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Larson_getDeathAnimation(self)), true, 0);
    }

static int32 Larson_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            if (self->state != Human_STATE_DEATH)
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Larson_getDeathAnimation(self)), true, 0);
            return self->goalState;
        }
        (self->extraE)->maxTurn =
            self->state == Human_STATE_RUN ? Human_TURN_RUN :
            self->state == Human_STATE_WALK ? Human_TURN_WALK : 0;
        return Larson_updateHumanState(self);
    }

static bool Larson_fire(ItemObj *self)
{
    vec3i offset=Larson_getMuzzleOffset(self);
    return Enemy_humanFire(self,Larson_getGunJoint(self),&offset,Larson_getShotDamage(self));
}

static void Larson_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Larson_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_larson(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Larson_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Larson_humanHit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_larson operation: %u",(unsigned)call->op);
    }
    return 0;
}
