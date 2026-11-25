/* SPDX-License-Identifier: MIT */
/* Platform mechanisms ported from MIT ac3n64 04134cc. */
#include <libdragon.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "tr64/mem.h"
#include "tr64/io.h"
#include <stdlib.h>
#include "tr64/route.h"
#include "engine/core/sys.h"
#include "engine/core/fmt.h"
#include "engine/core/log.h"
#include "plat/n64/harness.h"
#include "plat/n64/gpu-platform.h"
#include "build_id.h"
#include "plat/n64/fatal.h"
#include "plat/n64/hotdata.h"
extern char __bss_end[];
extern uintptr_t sys_fb_base(void),sys_stack_base(void);
static surface_t *last;
static uint64_t ticket;
static uint64_t read_started,total_bus_us;
static int active;
static uint32_t cart_base,cart_size;
static uint32_t clock_offset;
extern void tr64_snd_report(void) __attribute__((weak));
extern void tr64_gpu_sync(void) __attribute__((weak));
/* Private, allocation-free SDK ABI: d0ec7bdec src/rompak_internal.h.
 * Walk only the trusted bundle directory, never the player's pack contents. */
extern void rompak_walk(bool (*callback)(void *,const char *,pi_addr_t,size_t),void *);
static uint32_t rom_extent;
static bool extent_entry(void *context,const char *name,pi_addr_t address,size_t bytes)
{
 uint32_t *end=context,offset;
 (void)name;
 if(address<0x10000000u || bytes>TR64_CART_BYTES) { *end=UINT32_MAX; return false; }
 offset=address-0x10000000u;
 if(offset>TR64_CART_BYTES-bytes) { *end=UINT32_MAX; return false; }
 if(offset+bytes>*end)*end=offset+(uint32_t)bytes;
 return true;
}
static uint32_t rom_content_extent(void)
{
 if(!rom_extent) {
  uint32_t end=0;
  rompak_walk(extent_entry,&end);
  if(end==UINT32_MAX)return 0;
  rom_extent=end; /* The linked ROM bundle is immutable for this boot. */
 }
 return rom_extent;
}
uint32_t sys_hardware_rdram;
void sys_stack_check(void)
{
 /* Leave the first cache lines for libdragon kernel_init's stack cookie. */
 volatile uint32_t *guard=(volatile uint32_t *)(sys_stack_base()+64u); unsigned i;
 for(i=0;i<16;++i)if(guard[i]!=0x7374616bu)sys_error("pool stack guard corrupted");
}
void sys_boot_ctors(void)
{
 typedef void (*ctor_t)(void);
 extern ctor_t __CTOR_LIST__[],__CTOR_END__[];
 volatile uint32_t *guard=(volatile uint32_t *)(sys_stack_base()+64u); unsigned i,count;
 sys_fatal_install();
 for(i=0;i<16;++i)guard[i]=0x7374616bu;
#if TR64_TEST
 if(sys_hardware_rdram==8u*1024u*1024u) {
  volatile uint32_t *high=(volatile uint32_t *)0xa0400000u;
  for(i=0;i<1024u*1024u;++i)high[i]=0x6d306734u;
 }
#endif
 /* Equivalent C-only ctor traversal with checked GCC sentinels. The SDK
  * wrapper's __register_frame_info is deliberately omitted: no C++,
  * exceptions or DWARF unwinding are used; libdragon backtrace uses code. */
 count=(unsigned)(((uintptr_t)__CTOR_END__-(uintptr_t)__CTOR_LIST__)/sizeof(ctor_t));
 if(count<2 || (uintptr_t)__CTOR_LIST__[0]!=UINT32_MAX || __CTOR_LIST__[count-1])
  sys_error("invalid C constructor sentinels");
 for(i=count-1;i>1;) {
  --i; if(!__CTOR_LIST__[i])sys_error("empty constructor entry"); __CTOR_LIST__[i]();
 }
}
void sys_memory_boot(void)
{
 uintptr_t base=(((uintptr_t)__bss_end-TR64_POOL_PHASE+TR64_DCACHE_BYTES-1u)&~(uintptr_t)(TR64_DCACHE_BYTES-1u))+TR64_POOL_PHASE,sp;
 if(mem_is_booted())return;
 __asm__ volatile("move %0,$sp":"=r"(sp));
 if(get_memory_size()!=TR64_RDRAM_BYTES || sp<sys_stack_base() || sp>=0x80400000u)
  sys_error("pool stack placement outside stock RDRAM");
 if(base>=sys_fb_base())sys_error("image overlaps fixed surfaces");
 mem_boot_carve((void *)base,(uint32_t)(sys_fb_base()-base));
 mem_fixed(TR64_MEM_stack,(void *)sys_stack_base(),TR64_BYTES_stack);
 mem_fixed(TR64_MEM_image,(void *)0x80000000u,(uint32_t)((uintptr_t)__bss_end-0x80000000u));
}
void sys_memory_report(void)
{
 uintptr_t sp; unsigned changed=0,checked=0;
 __asm__ volatile("move %0,$sp":"=r"(sp));
 sys_stack_check();
 if(sp<sys_stack_base() || sp>=0x80400000u)sys_error("pool stack overflow");
#if TR64_TEST
 unsigned i;
 if(sys_hardware_rdram==8u*1024u*1024u) {
  volatile uint32_t *high=(volatile uint32_t *)0xa0400000u;
  checked=1024u*1024u;
  for(i=0;i<checked;++i)if(high[i]!=0x6d306734u)++changed;
 }
#endif
 log_printf("placement: hardware_rdram=%lu sdk_rdram=%lu guard_checked=%u guard_changed=%u\n",(unsigned long)sys_hardware_rdram,(unsigned long)get_memory_size(),checked,changed);
 if(changed)sys_error("highmem guard changed words=%u",changed);
 tr64_mem_log();
 log_printf("placement: stack=%08lx end=%08lx sp=%08lx highest_owned=%08lx\n",(unsigned long)sys_stack_base(),(unsigned long)(sys_stack_base()+TR64_BYTES_stack-1),(unsigned long)sp,(unsigned long)(sys_stack_base()+TR64_BYTES_stack-1));
 log_printf("placement: hot_data=%08lx bytes=%lu\n",(unsigned long)(uintptr_t)tr64_hot_data_start,(unsigned long)(tr64_hot_data_end-tr64_hot_data_start));
 sys_heap_report(); log_flush();
 if(tr64_snd_report) { tr64_snd_report();log_flush(); }
 log_report();
}
void sys_log_write(const char *s,uint32_t n) { (void)write(2,s,n); }
void sys_error(const char *fmt,...)
{
 char line[256]; va_list ap;
 va_start(ap,fmt);int n=tr64_vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
 if(n<0)tr64_snprintf(line,sizeof(line),"INVALID FATAL FORMAT: %s",fmt);
 sys_fatal_message(line,(uint32_t)(uintptr_t)__builtin_return_address(0));
}
void sys_cache_writeback(void *p,uint32_t n) { data_cache_hit_writeback(p,n); }
void sys_pad_read(tr64_pad_t *p,tr64_pad_t *second)
{
 joypad_inputs_t in; joypad_poll(); in=joypad_get_inputs(JOYPAD_PORT_1);
 p->buttons=in.btn.raw; p->x=in.stick_x; p->y=in.stick_y;
 if(second) {
  /* libdragon clears current inputs when polling a missing device. */
  in=joypad_get_inputs(JOYPAD_PORT_2);
  second->buttons=in.btn.raw; second->x=in.stick_x; second->y=in.stick_y;
 }
}
int sys_cart_bind(uint32_t rom_offset,uint32_t bytes)
{
 uint32_t extent;
 if(active || tr64_io_pending() || !bytes || (rom_offset&1u) ||
    bytes>TR64_CART_BYTES || rom_offset>TR64_CART_BYTES-bytes)return -1;
 extent=rom_content_extent();
 if(bytes>extent || rom_offset>extent-bytes)return -1;
 cart_base=0x10000000u+rom_offset; cart_size=bytes;
 return 0;
}
int sys_read_start(void *dst,uint32_t off,uint32_t n)
{
 if(active || !n || n>TR64_IO_PIECE || n>cart_size || off>cart_size-n)return -1;
 data_cache_hit_writeback_invalidate(dst,n);
 read_started=get_ticks();
 ticket=dma_read_async(dst,cart_base+off,n); active=1; return 0;
}
int sys_read_poll(void)
{
 if(!active || !dma_finished(ticket))return 0;
 total_bus_us+=TICKS_TO_US(get_ticks()-read_started);
 active=0; return 1;
}
uint64_t sys_io_total_us(void) { return total_bus_us; }
void sys_gpu_present(surface_t *surface)
{
 if(!surface)sys_error("video: NULL GPU surface");
 if(tr64_gpu_sync)tr64_gpu_sync();
 last=surface;display_show(surface);
}
void sys_present(const uint16_t *p)
{
 if(tr64_gpu_sync)tr64_gpu_sync();
 surface_t *fb=display_get();
 memcpy(fb->buffer,p,TR64_WIDTH*TR64_HEIGHT*2u);
 sys_gpu_present(fb);
}
int sys_shot(const char *path)
{
 (void)path;
 if(!last || !th_present())return 1;
 th_screenshot(last->buffer,TR64_WIDTH,TR64_HEIGHT,2); return 0;
}
void sys_quit(int code)
{
 log_flush(); th_exit(code);
 for(;;) { }
}
uint32_t sys_time_us(void) { return (uint32_t)TICKS_TO_US(get_ticks())+clock_offset; }
uint32_t sys_profile_ticks(void) { return TICKS_READ(); }
uint32_t sys_profile_us(uint32_t elapsed) { return (uint32_t)TICKS_TO_US((uint64_t)elapsed); }
void sys_test_clock_rebase(uint32_t target)
{
#if TR64_TEST
 clock_offset+=target-sys_time_us();
#else
 (void)target;sys_error("clock rebase requires TR64_TEST=1");
#endif
}
void sys_io_frame_begin(void) { }
uint32_t sys_io_frame_us(void) { return 0; }
void sys_test_probe(unsigned probe)
{
#if TR64_TEST
 if(probe==TR64_PROBE_HIGHMEM) {
  if(sys_hardware_rdram!=8u*1024u*1024u)sys_error("highmem probe requires Expansion Pak");
  *(volatile uint32_t *)0xa0400000u^=1u;
 } else if(probe==TR64_PROBE_HEAP) {
  volatile uint8_t *p=malloc(1); *p=1; free((void *)p);
 } else sys_error("unknown cart probe %u",probe);
#else
 sys_error("cart test probe %u requires TR64_TEST=1",probe);
#endif
}
