/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

static int32 Mutant_getMode(const ItemObj *self);
static uint16 Mutant_getControlFlags(const ItemObj *self);
static void Mutant_setControlFlags(ItemObj *self, uint16 value);
static void Mutant_configureNavigation(ItemObj *self, bool fly);
static void Mutant_syncNavigationMode(ItemObj *self);
static uint32 Mutant_getModelMask(const ItemObj *self);
static void Mutant_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
static void Mutant_update(ItemObj *self);
static int32 Mutant_updateAirState(ItemObj *self);
static int32 Mutant_updateState(ItemObj *self);
static void Mutant_updateBase(ItemObj *self);

static int32 Mutant_getMode(const ItemObj *self)
{
        switch (self->type)
        {
            case ITEM_MUTANT_1: return Mutant_MODE_FLY_RANGED;
            case ITEM_MUTANT_2: return Mutant_MODE_GROUND_RANGED;
            case ITEM_MUTANT_3: return Mutant_MODE_GROUND_MELEE;
            default: return Mutant_MODE_GROUND_MELEE;
        }
    }

static uint16 Mutant_getControlFlags(const ItemObj *self)
{
        return ((uint16)(self->turnSpeed));
    }

static void Mutant_setControlFlags(ItemObj *self, uint16 value)
{
        self->turnSpeed = wrap_i16(value);
    }

static void Mutant_configureNavigation(ItemObj *self, bool fly)
{
        if (fly)
        {
            ((self->extraE)->nav).stepHeight = (30 * 1024);
            ((self->extraE)->nav).dropHeight = i32_mul(-30, 1024);
            ((self->extraE)->nav).vSpeed = Mutant_LIFT_SPEED;
        } else {
            ((self->extraE)->nav).stepHeight = 256;
            ((self->extraE)->nav).dropHeight = -256;
            ((self->extraE)->nav).vSpeed = 0;
        }
    }

static void Mutant_syncNavigationMode(ItemObj *self)
{
        if (!self->extraE)
            return;
        bool fly = self->state == Mutant_STATE_FLY;
        uint32 zoneType = fly ? ZONE_FLY : ZONE_GROUND_1;
        bool configured = ((self->extraE)->nav).zoneType == zoneType &&
            ((self->extraE)->nav).stepHeight == (fly ? (30 * 1024) : 256) &&
            ((self->extraE)->nav).dropHeight == (fly ? i32_mul(-30, 1024) : -256) &&
            ((self->extraE)->nav).vSpeed == (fly ? Mutant_LIFT_SPEED : 0);
        if (configured)
            return;
        Mutant_configureNavigation(self, fly);
        uint16 boxIndex = (Room_getSector(self->room, self->pos.x, self->pos.z))->boxIndex;
        if (boxIndex != 0xFFFF)
            Nav_init(&((self->extraE)->nav), boxIndex);
    }

static uint32 Mutant_getModelMask(const ItemObj *self)
{
        int32 jointCount = (gs->level.models[self->type]).count;
        if (jointCount <= 0)
            return 0;
        return jointCount >= 32 ? 0xFFFFFFFF :
               ((1u << jointCount) - 1);
    }

static void Mutant_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
    (void)soundId;
        Enemy_hit(self, damage, point, 0);
    }

static void Mutant_update(ItemObj *self)
{
        bool deathStarted = (Mutant_getControlFlags(self) & Mutant_FLAG_DEATH_STARTED) != 0;
        if (self->health <= 0)
        {
            if (!deathStarted)
            {
                soundPlay(SND_MUTANT_DEATH, &self->pos);
                Mutant_setControlFlags(self, Mutant_getControlFlags(self) | Mutant_FLAG_DEATH_STARTED);
            }
            self->hSpeed = 0;
            Enemy_explode(self, 0xFFFFFFFF);
        } else {
            Mutant_syncNavigationMode(self);
        }
        Mutant_updateBase(self);
        if (deathStarted && self->health <= 0 &&
            !(self->visibleMask & Mutant_getModelMask(self)))
        {
            self->flags &= ~(((3 << 3)) | ((1 << 2)) |
                       ((1 << 5)));
            self->flags |= ((3 << 3));
            ItemObj_deactivate(self);
            if (self->extraE)
                Enemy_disable(self);
        }
    }

static int32 Mutant_updateAirState(ItemObj *self)
{
        (self->extraE)->maxTurn = Mutant_TURN_FAST;
        if (gs->tinfo.target)
            (gs->tinfo.waypoint).y = ((gs->tinfo.target)->pos).y;
        uint16 controlFlags = Mutant_getControlFlags(self);
        if ((controlFlags & Mutant_FLAG_FLY) &&
            self->input != MOOD_ESCAPE && Enemy_checkZone(self))
        {
            controlFlags &= ~Mutant_FLAG_FLY;
            Mutant_setControlFlags(self, controlFlags);
        }
        if (!(controlFlags & Mutant_FLAG_FLY))
        {
            const Sector * sector = Room_getSector(self->room, self->pos.x, self->pos.z);
            int32 floor = i32_sub(Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z), 128);
            if (self->pos.y >= floor)
                return Mutant_STATE_STOP;
        }
        return Mutant_STATE_FLY;
    }

static int32 Mutant_updateState(ItemObj *self)
{
        if (self->health <= 0)
            return self->goalState;
        if (self->state == Mutant_STATE_FLY)
            return Mutant_updateAirState(self);
        (self->extraE)->maxTurn =
            self->state == Mutant_STATE_RUN ? Mutant_TURN_FAST :
            self->state == Mutant_STATE_WALK ? Mutant_TURN_SLOW : 0;
        if (self->nextState == self->state)
            self->nextState = Mutant_STATE_NONE;
        uint16 controlFlags = Mutant_getControlFlags(self);
        if (Mutant_getMode(self) != Mutant_MODE_GROUND_MELEE)
        {
            if (controlFlags & (Mutant_FLAG_BULLET_1 | Mutant_FLAG_BULLET_2))
            {
                if (gs->tinfo.angle > (0x10000 / 8))
                    controlFlags &= ~(Mutant_FLAG_BULLET_1 | Mutant_FLAG_BULLET_2);
            } else if (gs->tinfo.angle < (0x10000 / 8) && self->state != Mutant_STATE_FIRE &&
                       (gs->tinfo.dist > Mutant_DIST_SHOT || !Enemy_checkZone(self)) &&
                       Enemy_targetIsVisible(self, Mutant_MAX_SHOT_DIST))
            {
                controlFlags |= (rand_logic() & 1) ?
                    Mutant_FLAG_BULLET_1 : Mutant_FLAG_BULLET_2;
            }
        }
        if (Mutant_getMode(self) == Mutant_MODE_FLY_RANGED &&
            (self->input == MOOD_ESCAPE ||
             (!Enemy_checkZone(self) &&
              !(controlFlags & (Mutant_FLAG_BULLET_1 | Mutant_FLAG_BULLET_2)))))
        {
            controlFlags |= Mutant_FLAG_FLY;
        }
        Mutant_setControlFlags(self, controlFlags);
        switch (self->state)
        {
            case Mutant_STATE_STOP:
                if (controlFlags & Mutant_FLAG_FLY)
                    return Mutant_STATE_FLY;
                if ((gs->tinfo.canAttack && gs->tinfo.dist < Mutant_DIST_ATTACK_3) ||
                    (self->hitMask & Mutant_HIT_MASK))
                    return Mutant_STATE_ATTACK_3;
                if (gs->tinfo.canAttack && gs->tinfo.dist < Mutant_DIST_ATTACK_1)
                    return Mutant_STATE_ATTACK_1;
                if (controlFlags & Mutant_FLAG_BULLET_1)
                    return Mutant_STATE_AIM_1;
                if (controlFlags & Mutant_FLAG_BULLET_2)
                    return Mutant_STATE_AIM_2;
                if (self->input == MOOD_SLEEP ||
                    (self->input == MOOD_STALK && gs->tinfo.dist < Mutant_DIST_STALK))
                    return Mutant_STATE_LOOKING;
                return Mutant_STATE_RUN;
            case Mutant_STATE_WALK:
                if (controlFlags)
                    return Mutant_STATE_STOP;
                if (self->input == MOOD_ATTACK || self->input == MOOD_ESCAPE)
                    return Mutant_STATE_STOP;
                if (self->input == MOOD_SLEEP ||
                    (self->input == MOOD_STALK && !Enemy_checkZone(self)))
                {
                    if (rand_logic() < 50)
                        return Mutant_STATE_LOOKING;
                } else if (self->input == MOOD_STALK &&
                           gs->tinfo.dist > Mutant_DIST_STALK) {
                    return Mutant_STATE_STOP;
                }
                break;
            case Mutant_STATE_RUN:
                if (controlFlags & Mutant_FLAG_FLY)
                    return Mutant_STATE_STOP;
                if (self->hitMask & Mutant_HIT_MASK)
                    return Mutant_STATE_STOP;
                if (gs->tinfo.canAttack && gs->tinfo.dist < Mutant_DIST_ATTACK_1)
                    return Mutant_STATE_STOP;
                if (gs->tinfo.front && gs->tinfo.dist < Mutant_DIST_ATTACK_2)
                    return Mutant_STATE_ATTACK_2;
                if (controlFlags & (Mutant_FLAG_BULLET_1 | Mutant_FLAG_BULLET_2))
                    return Mutant_STATE_STOP;
                if (self->input == MOOD_SLEEP ||
                    (self->input == MOOD_STALK && gs->tinfo.dist < Mutant_DIST_STALK))
                    return Mutant_STATE_STOP;
                break;
            case Mutant_STATE_LOOKING:
                if (controlFlags)
                    return Mutant_STATE_STOP;
                switch (self->input)
                {
                    case MOOD_SLEEP:
                        if (rand_logic() < 256)
                            return Mutant_STATE_WALK;
                        break;
                    case MOOD_STALK:
                        if (gs->tinfo.dist < Mutant_DIST_STALK)
                        {
                            if (Enemy_checkZone(self) && rand_logic() < 256)
                                return Mutant_STATE_WALK;
                        } else {
                            return Mutant_STATE_STOP;
                        }
                        break;
                    case MOOD_ATTACK:
                    case MOOD_ESCAPE:
                        return Mutant_STATE_STOP;
                }
                break;
            case Mutant_STATE_ATTACK_1:
            case Mutant_STATE_ATTACK_2:
            case Mutant_STATE_ATTACK_3:
                if (self->nextState == Mutant_STATE_NONE && (self->hitMask & Mutant_HIT_MASK))
                {
                    int32 damage = self->state == Mutant_STATE_ATTACK_1 ? Mutant_DAMAGE_ATTACK_1 :
                        self->state == Mutant_STATE_ATTACK_2 ? Mutant_DAMAGE_ATTACK_2 :
                                                  Mutant_DAMAGE_ATTACK_3;
                    Enemy_bite(self, damage, &(vec3i){-27, 98, 0}, Mutant_JOINT_ATTACK);
                    self->nextState = Mutant_STATE_STOP;
                }
                break;
            case Mutant_STATE_AIM_1:
                return (controlFlags & Mutant_FLAG_BULLET_1) ?
                    Mutant_STATE_FIRE : Mutant_STATE_STOP;
            case Mutant_STATE_AIM_2:
                return (controlFlags & Mutant_FLAG_BULLET_2) ?
                    Mutant_STATE_FIRE : Mutant_STATE_STOP;
            case Mutant_STATE_FIRE:
                if (controlFlags & Mutant_FLAG_BULLET_1)
                    Enemy_shotSpread(self, ITEM_MUTANT_BULLET, Mutant_JOINT_BULLET_1, &(vec3i){-35, 269, 0}, (i32_mul((3), (0x10000 / 360))) / 2, (i32_mul((3), (0x10000 / 360))) / 2);
                if (controlFlags & Mutant_FLAG_BULLET_2)
                    Enemy_shotSpread(self, ITEM_CENTAUR_BULLET, Mutant_JOINT_BULLET_2, &(vec3i){51, 213, 0}, (i32_mul((3), (0x10000 / 360))) / 2, (i32_mul((3), (0x10000 / 360))) / 2);
                controlFlags &=
                    ~(Mutant_FLAG_BULLET_1 | Mutant_FLAG_BULLET_2);
                Mutant_setControlFlags(self, controlFlags);
                break;
            case Mutant_STATE_IDLE:
                return Mutant_STATE_STOP;
            default:
                break;
        }
        return self->goalState;
    }

static void Mutant_updateBase(ItemObj *self)
{
    if (!Enemy_updateBegin(self))return;
    self->goalState=Mutant_updateState(self);
    Enemy_updateEnd(self);
}

uint32 tr64_unit_enemy_mutant(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Mutant_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Mutant_hit(self,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_mutant operation: %u",(unsigned)call->op);
    }
    return 0;
}
