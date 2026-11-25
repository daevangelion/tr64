/* SPDX-License-Identifier: MIT */
/* MIPS prolog encoding reference: libdragon61d9cab77966 src/backtrace.c.
 * Independent bounded walker: no symbol lookup, allocation or unchecked reads. */
#include "engine/core/unwind.h"
static int stack_word(const tr64_unwind_context_t *c,tr64_unwind_read_t read,
                      void *opaque,uint32_t base,int32_t offset,uint32_t *word)
{
 int64_t at=(int64_t)base+offset;
 if(at<c->stack_begin || at>UINT32_MAX || (uint64_t)at+4>c->stack_end || (at&3))return 0;
 return read(opaque,(uint32_t)at,0,word);
}
uint32_t tr64_unwind(const tr64_unwind_context_t *c,tr64_unwind_read_t read,
                     void *opaque,uint32_t *out,uint32_t capacity)
{
 uint32_t count=0,pc,sp,fp,ra;int interrupted;
 if(!c || !read || !out || !capacity)return 0;
 if(capacity>TR64_UNWIND_FRAMES)capacity=TR64_UNWIND_FRAMES;
 pc=c->pc;sp=c->sp;fp=c->fp;ra=c->ra;interrupted=c->interrupted;
 while(count<capacity) {
  int32_t size=0,ra_slot=0,fp_slot=0;int saved_ra=0,saved_fp=0,uses_fp=0;
  uint32_t at=pc,base,next,return_pc,old_fp;
  out[count++]=pc;
  if((sp&7u) || sp<c->stack_begin || sp>=c->stack_end)break;
  if(pc>=c->interrupt_begin && pc<c->interrupt_end) {
   uint32_t epc,cause;
   /* libdragon inthandler.S: 32 ABI bytes + reg_block_t (TR64_UNWIND_EXC_FRAME).
    * The saved RA is the interrupted function's caller, not its own PC. */
   if(c->stack_end-sp<TR64_UNWIND_EXC_FRAME ||
      !stack_word(c,read,opaque,sp,312,&epc) ||
      !stack_word(c,read,opaque,sp,308,&cause) ||
      !stack_word(c,read,opaque,sp,284,&ra))break;
   pc=epc+((cause&0x80000000u)?4u:0u);sp+=TR64_UNWIND_EXC_FRAME;interrupted=1;
   continue;
  }
  for(uint32_t i=0;i<TR64_UNWIND_SCAN;++i) {
   uint32_t op;
   if(!read(opaque,at,1,&op))break;
   if((op&0xffff0000u)==0x27bd0000u || (op&0xffff0000u)==0x67bd0000u) {
    int32_t immediate=(int16_t)op;
    if(immediate<0)size=-immediate;
   } else if((op&0xffff0000u)==0xffbf0000u) {
    ra_slot=(int16_t)op+4;saved_ra=1;size=0;
   } else if((op&0xffff0000u)==0xffbe0000u) {
    fp_slot=(int16_t)op+4;saved_fp=1;
   } else if(op==0x03a0f025u)uses_fp=1;
   else if((op&0xffff0000u)==0x3c1c0000u)break;
   if(size && saved_ra)break;
   /* Only an interrupted leaf needs the padding-NOP heuristic: normal
    * callers can contain aligned load-delay NOPs inside their body (MI IRQ).
    * A real return remains a conservative boundary in either context. */
   if(!saved_ra && op==0x03e00008u)break;
   if(interrupted && !saved_ra && !op && ((at+4u)&15u)==0)break;
   if(at<4u)break;
   at-=4u;
  }
  if(!size || !saved_ra) {
   if(!interrupted || ra<8u || ra-8u==pc)break;
   if(size && (uint32_t)size>c->stack_end-sp)break;
   sp+=(uint32_t)size;pc=ra-8u;ra=0;interrupted=0;continue;
  }
  base=uses_fp ? fp : sp;
  if((base&7u) || base<sp || base>=c->stack_end || (uint32_t)size>c->stack_end-base)break;
  next=base+(uint32_t)size;
  if(next<=sp || !stack_word(c,read,opaque,base,ra_slot,&return_pc))break;
  old_fp=fp;
  if(saved_fp && !stack_word(c,read,opaque,base,fp_slot,&old_fp))break;
  if(return_pc<8u)break;
  pc=return_pc-8u;sp=next;fp=old_fp;ra=0;interrupted=0;
 }
 return count;
}
