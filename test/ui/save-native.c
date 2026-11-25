/* SPDX-License-Identifier: MIT */
/* Explicit test-only main wrapper. Uses the real pageable medium, PI transfers,
 * fixed pool allocator and SRAM header; no Game state or emulation shortcuts. */
#include <debug.h>
#include <display.h>
#include <graphics.h>
#include <surface.h>
#include <timer.h>
#include <string.h>
#include "tr64/code.h"
#include "tr64/engine.h"
#include "tr64/io.h"
#include "tr64/mem.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/save/save.h"
extern const uint32_t tr64_save_probe_write;
extern void sys_surfaces_check(void);
#define CHECK(expression) do { if(!(expression))sys_error("SRAM probe: " #expression); } while(0)
int __wrap_main(void)
{
 sys_memory_boot();debug_init_isviewer();timer_init();
 display_init(RESOLUTION_320x240,DEPTH_16_BPP,2,GAMMA_NONE,FILTERS_DISABLED);
 (void)display_get_zbuf();sys_surfaces_check();
 tr64_eng_boot();tr64_io_init(0);code_boot();
 uint8_t *data=tr64_mem_alloc(TR64_MEM_state,8192),*out=tr64_mem_alloc(TR64_MEM_state,8192);
 for(unsigned i=0;i<8192;++i)data[i]=(uint8_t)(i*17+(i>>8)*29);
 code_loading_begin();tr64_save_unit_load();code_loading_end();
 int status=tr64_save_scan();CHECK(status==(tr64_save_probe_write?TR64_SAVE_EMPTY:TR64_SAVE_OK));
 const uint32_t sizes[3]={6050,3903,381};
 for(unsigned slot=0;slot<3;++slot) {
  tr64_save_info_t info={.level_id=(int32_t)slot+2,.stats={.time=1805,.distance=0x12345678,
   .mediUsed=2,.ammoUsed=31,.kills=17,.pickups=9,.secrets=5,.titleID=151,.secretsMax=3}},actual;
  if(tr64_save_probe_write)CHECK(tr64_save_write(slot,&info,data,sizes[slot])==TR64_SAVE_OK);
  CHECK(tr64_save_read(slot,out,8192,&actual)==TR64_SAVE_OK);
  CHECK(actual.bytes==sizes[slot] && actual.level_id==info.level_id && !memcmp(data,out,sizes[slot]));
  CHECK(actual.stats.time==1805 && actual.stats.distance==0x12345678 && actual.stats.mediUsed==2);
  CHECK(actual.stats.ammoUsed==31 && actual.stats.kills==17 && actual.stats.pickups==9);
  CHECK(actual.stats.secrets==5 && actual.stats.titleID==151 && actual.stats.secretsMax==3);
  log_printf("SRAM probe: slot=%lu bytes=%lu crc=%08lx verified\n",(unsigned long)slot,
   (unsigned long)actual.bytes,(unsigned long)actual.crc);
 }
 if(tr64_save_probe_write)CHECK(tr64_save_settings_write(data,32)==TR64_SAVE_OK);
 uint32_t bytes=0;CHECK(tr64_save_settings_read(out,8192,&bytes)==TR64_SAVE_OK);
 CHECK(bytes==32 && !memcmp(data,out,32) && tr64_save_any());
 uint16_t *pixels=mem_borrow_boot_card();tr64_boot_card(pixels);
 surface_t surface=surface_make(pixels,FMT_RGBA16,320,240,640);
 graphics_set_color(graphics_make_color(255,255,255,255),graphics_make_color(0,0,0,255));
 graphics_draw_text(&surface,16,184,tr64_save_probe_write?"SRAM WRITE VERIFIED":"SRAM LOAD VERIFIED");
 graphics_draw_text(&surface,16,198,"3 slots + settings / 32 KiB");
 sys_present(pixels);sys_heap_freeze();sys_memory_report();code_log();
 log_printf("SRAM probe: %s PASS\n",tr64_save_probe_write?"write":"load");log_flush();
 wait_ms(40);CHECK(sys_shot(NULL)>=0);sys_quit(0);
}
