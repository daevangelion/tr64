/* SPDX-License-Identifier: MIT */
#include <limits.h>
#include <string.h>
#include "tr64/code.h"
#include "tr64/game.h"
#include "tr64/mem.h"
#include "engine/mem/pool.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "units.h"
typedef uint32_t (*entry_t)(uint32_t);
#define DECLARE(n) extern uint32_t tr64_unit_##n(uint32_t);
TR64_UNITS(DECLARE)
#undef DECLARE
#define FRAME_COUNT (TR64_BYTES_code/4096u)
/* Preserve the shipped TR1 resident layout; TR2 can add a level-selected
 * unit while LOAD is still held. Host exercises the same preflight. */
#define TR64_CODE_PREFLIGHT_FREE_FRAMES TR64_HAS_TR2
typedef struct {
 const char *name; entry_t entry; const void *handle;
 uint32_t pages; int loaded; void *frames[FRAME_COUNT];
} unit_t;
#define ROW(n) {#n,tr64_unit_##n,NULL,0,0,{NULL}},
static unit_t units[]={TR64_UNITS(ROW)};
#undef ROW
static mem_slots_t frames;
static uint32_t bitmap[(FRAME_COUNT+31u)/32u];
enum { CODE_OFF, CODE_BOOT, CODE_IDLE, CODE_LOADING, CODE_FRAME };
static unsigned phase,loading_depth,dispatch_depth,frame_loads;
static int frame_seen;
static uint32_t last_frame,call_total,load_total;
static code_frame_handler_t frame_handler;
static void *frame_context;
static void context_check(void)
{
 if(phase==CODE_OFF)sys_error("code: not booted");
 if(!sys_code_context_ok())sys_error("code: requires IRQ-enabled resident context");
}
static unit_t *find(const char *name)
{
 unsigned i;
 if(!name)sys_error("code: NULL unit name");
 for(i=0;i<sizeof(units)/sizeof(units[0]);++i)
  if(units[i].name==name || !strcmp(units[i].name,name))return &units[i];
 sys_error("code unit %s unknown",name);
}
static void transition_check(void)
{
 context_check();
 if(dispatch_depth)sys_error("code: transition while unit executing");
 if(phase!=CODE_BOOT && phase!=CODE_LOADING && phase!=CODE_FRAME)
  sys_error("code: transition outside loading/frame point");
}
void code_load(const char *name)
{
 unit_t *u; uint32_t i;
 transition_check();u=find(name);
 if(phase!=CODE_BOOT && !strcmp(u->name,"init"))sys_error("code unit init boot only");
 if(u->loaded)sys_error("code unit %s already loaded",name);
#if TR64_CODE_PREFLIGHT_FREE_FRAMES
 uint32_t free=frames.count-frames.used;
 if(!u->pages || u->pages>free)
  sys_error("pool code cannot fit unit %s level=%s pages=%lu free=%lu",
   name,mem_level_name(),(unsigned long)u->pages,(unsigned long)free);
#else
 if(!u->pages || u->pages>frames.count)sys_error("pool code cannot fit unit %s pages=%lu",name,(unsigned long)u->pages);
#endif
 if(sys_code_entries()+(u->pages+1)/2>32)sys_error("code unit %s TLB capacity",name);
 if(phase==CODE_FRAME && frame_loads)sys_error("code: more than one load in frame");
 for(i=0;i<u->pages;++i)u->frames[i]=mem_slots_take(&frames,1,4096);
 sys_code_load(u->handle,u->frames,u->pages);sys_code_probe(u->handle,1);u->loaded=1;
 if(phase==CODE_FRAME)++frame_loads;
 ++load_total;
 log_printf("code: load %s pages=%lu pairs=%lu first=%08lx last=%08lx\n",name,(unsigned long)u->pages,(unsigned long)((u->pages+1)/2),(unsigned long)(uintptr_t)u->frames[0],(unsigned long)(uintptr_t)u->frames[u->pages-1]);
}
void code_unload(const char *name)
{
 unit_t *u;uint32_t i;
 transition_check();u=find(name);
 if(!u->loaded)sys_error("code unit %s not loaded",name);
 sys_code_unload(u->handle);sys_code_probe(u->handle,0);u->loaded=0;
 for(i=0;i<u->pages;++i)mem_slots_release(&frames,u->frames[i],1);
 log_printf("code: unload %s released=%lu\n",name,(unsigned long)u->pages);
}
int code_is_loaded(const char *name)
{ context_check();return find(name)->loaded; }
uint32_t code_call(const char *name,uint32_t argument)
{
 unit_t *u;uint32_t result;
 context_check();u=find(name);++call_total;
 if(dispatch_depth)sys_error("code: unit dispatch re-entry");
 /* Native fetch, rather than a software loaded-bit check, guards the cart. */
 sys_code_before_call(u->handle);dispatch_depth=1;
 result=u->entry(argument);dispatch_depth=0;
 return result;
}
void code_loading_begin(void)
{
 context_check();
 if(dispatch_depth)sys_error("code: loading scope while unit executing");
 if(phase!=CODE_IDLE && phase!=CODE_LOADING)sys_error("code: invalid loading scope entry");
 if(loading_depth==UINT_MAX)sys_error("code: loading scope overflow");
 ++loading_depth;phase=CODE_LOADING;
}
void code_loading_end(void)
{
 context_check();
 if(dispatch_depth)sys_error("code: loading scope while unit executing");
 if(phase!=CODE_LOADING || !loading_depth)sys_error("code: loading scope not open");
 if(!--loading_depth)phase=CODE_IDLE;
}
void code_set_frame_handler(code_frame_handler_t handler,void *context)
{
 context_check();
 if(phase!=CODE_LOADING || dispatch_depth)sys_error("code: frame planner registration requires loading state");
 frame_handler=handler;frame_context=context;
}
void code_level_release_begin(void)
{
 context_check();
 if(dispatch_depth || phase!=CODE_IDLE || loading_depth)
  sys_error("code: level release requires idle context");
 loading_depth=1;phase=CODE_LOADING;
}
void code_level_reset(void)
{
 unsigned i;
 context_check();
 if(dispatch_depth || phase!=CODE_LOADING || loading_depth!=1)
  sys_error("code: level reset requires outer loading transaction");
 frame_handler=NULL;frame_context=NULL;
 for(i=0;i<sizeof(units)/sizeof(units[0]);++i)
  if(units[i].loaded)code_unload(units[i].name);
 if(frames.used)sys_error("code: retained frames after level reset");
 frame_seen=0;last_frame=0;frame_loads=0;
}
void code_frame(uint32_t frame)
{
 context_check();
 if(phase!=CODE_IDLE || dispatch_depth)sys_error("code: invalid frame transition point");
 if(frame_seen && last_frame==frame)sys_error("code: repeated frame transition point");
 frame_seen=1;last_frame=frame;frame_loads=0;phase=CODE_FRAME;
 if(frame_handler)frame_handler(frame,frame_context);
 phase=CODE_IDLE;
}
void code_stats(uint32_t *calls,uint32_t *loads) { *calls=call_total;*loads=load_total; }
void code_log(void)
{
 log_printf("code: frames used=%lu free=%lu tlb_entries=%u wired=%u\n",(unsigned long)frames.used,(unsigned long)(frames.count-frames.used),sys_code_entries(),sys_code_wired());
}
void code_boot(void)
{
 uint32_t i,signature;
 if(phase!=CODE_OFF)sys_error("code: duplicate boot");
 phase=CODE_BOOT;sys_code_init();
 mem_slots_init(&frames,TR64_MEM_code,4096,bitmap,(FRAME_COUNT+31u)/32u);
 for(i=0;i<sizeof(units)/sizeof(units[0]);++i)units[i].handle=sys_code_find(units[i].name,&units[i].pages);
 /* The reserved init unit is called exactly once at boot, then released. */
 code_load("init");signature=code_call("init",0);
 if(signature!=0xd67ec9d8u)sys_error("code init signature %08lx",(unsigned long)signature);
 code_unload("init");phase=CODE_IDLE;code_log();log_flush();
}
