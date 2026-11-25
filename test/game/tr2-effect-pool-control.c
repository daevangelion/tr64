/* SPDX-License-Identifier: MIT */
/* Test-only link wrapper. Exercise the real allocator and breakup with
 * 0 or 3 slots available at the first spider's death. Never seal a trace. */
#include "game/internal.h"
#include <stdio.h>

void __real_game_tr2_spider_breakup(ItemObj *);

void __wrap_game_tr2_spider_breakup(ItemObj *item)
{
    const char *mode=getenv("TR64_EFFECT_POOL_CONTROL");
    if (!mode || (strcmp(mode,"full") && strcmp(mode,"three")))
        sys_error("combat effect control: expected full or three");
    unsigned available=!strcmp(mode,"full") ? 0 : 3;
    GameTR2 *tr2=gs->tr2;
    unsigned free=0;
    for (uint8 i=tr2->effectFree;i;i=tr2->effects[i-1].next)
        ++free;
    if (free<available || free>TR2_EFFECT_SLOTS)
        sys_error("combat effect control: invalid or overfull free list");
    for (unsigned i=available;i<free;++i)
        game_tr2_shot_effect(TR2_FX_BLOOD,item->room,item->pos,0,0);
    uint32 expected=gs->gRandSeedLogic;
    for (unsigned i=0;i<available*3;++i)
        expected=expected*UINT32_C(0x41c64e6d)+UINT32_C(0x3039);
    __real_game_tr2_spider_breakup(item);
    if (tr2->effectFree || item->room || gs->gRandSeedLogic!=expected)
        sys_error("combat effect control: allocation, removal or RNG mismatch");
    unsigned shards=0, total=0;
    for (uint8 i=tr2->effectActive;i;i=tr2->effects[i-1].next) {
        if (++total>TR2_EFFECT_SLOTS) sys_error("combat effect control: cyclic active list");
        shards+=tr2->effects[i-1].kind==TR2_FX_SHARD;
    }
    if (shards!=available || total!=TR2_EFFECT_SLOTS)
        sys_error("combat effect control: incorrect pool occupancy");
    printf("PASS combat effect control: %u free, %u shards, %u gameplay draws; unsealed\n",
           available,shards,available*3);
    exit(0);
}
