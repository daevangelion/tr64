/* SPDX-License-Identifier: MIT */
/* Gate lane only: linked into the gate ROM by `tools/ship/ship.sh build-gate`
 * (TR64_SCENE_TEST_SOURCES), never into the delivered ROM. With L held:
 *   Z  places Lara in the air in a tall dry room of the gate level (she falls and dies: the death page),
 *   R  places her on the first end-of-level trigger (the statistics page),
 *   B  places her on a flat patch of an empty room (GATE_RUN) and starts the run odometer: the pace gate,
 *   A  steps the load knob, 0, 20, 40, 60 ms of burned time per drawn frame, back to 0.
 * Placement is game_place, the same call the placed-script oracle uses; coordinates come from the
 * generated gate_place.h, derived from the player's level data at build time and never committed.
 *
 * The load knob stands in for a renderer that is slower than a tick (fill cost the emulator does not
 * model): after each drawn frame the gate burns that much of the hardware clock, pumping audio and I/O as the
 * play loop's own wait does. Game time must not slow down: `gate-run:` lines give, per frame, the
 * frame's start on the hardware clock, the ticks run and Lara's accumulated path (one chord per tick, taken
 * before each tick); `gate-audio:` lines the mixer's output against the same clock every 60 ticks. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/in/input.h"
#include "engine/snd/snd.h"
#include "engine/snd/music.h"
#include "tr64/game.h"
#include "tr64/io.h"
#include "gate_place.h"
void tr64_play_gate(const char *level,uint16_t buttons,uint16_t previous);
void tr64_play_gate_frame(uint32_t tick,uint32_t frame_us);
static uint32_t burn_ms,last_audio_tick;
static int run_active,have_last;
static int32_t last[3];
static uint64_t path;
/* Lara's position from the game's own trace record, the head of which holds it. */
static int lara_position(int32_t out[3])
{
 char text[300];const char *at;char *end;unsigned i;
 text[game_trace_prefix(text,sizeof(text)-1u)]=0;
 at=strstr(text,"\"lara\":{");if(!at)return 0;
 at=strstr(at,"\"pos\":[");if(!at)return 0;
 at+=7;
 for(i=0;i<3;++i) {
  out[i]=(int32_t)strtol(at,&end,10);
  if(end==at)return 0;
  at=end+1;
 }
 return 1;
}
static uint32_t isqrt64(uint64_t v)
{
 uint64_t r=0,bit=1ull<<62;
 while(bit>v)bit>>=2;
 while(bit) {
  if(v>=r+bit) { v-=r+bit;r=(r>>1)+bit; } else r>>=1;
  bit>>=2;
 }
 return (uint32_t)r;
}
void tr64_play_gate(const char *level,uint16_t buttons,uint16_t previous)
{
 const uint16_t pressed=(uint16_t)(buttons&~previous);
 if(strcmp(level,GATE_LEVEL))return;
 /* One chord of Lara's path per tick, taken before the tick: position after the previous one. */
 if(run_active) {
  int32_t now[3];
  if(lara_position(now)) {
   if(have_last) {
    const int64_t dx=now[0]-last[0],dz=now[2]-last[2];
    path+=isqrt64((uint64_t)(dx*dx+dz*dz));
   }
   memcpy(last,now,sizeof(last));have_last=1;
  }
 }
 if(!(buttons&TR64_PAD_L))return;
 if(pressed&TR64_PAD_Z) {
  const int ok=game_place(GATE_DEATH);
  log_printf("gate: place death ok=%d\n",ok);log_flush();
 }
 if(pressed&TR64_PAD_R) {
  const int ok=game_place(GATE_END);
  log_printf("gate: place end ok=%d\n",ok);log_flush();
 }
 if(pressed&TR64_PAD_B) {
  const int ok=game_place(GATE_RUN);
  run_active=ok;have_last=0;path=0;
  log_printf("gate: place run ok=%d\n",ok);log_flush();
 }
 if(pressed&TR64_PAD_A) {
  burn_ms=burn_ms>=60 ? 0 : burn_ms+20;
  log_printf("gate: burn_ms=%lu\n",(unsigned long)burn_ms);log_flush();
 }
}
void tr64_play_gate_frame(uint32_t tick,uint32_t frame_us)
{
 if(run_active)
  log_printf("gate-run: tick=%lu us=%lu path=%lu burn_ms=%lu\n",(unsigned long)tick,(unsigned long)frame_us,
   (unsigned long)path,(unsigned long)burn_ms);
 if(tick-last_audio_tick>=60 && run_active) {
  last_audio_tick=tick;
  log_printf("gate-audio: us=%lu\n",(unsigned long)sys_time_us());
  tr64_audio_report();tr64_music_report();
 }
 log_flush();
 if(burn_ms) {
  const uint32_t start=sys_time_us();
  while((uint32_t)(sys_time_us()-start)<burn_ms*1000u) {
   tr64_io_pump();
   tr64_snd_pump();
  }
 }
}
