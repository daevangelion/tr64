/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_slam.h, hazard_damocles.h, hazard_ceiling.h and hazard_oracles/{slam,damocles,falling-ceiling}.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/hazard.h"
void TrapSlam_update(ItemObj *self)
{
    if (Object_isActive(self)) {
        self->goalState=1;
        if (self->frameIndex >= 20) self->flags &= ~1u;
        CollisionInfo cinfo;
        memset(&cinfo,0,sizeof(cinfo));
        cinfo.radius=LARA_RADIUS;
        for (unsigned slot=0;slot<TR64_GAME_PLAYERS && !(self->flags&1) && self->state==1;++slot) {
            Lara *lara=gs->players[slot];
            if (!lara || lara->health<=0 || !ItemObj_collideBounds(self,lara,&cinfo))continue;
            hazardDamageLara(lara,400,HAZARD_HIT_SLAM,&self->pos);
            for (int32 i=0;i<6;i++) {
                int16 angle=wrap_i16((uint32)rand_draw()<<1);
                int32 x=(rand_draw()*64>>14)-64;
                int32 y=-(rand_draw()*512>>15);
                int32 z=(rand_draw()*64>>14)-64;
                vec3i p=vec3i_add(lara->pos,vec3i_create(x,y,z));
                ItemObj_fxBlood(lara,&p,angle,20);
            }
            self->flags |= 1;
        }
    } else self->goalState=0;
    ItemObj_animProcess(self,true);
}
void TrapDamocles_update(ItemObj *self)
{
    Lara *lara=Enemy_getLara(&self->pos);
    if (self->state == 0) {
        if (!lara) return;
        int32 dx=i32_sub(lara->pos.x,self->pos.x), dy=i32_sub(lara->pos.y,self->pos.y), dz=i32_sub(lara->pos.z,self->pos.z);
        if (dy > 0 && dy < 3072 && abs(dx)<1536 && abs(dz)<1536) {
            self->nextState=(uint8)(dx/32);
            self->goalState=(uint8)(dz/32);
            self->timer=50;
            self->state=1;
        }
        return;
    }
    if (self->state == 1) self->state=2;
    if (self->state != 2) return;
    int32 floor=Sector_getFloor(Room_getSector(self->room,self->pos.x,self->pos.z),self->pos.x,self->pos.y,self->pos.z);
    self->angle.y=wrap_i16(self->angle.y+self->hSpeed);
    self->timer += self->timer < 128 ? 6 : 1;
    int32 dx=(int32)(self->nextState & 127)-(int32)(self->nextState & 128);
    int32 dz=(int32)(self->goalState & 127)-(int32)(self->goalState & 128);
    self->pos.x=i32_add(self->pos.x,dx);
    self->pos.y=i32_add(self->pos.y,self->timer);
    self->pos.z=i32_add(self->pos.z,dz);
    if (self->pos.y > floor) {
        self->pos.y=floor;
        self->roomFloor=floor;
        soundPlay(103,&self->pos);
        self->state=3;
        ItemObj_deactivate(self);
        self->flags=(self->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INACTIVE;
    }
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
        lara=gs->players[slot];
        if (!lara || lara->health<=0 || !ItemObj_collideBounds(self,lara,&gs->cinfo))continue;
        ItemObj_fxBlood(lara,&lara->pos,lara->angle.y,lara->hSpeed);
        hazardDamageLara(lara,100,HAZARD_HIT_SWORD,&self->pos);
    }
}
void TrapCeiling_update(ItemObj *self)
{
    ItemObj_animProcess(self,false);
    if (self->state == 0) {
        self->timer=0;
        if (self->flags & ITEM_FLAG_ACTIVE) self->goalState=1;
    }
    if (self->state != 1) return;
    if (self->vSpeed == 0 && self->timer > 0) self->vSpeed=self->timer;
    self->vSpeed += self->vSpeed < 128 ? GRAVITY : 1;
    self->timer=self->vSpeed;
    self->pos.y=i32_add(self->pos.y,self->vSpeed);
    self->roomFloor=Room_getSector(self->room,self->pos.x,self->pos.z)->floor*256;
    if (self->pos.y > self->roomFloor) ItemObj_updateRoom(self,0);
    if (self->pos.y > self->roomFloor) {
        self->pos.y=self->roomFloor;
        self->vSpeed=self->timer=0;
        self->goalState=2;
    }
}
