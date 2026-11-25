/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/object.h"
static const int16 LIMIT_SWITCH[] = {-200,200,0,0,312,512,ANGLE(10),ANGLE(30),ANGLE(10)};
static const int16 LIMIT_SWITCH_UW[] = {-1024,1024,-1024,1024,-1024,1024,ANGLE(80),ANGLE(80),ANGLE(80)};
static const int16 LIMIT_PICKUP[] = {-256,256,-100,100,-256,100,ANGLE(10),0,0};
static const int16 LIMIT_PICKUP_UW[] = {-512,512,-512,512,-512,512,ANGLE(45),ANGLE(45),ANGLE(45)};
static const int16 LIMIT_HOLE[] = {-200,200,0,0,312,512,ANGLE(10),ANGLE(30),ANGLE(10)};
void Pickup_collide(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    (void)cinfo;
    if (Lara_claimBlocked(lara,self)) return;
    self->angle.y=lara->angle.y;
    self->angle.z=0;
    if (lara->waterState == WATER_STATE_ABOVE || lara->waterState == WATER_STATE_WADE) {
        self->angle.x=0;
        if (!Object_checkLimit(self,lara,LIMIT_PICKUP)) return;
        if (lara->state == Lara_STATE_PICKUP) {
            if (!ItemObj_animIsEnd(lara,23) || !inventory_addPickup(self->type)) return;
            gs->gSaveGame.pickups++;
            game_pickup_notify(self,lara);
            Room_remove(self->room,self);
            self->flags=(self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INVISIBLE;
        } else if (lara->state == Lara_STATE_STOP) {
            if (lara->extraL->weaponState != WEAPON_STATE_FREE || !(lara->input & IN_ACTION)) return;
            vec3i p=vec3i_create(0,0,-100);
            if (!ItemObj_moveTo(lara,&p,self,false)) return;
            Lara_claim(lara,self);
            ItemObj_animSkip(lara,Lara_STATE_PICKUP,Lara_STATE_STOP,false);
            lara->extraL->weaponState=WEAPON_STATE_BUSY;
        }
    }
    if (lara->waterState == WATER_STATE_UNDER) {
        self->angle.x=ANGLE(-25);
        if (!Object_checkLimit(self,lara,LIMIT_PICKUP_UW)) return;
        if (lara->state == Lara_STATE_PICKUP) {
            if (!ItemObj_animIsEnd(lara,14) || !inventory_addPickup(self->type)) return;
            gs->gSaveGame.pickups++;
            game_pickup_notify(self,lara);
            Room_remove(self->room,self);
            self->flags=(self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INVISIBLE;
        } else if (lara->state == Lara_STATE_UW_TREAD) {
            if (!(lara->input & IN_ACTION)) return;
            vec3i p=vec3i_create(0,-200,-350);
            if (!ItemObj_moveTo(lara,&p,self,true)) return;
            Lara_claim(lara,self);
            ItemObj_animSkip(lara,Lara_STATE_PICKUP,Lara_STATE_UW_TREAD,false);
        }
    }
}
void Switch_update(ItemObj *self)
{
    self->flags |= ITEM_FLAG_MASK;
    if (!Object_isActive(self)) {
        self->goalState=1;
        self->timer=0;
    }
    ItemObj_animProcess(self,true);
}
void Switch_collide(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    (void)cinfo;
    if (lara->extraL->weaponState != WEAPON_STATE_FREE || !(lara->input & IN_ACTION) ||
        lara->state != Lara_STATE_STOP || (self->flags & ITEM_FLAG_STATUS) ||
        !Object_checkLimit(self,lara,LIMIT_SWITCH)) return;
    if (Lara_claimBlocked(lara,self)) return;
    Lara_claim(lara,self);
    lara->angle.y=self->angle.y;
    ASSERT(self->state == 0 || self->state == 1);
    bool down=self->state == 1;
    self->goalState=down ? 0 : 1;
    ItemObj_animSkip(lara,down ? Lara_STATE_SWITCH_DOWN : Lara_STATE_SWITCH_UP,Lara_STATE_STOP,true);
    ItemObj_activate(self);
    self->flags=(self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
    lara->extraL->weaponState=WEAPON_STATE_BUSY;
}
void SwitchWater_collide(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    (void)cinfo;
    if (lara->extraL->weaponState != WEAPON_STATE_FREE || !(lara->input & IN_ACTION) ||
        lara->state != Lara_STATE_UW_TREAD || !Object_checkLimit(self,lara,LIMIT_SWITCH_UW)) return;
    if (Lara_claimBlocked(lara,self)) return;
    vec3i p=vec3i_create(0,0,108);
    if (!ItemObj_moveTo(lara,&p,self,true)) return;
    Lara_claim(lara,self);
    lara->vSpeed=0;
    self->goalState=self->state == 0 ? 1 : 0;
    ItemObj_animSkip(lara,Lara_STATE_SWITCH_DOWN,Lara_STATE_UW_TREAD,true);
    ItemObj_activate(self);
    self->flags=(self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
}
static void Hole_apply(ItemObj *self, int32 offset, Lara *lara, int32 stateUse)
{
    int32 slot=Lara_selectedUse(lara);
    if (lara->extraL->weaponState != WEAPON_STATE_FREE || (self->flags & ITEM_FLAG_STATUS) ||
        lara->state != Lara_STATE_STOP || lara->animIndex != Lara_ANIM_STAND_NORMAL ||
        (!(lara->input & IN_ACTION) && slot == SLOT_MAX) ||
        !Object_checkLimit(self,lara,LIMIT_HOLE)) return;
    if (Lara_claimBlocked(lara,self)) return;
    if (slot == SLOT_MAX) {
        if (gs->inventory.numKeys > 0 &&
            (!gs->players[1] || (gs->inventory.state==INV_STATE_NONE && gs->inventory.useSlot==SLOT_MAX))) {
            Lara_claim(lara,self);
            inventory_openUse(lara,self->type);
            return;
        }
    } else {
        if (inventory_applyItem(self)) {
            Lara_claim(lara,self);
            vec3i p=vec3i_create(0,0,offset);
            ItemObj_moveTo(lara,&p,self,false);
            ItemObj_animSkip(lara,stateUse,Lara_STATE_STOP,false);
            lara->extraL->weaponState=WEAPON_STATE_BUSY;
            self->flags=(self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
            gs->interactionAnim[Lara_slot(lara)]=(InteractionAnim){lara->animIndex,lara->frameIndex,lara->state,lara->goalState};
            return;
        }
        gs->inventory.useSlot=SLOT_MAX;
    }
    soundPlay(SND_NO,&lara->pos);
}
void Hole_collide(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    (void)cinfo;
    /* A finished slot keeps its PuzzleHole class after `type` becomes DONE (object.h:1160-1170). */
    if (Lara_claimBlocked(lara,self)) return;
    if (self->type >= ITEM_PUZZLEHOLE_1 && self->type <= ITEM_PUZZLEHOLE_DONE_4) {
        if (lara->state == Lara_STATE_USE_PUZZLE) {
            if (!Object_checkLimit(self,lara,LIMIT_HOLE) || !ItemObj_animIsEnd(lara,28)) return;
            if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE && self->type <= ITEM_PUZZLEHOLE_4)
                self->type += ITEM_PUZZLEHOLE_DONE_1-ITEM_PUZZLEHOLE_1;
            return;
        }
        Hole_apply(self,327,lara,Lara_STATE_USE_PUZZLE);
    } else Hole_apply(self,362,lara,Lara_STATE_USE_KEY);
}
