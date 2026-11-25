/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"

/* Great Wall Objects, Zip line: Lara and the handle run independent
 * authored lead-ins. The room visit itself decides reach and eligibility. */
void game_tr2_mount_zipline(ItemObj *item, Lara *lara)
{
    if (item->flags&ITEM_FLAG_ACTIVE)
        return;
    if (item->state!=1)
        game_tr2_refuse_interaction(item,"reuse at the endpoint");
    lara->pos=vec3i_add(item->pos,game_tr2_heading_step(item->angle.y,371));
    lara->angle=item->angle;
    ItemObj_animSet(lara,TR2_LARA_ZIP_MOUNT,true,0);
    lara->goalState=Lara_STATE_ZIPLINE;
    lara->extraL->weaponState=WEAPON_STATE_BUSY;
    item->flags|=TR2_ZIP_RIDE_MARK;
    game_tr2_activate_item(item);
}

static void moveRoom(ItemObj *item, Room *room)
{
    if (item->room!=room) {
        Room_remove(item->room,item);
        Room_add(room,item);
    }
}

static void returnToStart(ItemObj *item)
{
    const tr64_core_items_t *start=tr64_level_items(gs->core,item-gs->items);
    Room *room=gs->rooms+tr64_u16(start->room);
    item->pos=vec3i_create(tr64_i32(start->x),tr64_i32(start->y),tr64_i32(start->z));
    item->angle.y=tr64_i16(start->angle);
    item->hSpeed=item->vSpeed=0;
    ItemObj_animSet(item,gs->level.models[item->type].animIndex,true,0);
    moveRoom(item,room);
    ItemObj_deactivateBase(item);
}

void game_tr2_zipline_update(ItemObj *item, Lara *lara)
{
    if (!(item->flags&TR2_ZIP_RIDE_MARK)) {
        returnToStart(item);
        return;
    }
    if (item->state!=2) {
        ItemObj_animProcess(item,true);
        return;
    }
    ItemObj_animProcess(item,false);
    const int16 speed=item->vSpeed<95 ? item->vSpeed+5 : 100;
    const int16 descent=speed>>2;
    item->vSpeed=speed;
    item->pos=vec3i_add(item->pos,game_tr2_heading_step(item->angle.y,speed));
    item->pos.y=i32_add(item->pos.y,descent);
    moveRoom(item,Room_getRoom(item->room,item->pos.x,item->pos.y,item->pos.z));
    const bool riding=lara->state==Lara_STATE_ZIPLINE;
    if (riding)
        lara->pos=item->pos;
    vec3i next=vec3i_add(item->pos,game_tr2_heading_step(item->angle.y,1024));
    next.y=i32_add(next.y,64);
    Room *room=item->room;
    const Sector *sector=game_tr2_room_sector(&room,next.x,next.y,next.z);
    int32 floor=Sector_getFloor(sector,next.x,next.y,next.z);
    int32 ceiling=Sector_getCeiling(sector,next.x,next.y,next.z);
    if (floor!=WALL && floor>i32_add(item->pos.y,320) &&
        ceiling<i32_sub(item->pos.y,192)) {
        soundPlay(280,&item->pos);
        return;
    }
    if (riding)
        game_tr2_zipline_release(lara,speed,descent);
    soundPlay(281,&item->pos);
    item->flags&=~TR2_ZIP_RIDE_MARK;
    ItemObj_deactivateBase(item);
}
