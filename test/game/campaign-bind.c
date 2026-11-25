/* SPDX-License-Identifier: MIT */
/* One normal loader/renderer/game lifecycle across the canonical TITLE pack. */
#include <string.h>
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/engine.h"
#include "tr64/game.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/render.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"

/* Mandatory production hooks: omitting sound must fail the link, not skip proof. */
extern void tr64_snd_level_start(const tr64_level_t *level);
extern void tr64_snd_level_stop(void);
extern void tr64_snd_tick(void);
extern void tr64_snd_pump(void);
extern void tr64_snd_report(void);

static void released(const char *name)
{
    static const int pools[] = {TR64_MEM_level,TR64_MEM_state,TR64_MEM_code,TR64_MEM_io};
    tr64_mem_stats_t stats;
    const tr64_level_t *level = tr64_level_current();
    uint32_t image_bytes = 0;
    for(unsigned i=0;i<sizeof(pools)/sizeof(pools[0]);++i) {
        tr64_mem_stats(pools[i],&stats);
        if(stats.used)sys_error("campaign bind release retained %s",stats.name);
    }
    for(int i=0;i<TR64_LEVEL_COUNT;++i) {
        mem_level_stats(i,&stats);
        if(stats.base || stats.budget || stats.used || stats.peak)
            sys_error("campaign bind release retained section %s",stats.name);
    }
    if(level->ready || level->textures || level->sound_metadata || game_stop_info() ||
       tr64_level_title_image(level,&image_bytes) || image_bytes || tr64_render_scene_counts())
        sys_error("campaign bind release retained published assets");
    log_printf("campaign-bind: released=%s level=0 state=0 code=0 io=0\n",name);
}

void tr64_level_reload_probe(const char *requested)
{
    /* Kept in the same order as campaign-pack.py PACK_LEVELS; the host report
     * compares the complete sequence, not only the final success marker. */
    static const char *const names[] = {
        "title","gym","level1","level2","level3a","level3b","level4","level5",
        "level6","level7a","level7b","level8a","level8b","level8c",
        "level10a","level10b","level10c"
    };
    unsigned ticks = 0,frames = 0;
    if(strcmp(requested,names[0]))sys_error("campaign bind requires title first");
    tr64_render_scene_set_live(1);
    game_init();
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        tr64_mem_stats_t arena,core,tex,rooms,meshes,state,audio;
        uint32_t image_bytes = 0;
        const uint8_t *image;
        const tr64_level_t *level;
        mem_level_begin();
        code_loading_begin();tr64_scene_load(names[i]);code_loading_end();
        level = tr64_level_current();
        image = tr64_level_title_image(level,&image_bytes);
        if(!level->ready || !level->textures || !level->texture_bytes ||
           !level->sound_metadata || !level->sound_metadata_bytes)
            sys_error("campaign bind missing published Core/TX/SN: %s",names[i]);
        if((i==0 && (!image || !image_bytes || game_stop_info())) ||
           (i!=0 && (image || image_bytes)))
            sys_error("campaign bind TITLE image/game distinction: %s",names[i]);
        mem_level_stats(TR64_LEVEL_core,&core);mem_level_stats(TR64_LEVEL_tex,&tex);
        mem_level_stats(TR64_LEVEL_rooms,&rooms);mem_level_stats(TR64_LEVEL_meshes,&meshes);
        if(!core.used || core.used>core.budget || !tex.used || tex.used>tex.budget ||
           !rooms.budget || rooms.used!=rooms.budget ||
           !meshes.budget || meshes.used!=meshes.budget)
            sys_error("campaign bind incomplete resident payloads: %s",names[i]);
        if(i) {
            tr64_game_view_t view;
            const tr64_game_stop_t *stop;
            tr64_game_drawable_t *drawables;
            game_level_start(level,names[i]);tr64_snd_level_start(level);
            game_tick(0);tr64_snd_tick();tr64_snd_pump();
            stop=game_stop_info();
            if(!stop || stop->kind!=TR64_GAME_STOP_NONE)
                sys_error("campaign bind one-tick stopped: %s",names[i]);
            drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*drawables));
            tr64_render_game_scene_prepare();game_view_camera(&view);
            tr64_render_game_scene_frame(&view,drawables,TR64_GAME_DRAW_CHUNK);
            if(!tr64_render_scene_counts())sys_error("campaign bind no completed frame: %s",names[i]);
            ++ticks;++frames;
        }
        tr64_mem_stats(TR64_MEM_level,&arena);tr64_mem_stats(TR64_MEM_state,&state);
        tr64_mem_stats(TR64_MEM_audio,&audio);
        if(arena.used>arena.budget || state.used>state.budget || audio.used>audio.budget)
            sys_error("campaign bind exceeded fixed pool: %s",names[i]);
        log_printf("campaign-bind: loaded=%s ordinal=%u ticks=%u image_bytes=%lu level=%lu budget=%lu core=%lu tex=%lu rooms=%lu meshes=%lu state=%lu audio=%lu\n",
            names[i],i,i?1u:0u,(unsigned long)image_bytes,(unsigned long)arena.used,
            (unsigned long)arena.budget,(unsigned long)core.budget,(unsigned long)tex.budget,
            (unsigned long)rooms.budget,(unsigned long)meshes.budget,(unsigned long)state.used,(unsigned long)audio.used);
        tr64_snd_report();log_flush();
        tr64_snd_level_stop();
        if(i)game_level_stop();
        tr64_render_scene_release();eng_level_release();released(names[i]);
    }
    log_printf("campaign-bind PASS loads=17 ticks=%u frames=%u title_asset_only=1 releases=17\n",ticks,frames);
    uint16_t *pixels=mem_borrow_boot_card();tr64_boot_card(pixels);sys_present(pixels);
    log_flush();
}
