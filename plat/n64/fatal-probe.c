/* SPDX-License-Identifier: MIT */
/* Synthetic fault injection only: each case takes the real native fatal path. */
#include <libdragon.h>
#include <string.h>
#include "engine/core/sys.h"
static volatile int condition;
static int16_t audio_samples[4096] __attribute__((aligned(8)));
static void playing_audio(void)
{
 for(unsigned i=0;i<4096;++i)audio_samples[i]=(i&1)?-16384:16384;
 data_cache_hit_writeback(audio_samples,sizeof(audio_samples));
 disable_interrupts();
 *(volatile uint32_t *)0xa4500010u=2207;
 *(volatile uint32_t *)0xa4500014u=15;
 *(volatile uint32_t *)0xa4500008u=1;
 *(volatile uint32_t *)0xa4500000u=PhysicalAddr(audio_samples);
 *(volatile uint32_t *)0xa4500004u=sizeof(audio_samples);
 __asm__ volatile("sync":::"memory");
 sys_error("fatal-probe audio DMA");
}
__attribute__((noinline,optimize("no-optimize-sibling-calls")))
void tr64_fatal_test_exception(void)
{
 uintptr_t address=0x80000001u;
 __asm__ volatile("lw $v0,0(%0)"::"r"(address):"v0","memory");
 __asm__ volatile("":::"memory");
}
__attribute__((noinline,optimize("no-optimize-sibling-calls")))
static void exception_caller(void)
{ tr64_fatal_test_exception();__asm__ volatile("":::"memory"); }
static void irq_fault(void) { if(condition)exception_caller(); }
__attribute__((constructor)) static void irq_prepare(void) { register_VI_handler(irq_fault); }
__attribute__((noinline)) void tr64_fatal_test_irq_wait(void)
{
 condition=1;
 for(;;)__asm__ volatile("nop":::"memory");
}
extern void tr64_fatal_test_nested(void);
__attribute__((noinline,optimize("no-optimize-sibling-calls")))
static void error_caller(void) { sys_error("fatal-probe sys_error marker=%u",37u); }
void tr64_scene_load(const char *name)
{
 if(!strcmp(name,"fatal-exception"))exception_caller();
 else if(!strcmp(name,"fatal-assert"))assertf(condition,"fatal-probe assert marker=%u",17u);
 else if(!strcmp(name,"fatal-c-assert"))assert(condition);
 else if(!strcmp(name,"fatal-rsp"))__rsp_crash(__FILE__,__LINE__,__func__,"fatal-probe rsp marker=%u",29u);
 else if(!strcmp(name,"fatal-float-assert"))assertf(condition,"fatal-probe IEEE value=%f",1.5);
 else if(!strcmp(name,"fatal-audio"))playing_audio();
 else if(!strcmp(name,"fatal-irq")) { set_VI_interrupt(1,2);tr64_fatal_test_irq_wait(); }
 else if(!strcmp(name,"fatal-nested"))tr64_fatal_test_nested();
 else if(!strcmp(name,"fatal-sys-error"))error_caller();
 sys_error("fatal probe did not fault: %s",name);
}
