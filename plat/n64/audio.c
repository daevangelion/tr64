/* SPDX-License-Identifier: MIT */
#include <libdragon.h>
#include <malloc.h>
#include <string.h>
#include "engine/snd/snd.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "plat/n64/heap.h"
#include "tr64/mem.h"
static uint32_t underruns,output_frames,mix_peak,output_pumps;
static uint64_t mix_us;
static int active,started;
static uint32_t active_start_us;
static uint64_t active_us;
static tr64_snd_frame_t *cached_output;
typedef struct {
 uint64_t mix_us,copy_us;
 uint32_t buffers,mix_peak,copy_peak;
} profile_t;
static profile_t profile[TR64_SND_CHANNELS+1];
void *tr64_audio_boot(uint32_t bytes)
{
 void *arena;
 sys_heap_boot_pool_begin(TR64_MEM_audio);
 /* Three448-frame buffers:60.96ms maximum,at least40.64ms after service.
  * Two buffers starved during a26.7ms render at adverse DMA phase. */
 audio_init(22050,AUDIO_INIT_LATENCY_MS(60));
 if(audio_get_num_buffers()!=3)sys_error("audio three-buffer queue configuration");
 arena=memalign(16,bytes);
 cached_output=memalign(16,(uint32_t)audio_get_buffer_length()*8u);
 sys_heap_boot_pool_end();
 if(!arena || !cached_output)sys_error("audio arena/output allocation");
 log_printf("audio-output: cart rate=%d buffers=%d frames_per_buffer=%d arena=%lu\n",
  audio_get_frequency(),audio_get_num_buffers(),audio_get_buffer_length(),(unsigned long)bytes);
 return arena;
}
void tr64_audio_active(int enabled)
{
 if(active && !enabled)active_us+=(uint32_t)(sys_time_us()-active_start_us);
 if(enabled && !active)active_start_us=sys_time_us();
 active=enabled;started=0;
}
void tr64_audio_tick(void) { }
void tr64_audio_pump(void)
{
 uint32_t elapsed=0;
 /* SDK buf_full can retain the last DMA until its next callback. Check
  * physical AI BUSY as well: software queue ownership is not playback. */
 if(active && started && (!audio_get_queued_buffers() ||
    !(*(volatile uint32_t *)0xa450000cu & (1u<<30))))++underruns;
 while(audio_can_write()) {
  int16_t *out=audio_write_begin();
  uint32_t start,done,end,voices,mix_ticks,copy_ticks,n=(uint32_t)audio_get_buffer_length();
  profile_t *p;
  start=sys_profile_ticks();voices=tr64_snd_mix(cached_output,n,(uint32_t)audio_get_frequency());
  done=sys_profile_ticks();
  /* One aligned cached-to-uncached copy, rather than per-sample AI stores. */
  memcpy(out,cached_output,n*4u);end=sys_profile_ticks();
  mix_ticks=done-start;copy_ticks=end-done;elapsed+=end-start;
  p=profile+voices;++p->buffers;p->mix_us+=sys_profile_us(mix_ticks);p->copy_us+=sys_profile_us(copy_ticks);
  if(mix_ticks>p->mix_peak)p->mix_peak=mix_ticks;
  if(copy_ticks>p->copy_peak)p->copy_peak=copy_ticks;
  audio_write_end();output_frames+=n;
  started=active;
 }
 mix_us+=sys_profile_us(elapsed);if(elapsed>mix_peak)mix_peak=elapsed;++output_pumps;
}
void tr64_audio_perf(uint32_t *total_us,uint32_t *frames) { *total_us=(uint32_t)mix_us;*frames=output_frames; }
void tr64_audio_report(void)
{
 uint32_t i;
 log_printf("audio-output: cart rate=%d frames=%lu underruns=%lu pumps=%lu mixer_total_us=%llu mixer_peak_pump_us=%lu\n",
  audio_get_frequency(),(unsigned long)output_frames,(unsigned long)underruns,
  (unsigned long)output_pumps,(unsigned long long)mix_us,(unsigned long)sys_profile_us(mix_peak));
 log_printf("audio-time: active_us=%llu\n",(unsigned long long)(active_us+
  (active ? (uint32_t)(sys_time_us()-active_start_us) : 0u)));
 for(i=0;i<=TR64_SND_CHANNELS;++i) {
  const profile_t *p=profile+i;
  if(!p->buffers)continue;
  log_printf("audio-buffer: voices=%lu buffers=%lu mix_total_us=%llu copy_total_us=%llu mix_peak_us=%lu copy_peak_us=%lu\n",
   (unsigned long)i,(unsigned long)p->buffers,(unsigned long long)p->mix_us,(unsigned long long)p->copy_us,
   (unsigned long)sys_profile_us(p->mix_peak),(unsigned long)sys_profile_us(p->copy_peak));
 }
}
