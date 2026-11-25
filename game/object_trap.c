/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h, and for the drawbridge src/objects.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "object.h"
#include "enemy.h"
#if TR64_HAS_TR2
#include "tr2/objects.h"
#endif
#line 5
void TrapFloor_update(ItemObj *self)
{
    switch(self->state) {
    case 0: {
        const Lara *lara;
        int height;
#if TR64_HAS_TR2
        if (TR64_RULE(INTERACTIONS)) {
            lara = gs->players[0];
            height = tr2CollapseHeight(self);
        } else
#endif
        {
            lara = Enemy_getLara(&self->pos);
            height = 512;
        }
        if (!lara || lara->pos.y != i32_sub(self->pos.y,height)) {
#line 11
            self->flags &= ~ITEM_FLAG_STATUS;
            ItemObj_deactivate(self);
            return;
        }
        self->goalState=1;
        break;
    }
    case 1: self->goalState=2; break;
    case 2: if (self->goalState != 3) self->flags |= ITEM_FLAG_GRAVITY; break;
    }
    ItemObj_animProcess(self,true);
    if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        ItemObj_deactivate(self);
        return;
    }
    if (self->state == 2) ItemObj_updateRoom(self,0);
    if (self->state == 2 && self->pos.y >= self->roomFloor) {
        self->pos.y=self->roomFloor;
        self->vSpeed=0;
        self->flags &= ~ITEM_FLAG_GRAVITY;
        self->goalState=3;
    }
}
void TrapSwingBlade_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    if ((self->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE || self->state != 2 ||
        !ItemObj_updateHitMask(self,lara,cinfo)) return;
    /* Source argument evaluation follows its pinned oracle compiler, right to left. */
    int32 z=(rand_logic()-0x4000)>>8;
    int32 y=-256-(rand_logic()>>6);
    int32 x=(rand_logic()-0x4000)>>8;
    vec3i p=vec3i_add(lara->pos,vec3i_create(x,y,z));
    int32 angle=(rand_logic()-0x4000)>>3;
    ItemObj_fxBlood(lara,&p,wrap_i16(lara->angle.y+angle),lara->hSpeed);
    Lara_hit(lara,100,&self->pos,0);
}
void TrapSwingBlade_update(ItemObj *self)
{
    if (Object_isActive(self)) {
        if (self->state == 0) self->goalState=2;
    } else if (self->state == 2) self->goalState=0;
    ItemObj_animProcess(self,true);
}
void Dart_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    if (!ItemObj_updateHitMask(self,lara,cinfo)) return;
    if (self->hitMask) {
        ItemObj_fxBlood(lara,&self->pos,lara->angle.y,lara->hSpeed);
        Lara_hit(lara,50,&self->pos,0);
    }
}
void Dart_update(ItemObj *self)
{
    ItemObj_animProcess(self,true);
    ItemObj_updateRoom(self,0);
    if (self->pos.y < self->roomFloor) return;
    Room *impactRoom=self->room;
    vec3i p=self->pos;
    ItemObj_remove(self);
    ItemObj_fxRicochet(impactRoom,&p,false);
}
void TrapDartEmitter_update(ItemObj *self)
{
    self->goalState=Object_isActive(self) ? 1 : 0;
    if (self->state == 0 && self->state == self->goalState) {
        ItemObj_deactivate(self);
        return;
    }
    if (self->state == 1 && self->frameIndex == tr64_u16(gs->level.anims[self->animIndex].frame_start)) {
        vec3i p=getBlockOffset(self->angle.y,412);
        p.y=-512;
        p=vec3i_add(p,self->pos);
        ItemObj *dart=ItemObj_add(ITEM_DART,self->room,&p,self->angle.y);
        if (dart) {
            soundPlay(SND_DART,&self->pos);
            dart->intensity=0;
            dart->flags=(dart->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
            ItemObj_activate(dart);
            ItemObj_fxSmoke(self,&p);
        }
    }
    ItemObj_animProcess(self,true);
}
/* Repaired omission (oracle host.patch hunk 16; OpenLara src/objects.h:842-858 at 2333adef):
 * the reference never constructed a controller for ITEM_DRAWBRIDGE. Up (state 0) -> down
 * (state 1) while active, back otherwise; solid while up. */
void DrawBridge_update(ItemObj *self)
{
    if (Object_isActive(self)) {
        if (self->state == 0) self->goalState=1;
    } else if (self->state == 1) self->goalState=0;
    ItemObj_animProcess(self,true);
}
void DrawBridge_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    if (self->state != 0) return;
    if (!ItemObj_updateHitMask(self,lara,cinfo)) return;
    if (!cinfo->enemyPush) return;
    ItemObj_collidePush(self,lara,cinfo,self->state != self->goalState);
}
/* Repaired omission (oracle host.patch hunk 19; OpenLara src/objects.h:1463-1490 at 2333adef):
 * the reference never constructed a controller for ITEM_CABIN. Each completed trigger mask
 * advances it one stage; in state 4 flip slot 3 is completed, the map flips and it goes. */
void Cabin_update(ItemObj *self)
{
    if ((self->flags & ITEM_FLAG_MASK) == ITEM_FLAG_MASK) {
        if (self->state <= 2) self->goalState=self->state+1;
        self->flags &= ~ITEM_FLAG_MASK;
    }
    if (self->state == 4) {
        gs->flipMaps[3].mask=31;
        gs->flipMaps[3].flags |= 1;
        worldRequestFlipToggle();
        worldApplyPendingFlip();
        self->flags &= ~(ITEM_FLAG_STATUS | ITEM_FLAG_COLLISION);
        self->flags |= ITEM_FLAG_STATUS_INVISIBLE;
        ItemObj_deactivate(self);
        return;
    }
    ItemObj_animProcess(self,true);
}
/* Oracle hunk 22 (object.h LavaParticle, LavaEmitter; docs/specs/wall/objects-missing.md 16.1 and 16.2; floating-point
 * model OpenLara src/objects.h:301-390 at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a). The reference built no controller
 * for the particle, so the emitters produced nothing. A particle is one slot of the item pool (ItemObj_add, no other
 * allocation); the pool bounds how many exist at once (see docs/game-gaps.md). */
void LavaParticle_update(ItemObj *self)
{
    int32 s, c;
    sincos(self->angle.y, s, c);
    self->pos.x = i32_add(self->pos.x, i32_mul(s, self->hSpeed) >> 14);
    self->pos.z = i32_add(self->pos.z, i32_mul(c, self->hSpeed) >> 14);
    self->vSpeed = wrap_i16((uint32)self->vSpeed + 6);
    self->pos.y = i32_add(self->pos.y, self->vSpeed);
    Room *nextRoom = Room_getRoom(self->room, self->pos.x, self->pos.y, self->pos.z);
    const Sector *sector = Room_getSector(nextRoom, self->pos.x, self->pos.z);
    if (self->pos.y >= Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z) ||
        self->pos.y <= Sector_getCeiling(sector, self->pos.x, self->pos.y, self->pos.z)) {
        ItemObj_remove(self);
        return;
    }
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
        Lara *lara=gs->players[slot];
        if (!lara || lara->health<=0)continue;
        int32 dx = i32_sub(lara->pos.x, self->pos.x), dz = i32_sub(lara->pos.z, self->pos.z);
        const AABBs *box = ItemObj_getBoundingBox(lara, true);
        int32 top = i32_add(lara->pos.y, box->minY), bottom = i32_add(i32_add(lara->pos.y, box->maxY), 100);
        if (abs(dx) <= 200 && abs(dz) <= 200 && dx * dx + dz * dz <= 200 * 200 &&
            abs(i32_sub(lara->pos.y, self->pos.y)) <= 3072 && self->pos.y >= top && self->pos.y <= bottom) {
            Lara_hit(lara, 10, &self->pos, 0);
            ItemObj_remove(self);
            return;
        }
    }
    ItemObj_updateRoom(self, 0);
}
void LavaEmitter_update(ItemObj *self)
{
    ItemObj *particle = ItemObj_add(ITEM_LAVA_PARTICLE, self->room, &self->pos, wrap_i16((uint32)(rand_logic() * 0x10000 >> 15)));
    if (particle) {
        particle->hSpeed = (int16)(rand_logic() * 32 >> 15);
        particle->vSpeed = (int16)(-(rand_logic() * 164 >> 15));
        particle->frameIndex = (uint16)(rand_draw() * 5 >> 15);
    }
    soundPlay(SND_LAVA, &self->pos);
}
