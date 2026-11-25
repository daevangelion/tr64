/* SPDX-License-Identifier: MIT */
/* Boot library allocations are owned by libheap or explicit GPU/audio scopes.
 * No fallback heap, no mixed bump allocator, no allocation/free after freeze. */
#include <libdragon.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include "tr64/mem.h"
#include "engine/mem/pool.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "plat/n64/heap.h"
#define SLOT 16u
#define WORDS(bytes) (((bytes)/SLOT+31u)/32u)
#define MAGIC 0x74683634u
typedef struct { uint32_t magic,base,slots,size; } header_t;
_Static_assert(sizeof(header_t)==16,"library allocation header size");
typedef struct {
 int pool; const char *name; mem_slots_t slots;
 uint32_t *bits,words,calls,post; int initialized;
} owner_t;
static uint32_t lib_bits[WORDS(TR64_BYTES_libheap)],gpu_bits[WORDS(TR64_BYTES_gpu)],audio_bits[WORDS(TR64_BYTES_audio)];
static owner_t owners[]={
 {TR64_MEM_libheap,"libheap",{0},lib_bits,WORDS(TR64_BYTES_libheap),0,0,0},
 {TR64_MEM_gpu,"gpu",{0},gpu_bits,WORDS(TR64_BYTES_gpu),0,0,0},
 {TR64_MEM_audio,"audio",{0},audio_bits,WORDS(TR64_BYTES_audio),0,0,0}
};
static unsigned routed;
static int scoped,frozen;
static void allowed(owner_t *o,size_t bytes)
{
 if(frozen) {
  ++o->post;
  sys_error("pool %s allocation after boot bytes=%lu post_boot_allocations=%lu frozen=%d",o->name,(unsigned long)bytes,(unsigned long)o->post,frozen);
 }
}
static void initialize(owner_t *o)
{
 if(o->initialized)return;
 sys_memory_boot();mem_library_claim(o->pool);
 mem_slots_init(&o->slots,o->pool,SLOT,o->bits,o->words);o->initialized=1;
}
static owner_t *owner_of(const void *ptr)
{
 uintptr_t p=(uintptr_t)ptr;unsigned i;
 for(i=0;i<sizeof(owners)/sizeof(owners[0]);++i) {
  owner_t *o=&owners[i];uintptr_t b=(uintptr_t)o->slots.base;
  if(o->initialized && p>=b+sizeof(header_t) && p<b+mem_budget(o->pool))return o;
 }
 sys_error("library allocation pointer outside libheap/gpu/audio");
}
static header_t *header(owner_t *o,void *ptr)
{
 uintptr_t p=(uintptr_t)ptr,b=(uintptr_t)o->slots.base;header_t *h;
 if((p&15u) || p<b+sizeof(header_t) || p>=b+mem_budget(o->pool))sys_error("pool %s invalid free",o->name);
 h=(header_t *)(p-sizeof(header_t));
 if(h->magic!=MAGIC || h->base!=p-sizeof(header_t) || !h->slots ||
    h->slots>o->slots.count || h->base+h->slots*SLOT>b+mem_budget(o->pool) ||
    h->size>h->slots*SLOT-sizeof(header_t))sys_error("pool %s invalid allocation header",o->name);
 return h;
}
static void *allocate(owner_t *o,size_t align,size_t bytes)
{
 uint32_t n;uint8_t *base;header_t *h;size_t payload=bytes ? bytes : 1;
 allowed(o,bytes);initialize(o);
 if(align<16)align=16;
 if((align&(align-1)) || align>mem_budget(o->pool) || payload>mem_budget(o->pool)-sizeof(header_t))
  sys_error("pool %s overflow allocation",o->name);
 n=(uint32_t)((payload+sizeof(header_t)+SLOT-1u)/SLOT);
 base=mem_slots_take_offset(&o->slots,n,(uint32_t)align,sizeof(header_t));
 h=(header_t *)base;*h=(header_t){MAGIC,(uint32_t)(uintptr_t)base,n,(uint32_t)bytes};
 ++o->calls;return base+sizeof(header_t);
}
void *__wrap_memalign(size_t align,size_t bytes)
{
 void *p;disable_interrupts();p=allocate(&owners[routed],align,bytes);enable_interrupts();return p;
}
void *__wrap_malloc(size_t bytes) { return __wrap_memalign(16,bytes); }
void *__wrap_calloc(size_t n,size_t bytes)
{
 void *p;owner_t *o=&owners[routed];disable_interrupts();allowed(o,bytes);
 if(bytes && n>mem_budget(o->pool)/bytes)sys_error("pool %s overflow calloc",o->name);
 p=allocate(o,16,n*bytes);memset(p,0,n*bytes);enable_interrupts();return p;
}
void __wrap_free(void *p)
{
 owner_t *o;header_t *h;void *base;uint32_t n;
 disable_interrupts();o=p ? owner_of(p) : &owners[routed];
 if(frozen)sys_error("pool %s free after boot",o->name);
 if(!p) { enable_interrupts();return; }
 h=header(o,p);base=(void *)(uintptr_t)h->base;n=h->slots;h->magic=0;
 mem_slots_release(&o->slots,base,n);enable_interrupts();
}
void *__wrap_realloc(void *p,size_t bytes)
{
 owner_t *o;header_t *h;void *next;uint32_t old,n;
 disable_interrupts();o=p ? owner_of(p) : &owners[routed];allowed(o,bytes);
 if(!p) { next=allocate(o,16,bytes);enable_interrupts();return next; }
 h=header(o,p);
 if(!bytes) { __wrap_free(p);enable_interrupts();return NULL; }
 n=(uint32_t)((bytes+sizeof(header_t)+SLOT-1u)/SLOT);
 if(bytes<=h->slots*SLOT-sizeof(header_t)) {
  if(n<h->slots)mem_slots_release(&o->slots,(uint8_t *)(uintptr_t)h->base+n*SLOT,h->slots-n);
  h->slots=n;h->size=(uint32_t)bytes;enable_interrupts();return p;
 }
 old=h->size;next=allocate(o,16,bytes);memcpy(next,p,old<bytes ? old : bytes);__wrap_free(p);
 enable_interrupts();return next;
}
void *__wrap_sbrk(int n) { sys_error("pool libheap forbidden sbrk %d",n); }
void *__wrap_sbrk_top(int n) { sys_error("pool libheap forbidden sbrk_top %d",n); }
struct _reent;
void *__wrap__malloc_r(struct _reent *r,size_t n) { (void)r;return __wrap_malloc(n); }
void *__wrap__calloc_r(struct _reent *r,size_t n,size_t s) { (void)r;return __wrap_calloc(n,s); }
void *__wrap__realloc_r(struct _reent *r,void *p,size_t n) { (void)r;return __wrap_realloc(p,n); }
void *__wrap__memalign_r(struct _reent *r,size_t a,size_t n) { (void)r;return __wrap_memalign(a,n); }
void __wrap__free_r(struct _reent *r,void *p) { (void)r;__wrap_free(p); }
void *__wrap__sbrk_r(struct _reent *r,int n) { (void)r;return __wrap_sbrk(n); }
void sys_heap_boot_pool_begin(int pool)
{
 unsigned target;
 disable_interrupts();
 if(frozen || scoped || (pool!=TR64_MEM_gpu && pool!=TR64_MEM_audio))
  sys_error("invalid boot heap scope pool=%d",pool);
 target=pool==TR64_MEM_gpu ? 1u : 2u;
 initialize(&owners[target]);routed=target;scoped=1;enable_interrupts();
}
void sys_heap_boot_pool_end(void)
{
 disable_interrupts();if(frozen || !scoped)sys_error("boot heap scope not open");
 routed=0;scoped=0;enable_interrupts();
}
void sys_heap_freeze(void)
{
 disable_interrupts();if(scoped)sys_error("boot heap scope still open");
 frozen=1;enable_interrupts();sys_heap_report();
}
void sys_heap_check(void)
{
 extern void sys_stack_check(void);
 if(!frozen)sys_error("pool libheap not frozen");
 sys_stack_check();
}
void sys_heap_report(void)
{
 unsigned i;
 for(i=0;i<sizeof(owners)/sizeof(owners[0]);++i) {
  owner_t *o=&owners[i];tr64_mem_stats_t p;tr64_mem_stats(o->pool,&p);
  log_printf("%s%s: used=%lu peak=%lu calls=%lu post_boot_allocations=%lu frozen=%d\n",o->name,o->pool==TR64_MEM_libheap ? "" : "heap",(unsigned long)p.used,(unsigned long)p.peak,(unsigned long)o->calls,(unsigned long)o->post,frozen);
 }
 log_flush();
}
size_t __wrap_malloc_usable_size(void *p)
{
 size_t n;disable_interrupts();n=header(owner_of(p),p)->size;enable_interrupts();return n;
}
struct mallinfo __wrap_mallinfo(void) { sys_error("library pools do not expose SDK mallinfo"); }
void *__wrap_valloc(size_t n) { return __wrap_memalign(4096,n); }
void *__wrap_pvalloc(size_t n)
{
 if(n>mem_budget(owners[routed].pool)-4095u)sys_error("pool %s pvalloc overflow",owners[routed].name);
 return __wrap_memalign(4096,(n+4095u)&~4095u);
}
void *__wrap__valloc_r(struct _reent *r,size_t n) { (void)r;return __wrap_valloc(n); }
void *__wrap__pvalloc_r(struct _reent *r,size_t n) { (void)r;return __wrap_pvalloc(n); }
void *__wrap_realloc_uncached(void *p,size_t n)
{
 void *cached=p ? CachedAddr(p) : NULL,*next;owner_t *o;size_t old;
 disable_interrupts();o=p ? owner_of(cached) : &owners[routed];allowed(o,n);
 if(p && n) {
  old=header(o,cached)->size;
  if(old)data_cache_hit_invalidate(cached,(old+15u)&~15u);
 }
 next=__wrap_realloc(cached,n);
 if(next && n)data_cache_hit_writeback_invalidate(next,(n+15u)&~15u);
 enable_interrupts();return next ? UncachedAddr(next) : NULL;
}
size_t __wrap__malloc_usable_size_r(struct _reent *r,void *p) { (void)r;return __wrap_malloc_usable_size(p); }
struct mallinfo __wrap__mallinfo_r(struct _reent *r) { (void)r;return __wrap_mallinfo(); }
