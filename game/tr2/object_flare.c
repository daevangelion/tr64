/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "flare.h"

void game_tr2_update_world_flare(ItemObj *item)
{
    vec3i old = item->pos;
    Room *room = item->room;
    if (item->vSpeed) {
        item->angle.x = wrap_i16(item->angle.x + 546);
        item->angle.z = wrap_i16(item->angle.z + 910);
    } else
        item->angle.x = item->angle.z = 0;
    const vec3i step = game_tr2_heading_step(item->angle.y, item->hSpeed);
    item->pos.x = i32_add(old.x, step.x);
    item->pos.z = i32_add(old.z, step.z);
    if (tr64_u16(room->info->flags) & ROOM_FLAG_WATER_MASK) {
        item->hSpeed += (5 - item->hSpeed) / 2;
        item->vSpeed += (5 - item->vSpeed) / 2;
    } else
        item->vSpeed = wrap_i16(item->vSpeed + 6);
    item->pos.y = i32_add(old.y, item->vSpeed);
    room = Room_getRoom(room, item->pos.x, item->pos.y, item->pos.z);
    const Sector *cell = Room_getSector(room, item->pos.x, item->pos.z);
    int32 floor = Sector_getFloor(cell, item->pos.x, item->pos.y, item->pos.z);
    if (item->pos.y >= floor) {
        if (old.y > floor) {
            item->pos = old;
            item->angle.y = wrap_i16(item->angle.y + 32768u);
            item->hSpeed /= 2;
            room = item->room;
        } else {
            item->pos.y = floor;
            if (item->vSpeed <= 40) {
                item->vSpeed = 0;
                item->hSpeed = item->hSpeed > 3 ? item->hSpeed - 3 : 0;
            } else
                item->vSpeed = item->vSpeed > 140 ? -100 : 40 - item->vSpeed;
        }
    } else {
        int32 ceiling = Sector_getCeiling(cell, item->pos.x, item->pos.y, item->pos.z);
        if (item->pos.y < ceiling) {
            item->pos.y = ceiling;
            item->vSpeed = wrap_i16(-(int32)item->vSpeed);
        }
    }
    if (room != item->room) {
        Room_remove(item->room, item);
        Room_add(room, item);
    }
    if (item->headOffset < FLARE_BURN_END) {
        item->aggression = flareLight(++item->headOffset);
        if (item->aggression && (tr64_u16(room->info->flags) & ROOM_FLAG_WATER_MASK))
            soundPlay(12, &item->pos);
    } else if (!item->hSpeed && !item->vSpeed) {
        ItemObj_deactivateBase(item);
        Room_remove(room, item);
        item->room = NULL;
        item->nextItem = gs->firstFree;
        gs->firstFree = item;
    }
}
