/* SPDX-License-Identifier: MIT */
/* Real Core/TX loading and release, without Game or a renderer. The full
 * game-stop/renderer-drain restart policy belongs to the playable caller. */
#include <string.h>
#include "tr64/engine.h"
#include "tr64/eng.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/res.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
static uint32_t planner_calls;
static void *planner_context;
static void planner(uint32_t frame,void *context)
{
 if(frame!=1 || context!=planner_context || !context)sys_error("reload planner retained stale context");
 ++planner_calls;
}
static uint32_t fingerprint(const tr64_level_t *level)
{
 uint32_t hash=0,kind;
 if(!level->ready || !level->core_loaded || !level->textures || !level->texture_bytes)
  sys_error("reload level not published");
 for(kind=1;kind<=TR64_CORE_TABLES;++kind) {
  const tr64_level_table_t *table=tr64_level_table(level,kind);
  if(!table->present || !table->base || table->bytes<32)sys_error("reload missing table %lu",(unsigned long)kind);
  hash=tr64_crc32(hash,table->base,table->bytes);
 }
 return tr64_crc32(hash,level->textures,level->texture_bytes);
}
static void released(const char *name)
{
 const tr64_level_t *level=tr64_level_current();
 tr64_mem_stats_t stats;uint32_t used[4];unsigned i;
 const int pools[]={TR64_MEM_level,TR64_MEM_state,TR64_MEM_code,TR64_MEM_io};
 if(level->pack || level->textures || level->texture_bytes || level->loading || level->core_loaded || level->ready)
  sys_error("reload published level pointers retained");
 for(i=0;i<TR64_CORE_TABLES;++i) {
  const tr64_level_table_t *table=&level->tables[i];
  if(table->base || table->data || table->count || table->bytes || table->stride || table->present)
   sys_error("reload published table retained");
 }
 for(i=0;i<4;++i) {
  tr64_mem_stats(pools[i],&stats);used[i]=stats.used;
  if(stats.used)sys_error("reload pool %s not released",stats.name);
 }
 for(i=0;i<TR64_LEVEL_COUNT;++i) {
  mem_level_stats((int)i,&stats);
  if(stats.base || stats.budget || stats.used || stats.peak)sys_error("reload section %s metadata retained",stats.name);
 }
 if(code_is_loaded("load") || code_is_loaded("test") || sys_code_entries()!=sys_code_wired())
  sys_error("reload code unit mapping retained");
 log_printf("level-reload: released=%s level=%lu state=%lu code=%lu io=%lu\n",name,
  (unsigned long)used[0],(unsigned long)used[1],(unsigned long)used[2],(unsigned long)used[3]);
 code_log();log_flush();
}
void tr64_level_reload_probe(const char *requested)
{
 static const char *const names[]={"level1","gym","level1"};
 uint32_t hashes[3];unsigned i;
 if(strcmp(requested,names[0]))sys_error("reload probe requires level1 as first level");
 for(i=0;i<3;++i) {
  tr64_mem_stats_t stats;void *state;
  mem_level_begin();tr64_scene_load(names[i]);
  hashes[i]=fingerprint(tr64_level_current());
  log_printf("level-reload: loaded=%s ordinal=%u hash=%08lx\n",names[i],i,(unsigned long)hashes[i]);
  tr64_mem_stats(TR64_MEM_state,&stats);
  if(stats.used)sys_error("reload Core-only state was not empty");
  state=eng_game_state_alloc(37u+i);
  if(state!=stats.base)sys_error("reload state allocator did not restart");
  memset(state,0xa5,37u+i);
  /* The previous planner must be gone, and numbering may restart at zero. */
  code_frame(0);
  if(planner_calls!=i)sys_error("reload planner survived release");
  code_loading_begin();code_load("test");planner_context=state;
  code_set_frame_handler(planner,state);code_loading_end();code_frame(1);
  if(planner_calls!=i+1u)sys_error("reload planner did not run before release");
  eng_level_release();planner_context=NULL;released(names[i]);
 }
 if(hashes[0]!=hashes[2] || hashes[0]==hashes[1])sys_error("reload level fingerprints differ or gym was not loaded");
 log_printf("level-reload PASS loads=3 repeated_hash=%08lx distinct_gym=%08lx planners=%lu\n",
  (unsigned long)hashes[0],(unsigned long)hashes[1],(unsigned long)planner_calls);
 /* Only after the final release is the named no-level card borrow legal. */
 uint16_t *pixels=mem_borrow_boot_card();tr64_boot_card(pixels);sys_present(pixels);
 log_flush();
}
