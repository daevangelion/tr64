/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_registry.h, hazard_spikes.h, hazard_damocles.h, hazard_ceiling.h and hazard_thor_lightning.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "hazard.h"
bool Hazard_update(ItemObj *self)
{
    switch(self->type) {
    case ITEM_TRAP_SPIKES: ItemObj_animProcess(self,true); return true;
    case ITEM_TRAP_BOULDER: TrapBoulder_update(self); return true;
    case ITEM_TRAP_SLAM: TrapSlam_update(self); return true;
    case ITEM_TRAP_SWORD: TrapDamocles_update(self); return true;
    case ITEM_TRAP_CEILING: case ITEM_TRAP_CEILING_2: TrapCeiling_update(self); return true;
    case ITEM_HAMMER_HANDLE: ThorHandle_update(self); return true;
    case ITEM_HAMMER_BLOCK: return true; /* Source ThorHead::update is empty. */
    case ITEM_LIGHTNING: LightningEmitter_update(self); return true;
    case ITEM_FLAME: Flame_update(self); return true;
    case ITEM_FLAME_EMITTER: TrapFlameEmitter_update(self); return true;
    case ITEM_TRAP_LAVA: TrapLavaWedge_update(self); return true;
    default: return false;
    }
}
bool Hazard_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    switch(self->type) {
    case ITEM_TRAP_SPIKES:
        if (lara && cinfo && lara->health > 0 && ItemObj_collideBounds(self,lara,cinfo))
            hazardDamageLara(lara,15,HAZARD_HIT_SPIKES,&self->pos);
        return true;
    case ITEM_TRAP_BOULDER: TrapBoulder_collide(self,lara,cinfo); return true;
    case ITEM_TRAP_CEILING: case ITEM_TRAP_CEILING_2:
        if (self->state == 1 && lara && cinfo && lara->health > 0 && ItemObj_collideBounds(self,lara,cinfo))
            hazardDamageLara(lara,300,HAZARD_HIT_CEILING,&self->pos); /* oracle hunk 24: 300 per frame of contact, not 1000 */
        return true;
    case ITEM_HAMMER_BLOCK:
        if (self->nextState == 3) Object_collideDefault(self,lara,cinfo);
        return true;
    case ITEM_LIGHTNING: Object_collideDefault(self,lara,cinfo); return true;
    case ITEM_TRAP_LAVA:
        if ((self->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE && lara && cinfo &&
            lara->health > 0 && ItemObj_collideBounds(self,lara,cinfo))
            hazardDamageLara(lara,1001,HAZARD_HIT_LAVA,&self->pos);
        return true;
    case ITEM_TRAP_SLAM: case ITEM_TRAP_SWORD: case ITEM_HAMMER_HANDLE: case ITEM_FLAME: case ITEM_FLAME_EMITTER:
        return true; /* Empty source collision (or inherited ItemObj empty collision). */
    default: return false;
    }
}
