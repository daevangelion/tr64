/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
extern uint32 tr64_unit_enemy_skater(uint32);
extern uint32 tr64_unit_enemy_cowboy(uint32);
extern uint32 tr64_unit_enemy_mrt(uint32);
uint32 tr64_unit_enemy_henchmen(uint32 arg)
{
    GameItemCall *call = (GameItemCall *)(uintptr_t)arg;
    switch (call->self->type)
    {
    case ITEM_SKATER: return tr64_unit_enemy_skater(arg);
    case ITEM_COWBOY: return tr64_unit_enemy_cowboy(arg);
    case ITEM_MR_T: return tr64_unit_enemy_mrt(arg);
    default: sys_error("game enemy unit type mismatch");
    }
}
