/* SPDX-License-Identifier: MIT */
/* Resident side of video playback: find the streams, lend the empty level arena,
 * run the `video` unit, return everything. No level may be resident. */
#include <string.h>
#include "engine/media/film.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/code.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/pack.h"
extern void tr64_snd_pump(void) __attribute__((weak));
static const char *const names[TR64_VIDEO_COUNT]={
 "ESCAPE","CORE","CAFE","MANSION","SNOW","LIFT","VISION","CANYON","PYRAMID","PRISON","END"
};
const char *tr64_video_name(unsigned video)
{
 if(video>=TR64_VIDEO_COUNT)sys_error("video: invalid id %u",video);
 return names[video];
}
static void service(void)
{
 /* A paused state like loading, not play: the directory read is back to back. */
 if(tr64_io_frame_bytes()>=TR64_IO_FRAME)tr64_io_frame();
 tr64_io_pump();
 if(tr64_snd_pump)tr64_snd_pump();
}
static void name_into(char *out,const char *prefix,const char *name)
{
 size_t p=strlen(prefix),n=strlen(name);
 memcpy(out,prefix,p);memcpy(out+p,name,n+1);
}
int tr64_video_play(unsigned video,uint32_t max_ticks,tr64_video_report_t *report)
{
 tr64_film_request_t request;tr64_pack_t pack;
 char video_name[32],film_name[32];
 uint32_t bytes,capacity,start;int found;
 void *arena;
 const char *name=tr64_video_name(video);
 name_into(video_name,"VIDEO_",name);name_into(film_name,"FILM_",name);
 memset(&request,0,sizeof(request));
 bytes=tr64_level_pack_bind_streams();
 start=sys_time_us();
 /* Header and directory only: no level plan, staging or section table. */
 tr64_load_scope_begin();
 tr64_pack_mount_catalog(&pack,bytes);
 while(!tr64_pack_poll(&pack))service();
 found=tr64_pack_stream_source(&pack,video_name,TR64_PACK_VIDEO,&request.video);
 if(found && !tr64_pack_stream_source(&pack,film_name,TR64_PACK_MUSIC,&request.film))
  sys_error("video %s has no film sound %s",video_name,film_name);
 tr64_pack_unmount(&pack);
 tr64_load_scope_end();
 if(!found) {
  log_printf("video: name=%s result=absent\n",name);log_flush();
  return TR64_VIDEO_ABSENT;
 }
 request.pack_bytes=bytes;request.max_ticks=max_ticks;
 arena=mem_video_begin(&capacity);
 request.arena=arena;request.arena_bytes=capacity;
 code_loading_begin();code_load("video");code_loading_end();
 {
  uint32_t loaded=(uint32_t)(sys_time_us()-start);
  log_printf("video: begin name=%s stream_blocks=%lu film_blocks=%lu prepare_us=%lu\n",name,
   (unsigned long)request.video.block_count,(unsigned long)request.film.block_count,(unsigned long)loaded);
  log_flush();
 }
 request.result=(uint32_t)code_call("video",(uint32_t)(uintptr_t)&request);
 code_loading_begin();code_unload("video");code_loading_end();
 mem_video_end(arena);
 tr64_mem_log();
 {
  const tr64_video_report_t *r=&request.report;
  log_printf("video: name=%s result=%s pictures=%lu shown=%lu dropped=%lu decode_us_total=%lu decode_us_max=%lu present_us_max=%lu late_us_max=%lu early_us_max=%lu sync_us=%ld..%ld film_us=%lu wall_us=%lu\n",
   name,request.result==TR64_VIDEO_DONE ? "done" : "skipped",
   (unsigned long)r->pictures,(unsigned long)r->shown,(unsigned long)r->dropped,
   (unsigned long)r->decode_us_total,(unsigned long)r->decode_us_max,(unsigned long)r->present_us_max,
   (unsigned long)r->late_us_max,(unsigned long)r->early_us_max,
   (long)r->sync_min_us,(long)r->sync_max_us,(unsigned long)r->film_us,(unsigned long)r->wall_us);
  log_printf("video: name=%s index_us=%lu open_us=%lu io_bytes=%lu audio_underruns=%lu arena_peak=%lu arena=%lu allocs=%lu slow_pictures=%lu slow_run_max=%lu\n",
   name,(unsigned long)r->index_us,(unsigned long)r->open_us,(unsigned long)r->io_bytes,
   (unsigned long)r->audio_underruns,(unsigned long)r->arena_peak,(unsigned long)r->arena_bytes,
   (unsigned long)r->arena_allocs,(unsigned long)r->slow_pictures,(unsigned long)r->slow_run_max);
  log_flush();
 }
 if(report)*report=request.report;
 return (int)request.result;
}
