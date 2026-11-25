/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_boulder.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/hazard.h"
static void moveToRoom(ItemObj *self,Room *next)
{
    if (next != self->room) {
        Room_remove(self->room,self);
        Room_add(next,self);
    }
}
void TrapBoulder_update(ItemObj *self)
{
    if (self->flags & ITEM_FLAG_INJURED) return;
    if (self->vSpeed != 0) self->vSpeed += self->vSpeed < 128 ? GRAVITY : 1;
    int32 s,c;
    sincos(self->angle.y,s,c);
    self->pos.x=i32_add(self->pos.x,i32_mul(s,self->hSpeed)>>FIXED_SHIFT);
    self->pos.y=i32_add(self->pos.y,self->vSpeed);
    self->pos.z=i32_add(self->pos.z,i32_mul(c,self->hSpeed)>>FIXED_SHIFT);
    moveToRoom(self,Room_getRoom(self->room,self->pos.x,self->pos.y,self->pos.z));
    const Sector *sector=Room_getSector(self->room,self->pos.x,self->pos.z);
    int32 floor=Sector_getFloor(sector,self->pos.x,self->pos.y,self->pos.z);
    if (self->pos.y < floor) {
        if (self->vSpeed == 0) self->vSpeed=-10;
    } else if (self->state != 1) {
        self->goalState=1;
        const Anim *anim=ItemObj_animChange(self,gs->level.anims+self->animIndex);
        int64_t speed=(int64_t)tr64_i32(anim->speed)+(int64_t)tr64_i32(anim->acceleration)*(self->frameIndex-tr64_u16(anim->frame_start));
        speed >>= 16;
        ASSERT(speed >= -32768 && speed <= 32767);
        self->hSpeed=(int16)speed;
    }
    if (self->pos.y >= floor-256) {
        self->vSpeed=0;
        self->pos.y=floor;
    }
    vec3i ahead=self->pos;
    ahead.x=i32_add(ahead.x,(s*512)>>FIXED_SHIFT);
    ahead.z=i32_add(ahead.z,(c*512)>>FIXED_SHIFT);
    Room *aheadRoom=Room_getRoom(self->room,ahead.x,ahead.y,ahead.z);
    sector=Room_getSector(aheadRoom,ahead.x,ahead.z);
    if (self->pos.y > Sector_getFloor(sector,ahead.x,ahead.y,ahead.z)) {
        self->flags |= ITEM_FLAG_INJURED;
        self->pos.x=(self->pos.x/1024)*1024+512;
        self->pos.z=(self->pos.z/1024)*1024+512;
        moveToRoom(self,Room_getRoom(self->room,self->pos.x,self->pos.y,self->pos.z));
        sector=Room_getSector(self->room,self->pos.x,self->pos.z);
        self->pos.y=Sector_getFloor(sector,self->pos.x,self->pos.y,self->pos.z);
    }
    sector=Room_getSector(self->room,self->pos.x,self->pos.z);
    Sector_getFloor(sector,self->pos.x,self->pos.y,self->pos.z);
    game_check_trigger(gs->gLastFloorData,NULL);
    if (gs->stop.kind != TR64_GAME_STOP_NONE) return;
    ItemObj_animProcess(self,false);
}
void TrapBoulder_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    if (!lara || !cinfo || lara->health <= 0 || !ItemObj_collideBounds(self,lara,cinfo) ||
        (lara->waterState != WATER_STATE_ABOVE && lara->waterState != WATER_STATE_WADE)) return;
    if (lara->flags & ITEM_FLAG_GRAVITY) { Lara_hit(lara,100,&self->pos,0); return; }
    hazardDamageLara(lara,LARA_MAX_HEALTH,HAZARD_HIT_BOULDER,&self->pos);
}
