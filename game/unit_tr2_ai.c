/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "tr2/creature.h"

uint32_t tr64_unit_tr2_ai(uint32_t argument)
{
    TR2AiCall *call = (TR2AiCall *)(uintptr_t)argument;
    switch (call->op) {
    case TR2_AI_ACTIVATE:
        return game_tr2_creature_wake(call->self, (call->self->flags & ITEM_FLAG_STATUS) == 0);
    case TR2_AI_PREPARE:
        return game_tr2_ai_prepare(call);
    case TR2_AI_STRIDE:
        game_tr2_ai_stride(call->ai, call->turn);
        return 0;
    case TR2_AI_DEATH_STRIDE:
        game_tr2_ai_death_stride(call->ai, call->turn);
        return 0;
    case TR2_AI_COLLIDE:
        game_tr2_creature_contact(call->self, call->lara);
        return 0;
    default:
        sys_error("Tomb Raider II: unsupported AI unit operation %u", (unsigned)call->op);
    }
}
