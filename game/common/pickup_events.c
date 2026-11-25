/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h and src/lara.h, src/ui.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

void game_pickup_notify(const ItemObj *item,const Lara *lara)
{
    /* Fixed pickups are removed/hidden, not freed/recollected. Consequently at
     * most MAX_ITEMS successful collections can occur in one simulation tick.
     * Copy the origin now: the UI never recovers an event from later GS diffs. */
    ASSERT(gs->pickupCount<MAX_ITEMS);
    uint32 index=gs->pickupCount++;
    gs->pickupType[index]=(uint16)(item->type | (Lara_slot(lara)<<8));
    gs->pickupPosition[index]=item->pos;
    gs->pickupTick=gs->tick+1u;
}
uint32_t game_pickup_event_count(void)
{
    return gs ? gs->pickupCount : 0;
}
int game_pickup_event(uint32_t index,tr64_game_pickup_event_t *out)
{
    if(!gs || !out || index>=gs->pickupCount)return 0;
    const vec3i *p=gs->pickupPosition+index;
    uint16 tagged=gs->pickupType[index];
    *out=(tr64_game_pickup_event_t){gs->pickupTick,tagged & 0xff,{p->x,p->y,p->z},tagged>>8};
    return 1;
}
