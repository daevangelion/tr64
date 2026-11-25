/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include "engine/res/load-internal.h"
#include "tr64/mem.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
extern void tr64_snd_pump(void) __attribute__((weak));
void load_io_pump(void)
{
 /* Loading is a paused state, not a sequence of 30 Hz play frames. */
 if(tr64_io_frame_bytes()>=TR64_IO_FRAME)tr64_io_frame();
 tr64_io_pump();
 if(tr64_snd_pump)tr64_snd_pump();
}
static void level(tr64_level_t *l,uint32_t bytes,const char *name)
{
 tr64_pack_t p;
 uint32_t start=sys_time_us();
 load_pack_mount(&p,bytes,name);
 while(!load_pack_poll(&p))load_io_pump();
 load_core_begin(l,&p);
 while(!load_core_poll(l))load_io_pump();
 log_printf("core-prefetch: level=%s bytes_read=%lu total_us=%lu bus_us=%llu decode_us=%lu hash_us=%lu validation_us=%lu\n",name,
  (unsigned long)p.bytes_read,(unsigned long)(sys_time_us()-start),(unsigned long long)p.read_us,
  (unsigned long)p.decode_us,(unsigned long)p.hash_us,(unsigned long)p.validation_us);
 load_pack_kind(&p,TR64_PACK_TEX,TR64_MEM_level);
 while(!load_pack_poll(&p))load_io_pump();
 load_finish(l);
}
extern uint32_t tr64_render_load_dispatch(uint32_t) __attribute__((weak));
uint32_t tr64_unit_load(uint32_t argument)
{
 const uint32_t *request=(const uint32_t *)(uintptr_t)argument;
 tr64_load_call_t *c; uint32_t operation;
 if(!request)sys_error("load unit: missing request");
 operation=*request;
 if(operation>=LOAD_RENDER_FIRST) {
  if(!tr64_render_load_dispatch)sys_error("load unit: render operation unavailable %lu",(unsigned long)operation);
  return tr64_render_load_dispatch(argument);
 }
 c=(tr64_load_call_t *)(uintptr_t)argument;
 switch(operation) {
 case LOAD_MOUNT:load_pack_mount(c->pack,c->bytes,c->name);break;
 case LOAD_CATALOG_MOUNT:load_pack_mount_catalog(c->pack,c->bytes);break;
 case LOAD_POLL:return (uint32_t)load_pack_poll(c->pack);
 case LOAD_KIND:load_pack_kind(c->pack,c->kind,c->pool);break;
 case LOAD_UNMOUNT:load_pack_release(c->pack);break;
 case LOAD_CORE_BEGIN:load_core_begin(c->level,c->pack);break;
 case LOAD_CORE_POLL:return (uint32_t)load_core_poll(c->level);
 case LOAD_FINISH:load_finish(c->level);break;
 case LOAD_LEVEL:level(c->level,c->bytes,c->name);break;
 case LOAD_STREAM_INDEX:load_pack_stream_index(c->pack,c->kind,c->blocks,c->capacity,c->stream);break;
#if TR64_HAS_TR2
 case LOAD_VALIDATE_TR2:load_validate_tr2_gameplay(c->readonly_level);break;
#endif
 default:sys_error("load unit: invalid operation");
 }
 return 0;
}
