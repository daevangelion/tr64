/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

void Enemy_construct(ItemObj *self)
{
    int32 health,radius=341,headOffset=0,aggression=AGGRESSION_LVL_MAX;
    switch (self->type) {
    case ITEM_DOPPELGANGER: health=LARA_MAX_HEALTH;aggression=0;break;
    case ITEM_WOLF: health=6;headOffset=375;aggression=AGGRESSION_LVL_2;break;
    case ITEM_BEAR: health=20;headOffset=500;aggression=AGGRESSION_LVL_3;break;
    case ITEM_BAT: health=1;radius=102;aggression=AGGRESSION_LVL_1;break;
    case ITEM_CROCODILE_LAND:case ITEM_CROCODILE_WATER:
        health=20;headOffset=600;aggression=AGGRESSION_LVL_2;break;
    case ITEM_LION_MALE:health=30;headOffset=400;break;
    case ITEM_LION_FEMALE:health=25;headOffset=400;aggression=AGGRESSION_LVL_2;break;
    case ITEM_PUMA:health=45;headOffset=400;aggression=AGGRESSION_LVL_2;break;
    case ITEM_GORILLA:health=22;headOffset=250;break;
    case ITEM_RAT_LAND:case ITEM_RAT_WATER:
        health=5;radius=204;headOffset=200;aggression=AGGRESSION_LVL_2;break;
    case ITEM_REX:health=100;headOffset=2000;break;
    case ITEM_RAPTOR:health=20;headOffset=400;aggression=AGGRESSION_LVL_3;break;
    case ITEM_MUTANT_1:case ITEM_MUTANT_2:case ITEM_MUTANT_3:
        health=50;headOffset=150;break;
    case ITEM_CENTAUR:health=120;headOffset=400;break;
    case ITEM_MUMMY:health=18;headOffset=150;aggression=0;break;
    case ITEM_LARSON:health=50;radius=102;break;
    case ITEM_PIERRE:health=70;radius=102;break;
    case ITEM_SKATER:health=125;radius=102;break;
    case ITEM_COWBOY:health=150;radius=102;break;
    case ITEM_MR_T:health=200;radius=102;break;
    case ITEM_NATLA:health=400;radius=204;break;
    case ITEM_ADAM:health=500;headOffset=375;break;
    default:sys_error("game enemy constructor: type=%u",(unsigned)self->type);
    }
    self->flags|=ITEM_FLAG_SHADOW;
    self->angle.y=wrap_i16((uint32)self->angle.y+((rand_logic()-0x4000)>>1));
    self->health=health;
    self->timer=radius>>1;
    self->headOffset=headOffset;
    self->aggression=aggression;
    switch (self->type) {
    case ITEM_WOLF:
        self->frameIndex=tr64_u16(gs->level.anims[self->animIndex].frame_end)-1;
        break;
    case ITEM_BEAR:self->flags|=ITEM_FLAG_ANIMATED;break;
    case ITEM_MUTANT_1:case ITEM_MUTANT_2:case ITEM_MUTANT_3:
        self->turnSpeed=0;
        if (self->type!=ITEM_MUTANT_1) {
            Model *model=gs->level.models+self->type;
            if (model->count<=1) {
                *model=gs->level.models[ITEM_MUTANT_1];
                model->type=self->type;
                ItemObj_animSet(self,model->animIndex,true,0);
                self->nextState=Mutant_STATE_NONE;
            }
            self->visibleMask&=0xffe07fffu;
            self->aggression=AGGRESSION_LVL_2;
        }
        break;
    case ITEM_PIERRE:case ITEM_SKATER:self->turnSpeed=0;break;
    case ITEM_MR_T:self->state=Human_STATE_RUN;break;
    case ITEM_NATLA:self->turnSpeed=wrap_i16(Natla_CLOCK_FLY);break;
    default:break;
    }
}

/* Activation is resident, including its small type-specific nav setup: it
 * may run while another family unit is executing an encounter operation. */
void Enemy_initExtra(ItemObj *self)
{
    ExtraInfoEnemy *extra=self->extraE;
    switch (self->type) {
    case ITEM_WOLF:case ITEM_LION_MALE:case ITEM_LION_FEMALE:case ITEM_PUMA:
        extra->nav.dropHeight=-1024;break;
    case ITEM_BAT:
        extra->nav.stepHeight=20*1024;extra->nav.dropHeight=-20*1024;
        extra->nav.vSpeed=16;extra->maxTurn=ANGLE(20);break;
    case ITEM_CROCODILE_WATER:
        extra->nav.stepHeight=20*1024;extra->nav.dropHeight=-20*1024;
        extra->nav.vSpeed=Crocodile_LIFT_SPEED;break;
    case ITEM_GORILLA:
        extra->nav.stepHeight=1024;extra->nav.dropHeight=-1024;extra->_reserved=0;break;
    case ITEM_RAT_WATER:
        extra->nav.stepHeight=20*1024;extra->nav.dropHeight=-20*1024;break;
    case ITEM_MUTANT_1:case ITEM_MUTANT_2:case ITEM_MUTANT_3:
        if (self->state==Mutant_STATE_FLY) {
            extra->nav.stepHeight=30*1024;extra->nav.dropHeight=-30*1024;
            extra->nav.vSpeed=Mutant_LIFT_SPEED;
        }
        extra->maxTurn=(self->state==Mutant_STATE_FLY || self->state==Mutant_STATE_RUN)
            ?Mutant_TURN_FAST:(self->state==Mutant_STATE_WALK?Mutant_TURN_SLOW:0);
        break;
    case ITEM_NATLA:
        extra->nav.stepHeight=20*1024;extra->nav.dropHeight=-20*1024;
        extra->nav.vSpeed=Natla_LIFT_SPEED;extra->maxTurn=ANGLE(3);break;
    default:break;
    }
}

Lara *Enemy_getLara(const vec3i *pos)
{
    Lara *a=gs->players[0],*b=gs->players[1];
    if (!b || b->health<=0)return a;
    if (!a || a->health<=0)return b;
    return game_distance_squared(*pos,b->pos)<game_distance_squared(*pos,a->pos) ? b : a;
}

bool Enemy_updateBegin(ItemObj *self)
{
    if ((self->flags&ITEM_FLAG_STATUS)==ITEM_FLAG_STATUS_INVISIBLE) {
        if (!Enemy_enable(self,false))return false;
        self->flags=(self->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
    }
    if (!self->extraE)return false;
    Enemy_updateTargetInfo(self);
    if (self->health>0) {
        Enemy_updateMood(self);
        Enemy_updateNavigation(self);
    }
    return true;
}

void Enemy_updateEnd(ItemObj *self)
{
    ItemObj_animProcess(self,true);
    Enemy_updateLocation(self);
    self->extraE->rotHead=angleLerp(self->extraE->rotHead,gs->tinfo.rotHead,ANGLE(5));
    self->angle.z=wrap_i16((uint32)self->angle.z+
        X_CLAMP(gs->tinfo.tilt*4-self->angle.z,-ANGLE(3),ANGLE(3)));
    self->angle.y=wrap_i16((uint32)self->angle.y+gs->tinfo.turn);
    if ((self->flags&ITEM_FLAG_STATUS)==ITEM_FLAG_STATUS_INACTIVE) {
        self->flags&=~ITEM_FLAG_COLLISION;
        self->health=NOT_ENEMY;
        Enemy_disable(self);
        ItemObj_deactivate(self);
    }
}

void Enemy_explode(ItemObj *self, uint32 jointMask)
{
        int32 jointCount = (gs->level.models[self->type]).count;
        if (jointCount <= 0)
            return;
        uint32 pending = jointMask & self->visibleMask;
        if (jointCount < 32)
            pending &= (1u << jointCount) - 1;
        for (int32 joint = 0; pending && joint < jointCount && joint < 32;
             joint++)
        {
            uint32 jointBit = 1u << joint;
            if (!(pending & jointBit))
                continue;
            vec3i jointPos = vec3i_add(self->pos, ItemObj_getJoint(self, joint, &(vec3i){0, 0, 0}));
            if (addExplosion(self->room, &(jointPos), 0))
                self->visibleMask &= ~jointBit;
            pending &= ~jointBit;
        }
    }

void Enemy_setExtra(ItemObj *self, ExtraInfoEnemy * extra)
{
        ASSERT((!self->extraE));
        ASSERT((extra));
        if (extra->enemy)
        {
            (extra->enemy)->flags &= ~((3 << 3));
            (extra->enemy)->flags |= ((3 << 3));
            Enemy_disable(extra->enemy);
        }
        extra->enemy = self;
        extra->target = NULL;
        extra->contact[0]=extra->contact[1]=0;
        extra->rotHead = extra->rotNeck = 0;
        extra->maxTurn = 0;
        (extra->nav).stepHeight = 256;
        (extra->nav).dropHeight = -256;
        (extra->nav).vSpeed = 0;
        self->extraE = extra;
        Enemy_initExtra(self);
        const Sector * sector = Room_getSector(self->room, self->pos.x, self->pos.z);
        if (sector->boxIndex == 0xFFFF && self->type == ITEM_DOPPELGANGER)
        {
            /* A loaded, active doppelganger stands at the mirror image of Lara, which can be over a
             * wall sector with no box (LEVEL10B room 57). Nav_init(0xFFFF) would read the zone tables
             * out of bounds; it never navigates: leave the nav state cleared. Oracle hunk 16
             * (test/oracle/README.md). */
            Nav *nav = &((self->extraE)->nav);
            nav->zoneType = ZONE_GROUND_1;  /* stepHeight is 256 (set above) */
            nav->weight = 0;
            nav->endBox = nav->nextBox = nav->headBox = nav->tailBox = 0xFFFF;
            nav->mask = BOX_OVERLAP_BLOCKED;
            for (uint32 i = 0; i < gs->level.boxesCount; i++)
            {
                (nav->cells[i]).end = 0xFFFF;
                (nav->cells[i]).next = 0xFFFF;
                (nav->cells[i]).weight = 0;
            }
            nav->cellsCount = 0;
            return;
        }
        ASSERT((sector->boxIndex != 0xFFFF));
        Nav_init(&((self->extraE)->nav), sector->boxIndex);
    }

static int32 Enemy_cameraDistance(const ItemObj *item,const Lara *a,const Lara *b)
{
    vec3i d=vec3i_sub(item->pos,a->extraL->camera.view.pos);
    int32 x=d.x>>8,y=d.y>>8,z=d.z>>8;
    int32 dist=i32_add(i32_add(i32_mul(x,x),i32_mul(y,y)),i32_mul(z,z));
    if (b) {
        d=vec3i_sub(item->pos,b->extraL->camera.view.pos);
        x=d.x>>8;y=d.y>>8;z=d.z>>8;
        int32 other=i32_add(i32_add(i32_mul(x,x),i32_mul(y,y)),i32_mul(z,z));
        if (other<dist)dist=other;
    }
    return dist;
}

bool Enemy_enable(ItemObj *self, bool forced)
{
        if (self->extraE)
            return true;
        int32 index = -1;
        for (int32 i = 0; i < 8; i++)
        {
            if (!(gs->enemiesExtra[i]).enemy)
            {
                index = i;
                break;
            }
        }
        if (index == -1)
        {
            const Lara *a=gs->players[0],*b=gs->players[1];
            if (!a)return false;
            int32 maxDistQ=forced ? 0 : Enemy_cameraDistance(self,a,b);
            for (int32 j=0;j<8;++j) {
                int32 distQ=Enemy_cameraDistance(gs->enemiesExtra[j].enemy,a,b);
                if (distQ>maxDistQ) {
                    maxDistQ=distQ;
                    index=j;
                }
            }
        }
        if (index == -1)
            return false;
        Enemy_setExtra(self, gs->enemiesExtra + index);
        return true;
    }

void Enemy_disable(ItemObj *self)
{
        ASSERT((self->extraE));
        (self->extraE)->enemy = NULL;
        self->extraE->target = NULL;
        self->extraE = NULL;
    }

void Enemy_updateTargetInfo(ItemObj *self)
{
        Lara *target=Enemy_getLara(&self->pos);
        if (gs->players[1]) {
            self->hitMask=self->extraE->contact[Lara_slot(target)];
            self->extraE->contact[0]=self->extraE->contact[1]=0;
            if (self->extraE->target!=target)self->extraE->nav.nextBox=UINT16_MAX;
        }
        gs->tinfo.target=self->extraE->target=target;
        gs->tinfo.tilt = 0;
        gs->tinfo.turn = 0;
        (gs->tinfo.pos = self->pos);
        gs->tinfo.dist = (16 * 1024);
        gs->tinfo.angle = 0;
        gs->tinfo.front = false;
        gs->tinfo.behind = false;
        gs->tinfo.canAttack = false;
        gs->tinfo.rotHead = 0;
        const tr64_u16_be_t * zones = Enemy_getZones(self);
        gs->tinfo.boxIndex = (Room_getSector(self->room, self->pos.x, self->pos.z))->boxIndex;
        gs->tinfo.boxIndexTarget = (Room_getSector((gs->tinfo.target)->room, ((gs->tinfo.target)->pos).x, ((gs->tinfo.target)->pos).z))->boxIndex;
        gs->tinfo.zoneIndex = tr64_u16(zones[gs->tinfo.boxIndex]);
        gs->tinfo.zoneIndexTarget = gs->players[1] && gs->tinfo.boxIndexTarget>=gs->level.boxesCount
            ? UINT16_MAX : tr64_u16(zones[gs->tinfo.boxIndexTarget]);
        if (self->health <= 0)
            return;
        if ((gs->tinfo.target)->health <= 0)
        {
            self->hitMask = 0;
            return;
        }
        int32 dx = i32_sub(((gs->tinfo.target)->pos).x, self->pos.x);
        int32 dz = i32_sub(((gs->tinfo.target)->pos).z, self->pos.z);
        if (self->headOffset)
        {
            int32 s; int32 c;
            sincos(self->angle.y, s, c);
            (dx = i32_sub(dx, i32_mul(s, self->headOffset) >> 14));
            (dz = i32_sub(dz, i32_mul(c, self->headOffset) >> 14));
        }
        int16 rot = phd_atan(dz, dx);
        gs->tinfo.dist = fastLength(dx, dz);
        gs->tinfo.angle = wrap_i16((uint32)rot - (uint32)self->angle.y);
        gs->tinfo.front = abs(gs->tinfo.angle) < (0x10000 / 4);
        gs->tinfo.behind = abs(i32_sub(i32_sub(rot, ((gs->tinfo.target)->angle).y), -(0x10000 / 2))) < (0x10000 / 4);
        gs->tinfo.canAttack = gs->tinfo.front && (abs(i32_sub(((gs->tinfo.target)->pos).y, self->pos.y)) < 256);
        gs->tinfo.rotHead = (gs->tinfo.front && self->input != MOOD_SLEEP) ? gs->tinfo.angle : 0;
    }

void Enemy_updateMood(ItemObj *self)
{
        if (self->input != MOOD_ATTACK && ((self->extraE)->nav).nextBox != 0xFFFF && !Enemy_checkZoneBox(self, ((self->extraE)->nav).endBox))
        {
            bool inZone = Enemy_checkZone(self);
            if (!inZone) {
                self->input = MOOD_SLEEP;
            }
            ((self->extraE)->nav).nextBox = 0xFFFF;
        }
        uint32 nextMood = (gs->tinfo.target)->health <= 0 ? MOOD_SLEEP : ((self->flags & ((1 << 7))) ? Enemy_getMoodWild(self) : Enemy_getMoodNormal(self));
        self->flags &= ~((1 << 6));
        if (self->input != nextMood)
        {
            if (self->input == MOOD_ATTACK) {
                Enemy_setNextBox(self, ((self->extraE)->nav).endBox);
            }
            ((self->extraE)->nav).nextBox = 0xFFFF;
            self->input = nextMood;
        }
        switch (self->input)
        {
            case MOOD_SLEEP : Enemy_moodSleep(self); break;
            case MOOD_STALK : Enemy_moodStalk(self); break;
            case MOOD_ATTACK : Enemy_moodAttack(self); break;
            case MOOD_ESCAPE : Enemy_moodEscape(self); break;
        }
        if (((self->extraE)->nav).endBox == 0xFFFF) {
            Enemy_setNextBox(self, gs->tinfo.boxIndex);
        }
    }

void Enemy_moodSleep(ItemObj *self)
{
        int32 boxIndex = Enemy_getRandomBox(self);
        if (!Enemy_checkZoneBox(self, boxIndex))
            return;
        if (Enemy_checkStalk(self, boxIndex))
        {
            self->input = MOOD_STALK;
            Enemy_setNextBox(self, boxIndex);
            return;
        }
        if (((self->extraE)->nav).nextBox != 0xFFFF)
            return;
        Enemy_setNextBox(self, boxIndex);
    }

void Enemy_moodStalk(ItemObj *self)
{
        if (((self->extraE)->nav).nextBox != 0xFFFF && Enemy_checkStalk(self, ((self->extraE)->nav).nextBox))
            return;
        int32 boxIndex = Enemy_getRandomBox(self);
        if (!Enemy_checkZoneBox(self, boxIndex))
            return;
        if (Enemy_checkStalk(self, boxIndex))
        {
            Enemy_setNextBox(self, boxIndex);
            return;
        }
        if (((self->extraE)->nav).nextBox != 0xFFFF)
            return;
        Enemy_setNextBox(self, boxIndex);
        if (gs->tinfo.zoneIndex != gs->tinfo.zoneIndexTarget) {
            self->input = MOOD_SLEEP;
        }
    }

void Enemy_moodAttack(ItemObj *self)
{
        if (rand_logic() >= self->aggression)
            return;
        (((self->extraE)->nav).pos = (gs->tinfo.target)->pos);
        ((self->extraE)->nav).nextBox = gs->tinfo.boxIndexTarget;
        if (((self->extraE)->nav).zoneType == ZONE_FLY) {
            ((((self->extraE)->nav).pos).y = i32_add((((self->extraE)->nav).pos).y, ((*ItemObj_getBoundingBox(gs->tinfo.target, false))).minY));
        }
    }

void Enemy_moodEscape(ItemObj *self)
{
        int32 boxIndex = Enemy_getRandomBox(self);
        if (!Enemy_checkZoneBox(self, boxIndex) || (((self->extraE)->nav).nextBox != 0xFFFF))
            return;
        if (Enemy_checkEscape(self, boxIndex))
        {
            Enemy_setNextBox(self, boxIndex);
            return;
        }
        if ((gs->tinfo.zoneIndex != gs->tinfo.zoneIndexTarget) || !Enemy_checkStalk(self, boxIndex))
            return;
        self->input = MOOD_STALK;
        Enemy_setNextBox(self, boxIndex);
    }

void Enemy_updateNavigation(ItemObj *self)
{
        (gs->tinfo.waypoint = Nav_getWaypoint(&((self->extraE)->nav), gs->tinfo.boxIndex, &(self->pos)));
        int16 maxTurn = (self->extraE)->maxTurn;
        if (self->health <= 0 || !self->hSpeed || !maxTurn)
            return;
        int32 dx = i32_sub((gs->tinfo.waypoint).x, self->pos.x);
        int32 dz = i32_sub((gs->tinfo.waypoint).z, self->pos.z);
        int16 turn = wrap_i16((uint32)phd_atan(dz, dx) - (uint32)self->angle.y);
        int32 r = (i32_shl(self->hSpeed, 14)) / maxTurn;
        int32 d = fastLength(dx, dz);
        bool aim = (turn > -(0x10000 / 4)) && (turn < (0x10000 / 4));
        if (!aim && (d < r))
            maxTurn >>= 1;
        gs->tinfo.turn = ((turn) < (-maxTurn) ? (-maxTurn) : (X_MIN((turn), (maxTurn))));
    }

void Enemy_setNextBox(ItemObj *self, int16 boxIndex)
{
        boxIndex &= 0x7FFF;
        const tr64_core_boxes_t * box = Enemy_boxes() + boxIndex;
        int32 bMinX = i32_add((i32_shl(((tr64_i32(box->min_x)) >> 10), 10)), 512);
        int32 bMaxX = i32_sub((i32_shl(((tr64_i32(box->max_x) + 1) >> 10), 10)), 512);
        int32 bMinZ = i32_add((i32_shl(((tr64_i32(box->min_z)) >> 10), 10)), 512);
        int32 bMaxZ = i32_sub((i32_shl(((tr64_i32(box->max_z) + 1) >> 10), 10)), 512);
        ((self->extraE)->nav).nextBox = boxIndex;
        (((self->extraE)->nav).pos).x = i32_add(bMinX, (i32_mul(rand_logic(), (i32_sub(bMaxX, bMinX))) >> 15));
        (((self->extraE)->nav).pos).z = i32_add(bMinZ, (i32_mul(rand_logic(), (i32_sub(bMaxZ, bMinZ))) >> 15));
        (((self->extraE)->nav).pos).y = tr64_i16(box->floor);
        if (((self->extraE)->nav).zoneType != ZONE_FLY)
            ((((self->extraE)->nav).pos).y = i32_sub((((self->extraE)->nav).pos).y, 384));
    }

uint32 Enemy_getMoodWild(ItemObj *self)
{
        bool inZone = Enemy_checkZone(self);
        if (self->input == MOOD_SLEEP || self->input == MOOD_STALK)
            return inZone ? MOOD_ATTACK : ((self->flags & ((1 << 6))) ? MOOD_ESCAPE : self->input);
        if (self->input == MOOD_ATTACK)
            return inZone ? self->input : MOOD_SLEEP;
        return inZone ? MOOD_ATTACK : self->input;
    }

uint32 Enemy_getMoodNormal(ItemObj *self)
{
        bool inZone = Enemy_checkZone(self);
        if (self->input == MOOD_SLEEP || self->input == MOOD_STALK)
        {
            if ((self->flags & ((1 << 6))) && (rand_logic() < 0x800 || !inZone))
                return MOOD_ESCAPE;
            if (inZone)
                return ((gs->tinfo.dist < 3072) || (self->input == MOOD_STALK && ((self->extraE)->nav).nextBox == 0xFFFF)) ? MOOD_ATTACK : MOOD_STALK;
            return self->input;
        }
        if (self->input == MOOD_ATTACK)
            return ((self->flags & ((1 << 6))) && rand_logic() < 0x800) ? MOOD_ESCAPE : (!inZone ? MOOD_SLEEP : self->input);
        return (inZone && rand_logic() < 0x100) ? MOOD_STALK : self->input;
    }

bool Enemy_checkZone(ItemObj *self)
{
    (void)self;
        return gs->tinfo.zoneIndex == gs->tinfo.zoneIndexTarget;
    }

int32 Enemy_getRandomBox(ItemObj *self)
{
        return (((self->extraE)->nav).cells[(rand_logic() * (((self->extraE)->nav).cellsCount) >> 15)]).boxIndex;
    }

bool Enemy_checkZoneBox(ItemObj *self, int16 boxIndex)
{
        if (tr64_u16(Enemy_getZones(self)[boxIndex]) != gs->tinfo.zoneIndex)
            return false;
        const tr64_core_boxes_t * b = &(Enemy_boxes()[boxIndex]);
        return (self->pos.x <= (i32_shl(((tr64_i32(b->min_x)) >> 10), 10))) || (self->pos.x >= (i32_sub((i32_shl(((tr64_i32(b->max_x) + 1) >> 10), 10)), 1))) ||
               (self->pos.z <= (i32_shl(((tr64_i32(b->min_z)) >> 10), 10))) || (self->pos.z >= (i32_sub((i32_shl(((tr64_i32(b->max_z) + 1) >> 10), 10)), 1)));
    }

bool Enemy_checkStalk(ItemObj *self, int16 boxIndex)
{
        const tr64_core_boxes_t * b = &(Enemy_boxes()[boxIndex]);
        int32 dx = i32_sub(((i32_shl((i32_add(((tr64_i32(b->min_x)) >> 10), ((tr64_i32(b->max_x) + 1) >> 10))), 10)) >> 1), ((gs->tinfo.target)->pos).x);
        int32 dz = i32_sub(((i32_shl((i32_add(((tr64_i32(b->min_z)) >> 10), ((tr64_i32(b->max_z) + 1) >> 10))), 10)) >> 1), ((gs->tinfo.target)->pos).z);
        if ((X_MAX((abs(dx)), (abs(dz)))) > 3072)
            return false;
        int32 qLara = i32_add((((gs->tinfo.target)->angle).y >> 14), 2);
        int32 qBox = (dz > 0) ? ((dx > 0) ? 2 : 1) : ((dx > 0) ? 3 : 0);
        if (qLara == qBox)
            return false;
        if (abs(i32_sub(qLara, qBox)) != 2)
            return true;
        dx = i32_sub(self->pos.x, ((gs->tinfo.target)->pos).x);
        dz = i32_sub(self->pos.z, ((gs->tinfo.target)->pos).z);
        int32 qEnemy = (dz > 0) ? ((dx > 0) ? 2 : 1) : ((dx > 0) ? 3 : 0);
        return qLara != qEnemy;
    }

bool Enemy_checkEscape(ItemObj *self, int16 boxIndex)
{
        const tr64_core_boxes_t * b = &(Enemy_boxes()[boxIndex]);
        int32 dx = i32_sub(((i32_shl((i32_add(((tr64_i32(b->min_x)) >> 10), ((tr64_i32(b->max_x) + 1) >> 10))), 10)) >> 1), ((gs->tinfo.target)->pos).x);
        int32 dz = i32_sub(((i32_shl((i32_add(((tr64_i32(b->min_z)) >> 10), ((tr64_i32(b->max_z) + 1) >> 10))), 10)) >> 1), ((gs->tinfo.target)->pos).z);
        if ((X_MAX((abs(dx)), (abs(dz)))) < 5120)
            return false;
        int32 px = i32_sub(self->pos.x, ((gs->tinfo.target)->pos).x);
        int32 pz = i32_sub(self->pos.z, ((gs->tinfo.target)->pos).z);
        return !(((dx ^ px) & 0x80000000) && ((dz ^ pz) & 0x80000000));
    }

const tr64_u16_be_t * Enemy_getZones(ItemObj *self)
{
        return Enemy_zoneTable(gs->gSaveGame.flipped, ((self->extraE)->nav).zoneType);
    }

void Enemy_bite(ItemObj *self, int32 damage, const vec3i * offset, int32 joint)
{
        if (!gs->tinfo.target)
            return;
        vec3i fxPos = vec3i_add(self->pos, ItemObj_getJoint(self, joint, offset));
        ItemObj_fxBlood(self, &(fxPos), 0, 0);
        ItemObj_hit(gs->tinfo.target, damage, offset, 0);
    }

bool Enemy_targetIsVisible(ItemObj *self, int32 maxDist)
{
        if (!gs->tinfo.target || !gs->tinfo.front || maxDist <= 0 ||
            gs->tinfo.dist >= ((uint32)(maxDist)) ||
            (gs->tinfo.target)->health <= 0)
            return false;
        Location from;
        Location to;
        (from).room = self->room;
        ((from).pos = self->pos);
        (((from).pos).y = i32_sub(((from).pos).y, 768));
        (to).room = (gs->tinfo.target)->room;
        ((to).pos = (gs->tinfo.target)->pos);
        if ((gs->tinfo.target)->waterState != WATER_STATE_UNDER &&
            (gs->tinfo.target)->waterState != WATER_STATE_SURFACE)
        {
            (((to).pos).y = i32_sub(((to).pos).y, 768));
        }
        return trace(&(from), &(to), true);
    }

void Enemy_shot(ItemObj *self, ItemType bulletType, int32 joint, const vec3i * offset, int16 angleX, int16 angleY)
{
        if (!gs->tinfo.target)
            return;
        vec3i from = vec3i_add(self->pos, ItemObj_getJoint(self, joint, offset));
        const AABBs * box = &((*ItemObj_getBoundingBox(gs->tinfo.target, false)));
        vec3i to = (gs->tinfo.target)->pos;
        ((to).x = i32_add((to).x, (i32_add(((int32)(box->minX)), box->maxX)) >> 1));
        ((to).y = i32_add((to).y, (i32_add(((int32)(box->minY)), box->maxY)) >> 1));
        ((to).z = i32_add((to).z, (i32_add(((int32)(box->minZ)), box->maxZ)) >> 1));
        int32 dx = i32_sub((to).x, (from).x);
        int32 dy = i32_sub((to).y, (from).y);
        int32 dz = i32_sub((to).z, (from).z);
        int32 horizontal = phd_sqrt(((uint32)(i32_add((i32_mul((dx), (dx))), (i32_mul((dz), (dz)))))));
        int16 bulletAngleX = wrap_i16((uint32)-phd_atan(horizontal, dy) + (uint32)angleX);
        int16 bulletAngleY = wrap_i16((uint32)phd_atan(dz, dx) + (uint32)angleY);
        ItemObj * bullet = (ItemObj*)ItemObj_add(bulletType, self->room, &(from), bulletAngleY);
        if (bullet)
            Bullet_setAngle(bullet, bulletAngleX, bulletAngleY);
    }

void Enemy_shotSpread(ItemObj *self, ItemType bulletType, int32 joint, const vec3i * offset, int16 spreadX, int16 spreadY)
{
        int16 angleX = ((int16)(i32_mul((i32_sub(((int32)(rand_logic())), 0x4000)), spreadX) >> 14));
        int16 angleY = ((int16)(i32_mul((i32_sub(((int32)(rand_logic())), 0x4000)), spreadY) >> 14));
        Enemy_shot(self, bulletType, joint, offset, angleX, angleY);
    }

void Enemy_updateLocation(ItemObj *self)
{
        Room * oldRoom = self->room;
        ItemObj_updateRoom(self, 0);
        int32 h = i32_sub(self->pos.y, self->roomFloor);
        bool badPos = (h > ((self->extraE)->nav).stepHeight) || (h < ((self->extraE)->nav).dropHeight);
        if (!badPos)
        {
            if (((self->extraE)->nav).zoneType == ZONE_FLY)
            {
                int32 dy = ((i32_sub((gs->tinfo.waypoint).y, self->pos.y)) < (-((self->extraE)->nav).vSpeed) ? (-((self->extraE)->nav).vSpeed) : (X_MIN((i32_sub((gs->tinfo.waypoint).y, self->pos.y)), (((self->extraE)->nav).vSpeed))));
                int32 y = i32_add(self->pos.y, dy);
                const Sector * sector = Room_getSector(self->room, self->pos.x, self->pos.z);
                int32 floor = Sector_getFloor(sector, self->pos.x, y, self->pos.z);
                if (y > floor)
                {
                    if (self->pos.y > floor)
                    {
                        self->pos.x = (gs->tinfo.pos).x;
                        self->pos.z = (gs->tinfo.pos).z;
                        dy = -((self->extraE)->nav).vSpeed;
                    } else {
                        self->pos.y = floor;
                        dy = 0;
                    }
                }
                int32 ceiling = Sector_getCeiling(sector, self->pos.x, y, self->pos.z);
                if (y < ceiling)
                {
                    if (self->pos.y < ceiling)
                    {
                        self->pos.x = (gs->tinfo.pos).x;
                        self->pos.z = (gs->tinfo.pos).z;
                        dy = ((self->extraE)->nav).vSpeed;
                    } else {
                        self->pos.y = ceiling;
                        dy = 0;
                    }
                }
                (self->pos.y = i32_add(self->pos.y, dy));
                int32 pitch = 0;
                if (self->hSpeed) {
                    pitch = phd_atan(self->hSpeed, -dy);
                }
                self->angle.x = angleLerp(self->angle.x, pitch, (0x10000 / 360));
                ItemObj_updateRoom(self, 0);
            } else {
                if (self->pos.y > self->roomFloor) {
                    self->pos.y = self->roomFloor;
                } else if (self->pos.y < i32_sub(self->roomFloor, 64)) {
                    (self->pos.y = i32_add(self->pos.y, 64));
                } else {
                    self->pos.y = self->roomFloor;
                }
            }
        }
        if (!badPos)
        {
            ItemObj * item = (self->room)->firstItem;
            while (item)
            {
                if (item->type != ITEM_LARA && item != self && item->health > 0 && ((item->flags & ((3 << 3))) == ((1 << 3))))
                {
                    int32 dx = i32_sub((item->pos).x, self->pos.x);
                    int32 dz = i32_sub((item->pos).z, self->pos.z);
                    int32 d = fastLength(dx, dz);
                    if ((d < (i32_shl(self->timer, 1))) && self->hSpeed)
                    {
                        gs->tinfo.turn = (i32_mul((8), (0x10000 / 360)));
                        badPos = true;
                        break;
                    }
                }
                item = item->nextItem;
            }
        }
        if (!badPos)
        {
            const tr64_u16_be_t * zones = Enemy_getZones(self);
            uint32 boxIndex = (Room_getSector(self->room, self->pos.x, self->pos.z))->boxIndex;
            badPos = (boxIndex == 0xFFFF) || (gs->tinfo.zoneIndex != tr64_u16(zones[boxIndex]));
        }
        if (badPos)
        {
            self->angle.y = wrap_i16((uint32)self->angle.y + (uint32)gs->tinfo.turn);
            (self->pos = gs->tinfo.pos);
            if (self->room != oldRoom)
            {
                Room_remove(self->room, self);
                Room_add(oldRoom, self);
            }
        }
    }

void Enemy_activate(ItemObj *self)
{
        if (self->health <= 0)
            return;
        ItemObj_activateBase(self);
        if (!Enemy_enable(self, !(self->flags & ((3 << 3))))) {
            self->flags &= ~((3 << 3));
            self->flags |= ((3 << 3));
        }
    }

void Enemy_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId)
{
        ASSERT((self->health > 0));
        self->health = wrap_i16((uint32)self->health - (uint32)damage);
        if (self->health > 0)
        {
            if (soundId) {
                soundPlay(soundId, &self->pos);
            }
        }
        if (self->type != ITEM_MUMMY) {
            ItemObj_fxBlood(self, point, 0, 0);
        }
        self->flags |= ((1 << 6));
    }

void Enemy_collide(ItemObj *self, Lara * lara, CollisionInfo * cinfo)
{
        if (!self->extraE)
            return;
        uint32 previous=self->hitMask;
        bool hit=ItemObj_updateHitMask(self,lara,cinfo);
        if (gs->players[1]) {
            self->extraE->contact[Lara_slot(lara)]=self->hitMask;
            self->hitMask=previous;
        }
        if (!hit)
            return;
        if (!cinfo->enemyPush)
            return;
        int32 r = cinfo->radius;
        cinfo->radius = 0;
        ItemObj_collidePush(self, lara, cinfo, true);
        cinfo->radius = r;
    }

bool Enemy_humanWaitSelected(ItemObj *self)
{
    (void)self;
        int32 value = rand_logic();
        return value < Human_WAIT_CHANCE;
    }

bool Enemy_humanFire(ItemObj *self, int32 gunJoint, const vec3i * muzzleOffset, int32 damage)
{
        MuzzleFlash_add(self, gunJoint, muzzleOffset);
        if (!Enemy_targetIsVisible(self, Human_DIST_SHOT))
            return false;
        int32 chance = i32_sub(((int32)(((int64_t)(Human_DIST_SHOT - gs->tinfo.dist)) * 0x8000 / Human_DIST_SHOT)), 0x2000);
        int32 roll = rand_logic();
        if (roll >= chance)
            return false;
        ItemObj * target = gs->tinfo.target;
        vec3i hitPos = target->pos;
        int32 jointCount = (gs->level.models[target->type]).count;
        if (jointCount > 0)
        {
            int32 jointValue = rand_logic();
            (hitPos = vec3i_add(hitPos, ItemObj_getJoint(target, i32_mul(jointValue, jointCount) >> 15, &(vec3i){0, 0, 0})));
        }
        ItemObj_hit(target, damage, &(hitPos), 0);
        ItemObj_fxBlood(target, &(hitPos), 0, 0);
        soundPlay(target->waterState == WATER_STATE_UNDER ?
                  SND_HIT_UNDERWATER : SND_HIT, &target->pos);
        return true;
    }
