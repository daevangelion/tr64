/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory.h, common.cpp and level_stats.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "inventory-private.h"
#if TR64_HAS_TR2
#include "game/tr2/objects.h"
#endif
static int32 levelTitle(int32 level)
{
    switch (level) {
    case LVL_TR1_GYM: return STR_TR1_GYM;
    case LVL_TR1_1: return STR_TR1_LEVEL1;
    case LVL_TR1_2: return STR_TR1_LEVEL2;
    case LVL_TR1_3A: return STR_TR1_LEVEL3A;
    case LVL_TR1_3B: return STR_TR1_LEVEL3B;
    case LVL_TR1_4: return STR_TR1_LEVEL4;
    case LVL_TR1_5: return STR_TR1_LEVEL5;
    case LVL_TR1_6: return STR_TR1_LEVEL6;
    case LVL_TR1_7A: return STR_TR1_LEVEL7A;
    case LVL_TR1_7B: return STR_TR1_LEVEL7B;
    case LVL_TR1_8A: return STR_TR1_LEVEL8A;
    case LVL_TR1_8B: return STR_TR1_LEVEL8B;
    case LVL_TR1_8C: return STR_TR1_LEVEL8C;
    case LVL_TR1_10A: return STR_TR1_LEVEL10A;
    case LVL_TR1_10B: return STR_TR1_LEVEL10B;
    case LVL_TR1_10C: return STR_TR1_LEVEL10C;
    default: return STR_EMPTY;
    }
}
static int32 levelSecrets(int32 level)
{
    switch (level) {
    case LVL_TR1_3A: return 5;
    case LVL_TR1_4: return 4;
    case LVL_TR1_7B: return 2;
    case LVL_TR1_8C: return 1;
    case LVL_TR1_1: case LVL_TR1_2: case LVL_TR1_3B: case LVL_TR1_5:
    case LVL_TR1_6: case LVL_TR1_7A: case LVL_TR1_8A: case LVL_TR1_8B:
    case LVL_TR1_10A: case LVL_TR1_10B: case LVL_TR1_10C: return 3;
    default: return 0;
    }
}

int game_stats_view(tr64_game_level_stats_t *out)
{
    if(!gs || !out)return 0;
#if TR64_HAS_TR2
    if(TR64_RULE(FRESH_STOCK)) {
        /* tr2-oracle-trace, save: actual saved fields, no pickup tally. */
        *out=(tr64_game_level_stats_t){
            .time=gs->gSaveGame.time,.distance=gs->gSaveGame.distance,
            .mediUsed=gs->gSaveGame.mediUsed,.kills=gs->gSaveGame.kills,
            .secrets=gs->gSaveGame.secrets,.titleID=game_tr2_level_script_title_id(),
            .secretsMax=game_tr2_level_script_secrets_max(),
            .shots=gs->tr2->arms.ammoUsed,.hits=gs->tr2->ammoHits};
        return 1;
    }
#endif
    *out=(tr64_game_level_stats_t){
        gs->gSaveGame.time,gs->gSaveGame.distance,
        gs->gSaveGame.mediUsed,gs->gSaveGame.ammoUsed,gs->gSaveGame.kills,
        gs->gSaveGame.pickups,gs->gSaveGame.secrets,
        levelTitle(gs->gLevelID),levelSecrets(gs->gLevelID)
#if TR64_HAS_TR2
        ,0,0
#endif
    };
    return 1;
}
