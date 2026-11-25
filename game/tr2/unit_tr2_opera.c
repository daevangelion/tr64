/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"

uint32_t tr64_unit_tr2_opera(uint32_t argument)
{
    const TR2OperaCall *call = (const TR2OperaCall *)(uintptr_t)argument;
    switch (call->op) {
    case TR2_OPERA_INIT:
        game_tr2_opera_init();
        return 0;
    case TR2_OPERA_UPDATE:
        return game_tr2_opera_update(call->self, call->lara);
    case TR2_OPERA_COLLIDE:
        return game_tr2_opera_collide(call->self, call->lara);
    default:
        sys_error("Tomb Raider II: unsupported opera unit operation %u", (unsigned)call->op);
    }
}
