/* SPDX-License-Identifier: MIT */
/* Explicit surfaces: bypass the SDK's optional bank-choosing allocator.
 * Export its public adapter ABI so its default heap arenas are never linked.
 * These bytes belong to FIXED rows, never to libheap or a hidden bank. */
#include <libdragon.h>
#include <rba_ld.h>
#include "tr64/mem.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
static unsigned framebuffer_count;
static int z_allocated;
uintptr_t sys_stack_base(void) { return 0x80400000u-TR64_BYTES_stack; }
uintptr_t sys_z_base(void) { return sys_stack_base()-TR64_BYTES_zbuf; }
uintptr_t sys_fb_base(void) { return sys_z_base()-TR64_BYTES_fb; }
int rba_ld_init(const rba_ld_config_t *cfg) { (void)cfg; sys_memory_boot(); return 0; }
bool rba_ld_ready(void) { return mem_is_booted(); }
void *rba_ld_alloc_surface(size_t bytes)
{
 uintptr_t base;
 if(bytes!=TR64_WIDTH*TR64_HEIGHT*2u || framebuffer_count>=2)sys_error("pool fb invalid surface allocation");
 base=sys_fb_base()+framebuffer_count*bytes;
 ++framebuffer_count; data_cache_hit_invalidate((void *)base,bytes);
 log_printf("placement: fb%u=%08lx bytes=%lu bank=%lu\n",framebuffer_count-1,(unsigned long)base,(unsigned long)bytes,(unsigned long)((base&0x1fffffffu)>>20));
 return UncachedAddr((void *)base);
}
void *rba_ld_alloc_z(size_t bytes)
{
 uintptr_t base=sys_z_base();
 if(bytes!=TR64_BYTES_zbuf || z_allocated)sys_error("pool zbuf invalid surface allocation");
 z_allocated=1; data_cache_hit_invalidate((void *)base,bytes);
 log_printf("placement: z=%08lx bytes=%lu bank=%lu\n",(unsigned long)base,(unsigned long)bytes,(unsigned long)((base&0x1fffffffu)>>20));
 return UncachedAddr((void *)base);
}
void *rba_ld_alloc_stream(size_t bytes) { sys_error("pool io unassigned bank stream %lu",(unsigned long)bytes); }
void rba_ld_free(void *ptr) { sys_error("fixed pool surface free %p",ptr); }
void rba_ld_dump(void)
{ log_printf("placement: fixed surfaces fb=%u z=%d\n",framebuffer_count,z_allocated); }
void sys_surfaces_check(void)
{
 surface_t *z=display_get_zbuf();
 if(framebuffer_count!=2 || !z_allocated || (uintptr_t)CachedAddr(z->buffer)!=sys_z_base())sys_error("fixed surface placement mismatch");
 mem_fixed(TR64_MEM_fb,(void *)sys_fb_base(),TR64_BYTES_fb);
 mem_fixed(TR64_MEM_zbuf,(void *)sys_z_base(),TR64_BYTES_zbuf);
}
