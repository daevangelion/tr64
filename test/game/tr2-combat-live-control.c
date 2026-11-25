/* SPDX-License-Identifier: MIT */
/* Test-only link wrapper. Exercise actual activation and shot allocations;
 * no gameplay outputs are supplied to a replay. */
#include "game/common/internal.h"
#include <stdio.h>

void __real_game_tr2_tick(uint32 mask);
void __real_game_tr2_shot_effect(TR2EffectKind,Room *,vec3i,int16,int16);
void __real_game_tr2_bite_effect(const ItemObj *,unsigned,vec3i,int16);
void __real_game_tr2_creature_waypoint(TR2Creature *);
void __real_game_tr2_creature_stride(TR2Creature *,int16);

static ItemObj *runningSpider;
static uint32 afterWaypoint;
static bool sawWaypoint;

static void boredSpider(void)
{
    runningSpider=gs->items+8;
    if (runningSpider->type!=TR2_ITEM_SPIDER || !game_tr2_activate_item(runningSpider))
        sys_error("spider control needs Wall spider 8");
    gs->players[0]->health=0; /* No living target makes the selected mood Bored. */
    runningSpider->state=runningSpider->goalState=3;
    game_tr2_update_creature(runningSpider,gs->players[0]);
    sys_error("spider control did not reach the stride");
}

void __wrap_game_tr2_creature_waypoint(TR2Creature *ai)
{
    __real_game_tr2_creature_waypoint(ai);
    if (runningSpider==ai->item) {
        sawWaypoint=true;
        afterWaypoint=gs->gRandSeedLogic;
    }
}

void __wrap_game_tr2_creature_stride(TR2Creature *ai,int16 turn)
{
    if (runningSpider==ai->item) {
        if (!sawWaypoint || runningSpider->goalState!=2 || gs->gRandSeedLogic!=afterWaypoint)
            sys_error("running spider control: crawl goal or no-draw rule failed (goal %u)",runningSpider->goalState);
        puts("PASS running Bored spider requests Crawl without a pose draw; unsealed");
        exit(0);
    }
    __real_game_tr2_creature_stride(ai,turn);
}

static const ItemObj *poolFilledAtBite;
static int16 healthBeforeFullBite;

void __wrap_game_tr2_bite_effect(const ItemObj *item,unsigned joint,vec3i offset,int16 extraYaw)
{
    const char *mode=getenv("TR64_COMBAT_LIVE_CONTROL");
    if (mode && !strcmp(mode,"bite-full")) {
        if (item->type!=TR2_ITEM_CROW) sys_error("full bite control requires a crow");
        poolFilledAtBite=item;
        healthBeforeFullBite=gs->players[0]->health;
        while (gs->tr2->effectFree)
            __real_game_tr2_shot_effect(TR2_FX_BLOOD,item->room,item->pos,0,0);
    }
    uint32 logic=gs->gRandSeedLogic,draw=gs->gRandSeedDraw;
    uint8 free=gs->tr2->effectFree, head=gs->tr2->effectActive;
    __real_game_tr2_bite_effect(item,joint,offset,extraYaw);
    if (gs->gRandSeedLogic!=logic || gs->gRandSeedDraw!=draw)
        sys_error("bite blood advanced an RNG stream");
    if (free) {
        const TR2Effect *fx=gs->tr2->effects+free-1;
        if (gs->tr2->effectActive!=free || fx->next!=head || fx->kind!=TR2_FX_BLOOD ||
            fx->age!=0 || fx->room!=item->room || fx->speed!=item->hSpeed || fx->angle.y!=item->angle.y)
            sys_error("bite blood did not allocate one room effect with the attacker's motion");
    } else if (gs->tr2->effectActive!=head || gs->tr2->effectFree)
        sys_error("full bite pool altered occupancy");
    if (mode && !strcmp(mode,"effects"))
        printf("BITE %u %u %u\n",(unsigned)(gs->tick+1),item->type,free!=0);
}

static void forceWake(bool visible)
{
    ItemObj *entrant=gs->items+18;
    const unsigned slots[]={8,9,10,11,16};
    if (entrant->type!=TR2_ITEM_TIGER || entrant->flags&ITEM_FLAG_ACTIVE)
        sys_error("wake control needs dormant Wall tiger 18");
    vec3i camera=entrant->pos;
    camera.z=i32_sub(camera.z,4096);
    gs->playersExtra[0].camera.view.pos=camera;
    for (unsigned i=0;i<TR2_CREATURE_SLOTS;++i) {
        ItemObj *o=gs->items+slots[i];
        o->pos=camera;
        o->pos.z=i32_add(camera.z,i==3 ? 1024 : 512);
        o->flags=(o->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
        gs->tr2->creatures[i].item=o;
    }
    if (visible) entrant->flags&=~ITEM_FLAG_STATUS;
    unsigned before=gs->tr2->creatures[3].item-gs->items;
    if (!game_tr2_activate_item(entrant)) sys_error("wake control: no action dispatch");
    if (visible) {
        if (gs->tr2->creatures[3].item!=entrant ||
            (gs->items[before].flags&ITEM_FLAG_STATUS)!=ITEM_FLAG_STATUS_INVISIBLE)
            sys_error("wake control: visible dormant entrant did not evict farthest occupant");
    } else if (game_tr2_creature_slot(entrant) ||
               (entrant->flags&ITEM_FLAG_STATUS)!=ITEM_FLAG_STATUS_INVISIBLE) {
        sys_error("wake control: hidden entrant should wait without evicting closer occupants");
    }
    printf("PASS %s dormant mask completion; unsealed\n",visible ? "visible forced" : "hidden ordinary");
    exit(0);
}

void __wrap_game_tr2_tick(uint32 mask)
{
    const char *mode=getenv("TR64_COMBAT_LIVE_CONTROL");
    if (mode && !strcmp(mode,"visible")) forceWake(true);
    if (mode && !strcmp(mode,"hidden")) forceWake(false);
    if (mode && !strcmp(mode,"spider-bored")) boredSpider();
    __real_game_tr2_tick(mask);
    if (poolFilledAtBite) {
        const TR2Creature *ai=game_tr2_creature_slot(poolFilledAtBite);
        if (!ai || ai->flags!=1 || gs->players[0]->health!=healthBeforeFullBite ||
            healthBeforeFullBite!=980 || gs->tr2->effectFree)
            sys_error("full bite pool lost damage, latch or occupancy");
        puts("PASS full bite pool refuses blood but retains damage and latch; unsealed");
        exit(0);
    }
    if (mode && !strcmp(mode,"effects")) {
        unsigned kinds[TR2_FX_RICOCHET+1]={0};
        for (uint8 i=gs->tr2->effectActive;i;i=gs->tr2->effects[i-1].next)
            ++kinds[gs->tr2->effects[i-1].kind];
        printf("EFFECTS %u flame=%u shard=%u splash=%u blood=%u ricochet=%u\n",
               (unsigned)gs->tick,kinds[TR2_FX_FLAME],kinds[TR2_FX_SHARD],
               kinds[TR2_FX_SPLASH],kinds[TR2_FX_BLOOD],kinds[TR2_FX_RICOCHET]);
    }
}

void __wrap_game_tr2_shot_effect(TR2EffectKind kind,Room *room,vec3i pos,int16 speed,int16 yaw)
{
    uint32 draw=gs->gRandSeedDraw,logic=gs->gRandSeedLogic;
    bool canAllocate=gs->tr2->effectFree!=0;
    __real_game_tr2_shot_effect(kind,room,pos,speed,yaw);
    if (kind==TR2_FX_RICOCHET && canAllocate)
        draw=draw*UINT32_C(0x41c64e6d)+UINT32_C(0x3039);
    if (gs->gRandSeedDraw!=draw || gs->gRandSeedLogic!=logic)
        sys_error("shot effect used the wrong random stream or draw count");
    if (kind==TR2_FX_RICOCHET && canAllocate)
        printf("SPARK %u %d %d %d %d\n",(unsigned)(gs->tick+1),(int)(room-gs->rooms),pos.x,pos.y,pos.z);
}
