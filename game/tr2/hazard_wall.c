/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_spikes.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"
#include "game/common/object.h"

/* Great Wall objects, Moving spike walls: I has no moving wall. Stationary
 * spikes supply the 4-draw blood shape; the cited differences are the
 * 16-unit step, the permanent status-2 stop, and harm on the next update. */

static int32 floorAt(Room *room, int32 x, int32 y, int32 z)
{
    const FloorData *saved = gs->gLastFloorData;
    uint16 slant = gs->gLastFloorSlant;
    Room *at = Room_getRoom(room, x, y, z);
    int32 floor = Sector_getFloor(Room_getSector(at, x, z), x, y, z);
    gs->gLastFloorData = saved;
    gs->gLastFloorSlant = slant;
    return floor;
}

/* tr2-great-wall-objects.md:127. Three effects from Lara's X/Z and Y
 * minus 512, in her room, at the wall's yaw and speed 1. Draws are Z,
 * Y, X. Sound 205 then spends three presentation draws. */
void game_tr2_spike_wall_harm(ItemObj *item, Lara *lara)
{
    vec3i base;
    if (!item->hitMask)
        return;
    item->hitMask = 0;
    Lara_hit(lara, 20, &item->pos, 0);
    base = lara->pos;
    base.y = i32_sub(base.y, 512);
    for (unsigned i = 0; i < 3; ++i) {
        vec3i point = base;
        point.z = i32_add(point.z, 256 - rand_draw() / 64);
        point.y = i32_add(point.y, 256 - rand_draw() / 64);
        point.x = i32_add(point.x, 256 - rand_draw() / 64);
        game_tr2_shot_effect(TR2_FX_BLOOD, lara->room, point, 1, item->angle.y);
    }
    (void)rand_draw();
    (void)rand_draw();
    (void)rand_draw();
    soundPlay(205, &lara->pos);
}

void game_tr2_spike_wall_update(ItemObj *item)
{
    vec3i step, next;
    int32 here, ahead;
    if (item->state != 0)
        sys_error("Tomb Raider II: spiked wall: unknown state %u", item->state);
    if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        return;
    /* Sixteen along the facing. Stored horizontal speed stays 0, and the
     * clip stays on frame 0. A different floor ahead stops at this position. */
    step = game_tr2_heading_step(item->angle.y, 16);
    next = vec3i_add(item->pos, step);
    here = floorAt(item->room, item->pos.x, item->pos.y, item->pos.z);
    ahead = floorAt(item->room, next.x, next.y, next.z);
    if (ahead != here) {
        item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INACTIVE;
        return;
    }
    item->pos = next;
    ItemObj_updateRoom(item, 0);
}
