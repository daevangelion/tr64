/* SPDX-License-Identifier: MIT */
#include <libdragon.h>
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "plat/n64/fatal.h"
#include "engine/core/unwind.h"
/* The unwinder steps over interrupt frames by this size (inthandler.S). */
_Static_assert(sizeof(reg_block_t)+32==TR64_UNWIND_EXC_FRAME,"SDK exception frame size");
typedef struct { uint32_t hi,lo0,lo1,mask; } tlb_entry_t;
#define SNAPSHOT_SKIPPED_ASID 0xffu
static void snapshot(tlb_entry_t entries[32])
{
 uint32_t hi,lo0,lo1,mask,index,i;
 disable_interrupts();
 __asm__ volatile("mfc0 %0,$10; mfc0 %1,$2; mfc0 %2,$3; mfc0 %3,$5; mfc0 %4,$0":"=r"(hi),"=r"(lo0),"=r"(lo1),"=r"(mask),"=r"(index));
 for(i=0;i<32;++i) {
  /* The M64's FPGA core silently skips a TLBR while one of its own TLB
   * lookups is in flight (docs/hw/m64-tlb.md 3.2), leaving the CP0 registers
   * as they were. EntryHi.ASID is preset to a value no entry has (all of ours
   * are written with ASID 0, and TLBR loads the entry's ASID): if it is still
   * there afterwards, the read was skipped and is repeated. */
  unsigned tries;
  for(tries=0;;++tries) {
   __asm__ volatile("mtc0 %5,$10; mtc0 %4,$0; nop; nop; tlbr; nop; nop; mfc0 %0,$10; mfc0 %1,$2; mfc0 %2,$3; mfc0 %3,$5":"=r"(entries[i].hi),"=r"(entries[i].lo0),"=r"(entries[i].lo1),"=r"(entries[i].mask):"r"(i),"r"(SNAPSHOT_SKIPPED_ASID):"memory");
   if((entries[i].hi&0xffu)!=SNAPSHOT_SKIPPED_ASID)break;
   if(tries==255) { enable_interrupts(); sys_error("TLB snapshot: TLBR skipped 256 times"); }
  }
 }
 __asm__ volatile("mtc0 %0,$10; mtc0 %1,$2; mtc0 %2,$3; mtc0 %3,$5; mtc0 %4,$0; nop; nop"::"r"(hi),"r"(lo0),"r"(lo1),"r"(mask),"r"(index):"memory");
 enable_interrupts();
}
static void fault(exception_t *ex)
{
 /* info is the SDK exception decoder's diagnostic, including the actual
  * instruction-fetch miss's unloaded-unit name, not a dispatcher check. */
 sys_fatal_exception(ex);
}
void sys_code_init(void) { tlbcode_init(); register_exception_handler(fault); }
const void *sys_code_find(const char *name,uint32_t *pages)
{
 const tlbcode_unit_t *unit=tlbcode_find(name);
 if(!unit)sys_error("code unit %s missing",name);
 *pages=unit->page_count; return unit;
}
void sys_code_load(const void *unit,void *const *frames,uint32_t count)
{ if(!tlbcode_load(unit,frames,count))sys_error("code unit load failed"); }
void sys_code_unload(const void *unit)
{ if(!tlbcode_unload(unit))sys_error("code unit unload failed"); }
unsigned sys_code_wired(void) { return tlbcode_wired_entries(); }
unsigned sys_code_entries(void)
{
 tlb_entry_t rows[32]; unsigned i,count=0;
 snapshot(rows);
 for(i=0;i<32;++i)if((rows[i].lo0|rows[i].lo1)&2u)++count;
 return count;
}
void sys_code_probe(const void *handle,int loaded)
{
 const tlbcode_unit_t *unit=handle; tlb_entry_t rows[32]; uint32_t page,i,mapped=0;
 snapshot(rows);
 for(page=0;page<unit->page_count;++page) {
  uint32_t va=unit->virtual_base+4096u*page;
  for(i=0;i<32;++i)if(!rows[i].mask && (rows[i].hi&~8191u)==(va&~8191u) && ((va&4096u ? rows[i].lo1 : rows[i].lo0)&2u)) { ++mapped; break; }
 }
 log_printf("code: probe %s mapped_pages=%lu expected=%lu\n",unit->name,(unsigned long)mapped,(unsigned long)(loaded ? unit->page_count : 0));
 if(mapped!=(loaded ? unit->page_count : 0))sys_error("code unit %s TLB mapping mismatch",unit->name);
}
void sys_code_before_call(const void *unit)
{ (void)unit; /* Native fetch, not software residency, is the cart guard. */ }
int sys_code_context_ok(void)
{
 uint32_t status=C0_STATUS();
 return (status&C0_STATUS_IE) && !(status&(C0_STATUS_EXL|C0_STATUS_ERL));
}
