/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a (reach and alignment primitives). */
#include "objects.h"
#include "game/common/object.h"

/* Great Wall objects, nearest-face push. Four edges extended by 100.
 * tr2-great-wall-objects.md:119: an equal face uses the door order,
 * lower X, higher X, higher Z, lower Z. Across is the X pair. */
void game_tr2_nearest_face(ItemObj *item, Lara *lara)
{
    AABBs box = *ItemObj_getBoundingBox(item, false);
    vec3i d = vec3i_sub(lara->pos, item->pos);
    int32 s = tr2_sin(item->angle.y);
    int32 c = tr2_sin(item->angle.y + ANGLE_90);
    int32 across = q14((int64_t)c * d.x - (int64_t)s * d.z);
    int32 along = q14((int64_t)c * d.z + (int64_t)s * d.x);
    int32 minX = box.minX - 100, maxX = box.maxX + 100;
    int32 minZ = box.minZ - 100, maxZ = box.maxZ + 100;
    int32 dx0, dx1, dz0, dz1;
    Room *room;
    if (across < minX || across > maxX || along < minZ || along > maxZ)
        return;
    dx0 = across - minX;
    dx1 = maxX - across;
    dz1 = maxZ - along;
    dz0 = along - minZ;
    if (dx0 <= dx1 && dx0 <= dz1 && dx0 <= dz0) across = minX;
    else if (dx1 <= dz1 && dx1 <= dz0) across = maxX;
    else if (dz1 <= dz0) along = maxZ;
    else along = minZ;
    lara->pos.x = i32_add(item->pos.x, q14((int64_t)c * across + (int64_t)s * along));
    lara->pos.z = i32_add(item->pos.z, q14((int64_t)c * along - (int64_t)s * across));
    gs->cinfo.pos = lara->pos;
    room = Room_getRoom(lara->room, lara->pos.x, i32_sub(lara->pos.y, 10), lara->pos.z);
    if (room != lara->room) {
        Room_remove(lara->room, lara);
        Room_add(room, lara);
    }
}
