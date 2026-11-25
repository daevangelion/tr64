/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
extern uint32 tr64_unit_enemy_larson(uint32);
extern uint32 tr64_unit_enemy_pierre(uint32);
uint32 tr64_unit_enemy_gunmen(uint32 arg)
{
    GameItemCall *call = (GameItemCall *)(uintptr_t)arg;
    switch (call->self->type)
    {
    case ITEM_LARSON: return tr64_unit_enemy_larson(arg);
    case ITEM_PIERRE: return tr64_unit_enemy_pierre(arg);
    default: sys_error("game enemy unit type mismatch");
    }
}
