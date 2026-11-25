/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
extern uint32 tr64_unit_enemy_wolf(uint32);
extern uint32 tr64_unit_enemy_bear(uint32);
extern uint32 tr64_unit_enemy_bat(uint32);
uint32 tr64_unit_enemy_peru(uint32 arg)
{
    GameItemCall *call = (GameItemCall *)(uintptr_t)arg;
    switch (call->self->type)
    {
    case ITEM_WOLF: return tr64_unit_enemy_wolf(arg);
    case ITEM_BEAR: return tr64_unit_enemy_bear(arg);
    case ITEM_BAT: return tr64_unit_enemy_bat(arg);
    default: sys_error("game enemy unit type mismatch");
    }
}
