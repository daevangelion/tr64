/* SPDX-License-Identifier: MIT */
/* Scene probe (`probe video`): plays one video through the production path with
 * no level resident, then proves everything it borrowed was returned. */
#include <ctype.h>
#include <string.h>
#include "tr64/code.h"
#include "tr64/io.h"
#include "tr64/mem.h"
#include "tr64/video.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
static void returned(void)
{
 static const int pools[]={TR64_MEM_level,TR64_MEM_code,TR64_MEM_io,TR64_MEM_state};
 unsigned i;tr64_mem_stats_t stats;
 for(i=0;i<sizeof(pools)/sizeof(pools[0]);++i) {
  tr64_mem_stats(pools[i],&stats);
  if(stats.used)sys_error("video probe: pool %s not returned (%lu bytes)",stats.name,(unsigned long)stats.used);
 }
 if(code_is_loaded("video") || code_is_loaded("load"))sys_error("video probe: code unit still mapped");
 if(sys_code_entries()!=sys_code_wired())sys_error("video probe: TLB entries retained");
 if(tr64_io_pending())sys_error("video probe: engine/io requests still pending");
}
void tr64_video_probe(const char *name,uint32_t max_ticks)
{
 unsigned i,id=TR64_VIDEO_COUNT,first=0,last;
 size_t length=strlen(name);
 int onward=length>3 && !strcmp(name+length-3,"-on"); /* "<name>-on": that video and every later one */
 size_t base=onward ? length-3 : length;
 tr64_video_report_t report;int result=TR64_VIDEO_DONE;uint32_t pictures=0;
 if(!strcmp(name,"all")) {
  /* Every video in id order, one after the other: the slow-picture census. */
  last=TR64_VIDEO_COUNT;
 } else {
  for(i=0;i<TR64_VIDEO_COUNT;++i) {
   const char *native=tr64_video_name(i);unsigned n=0;
   while(native[n] && n<base && tolower((unsigned char)native[n])==name[n])++n;
   if(!native[n] && n==base)id=i;
  }
  if(id==TR64_VIDEO_COUNT)sys_error("video probe: unknown video %s",name);
  first=id;last=onward ? TR64_VIDEO_COUNT : id+1;
 }
 for(i=first;i<last;++i) {
  int one=tr64_video_play(i,max_ticks,&report);
  returned();
  if(one!=TR64_VIDEO_DONE)result=one;
  pictures+=report.pictures;
 }
 log_printf("video-probe: name=%s result=%d pictures=%lu returned=1\n",name,result,(unsigned long)pictures);
 tr64_mem_log();log_flush();
}
