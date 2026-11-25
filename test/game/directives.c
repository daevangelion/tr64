/* SPDX-License-Identifier: MIT */
/* Host-only setup directives `give TYPE COUNT`, `item INDEX HEALTH` and `activate INDEX TIMER`, the native
 * side of the oracle's directives of the same names (test/oracle/oracle.cpp).
 * Not linked into the cart or any shipping build: select it only through
 * TR64_SCENE_TEST_SOURCES, beside test/game/save-io.c.
 * `give` calls the real inventory_addPickup (source Inventory::addPickup) COUNT
 * times; `item` writes one item's health and nothing else; `activate` does what
 * an ACTIVATE_OBJECT trigger action does to the item (every mask bit set, timer
 * field TIMER). Paths reached through
 * them are placed-state evidence, not play-reached evidence. */
#ifdef TR64_HOST
#include "game/internal.h"

int tr64_game_test_give(int32_t type, int32_t count)
{
    int32_t i;
    if (!gs || !gs->players[0] || type < 0 || type >= ITEM_MAX || count < 1 || count > 1000)
        return 0;
    for (i = 0; i < count; ++i)
        if (!inventory_addPickup((ItemType)type))
            return 0;
    return 1;
}

int tr64_game_test_item_health(int32_t index, int32_t health)
{
    if (!gs || index < 0 || (uint32_t)index >= gs->level.itemsCount ||
        health < -32768 || health > 32767)
        return 0;
    gs->items[index].health = (int16_t)health;
    return 1;
}

int tr64_game_test_item_activate(int32_t index, int32_t timer)
{
    ItemObj *item;
    if (!gs || index < 0 || (uint32_t)index >= gs->level.itemsCount || timer < 0 || timer > 127)
        return 0;
    item = gs->items + index;
    if (item->flags & ITEM_FLAG_ONCE)
        return 0;
    item->timer = timer;
    if (item->timer != 1)
        item->timer *= 30;
    item->flags |= ITEM_FLAG_MASK;
    if (!(item->flags & ITEM_FLAG_ACTIVE))
        worldTriggerActivate(item);
    return 1;
}
int tr64_game_test_player_health(unsigned slot,int32_t health)
{
    if (!gs || slot>=TR64_GAME_PLAYERS || !gs->players[slot] ||
        health<INT16_MIN || health>INT16_MAX)
        return 0;
    gs->players[slot]->health=(int16_t)health;
    return 1;
}
int tr64_game_test_weapon_hit(unsigned slot,int32_t index,int32_t damage)
{
    if (!gs || slot>=TR64_GAME_PLAYERS || !gs->players[slot] || gs->players[slot]->health<=0 ||
        index<0 || (uint32_t)index>=gs->level.itemsCount || damage<1 || damage>1000)
        return 0;
    ItemObj *target=gs->items+index;
    return Lara_applyWeaponHit(gs->players[slot],target,damage,&target->pos);
}
int tr64_game_test_heavy(unsigned room,int32_t x,int32_t y,int32_t z)
{
    if (!gs || room>=gs->level.roomsCount)return 0;
    Room *r=gs->rooms+room;
    Sector_getFloor(Room_getSector(r,x,z),x,y,z);
    game_check_trigger(gs->gLastFloorData,NULL);
    return 1;
}
int tr64_game_test_flip(void)
{
    worldRequestFlipToggle();
    return worldApplyPendingFlip();
}
int tr64_game_test_pit(unsigned room,int32_t x,int32_t z,int32_t steps)
{
    if (!gs || room>=gs->level.roomsCount || steps<=0 || steps>127)return 0;
    Sector *s=(Sector *)Room_getSector(gs->rooms+room,x,z);
    if (s->floor==NO_FLOOR || s->floor+steps>127)return 0;
    s->floor+=steps;
    return 1;
}
#endif
