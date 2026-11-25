/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/engine.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "tr64/code.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/core/frame.h"
int tr64_selftest(void);
extern void tr64_gpu_init(void) __attribute__((weak));
extern void tr64_snd_boot(void) __attribute__((weak));
extern int tr64_game_host_scene(int argc,char **argv) __attribute__((weak));
int main(int argc,char **argv)
{
 if(tr64_game_host_scene && tr64_game_host_scene(argc,argv))return 0;
 const char *route_path=NULL,*shot=NULL; uint8_t route_data[TR64_ROUTE_MAX];
 uint16_t *pixels=NULL; tr64_route_t route; uint32_t i,phase=0;
 int unit=0,small=0,unloaded=0;
 for(i=1;i<(uint32_t)argc;++i) {
  if(!strcmp(argv[i],"--route") && i+1<(uint32_t)argc)route_path=argv[++i];
  else if(!strcmp(argv[i],"--shot") && i+1<(uint32_t)argc)shot=argv[++i];
  else if(!strcmp(argv[i],"--selftest"))unit=1;
  else if(!strcmp(argv[i],"--pool-small"))small=1;
  else if(!strcmp(argv[i],"--unloaded"))unloaded=1;
  else sys_error("usage: tr64-host [--route path] [--shot path] [--selftest|--pool-small|--unloaded]");
 }
 tr64_eng_boot(); tr64_io_init(0);
 if(small) {
  mem_limit(TR64_MEM_state,TR64_BYTES_state-1u);
  (void)tr64_mem_alloc(TR64_MEM_state,TR64_BYTES_state);
  sys_error("pool-small control escaped");
 }
 code_boot();
 if(tr64_gpu_init)tr64_gpu_init();
 if(tr64_snd_boot)tr64_snd_boot();
 if(unloaded) {
  code_loading_begin();code_load("test");
  if(code_call("test",0)!=156)sys_error("code test rodata mismatch");
  code_unload("test");code_loading_end();
  (void)code_call("test",0);sys_error("unloaded control escaped");
 }
 if(unit)sys_quit(tr64_selftest());
 if(route_path) {
  FILE *f=fopen(route_path,"rb"); size_t n;
  if(!f)sys_error("route: cannot open %s",route_path);
  n=fread(route_data,1,sizeof(route_data),f);
  if(ferror(f) || !feof(f))sys_error("route: read or size");
  fclose(f);
  if(tr64_route_open(&route,route_data,(uint32_t)n))sys_error("route: invalid bytecode");
  tr64_eng_start(route.seed);
  tr64_scene_begin(&route);
  if(!route.level) { pixels=mem_borrow_boot_card(); tr64_boot_card(pixels); sys_present(pixels); }
  if(route.probe==TR64_PROBE_CLOCK_WRAP)phase=sys_time_us()-TR64_FRAME_US;
  for(i=0;i<route.ticks;++i) {
   if(route.probe==TR64_PROBE_CLOCK_WRAP)while(!tr64_frame_due(&phase,sys_time_us())) { }
   code_frame(i);tr64_eng_tick(tr64_route_tick(&route,i));
  }
  tr64_scene_clock_report();
  if(pixels) { tr64_boot_card(pixels); sys_present(pixels); }
  log_printf("scene: ticks=%lu state=%08lx\n",(unsigned long)tr64_eng_state()->tick,
             (unsigned long)tr64_state_hash(tr64_eng_state()));
 }
 else { pixels=mem_borrow_boot_card(); tr64_boot_card(pixels); sys_present(pixels); }
 sys_memory_report();
 if(shot && sys_shot(shot)<0)sys_error("shot: write failed");
 log_flush(); sys_quit(0);
}
