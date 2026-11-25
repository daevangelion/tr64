/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static uint16 Natla_getClock(const ItemObj *self);
static bool Natla_isFlying(const ItemObj *self);
static void Natla_setClock(ItemObj *self, uint16 value);
static void Natla_tickClock(ItemObj *self);
static bool Natla_isFainting(const ItemObj *self);
static void Natla_setFainting(ItemObj *self, bool fainting);
static void Natla_setFlying(ItemObj *self, bool flying);
static void Natla_fireSmallSpread(ItemObj *self, ItemType bulletType);
static void Natla_fireWideShot(ItemObj *self);
static int32 Natla_updatePhase1(ItemObj *self);
static int32 Natla_updatePhase2(ItemObj *self);
static int32 Natla_updateDeath(ItemObj *self);
static int32 Natla_updateState(ItemObj *self);
static void Natla_updateBase(ItemObj *self);

static uint16 Natla_getClock(const ItemObj *self)
{
        return ((uint16)(self->turnSpeed)) & Natla_CLOCK_MASK;
    }

static bool Natla_isFlying(const ItemObj *self)
{
        return (((uint16)(self->turnSpeed)) & Natla_CLOCK_FLY) != 0;
    }

static void Natla_setClock(ItemObj *self, uint16 value)
{
        self->turnSpeed = wrap_i16(((uint16)self->turnSpeed &
                           (Natla_CLOCK_FLY | Natla_CLOCK_FAINT)) |
                          X_MIN(value, (uint16)Natla_CLOCK_MASK));
    }

static void Natla_tickClock(ItemObj *self)
{
        Natla_setClock(self, i32_add(Natla_getClock(self), 1));
    }

static bool Natla_isFainting(const ItemObj *self)
{
        return (((uint16)(self->turnSpeed)) & Natla_CLOCK_FAINT) != 0;
    }

static void Natla_setFainting(ItemObj *self, bool fainting)
{
        uint16 value = Natla_getClock(self) |
            (Natla_isFlying(self) ? Natla_CLOCK_FLY : 0) |
            (fainting ? Natla_CLOCK_FAINT : 0);
        self->turnSpeed = wrap_i16(value);
    }

static void Natla_setFlying(ItemObj *self, bool flying)
{
        uint16 value = Natla_getClock(self) |
            (Natla_isFainting(self) ? Natla_CLOCK_FAINT : 0) |
            (flying ? Natla_CLOCK_FLY : 0);
        self->turnSpeed = wrap_i16(value);
        if (!self->extraE)
            return;
        int32 step = flying ? (20 * 1024) : 256;
        int32 drop = flying ? i32_mul(-20, 1024) : -256;
        int32 lift = flying ? Natla_LIFT_SPEED : 0;
        if (((self->extraE)->nav).stepHeight == step &&
            ((self->extraE)->nav).dropHeight == drop &&
            ((self->extraE)->nav).vSpeed == lift)
            return;
        ((self->extraE)->nav).stepHeight = step;
        ((self->extraE)->nav).dropHeight = drop;
        ((self->extraE)->nav).vSpeed = lift;
        uint16 boxIndex = (Room_getSector(self->room, self->pos.x, self->pos.z))->boxIndex;
        if (boxIndex != 0xFFFF)
            Nav_init(&((self->extraE)->nav), boxIndex);
    }

static void Natla_fireSmallSpread(ItemObj *self, ItemType bulletType)
{
        soundPlay(SND_NATLA_SHOT, &self->pos);
        Enemy_shotSpread(self, bulletType, Natla_JOINT_GUN, &(vec3i){5, 220, 7}, Natla_SPREAD_SMALL, Natla_SPREAD_SMALL);
    }

static void Natla_fireWideShot(ItemObj *self)
{
        int16 spreadY = ((int16)(i32_mul((i32_sub(((int32)(rand_logic())), 0x4000)), Natla_SPREAD_WIDE) >> 14));
        Enemy_shot(self, ITEM_CENTAUR_BULLET, Natla_JOINT_GUN, &(vec3i){5, 220, 7}, 0, spreadY);
    }

static int32 Natla_updatePhase1(ItemObj *self)
{
        if (self->state == Natla_STATE_FLY && !Natla_isFlying(self) && Natla_getClock(self) == 0 &&
            ((self->extraE)->nav).zoneType == ZONE_FLY)
            Natla_setFlying(self, true);
        Natla_tickClock(self);
        bool canShot = gs->tinfo.target && (gs->tinfo.target)->health > 0 &&
                       Enemy_targetIsVisible(self, Natla_DIST_SHOT);
        if (canShot && self->state == Natla_STATE_FLY && Natla_isFlying(self) &&
            rand_logic() < 256)
            Natla_setFlying(self, false);
        else if (!canShot)
            Natla_setFlying(self, true);
        if (Natla_isFlying(self) && gs->tinfo.target)
            (gs->tinfo.waypoint).y = ((gs->tinfo.target)->pos).y;
        if (self->state == self->nextState)
            self->nextState = Natla_STATE_NONE;
        switch (self->state)
        {
            case Natla_STATE_STOP:
                Natla_setClock(self, 0);
                return Natla_isFlying(self) ? Natla_STATE_FLY : Natla_STATE_AIM;
            case Natla_STATE_FLY:
                if (Natla_getClock(self) >= Natla_FLY_SHOT_FRAMES)
                {
                    Natla_fireSmallSpread(self, ITEM_CENTAUR_BULLET);
                    Natla_setClock(self, i32_sub(Natla_getClock(self), Natla_FLY_SHOT_FRAMES));
                }
                if (!Natla_isFlying(self) && self->pos.y >= self->roomFloor)
                {
                    self->pos.y = self->roomFloor;
                    return Natla_STATE_STOP;
                }
                break;
            case Natla_STATE_AIM:
                if (self->nextState != Natla_STATE_NONE)
                    return self->nextState;
                return canShot ? Natla_STATE_FIRE : Natla_STATE_STOP;
            case Natla_STATE_FIRE:
                if (self->nextState != Natla_STATE_NONE)
                    return self->state;
                Natla_fireSmallSpread(self, ITEM_CENTAUR_BULLET);
                Natla_fireWideShot(self);
                Natla_fireWideShot(self);
                self->nextState = Natla_STATE_STOP;
                break;
        }
        return self->goalState;
    }

static int32 Natla_updatePhase2(ItemObj *self)
{
        Natla_setFlying(self, false);
        Natla_tickClock(self);
        bool canShot = gs->tinfo.target && (gs->tinfo.target)->health > 0 &&
                       abs(gs->tinfo.angle) < Natla_FIRE_ANGLE &&
                       Enemy_targetIsVisible(self, Natla_DIST_SHOT);
        switch (self->state)
        {
            case Natla_STATE_RUN:
            case Natla_STATE_STAND:
                if (Natla_getClock(self) >= Natla_GROUND_SHOT_FRAMES)
                {
                    Natla_fireSmallSpread(self, ITEM_MUTANT_BULLET);
                    Natla_setClock(self, i32_sub(Natla_getClock(self), Natla_GROUND_SHOT_FRAMES));
                }
                return canShot ? Natla_STATE_STAND : Natla_STATE_RUN;
            case Natla_STATE_FLY:
                self->health = Natla_FAINT_HEALTH;
                Natla_setClock(self, 0);
                Natla_setFainting(self, true);
                self->flags |= ((1 << 1));
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Natla_ANIM_FALL), true, 0);
                return Natla_STATE_FALL;
            case Natla_STATE_STOP:
            case Natla_STATE_AIM:
            case Natla_STATE_FIRE:
                self->health = Natla_FAINT_HEALTH;
                Natla_setClock(self, 0);
                Natla_setFainting(self, true);
                self->flags &= ~((1 << 1));
                ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Natla_ANIM_FAINT), true, 0);
                return Natla_STATE_FAINT;
        }
        return self->goalState;
    }

static int32 Natla_updateDeath(ItemObj *self)
{
        Natla_setFlying(self, false);
        if (Natla_isFainting(self))
        {
            if (self->state == Natla_STATE_FALL)
            {
                self->flags |= ((1 << 1));
                ((self->extraE)->nav).stepHeight = (20 * 1024);
                ((self->extraE)->nav).dropHeight = i32_mul(-20, 1024);
                ((self->extraE)->nav).vSpeed = 0;
                if (self->pos.y >= self->roomFloor)
                {
                    self->pos.y = self->roomFloor;
                    self->vSpeed = 0;
                    self->flags &= ~((1 << 1));
                    Natla_setClock(self, 0);
                    ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Natla_ANIM_FAINT), true, 0);
                    return Natla_STATE_FAINT;
                }
                return self->state;
            }
            if (self->state == Natla_STATE_FAINT)
            {
                self->flags &= ~((1 << 1));
                Natla_tickClock(self);
                if (Natla_getClock(self) >= Natla_FAINT_FRAMES)
                {
                    self->health = Natla_HALF_HEALTH;
                    Natla_setClock(self, 0);
                    Natla_setFainting(self, false);
                    ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Natla_ANIM_STAND_GETUP), true, 0);
                    game_sndPlayTrack(54);
                    return Natla_STATE_STAND;
                }
                return self->state;
            }
            return self->goalState == Natla_STATE_FALL || self->goalState == Natla_STATE_FAINT ?
                self->goalState : Natla_STATE_FAINT;
        }
        self->flags &= ~((1 << 1));
        int32 deathAnim = i32_add((gs->level.models[self->type]).animIndex, Natla_ANIM_DEATH);
        if (self->animIndex != deathAnim)
            ItemObj_animSet(self, deathAnim, true, 0);
        return Natla_STATE_DEATH;
    }

static int32 Natla_updateState(ItemObj *self)
{
        (self->extraE)->maxTurn = self->state == Natla_STATE_RUN ? (i32_mul((6), (0x10000 / 360))) : (i32_mul((3), (0x10000 / 360)));
        if (self->health <= 0)
            return Natla_updateDeath(self);
        return self->health > Natla_HALF_HEALTH ? Natla_updatePhase1(self) : Natla_updatePhase2(self);
    }

static void Natla_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Natla_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_natla(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Natla_updateBase(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Enemy_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_natla operation: %u",(unsigned)call->op);
    }
    return 0;
}
