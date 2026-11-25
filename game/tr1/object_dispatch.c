/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h, object.h and hazard_registry.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/object.h"
#include "game/common/hazard.h"
void Pickup_collide(ItemObj *,Lara *,CollisionInfo *);
void Switch_update(ItemObj *);
void Switch_collide(ItemObj *,Lara *,CollisionInfo *);
void SwitchWater_collide(ItemObj *,Lara *,CollisionInfo *);
void Hole_collide(ItemObj *,Lara *,CollisionInfo *);
void Block_collide(ItemObj *,Lara *,CollisionInfo *);
void Block_update(ItemObj *);
void TrapFloor_update(ItemObj *);
void DrawBridge_update(ItemObj *);
void Cabin_update(ItemObj *);
void MovingBlock_update(ItemObj *);
void MovingBar_update(ItemObj *);
void DrawBridge_collide(ItemObj *,Lara *,CollisionInfo *);
void TrapSwingBlade_collide(ItemObj *,Lara *,CollisionInfo *);
void TrapSwingBlade_update(ItemObj *);
void Dart_collide(ItemObj *,Lara *,CollisionInfo *);
void Dart_update(ItemObj *);
void TrapDartEmitter_update(ItemObj *);
void MidasHand_collide(ItemObj *,Lara *,CollisionInfo *);
void ScionQualopecPickup_collide(ItemObj *,Lara *,CollisionInfo *);
void ScionHolderPickup_collide(ItemObj *,Lara *,CollisionInfo *);
void ScionTarget_update(ItemObj *);
void LavaEmitter_update(ItemObj *);
void LavaParticle_update(ItemObj *);
void Object_update(ItemObj *self)
{
    if (Hazard_update(self)) return;
    if (Object_isPickup(self->type) || self->type == ITEM_SCION_PICKUP_QUALOPEC) {
        if (gs->level.models[self->type].count > 0) ItemObj_animProcess(self,true);
        return;
    }
    switch(self->type) {
    case ITEM_SCION_TARGET: ScionTarget_update(self); return;
    case ITEM_CUT_1: case ITEM_CUT_2: case ITEM_CUT_3: case ITEM_CUT_4: Cut_update(self); return;
    case ITEM_MIDAS_HAND: case ITEM_SCION_PICKUP_HOLDER: case ITEM_SCION_HOLDER:
        ItemObj_animProcess(self,true); return;
    case ITEM_SWITCH: case ITEM_SWITCH_WATER: Switch_update(self); return;
    case ITEM_TRAP_FLOOR: TrapFloor_update(self); return;
    case ITEM_TRAP_DART_EMITTER: TrapDartEmitter_update(self); return;
    case ITEM_DART: Dart_update(self); return;
    case ITEM_TRAP_SWING_BLADE: TrapSwingBlade_update(self); return;
    case ITEM_BLOCK_1: case ITEM_BLOCK_2: case ITEM_BLOCK_3: case ITEM_BLOCK_4: Block_update(self); return;
    case ITEM_DOOR_1: case ITEM_DOOR_2: case ITEM_DOOR_3: case ITEM_DOOR_4:
    case ITEM_DOOR_5: case ITEM_DOOR_6: case ITEM_DOOR_7: case ITEM_DOOR_8:
    case ITEM_TRAP_DOOR_1: case ITEM_TRAP_DOOR_2: {
        bool active=Door_animate(self);
#line 54
        if (self->type >= ITEM_DOOR_1 && self->type <= ITEM_DOOR_8 && self->state == (active ? 1 : 0))
            game_door_action(self,!active);
        return;
    }
    case ITEM_DRAWBRIDGE: DrawBridge_update(self); return;
    case ITEM_CABIN: Cabin_update(self); return;
    case ITEM_MOVING_BLOCK: MovingBlock_update(self); return;
    case ITEM_MOVING_OBJECT: MovingBar_update(self); return;
    case ITEM_GEARS_1: case ITEM_GEARS_2: case ITEM_GEARS_3:
        self->goalState=Object_isActive(self) ? 1 : 0;
        ItemObj_animProcess(self,true);
        return;
    case ITEM_LAVA_EMITTER: LavaEmitter_update(self); return;
    case ITEM_LAVA_PARTICLE: LavaParticle_update(self); return;
    case ITEM_CRYSTAL: case ITEM_VIEW_TARGET: case ITEM_WATERFALL:
    case ITEM_PUZZLEHOLE_1: case ITEM_PUZZLEHOLE_2: case ITEM_PUZZLEHOLE_3: case ITEM_PUZZLEHOLE_4:
    /* PuzzleHole::collide only changes `type`; the object stays a PuzzleHole (object.h:1160-1166). */
    case ITEM_PUZZLEHOLE_DONE_1: case ITEM_PUZZLEHOLE_DONE_2: case ITEM_PUZZLEHOLE_DONE_3: case ITEM_PUZZLEHOLE_DONE_4:
    case ITEM_KEYHOLE_1: case ITEM_KEYHOLE_2: case ITEM_KEYHOLE_3: case ITEM_KEYHOLE_4:
        ItemObj_animProcess(self,true); return;
    /* item.h:1809,1967 constructs plain ItemObj; Skater::syncBoard owns its pose. */
    case ITEM_SKATEBOARD:
    case ITEM_BRIDGE_FLAT: case ITEM_BRIDGE_TILT_1: case ITEM_BRIDGE_TILT_2:
    case ITEM_BOAT: case ITEM_EARTHQUAKE:
        return; /* ItemObj::update is empty in the reference. */
    default:
        game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game object update not ported: type=%u",self->type);
        return;
    }
}
void Object_collide(ItemObj *self,Lara *lara,CollisionInfo *cinfo)
{
    if (Hazard_collide(self,lara,cinfo)) return;
    if (Object_isPickup(self->type)) { Pickup_collide(self,lara,cinfo); return; }
    switch(self->type) {
    case ITEM_MIDAS_HAND: MidasHand_collide(self,lara,cinfo); return;
    case ITEM_SCION_PICKUP_QUALOPEC: ScionQualopecPickup_collide(self,lara,cinfo); return;
    case ITEM_SCION_PICKUP_HOLDER: ScionHolderPickup_collide(self,lara,cinfo); return;
    case ITEM_SCION_HOLDER: Object_collideDefault(self,lara,cinfo); return;
    case ITEM_SCION_TARGET: return;
    case ITEM_SWITCH: Switch_collide(self,lara,cinfo); return;
    case ITEM_SWITCH_WATER: SwitchWater_collide(self,lara,cinfo); return;
    case ITEM_DART: Dart_collide(self,lara,cinfo); return;
    case ITEM_TRAP_SWING_BLADE: TrapSwingBlade_collide(self,lara,cinfo); return;
    case ITEM_BLOCK_1: case ITEM_BLOCK_2: case ITEM_BLOCK_3: case ITEM_BLOCK_4: Block_collide(self,lara,cinfo); return;
    case ITEM_DRAWBRIDGE: DrawBridge_collide(self,lara,cinfo); return;
    case ITEM_CABIN: case ITEM_MOVING_OBJECT: Object_collideDefault(self,lara,cinfo); return;
    case ITEM_PUZZLEHOLE_1: case ITEM_PUZZLEHOLE_2: case ITEM_PUZZLEHOLE_3: case ITEM_PUZZLEHOLE_4:
    case ITEM_PUZZLEHOLE_DONE_1: case ITEM_PUZZLEHOLE_DONE_2: case ITEM_PUZZLEHOLE_DONE_3: case ITEM_PUZZLEHOLE_DONE_4:
    case ITEM_KEYHOLE_1: case ITEM_KEYHOLE_2: case ITEM_KEYHOLE_3: case ITEM_KEYHOLE_4:
        Hole_collide(self,lara,cinfo); return;
    case ITEM_CRYSTAL: case ITEM_DOOR_1: case ITEM_DOOR_2: case ITEM_DOOR_3: case ITEM_DOOR_4:
    case ITEM_DOOR_5: case ITEM_DOOR_6: case ITEM_DOOR_7: case ITEM_DOOR_8:
    case ITEM_TRAP_DOOR_1: case ITEM_TRAP_DOOR_2: case ITEM_TRAP_FLOOR: case ITEM_TRAP_DART_EMITTER:
    case ITEM_GEARS_1: case ITEM_GEARS_2: case ITEM_GEARS_3:
    case ITEM_VIEW_TARGET: case ITEM_WATERFALL: case ITEM_LAVA_EMITTER: case ITEM_LAVA_PARTICLE:
    case ITEM_SKATEBOARD:
    case ITEM_MOVING_BLOCK:
    case ITEM_BRIDGE_FLAT: case ITEM_BRIDGE_TILT_1: case ITEM_BRIDGE_TILT_2:
    case ITEM_BOAT: case ITEM_EARTHQUAKE:
        return; /* The selected source class inherits empty collision (Door/Crystal explicit). */
    default:
        game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game object collision not ported: type=%u",self->type);
        return;
    }
}
uint32 tr64_unit_objects(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ASSERT(call && call->self);
    switch (call->op) {
    case GAME_ITEM_UPDATE: Object_update(call->self); break;
    case GAME_ITEM_COLLIDE: Object_collide(call->self,call->lara,&gs->cinfo); break;
    case GAME_ITEM_HIT:
        if (call->self->type == ITEM_SCION_TARGET) ScionTarget_hit(call->self,call->damage);
        break; /* Other object classes inherit ItemObj::hit (empty). */
    default: sys_error("game objects unit operation %u",(unsigned)call->op);
    }
    return call->result=1;
}
