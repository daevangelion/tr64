/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
extern uint32 tr64_unit_enemy_crocodile(uint32);
extern uint32 tr64_unit_enemy_lion(uint32);
extern uint32 tr64_unit_enemy_gorilla(uint32);
uint32 tr64_unit_enemy_greece(uint32 arg)
{
    GameItemCall *call = (GameItemCall *)(uintptr_t)arg;
    switch (call->self->type)
    {
    case ITEM_CROCODILE_LAND: return tr64_unit_enemy_crocodile(arg);
    case ITEM_CROCODILE_WATER: return tr64_unit_enemy_crocodile(arg);
    case ITEM_LION_MALE: return tr64_unit_enemy_lion(arg);
    case ITEM_LION_FEMALE: return tr64_unit_enemy_lion(arg);
    case ITEM_PUMA: return tr64_unit_enemy_lion(arg);
    case ITEM_GORILLA: return tr64_unit_enemy_gorilla(arg);
    default: sys_error("game enemy unit type mismatch");
    }
}
