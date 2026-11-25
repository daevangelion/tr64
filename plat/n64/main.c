/* SPDX-License-Identifier: MIT */
/* Boot and frame bracket ported from MIT ac3n64 04134cc. */
#include <libdragon.h>
#include "tr64/engine.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "tr64/code.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "cart_identity.h"
#if !(TR64_PLAY && !TR64_TEST)
#include "engine/core/frame.h"
#include "route_blob.h"
#include "plat/n64/harness.h"
#endif
extern void sys_surfaces_check(void);
extern void tr64_gpu_init(void) __attribute__((weak));
extern void tr64_snd_boot(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));
#if TR64_PLAY && !TR64_TEST
extern void tr64_play_run(void) __attribute__((noreturn));
#else
extern void tr64_play_run(void) __attribute__((weak,noreturn));
#endif
int main(void)
{
 static const char build_line[]="cart: build_id=" TR64_BUILD_ID " config_id=" TR64_CONFIG_ID "\n";
#if !(TR64_PLAY && !TR64_TEST)
 uint16_t *pixels=NULL; tr64_route_t route; uint32_t tick=0,phase;
#endif
 sys_memory_boot(); debug_init_isviewer(); timer_init();
 sys_log_write(build_line,sizeof(build_line)-1);
 display_init(RESOLUTION_320x240,DEPTH_16_BPP,2,GAMMA_NONE,FILTERS_DISABLED);
 (void)display_get_zbuf(); sys_surfaces_check(); joypad_init();
 tr64_eng_boot(); tr64_io_init(0); code_boot();
 if(tr64_gpu_init)tr64_gpu_init();
 if(tr64_snd_boot)tr64_snd_boot();
#if TR64_PLAY && !TR64_TEST
 /* Shipping playable build: enter the play loop directly so the scene/route
  * harness and test code unit are omitted from the linked image. */
 sys_heap_freeze(); sys_memory_report(); tr64_play_run();
#else
 if(!tr64_route_blob_size || tr64_route_open(&route,tr64_route_blob,tr64_route_blob_size))
  sys_error("route: invalid embedded bytecode");
 tr64_eng_start(route.seed);
 /* An opt-in playable module owns loading; never lend its level arena to the
  * no-level boot card first. All SDK boot allocations are already complete. */
 if(tr64_play_run) { sys_heap_freeze();sys_memory_report();tr64_play_run(); }
 if(!route.level) { pixels=mem_borrow_boot_card(); tr64_boot_card(pixels); sys_present(pixels); }
 sys_heap_freeze(); sys_memory_report();
 tr64_scene_begin(&route);
 phase=sys_time_us()-TR64_FRAME_US;
 for(;;) {
  tr64_io_pump();
  if(tr64_snd_pump)tr64_snd_pump();
  if(!tr64_frame_due(&phase,sys_time_us()))continue;
  /* Code transitions occur only here, before each frame's work. */
  code_frame(tick);
  if(tick==2) {
   if(route.probe==TR64_PROBE_UNLOADED)(void)code_call("test",0);
   else if(route.probe==TR64_PROBE_POOL) {
    mem_limit(TR64_MEM_state,TR64_BYTES_state-1u);
    (void)tr64_mem_alloc(TR64_MEM_state,TR64_BYTES_state);
   } else if(route.probe && route.probe!=TR64_PROBE_CLOCK_WRAP && route.probe!=TR64_PROBE_RELOAD &&
             route.probe!=TR64_PROBE_VIDEO)sys_test_probe(route.probe);
  }
  tr64_eng_tick(tr64_route_tick(&route,tick++)); sys_heap_check();
  if(tick==route.ticks) {
   tr64_scene_clock_report();
   if(pixels) { tr64_boot_card(pixels); sys_present(pixels); }
   log_printf("scene: ticks=%lu state=%08lx\n",(unsigned long)tick,
              (unsigned long)tr64_state_hash(tr64_eng_state()));
   sys_memory_report(); code_log(); log_flush();
   wait_ms(40);
   if(sys_shot(NULL)<0 && th_present())sys_error("shot: capture failed");
   if(th_present())sys_quit(0);
   for(;;) { }
  }
 }
#endif
}
