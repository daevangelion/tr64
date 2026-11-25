/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"

void game_tr2_creature_strike(TR2Creature *ai, Lara *lara, int damage, uint32 mask)
{
    ItemObj *item = ai->item;
    if (ai->flags || !(item->hitMask & mask)) return;
    if (lara->health > 0) {
        /* Q-T2K-12: the spider attempts blood before damage, cats and
         * crows after it. Full effects capacity never suppresses damage. */
        if (item->type == TR2_ITEM_SPIDER)
            game_tr2_bite_effect(item, TR2_SPIDER_BITE_JOINT, vec3i_create(0, 0, 41), 0);
        lara->health -= damage;
        lara->flags |= ITEM_FLAG_INJURED;
        if (item->type == TR2_ITEM_TIGER)
            game_tr2_bite_effect(item, TR2_TIGER_BITE_JOINT, vec3i_create(19, -13, 3), ai->jointYaw[0]);
        else if (item->type == TR2_ITEM_CROW)
            game_tr2_bite_effect(item, TR2_CROW_BITE_JOINT, vec3i_create(2, 10, 60), 0);
    }
    ai->flags = 1;
}

uint32_t tr64_unit_tr2_creatures(uint32_t argument)
{
    const TR2SpeciesCall *call = (const TR2SpeciesCall *)(uintptr_t)argument;
    ItemObj *item = call->ai->item;
    switch (call->op) {
    case TR2_SPECIES_CONTROL:
        switch (item->type) {
        case TR2_ITEM_TIGER:
            /* 5 is the controller roar literal, not a numbered spec state (Q-HARD-02). */
            if (item->state < 1 || item->state > 8)
                sys_error("Tomb Raider II: tiger state %u unknown", item->state);
            game_tr2_tiger_control(call->ai, call->lara, call->interest);
            return 0;
        case TR2_ITEM_CROW:
            if (item->state != 1 && item->state != 2 && item->state != 3 &&
                item->state != 6 && item->state != 7)
                sys_error("Tomb Raider II: crow state %u unknown", item->state);
            game_tr2_crow_control(call->ai, call->lara, call->interest);
            return 0;
        case TR2_ITEM_SPIDER:
            if (item->state < 1 || item->state > 6)
                sys_error("Tomb Raider II: spider state %u unknown", item->state);
            game_tr2_spider_control(call->ai, call->lara, call->interest);
            return 0;
        case TR2_ITEM_REX:
            game_tr2_rex_control(call->ai, call->lara, call->interest);
            return 0;
        default:
            sys_error("Tomb Raider II: unknown creature species %u", (unsigned)item->type);
        }
    case TR2_SPECIES_DEATH:
        switch (item->type) {
        case TR2_ITEM_SPIDER: game_tr2_spider_death(call->ai); return TR2_DEATH_DONE;
        case TR2_ITEM_CROW: game_tr2_crow_death(call->ai); return TR2_DEATH_STRIDE_ORDINAL;
        case TR2_ITEM_REX: game_tr2_rex_death(call->ai); return TR2_DEATH_STRIDE;
        case TR2_ITEM_TIGER: game_tr2_tiger_death(call->ai); return TR2_DEATH_STRIDE;
        default:
            sys_error("Tomb Raider II: unsupported creature death type %u", (unsigned)item->type);
        }
    default:
        sys_error("Tomb Raider II: unsupported creature unit operation %u", (unsigned)call->op);
    }
}
