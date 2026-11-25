/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/core/fmt.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "cart_layout.h"
#include "units.h"
#include "build_id.h"
static uint8_t rdram[TR64_RDRAM_BYTES] __attribute__((aligned(4096)));
static uint16_t *pixels;
static FILE *cart;
static uint32_t cart_size,frame_bus_us;
static uint64_t total_bus_us;
static uint32_t clock_offset;
extern void tr64_snd_report(void) __attribute__((weak));
static int read_result,presented;
void sys_error(const char *fmt,...)
{
 va_list ap;char line[256];int n;log_flush();va_start(ap,fmt);
 n=tr64_vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
 if(n<0)tr64_snprintf(line,sizeof(line),"INVALID FATAL FORMAT: %s",fmt);
 fputs("tr64 fatal: ",stderr);fputs(line,stderr);fputc('\n',stderr);sys_quit(1);
}
void sys_log_write(const char *s,uint32_t n) { fwrite(s,1,n,stdout); fflush(stdout); }
void sys_cache_writeback(void *p,uint32_t n) { (void)p;(void)n; }
void sys_memory_boot(void)
{
 uint32_t fb=sizeof(rdram)-TR64_BYTES_stack-TR64_BYTES_zbuf-TR64_BYTES_fb;
 if(mem_is_booted())return;
 if(TR64_CART_CARVE_START>=fb)sys_error("image overlaps fixed surfaces");
 mem_boot_carve(rdram+TR64_CART_CARVE_START,fb-TR64_CART_CARVE_START);
 mem_fixed(TR64_MEM_image,rdram,TR64_CART_EXTENT);
 mem_fixed(TR64_MEM_stack,rdram+sizeof(rdram)-TR64_BYTES_stack,TR64_BYTES_stack);
 mem_fixed(TR64_MEM_zbuf,rdram+sizeof(rdram)-TR64_BYTES_stack-TR64_BYTES_zbuf,TR64_BYTES_zbuf);
 mem_fixed(TR64_MEM_fb,rdram+fb,TR64_BYTES_fb);
 pixels=(uint16_t *)(rdram+fb);
}
void sys_memory_report(void)
{
 tr64_mem_log();log_printf("frame: io_us=%lu\n",(unsigned long)sys_io_frame_us());
 if(tr64_snd_report)tr64_snd_report();
 log_report();
}
/* The host libc owns its FILE runtime, not libdragon. No host libc allocation
 * is charged to the cart library pool; engine allocations use the real carve. */
void sys_heap_freeze(void) { }
void sys_heap_check(void) { }
void sys_heap_report(void) { log_printf("libheap: host-libc outside cart SDK model\n"); }
void sys_test_probe(unsigned probe) { sys_error("host unsupported cart probe %u",probe); }
void sys_pad_read(tr64_pad_t *p,tr64_pad_t *second)
{
 *p=(tr64_pad_t){0};
 if(second)*second=(tr64_pad_t){0};
}
int sys_cart_open(const char *path)
{
 long n;
 if(read_result || tr64_io_pending())return -1;
 if(cart) { fclose(cart); cart=NULL; }
 cart=fopen(path,"rb");
 if(!cart || fseek(cart,0,SEEK_END) || (n=ftell(cart))<0 || (unsigned long)n>TR64_CART_BYTES) {
  if(cart)fclose(cart);
  cart=NULL; return -1;
 }
 cart_size=(uint32_t)n; rewind(cart); return (int)cart_size;
}
int sys_cart_bind(uint32_t rom_offset,uint32_t bytes)
{ (void)rom_offset; (void)bytes; return -1; }
int sys_read_start(void *dst,uint32_t offset,uint32_t n)
{
 uint32_t us;
 if(!cart || n>TR64_IO_PIECE || n>cart_size || offset>cart_size-n || read_result)return -1;
 us=(n*195u+1023u)/1024u; frame_bus_us+=us; total_bus_us+=us;
 read_result=fseek(cart,(long)offset,SEEK_SET) || fread(dst,1,n,cart)!=n ? -1 : 1;
 return 0;
}
int sys_read_poll(void) { int result=read_result; read_result=0; return result; }
void sys_io_frame_begin(void) { frame_bus_us=0; }
uint32_t sys_io_frame_us(void) { return frame_bus_us; }
uint64_t sys_io_total_us(void) { return total_bus_us; }
void sys_present(const uint16_t *p) { memcpy(pixels,p,TR64_WIDTH*TR64_HEIGHT*2u); presented=1; }
int sys_shot(const char *path)
{
 FILE *f; unsigned i; int failed=0;
 if(!presented)return 1;
 f=fopen(path,"wb");
 if(!f)return -1;
 if(fprintf(f,"P6\n%d %d\n255\n",TR64_WIDTH,TR64_HEIGHT)<0)failed=1;
 for(i=0;i<TR64_WIDTH*TR64_HEIGHT;++i) {
  unsigned r=pixels[i]>>11&31,g=pixels[i]>>6&31,b=pixels[i]>>1&31;
  uint8_t rgb[3]={(uint8_t)(r<<3|r>>2),(uint8_t)(g<<3|g>>2),(uint8_t)(b<<3|b>>2)};
  if(fwrite(rgb,1,3,f)!=3)failed=1;
 }
 if(fclose(f))failed=1;
 return failed?-1:0;
}
void sys_quit(int status) { if(cart)fclose(cart); exit(status); }
uint32_t sys_time_us(void)
{
 struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
 return (uint32_t)((uint64_t)t.tv_sec*1000000u+(uint32_t)t.tv_nsec/1000u+total_bus_us)+clock_offset;
}
/* Host test clock: skip time instead of waiting for it (the film player, when it
 * has nothing to do until the next picture, so a whole film plays in the decode time). */
void sys_host_clock_skip(uint32_t us) { clock_offset+=us; }
uint32_t sys_profile_ticks(void) { return sys_time_us(); }
uint32_t sys_profile_us(uint32_t elapsed) { return elapsed; }
void sys_test_clock_rebase(uint32_t target)
{
#if TR64_TEST
 clock_offset+=target-sys_time_us();
#else
 (void)target;sys_error("clock rebase requires TR64_TEST=1");
#endif
}
typedef struct { const char *name; uint32_t pages; int loaded; } model_t;
#define MODEL(n) {#n,TR64_UNIT_PAGES_##n,0},
static model_t models[]={TR64_UNITS(MODEL)};
#undef MODEL
void sys_code_init(void) { }
int sys_code_context_ok(void) { return 1; } /* Host model has no ISR callbacks. */
const void *sys_code_find(const char *name,uint32_t *pages)
{
 unsigned i;
 for(i=0;i<sizeof(models)/sizeof(models[0]);++i)if(!strcmp(name,models[i].name)) {
  *pages=models[i].pages; return &models[i];
 }
 sys_error("code unit %s missing",name);
}
void sys_code_load(const void *unit,void *const *frames,uint32_t count)
{
 model_t *u=(model_t *)unit;
 if(!frames || count!=u->pages || u->loaded || sys_code_entries()+(count+1)/2>32)sys_error("host code load/TLB capacity");
 u->loaded=1;
}
void sys_code_unload(const void *unit)
{
 model_t *u=(model_t *)unit;
 if(!u->loaded)sys_error("host code unload state");
 u->loaded=0;
}
unsigned sys_code_wired(void) { return 1; }
unsigned sys_code_entries(void)
{
 unsigned i,count=sys_code_wired();
 for(i=0;i<sizeof(models)/sizeof(models[0]);++i)if(models[i].loaded)count+=(models[i].pages+1)/2;
 return count;
}
void sys_code_probe(const void *unit,int loaded)
{
 const model_t *u=unit;
 if(u->loaded!=loaded)sys_error("host code residency mismatch");
 log_printf("code: probe %s mapped_pages=%lu expected=%lu\n",u->name,(unsigned long)(loaded ? u->pages : 0),(unsigned long)(loaded ? u->pages : 0));
}
void sys_code_before_call(const void *unit)
{ const model_t *u=unit; if(!u->loaded)sys_error("code unit %s not loaded",u->name); }
