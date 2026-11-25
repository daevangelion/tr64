/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"
#include "game/common/object.h"

static uint32 interactItem(ItemObj *item, Lara *lara)
{
    if (tr2DoorType(item->type))
        return game_tr2_solid_contact(item,lara,true,true)==TR2_SOLID_BUMP ?
               TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
    if (TR64_RULE(WALL_HAZARDS)) {
        switch (item->type) {
        case TR2_ITEM_SPIKES:
            game_tr2_spike_contact(item,lara);
            return TR2_INTERACT_NONE;
        case TR2_ITEM_GUILLOTINE:
            game_tr2_blade_contact(item,lara);
            return TR2_INTERACT_NONE;
        case TR2_ITEM_STATUE_BLADE:
            return game_tr2_statue_blade_contact(item,lara);
        case TR2_ITEM_DISC: case TR2_ITEM_ROLLING_BLADE: case TR2_ITEM_SPIKE_WALL:
            game_tr2_moving_contact(item,lara);
            return TR2_INTERACT_NONE;
        case TR2_ITEM_BOULDER:
            game_tr2_boulder_contact(item,lara);
            return TR2_INTERACT_NONE;
        }
    }
    if (game_tr2_pickup_type(item->type))
        game_tr2_interact_pickup(item,lara);
    else if (item->type == TR2_ITEM_WALL_SWITCH)
        game_tr2_interact_switch(item,lara);
    else if (game_tr2_extra_switch_type(item->type))
        (void)game_tr2_extra_switch_interact(item,lara);
    else if (item->type >= 182 && item->type <= 189)
        return game_tr2_interact_puzzle(item,lara);
    else if (tr2BlockType(item->type))
        Block_collide(item,lara,&gs->cinfo);
    else if (item->type == TR2_ITEM_DETONATOR)
        return game_tr2_interact_detonator(item,lara);
    else
        return game_tr2_check_deferred(item,lara);
    return TR2_INTERACT_NONE;
}

uint32_t tr64_unit_tr2_wall(uint32_t argument)
{
    const GameItemCall *call=(const GameItemCall *)(uintptr_t)argument;
    ItemObj *item=call->self;
    if (call->op == GAME_ITEM_COLLIDE) {
        if (call->x == 1) {
            if (item->type == TR2_ITEM_DETONATOR)
                return game_tr2_finish_detonator(item,call->lara);
            if (item->type >= 182 && item->type <= 185) {
                game_tr2_finish_puzzle(item,call->lara,true);
                return TR2_INTERACT_NONE;
            }
            game_tr2_finish_keyhole(item,call->lara);
            return TR2_INTERACT_NONE;
        }
        return interactItem(item,call->lara);
    } else if (tr2DoorType(item->type)) {
        bool open = Object_isActive(item);
        if (item->state == (unsigned)open)
            game_door_action(item,!open);
        else
            item->goalState = open;
        if (TR64_RULE(INTERACTIONS))
            item->hitMask = 0; /* Q-INT-03: active animation clears old touch. */
        ItemObj_animProcess(item,true);
    } else if ((item->type == TR2_ITEM_WALL_SWITCH || game_tr2_extra_switch_type(item->type)) &&
               TR64_RULE(INTERACTIONS)) {
        game_tr2_switch_update(item);
    } else if (item->type == TR2_ITEM_GRENADE_PROJ) {
        game_tr2_grenade_update(item);
    } else if (item->type == TR2_ITEM_BOULDER && TR64_RULE(INTERACTIONS)) {
        return game_tr2_boulder_update(item,call->x,call->z);
    } else if (tr2BlockType(item->type)) {
        Block_update(item);
    } else if (item->type == TR2_ITEM_COLLAPSE || item->type == TR2_ITEM_COLLAPSE_HIGH) {
        TrapFloor_update(item);
    } else if (tr2TrapdoorType(item->type)) {
        (void)Door_animate(item);
    } else if (item->type == TR2_ITEM_DETONATOR && TR64_RULE(INTERACTIONS)) {
        game_tr2_detonator_update(item);
    } else if (item->type == TR2_ITEM_GUILLOTINE && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_blade_update(item,call->lara);
    } else if (item->type == TR2_ITEM_STATUE_BLADE && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_statue_blade_update(item,call->lara);
    } else if (item->type == TR2_ITEM_ZIPLINE && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_zipline_update(item,call->lara);
    } else if (item->type == TR2_ITEM_LAUNCHER && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_disc_launch(item);
        game_tr2_launcher_update(item);
    } else if (item->type == TR2_ITEM_DISC && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_disc_harm(item,call->lara);
        if (game_tr2_disc_update(item))
            game_tr2_disc_removed(item);
    } else if (item->type == TR2_ITEM_ROLLING_BLADE && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_rolling_harm(item,call->lara);
        game_tr2_rolling_update(item);
    } else if (item->type == TR2_ITEM_SPIKE_WALL && TR64_RULE(WALL_HAZARDS)) {
        game_tr2_spike_wall_harm(item,call->lara);
        game_tr2_spike_wall_update(item);
    } else if (item->type==TR2_ITEM_HELICOPTER)
        game_tr2_update_helicopter(item,call->lara);
    else if (item->type==TR2_ITEM_CAMPFIRE)
        game_tr2_update_campfire(item,call->lara);
    else if (TR64_RULE(ARMS) && item->type==TR2_ITEM_LOOSE_FLARE)
        game_tr2_update_world_flare(item);
    else
        sys_error("Tomb Raider II: no Wall controller for type %u",(unsigned)item->type);
    return 0;
}
