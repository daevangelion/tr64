/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h and tr1_campaign.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#if TR64_HAS_TR2
#include "tr64/code.h"
#endif
#include <stdarg.h>
#include "engine/core/fmt.h"

const tr64_game_stop_t *game_stop_info(void)
{
    return gs ? &gs->stop : NULL;
}
void game_stop_set(uint32 kind, int32 next_level_id, const char *format, ...)
{
    va_list args;
    int count;
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
    va_start(args, format);
    count = tr64_vsnprintf(gs->stop.name, sizeof(gs->stop.name), format, args);
    va_end(args);
    if (count < 0 || (uint32)count >= sizeof(gs->stop.name))
        sys_error("game stop name formatting");
    gs->stop.next_level_id = next_level_id;
    gs->stop.kind = kind;
}
int32 game_next_level_id(void)
{
    /* Default campaign tr1CampaignNextLevel increments through cinematic ids;
     * tr1CampaignResolveTransition returns to title after the Great Pyramid. */
    return gs->gLevelID == LVL_TR1_10C ? LVL_TR1_TITLE : gs->gLevelID + 1;
}
void game_level_stop(void)
{
    if (!gs)
        return;
#if TR64_HAS_TR2
    if (gs->tr2) {
        code_loading_begin();
        if (code_is_loaded("tr2_wall"))
            code_unload("tr2_wall");
        if (gs->tr2->aiUnit)
            code_unload("tr2_ai");
        if (gs->tr2->creatureUnit)
            code_unload("tr2_creatures");
        if (gs->tr2->boatUnit)
            code_unload("tr2_boat");
        if (gs->tr2->urbanUnit)
            code_unload("tr2_urban");
        if (gs->tr2->operaUnit)
            code_unload("tr2_opera");
        code_loading_end();
    } else
#endif
#if !TR64_CART_TR2
    game_unload_item_units();
#else
        sys_error("Tomb Raider II: release without sequel state");
#endif
    gs = NULL;
}
