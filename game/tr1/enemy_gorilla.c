/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static void Gorilla_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static void Gorilla_strafe(ItemObj *self, int32 currentState);
static int32 Gorilla_updateState(ItemObj *self);
static bool Gorilla_climb(ItemObj *self, const vec3i * old, int32 oldFloor);
static void Gorilla_update(ItemObj *self);
static void Gorilla_updateBase(ItemObj *self);

static void Gorilla_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, 0);
        if (self->extraE) (self->extraE)->_reserved |= Gorilla_FLAG_ATTACK;
        if (self->health <= 0)
            ItemObj_animSet(self, i32_add(i32_add((gs->level.models[self->type]).animIndex, Gorilla_ANIM_DEATH), (i32_mul(rand_logic(), (2)) >> 15)), true, 0);
    }

static void Gorilla_strafe(ItemObj *self, int32 currentState)
{
        switch (currentState)
        {
            case Gorilla_STATE_STOP:
                if ((self->extraE)->_reserved & Gorilla_FLAG_LEFT) self->angle.y = wrap_i16((uint32)self->angle.y + ANGLE_90);
                if ((self->extraE)->_reserved & Gorilla_FLAG_RIGHT) self->angle.y = wrap_i16((uint32)self->angle.y - ANGLE_90);
                (self->extraE)->_reserved &= ~(Gorilla_FLAG_LEFT | Gorilla_FLAG_RIGHT);
                break;
            case Gorilla_STATE_LEFT:
                if (!((self->extraE)->_reserved & Gorilla_FLAG_LEFT))
                {
                    (self->extraE)->_reserved |= Gorilla_FLAG_LEFT;
                    self->angle.y = wrap_i16((uint32)self->angle.y - ANGLE_90);
                }
                break;
            case Gorilla_STATE_RIGHT:
                if (!((self->extraE)->_reserved & Gorilla_FLAG_RIGHT))
                {
                    (self->extraE)->_reserved |= Gorilla_FLAG_RIGHT;
                    self->angle.y = wrap_i16((uint32)self->angle.y + ANGLE_90);
                }
                break;
        }
    }

static int32 Gorilla_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        Gorilla_strafe(self, self->state);
        if (self->nextState == self->state)
            self->nextState = Gorilla_STATE_NONE;
        if (gs->tinfo.dist < Gorilla_DIST_AGGRESSION)
            (self->extraE)->_reserved |= Gorilla_FLAG_ATTACK;
        switch (self->state)
        {
            case Gorilla_STATE_STOP:
            {
                if (self->nextState)
                    return self->nextState;
                if (gs->tinfo.canAttack && (gs->tinfo.dist < Gorilla_DIST_ATTACK))
                    return Gorilla_STATE_ATTACK;
                if (!((self->extraE)->_reserved & Gorilla_FLAG_ATTACK) &&
                    Enemy_checkZone(self) && gs->tinfo.front)
                {
                    int32 value = rand_logic() & 511;
                    if (value < 120) return Gorilla_STATE_JUMP;
                    if (value < 240) return Gorilla_STATE_IDLE_1;
                    if (value < 360) return Gorilla_STATE_IDLE_2;
                    if (value < 480) return Gorilla_STATE_RUN;
                    return (value & 1) ? Gorilla_STATE_LEFT : Gorilla_STATE_RIGHT;
                }
                return Gorilla_STATE_RUN;
            }
            case Gorilla_STATE_RUN:
            {
                (self->extraE)->maxTurn = Gorilla_TURN_FAST;
                gs->tinfo.tilt = gs->tinfo.turn >> 1;
                if (!((self->extraE)->_reserved & Gorilla_FLAG_AI_MASK) && gs->tinfo.front)
                    return Gorilla_STATE_STOP;
                if (gs->tinfo.front && (self->hitMask & Gorilla_HIT_MASK))
                {
                    self->nextState = Gorilla_STATE_ATTACK;
                    return Gorilla_STATE_STOP;
                }
                if (self->input != MOOD_ESCAPE)
                {
                    int32 value = rand_logic();
                    if (value < 160)
                        self->nextState = Gorilla_STATE_JUMP;
                    else if (value < 320)
                        self->nextState = Gorilla_STATE_IDLE_1;
                    else if (value < 480)
                        self->nextState = Gorilla_STATE_IDLE_2;
                    else
                        break;
                    return Gorilla_STATE_STOP;
                }
                break;
            }
            case Gorilla_STATE_ATTACK:
                if (!self->nextState && (self->hitMask & Gorilla_HIT_MASK))
                {
                    Enemy_bite(self, Gorilla_DAMAGE_ATTACK, &(vec3i){0, -19, 75}, 15);
                    self->nextState = Gorilla_STATE_STOP;
                }
                break;
            case Gorilla_STATE_LEFT:
            case Gorilla_STATE_RIGHT:
                return Gorilla_STATE_STOP;
        }
        return self->goalState;
    }

static bool Gorilla_climb(ItemObj *self, const vec3i * old, int32 oldFloor)
{
        int32 ox = old->x / 1024;
        int32 oz = old->z / 1024;
        int32 cx = self->pos.x / 1024;
        int32 cz = self->pos.z / 1024;
        if ((ox == cx) == (oz == cz))
            return false;
        Gorilla_strafe(self, Gorilla_STATE_STOP);
        if (oz == cz)
        {
            if (ox < cx)
            {
                self->pos.x = i32_sub(i32_mul(cx, 1024), 75);
                self->angle.y = (0x10000 / 4);
            } else {
                self->pos.x = i32_add(i32_mul(ox, 1024), 75);
                self->angle.y = -(0x10000 / 4);
            }
        } else {
            if (oz < cz)
            {
                self->pos.z = i32_sub(i32_mul(cz, 1024), 75);
                self->angle.y = 0;
            } else {
                self->pos.z = i32_add(i32_mul(oz, 1024), 75);
                self->angle.y = -(0x10000 / 2);
            }
        }
        self->pos.y = oldFloor;
        ItemObj_animSet(self, i32_add((gs->level.models[self->type]).animIndex, Gorilla_ANIM_CLIMB), true, 0);
        return true;
    }

static void Gorilla_update(ItemObj *self)
{
        vec3i old = self->pos;
        const Sector * oldSector = Room_getSector(self->room, (old).x, (old).z);
        int32 oldFloor = Sector_getFloor(oldSector, (old).x, (old).y, (old).z);
        Gorilla_updateBase(self);
        if (self->health <= 0 || self->state == Gorilla_STATE_CLIMB || !self->extraE)
            return;
        const Sector * sector = Room_getSector(self->room, self->pos.x, self->pos.z);
        int32 floor = Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z);
        bool raisedFloor = floor < i32_sub(oldFloor, 384);
        bool raisedTarget = gs->tinfo.target &&
                            ((gs->tinfo.target)->pos).y < i32_sub(oldFloor, 384);
        if (raisedFloor || raisedTarget)
            Gorilla_climb(self, &(old), oldFloor);
    }

static void Gorilla_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Gorilla_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_gorilla(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Gorilla_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Gorilla_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_gorilla operation: %u",(unsigned)call->op);
    }
    return 0;
}
