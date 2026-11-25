/* SPDX-License-Identifier: MIT */
/* Hook/allocator fixture only: no AI initialization, PCM decoder or mixer.
 * Actual sound and CPU/RSP parity are tested by the sound module's gates. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/eng.h"
#include "tr64/game.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "engine/mem/pool.h"
#include "engine/core/log.h"
#ifndef TR64_HOST
#include "plat/n64/heap.h"
#endif
extern const tr64_game_sound_event_t *game_sound_events(uint32_t *) __attribute__((weak));
static uint32_t booted,starts,ticks,pumps,stops,last_pump_tick;
static int active;
#ifdef TR64_HOST
static mem_slots_t audio_heap;
static uint32_t audio_bits[(TR64_BYTES_audio/16u+31u)/32u];
#endif
void tr64_snd_boot(void)
{
 if(booted)sys_error("audio hook duplicate boot");
#ifdef TR64_HOST
 /* The same claimed pool and fixed-slot family as native boot malloc, not
  * the arena API: a later bump/reset must fail identically on both targets. */
 mem_library_claim(TR64_MEM_audio);
 mem_slots_init(&audio_heap,TR64_MEM_audio,16,audio_bits,sizeof(audio_bits)/sizeof(audio_bits[0]));
 void *p=mem_slots_take(&audio_heap,2,16);memset(p,0xa5,32);
 mem_slots_release(&audio_heap,p,2);
#else
 sys_heap_boot_pool_begin(TR64_MEM_audio);
 volatile uint8_t *p=malloc(1);*p=0xa5;free((void *)p);
 sys_heap_boot_pool_end();
#endif
 booted=1;log_printf("audio hook boot\n");
}
void tr64_snd_level_start(const struct tr64_level *level)
{
 if(!booted || !level || active)sys_error("audio hook level start order");
 ++starts;active=1;log_printf("audio hook level-start\n");
}
void tr64_snd_tick(void)
{
 uint32_t count=0;const tr64_game_sound_event_t *events;
 if(!booted || !active || !game_sound_events)sys_error("audio hook tick without ready game events");
 events=game_sound_events(&count);++ticks;
 if(count==1 && events && events[0].sound==INT16_MIN) {
  (void)tr64_mem_alloc(TR64_MEM_audio,1);
  sys_error("audio hook late arena allocation escaped ownership check");
 }
 if(count==1 && events && events[0].sound==INT16_MIN+1) {
  tr64_mem_reset(TR64_MEM_audio);
  sys_error("audio hook late arena reset escaped ownership check");
 }
 if(count!=1 || !events || events[0].sound!=(int16_t)ticks || events[0].volume!=123)
  sys_error("audio hook wrong/duplicate sound events");
 log_printf("audio hook tick=%lu\n",(unsigned long)ticks);
}
void tr64_snd_pump(void)
{
 if(!booted)sys_error("audio hook pump before boot");
 ++pumps;
 if(ticks!=last_pump_tick) {
  last_pump_tick=ticks;log_printf("audio hook pump-tick=%lu\n",(unsigned long)ticks);
 }
}
void tr64_snd_level_stop(void)
{
 tr64_mem_stats_t state;
 if(!booted)sys_error("audio hook stop before boot");
 tr64_mem_stats(TR64_MEM_state,&state);++stops;
 log_printf("audio hook stop active=%d state=%lu test_loaded=%d\n",active,(unsigned long)state.used,code_is_loaded("test"));
 active=0;
}
void tr64_snd_report(void)
{
 log_printf("audio hook report boot=%lu starts=%lu ticks=%lu pumps=%lu stops=%lu active=%d\n",
  (unsigned long)booted,(unsigned long)starts,(unsigned long)ticks,(unsigned long)pumps,(unsigned long)stops,active);
}
