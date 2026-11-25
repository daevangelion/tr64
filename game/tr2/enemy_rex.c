/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"

enum { REX_REST=1, REX_WALK=2, REX_RUN=3, REX_DEATH=5, REX_ROAR=6, REX_LUNGE=7 };

/* tr2-great-wall-creatures, Tyrannosaurus: wary flag, prior contact,
 * pose choices and turn limits. The shared core already ran navigation. */
void game_tr2_rex_control(TR2Creature *ai,Lara *lara,const Interest *p)
{
    ItemObj *item=ai->item;
    bool bite=p->ahead && lara->health>0 && llabs((int64_t)lara->pos.y-item->pos.y)<=384;
    ai->flags=(!p->ahead && p->facing && ai->mood!=ESCAPE) ||
              (bite && p->distance>1500*1500 && p->distance<4096*4096);
    int16 yaw=angleLerp(ai->jointYaw[0],p->ahead ? p->yaw>>1 : 0,910);
    ai->jointYaw[0]=ai->jointYaw[1]=X_MAX(-12288,X_MIN(12288,yaw));
    if (item->hitMask && lara->health>0)
        lara->health-=item->state==REX_RUN ? 10 : 1;
    switch (item->state) {
    case REX_REST:
        if (item->nextState) item->goalState=item->nextState;
        else if (bite && p->distance<1500*1500) item->goalState=REX_LUNGE;
        else item->goalState=ai->mood==BORED || ai->flags ? REX_WALK : REX_RUN;
        break;
    case REX_WALK:
        ai->maxTurn=364;
        if (ai->mood!=BORED && !ai->flags) item->goalState=REX_REST;
        else if (p->ahead && rand_logic()<512) {
            item->nextState=REX_ROAR;
            item->goalState=REX_REST;
        }
        break;
    case REX_RUN:
        ai->maxTurn=728;
        if ((bite && p->distance<5120*5120) || ai->flags) item->goalState=REX_REST;
        else if (p->ahead && ai->mood!=ESCAPE && rand_logic()<512) {
            item->nextState=REX_ROAR;
            item->goalState=REX_REST;
        } else if (ai->mood==BORED) item->goalState=REX_REST;
        break;
    case REX_LUNGE:
        /* The special Lara pose bank is not admitted by the ordinary
         * controller. Do not enter a normal Lara state for the cut. */
        if (item->hitMask&((1u<<12)|(1u<<13)))
            sys_error("Tomb Raider II: unsupported Tyrannosaurus fatal lunge pose");
        item->nextState=REX_WALK;
        break;
    case REX_ROAR:
    case REX_DEATH:
        break; /* Known poses play out; death retargeting is game_tr2_rex_death. */
    default:
        sys_error("Tomb Raider II: unknown Tyrannosaurus state %u",item->state);
    }
}

void game_tr2_rex_death(TR2Creature *ai)
{
    ItemObj *item=ai->item;
    item->goalState=item->state==REX_REST ? REX_DEATH : REX_REST;
    for (unsigned i=0;i<2;++i) ai->jointYaw[i]=angleLerp(ai->jointYaw[i],0,910);
}
