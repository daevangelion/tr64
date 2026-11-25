/* SPDX-License-Identifier: MIT */
/* Deliberate MMU corruption, never a game path: first fault is a real AdEL in
 * the loaded test unit; its handler removes only that test mapping while the
 * SDK still regards it as loaded. Guarded unwind then takes a real TLB fault. */
#include <libdragon.h>
#include "tr64/code.h"
#include "engine/core/sys.h"
#include "plat/n64/fatal.h"
extern uint32_t tr64_unit_test(uint32_t);
static uint32_t slot;
static int first=1;
static void fault(exception_t *ex)
{
 if(first) {
  uint32_t index,lo0,lo1;first=0;
  __asm__ volatile("mfc0 %0,$0; mfc0 %1,$2; mfc0 %2,$3":"=r"(index),"=r"(lo0),"=r"(lo1));
  __asm__ volatile("mtc0 %0,$0; mtc0 $zero,$2; mtc0 $zero,$3; nop; nop; tlbwi; nop; nop"::"r"(slot):"memory");
  __asm__ volatile("mtc0 %0,$0; mtc0 %1,$2; mtc0 %2,$3; nop; nop"::"r"(index),"r"(lo0),"r"(lo1):"memory");
 }
 sys_fatal_exception(ex);
}
void tr64_fatal_test_nested(void)
{
 uint32_t index,hi,lo0,lo1,mask,entry0,entry1,at=(uint32_t)(uintptr_t)tr64_unit_test;
 code_loading_begin();code_load("test");code_loading_end();disable_interrupts();
 __asm__ volatile("mfc0 %0,$0; mfc0 %1,$10; mfc0 %2,$2; mfc0 %3,$3; mfc0 %4,$5":"=r"(index),"=r"(hi),"=r"(lo0),"=r"(lo1),"=r"(mask));
 __asm__ volatile("mtc0 %3,$10; nop; nop; tlbp; nop; nop; mfc0 %0,$0; tlbr; nop; nop; mfc0 %1,$2; mfc0 %2,$3":"=r"(slot),"=r"(entry0),"=r"(entry1):"r"(at&~8191u):"memory");
 __asm__ volatile("mtc0 %0,$0; mtc0 %1,$10; mtc0 %2,$2; mtc0 %3,$3; mtc0 %4,$5; nop; nop"::"r"(index),"r"(hi),"r"(lo0),"r"(lo1),"r"(mask):"memory");
 if(slot&0x80000000u)sys_error("nested probe missing test mapping");
 uint32_t physical=(((at&4096u)?entry1:entry0)&0x3fffffc0u)<<6;
 volatile uint32_t *instruction=(volatile uint32_t *)(uintptr_t)(0xa0000000u|physical|(at&4095u));
 *instruction=0x8c020001u; /* lw v0,1(zero): actual misaligned load. */
 __asm__ volatile("sync":::"memory");inst_cache_hit_invalidate((void *)(uintptr_t)at,4);
 register_exception_handler(fault);enable_interrupts();(void)code_call("test",0);
 sys_error("nested probe did not fault");
}
