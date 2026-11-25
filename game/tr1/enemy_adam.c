/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static void Adam_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static void Adam_update(ItemObj *self);
static int32 Adam_updateState(ItemObj *self);
static void Adam_updateBase(ItemObj *self);

static void Adam_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, SND_HIT_ADAM);
    }

static void Adam_update(ItemObj *self)
{
        if (self->health == -0x4000 &&
            (self->flags & ((3 << 3))) == ((3 << 3)))
            return;
        int32 jointCount = (gs->level.models[self->type]).count;
        uint32 modelMask = jointCount >= 32 ?
            0xFFFFFFFF : (jointCount > 0 ?
                (1u << jointCount) - 1 : 0);
        int32 deathAnim = i32_add((gs->level.models[self->type]).animIndex, Adam_ANIM_DEATH);
        bool disintegrating = (self->visibleMask & modelMask) != modelMask;
        if (self->health <= 0 &&
            (disintegrating ||
             (self->animIndex == deathAnim && ItemObj_animIsEnd(self, 0))))
        {
            if (!disintegrating)
            {
                soundPlay(SND_MUTANT_DEATH, &self->pos);
                game_check_trigger(gs->gLastFloorData, NULL);
                if (gs->stop.kind != TR64_GAME_STOP_NONE)
                    return;
            }
            Enemy_explode(self, modelMask);
            if (!(self->visibleMask & modelMask))
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
        Adam_updateBase(self);
    }

static int32 Adam_updateState(ItemObj *self)
{
        if (self->health <= 0)
        {
            int32 deathAnim = i32_add((gs->level.models[self->type]).animIndex, Adam_ANIM_DEATH);
            if (self->animIndex != deathAnim)
                ItemObj_animSet(self, deathAnim, true, 0);
            return self->goalState;
        }
        if (self->state == Adam_STATE_BORN || self->state == Adam_STATE_FALL)
        {
            self->flags |= ((1 << 1));
            if (self->extraE)
            {
                ((self->extraE)->nav).stepHeight = (20 * 1024);
                ((self->extraE)->nav).dropHeight = i32_mul(-20, 1024);
            }
            if (self->state == Adam_STATE_BORN)
                return Adam_STATE_FALL;
            if (self->pos.y >= self->roomFloor)
            {
                self->pos.y = self->roomFloor;
                self->vSpeed = 0;
                self->flags &= ~((1 << 1));
                if (self->extraE)
                {
                    ((self->extraE)->nav).stepHeight = 256;
                    ((self->extraE)->nav).dropHeight = -256;
                }
                return Adam_STATE_STOP;
            }
            return Adam_STATE_FALL;
        }
        bool turnToTarget = gs->tinfo.target && (gs->tinfo.target)->health > 0 &&
            abs(gs->tinfo.angle) > (i32_mul((10), (0x10000 / 360))) &&
            (self->state == Adam_STATE_STOP ||
             self->state == Adam_STATE_TURN_LEFT ||
             self->state == Adam_STATE_TURN_RIGHT ||
             self->state == Adam_STATE_WALK);
        if (turnToTarget)
        {
            (self->extraE)->maxTurn = (i32_mul((3), (0x10000 / 360)));
            gs->tinfo.turn = ((gs->tinfo.angle) < (-(i32_mul((3), (0x10000 / 360)))) ? (-(i32_mul((3), (0x10000 / 360)))) : (X_MIN((gs->tinfo.angle), ((i32_mul((3), (0x10000 / 360)))))));
            gs->tinfo.tilt = gs->tinfo.turn >> 2;
        } else {
            (self->extraE)->maxTurn = 0;
            gs->tinfo.turn = 0;
        }
        if (!gs->tinfo.target || (gs->tinfo.target)->health <= 0)
            return Adam_STATE_STOP;
        if (self->hitMask)
            ItemObj_hit(gs->tinfo.target, Adam_DAMAGE_WALK, &(self->pos), 0);
        switch (self->state)
        {
            case Adam_STATE_STOP:
                self->nextState = Adam_STATE_NONE;
                if (gs->tinfo.angle > (0x10000 / 8))
                    return Adam_STATE_TURN_RIGHT;
                if (gs->tinfo.angle < -(0x10000 / 8))
                    return Adam_STATE_TURN_LEFT;
                if (gs->tinfo.dist < Adam_DIST_ATTACK)
                {
                    if ((gs->tinfo.target)->health <= Adam_DAMAGE_ATTACK)
                    {
                        if (gs->tinfo.dist < Adam_DIST_FATAL)
                            return Adam_STATE_ATTACK_3;
                    } else {
                        return (rand_logic() % 2) ?
                            Adam_STATE_ATTACK_1 : Adam_STATE_ATTACK_2;
                    }
                }
                return Adam_STATE_WALK;
            case Adam_STATE_WALK:
                if (gs->tinfo.dist < Adam_DIST_ATTACK ||
                    gs->tinfo.angle > (0x10000 / 8) ||
                    gs->tinfo.angle < -(0x10000 / 8))
                    return Adam_STATE_STOP;
                break;
            case Adam_STATE_TURN_RIGHT:
                if (gs->tinfo.angle < (0x10000 / 8))
                    return Adam_STATE_STOP;
                break;
            case Adam_STATE_TURN_LEFT:
                if (gs->tinfo.angle > -(0x10000 / 8))
                    return Adam_STATE_STOP;
                break;
            case Adam_STATE_ATTACK_1:
            case Adam_STATE_ATTACK_2:
                if (!self->nextState &&
                    ((self->state == Adam_STATE_ATTACK_1 &&
                      (self->hitMask & Adam_HIT_MASK_HAND)) ||
                     (self->state == Adam_STATE_ATTACK_2 &&
                      (self->hitMask & Adam_HIT_MASK_HANDS))))
                {
                    ItemObj_hit(gs->tinfo.target, Adam_DAMAGE_ATTACK, &(self->pos), 0);
                    self->nextState = Adam_STATE_STOP;
                }
                break;
            case Adam_STATE_ATTACK_3:
                if (self->hitMask & Adam_HIT_MASK_HAND)
                {
                    Lara * lara = (Lara*)gs->tinfo.target;
                    bool hanging = lara->state == Lara_STATE_HANG ||
                        lara->state == Lara_STATE_HANG_LEFT ||
                        lara->state == Lara_STATE_HANG_RIGHT ||
                        lara->state == Lara_STATE_HANG_UP;
                    if (!hanging)
                    {
                        Lara_hit(lara, Adam_DAMAGE_FATAL, &(self->pos), 0);
                        (lara->pos = self->pos);
                        (lara->angle = self->angle);
                        ItemObj_animSet(lara, (gs->level.models[ITEM_LARA_SPEC]).animIndex, true, 0);
                        if (lara->room != self->room)
                        {
                            Room_remove(lara->room, lara);
                            Room_add(self->room, lara);
                        }
                        return Adam_STATE_FATAL;
                    }
                }
                break;
        }
        return self->goalState;
    }

static void Adam_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Adam_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_adam(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Adam_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Adam_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_adam operation: %u",(unsigned)call->op);
    }
    return 0;
}
