/* SPDX-License-Identifier: MIT */
/* Run production spike-jump and boulder-hit with and without free effect
 * slots just before the fatal contact. These are harness-only links. */
#include "game/internal.h"
#include <stdio.h>

static bool collect;
static unsigned attempts;

void __real_game_tr2_tick(uint32);
void __real_game_tr2_shot_effect(TR2EffectKind,Room *,vec3i,int16,int16);
void __real_Lara_updateObjectsCollision(Lara *);

static uint32 advance(uint32 seed)
{
    return 0x3039u+seed*0x41c64e6du;
}

void __wrap_Lara_updateObjectsCollision(Lara *lara)
{
    const char *route=getenv("TR64_HAZARD_POOL_CONTROL");
    if (gs->tick!=12 || !route || (strcmp(route,"spike-29") && strcmp(route,"spike-30"))) {
        __real_Lara_updateObjectsCollision(lara);
        return;
    }
    if (lara->flags&ITEM_FLAG_GRAVITY)
        sys_error("spike speed control needs grounded Walk contact");
    const bool damage=!strcmp(route,"spike-30");
    lara->hSpeed=damage ? 30 : 29;
    const int priorHP=lara->health;
    uint32 expected=advance(gs->gRandSeedLogic);
    const bool blood=damage && ((expected>>10)&32767)>=24576;
    for (unsigned i=0;blood && i<4;++i)
        expected=advance(expected);
    collect=true;
    __real_Lara_updateObjectsCollision(lara);
    if (lara->health!=priorHP-(damage ? 15 : 0) || gs->gRandSeedLogic!=expected || attempts!=blood)
        sys_error("spike speed control lost the contact roll or damage cutoff");
    printf("PASS %s: grounded contact and exact gameplay roll\n",route);
    exit(0);
}

void __wrap_game_tr2_shot_effect(TR2EffectKind kind,Room *room,vec3i origin,int16 speed,int16 yaw)
{
    if (collect && kind==TR2_FX_BLOOD)
        ++attempts;
    __real_game_tr2_shot_effect(kind,room,origin,speed,yaw);
}

static void bladeTick(const char *route, uint32 mask)
{
    const bool idle=!strcmp(route,"blade-idle");
    const bool damage=!strcmp(route,"blade-cut");
    if (!idle && !damage && strcmp(route,"blade-pedestal"))
        sys_error("unknown blade touch control");
    const unsigned tick=idle ? 2 : 3;
    if (gs->tick+1!=tick) {
        __real_game_tr2_tick(mask);
        return;
    }
    ItemObj *blade=gs->items+49;
    Lara *lara=gs->players[0];
    if (blade->type!=TR2_ITEM_GUILLOTINE || blade->state!=(idle ? 1 : 2))
        sys_error("blade touch control needs the real authored wake and pose");
    blade->hitMask=(idle || damage) ? 2u : 1u;
    const int16 priorHP=lara->health;
    const uint32 logic=gs->gRandSeedLogic;
    uint32 draw=gs->gRandSeedDraw;
    for (unsigned i=0;damage && i<6;++i)
        draw=advance(draw);
    collect=true;
    __real_game_tr2_tick(mask);
    unsigned blood=0;
    for (uint8 handle=gs->tr2->effectActive;handle;handle=gs->tr2->effects[handle-1].next) {
        const TR2Effect *fx=gs->tr2->effects+handle-1;
        if (fx->kind==TR2_FX_BLOOD) {
            if (fx->age!=1)
                sys_error("blade blood missed the effect phase");
            ++blood;
        }
    }
    if (lara->health!=priorHP-(damage ? 100 : 0) || blade->goalState!=(idle ? 2 : 1) ||
        blade->hitMask || attempts!=(damage ? 2u : 0u) || attempts!=blood ||
        gs->gRandSeedLogic!=logic || gs->gRandSeedDraw!=draw)
        sys_error("blade touch control disagrees on pose, damage, effects or draws");
    printf("PASS %s: %u blood attempts in the item phase\n",route,attempts);
    exit(0);
}

static void checkCompression(Lara *lara)
{
    /* Q-HAZ-03: no eligible dry direction keeps the goal; wade never tests. */
    lara->state=Lara_STATE_COMPRESS;
    lara->goalState=Lara_STATE_COMPRESS;
    lara->waterState=WATER_STATE_ABOVE;
    lara->input=0;
    lara->vSpeed=0;
    Lara_s_STATE_COMPRESS(lara);
    if (lara->goalState!=Lara_STATE_COMPRESS)
        sys_error("dry neutral compression requested goal %d",lara->goalState);
    lara->goalState=Lara_STATE_COMPRESS;
    lara->waterState=WATER_STATE_WADE;
    lara->input=IN_UP|IN_LEFT|IN_RIGHT|IN_DOWN;
    Lara_s_STATE_COMPRESS(lara);
    if (lara->goalState!=Lara_STATE_COMPRESS)
        sys_error("wading compression requested goal %d",lara->goalState);
    lara->goalState=Lara_STATE_COMPRESS;
    lara->waterState=WATER_STATE_ABOVE;
    lara->vSpeed=132;
    Lara_s_STATE_COMPRESS(lara);
    if (lara->goalState==Lara_STATE_COMPRESS)
        sys_error("descent over 131 kept the compression goal");
    printf("PASS compression goal stays without an eligible direction, including wade\n");
    exit(0);
}

void __wrap_game_tr2_tick(uint32 mask)
{
    const char *route=getenv("TR64_HAZARD_POOL_CONTROL");
    const char *capacity=getenv("TR64_HAZARD_POOL_CAPACITY");
    if (route && !strcmp(route,"compress-goal")) {
        checkCompression(gs->players[0]);
        return;
    }
    if (!route || !capacity || (strcmp(capacity,"zero") && strcmp(capacity,"normal")))
        sys_error("hazard pool control needs a route and zero/normal capacity");
    if (!strcmp(route,"spike-29") || !strcmp(route,"spike-30")) {
        __real_game_tr2_tick(mask);
        return;
    }
    if (!strncmp(route,"blade-",6)) {
        bladeTick(route,mask);
        return;
    }
    const bool spike=!strcmp(route,"spike-jump");
    if (!spike && strcmp(route,"boulder-hit"))
        sys_error("hazard pool control needs a supported route");
    const unsigned target=spike ? 43 : 84;
    if (gs->tick+1!=target) {
        __real_game_tr2_tick(mask);
        return;
    }
    /* Fill the actual free list with inert flame slots; preserve all
     * existing effects and let the real item/effect phase run. */
    GameTR2 *tr2=gs->tr2;
    const bool full=!strcmp(capacity,"zero");
    const uint8 freeHead=tr2->effectFree;
    if (!freeHead)
        sys_error("hazard pool control had no free slots before saturation");
    if (full) {
        while (tr2->effectFree) {
            unsigned handle=tr2->effectFree;
            TR2Effect *fx=tr2->effects+handle-1;
            tr2->effectFree=fx->next;
            *fx=(TR2Effect){.kind=TR2_FX_FLAME,.room=gs->players[0]->room,
                            .pos=gs->players[0]->pos,.next=tr2->effectActive};
            tr2->effectActive=handle;
        }
    }
    collect=true;
    __real_game_tr2_tick(mask);
    collect=false;
    Lara *lara=gs->players[0];
    const bool airOk=spike ? lara->timer==-1 && lara->state==Lara_STATE_DEATH
                           : lara->timer==1800 && lara->state==Lara_STATE_SPECIAL;
    if (attempts!=(spike ? 20u : 15u) || lara->health!=-1 || !airOk ||
        (full ? tr2->effectFree!=0 : tr2->effectFree==freeHead))
        sys_error("hazard pool control lost the burst, air or damage");
    printf("PASS %s %s: %u blood attempts and lethal contact at %u\n",route,capacity,attempts,target);
}
