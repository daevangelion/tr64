/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static int32 Pierre_getDeathAnimation(const ItemObj *self);
static int32 Pierre_getHitSound(const ItemObj *self);
static bool Pierre_isActuallyVisible(ItemObj *self);
static void Pierre_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static void Pierre_update(ItemObj *self);
static int32 Pierre_updateHumanState(ItemObj *self);
static void Pierre_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static int32 Pierre_updateState(ItemObj *self);
static void Pierre_updateBase(ItemObj *self);


static int32 Pierre_getDeathAnimation(const ItemObj *self)
{
    (void)self;
        return Pierre_ANIM_DEATH;
    }

static int32 Pierre_getHitSound(const ItemObj *self)
{
    (void)self;
        return 0;
    }

static bool Pierre_isActuallyVisible(ItemObj *self)
{
        for (unsigned i=0;i<TR64_GAME_PLAYERS;++i)
        {
            Lara * lara = gs->players[i];
            if (!lara)
                continue;
            Location from = ((lara->extraL)->camera).view;
            vec3i targetPos = self->pos;
            ((targetPos).y = i32_sub((targetPos).y, 1024));
            Location to = { self->room, targetPos };
            if ((from).room && trace(&(from), &(to), true))
                return true;
        }
        return false;
    }

static void Pierre_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
        if (self->health <= 0)
            return;
        bool wasAlive = self->health > 0;
        if (!(self->flags & ((1 << 8))) &&
            i32_sub(self->health, damage) <= Pierre_MIN_HEALTH)
        {
            int32 healthBefore = self->health;
            Pierre_hit(self, (X_MAX((i32_sub(self->health, Pierre_MIN_HEALTH)), (0))), point, soundId);
            self->health = Pierre_MIN_HEALTH;
            if (healthBefore > Pierre_MIN_HEALTH)
                self->turnSpeed = 0;
            return;
        }
        Pierre_humanHit(self, damage, point, soundId);
        if (wasAlive && self->health <= 0)
        {
            ItemObj_add(ITEM_MAGNUMS, self->room, &(self->pos), 0);
            ItemObj_add(ITEM_SCION_PICKUP_DROP, self->room, &(self->pos), 0);
            ItemObj_add(ITEM_KEY_ITEM_1, self->room, &(self->pos), 0);
        }
    }

static void Pierre_update(ItemObj *self)
{
        if ((self->flags & ((3 << 3))) == ((3 << 3)) &&
            !(self->flags & ((1 << 2))))
            return;
        Pierre_updateBase(self);
        if ((self->flags & ((3 << 3))) == ((3 << 3)) &&
            self->extraE)
            Enemy_disable(self);
    }

static int32 Pierre_updateHumanState(ItemObj *self)
{
        int16 * retreatTimer = &(self->turnSpeed);
        if (!(self->flags & ((1 << 8))) && self->health <= Pierre_MIN_HEALTH)
        {
            self->health = Pierre_MIN_HEALTH;
            (*retreatTimer)++;
        }
        if ((*retreatTimer) > 0 && Pierre_isActuallyVisible(self))
            (*retreatTimer) = 0;
        if (((tr64_u16(((self->room)->info)->flags)) & 1))
            (*retreatTimer) = i32_add(Pierre_RETREAT_FRAMES, 1);
        if ((*retreatTimer) > Pierre_RETREAT_FRAMES)
        {
            ItemObj_deactivate(self);
            self->flags &= ~((3 << 3));
            self->flags |= ((3 << 3));
            return self->goalState;
        }
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
                    Enemy_humanFire(self, 11, &(vec3i){60, 0, 50}, Pierre_DAMAGE);
                    Enemy_humanFire(self, 14, &(vec3i){-60, 0, 50}, Pierre_DAMAGE);
                    self->nextState = Human_STATE_AIM;
                }
                if (self->input == MOOD_ESCAPE && (rand_logic() % 2))
                    self->nextState = Human_STATE_STOP;
                break;
        }
        return self->goalState;
    }

static void Pierre_humanHit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, Pierre_getHitSound(self));
        if (self->health <= 0)
            ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Pierre_getDeathAnimation(self)), true, 0);
    }

static int32 Pierre_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            if (self->state != Human_STATE_DEATH)
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Pierre_getDeathAnimation(self)), true, 0);
            return self->goalState;
        }
        (self->extraE)->maxTurn =
            self->state == Human_STATE_RUN ? Human_TURN_RUN :
            self->state == Human_STATE_WALK ? Human_TURN_WALK : 0;
        return Pierre_updateHumanState(self);
    }


static void Pierre_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Pierre_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_pierre(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Pierre_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Pierre_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_pierre operation: %u",(unsigned)call->op);
    }
    return 0;
}
