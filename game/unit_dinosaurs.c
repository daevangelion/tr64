/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
extern uint32 tr64_unit_enemy_rex(uint32);
extern uint32 tr64_unit_enemy_raptor(uint32);
uint32 tr64_unit_enemy_dinosaurs(uint32 arg)
{
    GameItemCall *call = (GameItemCall *)(uintptr_t)arg;
    switch (call->self->type)
    {
    case ITEM_REX: return tr64_unit_enemy_rex(arg);
    case ITEM_RAPTOR: return tr64_unit_enemy_raptor(arg);
    default: sys_error("game enemy unit type mismatch");
    }
}
