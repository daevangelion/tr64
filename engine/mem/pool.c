/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "tr64/mem.h"
#include "engine/mem/pool.h"
#include "engine/core/sys.h"
static int occupied(mem_slots_t *s,uint32_t i) { return (s->bits[i/32]>>(i%32))&1; }
static void mark(mem_slots_t *s,uint32_t i,int value)
{
 uint32_t mask=1u<<(i%32);
 if(value)s->bits[i/32]|=mask; else s->bits[i/32]&=~mask;
}
void mem_slots_init(mem_slots_t *s,int pool,uint32_t slot,uint32_t *bits,uint32_t words)
{
 tr64_mem_stats_t p; tr64_mem_stats(pool,&p);
 if(p.kind!=TR64_CARVE || p.used || !slot || (slot&(slot-1)) || p.budget%slot || words<(p.budget/slot+31)/32)
  sys_error("pool %s invalid slots",p.name);
 memset(bits,0,words*sizeof(*bits));
 *s=(mem_slots_t){pool,p.base,slot,p.budget/slot,0,0,bits};
}
void *mem_slots_take_offset(mem_slots_t *s,uint32_t count,uint32_t align,uint32_t offset)
{
 uint32_t attempt,k,i; tr64_mem_stats_t p; tr64_mem_stats(s->pool,&p);
 if(!count || !align || (align&(align-1)))sys_error("pool %s invalid slot request",p.name);
 if(count>s->count)sys_error("pool %s overflow slots=%lu",p.name,(unsigned long)count);
 if(offset>=count*s->slot_bytes)sys_error("pool %s invalid slot alignment offset",p.name);
 for(attempt=0;attempt<s->count;++attempt) {
  i=(s->cursor+attempt)%s->count;
  if(count>s->count-i || (((uintptr_t)(s->base+i*s->slot_bytes)+offset)&(align-1)))continue;
  for(k=0;k<count && !occupied(s,i+k);++k) { }
  if(k!=count)continue;
  for(k=0;k<count;++k)mark(s,i+k,1);
  s->used+=count; s->cursor=(i+count)%s->count;
  mem_account(s->pool,s->used*s->slot_bytes); return s->base+i*s->slot_bytes;
 }
 sys_error("pool %s overflow slots=%lu",p.name,(unsigned long)count);
}
void *mem_slots_take(mem_slots_t *s,uint32_t count,uint32_t align)
{ return mem_slots_take_offset(s,count,align,0); }
void mem_slots_release(mem_slots_t *s,void *base,uint32_t count)
{
 uintptr_t offset=(uintptr_t)base-(uintptr_t)s->base; uint32_t i,k;
 tr64_mem_stats_t p; tr64_mem_stats(s->pool,&p);
 if(offset>=s->count*s->slot_bytes || offset%s->slot_bytes || !count || count>s->count-offset/s->slot_bytes)
  sys_error("pool %s invalid slot free",p.name);
 i=(uint32_t)(offset/s->slot_bytes);
 for(k=0;k<count;++k)if(!occupied(s,i+k))sys_error("pool %s double slot free",p.name);
 for(k=0;k<count;++k)mark(s,i+k,0);
 s->used-=count; mem_account(s->pool,s->used*s->slot_bytes);
}
