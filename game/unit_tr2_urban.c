/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "tr2/enemy_urban.h"

uint32_t tr64_unit_tr2_urban(uint32_t argument)
{
    const TR2UrbanCall *call = (const TR2UrbanCall *)(uintptr_t)argument;
    switch (call->op) {
    case TR2_URBAN_CONTROL:
        switch (call->ai->item->type) {
        case TR2_ITEM_DOBERMAN:
            game_tr2_doberman_control(call->ai, call->lara, call->interest);
            return 0;
        case TR2_ITEM_GUNMAN_1: case TR2_ITEM_GUNMAN_2: case TR2_ITEM_GUNMAN_3:
            game_tr2_gunman_control(call->ai, call->lara, call->interest);
            return 0;
        case TR2_ITEM_OPERA_BOSS:
            game_tr2_boss_control(call->ai, call->lara, call->interest, call->turn);
            return 0;
        case TR2_ITEM_RAT:
            game_tr2_rat_control(call->ai, call->lara, call->interest);
            return 0;
        case TR2_ITEM_THUG:
            game_tr2_thug_control(call->ai, call->lara, call->interest);
            return 0;
        default:
            sys_error("Tomb Raider II: unknown urban species %u", (unsigned)call->ai->item->type);
        }
    case TR2_URBAN_DEATH:
        switch (call->ai->item->type) {
        case TR2_ITEM_DOBERMAN: game_tr2_doberman_death(call->ai); return TR2_DEATH_STRIDE;
        case TR2_ITEM_GUNMAN_1: case TR2_ITEM_GUNMAN_2: case TR2_ITEM_GUNMAN_3:
            game_tr2_gunman_death(call->ai); return TR2_DEATH_STRIDE;
        case TR2_ITEM_OPERA_BOSS:
            game_tr2_boss_death(call->ai); return TR2_DEATH_STRIDE;
        case TR2_ITEM_RAT: game_tr2_rat_death(call->ai); return TR2_DEATH_STRIDE;
        case TR2_ITEM_THUG: game_tr2_thug_death(call->ai); return TR2_DEATH_STRIDE;
        default:
            sys_error("Tomb Raider II: unknown urban death species %u", (unsigned)call->ai->item->type);
        }
    case TR2_URBAN_WINDOW_UPDATE:
        game_tr2_window_update(call->self, call->lara);
        return 0;
    case TR2_URBAN_WINDOW_INTERACT:
        return game_tr2_window_interact(call->self, call->lara) ? 1u : 0u;
    case TR2_URBAN_WINDOW_RAY:
        return game_tr2_window_check_ray(call->from, call->to) ? 1u : 0u;
    default:
        sys_error("Tomb Raider II: unsupported urban unit operation %u", (unsigned)call->op);
    }
}
