/* SPDX-License-Identifier: MIT */
/* Opt-in ranking probe, never shipping instrumentation. A 2003us timer samples
 * the interrupted EPC (including branch-delay adjustment). IRQ masking biases
 * samples; this is statistical function occupancy, not cycle/cache-miss proof.
 * Performance A/B MUST omit this source and its sys_memory_report link wrap.
 * Use (test build, 4 MiB image margin permitting; the samples take 16 KiB):
 *   make rom TR64_CART_SOURCES=plat/n64/hot-profile.c \
 *     TR64_CART_LIBS='--wrap sys_memory_report' ...
 * then test/run/pc-rank.py ENGINE_LOG tr64.elf. Samples run only from tick 1. */
#include <libdragon.h>
#include "tr64/engine.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
enum { PROFILE_SAMPLES=4096,PROFILE_PERIOD_US=2003 };
static uint32_t pcs[PROFILE_SAMPLES];
static volatile uint32_t count,overflow;
static timer_link_t *timer;
static int started,finished;
static void sample(int late)
{
 uint32_t pc,cause;(void)late;
 if(!tr64_eng_state()->tick)return;
 __asm__ volatile("mfc0 %0,$14; mfc0 %1,$13":"=r"(pc),"=r"(cause));
 pc+=(cause&0x80000000u)?4u:0u;
 if(count<PROFILE_SAMPLES)pcs[count++]=pc;else ++overflow;
}
__attribute__((constructor)) static void profile_boot(void)
{
 timer_init();timer=new_timer(TIMER_TICKS(PROFILE_PERIOD_US),TF_CONTINUOUS|TF_DISABLED,sample);
}
extern void __real_sys_memory_report(void);
void __wrap_sys_memory_report(void)
{
 if(started && !finished && tr64_eng_state()->tick) {
  stop_timer(timer);finished=1;
 }
 __real_sys_memory_report();
 if(!started) {
  start_timer(timer,TIMER_TICKS(PROFILE_PERIOD_US),TF_CONTINUOUS,sample);started=1;
 } else if(finished) {
  if(overflow || !count)sys_error("PC profile invalid samples=%lu overflow=%lu",(unsigned long)count,(unsigned long)overflow);
  log_printf("pc-profile: period_us=%u samples=%lu overflow=%lu ranking-only=1\n",PROFILE_PERIOD_US,(unsigned long)count,(unsigned long)overflow);
  for(uint32_t i=0;i<count;++i) {
   log_printf("pc-sample: %08lx\n",(unsigned long)pcs[i]);
   if((i&31u)==31u)log_flush();
  }
  log_flush();
 }
}
