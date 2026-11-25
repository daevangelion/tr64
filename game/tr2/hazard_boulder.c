/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"
#include "game/common/object.h"


/* Great Wall boulders: the admitted release-free route exercises the
 * authored roll and the linked floor following, not the final stop. */
/* Return a one-based Core floor word index, not a pointer into the unit. */
uint32 game_tr2_boulder_update(ItemObj *item, int32 listener_x, int32 listener_z)
{
    if (!Object_isActive(item))
        game_tr2_refuse_interaction(item,"release/reset");
    if (item->state == 0 && item->pos.y >= item->roomFloor)
        item->goalState = 1;
    else if (item->pos.y < item->roomFloor && !(item->flags & ITEM_FLAG_GRAVITY)) {
        item->flags |= ITEM_FLAG_GRAVITY;
        item->vSpeed = -10;
    }
    item->hitMask = 0;
    /* Great Wall boulders, motion: a higher floor or a missing floor 384
     * ahead stops the ball at the horizontal position it began this tick.
     * The stride is not taken. Speeds and the touch cleared above stay clear. */
    vec3i ahead = vec3i_add(item->pos, game_tr2_heading_step(item->angle.y, 384));
    Room *aheadRoom = Room_getRoom(item->room, ahead.x, ahead.y, ahead.z);
    int32 aheadFloor = Sector_getFloor(Room_getSector(aheadRoom, ahead.x, ahead.z),
                                       ahead.x, ahead.y, ahead.z);
    if (aheadFloor == WALL || aheadFloor < item->pos.y) {
        ItemObj_updateRoom(item, 0);
        if (item->roomFloor == WALL)
            game_tr2_refuse_interaction(item, "missing supporting floor");
        item->pos.y = item->roomFloor;
        item->hSpeed = 0;
        item->vSpeed = 0;
        item->flags &= ~ITEM_FLAG_GRAVITY;
        const FloorData *stopped = gs->gLastFloorData;
        return stopped ? 1u + (uint32)(stopped - gs->level.floors) : 0;
    }
    ItemObj_animProcess(item,true);
    ItemObj_updateRoom(item,0);
    if (item->roomFloor == WALL)
        game_tr2_refuse_interaction(item,"missing supporting floor");
    const FloorData *trigger = gs->gLastFloorData;
    if (item->pos.y >= i32_sub(item->roomFloor,256)) {
        item->pos.y = item->roomFloor;
        item->vSpeed = 0;
        item->flags &= ~ITEM_FLAG_GRAVITY;
        soundPlay(147,&item->pos);
        Camera *cam = &gs->playersExtra[0].camera;
        /* tr2-great-wall-objects, Shake ordering and rounding: the
         * squared X and Z differences are summed at 32 bits. A
         * separation of 65536 therefore wraps to the listener shake. */
        uint32 dx = (uint32)listener_x - (uint32)item->pos.x;
        uint32 dz = (uint32)listener_z - (uint32)item->pos.z;
        uint32 d = phd_sqrt(dx*dx + dz*dz);
        if (d < 10240)
            cam->bounce = -(int32)(40*(10240-d)/10240);
    }
    return trigger ? 1u + (uint32)(trigger - gs->level.floors) : 0;
}

/* Great Wall boulders, Q-HAZ-01: the airborne spray follows any solid
 * displacement. The grounded burst takes 60 gameplay draws even with
 * a full effects pool; the camera applies the existing shake later. */
void game_tr2_boulder_contact(ItemObj *item, Lara *lara)
{
    const bool active=(item->flags&ITEM_FLAG_STATUS)==ITEM_FLAG_STATUS_ACTIVE;
    const bool airborne=(lara->flags&ITEM_FLAG_GRAVITY)!=0;
    if (game_tr2_solid_contact(item,lara,!active || airborne,false)==TR2_SOLID_MISS || !active)
        return;
    if (airborne) {
        Lara_hit(lara,100,&item->pos,0);
        vec3i d=vec3i_sub(lara->pos,item->pos);
        d.y=i32_add(d.y,162);
        uint32 square=(uint32)d.x*d.x+(uint32)d.y*d.y+(uint32)d.z*d.z;
        int32 distance=phd_sqrt(square);
        if (distance<512)
            distance=512;
        vec3i point=vec3i_add(item->pos,vec3i_create(d.x*512/distance,
            i32_sub(d.y*512/distance,512),d.z*512/distance));
        game_tr2_shot_effect(TR2_FX_BLOOD,item->room,point,item->hSpeed,item->angle.y);
        return;
    }
    if (lara->health<=0)
        return;
    lara->health=-1;
    /* Inherited grounded-kill mark: objects-missing, Rolling ball. */
    lara->flags|=ITEM_FLAG_INJURED;
    lara->angle.x=lara->angle.z=0;
    lara->angle.y=item->angle.y;
    ItemObj_animSet(lara,Lara_ANIM_DEATH_BOULDER,true,0);
    Camera *camera=&lara->extraL->camera;
    camera->targetAngle.y=30940;
    camera->targetAngle.x=-4550;
    camera->center=true;
    for (unsigned i=0;i<15;++i) {
        vec3i point=lara->pos;
        point.x=i32_add(point.x,(rand_logic()-16384)/256);
        point.z=i32_add(point.z,(rand_logic()-16384)/256);
        point.y=i32_sub(point.y,rand_logic()>>6);
        int16 yaw=wrap_i16(item->angle.y+(rand_logic()-16384)/8);
        game_tr2_shot_effect(TR2_FX_BLOOD,item->room,point,wrap_i16(2*item->hSpeed),yaw);
    }
}
