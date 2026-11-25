/* SPDX-License-Identifier: MIT */
/* Merge review control: run door-closed and force only the accepted tick-14
 * push's destination room. Check the two creature visits before any footer. */
#include "game/common/internal.h"
#include <stdio.h>

static Room *start, *adjacent;
static unsigned visits[2], count;
static bool inDoor, resolvedDestination;
void __real_Lara_updateObjectsCollision(Lara *);
bool __real_game_tr2_interact(ItemObj *, Lara *);
Room *__real_Room_getRoom(Room *, int32, int32, int32);
void __real_game_tr2_creature_contact(ItemObj *, Lara *);

static void move(ItemObj *item, Room *room)
{
    Room_remove(item->room, item);
    Room_add(room, item);
}

void __wrap_Lara_updateObjectsCollision(Lara *lara)
{
    if (gs->tick != 13) {
        __real_Lara_updateObjectsCollision(lara);
        return;
    }
    start = lara->room;
    unsigned first = tr64_u32(start->info->portal_start);
    unsigned portals = tr64_u32(start->info->portal_count);
    for (unsigned i = 0; i < portals && !adjacent; ++i) {
        Room *room = gs->rooms + tr64_u16(tr64_level_portals(gs->core, first+i)->room);
        if (room != start)
            adjacent = room;
    }
    if (!adjacent || !tr2DoorType(gs->items[3].type))
        sys_error("mixed contact control requires the Great Wall door-closed fixture");
    for (unsigned i = 0; i < 2; ++i) {
        ItemObj *item = gs->items + 18 + i;
        if (!game_tr2_creature_type(item->type))
            sys_error("mixed contact control requires two placed creatures");
        item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_COLLISION | ITEM_FLAG_STATUS_ACTIVE;
        item->pos = lara->pos;
        item->pos.z += 1800;
        move(item, i ? adjacent : start);
    }
    /* The door must be followed by Lara, then the old-room creature. */
    move(lara, start);
    move(gs->items+3, start);
    __real_Lara_updateObjectsCollision(lara);
    bool transferred = resolvedDestination && lara->room == adjacent;
    printf("Mixed scan control: crossed=%d visits=%u first=%u second=%u\n",
           transferred, count, visits[0], visits[1]);
    exit(!transferred || count != 2 || visits[0] != 18 || visits[1] != 19);
}

bool __wrap_game_tr2_interact(ItemObj *item, Lara *lara)
{
    inDoor = start && item == gs->items+3;
    bool result = __real_game_tr2_interact(item, lara);
    inDoor = false;
    return result;
}

Room *__wrap_Room_getRoom(Room *room, int32 x, int32 y, int32 z)
{
    const ItemObj *lara = gs ? gs->players[0] : NULL;
    if (inDoor && lara && room == start && x == 49310 && y == lara->pos.y-10 && z == lara->pos.z) {
        resolvedDestination = true;
        return adjacent;
    }
    return __real_Room_getRoom(room, x, y, z);
}

void __wrap_game_tr2_creature_contact(ItemObj *item, Lara *lara)
{
    if (start && (item == gs->items+18 || item == gs->items+19)) {
        if (count < sizeof(visits)/sizeof(*visits))
            visits[count] = item-gs->items;
        ++count;
    }
    __real_game_tr2_creature_contact(item, lara);
}
