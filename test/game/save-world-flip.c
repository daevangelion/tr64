/* SPDX-License-Identifier: MIT */
/* Exercise the real Core-backed restore helper with the loader's header-first
 * ordering. No cloned entities, fake Core, or derived-state save snapshot. */
#include "game/common/internal.h"
#include "game/common/save_world.h"
#include "tr64/engine.h"
#include "tr64/eng.h"
#include "tr64/render.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/res.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"

static uint32 worldDigest(void)
{
    uint32 hash=tr64_crc32(0,(const uint8 *)gs->sectorState,
        tr64_level_table(gs->core,TR64_CORE_sectors)->count*sizeof(Sector));
    hash=tr64_crc32(hash,(const uint8 *)gs->boxFlags,gs->level.boxesCount*sizeof(uint16));
    for (uint32 i=0;i<gs->level.roomsCount;i++)
        hash=tr64_crc32(hash,(const uint8 *)&gs->rooms[i].source,sizeof(gs->rooms[i].source));
    return hash;
}
static uint32 expectBindings(bool flipped)
{
    uint8 mate[MAX_ROOMS];
    uint32 pairs=0;
    memset(mate,NO_ROOM,sizeof(mate));
    for (uint32 i=0;i<gs->level.roomsCount;i++) {
        int32 other=tr64_i16(tr64_level_rooms(gs->core,i)->alternate_room);
        if (other >= 0) {
            mate[i]=(uint8)other;
            mate[other]=(uint8)i;
            pairs++;
        }
    }
    for (uint32 i=0;i<gs->level.roomsCount;i++) {
        uint32 selected=flipped && mate[i] != NO_ROOM ? mate[i] : i;
        const RoomInfo *info=tr64_level_rooms(gs->core,selected);
        if (gs->rooms[i].source != selected || gs->rooms[i].info != info ||
            gs->rooms[i].sectors != gs->sectorState+tr64_u32(info->sector_start))
            sys_error("save-world flip did not rebuild Core-relative room binding");
    }
    return pairs;
}
static void checkWorld(const char *name)
{
    for (uint32 flipped=0;flipped<=1;flipped++) {
        /* Put bindings in the opposite state, then apply the target header.
         * Calling a toggle helper with this equal flag would silently return. */
        gs->gSaveGame.flipped=(uint16)!flipped;
        if (!game_save_rebuild_world()) sys_error("save-world opposite baseline rejected");
        gs->gSaveGame.flipped=(uint16)flipped;
        if (!game_save_rebuild_world()) sys_error("save-world target rebuild rejected");
        uint32 pairs=expectBindings(flipped != 0);
        if (!pairs) sys_error("save-world test needs actual retail alternate rooms");
        uint32 before=worldDigest();
        uint32 bits=(uint8)gs->sectorState[0].floor^1u;
        gs->sectorState[0].floor=(int8)((int32)(bits&127u)-(int32)(bits&128u));
        if (worldDigest() == before) sys_error("save-world sector negative control did not change state");
        if (!game_save_rebuild_world() || worldDigest() != before)
            sys_error("save-world same-flag rebuild failed or doubled a block floor");
        expectBindings(flipped != 0);
        log_printf("save-world-flip: level=%s flip=%lu pairs=%lu digest=%08lx equal_header=1 repeated=1\n",
            name,(unsigned long)flipped,(unsigned long)pairs,(unsigned long)before);
    }
    for (uint32 i=0;i<gs->level.itemsCount;i++) {
        ItemObj *door=gs->items+i;
        if (door->type < ITEM_DOOR_1 || door->type > ITEM_DOOR_8) continue;
        int16 angle=door->angle.y;
        door->angle.y=1;
        uint32 before=tr64_crc32(0,(const uint8 *)gs,gs->bytes);
        if (game_save_rebuild_world() || tr64_crc32(0,(const uint8 *)gs,gs->bytes) != before)
            sys_error("save-world invalid preflight changed game state");
        door->angle.y=angle;
        return;
    }
    sys_error("save-world test needs an actual retail door");
}
void tr64_level_reload_probe(const char *requested)
{
    const char *names[]={"level3b","level6"};
    if (strcmp(requested,names[0])) sys_error("save-world probe starts in level3b");
    game_init();
    tr64_render_scene_set_live(1);
    for (uint32 i=0;i<2;i++) {
        mem_level_begin();
        code_loading_begin();
        tr64_scene_load(names[i]);
        code_loading_end();
        game_level_start(tr64_level_current(),names[i]);
        checkWorld(names[i]);
        tr64_render_scene_release();
        game_level_stop();
        eng_level_release();
        tr64_mem_stats_t state;
        tr64_mem_stats(TR64_MEM_state,&state);
        if (gs || state.used) sys_error("save-world probe retained released game state");
    }
    log_printf("save-world-flip PASS levels=2 targets=4 equal_header=1 repeated=1 invalid_nonmutation=1\n");
    uint16_t *pixels=mem_borrow_boot_card();
    tr64_boot_card(pixels);
    sys_present(pixels);
    log_flush();
}
