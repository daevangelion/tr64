/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h and item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "object.h"
void ItemObj_animSkip(ItemObj *self, int32 stateBefore, int32 stateAfter, bool advance)
{
    self->goalState = stateBefore;
    vec3i p = self->pos;
    while (self->state != self->goalState) ItemObj_animProcess(self, false);
    if (advance) ItemObj_animProcess(self, true);
    self->pos = p;
    self->vSpeed = self->hSpeed = 0;
    self->goalState = stateAfter;
}
bool ItemObj_moveTo(ItemObj *self, const vec3i *point, ItemObj *item, bool lerp)
{
    vec3i p = ItemObj_getRelative(item, point);
    if (!lerp) {
        self->pos = p;
        self->angle = item->angle;
        return true;
    }
    vec3i d = vec3i_sub(p,self->pos);
    int32 dist = i32_add(i32_add(i32_mul(d.x,d.x),i32_mul(d.y,d.y)),i32_mul(d.z,d.z));
    if (dist > 256) {
        dist = phd_sqrt((uint32)dist) >> 1;
        ASSERT(dist < 1025);
        dist = divTable[dist];
        self->pos.x = i32_add(self->pos.x,i32_mul(d.x,dist) >> 13);
        self->pos.y = i32_add(self->pos.y,i32_mul(d.y,dist) >> 13);
        self->pos.z = i32_add(self->pos.z,i32_mul(d.z,dist) >> 13);
    } else self->pos = p;
    self->angle.x = angleLerp(self->angle.x,item->angle.x,ANGLE(2));
    self->angle.y = angleLerp(self->angle.y,item->angle.y,ANGLE(2));
    self->angle.z = angleLerp(self->angle.z,item->angle.z,ANGLE(2));
    return vec3i_equal(self->pos,p) && self->angle.x == item->angle.x &&
        self->angle.y == item->angle.y && self->angle.z == item->angle.z;
}
vec3i getBlockOffset(int16 angleY, int32 offset)
{
    if (angleY == 0) return vec3i_create(0,0,-offset);
    if (angleY == ANGLE_180) return vec3i_create(0,0,offset);
    if (angleY == ANGLE_90) return vec3i_create(-offset,0,0);
    return vec3i_create(offset,0,0);
}
bool Object_isActive(ItemObj *self)
{
    if ((self->flags & ITEM_FLAG_MASK) != ITEM_FLAG_MASK || self->timer == -1)
        return (self->flags & ITEM_FLAG_REVERSE) != 0;
    if (self->timer == 0) return (self->flags & ITEM_FLAG_REVERSE) == 0;
    self->timer--;
    if (self->timer == 0) self->timer = -1;
    return (self->flags & ITEM_FLAG_REVERSE) == 0;
}
bool Object_checkLimit(const ItemObj *self, const Lara *lara, const int16 *limit)
{
    int16 ax = wrap_i16(abs((int32)lara->angle.x-self->angle.x));
    int16 ay = wrap_i16(abs((int32)lara->angle.y-self->angle.y));
    int16 az = wrap_i16(abs((int32)lara->angle.z-self->angle.z));
    if (ax > limit[6] || ay > limit[7] || az > limit[8]) return false;
    vec3i d = vec3i_sub(lara->pos,self->pos);
    matrixSetIdentity_c();
    matrixRotateZ_c(-self->angle.z);
    matrixRotateX_c(-self->angle.x);
    matrixRotateY_c(-self->angle.y);
    const Matrix *m = gs->gMatrixPtr;
    int32 x = i32_add(i32_add(i32_mul(m->e00,d.x),i32_mul(m->e01,d.y)),i32_mul(m->e02,d.z)) >> FIXED_SHIFT;
    int32 y = i32_add(i32_add(i32_mul(m->e10,d.x),i32_mul(m->e11,d.y)),i32_mul(m->e12,d.z)) >> FIXED_SHIFT;
    int32 z = i32_add(i32_add(i32_mul(m->e20,d.x),i32_mul(m->e21,d.y)),i32_mul(m->e22,d.z)) >> FIXED_SHIFT;
    return x >= limit[0] && x <= limit[1] && y >= limit[2] && y <= limit[3] && z >= limit[4] && z <= limit[5];
}
void Object_collideDefault(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    if (!ItemObj_updateHitMask(self,lara,cinfo) || !cinfo->enemyPush) return;
    ItemObj_collidePush(self,lara,cinfo,false);
}
bool useSwitch(ItemObj *self, int32 t)
{
    if ((self->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INACTIVE) return false;
    self->flags &= ~ITEM_FLAG_STATUS;
    if (t > 0 && self->state == 0) {
        if (t != 1) t *= 30;
        self->timer = t;
        self->flags |= ITEM_FLAG_STATUS_ACTIVE;
    } else ItemObj_deactivate(self);
    return true;
}
bool useKey(ItemObj *self, ItemObj *lara)
{
    if ((self->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE || lara->extraL->weaponState != WEAPON_STATE_FREE) return false;
    self->flags = (self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INACTIVE;
    return true;
}
bool usePickup(ItemObj *self)
{
    if ((self->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE) return false;
    self->flags = (self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INACTIVE;
    return true;
}
static int32 rotateX(int32 x, int32 z, int32 s, int32 c)
{
    return i32_sub(i32_mul(x,c),i32_mul(z,s)) >> FIXED_SHIFT;
}
static int32 rotateZ(int32 x, int32 z, int32 s, int32 c)
{
    return i32_add(i32_mul(z,c),i32_mul(x,s)) >> FIXED_SHIFT;
}
bool ItemObj_collideBounds(const ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    AABBs a = *ItemObj_getBoundingBox(self,false);
    AABBs b = *ItemObj_getBoundingBox(lara,false);
    int32 dy = i32_sub(lara->pos.y,self->pos.y);
    if (a.maxY-b.minY <= dy || a.minY-b.maxY >= dy) return false;
    int32 dx=i32_sub(lara->pos.x,self->pos.x), dz=i32_sub(lara->pos.z,self->pos.z);
    int32 s,c;
    sincos(self->angle.y,s,c);
    int32 px=rotateX(dx,dz,s,c), pz=rotateZ(dx,dz,s,c), r=cinfo->radius;
    return px >= a.minX-r && px <= a.maxX+r && pz >= a.minZ-r && pz <= a.maxZ+r;
}
bool ItemObj_updateHitMask(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    self->hitMask=0;
    if (!ItemObj_collideBounds(self,lara,cinfo)) return false;
    self->hitMask=ItemObj_collideSpheres(self,lara);
    return self->hitMask != 0;
}
void ItemObj_collidePush(ItemObj *self, Lara *lara, CollisionInfo *cinfo, bool enemyHit)
{
    int32 dx=i32_sub(lara->pos.x,self->pos.x), dz=i32_sub(lara->pos.z,self->pos.z);
    int32 s,c;
    sincos(self->angle.y,s,c);
    int32 px=rotateX(dx,dz,s,c), pz=rotateZ(dx,dz,s,c);
    AABBs box=*ItemObj_getBoundingBox(self,false);
    int32 minX=box.minX-cinfo->radius, maxX=box.maxX+cinfo->radius;
    int32 minZ=box.minZ-cinfo->radius, maxZ=box.maxZ+cinfo->radius;
    if (px < minX || px > maxX || pz < minZ || pz > maxZ) return;
    enemyHit=enemyHit && cinfo->enemyHit && box.maxY-box.minY > 256;
    int32 ax=px-minX, bx=maxX-px, az=pz-minZ, bz=maxZ-pz;
    if (ax <= bx && ax <= az && ax <= bz) px-=ax;
    else if (bx <= ax && bx <= az && bx <= bz) px+=bx;
    else if (az <= ax && az <= bx && az <= bz) pz-=az;
    else pz+=bz;
    s=-s;
    lara->pos.x=i32_add(self->pos.x,rotateX(px,pz,s,c));
    lara->pos.z=i32_add(self->pos.z,rotateZ(px,pz,s,c));
    if (enemyHit) {
        int32 cx=(minX+maxX)>>1, cz=(minZ+maxZ)>>1;
        dx-=rotateX(cx,cz,s,c);
        dz-=rotateZ(cx,cz,s,c);
        lara->extraL->hitQuadrant=(uint16)(lara->angle.y-phd_atan(dz,dx)+ANGLE_180+ANGLE_45)>>14;
        lara->extraL->hitTimer=5;
    }
    int32 tmpAngle=cinfo->angle;
    cinfo->gapPos=-WALL;
    cinfo->gapNeg=-LARA_STEP_HEIGHT;
    cinfo->gapCeiling=0;
    CollisionInfo_setAngle(cinfo,phd_atan(lara->pos.z-cinfo->pos.z,lara->pos.x-cinfo->pos.x));
    /* Source invokes this object's collideRoom, not Lara's. */
    ItemObj_collideRoom(self,LARA_HEIGHT,0);
    CollisionInfo_setAngle(cinfo,tmpAngle);
    if (cinfo->type != CT_NONE) {
        lara->pos.x=cinfo->pos.x;
        lara->pos.z=cinfo->pos.z;
    } else {
        cinfo->pos=lara->pos;
        ItemObj_updateRoom(lara,-10);
    }
}
void inventory_openUse(Lara *lara, int32 type)
{
    game_inventory_queue_open(lara,INV_PAGE_USE,type);
}
bool Object_isPickup(int32 type)
{
    return (type >= ITEM_PISTOLS && type <= ITEM_MEDIKIT_BIG) ||
        (type >= ITEM_PUZZLE_1 && type <= ITEM_PUZZLE_4) || type == ITEM_LEADBAR ||
        (type >= ITEM_KEY_ITEM_1 && type <= ITEM_KEY_ITEM_4) || type == ITEM_SCION_PICKUP_DROP;
}
