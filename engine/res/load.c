/* SPDX-License-Identifier: MIT */
/* Resident dispatch, lease state and published gameplay views only. */
#include <string.h>
#include "engine/res/load-internal.h"
#include "tr64/code.h"
#include "tr64/res.h"
#include "tr64/mem.h"
#include "engine/core/log.h"
#include "level_pack.h"
static tr64_level_t current;
static uint32_t scope_depth,scope_started_us,scope_unit_us;
#if TR64_HAS_TR2
static int current_tr2_validated;
#endif
void tr64_load_scope_begin(void)
{
 uint32_t start=0,unit_start;
 if(scope_depth==UINT32_MAX)sys_error("load scope: nesting overflow");
 if(!scope_depth)start=sys_time_us();
 /* The shared scope validates resident/IRQ/thread context even when nested. */
 code_loading_begin();
 if(!scope_depth) {
  scope_started_us=start; unit_start=sys_time_us();
  code_load("load"); scope_unit_us=sys_time_us()-unit_start;
 }
 ++scope_depth;
}
void tr64_load_scope_end(void)
{
 uint32_t before,unload_us;
 if(!scope_depth)sys_error("load scope: unbalanced end");
 if(scope_depth>1) {
  code_loading_end(); --scope_depth; return;
 }
 before=sys_time_us(); code_unload("load"); unload_us=sys_time_us()-before;
 if(code_is_loaded("load"))sys_error("load scope: unit remained loaded");
 scope_depth=0; code_loading_end();
 log_printf("load-scope: unit_load_us=%lu total_us=%lu unload_us=%lu released=1\n",
  (unsigned long)scope_unit_us,(unsigned long)(sys_time_us()-scope_started_us),(unsigned long)unload_us);
 code_log();
}
static uint32_t call(tr64_load_call_t *c)
{
 if(!scope_depth)sys_error("load scope required for operation %lu",(unsigned long)c->operation);
 return code_call("load",(uint32_t)(uintptr_t)c);
}
void tr64_pack_mount(tr64_pack_t *p,uint32_t bytes,const char *name)
{ tr64_load_call_t c={.operation=LOAD_MOUNT,.bytes=bytes,.name=name,.pack=p}; (void)call(&c); }
void tr64_pack_mount_catalog(tr64_pack_t *p,uint32_t bytes)
{ tr64_load_call_t c={.operation=LOAD_CATALOG_MOUNT,.bytes=bytes,.pack=p}; (void)call(&c); }
int tr64_pack_poll(tr64_pack_t *p)
{ tr64_load_call_t c={.operation=LOAD_POLL,.pack=p}; return (int)call(&c); }
void tr64_pack_begin_kind(tr64_pack_t *p,uint32_t kind,int pool)
{ tr64_load_call_t c={.operation=LOAD_KIND,.kind=kind,.pool=pool,.pack=p}; (void)call(&c); }
const void *tr64_pack_data(const tr64_pack_t *p,uint32_t *bytes)
{
 if(p->phase || !p->output)sys_error("pack section %lu: data not ready",(unsigned long)p->kind);
 if(bytes)*bytes=p->output_bytes;
 return p->output;
}
void tr64_pack_unmount(tr64_pack_t *p)
{ tr64_load_call_t c={.operation=LOAD_UNMOUNT,.pack=p}; (void)call(&c); }
int tr64_pack_has_kind(const tr64_pack_t *p,uint32_t kind)
{
 uint32_t i;
 if(p->phase || !p->entries)sys_error("pack: kind query before mount");
 for(i=0;i<p->count;++i)if(tr64_be32(p->entries+i*TR64_PACK_RETAINED_ENTRY_BYTES)==kind)return 1;
 return 0;
}
void tr64_pack_stream_index(tr64_pack_t *p,uint32_t kind,
 tr64_pack_stream_block_t *blocks,uint32_t capacity,tr64_pack_stream_t *stream)
{
 tr64_load_call_t c={.operation=LOAD_STREAM_INDEX,.pack=p,.kind=kind,
  .blocks=blocks,.capacity=capacity,.stream=stream};
 (void)call(&c);
}
void tr64_level_begin(tr64_level_t *l,tr64_pack_t *p)
{
#if TR64_HAS_TR2
 if(l==&current)current_tr2_validated=0;
#endif
 tr64_load_call_t c={.operation=LOAD_CORE_BEGIN,.level=l,.pack=p}; (void)call(&c);
}
int tr64_level_poll(tr64_level_t *l)
{
 tr64_load_call_t c={.operation=LOAD_CORE_POLL,.level=l};
 return (int)call(&c);
}
void tr64_level_finish(tr64_level_t *l)
{
#if TR64_HAS_TR2
 if(l==&current)current_tr2_validated=0;
#endif
 tr64_load_call_t c={.operation=LOAD_FINISH,.level=l}; (void)call(&c);
}
void tr64_level_load(tr64_level_t *l,uint32_t bytes,const char *name)
{
 uint32_t start=sys_time_us(),opened=!scope_depth,unit_us;
 tr64_load_call_t c={.operation=LOAD_LEVEL,.bytes=bytes,.name=name,.level=l};
#if TR64_HAS_TR2
 if(l==&current)current_tr2_validated=0;
#endif
 tr64_load_scope_begin(); unit_us=opened ? scope_unit_us : 0;
 (void)call(&c); tr64_load_scope_end();
 l->stats.total_us=sys_time_us()-start; l->stats.unit_load_us=unit_us;
 log_printf("level-load: level=%s bytes_read=%lu total_us=%lu bus_us=%llu decode_us=%lu hash_us=%lu validation_us=%lu unit_load_us=%lu staging_peak=%lu ready=%s sound_refs=%s placement=code-unit\n",name,
  (unsigned long)l->stats.bytes_read,(unsigned long)l->stats.total_us,(unsigned long long)l->stats.bus_us,
  (unsigned long)l->stats.decode_us,(unsigned long)l->stats.hash_us,(unsigned long)l->stats.validation_us,
  (unsigned long)l->stats.unit_load_us,(unsigned long)l->stats.staging_peak,
  l->sound_metadata ? "core+textures+sound" : "core+textures",
  l->sound_metadata ? "validated" : "unvalidated");
 code_log();
}
#if TR64_HAS_TR2
void tr64_level_validate_tr2_gameplay(const tr64_level_t *level)
{
 if(level==&current && current_tr2_validated)return;
 tr64_load_call_t c={.operation=LOAD_VALIDATE_TR2,.readonly_level=level};
 tr64_load_scope_begin();
 (void)call(&c);
 tr64_load_scope_end();
 if(level==&current)current_tr2_validated=1;
}
#endif
const tr64_level_t *tr64_level_current(void) { return &current; }
void tr64_level_release(void)
{
 tr64_mem_stats_t io;
 if(scope_depth)sys_error("level release: load lease active");
 if(tr64_io_pending())sys_error("level release: I/O pending");
 tr64_mem_stats(TR64_MEM_io,&io);
 if(io.used)sys_error("level release: I/O scratch retained");
#if TR64_HAS_TR2
 current_tr2_validated=0;
#endif
 memset(&current,0,sizeof(current));
}
uint32_t tr64_level_pack_bind_streams(void)
{
 tr64_mem_stats_t io;
 if(tr64_io_pending())sys_error("level pack stream bind: I/O pending");
 tr64_mem_stats(TR64_MEM_io,&io);
 if(io.used)sys_error("level pack stream bind: exclusive io lease active");
 if(scope_depth)sys_error("level pack stream bind: load scope active");
 if(!TR64_LEVEL_PACK_BYTES)sys_error("level pack not configured: set TR64_LEVEL_PACK");
#ifdef TR64_HOST
 if(sys_cart_open(TR64_LEVEL_PACK_PATH)!=(int)TR64_LEVEL_PACK_BYTES)sys_error("level pack open/size");
#else
 if(sys_cart_bind(TR64_LEVEL_PACK_OFFSET,TR64_LEVEL_PACK_BYTES))sys_error("level pack raw ROM bind");
#endif
 tr64_io_init(TR64_LEVEL_PACK_BYTES); tr64_io_frame();
 log_printf("level-pack: sha256=%s\n",TR64_LEVEL_PACK_SHA256);
 return TR64_LEVEL_PACK_BYTES;
}
extern void tr64_render_scene_load(const tr64_level_t *,const char *,uint32_t) __attribute__((weak));
static inline void scene_load(const char *name)
{
 uint32_t bytes=tr64_level_pack_bind_streams();
 tr64_load_scope_begin();
 tr64_level_load(&current,bytes,name);
 if(tr64_render_scene_load)tr64_render_scene_load(&current,name,bytes);
}
#if TR64_HAS_TR2
void tr64_scene_load_held(const char *name) { scene_load(name); }
#endif
void tr64_scene_load(const char *name)
{
 scene_load(name);
 tr64_load_scope_end(); log_flush();
}
