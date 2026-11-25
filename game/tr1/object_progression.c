/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/progression.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/hazard.h"
static const int16 LIMIT_MIDAS[]={-700,700,284,996,-700,700,ANGLE(10),ANGLE(30),ANGLE(10)};
static const int16 LIMIT_SCION_QUALOPEC[]={-256,256,540,740,-350,-200,ANGLE(10),0,0};
static const int16 LIMIT_SCION_HOLDER_PICKUP[]={-256,256,206,306,-862,-200,ANGLE(10),0,0};
void MidasHand_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    (void)cinfo;
    if (gs->players[1] && lara->room!=self->room)return;
    if (!(lara->flags & ITEM_FLAG_GRAVITY) && lara->state == Lara_STATE_STOP &&
        lara->pos.x > self->pos.x-512 && lara->pos.x < self->pos.x+512 &&
        lara->pos.z > self->pos.z-512 && lara->pos.z < self->pos.z+512) {
        ItemObj_animSet(lara,gs->level.models[ITEM_LARA_SPEC].animIndex+1,true,0);
        lara->state=lara->goalState=Lara_STATE_DEATH_MIDAS;
        lara->health=-1;
        lara->flags &= ~ITEM_FLAG_GRAVITY;
        lara->timer=-1;
        lara->extraL->weaponState=WEAPON_STATE_BUSY;
        lara->extraL->weapon=lara->extraL->goalWeapon=WEAPON_NONE;
        Camera *cam=&lara->extraL->camera;
        cam->mode=CAMERA_MODE_CUTSCENE;
        cam->timer=0;
        cam->view.pos=lara->pos;
        cam->view.room=lara->room;
        cam->angle.x=0;
        cam->angle.y=lara->angle.y;
        cam->targetAngle.y=0;
        cam->lookAtItem=NULL;
        return;
    }
    int32 slot=Lara_selectedUse(lara);
    if ((lara->flags & ITEM_FLAG_GRAVITY) || lara->state != Lara_STATE_STOP ||
        lara->extraL->weaponState != WEAPON_STATE_FREE ||
        (!(lara->input & IN_ACTION) && slot == SLOT_MAX)) return;
    uint16 quadrant=(uint16)(lara->angle.y+ANGLE_45)>>14;
    self->angle.y=wrap_i16((uint32)quadrant<<14);
    if (!Object_checkLimit(self,lara,LIMIT_MIDAS)) return;
    if (Lara_claimBlocked(lara,self)) return;
    if (slot == SLOT_MAX) {
        if (!gs->players[1] || (gs->inventory.state==INV_STATE_NONE && gs->inventory.useSlot==SLOT_MAX)) {
            Lara_claim(lara,self);
            inventory_openUse(lara,0);
        }
        return;
    }
    if (slot != SLOT_LEADBAR) { gs->inventory.useSlot=SLOT_MAX; return; }
    int32 lead=gs->inventory.counts[SLOT_LEADBAR], puzzle=gs->inventory.counts[SLOT_PUZZLE_1];
    gs->inventory.useSlot=SLOT_MAX;
    if (lead < 1 || lead > 32767 || puzzle < 0 || puzzle >= 32767 ||
        !inventory_addCount(SLOT_PUZZLE_1,1) || !inventory_consumeCount(SLOT_LEADBAR,1)) return;
    Lara_claim(lara,self);
    ItemObj_animSet(lara,gs->level.models[ITEM_LARA_SPEC].animIndex,true,0);
    lara->state=lara->goalState=Lara_STATE_USE_MIDAS;
    lara->extraL->weaponState=WEAPON_STATE_BUSY;
}
void ScionQualopecPickup_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    (void)cinfo;
    if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE) return;
    if (Lara_claimBlocked(lara,self)) return;
    self->angle.x=self->angle.z=0;
    self->angle.y=lara->angle.y;
    if (!Object_checkLimit(self,lara,LIMIT_SCION_QUALOPEC)) return;
    if (lara->state == Lara_STATE_PICKUP) {
        if (lara->frameIndex != tr64_u16(gs->level.anims[lara->animIndex].frame_start)+44 ||
            !inventory_addPickup(self->type)) return;
        gs->gSaveGame.pickups++;
        game_pickup_notify(self,lara);
        Room_remove(self->room,self);
        self->flags=(self->flags & ~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INVISIBLE;
        return;
    }
    if (!(lara->input & IN_ACTION) || lara->extraL->weaponState != WEAPON_STATE_FREE ||
        (lara->flags & ITEM_FLAG_GRAVITY) || lara->state != Lara_STATE_STOP) return;
    vec3i p=vec3i_create(0,640,-310);
    ItemObj_moveTo(lara,&p,self,false);
    Lara_claim(lara,self);
    ItemObj_animSet(lara,gs->level.models[ITEM_LARA_SPEC].animIndex,true,0);
    lara->state=lara->goalState=Lara_STATE_PICKUP;
    lara->extraL->weaponState=WEAPON_STATE_BUSY;
    gs->interactionAnim[Lara_slot(lara)]=(InteractionAnim){lara->animIndex,lara->frameIndex,lara->state,lara->goalState};
    Camera *cam=&lara->extraL->camera;
    cam->mode=CAMERA_MODE_CUTSCENE;
    cam->timer=0;
    cam->view.pos=lara->pos;
    cam->view.room=lara->room;
    cam->targetAngle.y=0;
}
void ScionHolderPickup_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    (void)cinfo;
    if (Lara_claimBlocked(lara,self)) return;
    self->angle.x=self->angle.z=0;
    self->angle.y=lara->angle.y;
    if (!Object_checkLimit(self,lara,LIMIT_SCION_HOLDER_PICKUP)) return;
    if (!(lara->input & IN_ACTION) || lara->extraL->weaponState != WEAPON_STATE_FREE ||
        (lara->flags & ITEM_FLAG_GRAVITY) || lara->state != Lara_STATE_STOP) return;
    vec3i p=vec3i_create(0,280,-407);
    ItemObj_moveTo(lara,&p,self,false);
    Lara_claim(lara,self);
    ItemObj_animSet(lara,gs->level.models[ITEM_LARA_SPEC].animIndex,true,0);
    lara->state=lara->goalState=Lara_STATE_PICKUP;
    lara->extraL->weaponState=WEAPON_STATE_BUSY;
    gs->interactionAnim[Lara_slot(lara)]=(InteractionAnim){lara->animIndex,lara->frameIndex,lara->state,lara->goalState};
    Camera *cam=&lara->extraL->camera;
    cam->mode=CAMERA_MODE_CUTSCENE;
    cam->timer=0;
    cam->view.pos=lara->pos;
    cam->view.room=lara->room;
    cam->targetAngle.y=-ANGLE_90;
}
void ScionTarget_hit(ItemObj *self,int32 damage)
{
    self->health=wrap_i16(self->health-wrap_i16(damage));
}
void ScionTarget_update(ItemObj *self)
{
    if (self->health > 0) {
        self->timer=0;
        ItemObj_animProcess(self,true);
        return;
    }
    if (self->health != -0x4000) {
        self->health=-0x4000;
        self->flags=(self->flags & ~(ITEM_FLAG_STATUS|ITEM_FLAG_COLLISION))|ITEM_FLAG_STATUS_INVISIBLE;
        if (self->room) {
            const Sector *sector=Room_getSector(self->room,self->pos.x,self->pos.z);
            Sector_getFloor(sector,self->pos.x,self->pos.y,self->pos.z);
            if (gs->gLastFloorData) game_check_trigger(gs->gLastFloorData,NULL);
            if (gs->stop.kind != TR64_GAME_STOP_NONE) return;
        }
        Room_remove(self->room,self);
    }
    if (self->timer%10 == 0) {
        ItemObj *effect=reserveExplosionSlot();
        if (effect) {
            vec3i p=self->pos;
            p.x=i32_add(p.x,(rand_logic()-0x4000)/32);
            p.y=i32_add(p.y,(rand_logic()-0x4000)/256-500);
            p.z=i32_add(p.z,(rand_logic()-0x4000)/32);
            initExplosionSlot(effect,self->room,&p,0);
            gs->playersExtra[0].camera.bounce=-200;
            if (gs->players[1])gs->playersExtra[1].camera.bounce=-200;
        }
    }
    self->timer++;
    if (self->timer == 90) ItemObj_deactivate(self);
}
