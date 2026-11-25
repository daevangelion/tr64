/* SPDX-License-Identifier: MIT */
/* Production loaders, game state and cold codec; size queries allocate nothing. */
#include <string.h>
#include "tr64/game.h"
#include "tr64/level.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/route.h"
#include "tr64/render.h"
#include "tr64/engine.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/snd/snd.h"
extern int tr64_game_save_check(void *memory,uint32_t capacity);

void tr64_level_reload_probe(const char *requested)
{
    static const char *const names[]={"gym","level1","level2","level3a","level3b",
        "level4","level5","level6","level7a","level7b","level8a","level8b",
        "level8c","level10a","level10b","level10c"};
    uint32_t largest=0,code_peak=0;
    if(strcmp(requested,"level1"))sys_error("save measure initial route identity");
    tr64_render_scene_set_live(1);game_init();
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        tr64_mem_stats_t code,level,state;
        mem_level_begin();code_loading_begin();tr64_scene_load(names[i]);code_loading_end();
        game_level_start(tr64_level_current(),names[i]);
        tr64_snd_level_start(tr64_level_current());
        uint32_t hash=game_state_hash(),bytes=game_save(NULL,0);
        if(!bytes || game_state_hash()!=hash)sys_error("save size failed/mutated %s",names[i]);
        tr64_mem_stats(TR64_MEM_code,&code);tr64_mem_stats(TR64_MEM_level,&level);
        tr64_mem_stats(TR64_MEM_state,&state);
        if(code.used>code.budget-8192)sys_error("save/inv leave fewer than two code frames: %s",names[i]);
        if(bytes>largest)largest=bytes;
        if(code.used>code_peak)code_peak=code.used;
        log_printf("save-size: level=%s tick=0 bytes=%lu code=%lu state=%lu level_free=%lu state_free=%lu\n",
            names[i],(unsigned long)bytes,(unsigned long)code.used,(unsigned long)state.used,
            (unsigned long)(level.budget-level.used),(unsigned long)(state.budget-state.used));
        uint32_t check_bytes=bytes*2+32;
        if(check_bytes>state.budget-state.used)
            sys_error("source save check lacks caller state buffer: %s",names[i]);
        void *memory=tr64_mem_alloc(TR64_MEM_state,check_bytes);
        if(!tr64_game_save_check(memory,check_bytes))
            sys_error("source save roundtrip/corruption check failed: %s",names[i]);
        log_printf("save-check PASS level=%s bytes=%lu\n",names[i],(unsigned long)bytes);
        tr64_snd_level_stop();game_level_stop();tr64_render_scene_release();eng_level_release();
    }
    log_printf("save-measure PASS levels=16 initial_max=%lu code_max=%lu free_code_frames=%lu\n",
        (unsigned long)largest,(unsigned long)code_peak,(unsigned long)((98304-code_peak)/4096));
    uint16_t *pixels=mem_borrow_boot_card();tr64_boot_card(pixels);sys_present(pixels);log_flush();
}
