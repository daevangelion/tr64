/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"

enum { TIGER_REST=1, TIGER_WALK=2, TIGER_RUN=3, TIGER_ROAR=5, TIGER_CLOSE=6,
       TIGER_RUNNING_STRIKE=7, TIGER_POUNCE=8, TIGER_DIE=9 };

void game_tr2_tiger_control(TR2Creature *ai,Lara *lara,const Interest *p)
{
    ItemObj *item=ai->item;
    switch (item->state) {
    case TIGER_REST:
        ai->maxTurn=0;
        ai->flags=0;
        if (ai->mood==ESCAPE) item->goalState=TIGER_RUN;
        else if (ai->mood==BORED) { (void)rand_logic(); item->goalState=TIGER_WALK; }
        else if (p->ahead && p->distance<341*341) item->goalState=TIGER_CLOSE;
        else if (p->ahead && p->distance<1024*1024) { item->goalState=TIGER_POUNCE; ai->maxTurn=546; }
        else if (item->nextState) item->goalState=item->nextState;
        else if (ai->mood!=ATTACK && rand_logic()<96) item->goalState=TIGER_ROAR;
        else item->goalState=TIGER_RUN;
        break;
    case TIGER_WALK:
        ai->maxTurn=546;
        if (ai->mood!=BORED) item->goalState=TIGER_REST;
        else if (rand_logic()<96) { item->nextState=TIGER_ROAR; item->goalState=TIGER_REST; }
        break;
    case TIGER_RUN:
        ai->maxTurn=1092;
        if (ai->mood==BORED || (ai->flags && p->ahead)) item->goalState=TIGER_REST;
        else if (!ai->flags && p->ahead && p->distance<1536*1536)
            item->goalState=lara->hSpeed ? TIGER_RUNNING_STRIKE : TIGER_REST;
        else if (ai->mood!=ATTACK && rand_logic()<96) { item->nextState=TIGER_ROAR; item->goalState=TIGER_REST; }
        ai->flags=0;
        break;
    case TIGER_CLOSE: case TIGER_RUNNING_STRIKE: case TIGER_POUNCE:
        game_tr2_creature_strike(ai,lara,100,UINT32_C(0x07fdc000));
        break;
    case TIGER_ROAR:
        /* The controller's own roar goal. Q-HARD-02: the specification
         * queues a roar without a state number or a clip, so this stays
         * the literal 5 until the author names both. */
        break;
    default:
        sys_error("Tomb Raider II: tiger state %u unknown", item->state);
    }
    /* Q-T2K-13: blood uses the live mesh-22 yaw from before this turn. */
    int16 head=p->ahead?p->yaw:0;
    ai->jointYaw[0]=angleLerp(ai->jointYaw[0],head,910);
    ai->jointYaw[0]=X_MAX(-12288,X_MIN(12288,ai->jointYaw[0]));
}

void game_tr2_tiger_death(TR2Creature *ai)
{
    ItemObj *item=ai->item;
    if (item->state!=TIGER_DIE) {
        /* Great Wall creatures, Navigation during tiger death: keep the old goal. */
        ItemObj_animSet(item,gs->level.models[item->type].animIndex+TR2_TIGER_DEATH_CLIP,false,0);
        item->state=TIGER_DIE;
    }
    ai->jointYaw[0]=angleLerp(ai->jointYaw[0],0,910);
}
