/* SPDX-License-Identifier: MIT */
/* Link normal host objects except plat/host/main.o, with --wrap=ItemObj_collideRoom. */
#include "game/internal.h"
#include <stdio.h>

static unsigned errors, frontQueries;

void __wrap_ItemObj_collideRoom(const ItemObj *self,int32 height,int32 offset)
{
    (void)self;
    (void)height;
    (void)offset;
    ++frontQueries;
    gs->cinfo.type=CT_FRONT;
}

static void check(bool ok,const char *name)
{
    printf("%s %s\n",ok ? "PASS" : "FAIL",name);
    errors+=!ok;
}

static Lara at(ExtraInfoLara *extra,unsigned state,unsigned water,unsigned input)
{
    return (Lara){.extraL=extra,.state=state,.goalState=state,.waterState=water,
                  .input=input,.health=1000};
}

int main(void)
{
    static GameState game;
    GameTR2 tr2={0};
    ExtraInfoLara extra={0};
    gs=&game;
    gs->tr2=&tr2;
    gs->tr2Rules=TR2_RULES_ALL | TR2_RULES_CAMERA | TR2_RULE_BLOCKED_MOTION | TR2_RULE_WATER;

    Lara lara=at(&extra,Lara_STATE_BACK,WATER_STATE_WADE,IN_DOWN);
    Lara_s_STATE_BACK(&lara);
    check(lara.goalState==Lara_STATE_BACK,"Back without Walk keeps Wade backward");
    lara.input=IN_WALK;
    Lara_s_STATE_BACK(&lara);
    check(lara.goalState==Lara_STATE_STOP && lara.state==Lara_STATE_BACK,"Walk alone releases Back without switching clips");
    lara=at(&extra,Lara_STATE_BACK,WATER_STATE_WADE,IN_DOWN);
    lara.health=0;
    Lara_s_STATE_BACK(&lara);
    check(lara.goalState==Lara_STATE_STOP,"death cancels backward Wade");
    lara=at(&extra,Lara_STATE_BACK,WATER_STATE_ABOVE,IN_DOWN);
    Lara_s_STATE_BACK(&lara);
    check(lara.goalState==Lara_STATE_STOP,"dry Back alone does not keep backward walk");
    lara.input|=IN_WALK;
    Lara_s_STATE_BACK(&lara);
    check(lara.goalState==Lara_STATE_BACK,"dry Back with Walk keeps backward walk");

    for (unsigned walk=0;walk<=IN_WALK;walk+=IN_WALK) {
        frontQueries=0;
        lara=at(&extra,Lara_STATE_WALK,WATER_STATE_WADE,IN_UP|walk);
        Lara_s_STATE_WALK(&lara);
        check(lara.goalState==Lara_STATE_WADE && !frontQueries,
              walk ? "retained Walk + Forward and Walk chooses Wade before collision" :
                     "retained Walk + Forward alone chooses Wade before collision");
        frontQueries=0;
        lara=at(&extra,Lara_STATE_RUN,WATER_STATE_WADE,IN_UP|walk);
        Lara_s_STATE_RUN(&lara);
        check(lara.goalState==Lara_STATE_WADE && !frontQueries,
              walk ? "retained Run + Forward and Walk chooses Wade before collision" :
                     "retained Run + Forward alone chooses Wade before collision");
        lara=at(&extra,Lara_STATE_TURN_LEFT,WATER_STATE_WADE,IN_UP|IN_LEFT|walk);
        lara.turnSpeed=-500;
        Lara_s_STATE_TURN_LEFT(&lara);
        check(lara.goalState==Lara_STATE_WADE && lara.turnSpeed==-(walk ? 728 : 909),
              walk ? "left turn with Walk caps rate and chooses Wade" : "left turn without Walk chooses Wade");
        lara=at(&extra,Lara_STATE_TURN_RIGHT,WATER_STATE_WADE,IN_UP|IN_RIGHT|walk);
        lara.turnSpeed=500;
        Lara_s_STATE_TURN_RIGHT(&lara);
        check(lara.goalState==Lara_STATE_WADE && lara.turnSpeed==(walk ? 728 : 909),
              walk ? "right turn with Walk caps rate and chooses Wade" : "right turn without Walk chooses Wade");
    }
    lara=at(&extra,Lara_STATE_WALK,WATER_STATE_WADE,IN_WALK);
    Lara_s_STATE_WALK(&lara);
    check(lara.goalState==Lara_STATE_STOP,"Walk without Forward requests Stand");
    lara=at(&extra,Lara_STATE_RUN,WATER_STATE_WADE,IN_WALK);
    Lara_s_STATE_RUN(&lara);
    check(lara.goalState==Lara_STATE_STOP,"Run without Forward requests Stand");
    lara=at(&extra,Lara_STATE_RUN,WATER_STATE_WADE,IN_UP|IN_JUMP);
    lara.animIndex=Lara_ANIM_RUN;
    lara.frameIndex=4;
    Lara_s_STATE_RUN(&lara);
    check(lara.goalState==Lara_STATE_JUMP,"Run jump admission precedes the Wade request");
    lara=at(&extra,Lara_STATE_RUN,WATER_STATE_WADE,IN_UP);
    lara.health=0;
    Lara_s_STATE_RUN(&lara);
    check(lara.goalState==Lara_STATE_DEATH,"Run death precedes the Wade request");
    lara=at(&extra,Lara_STATE_TURN_RIGHT,WATER_STATE_WADE,IN_RIGHT|IN_WALK);
    lara.turnSpeed=500;
    Lara_s_STATE_TURN_RIGHT(&lara);
    check(lara.goalState==Lara_STATE_TURN_RIGHT && lara.turnSpeed==728,"Walk caps an ordinary wet turn");
    extra.weaponState=WEAPON_STATE_READY;
    lara.turnSpeed=500;
    Lara_s_STATE_TURN_RIGHT(&lara);
    check(lara.goalState==Lara_STATE_TURN_FAST && lara.turnSpeed==909,"weapon readiness retains fast-turn precedence over Walk");
    extra.weaponState=WEAPON_STATE_FREE;

    lara=at(&extra,Lara_STATE_WADE,WATER_STATE_WADE,IN_UP);
    Lara_s_STATE_WADE(&lara);
    check(lara.goalState==Lara_STATE_WADE,"Forward alone continues Wade");
    lara.input=IN_WALK;
    Lara_s_STATE_WADE(&lara);
    check(lara.goalState==Lara_STATE_STOP,"Walk alone does not continue Wade");
    lara.waterState=WATER_STATE_ABOVE;
    lara.input=IN_UP|IN_WALK;
    Lara_s_STATE_WADE(&lara);
    check(lara.goalState==Lara_STATE_RUN,"departing Wade requests Run even with Walk held");
    return errors!=0;
}
