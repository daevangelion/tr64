/* SPDX-License-Identifier: MIT */
/* Focused M0 checks, ported from MIT ac3n64 04134cc. */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include "tr64/io.h"
#include "engine/core/sys.h"
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tr64/engine.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/fixed.h"
#include "tr64/res.h"
#include "engine/mem/pool.h"
#include "tr64/code.h"
#include "engine/core/log.h"
#include "tr64/game.h"
#include "engine/core/frame.h"
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"unit FAIL line=%d %s\n",__LINE__,#x); return 1; } } while(0)
static void wr(uint8_t *p,uint32_t x)
{ p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x; }
static void double_load_frame(uint32_t frame,void *context)
{
 (void)frame;(void)context;
 code_load("test");code_unload("test");code_load("test");
}
static int operation_rejected(int operation,const char *reason)
{
 int fd[2],status; pid_t child; char message[256]; size_t used=0;
 if(pipe(fd))return 0;
 child=fork();
 if(child<0) { close(fd[0]); close(fd[1]); return 0; }
 if(!child) {
  uint32_t sizes[TR64_LEVEL_COUNT]={16,16,16,16},capacity;void *borrow;
  close(fd[0]); if(dup2(fd[1],2)<0)_exit(99); close(fd[1]);
  switch(operation) {
  case 0: mem_level_begin();(void)mem_borrow_boot_card();break;
  case 1: (void)mem_borrow_boot_card();mem_level_begin();break;
  case 2: (void)mem_borrow_boot_card();tr64_mem_reset(TR64_MEM_level);break;
  case 3: code_load("test");break;
  case 4: code_loading_end();break;
  case 5: code_loading_begin();code_load("invalid-unit!");break;
  case 6: code_loading_begin();code_load("test");(void)code_call("test",UINT32_MAX);break;
  case 7: code_loading_begin();code_set_frame_handler(double_load_frame,NULL);code_loading_end();code_frame(0);break;
  case 8: code_loading_begin();code_load("init");break;
  case 9: (void)eng_game_state_alloc(TR64_BYTES_state+1u);break;
  case 10: mem_level_plan("unit",sizes);break;
  case 11: mem_level_begin();sizes[0]=TR64_BYTES_level+1u;mem_level_plan("over-total",sizes);break;
  case 12: mem_level_begin();sizes[0]=17;mem_level_plan("unit",sizes);(void)mem_level_alloc(TR64_LEVEL_core,17);(void)mem_level_alloc(TR64_LEVEL_core,1);break;
  case 13: (void)mem_level_alloc(-1,1);break;
  case 14: mem_level_begin();(void)mem_level_alloc(TR64_LEVEL_core,1);break;
  case 15: mem_level_begin();mem_level_plan("unit",sizes);++sizes[1];mem_level_plan("unit",sizes);break;
  case 16: mem_level_begin();mem_level_plan("unit",sizes);mem_level_plan("other",sizes);break;
  case 17: mem_level_begin();tr64_mem_reset(TR64_MEM_level);break;
  case 18: mem_level_begin();(void)tr64_mem_alloc(TR64_MEM_level,1);break;
  case 19: mem_level_begin();memset(sizes,0,sizeof(sizes));mem_level_plan("unit",sizes);mem_limit(TR64_MEM_level,64);break;
  case 20: mem_level_begin();sizes[0]=UINT32_MAX;mem_level_plan("unit",sizes);break;
  case 21: mem_level_begin();mem_limit(TR64_MEM_level,64);sizes[0]=33;sizes[1]=17;mem_level_plan("aligned-total",sizes);break;
  case 22: mem_level_begin();mem_level_plan("",sizes);break;
  case 23: (void)tr64_mem_alloc(TR64_MEM_level,16);mem_level_begin();break;
  case 24: code_level_reset();break;
  case 25: code_loading_begin();code_loading_begin();code_level_reset();break;
  case 26: mem_level_end();break;
  case 27: mem_level_begin();code_loading_begin();code_load("test");code_loading_end();mem_level_end();break;
  case 28: eng_level_release();break; /* Parent deliberately has a pending real I/O request. */
  case 29: mem_level_begin();(void)tr64_mem_alloc(TR64_MEM_io,1);eng_level_release();break;
  case 30: code_loading_begin();eng_level_release();break;
  case 31: code_level_release_begin();code_level_release_begin();break;
  case 32: (void)mem_video_begin(&capacity);(void)mem_video_begin(&capacity);break;
  case 33: (void)mem_video_begin(&capacity);mem_level_begin();break;
  case 34: (void)mem_video_begin(&capacity);tr64_mem_reset(TR64_MEM_level);break;
  case 35: (void)mem_video_begin(&capacity);(void)mem_borrow_boot_card();break;
  case 36: (void)mem_borrow_boot_card();(void)mem_video_begin(&capacity);break;
  case 37: mem_level_begin();(void)mem_video_begin(&capacity);break;
  case 38: (void)tr64_mem_alloc(TR64_MEM_level,16);(void)mem_video_begin(&capacity);break;
  case 39: borrow=mem_video_begin(&capacity);mem_video_end((uint8_t *)borrow+16);break;
  case 40: mem_video_end(NULL);break;
  case 41: (void)mem_video_begin(NULL);break;
  case 42: mem_limit(TR64_MEM_level,TR64_BYTES_level-1);(void)mem_video_begin(&capacity);break;
  case 43: mem_level_begin();mem_level_plan("unit",sizes);(void)mem_video_begin(&capacity);break;
  case 44: borrow=mem_video_begin(&capacity);mem_video_end(borrow);mem_video_end(borrow);break;
  case 45:
   mem_level_begin();mem_level_plan("code-overlap",sizes);
   code_loading_begin();
   code_load("video");code_unload("video"); /* it fits by itself */
   code_load("load");code_load("video");
   break;
  default: _exit(98);
  }
  _exit(0);
 }
 close(fd[1]);
 while(used<sizeof(message)-1) {
  ssize_t n=read(fd[0],message+used,sizeof(message)-1-used);
  if(n<0 && errno==EINTR)continue;
  if(n<=0)break;
  used+=(size_t)n;
 }
 message[used]=0; close(fd[0]);
 while(waitpid(child,&status,0)<0)if(errno!=EINTR)return 0;
 return WIFEXITED(status) && WEXITSTATUS(status)==1 && strstr(message,reason)!=NULL;
}
int tr64_selftest(void)
{
 unsigned checks=0,i; uint32_t total=0,rng=1,bitmap[32]; uint8_t *end=NULL;
 tr64_mem_stats_t p; mem_slots_t slots; void *first,*second;
 uint8_t route[TR64_ROUTE_HEADER+12]={0}; tr64_route_t r; tr64_input_t in;
 for(i=0;i<TR64_MEM_COUNT;++i) {
  tr64_mem_stats((int)i,&p); total+=p.budget; CHECK(p.used<=p.budget);
  if(p.kind==TR64_CARVE) { if(end)CHECK(end==p.base); end=p.base+p.budget; }
 }
 {
  static const uint32_t rows_kib=0
#define TR64_POOL(id,kib,kind) +(uint32_t)(kib)
#include TR64_POOL_ROWS
#undef TR64_POOL
  ;
  CHECK(total==rows_kib*1024u);
 }
 CHECK(sizeof(void *)==4);
 {
  uint32_t phase=UINT32_MAX-10000u,origin=phase;
  CHECK(!tr64_frame_due(&phase,origin+TR64_FRAME_US-1u) && phase==origin);
  CHECK(tr64_frame_due(&phase,origin+TR64_FRAME_US) && phase==origin+TR64_FRAME_US);
  CHECK(!tr64_frame_due(&phase,origin+TR64_FRAME_US));
  CHECK(tr64_frame_due(&phase,origin+3u*TR64_FRAME_US));
  CHECK(tr64_frame_due(&phase,origin+3u*TR64_FRAME_US));
  CHECK(!tr64_frame_due(&phase,origin+3u*TR64_FRAME_US));
  CHECK((uint32_t)(50u-(UINT32_MAX-50u))==101u);
 }
 {
  uint32_t before=sys_time_us(),ticks=sys_profile_ticks(),after=sys_time_us();
  CHECK((uint32_t)(ticks-before)<=(uint32_t)(after-before));
  CHECK(sys_profile_us(0)==0);CHECK(sys_profile_us(1234567u)==1234567u);
  CHECK(sys_profile_us(UINT32_MAX)==UINT32_MAX); /* Host profile units are us. */
 }
 code_loading_begin();code_loading_begin();CHECK(!code_is_loaded("test"));
 code_load("test");CHECK(code_is_loaded("test") && code_call("test",123)==279u);
 code_loading_end();CHECK(code_call("test",321)==477u);
 code_unload("test");code_loading_end();CHECK(!code_is_loaded("test"));log_flush();
 CHECK(operation_rejected(1,"scene level cannot coexist with boot-card borrow"));
 CHECK(operation_rejected(0,"boot-card borrow requires no-level state"));
 CHECK(operation_rejected(2,"pool level cannot reset active boot-card borrow"));
 CHECK(operation_rejected(3,"code: transition outside loading/frame point"));
 CHECK(operation_rejected(4,"code: loading scope not open"));
 CHECK(operation_rejected(5,"code unit invalid-unit! unknown"));
 CHECK(operation_rejected(6,"code: transition while unit executing"));
 CHECK(operation_rejected(7,"code: more than one load in frame"));
 CHECK(operation_rejected(8,"code unit init boot only"));
 CHECK(operation_rejected(9,"pool state overflow"));
 CHECK(operation_rejected(10,"pool level plan outside level residency"));
 CHECK(operation_rejected(11,"pool level overflow level=over-total section=core"));
 CHECK(operation_rejected(12,"pool level/core overflow request=1 used=17 budget=17"));
 CHECK(operation_rejected(13,"pool level invalid section -1"));
 CHECK(operation_rejected(14,"pool level/core allocation before layout"));
 CHECK(operation_rejected(15,"pool level remount layout mismatch level=unit"));
 CHECK(operation_rejected(16,"pool level remount layout mismatch level=other"));
 CHECK(operation_rejected(17,"pool level cannot reset resident layout"));
 CHECK(operation_rejected(18,"pool level requires named section allocation"));
 CHECK(operation_rejected(19,"pool level cannot limit resident layout"));
 CHECK(operation_rejected(20,"pool level/core size alignment overflow"));
 CHECK(operation_rejected(21,"pool level overflow level=aligned-total section=tex request=32 reserved=48 budget=64"));
 CHECK(operation_rejected(22,"pool level invalid level name"));
 CHECK(operation_rejected(23,"pool level already allocated before residency"));
 CHECK(operation_rejected(24,"code: level reset requires outer loading transaction"));
 CHECK(operation_rejected(25,"code: level reset requires outer loading transaction"));
 CHECK(operation_rejected(26,"level release: no active level"));
 CHECK(operation_rejected(27,"level release: code frames retained"));
 CHECK(operation_rejected(29,"level release: I/O scratch retained"));
 CHECK(operation_rejected(30,"code: level release requires idle context"));
 CHECK(operation_rejected(31,"code: level release requires idle context"));
 for(i=32;i<=44;++i)CHECK(operation_rejected((int)i,i==42 ? "pool level overflow" : "VIDEO"));
 CHECK(operation_rejected(45,"pool code cannot fit unit video level=code-overlap"));
 CHECK(sys_cart_bind(0,16)==-1);
 first=tr64_mem_alloc(TR64_MEM_state,17); second=eng_game_state_alloc(1);
 CHECK((uint8_t *)second==(uint8_t *)first+32);
 tr64_mem_stats(TR64_MEM_state,&p); CHECK(p.used==33 && p.peak==33);
 tr64_mem_reset(TR64_MEM_state); tr64_mem_stats(TR64_MEM_state,&p); CHECK(!p.used && p.peak==33);
 mem_slots_init(&slots,TR64_MEM_io,64,bitmap,32);
 first=mem_slots_take(&slots,2,64); second=mem_slots_take(&slots,1,64);
 CHECK(first!=second && slots.used==3);
 memset(first,0xa5,128); memset(second,0x5a,64);
 mem_slots_release(&slots,first,2); CHECK(slots.used==1 && *(uint8_t *)second==0x5a);
 mem_slots_release(&slots,second,1); CHECK(!slots.used);
 CHECK(tr64_fixed_mul(65536,-32768)==-32768); CHECK(tr64_fixed_add(INT32_MAX,1)==INT32_MAX);
 CHECK(tr64_random(&rng)==270369u); CHECK(tr64_random(&rng)==67634689u);
 CHECK(tr64_crc32(0,(const uint8_t *)"123456789",9)==0xcbf43926u);
 memcpy(route,"TR6R",4);wr(route+4,TR64_ROUTE_VERSION);wr(route+8,sizeof(route));wr(route+12,60);wr(route+16,1);wr(route+20,1);
 route[TR64_ROUTE_HEADER+5]=1;route[TR64_ROUTE_HEADER+6]=16;route[TR64_ROUTE_HEADER+7]=240;wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)));in=tr64_route_tick(&r,0);
 CHECK(!r.level && in.buttons==1 && in.x==16 && in.y==-16);
 wr(route+28,TR64_PROBE_CLOCK_WRAP);CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.probe==TR64_PROBE_CLOCK_WRAP);
 wr(route+28,TR64_PROBE_RELOAD);CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 wr(route+28,TR64_PROBE_VIDEO);CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 wr(route+28,TR64_PROBE_VIDEO+1u);CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+28,TR64_PROBE_NONE);
 for(i=0;i<sizeof(route);++i)CHECK(tr64_route_open(&r,route,i)<0);
 route[TR64_ROUTE_HEADER+6]=81;wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 route[TR64_ROUTE_HEADER+6]=16;memcpy(route+32,"level1",7);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.level && !strcmp(r.level,"level1"));
 wr(route+28,TR64_PROBE_RELOAD);CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.probe==TR64_PROBE_RELOAD);
 wr(route+28,TR64_PROBE_VIDEO);CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.probe==TR64_PROBE_VIDEO);wr(route+28,TR64_PROBE_NONE);
 route[32]='L';wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 route[32]='l';wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 wr(route+4,2);CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+4,TR64_ROUTE_VERSION);
 memset(route+32,'a',32);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 memset(route+32,0,32);memcpy(route+32,"level1",7);route[39]='x';wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 memset(route+32,0,TR64_ROUTE_HEADER-32);memcpy(route+32,"level1",7);
 route[TR64_ROUTE_HEADER+4]=255;route[TR64_ROUTE_HEADER+5]=255;
 route[TR64_ROUTE_HEADER+6]=0;route[TR64_ROUTE_HEADER+7]=0;
 wr(route+64,1);wr(route+68,1);wr(route+72,UINT32_MAX);
 wr(route+76,0x80000000u);wr(route+80,0x7fffffffu);wr(route+84,0xfffffff9u);wr(route+88,0xffff8000u);
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.game && r.place &&
  r.place_room==UINT32_MAX && r.place_x==INT32_MIN && r.place_y==INT32_MAX &&
  r.place_z==-7 && r.place_angle==INT16_MIN);
 in=tr64_route_tick(&r,0);CHECK(in.buttons==65535 && !in.x && !in.y);
 wr(route+64,2);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+64,1);
 wr(route+68,2);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+68,1);
 wr(route+88,32768);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+88,0xffff8000u);
 wr(route+64,0);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+64,1);
 wr(route+68,0);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 memset(route+72,0,20);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.game && !r.place);
 CHECK(r.game_hash_schema==2);
 wr(route+92,3);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.game_hash_schema==3);
 wr(route+64,0);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+64,1);
 wr(route+92,4);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+92,0);
 route[TR64_ROUTE_HEADER+6]=1;wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);route[TR64_ROUTE_HEADER+6]=0;
 wr(route+92,1);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);wr(route+92,0);
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));route[TR64_ROUTE_HEADER+5]^=1;
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);route[TR64_ROUTE_HEADER+5]^=1;
 memset(route+32,0,32);wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 /* A repaired checksum does not permit undefined sequel action bits. */
 memcpy(route+32,"wall",5);wr(route+92,20);wr(route+4,TR64_ROUTE_VERSION_TR2);
 wr(route+TR64_ROUTE_HEADER+4,TR2_IN_STEP_LEFT);
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)) && r.game_hash_schema==20);
 in=tr64_route_tick(&r,0);CHECK(in.buttons==TR2_IN_STEP_LEFT && !in.x && !in.y);
 wr(route+4,TR64_ROUTE_VERSION);CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 wr(route+4,TR64_ROUTE_VERSION_TR2);wr(route+TR64_ROUTE_HEADER+4,TR2_IN_STEP_LEFT|1u);
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 wr(route+TR64_ROUTE_HEADER+4,TR2_IN_STEP_LEFT);route[TR64_ROUTE_HEADER+11]=1;
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 /* The ring word is one-shot. Holding the raw mask cannot repeat it. */
 const uint32_t key=TR2_RING_EVENT|TR2_RING_EVENT_KEY|198u|(50u<<11);
 wr(route+TR64_ROUTE_HEADER+4,TR64_GAME_ACTION);
 wr(route+TR64_ROUTE_HEADER+8,key);
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(!tr64_route_open(&r,route,sizeof(route)));
 in=tr64_route_tick(&r,0);CHECK(in.buttons==TR64_GAME_ACTION && r.ring_event==key);
 in=tr64_route_tick(&r,1);CHECK(in.buttons==TR64_GAME_ACTION && !r.ring_event);
 wr(route+TR64_ROUTE_HEADER+4,0);
 wr(route+24,tr64_crc32(0,route+32,sizeof(route)-32));
 CHECK(tr64_route_open(&r,route,sizeof(route))<0);
 {
  uint8_t *data=tr64_mem_alloc(TR64_MEM_io,8192); char path[512]; FILE *file;
  tr64_io_req_t request={0}; uint32_t before;uint64_t bus_before;
  CHECK(getenv("TMPDIR")!=NULL);
  CHECK(snprintf(path,sizeof(path),"%s/bus-model.cart",getenv("TMPDIR"))<(int)sizeof(path));
  file=fopen(path,"wb"); CHECK(file!=NULL);
  memset(data,0x5a,8192); CHECK(fwrite(data,1,8192,file)==8192); CHECK(!fclose(file));
  CHECK(sys_cart_open(path)==8192); tr64_io_init(8192);
  request.dst=data; request.size=8192; CHECK(!tr64_io_submit(&request));
  CHECK(tr64_io_pending()==1 && sys_cart_open(path)==-1);
  CHECK(operation_rejected(28,"level release: I/O pending"));
  tr64_io_frame();before=sys_time_us();bus_before=sys_io_total_us();
  for(i=0;i<8;++i)tr64_io_pump();
  CHECK(request.state==TR64_IO_DONE && request.done==8192);
  CHECK(sys_io_frame_us()==1560 && sys_time_us()-before>=1560);
  CHECK(sys_io_total_us()-bus_before==1560u && !tr64_io_pending());
  tr64_io_frame(); CHECK(sys_io_frame_us()==0);
  {
   tr64_io_req_t queued[TR64_IO_REQUESTS],extra={0};
   memset(queued,0,sizeof(queued));
   for(i=0;i<TR64_IO_REQUESTS;++i) { queued[i].dst=data;queued[i].size=2;CHECK(!tr64_io_submit(&queued[i])); }
   extra.dst=data;extra.size=2;extra.done=123;
   CHECK(tr64_io_submit(&extra)==TR64_IO_SUBMIT_BUSY && extra.done==123 && extra.state==TR64_IO_IDLE);
   CHECK(tr64_io_submit(&queued[0])==TR64_IO_SUBMIT_INVALID);
   extra.offset=8192;CHECK(tr64_io_submit(&extra)==TR64_IO_SUBMIT_INVALID);extra.offset=0;
   for(i=0;i<2*TR64_IO_REQUESTS+2;++i)tr64_io_pump();
   CHECK(!tr64_io_pending());
   for(i=0;i<TR64_IO_REQUESTS;++i)CHECK(queued[i].state==TR64_IO_DONE);
   CHECK(!tr64_io_submit(&extra));tr64_io_pump();tr64_io_pump();
   CHECK(extra.state==TR64_IO_DONE && extra.done==2 && !tr64_io_pending());
  }
  CHECK(!remove(path));
  printf("IO model PASS bytes=8192 bus_us=1560 (195 us/KiB counted in sys_time_us)\n");
 }
 tr64_mem_reset(TR64_MEM_io); /* The completed I/O fixture retains no live data. */
 {
  uint32_t sizes[TR64_LEVEL_COUNT]={33,17,0,9};
  char name[]="arena-unit";uint8_t *base,*core,*tex,*meshes;
  tr64_mem_stats(TR64_MEM_level,&p);base=p.base;CHECK(!p.used);
  mem_level_begin();mem_level_plan(name,sizes);name[0]='X';
  tr64_mem_stats(TR64_MEM_level,&p);CHECK(p.used==96 && p.peak==96);
  tr64_mem_stats_t section;
  for(i=0;i<TR64_LEVEL_COUNT;++i) {
   mem_level_stats((int)i,&section);CHECK(section.budget==sizes[i] && !section.used && !section.peak);
  }
  meshes=mem_level_alloc(TR64_LEVEL_meshes,9);CHECK(meshes==base+80);
  core=mem_level_alloc(TR64_LEVEL_core,17);CHECK(core==base);memset(core,0xa5,17);
  CHECK(mem_level_alloc(TR64_LEVEL_core,1)==base+32);
  tex=mem_level_alloc(TR64_LEVEL_tex,17);CHECK(tex==base+48);memset(tex,0x5a,17);
  CHECK(mem_level_alloc(TR64_LEVEL_rooms,0)==base+80);
  mem_level_stats(TR64_LEVEL_core,&section);CHECK(section.used==33 && section.peak==33);
  mem_level_stats(TR64_LEVEL_tex,&section);CHECK(section.used==17 && section.peak==17);
  mem_level_plan("arena-unit",sizes); /* Reopening the pack must not reset data. */
  mem_level_stats(TR64_LEVEL_core,&section);CHECK(section.used==33 && section.peak==33);
  CHECK(core[0]==0xa5 && core[16]==0xa5 && tex[0]==0x5a && tex[16]==0x5a);
  tr64_mem_stats(TR64_MEM_level,&p);CHECK(p.used==96 && p.peak==96);
  code_frame(0);
  code_loading_begin();code_load("test");code_set_frame_handler(double_load_frame,NULL);code_loading_end();
  code_loading_begin();code_level_reset();code_loading_end();CHECK(!code_is_loaded("test"));
  code_frame(0); /* Sequence restarted; the old failure planner must be gone. */
  mem_level_end();tr64_mem_stats(TR64_MEM_level,&p);CHECK(!p.used && p.peak==96);
  tr64_mem_stats(TR64_MEM_state,&p);CHECK(!p.used);
  for(i=0;i<TR64_LEVEL_COUNT;++i) {
   mem_level_stats((int)i,&section);CHECK(!section.base && !section.budget && !section.used && !section.peak);
  }
  mem_level_begin();mem_level_plan("second-level",sizes);
  CHECK(mem_level_alloc(TR64_LEVEL_core,1)==base);mem_level_end();
 }
 {
  uint32_t capacity;uint8_t *borrow=mem_video_begin(&capacity);
  tr64_mem_stats(TR64_MEM_level,&p);
  CHECK(capacity==TR64_BYTES_level && borrow==p.base && !((uintptr_t)borrow&15u));
  CHECK(p.used==capacity && p.peak==capacity);
  borrow[0]=0xa5;borrow[capacity-1]=0x5a;
  mem_video_end(borrow);tr64_mem_stats(TR64_MEM_level,&p);
  CHECK(!p.used && p.peak==capacity);
  CHECK(mem_video_begin(&capacity)==borrow);mem_video_end(borrow);
  mem_level_begin();mem_level_end();
 }
 printf("unit: PASS (%u checks, 32-bit pointers, carve/bump/slots/VM/hash/fixed/RNG)\n",checks);
 return 0;
}
