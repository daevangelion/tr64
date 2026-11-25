/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "boat.h"
#include "game/common/object.h"

uint32_t tr64_unit_tr2_boat(uint32_t argument)
{
    TR2BoatCall *call = (TR2BoatCall *)(uintptr_t)argument;
    ItemObj *item = call->self;
    switch (call->op) {
    case TR2_BOAT_INIT:
        game_tr2_boat_init();
        return TR2_BOAT_RES_DONE;
    case TR2_BOAT_CAMERA:
        game_tr2_boat_camera(call->lara);
        return TR2_BOAT_RES_DONE;
    case TR2_BOAT_INTERACT:
        if (item->type == TR2_ITEM_BOAT)
            return game_tr2_boat_interact(item, call->lara) ? TR2_BOAT_RES_BUMP : TR2_BOAT_RES_DONE;
        if (item->type == TR2_ITEM_GONDOLA || item->type == TR2_ITEM_MINE) {
            if (item->animIndex != UINT16_MAX && ItemObj_collideBounds(item, call->lara, &gs->cinfo))
                return game_tr2_solid_contact(item, call->lara, true, false) == TR2_SOLID_BUMP ?
                       TR2_BOAT_RES_BUMP : TR2_BOAT_RES_DONE;
            return TR2_BOAT_RES_DONE;
        }
        return TR2_BOAT_RES_DONE;
    case TR2_BOAT_UPDATE:
        if (item->type == TR2_ITEM_BOAT)
            return game_tr2_boat_update_phase1(item, call->lara, call);
        if (item->type == TR2_ITEM_GONDOLA) {
            game_tr2_gondola_update(item);
            return TR2_BOAT_RES_DONE;
        }
        if (item->type == TR2_ITEM_RACE_CLOCK) {
            game_tr2_race_clock_update(item, call->lara);
            return TR2_BOAT_RES_DONE;
        }
        if (item->type == TR2_ITEM_MINE)
            return game_tr2_mine_update_phase1(item, call->lara, call);
        sys_error("Tomb Raider II: no boat-unit controller for type %u", (unsigned)item->type);
    case TR2_BOAT_UPDATE_POST:
        game_tr2_boat_update_post(item, call->lara, call->waterH);
        return TR2_BOAT_RES_DONE;
    case TR2_BOAT_MINE_POST:
        game_tr2_mine_update_post(item, call->lara, call->killedRider);
        return TR2_BOAT_RES_DONE;
    default:
        sys_error("Tomb Raider II: unsupported boat unit operation %u", (unsigned)call->op);
    }
}
