/* SPDX-License-Identifier: MIT */
/* Explicit scene services. This fixture never ticks, renders or pumps sound. */
#include "tr64/route.h"
#include "tr64/game.h"
#include "tr64/level.h"
#include "tr64/code.h"
#include "tr64/mem.h"
#include "tr64/eng.h"
#include "tr64/engine.h"
#include "tr64/render.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
/* Written by cart-reference.py into the scene build generated/ before the build:
 * TR64_CART_SAVE_TICK / TR64_CART_LOAD_TICK, UINT32_MAX meaning never. */
#include "cart_scene.h"
extern int tr64_game_test_save(void);
extern int tr64_game_test_load(void);
extern int32_t tr64_game_test_saved_level(void);
extern uint32_t tr64_game_test_save_bytes(void);
extern void tr64_snd_level_start(const tr64_level_t *);
extern void tr64_snd_level_stop(void);

static void loadCore(int32_t target,uint32_t reason,uint32_t tick)
{
    const char *name=game_level_name(target);
    tr64_game_inventory_carry_t carry;
    int32_t from=game_level_id();
    if(!name || !game_inventory_capture(&carry))sys_error("cart actions: unsupported transition/carry");
    carry.transition=reason;
    tr64_snd_level_stop();game_level_stop();tr64_render_scene_release();eng_level_release();
    /* The scene's bind-time pose names only the first Core; later Cores are live. */
    tr64_render_scene_set_live(1);
    mem_level_begin();code_loading_begin();tr64_scene_load(name);
    game_level_start_transition(tr64_level_current(),name,&carry);
    tr64_snd_level_start(tr64_level_current());code_loading_end();
    log_printf("game-service: tick=%lu transition from=%ld to=%ld carry_bytes=%lu reason=%lu\n",
        (unsigned long)tick,(long)from,(long)target,(unsigned long)sizeof(carry),(unsigned long)reason);
}
int tr64_scene_game_boundary(const tr64_route_t *route,uint32_t tick,uint16_t input)
{
    (void)route;(void)input;
    const tr64_game_stop_t *stop=game_stop_info();
    if(!stop)sys_error("cart actions: no loaded game");
    if(stop->kind==TR64_GAME_STOP_LEVEL_END) {
        uint32_t reason=stop->transition;
        int32_t target=reason==TR64_GAME_TRANSITION_LOAD ?
            tr64_game_test_saved_level() : stop->next_level_id;
        loadCore(target,reason,tick);
        if(reason==TR64_GAME_TRANSITION_LOAD && !tr64_game_test_load())
            sys_error("cart actions: selected save failed to load");
        return 1; /* Only the common engine call site replays this input. */
    }
    if(stop->kind!=TR64_GAME_STOP_NONE)return 0; /* Ordinary named-stop policy. */
    if(tick==TR64_CART_SAVE_TICK) {
        if(!tr64_game_test_save())sys_error("cart actions: actual save failed");
        log_printf("game-service: tick=%lu save bytes=%lu\n",
            (unsigned long)tick,(unsigned long)tr64_game_test_save_bytes());
    }
    if(tick==TR64_CART_LOAD_TICK) {
        /* Scene-directive restoration is after the tick, not a passport load.
         * These single-Core SaveN/LoadM scenes do not request a recursive tick. */
        if(tr64_game_test_saved_level()!=game_level_id())
            sys_error("cart actions: SaveN/LoadM scene crossed its declared Core");
        if(!tr64_game_test_load())sys_error("cart actions: actual load failed");
        log_printf("game-service: tick=%lu load bytes=%lu\n",
            (unsigned long)tick,(unsigned long)tr64_game_test_save_bytes());
    }
    return 0;
}
