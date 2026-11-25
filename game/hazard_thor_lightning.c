/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_thor_lightning.h and hazard_oracles/{thor,lightning}.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "hazard.h"
void ThorHandle_update(ItemObj *self)
{
    switch(self->state) {
    case 0: if (Object_isActive(self)) self->goalState=1; break;
    case 1: self->goalState=Object_isActive(self) ? 2 : 0; break;
    case 2: {
        /* Oracle hunk 27: more than 30 frames into the animation (frameIndex is level-wide, the source's test was never true). */
        if (self->frameIndex-tr64_u16(gs->level.anims[self->animIndex].frame_start)<=30)break;
        /* Oracle hunk 27: the point 3072 ahead in the hammer's facing (0: +z, 90: +x, 180: -z, 270: -x; objects-missing.md 12);
         * getBlockOffset's axes are the other way round, hence the negative distance. */
        vec3i strike=vec3i_add(self->pos,getBlockOffset(self->angle.y,-3072));
        for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
            Lara *lara=gs->players[slot];
            if (!lara || lara->health<=0)continue;
            vec3i d=vec3i_sub(strike,lara->pos);
            if (abs(d.x)<520 && abs(d.z)<520)hazardDamageLara(lara,1001,HAZARD_HIT_THOR,&self->pos);
        }
        break;
    }
    case 3:
        if (self->room) {
            const Sector *sector=Room_getSector(self->room,self->pos.x,self->pos.z);
            if (sector) {
                int32 floor=0;
                gs->gLastFloorData=NULL;
                Sector_getTriggerFloorCeiling(sector,self->pos.x,self->pos.y,self->pos.z,&floor,NULL);
                game_check_trigger(gs->gLastFloorData,NULL);
                if (gs->stop.kind != TR64_GAME_STOP_NONE) return;
            }
        }
        ItemObj_deactivate(self);
        break;
    }
    ItemObj_animProcess(self,true);
    if (self->headOffset >= MAX_ITEMS) return;
    ItemObj *head=gs->items+self->headOffset;
    if (head->type != ITEM_HAMMER_BLOCK) return;
    head->pos=self->pos;
    head->angle=self->angle;
    head->animIndex=self->animIndex;
    head->frameIndex=self->frameIndex;
    head->nextState=self->state;
}
static int32 randomRange(int32 n)
{
    return rand_logic()*n>>15;
}
static void LightningEmitter_strike(ItemObj *self)
{
    bool targets=gs->level.models[self->type].count>1;
    int32 radius=targets ? 2560 : 1024;
    self->turnSpeed=0;
    Lara *victim=NULL;
    if (!gs->players[1]) {
        Lara *lara=gs->players[0];
        if (lara) {
            vec3i d=vec3i_sub(lara->pos,self->pos);
            int32 dot=i32_add(i32_add(i32_mul(d.x,d.x),i32_mul(d.y,d.y)),i32_mul(d.z,d.z));
            if (dot<radius*radius)victim=lara;
        }
    } else {
        uint64_t best=(uint32)(radius*radius);
        for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
            Lara *lara=gs->players[slot];
            if (lara->health<=0)continue;
            uint64_t distance=game_distance_squared(self->pos,lara->pos);
            if (distance<best) { best=distance;victim=lara; }
        }
    }
    if (victim) {
        hazardDamageLara(victim,400,HAZARD_HIT_LIGHTNING,&self->pos);
        self->nextState=0;
    } else if (targets)self->turnSpeed=1+randomRange(5);
    soundPlay(SND_LIGHTNING,&self->pos);
}
void LightningEmitter_update(ItemObj *self)
{
    if (!Object_isActive(self)) {
        self->timer=0;
        self->state=self->nextState=0;
        if (gs->gSaveGame.flipped) worldRequestFlipToggle();
        ItemObj_deactivate(self);
        return;
    }
    if (--self->timer > 0) return;
    if (self->state == 1) {
        if (gs->gSaveGame.flipped) worldRequestFlipToggle();
        self->state=0;
        self->nextState=1;
        self->timer=35+randomRange(45);
        soundPlay(SND_LIGHTNING,&self->pos);
        return;
    }
    if (!gs->gSaveGame.flipped) worldRequestFlipToggle();
    self->state=1;
    self->timer=20;
    LightningEmitter_strike(self);
}
