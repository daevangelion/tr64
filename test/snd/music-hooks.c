/* SPDX-License-Identifier: MIT */
/* Synthetic bus/lifecycle control, NOT evidence of a real music decoder. */
#include <stdint.h>
#include <string.h>
#include "engine/core/sys.h"
#include "engine/core/log.h"
const uint32_t tr64_snd_test_disable=1;
static int16_t *pcm;
static uint32_t initialized,active,ticks,pumps,pulls,frames;
uint32_t tr64_music_memory_bytes(void) { return 1024; }
void tr64_music_init(void *memory,uint32_t bytes)
{
 if(initialized || !memory || ((uintptr_t)memory&15u) || bytes!=1024)
  sys_error("music hooks boot ownership");
 memset(memory,0,bytes);pcm=memory;initialized=1;
}
void tr64_music_level_start(void)
{
 if(!initialized || active)sys_error("music hooks start order");
 active=1;
}
void tr64_music_level_stop(void) { active=0; }
void tr64_music_tick(void)
{
 if(!active)sys_error("music hooks tick outside level");
 ++ticks;
}
void tr64_music_pump(void) { if(active)++pumps; }
const int16_t *tr64_music_mono_pcm(uint32_t count,uint32_t rate)
{
 uint32_t i;
 if(!active)return 0;
 if(count>448 || rate!=22050)sys_error("music hooks output contract");
 for(i=0;i<count;++i)pcm[i]=(frames+i)&1u ? -512 : 512;
 frames+=count;++pulls;return pcm;
}
void tr64_music_report(void)
{
 if(initialized!=1 || ticks!=30 || pumps!=30 || pulls!=60 || frames!=22050)
  sys_error("music hooks lifecycle/count mismatch");
 log_printf("music-hooks PASS boot=1 ticks=30 pumps=30 pulls=60 frames=22050\n");
}
