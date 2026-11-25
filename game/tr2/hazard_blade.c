/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"
#include "game/common/object.h"

/* Great Wall objects, Large rolling blade: I's swing blade supplies the
 * damage primitive, not its immediate contact or its pendulum states.
 * Clip speeds come from the authored bank; this controller only chooses
 * the goal, the facing flip, and the floor sample. */

static int32 floorAhead(const ItemObj *item, int32 distance)
{
    const FloorData *saved = gs->gLastFloorData;
    uint16 slant = gs->gLastFloorSlant;
    vec3i at = vec3i_add(item->pos, game_tr2_heading_step(item->angle.y, (int16)distance));
    Room *room = Room_getRoom(item->room, at.x, at.y, at.z);
    int32 floor = Sector_getFloor(Room_getSector(room, at.x, at.z), at.x, at.y, at.z);
    gs->gLastFloorData = saved;
    gs->gLastFloorSlant = slant;
    return floor;
}

/* Two presentation effects before the advance. Base Y is Lara's Y minus
 * 512. Draws are Z, Y, X even if the pool refuses the slot. Speed is
 * twice the incoming horizontal speed. No gameplay draw. State 1 does
 * not apply the 100. */
void game_tr2_rolling_harm(ItemObj *item, Lara *lara)
{
    if (item->state != 1 && item->hitMask) {
        int16 speed = wrap_i16(2 * item->hSpeed);
        Lara_hit(lara, 100, &item->pos, 0);
        for (unsigned i = 0; i < 2; ++i) {
            int32 dz = rand_draw(), dy = rand_draw(), dx = rand_draw();
            vec3i point = lara->pos;
            point.z = i32_add(point.z, 256 - (dz >> 6));
            point.y = i32_add(i32_sub(lara->pos.y, 512), 256 - (dy >> 6));
            point.x = i32_add(point.x, 256 - (dx >> 6));
            game_tr2_shot_effect(TR2_FX_BLOOD, lara->room, point, speed, lara->angle.y);
        }
    }
    item->hitMask = 0;
}

void game_tr2_rolling_update(ItemObj *item)
{
    unsigned started = item->state;
    if (item->state > 2)
        sys_error("Tomb Raider II: rolling blade: unknown state %u", item->state);
    /* Wake left goal 2. A later visit to state 1 asks to roll again.
     * A missing floor 1536 ahead, sampled before the step, asks to reverse. */
    if (item->state == 1)
        item->goalState = 2;
    else if (floorAhead(item, 1536) == WALL)
        item->goalState = 1;
    ItemObj_animProcess(item, true);
    if (started != 1 && item->state == 1)
        item->angle.y = wrap_i16((int32)item->angle.y + ANGLE_180);
    ItemObj_updateRoom(item, 0);
    /* From the rolling update on, height is the floor sample, including
     * the missing-floor sentinel. That sentinel does not itself stop it. */
    if (item->state == 2)
        item->pos.y = item->roomFloor;
}
