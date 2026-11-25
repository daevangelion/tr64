/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"

void game_tr2_spider_control(TR2Creature *ai,Lara *lara,const Interest *p)
{
    ItemObj *item=ai->item;
    switch (item->state) {
    case 1: case 2:
        ai->flags=0;
        if (p->ahead && item->hitMask) item->goalState=4;
        else if (ai->mood==ATTACK || ai->mood==ESCAPE) item->goalState=3;
        else if (ai->mood==STALK || rand_logic()<256) item->goalState=item->state==1 || ai->mood==STALK ? 2 : 1;
        break;
    case 3:
        ai->flags=0;
        if (ai->mood==BORED || ai->mood==STALK) item->goalState=2;
        else if (p->ahead && item->hitMask) item->goalState=1;
        else if (p->ahead && p->distance<204*204) item->goalState=6;
        else if (p->ahead && p->distance<512*512) item->goalState=5;
        break;
    case 4: case 5: case 6: game_tr2_creature_strike(ai,lara,25,UINT32_MAX); break;
    default:
        sys_error("Tomb Raider II: spider state %u unknown", item->state);
    }
}

void game_tr2_spider_death(TR2Creature *ai)
{
    ItemObj *item=ai->item;
    ai->item=NULL;
    game_tr2_spider_breakup(item);
}
