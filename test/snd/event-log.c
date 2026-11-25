/* SPDX-License-Identifier: MIT */
/* Explicit scene-only observer of real game commands; never changes them. */
#include "tr64/game.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
static uint32_t starts[256],stops[256];
void tr64_snd_test_event(const tr64_game_sound_event_t *e)
{
 if(e->sound<0 || e->sound>=256)sys_error("sound evidence ID outside map");
 if(e->stop)++stops[e->sound];else ++starts[e->sound];
}
void tr64_snd_test_report(void)
{
 uint32_t i;
 for(i=0;i<256;++i)if(starts[i] || stops[i])
  log_printf("audio-event: sound=%lu starts=%lu stops=%lu\n",
   (unsigned long)i,(unsigned long)starts[i],(unsigned long)stops[i]);
}
