/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"
#include "game/common/weapon.h"
#include "game/common/enemy.h"

/* tr2-great-wall-creatures, Collision after the creature turns. */
static const Sector *sectorAt(Room **room,const vec3i *pos, int32 y)
{
    return game_tr2_room_sector(room,pos->x,y,pos->z);
}

static int16 sectorBox(const Sector *sector)
{
    return sector->boxIndex==UINT16_MAX ? -1 : sector->boxIndex;
}

static int32 floorOf(int box)
{
    return tr64_i16(gs->level.boxes[box].floor);
}

static bool terrainFits(const TR2Creature *ai,int16 box,int16 from)
{
    if (box<0 || from<0 || gs->boxFlags[box]&ai->blockMask)
        return false;
    const int32 rise=floorOf(from)-floorOf(box);
    return rise>=ai->drop && rise<=ai->step;
}

static bool marginFits(const TR2Creature *ai,Room *room,vec3i p,int32 top,int16 here)
{
    int16 probe=sectorBox(sectorAt(&room,&p,top));
    if (!terrainFits(ai,probe,here)) return false;
    int16 next=ai->route[here].link;
    int32 floor=floorOf(probe);
    return !(floor-floorOf(here)>ai->step && floor>floorOf(next>=0?next:here)) &&
           !(ai->fly && top>floor+ai->fly);
}

static int radiusFor(unsigned type)
{
    return type==TR2_ITEM_SPIDER ||
           (type>=TR2_ITEM_GUNMAN_1 && type<=TR2_ITEM_GUNMAN_3) ||
           type==TR2_ITEM_OPERA_BOSS ||
           type==TR2_ITEM_RAT || type==TR2_ITEM_THUG
           ? 102 : type==TR2_ITEM_CROW ? 204 : 341;
}

static int32 corner(int32 old,int32 proposedTile,int32 actual)
{
    int32 tile=old>>10;
    return proposedTile<tile ? old&~1023 : proposedTile>tile ? old|1023 : actual;
}

static void moveRoom(ItemObj *item,Room *room)
{
    if (room!=item->room) {
        Room_remove(item->room,item);
        Room_add(room,item);
    }
}

/* Q-T2K-08: the pose-top floor sample and the final room lookup
 * are different probes. Pitch follows only the retained steering step. */
static void fly(TR2Creature *ai,Room *room,vec3i old,int32 top)
{
    ItemObj *item=ai->item;
    vec3i *p=&item->pos;
    int32 step=i32_sub(ai->waypoint[1],p->y);
    step=X_MAX(-ai->fly,X_MIN(ai->fly,step));
    const Sector *s=sectorAt(&room,p,top);
    int32 floor=Sector_getFloor(s,p->x,top,p->z);
    if (i32_add(p->y,step)>floor) {
        if (p->y>floor) {
            p->x=old.x; p->z=old.z;
            s=sectorAt(&room,p,top);
            floor=Sector_getFloor(s,p->x,top,p->z);
            step=-ai->fly;
        } else {
            p->y=floor;
            step=0;
        }
    } else {
        const int32 ceiling=Sector_getCeiling(s,p->x,top,p->z);
        if (i32_add(top,step)<ceiling) {
            if (top<ceiling) {
                p->x=old.x; p->z=old.z;
                s=sectorAt(&room,p,top);
                floor=Sector_getFloor(s,p->x,top,p->z);
                step=ai->fly;
            } else step=0;
        }
    }
    p->y=i32_add(p->y,step);
    item->roomFloor=floor;
    moveRoom(item,Room_getRoom(room,p->x,i32_sub(p->y,512),p->z));
    int32 pitch=item->hSpeed ? wrap_i16(game_tr2_bearing(item->hSpeed,-step)) : 0;
    item->angle.x=angleLerp(item->angle.x,X_MAX(-3640,X_MIN(3640,pitch)),182);
}

static bool spiderFloor(ItemObj *item, const vec3i *old, int32 floor)
{
    if (!(item->flags & ITEM_FLAG_GRAVITY) && old->y - floor >= 384) {
        item->pos = *old;
        ItemObj_animSet(item, gs->level.models[item->type].animIndex + TR2_SPIDER_LEAP_CLIP, true, 0);
        return true;
    }
    return false;
}

static bool thugFloor(ItemObj *item, const vec3i *old, int32 floor)
{
    enum {
        THUG_DIE = 11, THUG_CLIMB512 = 12, THUG_DROP1024 = 15,
        THUG_CLIMB1024_CLIP = 27, THUG_CLIMB512_CLIP = 28,
        THUG_CLIMB768_CLIP = 29, THUG_DROP1024_CLIP = 30
    };
    if (item->health <= 0 || item->state == THUG_DIE ||
        (item->state >= THUG_CLIMB512 && item->state <= THUG_DROP1024))
        return false;
    int32 oldX = old->x >> 10, oldZ = old->z >> 10;
    int32 newX = item->pos.x >> 10, newZ = item->pos.z >> 10;
    if (newX == oldX && newZ == oldZ)
        return false;
    int32 rise = i32_sub(old->y, floor);
    int32 drop = i32_sub(floor, old->y);
    unsigned clip = UINT16_MAX;
    if (rise >= 384) {
        clip = rise < 640 ? THUG_CLIMB512_CLIP :
               rise < 896 ? THUG_CLIMB768_CLIP : THUG_CLIMB1024_CLIP;
    } else if (drop > 896) {
        clip = THUG_DROP1024_CLIP;
    }
    if (clip == UINT16_MAX)
        return false;
    bool dx = newX != oldX, dz = newZ != oldZ;
    if (dx != dz) {
        if (dx) {
            if (newX > oldX) {
                item->angle.y = ANGLE_90;
                item->pos.x = i32_sub(newX * 1024, 260);
            } else {
                item->angle.y = -ANGLE_90;
                item->pos.x = i32_add(oldX * 1024, 260);
            }
        } else {
            if (newZ > oldZ) {
                item->angle.y = 0;
                item->pos.z = i32_sub(newZ * 1024, 260);
            } else {
                item->angle.y = -ANGLE_180;
                item->pos.z = i32_add(oldZ * 1024, 260);
            }
        }
        item->pos.y = old->y;
    } else {
        item->pos = *old;
    }
    ItemObj_animSet(item, gs->level.models[item->type].animIndex + clip, true, 0);
    return true;
}

void game_tr2_creature_stride(TR2Creature *ai,int16 turn)
{
    ItemObj *item=ai->item;
    vec3i old=item->pos;
    ItemObj_animProcess(item,true);
    /* A crow's last death advance moves, but does not probe or steer. */
    if (ai->fly && (item->flags&ITEM_FLAG_STATUS)==ITEM_FLAG_STATUS_INACTIVE) return;
    const AABBs *pose=ItemObj_getBoundingBox(item,ai->fly!=0);
    vec3i *p=&item->pos;
    int32 yTop=i32_add(p->y,pose->minY);
    vec3i oldHorizontal={.x=old.x,.y=p->y,.z=old.z};
    Room *room=item->room;
    int16 oldBox=sectorBox(sectorAt(&room,&oldHorizontal,yTop));
    int16 box=sectorBox(sectorAt(&room,p,yTop));
    const tr64_u16_be_t *zones=game_tr2_creature_zones(ai,gs->gSaveGame.flipped);
    if (!terrainFits(ai,box,oldBox) || zones[box].raw!=zones[oldBox].raw) {
        const int32 proposedX=p->x>>10;
        p->x=corner(old.x,proposedX,p->x);
        p->z=corner(old.z,proposedX,p->z);
        box=sectorBox(sectorAt(&room,p,yTop));
    }
    int32 radius=radiusFor(item->type);
    if (box>=0) {
        int32 x=p->x&1023,z=p->z&1023;
        int32 sx=x<radius?-1:x>1024-radius?1:0;
        int32 sz=z<radius?-1:z>1024-radius?1:0;
        vec3i across=*p, along=*p;
        across.x=i32_add(across.x,sx*radius);
        along.z=i32_add(along.z,sz*radius);
        bool bx=sx && !marginFits(ai,room,across,yTop,box);
        bool bz=sz && !marginFits(ai,room,along,yTop,box);
        if (sx && sz && !bx && !bz) {
            across.z=along.z;
            if (!marginFits(ai,room,across,yTop,box)) {
                int32 yaw=item->angle.y;
                bool wide=sx==sz ? (yaw>=-24575 && yaw<=8191) : (yaw>=-8191 && yaw<=24575);
                bz=sz<0 ? wide : !wide;
                bx=!bz;
            }
        }
        if (bx) p->x=i32_add(p->x&~1023,sx<0?radius:1024-radius);
        if (bz) p->z=i32_add(p->z&~1023,sz<0?radius:1024-radius);
        if (bx || bz) item->angle.y=wrap_i16((int32)item->angle.y+turn);
    }
    for (ItemObj *peer=item->room->firstItem;peer && peer!=item;peer=peer->nextItem) {
        if (peer->type==ITEM_LARA || !peer->hSpeed || !(peer->flags&ITEM_FLAG_ACTIVE)) continue;
        int64_t x=(int64_t)p->x-peer->pos.x,y=(int64_t)p->y-peer->pos.y,z=(int64_t)p->z-peer->pos.z;
        if (llabs(x)<radius && llabs(y)<radius && llabs(z)<radius && x*x+y*y+z*z<radius*radius) {
            *p=old;
            return;
        }
    }
    if (ai->fly) {
        fly(ai,room,old,yTop);
        return;
    }
    const Sector *s=sectorAt(&room,p,p->y);
    int32 floor=Sector_getFloor(s,p->x,p->y,p->z);
    item->roomFloor=floor;
    if (floor!=WALL) {
        if (item->type==TR2_ITEM_SPIDER && spiderFloor(item,&old,floor))
            return;
        if (item->type==TR2_ITEM_THUG && thugFloor(item,&old,floor))
            return;
        if (p->y>floor) p->y=floor;
        else p->y=i32_add(p->y,X_MIN(64,floor-p->y));
    }
    moveRoom(item,Room_getRoom(room,p->x,i32_sub(p->y,512),p->z));
}

/* tr2-great-wall-creatures, Contact pose and timing, Body push and hit reaction. */
void game_tr2_creature_contact(ItemObj *item,Lara *lara)
{
    AABBs box=*ItemObj_getBoundingBox(item,false);
    AABBs body=*ItemObj_getBoundingBox(lara,false);
    int32 dx=i32_sub(lara->pos.x,item->pos.x), dz=i32_sub(lara->pos.z,item->pos.z);
    int32 y=i32_sub(lara->pos.y,item->pos.y);
    int32 s=tr2_sin(item->angle.y),c=tr2_sin(item->angle.y+ANGLE_90);
    int32 x=((int64_t)dx*c-(int64_t)dz*s)>>14;
    int32 z=((int64_t)dx*s+(int64_t)dz*c)>>14;
    int r=gs->cinfo.radius;
    item->hitMask=0;
    if (y+body.maxY<=box.minY || y+body.minY>=box.maxY || x<box.minX-r || x>box.maxX+r ||
        z<box.minZ-r || z>box.maxZ+r) return;
    item->hitMask=game_tr2_creature_touch(item,lara);
    if (!gs->cinfo.enemyPush || !item->hitMask || x<box.minX || x>box.maxX || z<box.minZ || z>box.maxZ)
        return;
    int32 left=x-box.minX,right=box.maxX-x,high=box.maxZ-z,low=z-box.minZ;
    int32 best=X_MIN(X_MIN(left,right),X_MIN(high,low));
    if (gs->cinfo.enemyHit && box.maxY-box.minY>256) {
        int32 mx=(box.minX+box.maxX)/2,mz=(box.minZ+box.maxZ)/2;
        int32 facing=game_tr2_bearing(dz-((mz*c-mx*s)>>14),dx-((mx*c+mz*s)>>14));
        ExtraInfoLara *e=lara->extraL;
        e->hitQuadrant=(uint16)(lara->angle.y-facing+40960u)>>14;
        if (!e->hitFrame) soundPlay(31,&lara->pos);
        if (e->hitFrame<34) ++e->hitFrame;
    }
    if (best==left) x=box.minX;
    else if (best==right) x=box.maxX;
    else if (best==high) z=box.maxZ;
    else z=box.minZ;
    lara->pos.x=i32_add(item->pos.x,(x*c+z*s)>>14);
    lara->pos.z=i32_add(item->pos.z,(z*c-x*s)>>14);
    CollisionInfo old=gs->cinfo;
    int16 angle=wrap_i16(game_tr2_bearing(i32_sub(lara->pos.z,old.pos.z),i32_sub(lara->pos.x,old.pos.x)));
    gs->cinfo.angle=angle;
    gs->cinfo.quadrant=((uint16)angle+8192u)>>14 & 3;
    gs->cinfo.gapPos=-WALL; gs->cinfo.gapNeg=-384; gs->cinfo.gapCeiling=0;
    ItemObj_collideRoom(lara,LARA_HEIGHT,0);
    if (gs->cinfo.type!=CT_NONE) lara->pos=old.pos;
    else old.pos=lara->pos;
    gs->cinfo=old;
}
