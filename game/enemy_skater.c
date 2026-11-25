/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static vec3i Skater_getMuzzleOffset(const ItemObj *self);
static int32 Skater_getShotDamage(const ItemObj *self);
static int32 Skater_getDeathAnimation(const ItemObj *self);
static int32 Skater_getHitSound(const ItemObj *self);
static void Skater_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Skater_updateHumanState(ItemObj *self);
static void Skater_update(ItemObj *self);
static void Skater_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Skater_updateState(ItemObj *self);
static void Skater_updateBase(ItemObj *self);



static vec3i Skater_getMuzzleOffset(const ItemObj *self)
{
    (void)self;
        return vec3i_create(0, -32, 0);
    }

static int32 Skater_getShotDamage(const ItemObj *self)
{
        return self->state == Skater_STATE_STAND_FIRE ? Skater_DAMAGE_STAND : Skater_DAMAGE_MOVE;
    }

static int32 Skater_getDeathAnimation(const ItemObj *self)
{
    (void)self;
        return Skater_ANIM_DEATH;
    }

static int32 Skater_getHitSound(const ItemObj *self)
{
    (void)self;
        return SND_HIT_SKATER;
    }

static void Skater_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
        if (self->health <= 0)
            return;
        bool damageTrack = self->health >= 120;
        Skater_humanHit(self, damage, point, soundId);
        if (damageTrack && self->health < 120)
            game_sndPlayTrack(Skater_TRACK_DAMAGE);
        if (self->health <= 0)
            ItemObj_add(ITEM_UZIS, self->room, &(self->pos), 0);
    }

static int32 Skater_updateHumanState(ItemObj *self)
{
        int16 * fired = &(self->turnSpeed);
        switch (self->state)
        {
            case Skater_STATE_STOP:
                (*fired) = 0;
                return Enemy_targetIsVisible(self, Human_DIST_SHOT) ?
                    Skater_STATE_STAND_FIRE : Skater_STATE_MOVE;
            case Skater_STATE_MOVE:
                (*fired) = 0;
                if (rand_logic() < 512)
                    return Skater_STATE_STEP;
                if (Enemy_targetIsVisible(self, Human_DIST_SHOT))
                    return self->input != MOOD_ESCAPE &&
                           gs->tinfo.dist > Skater_DIST_MIN && gs->tinfo.dist < Skater_DIST_MAX ?
                        Skater_STATE_STOP : Skater_STATE_MOVE_FIRE;
                break;
            case Skater_STATE_STEP:
                if (rand_logic() < 1024)
                    return Skater_STATE_MOVE;
                break;
            case Skater_STATE_STAND_FIRE:
            case Skater_STATE_MOVE_FIRE:
                if (!(*fired) && Enemy_targetIsVisible(self, Human_DIST_SHOT))
                {
                    int32 damage = Skater_getShotDamage(self);
                    vec3i offset = Skater_getMuzzleOffset(self);
                    Enemy_humanFire(self, Skater_JOINT_GUN_LEFT, &offset, damage);
                    Enemy_humanFire(self, Skater_JOINT_GUN_RIGHT, &offset, damage);
                    (*fired) = 1;
                }
                if (self->input == MOOD_ESCAPE || gs->tinfo.dist < 1024)
                    return Skater_STATE_STEP;
                break;
        }
        return self->goalState;
    }

static void Skater_update(ItemObj *self)
{
        ItemObj * board = Skater_ensureBoard(self);
        Skater_updateBase(self);
        Skater_syncBoard(self, board);
    }


static void Skater_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, Skater_getHitSound(self));
        if (self->health <= 0)
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Skater_getDeathAnimation(self)), true, 0);
    }

static int32 Skater_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            if (self->state != Human_STATE_DEATH)
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Skater_getDeathAnimation(self)), true, 0);
            return self->goalState;
        }
        (self->extraE)->maxTurn =
            self->state == Human_STATE_RUN ? Human_TURN_RUN :
            self->state == Human_STATE_WALK ? Human_TURN_WALK : 0;
        return Skater_updateHumanState(self);
    }


static void Skater_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Skater_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_skater(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Skater_activate(self);break;
    case GAME_ITEM_UPDATE:Skater_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Skater_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_skater operation: %u",(unsigned)call->op);
    }
    return 0;
}
