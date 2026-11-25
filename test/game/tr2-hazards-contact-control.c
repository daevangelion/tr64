/* SPDX-License-Identifier: MIT */
/* Exercise the production pass with a cooked room-3 portal sequence
 * of 33, 0, 33. Observe the visits, not a second room-walk algorithm. */
#include "game/internal.h"
#include <stdio.h>

static ItemObj *probes[6];
static unsigned visits[4], count;
void __real_Lara_updateObjectsCollision(Lara *);

bool __wrap_game_tr2_interact(ItemObj *item, Lara *lara)
{
    for (unsigned i=0;i<sizeof(probes)/sizeof(*probes);++i) {
        if (item!=probes[i])
            continue;
        if (count==sizeof(visits)/sizeof(*visits))
            sys_error("too many hazard contact visits");
        visits[count++]=i;
        if (!i) {
            lara->health=0; /* Entry was healthy. Later visits must still run. */
            /* Q-INT-08: transferring her room must not restart or extend the pass. */
            Room_remove(lara->room,lara);
            Room_add(gs->rooms+33,lara);
        }
        return false;
    }
    sys_error("unexpected hazard contact control visit type=%u room=%u item=%u probes=%u,%u,%u,%u,%u,%u count=%u",
              item->type, item->room ? (unsigned)(item->room-gs->rooms) : 999u,
              (unsigned)(item-gs->items),
              (unsigned)(probes[0]-gs->items),(unsigned)(probes[1]-gs->items),
              (unsigned)(probes[2]-gs->items),(unsigned)(probes[3]-gs->items),
              (unsigned)(probes[4]-gs->items),(unsigned)(probes[5]-gs->items),count);
}

void __wrap_Lara_updateObjectsCollision(Lara *lara)
{
    if (lara->room!=gs->rooms+3)
        sys_error("hazard contact control needs the room-3 placement");
    unsigned n=0;
    for (unsigned i=0;i<gs->level.itemsCount;++i) {
        ItemObj *item=gs->items+i;
        if (item==lara || !item->room)
            continue;
        item->flags&=~ITEM_FLAG_COLLISION;
        if (n<sizeof(probes)/sizeof(*probes))
            probes[n++]=item;
    }
    if (n!=sizeof(probes)/sizeof(*probes))
        sys_error("hazard contact control needs six placed items");
    for (unsigned i=0;i<n;++i) {
        ItemObj *item=probes[i];
        Room_remove(item->room,item);
        item->type=149; /* Ordinary pickup callback, intercepted above. */
        item->flags=(item->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_COLLISION;
        item->pos=lara->pos;
        Room_add(gs->rooms+(i<4 ? 3 : i==4 ? 33 : 0),item);
    }
    probes[0]->pos.x+=4095;
    probes[1]->pos.x+=4096;
    probes[2]->pos.y-=4096;
    probes[3]->pos.z+=4096;
    __real_Lara_updateObjectsCollision(lara);
    const unsigned expected[]={0,4,5,4};
    if (count!=sizeof(expected)/sizeof(*expected) || memcmp(visits,expected,sizeof(expected)))
        sys_error("wrong portal contact visits=%u order=%u,%u,%u,%u",count,visits[0],visits[1],visits[2],visits[3]);
    count=0;
    __real_Lara_updateObjectsCollision(lara);
    if (count)
        sys_error("contact pass started again with dead Lara");
    printf("PASS authored duplicate portal order, push keeps the entry rooms, strict per-axis reach and entry-only health gate\n");
    exit(0);
}
