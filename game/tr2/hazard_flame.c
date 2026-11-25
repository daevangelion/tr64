/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"

#include "game/common/object.h"

/* tr2-items-general, Campfire: the range is a horizontal disk plus
 * vertical range and animated pose, not a sphere or same-room test. */
static bool tr2CampfireInRange(const ItemObj *item, const Lara *lara)
{
    int64_t x = (int64_t)item->pos.x - lara->pos.x;
    int64_t z = (int64_t)item->pos.z - lara->pos.z;
    int64_t y = (int64_t)item->pos.y - lara->pos.y;
    if (x < -600 || x > 600 || z < -600 || z > 600 ||
        y < -3072 || y > 3072 || x*x + z*z > 600*600)
        return false;
    const AABBs *pose = ItemObj_getBoundingBox(lara, true);
    return y >= pose->minY && y <= pose->maxY + 100;
}

void game_tr2_update_campfire(ItemObj *item, const Lara *lara)
{
    bool triggered=Object_isActive(item);
    if (TR64_RULE(ROOM_EFFECTS))
        game_tr2_campfire_effect(item,triggered);
    if (triggered && tr2CampfireInRange(item,lara))
        ++gs->tr2->campfireRangeTicks;
}
