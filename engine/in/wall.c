/* SPDX-License-Identifier: MIT */
#include "tr64/game.h"
#if TR64_HAS_TR2
#include <string.h>
#include "tr64/code.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/render.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/in/wall.h"
#include "engine/mem/pool.h"

tr64_game_drawable_t *tr64_wall_load(const char *name,const int32_t *placement,unsigned schema)
{
 mem_level_begin();
 code_loading_begin();code_set_frame_handler(NULL,NULL);
 tr64_render_scene_set_live(1);
 tr64_scene_load_held(name);
 tr64_level_validate_tr2_gameplay(tr64_level_current());
 tr64_load_scope_end();
 game_init();game_tr2_start(tr64_level_current(),name,schema);
 if(placement && !game_tr2_place(placement))sys_error("Tomb Raider II: placement outside dry live sector");
 tr64_game_drawable_t *drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*drawables));
 tr64_render_game_scene_prepare();
 code_loading_end();
 log_printf("tr2: %s live\n",name);
 log_flush();
 return drawables;
}
#endif
