/* SPDX-License-Identifier: MIT */
/* tr64 M0: one build-selected table defines all sizes and carve order. */
#include <stdint.h>
#include <string.h>
#include "tr64/mem.h"
#include "engine/mem/pool.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
static tr64_mem_stats_t pools[] = {
#define TR64_POOL(id, kib, kind) {#id, TR64_##kind, NULL, kib * 1024u, 0, 0},
#include TR64_POOL_ROWS
#undef TR64_POOL
};
static int booted;
static void *boot_card_borrow;
static void *video_borrow;
static int level_resident;
static int level_planned;
static char level_name[32];
static tr64_mem_stats_t level_sections[TR64_LEVEL_COUNT] = {
 {"core",TR64_CARVE,NULL,0,0,0}, {"tex",TR64_CARVE,NULL,0,0,0},
 {"rooms",TR64_CARVE,NULL,0,0,0}, {"meshes",TR64_CARVE,NULL,0,0,0}
};
static uint8_t library_owned[TR64_MEM_COUNT];
static tr64_mem_stats_t *get(int id)
{
 if(id<0 || id>=TR64_MEM_COUNT)sys_error("memory: invalid pool %d",id);
 return &pools[id];
}
uint32_t mem_budget(int id) { return get(id)->budget; }
int mem_is_booted(void) { return booted; }
uint32_t mem_carve_bytes(void)
{
 uint32_t bytes=0; int i;
 for(i=0;i<TR64_MEM_COUNT;++i)if(pools[i].kind==TR64_CARVE)bytes+=pools[i].budget;
 return bytes;
}
#ifdef TR64_HOST
/* Only the level and state row sizes may differ. Keep every boot-owned
 * allocation and the pre-selection directory capacity fixed. */
static const uint32_t sequel_budgets[TR64_MEM_COUNT] = {
#define TR64_POOL(id, kib, kind) [TR64_MEM_##id] = (kib) * 1024u,
#include "tr64/pools-tr2.def"
#undef TR64_POOL
};
static const uint32_t tr3_budgets[TR64_MEM_COUNT] = {
#define TR64_POOL(id, kib, kind) [TR64_MEM_##id] = (kib) * 1024u,
#include "tr64/pools-tr3.def"
#undef TR64_POOL
};
#define TR64_POOL(id, kib, kind) \
 typedef char host_row_##id[(TR64_MEM_##id == TR64_MEM_level || \
     TR64_MEM_##id == TR64_MEM_state || TR64_BYTES_##id == (kib) * 1024u) ? 1 : -1];
#include "tr64/pools-tr2.def"
#undef TR64_POOL
#define TR64_POOL(id, kib, kind) \
 typedef char host_tr3_row_##id[(TR64_MEM_##id == TR64_MEM_level || \
     TR64_MEM_##id == TR64_MEM_state || TR64_BYTES_##id == (kib) * 1024u) ? 1 : -1];
#include "tr64/pools-tr3.def"
#undef TR64_POOL
static unsigned host_game = 1;

static void host_level_rows(unsigned game)
{
 tr64_mem_stats_t *level=&pools[TR64_MEM_level],*state=&pools[TR64_MEM_state],*io=&pools[TR64_MEM_io];
 uint32_t level_bytes=game==3 ? tr3_budgets[TR64_MEM_level] :
                      game==2 ? sequel_budgets[TR64_MEM_level] : TR64_BYTES_level;
 uint32_t state_bytes=game==3 ? tr3_budgets[TR64_MEM_state] :
                      game==2 ? sequel_budgets[TR64_MEM_state] : TR64_BYTES_state;
 uint32_t capacity=(uint32_t)(pools[TR64_MEM_fb].base-level->base);
 if(level_bytes+state_bytes+io->budget>capacity)
  sys_error("host game %u: level rows overlap fixed pools",game);
 level->budget=level_bytes; state->budget=state_bytes;
 state->base=level->base+level_bytes; io->base=state->base+state_bytes;
 level->peak=state->peak=0;io->peak=io->used;
 host_game=game;
}

uint8_t *mem_host_level_game(unsigned game,uint8_t *directory,uint32_t bytes)
{
 tr64_mem_stats_t *io=&pools[TR64_MEM_io];
 if(game==host_game)return directory; /* A remount keeps the existing layout. */
 if(!level_resident || level_planned || pools[TR64_MEM_state].used ||
    directory!=io->base || bytes!=io->used)
  sys_error("host game %u: cannot change live level rows",game);
 host_level_rows(game);
 memmove(io->base,directory,bytes);
 return io->base;
}
#endif
void mem_boot_carve(void *region,uint32_t capacity)
{
 uintptr_t cursor=(uintptr_t)region; uint32_t left=capacity; int i;
 if(booted)sys_error("memory: duplicate mem_boot_carve");
 if(!region || (cursor & 4095u))sys_error("memory: carve region alignment");
 /* Fail before publishing anything, naming the first row that does not fit. */
 for(i=0;i<TR64_MEM_COUNT;++i)if(pools[i].kind==TR64_CARVE) {
  if(pools[i].budget>left)sys_error("pool %s overflow during carve",pools[i].name);
  left-=pools[i].budget;
 }
 for(i=0;i<TR64_MEM_COUNT;++i)if(pools[i].kind==TR64_CARVE) {
  pools[i].base=(uint8_t *)cursor; cursor+=pools[i].budget;
 }
 booted=1;
}
void mem_fixed(int id,void *base,uint32_t bytes)
{
 tr64_mem_stats_t *p=get(id);
 if(!booted || p->kind==TR64_CARVE || !base || p->base)sys_error("pool %s invalid placement",p->name);
 if(p->kind!=TR64_IMAGE && bytes>p->budget)sys_error("pool %s overflow placed=%lu budget=%lu",p->name,(unsigned long)bytes,(unsigned long)p->budget);
 p->base=base; p->used=p->peak=bytes;
}
void mem_account(int id,uint32_t bytes)
{
 tr64_mem_stats_t *p=get(id);
 if(!booted || bytes>p->budget)sys_error("pool %s overflow used=%lu budget=%lu",p->name,(unsigned long)bytes,(unsigned long)p->budget);
 p->used=bytes; if(bytes>p->peak)p->peak=bytes;
}
void mem_library_claim(int id)
{
 tr64_mem_stats_t *p=get(id);
 if(!booted || p->kind!=TR64_CARVE || p->used || library_owned[id] || id==TR64_MEM_level)
  sys_error("pool %s cannot transfer library ownership",p->name);
 library_owned[id]=1;
}
void *tr64_mem_alloc(int id,uint32_t bytes)
{
 tr64_mem_stats_t *p=get(id); uint32_t start=(p->used+15u)&~15u;
 if(!booted || p->kind!=TR64_CARVE)sys_error("pool %s not an arena",p->name);
 if(library_owned[id])sys_error("pool %s owned by library allocator",p->name);
 if(id==TR64_MEM_level && level_resident)sys_error("pool level requires named section allocation");
 if(start>p->budget || bytes>p->budget-start)sys_error("pool %s overflow request=%lu used=%lu budget=%lu",p->name,(unsigned long)bytes,(unsigned long)p->used,(unsigned long)p->budget);
 mem_account(id,start+bytes); return p->base+start;
}
void tr64_mem_reset(int id)
{
 tr64_mem_stats_t *p=get(id);
 if(p->kind!=TR64_CARVE)sys_error("pool %s cannot reset",p->name);
 if(id==TR64_MEM_level && boot_card_borrow)sys_error("pool level cannot reset active boot-card borrow");
 if(id==TR64_MEM_level && video_borrow)sys_error("pool level cannot reset active VIDEO borrow");
 if(id==TR64_MEM_level && level_resident)sys_error("pool level cannot reset resident layout");
 if(library_owned[id])sys_error("pool %s owned by library allocator",p->name);
 p->used=0;
}
void mem_limit(int id,uint32_t capacity)
{
 tr64_mem_stats_t *p=get(id);
 if(library_owned[id])sys_error("pool %s owned by library allocator",p->name);
 if(id==TR64_MEM_level && level_planned)sys_error("pool level cannot limit resident layout");
 if(!booted || p->kind!=TR64_CARVE || p->used || !capacity || capacity>=p->budget)
  sys_error("pool %s invalid limit",p->name);
 p->budget=capacity;
}
void tr64_mem_stats(int id,tr64_mem_stats_t *out) { *out=*get(id); }
void *mem_borrow_boot_card(void)
{
 if(boot_card_borrow)sys_error("pool level duplicate boot-card borrow");
 if(level_resident)sys_error("boot-card borrow requires no-level state");
 if(video_borrow)sys_error("pool level boot-card cannot coexist with VIDEO borrow");
 boot_card_borrow=tr64_mem_alloc(TR64_MEM_level,320u*240u*2u);
 return boot_card_borrow;
}
void *mem_video_begin(uint32_t *capacity)
{
 if(!booted || !capacity || level_resident || level_planned || boot_card_borrow ||
    video_borrow || pools[TR64_MEM_level].used)
  sys_error("pool level VIDEO requires empty no-level arena");
 video_borrow=tr64_mem_alloc(TR64_MEM_level,TR64_BYTES_level);
 *capacity=TR64_BYTES_level;
 return video_borrow;
}
void mem_video_end(void *base)
{
 if(!video_borrow || base!=video_borrow || level_resident || level_planned)
  sys_error("pool level VIDEO invalid release");
 video_borrow=NULL;
 tr64_mem_reset(TR64_MEM_level);
}
void mem_level_begin(void)
{
 if(!booted || level_resident)sys_error("invalid level residency transition");
 if(boot_card_borrow)sys_error("scene level cannot coexist with boot-card borrow");
 if(video_borrow)sys_error("pool level cannot begin level with VIDEO borrow");
 if(pools[TR64_MEM_level].used)sys_error("pool level already allocated before residency");
 level_resident=1;
}
const char *mem_level_name(void) { return level_planned ? level_name : "(no level)"; }
void mem_level_plan(const char *name,const uint32_t bytes[TR64_LEVEL_COUNT])
{
 tr64_mem_stats_t *p=&pools[TR64_MEM_level];
 uint32_t total=0,n=0;int i;
 if(!booted || !level_resident || !name || !bytes)sys_error("pool level plan outside level residency");
 while(n<sizeof(level_name) && name[n])++n;
 if(!n || n==sizeof(level_name))sys_error("pool level invalid level name");
 if(level_planned) {
  if(strcmp(name,level_name))sys_error("pool level remount layout mismatch level=%s",name);
  for(i=0;i<TR64_LEVEL_COUNT;++i)
   if(bytes[i]!=level_sections[i].budget)sys_error("pool level remount layout mismatch level=%s",name);
  return;
 }
 if(p->used || library_owned[TR64_MEM_level])sys_error("pool level plan requires empty arena");
 /* Validate every reservation before publishing any section pointer. */
 for(i=0;i<TR64_LEVEL_COUNT;++i) {
  uint32_t span;
  if(bytes[i]>UINT32_MAX-15u)sys_error("pool level/%s size alignment overflow",level_sections[i].name);
  span=(bytes[i]+15u)&~15u;
  if(span>p->budget-total)
   sys_error("pool level overflow level=%s section=%s request=%lu reserved=%lu budget=%lu",name,level_sections[i].name,(unsigned long)span,(unsigned long)total,(unsigned long)p->budget);
  total+=span;
 }
 total=0;
 for(i=0;i<TR64_LEVEL_COUNT;++i) {
  level_sections[i].base=p->base+total;level_sections[i].budget=bytes[i];
  total+=(bytes[i]+15u)&~15u;
 }
 memcpy(level_name,name,n+1u);level_planned=1;mem_account(TR64_MEM_level,total);
}
static tr64_mem_stats_t *get_section(int section)
{
 if(section<0 || section>=TR64_LEVEL_COUNT)sys_error("pool level invalid section %d",section);
 return &level_sections[section];
}
void *mem_level_alloc(int section,uint32_t bytes)
{
 tr64_mem_stats_t *p=get_section(section);
 uint32_t start=(p->used+15u)&~15u;
 if(!level_planned)sys_error("pool level/%s allocation before layout",p->name);
 if(start>p->budget || bytes>p->budget-start)
  sys_error("pool level/%s overflow request=%lu used=%lu budget=%lu",p->name,(unsigned long)bytes,(unsigned long)p->used,(unsigned long)p->budget);
 p->used=start+bytes;if(p->used>p->peak)p->peak=p->used;
 return p->base+start;
}
void mem_level_stats(int section,tr64_mem_stats_t *out) { *out=*get_section(section); }
void mem_level_end(void)
{
 int i;
 if(!booted || !level_resident || boot_card_borrow)sys_error("level release: no active level");
 if(pools[TR64_MEM_code].used)sys_error("level release: code frames retained");
 if(pools[TR64_MEM_io].used)sys_error("level release: I/O scratch retained");
 tr64_mem_reset(TR64_MEM_state);
 pools[TR64_MEM_level].used=0;
 level_resident=0;level_planned=0;level_name[0]=0;
#ifdef TR64_HOST
 if(host_game!=1)host_level_rows(1);
#endif
 for(i=0;i<TR64_LEVEL_COUNT;++i) {
  level_sections[i].base=NULL;level_sections[i].budget=0;
  level_sections[i].used=0;level_sections[i].peak=0;
 }
}
void tr64_mem_log(void)
{
 int i;
 for(i=0;i<TR64_MEM_COUNT;++i) {
  tr64_mem_stats_t *p=&pools[i];
  log_printf("pool: %s base=%08lx size=%lu used=%lu peak=%lu kind=%u\n",p->name,(unsigned long)(uintptr_t)p->base,(unsigned long)p->budget,(unsigned long)p->used,(unsigned long)p->peak,p->kind);
 }
 if(level_planned)for(i=0;i<TR64_LEVEL_COUNT;++i) {
  tr64_mem_stats_t *p=&level_sections[i];
  log_printf("level-section: %s base=%08lx size=%lu used=%lu peak=%lu\n",p->name,(unsigned long)(uintptr_t)p->base,(unsigned long)p->budget,(unsigned long)p->used,(unsigned long)p->peak);
 }
 if(boot_card_borrow)log_printf("borrow: boot-card pool=level base=%08lx bytes=%lu\n",(unsigned long)(uintptr_t)boot_card_borrow,(unsigned long)(320u*240u*2u));
 if(video_borrow)log_printf("borrow: VIDEO pool=level base=%08lx bytes=%lu\n",(unsigned long)(uintptr_t)video_borrow,(unsigned long)TR64_BYTES_level);
 log_printf("residency: %s\n",level_resident ? "level" : "no-level");
 log_flush();
}
