/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h, src/fixed/item.h, src/fixed/enemy.h and src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#if TR64_HAS_TR2
#include "tr64/code.h"
#include "game/tr2/creature.h"
#endif
static void itemCollide(ItemObj *item, Lara *lara)
{
    GameItemCall call = {0};
    call.op = GAME_ITEM_COLLIDE;
    call.self = item;
    call.lara = lara;
    game_item_call(&call);
}

#if TR64_HAS_TR2
/* Terrain push probes borrow gs->roomsList. Walk adjacent portals directly
 * so doors and creatures are visited in room-item order despite those probes. */
static void tr2ObjectsCollision(Lara *lara)
{
    const bool creatures = TR64_RULE(CREATURES);
    const bool interactions = TR64_RULE(INTERACTIONS);
    ExtraInfoLara *e = lara->extraL;
    e->hitQuadrant = -1;
    if (creatures)
        lara->flags &= ~ITEM_FLAG_INJURED;
    if (lara->health <= 0) {
        e->hitFrame = 0;
        return;
    }
    /* Great Wall objects, Q-INT-08: the starting room and its immutable
     * authored portal order are fixed even if a push moves Lara. */
    Room *start = lara->room;
    unsigned first = tr64_u32(start->info->portal_start);
    unsigned count = tr64_u32(start->info->portal_count);
    for (unsigned i = 0; i <= count; ++i) {
        unsigned room = !i ? (unsigned)(start - gs->rooms) :
                             tr64_u16(tr64_level_portals(gs->core,first+i-1)->room);
        for (ItemObj *item = gs->rooms[room].firstItem, *next; item; item = next) {
            /* A push can relink Lara into another room before we resume.
             * Retain her successor in this room, not Lara's mutable link. */
            next = item->nextItem == lara ? lara->nextItem : item->nextItem;
            if (item == lara || !(item->flags & ITEM_FLAG_COLLISION) ||
                (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
                continue;
            if (llabs((int64_t)item->pos.x-lara->pos.x) >= 4096 ||
                llabs((int64_t)item->pos.y-lara->pos.y) >= 4096 ||
                llabs((int64_t)item->pos.z-lara->pos.z) >= 4096)
                continue;
            if (creatures && game_tr2_creature_type(item->type)) {
                if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE) {
                    TR2AiCall call={.op=TR2_AI_COLLIDE,.self=item,.lara=lara};
                    (void)code_call("tr2_ai",(uint32_t)(uintptr_t)&call);
                }
            } else if (interactions && game_tr2_interact(item,lara)) {
                /* Share the reaction counter and first-bump sound with
                 * the creatures, rather than starting a door-only pass. */
                if (!e->hitFrame)
                    soundPlay(31,&lara->pos);
                if (e->hitFrame < 34)
                    ++e->hitFrame;
            }
        }
    }
    if (e->hitQuadrant < 0)
        e->hitFrame = 0;
}

void game_tr2_admit_counts(const char *name, uint32 rooms, uint32 items)
{
    /* 256 is the ItemObj allocation in levelStart. The generated
     * items limit stays the arena bound so the first cart's loader,
     * which compiles that constant, does not change. */
    if (rooms > TR64_LIMIT_rooms_2)
        sys_error("game %s: room capacity", name);
    if (items > 256)
        sys_error("game %s: item capacity", name);
}
#endif
void Lara_updateObjectsCollision(Lara *self)
{
    Room **adjRoom;
#if TR64_HAS_TR2
    if (TR64_RULE(CREATURES) || TR64_RULE(INTERACTIONS)) {
        tr2ObjectsCollision(self);
        return;
    }
#endif
    if (self->health <= 0)
    {
        self->extraL->hitQuadrant = -1;
        return;
    }
#if TR64_HAS_TR2
    /* Sequel item placement/list membership is live. Its collision
     * callbacks are not the first game's object-ID dispatch. */
    if (TR64_RULE(WORLD_ITEMS))
        return;
#endif
    adjRoom = Room_getAdjRooms(self->room);
    while (*adjRoom)
    {
        ItemObj *item = (*adjRoom++)->firstItem;
        while (item)
        {
            ItemObj *next = item->nextItem;
            if ((item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE &&
                (item->flags & ITEM_FLAG_COLLISION))
            {
                vec3i d = vec3i_sub(self->pos, item->pos);
                if (abs(d.x) < 4096 && abs(d.y) < 4096 && abs(d.z) < 4096)
                    itemCollide(item, self);
            }
            if (gs->stop.kind != TR64_GAME_STOP_NONE)
                return;
            item = next;
        }
    }
}
