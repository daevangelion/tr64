/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "object.h"
#if TR64_HAS_TR2
#include "game/tr2/objects.h"
#endif
#line 4
static const int16 LIMIT_BLOCK[] = {-300,300,0,0,-692,-512,ANGLE(10),ANGLE(30),ANGLE(10)};
static void Block_updateFloor(ItemObj *self,int32 offset)
{
#if TR64_HAS_TR2
    if (TR64_RULE(INTERACTIONS)) {
        game_tr2_block_occupancy(self,offset < 0);
        return;
    }
#endif
#line 7
    Room *target=Room_getRoom(self->room,self->pos.x,self->pos.y,self->pos.z);
    Sector *sector=(Sector *)Room_getSector(target,self->pos.x,self->pos.z);
    sector->floor=wrap_i16(sector->floor+(offset>>8));
}
static bool Block_checkBlocking(ItemObj *self)
{
    const Sector *s=Room_getSector(self->room,self->pos.x,self->pos.z);
    return s->floor == NO_FLOOR || s->floor*256+1024 == self->pos.y;
}
#if TR64_HAS_TR2
#define PUSH_TEST_PARAM ,bool push
#define PUSH_TEST_ARG(value) ,value
#else
#define PUSH_TEST_PARAM
#define PUSH_TEST_ARG(value)
#endif
#line 16
static bool Block_checkObstacles(ItemObj *self,int32 x,int32 z,int32 height PUSH_TEST_PARAM)
{
    Room *next=Room_getRoom(self->room,x,self->pos.y,z);
    const Sector *s=Room_getSector(next,x,z);
    int32 floor=self->pos.y, ceiling=self->pos.y-height;
#if TR64_HAS_TR2
    if (TR64_RULE(INTERACTIONS) && push) {
        /* Great Wall objects, Push one block ahead: both the base
         * height and the absence of a sloped surface are required. */
        if (s->floor*256 != floor)
            return false;
        (void)Sector_getFloor(s,x,floor,z);
        if (gs->gLastFloorSlant)
            return false;
        ceiling = i32_add(ceiling,100);
    }
#endif
#line 21
    if (s->floor*256 != floor) return false;
    next=Room_getRoom(next,x,ceiling,z);
    s=Room_getSector(next,x,z);
#if TR64_HAS_TR2
    if (TR64_RULE(INTERACTIONS)) {
        int32 clear = Sector_getCeiling(s,x,ceiling,z);
        if (clear == WALL || clear > ceiling)
            return false;
        const bool block = height == 1024;
        Room *room = Room_getRoom(self->room,x,floor,z);
        vec3i at = vec3i_create(x,floor,z);
        CollisionInfo test = {.radius=block ? 500 : LARA_RADIUS};
        if (Room_collideStatic(room,&test,&at,block ? 1000 : height))
            return false;
        if (tr64_u16(room->info->flags) & ROOM_FLAG_WATER_MASK)
            game_tr2_refuse_interaction(self,"movement into water");
        if (block) {
            const Sector *cell = Room_getSector(room,x,z);
            for (const ItemObj *other = room->firstItem; other; other = other->nextItem)
                if (game_tr2_creature_type(other->type) && (other->flags & ITEM_FLAG_COLLISION) &&
                    (other->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE &&
                    Room_getSector(room,other->pos.x,other->pos.z) == cell)
                    game_tr2_refuse_interaction(self,"creature in destination");
        }
        return true;
    }
#endif
#line 24
    return s->ceiling*256 <= ceiling;
}
#if TR64_HAS_TR2
/* Great Wall objects, Grasp and move: test the pre-step origin against
 * a cardinal frame. The item's current yaw need not already be that axis. */
static bool Block_cardinalReach(const ItemObj *item,const Lara *lara,vec3i origin,int16 axis)
{
    vec3i d = vec3i_sub(origin,item->pos);
    vec3i f = getBlockOffset(axis,-1);
    int64_t x = (int64_t)d.x*f.z-(int64_t)d.z*f.x;
    int64_t z = (int64_t)d.z*f.z+(int64_t)d.x*f.x;
    return d.y == 0 && x >= -300 && x <= 300 && z >= -692 && z <= -512 &&
           abs(lara->angle.x) <= 1820 && abs(wrap_i16(lara->angle.y-axis)) <= 5460 &&
           abs(lara->angle.z) <= 1820;
}
#endif
#line 26
void Block_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    (void)cinfo;
#if TR64_HAS_TR2
    const bool sequel = TR64_RULE(INTERACTIONS);
    if (sequel) {
        if (!(lara->input & IN_ACTION) || (self->flags & ITEM_FLAG_ACTIVE) ||
            lara->waterState != WATER_STATE_ABOVE || (lara->flags & ITEM_FLAG_GRAVITY) ||
            self->room != lara->room ||
            Room_getRoom(self->room,self->pos.x,self->pos.y,self->pos.z) != self->room)
            return;
    } else
#endif
    {
        if (!(lara->input & IN_ACTION) || (self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE || lara->pos.y != self->pos.y) return;
        if (Lara_claimBlocked(lara,self)) return;
    }
#line 31
    uint16 quadrant=(uint16)(lara->angle.y+ANGLE_45)>>14;
    if (lara->animIndex == Lara_ANIM_BLOCK_READY && (lara->input & (IN_UP|IN_DOWN))) {
        if (!ItemObj_animIsEnd(lara,0))
            return;
#if TR64_HAS_TR2
        if (sequel) {
            if (!Block_cardinalReach(self,lara,lara->pos,self->angle.y)) return;
        } else
#endif
        if (!Object_checkLimit(self,lara,LIMIT_BLOCK)) return;
#line 34
        if (!Block_checkBlocking(self)) return;
        if (lara->input & IN_UP) {
            vec3i offset=getBlockOffset(self->angle.y,-1024);
            if (!Block_checkObstacles(self,i32_add(self->pos.x,offset.x),i32_add(self->pos.z,offset.z),1024 PUSH_TEST_ARG(true))) return;
            lara->goalState=Lara_STATE_BLOCK_PUSH;
            self->goalState=2;
        } else {
            vec3i offset=getBlockOffset(self->angle.y,1024);
            if (!Block_checkObstacles(self,i32_add(self->pos.x,offset.x),i32_add(self->pos.z,offset.z),1024 PUSH_TEST_ARG(false)) ||
                !Block_checkObstacles(self,i32_add(lara->pos.x,offset.x),i32_add(lara->pos.z,offset.z),LARA_HEIGHT PUSH_TEST_ARG(false))) return;
            lara->goalState=Lara_STATE_BLOCK_PULL;
            self->goalState=3;
        }
#if TR64_HAS_TR2
        if (sequel) {
            Block_updateFloor(self,1024);
            ItemObj_activateBase(self);
        } else
#endif
        {
            Lara_claim(lara,self);
            Block_updateFloor(self,1024);
            ItemObj_activate(self);
        }
#line 50
        self->flags=(self->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
        if (gs->players[1]) {
            unsigned index=(unsigned)(self-gs->items);
            gs->coopBlocks[index/32]|=1u<<(index%32);
        }
        ItemObj_animProcess(self,true);
        ItemObj_animProcess(lara,true);
    }
#if TR64_HAS_TR2
    if (sequel) {
        int16 axis = wrap_i16(quadrant*ANGLE_90);
        if (gs->tr2->motionStart.state != Lara_STATE_STOP ||
            lara->extraL->weaponState != WEAPON_STATE_FREE ||
            !Block_cardinalReach(self,lara,cinfo->pos,axis))
            return;
        self->angle.y = lara->angle.y = axis;
        lara->pos = vec3i_add(self->pos,getBlockOffset(axis,612));
        ItemObj_animSet(lara,Lara_ANIM_BLOCK_READY,true,0);
        Lara_setWeaponState(lara,WEAPON_STATE_BUSY);
        return;
    }
#endif
#line 58
    if (lara->state == Lara_STATE_STOP) {
        if (lara->extraL->weaponState != WEAPON_STATE_FREE || (lara->input & (IN_UP|IN_DOWN))) return;
        self->angle.y=wrap_i16(quadrant*ANGLE_90);
        if (!Object_checkLimit(self,lara,LIMIT_BLOCK)) return;
        Lara_claim(lara,self);
        lara->goalState=Lara_STATE_BLOCK_READY;
        lara->angle.y=self->angle.y;
        Lara_alignWall(lara,LARA_RADIUS);
        ItemObj_animProcess(lara,true);
        if (lara->state == Lara_STATE_BLOCK_READY)Lara_setWeaponState(lara,WEAPON_STATE_BUSY);
    }
}
#undef PUSH_TEST_PARAM
#undef PUSH_TEST_ARG
#line 70
void Block_update(ItemObj *self)
{
    unsigned index=(unsigned)(self-gs->items);
    uint32 *moving=gs->coopBlocks+index/32, mask=1u<<(index%32);
    if (self->flags & ITEM_FLAG_ONCE) {
#if TR64_HAS_TR2
        if (!TR64_RULE(INTERACTIONS) || self->state == 1)
#endif
        Block_updateFloor(self,1024);
        ItemObj_remove(self);
        *moving &= ~mask;
        return;
    }
    ItemObj_animProcess(self,true);
    ItemObj_updateRoom(self,0);
#if TR64_HAS_TR2
    if (TR64_RULE(INTERACTIONS)) {
        /* Great Wall objects, Occupied and vacated support: the block
         * stays on the vacated cell until the end command. A floor that
         * opens under it stops on that active tick, before occupancy or
         * the heavy trigger, and does not seal. */
        if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE &&
            self->pos.y != self->roomFloor)
            game_tr2_refuse_interaction(self,"moving block left its supporting floor");
    } else
#endif
#line 82
    if (*moving & mask) {
        /* The tile was level when the push began; the live floor may have
         * opened or moved before the terminal translation. */
        if (self->pos.y<self->roomFloor) {
            self->flags=(self->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE|ITEM_FLAG_GRAVITY;
            return;
        }
        if (self->flags&ITEM_FLAG_GRAVITY) {
            self->flags=(self->flags&~(ITEM_FLAG_STATUS|ITEM_FLAG_GRAVITY))|ITEM_FLAG_STATUS_INACTIVE;
            self->pos.y=self->roomFloor;
            self->vSpeed=0;
        }
    } else if (self->pos.y > self->roomFloor) self->flags |= ITEM_FLAG_GRAVITY;
    else if (self->flags & ITEM_FLAG_GRAVITY) {
        self->flags=(self->flags & ~(ITEM_FLAG_GRAVITY|ITEM_FLAG_STATUS))|ITEM_FLAG_STATUS_INACTIVE;
        self->pos.y=self->roomFloor;
    }
    if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        *moving &= ~mask;
        ItemObj_deactivate(self);
        self->flags &= ~ITEM_FLAG_STATUS;
        Block_updateFloor(self,-1024);
        gs->blockTrigger=gs->gLastFloorData;
    }
}
/* Repaired omission (oracle host.patch hunk 25; docs/specs/wall/objects-missing.md sections 2.1 and 3; floating-point
 * model OpenLara src/objects.h:613-669 at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a): the reference never constructed
 * a controller for ITEM_MOVING_BLOCK, the rolling block. Its sector is raised by 2048 from the start (the world
 * rebuild does it, game_initial_world); while triggered in state 0 the goal is 1, while not triggered in state 1 it
 * is 0, and the sector is restored when a slide begins; the slide ends with the kill command, then the block stays
 * in the active list, snaps to the centre of its sector and raises it there. No heavy trigger is evaluated. */
static void MovingBlock_floor(ItemObj *self,bool raise)
{
    Room *target=Room_getRoom(self->room,self->pos.x,self->pos.y,self->pos.z);
    game_moving_block_sector(target,&self->pos,(Sector *)Room_getSector(target,self->pos.x,self->pos.z),raise);
}
void MovingBlock_update(ItemObj *self)
{
    if (Object_isActive(self)) {
        if (self->state == 0) { self->goalState=1; MovingBlock_floor(self,false); }
    } else if (self->state == 1) { self->goalState=0; MovingBlock_floor(self,false); }
    ItemObj_animProcess(self,true);
    if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        self->flags=(self->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
        self->pos.x=(self->pos.x & ~1023)+512;
        self->pos.z=(self->pos.z & ~1023)+512;
        MovingBlock_floor(self,true);
    }
}
/* Repaired omission (oracle host.patch hunk 26; specification section 6; OpenLara src/objects.h:1406-1425): the moving
 * bar (LEVEL4 75 to 78, LEVEL10A 12). The goal is 1 while triggered and 0 otherwise, the item follows its animation
 * and its room follows its position; its collision is the standard solid (Object_collideDefault). */
void MovingBar_update(ItemObj *self)
{
    self->goalState=Object_isActive(self) ? 1 : 0;
    ItemObj_animProcess(self,true);
    Room *next=Room_getRoom(self->room,self->pos.x,self->pos.y,self->pos.z);
    if (self->room != next) {
        Room_remove(self->room,self);
        Room_add(next,self);
    }
}
