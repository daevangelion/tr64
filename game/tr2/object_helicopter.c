/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"

#include "game/common/object.h"

/* tr2-items-general, Helicopter: drift and sound, retirement, then
 * complete the animation and membership check even on the last tick. */
void game_tr2_update_helicopter(ItemObj *item, const Lara *lara)
{
    item->pos.z = i32_add(item->pos.z, 100);
    int64_t dz = (int64_t)item->pos.z - lara->pos.z;
    vec3i soundAt = vec3i_create(
        lara->pos.x + (((int64_t)item->pos.x - lara->pos.x) >> 2),
        lara->pos.y + (((int64_t)item->pos.y - lara->pos.y) >> 2),
        lara->pos.z + (dz >> 2));
    soundPlay(SND_HELICOPTER, &soundAt);
    bool retire = dz > 30720 || dz < -30720;
    if (retire) {
        ItemObj_deactivateBase(item);
        Room_remove(item->room, item);
        item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INVISIBLE | TR2_FLAG_RETIRED;
    }
    ItemObj_animProcess(item, true);
    Room *room = Room_getRoom(item->room, item->pos.x, item->pos.y, item->pos.z);
    if (retire)
        item->room = room; /* Keep final membership without relinking a retired placement. */
    else if (room != item->room) {
        Room_remove(item->room, item);
        Room_add(room, item);
    }
}
