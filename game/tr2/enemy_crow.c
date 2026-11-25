/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"

enum { CROW_FLY=1, CROW_STAND=2, CROW_APPROACH=3, CROW_FALL=4,
       CROW_DEAD=5, CROW_STRIKE=6, CROW_PERCH=7 };

/* Great Wall creatures, Crow: settled, flight, approach, and strike. */
void game_tr2_crow_control(TR2Creature *ai,Lara *lara,const Interest *p)
{
    ItemObj *item=ai->item;
    switch (item->state) {
    case CROW_STAND: case CROW_PERCH:
        item->pos.y=item->roomFloor;
        if (ai->mood!=BORED) item->goalState=CROW_FLY;
        break;
    case CROW_FLY:
        ai->flags=0;
        if (ai->mood==BORED) item->goalState=CROW_STAND;
        else item->goalState=p->ahead && p->distance<512*512 ? CROW_STRIKE : CROW_APPROACH;
        break;
    case CROW_APPROACH:
        if (ai->mood==BORED) item->goalState=CROW_FLY;
        else if (p->ahead && p->distance<512*512) item->goalState=CROW_STRIKE;
        break;
    case CROW_STRIKE: game_tr2_creature_strike(ai,lara,20,UINT32_MAX); break;
    default:
        sys_error("Tomb Raider II: crow state %u unknown", item->state);
    }
}

void game_tr2_crow_death(TR2Creature *ai)
{
    ItemObj *item=ai->item;
    if (item->state!=CROW_FALL && item->state!=CROW_DEAD) {
        ItemObj_animSet(item,gs->level.models[item->type].animIndex+TR2_CROW_FALL_CLIP,false,0);
        item->state=CROW_FALL;
        item->hSpeed=0;
        item->flags|=ITEM_FLAG_GRAVITY;
    }
    if (item->state==CROW_DEAD || item->pos.y>item->roomFloor) {
        item->pos.y=item->roomFloor;
        if (item->state==CROW_FALL) {
            item->vSpeed=0;
            item->flags&=~ITEM_FLAG_GRAVITY;
            item->goalState=CROW_DEAD;
        }
    }
    item->angle.x=0;
    /* No pursuit turn. Only a radius correction adds the placement ordinal
     * (supplied by the caller to game_tr2_ai_death_stride). */
}
