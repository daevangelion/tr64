/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "engine/core/unwind.h"
static uint32_t code[256],stack[1024],reads;
static int word(void *opaque,uint32_t at,int text,uint32_t *out)
{
 (void)opaque;++reads;
 if(at&3u)return 0;
 if(text) { if(at<0x1000u || at>=0x1400u)return 0;*out=code[(at-0x1000u)/4]; }
 else { if(at<0x8000u || at>=0x9000u)return 0;*out=stack[(at-0x8000u)/4]; }
 return 1;
}
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"unwind FAIL line%d: %s\n",__LINE__,#x);return 1; } } while(0)
static void reset(void)
{
 for(unsigned i=0;i<256;++i)code[i]=0x01294825u;
 memset(stack,0,sizeof(stack));reads=0;
 for(unsigned i=0;i<3;++i) {
  code[i*64]=0x27bdffe0u;code[i*64+1]=0xffbf0018u;
 }
 stack[0x1c/4]=0x1118u;stack[0x3c/4]=0x1018u;
}
int main(void)
{
 tr64_unwind_context_t c={0x1220u,0x8000u,0,0x1118u,0x8000u,0x9000u,1,0,0};
 uint32_t out[TR64_UNWIND_FRAMES+1],n;
 reset();n=tr64_unwind(&c,word,NULL,out,8);
 CHECK(n==3 && out[0]==0x1220 && out[1]==0x1110 && out[2]==0x1010);
 CHECK(reads<=TR64_UNWIND_FRAMES*(TR64_UNWIND_SCAN+2));
 reset();c.pc=0xc1080000u;stack[0x1c/4]=0x1018;stack[0x3c/4]=0;n=tr64_unwind(&c,word,NULL,out,8);
 CHECK(n==3 && out[0]==0xc1080000u && out[1]==0x1110 && out[2]==0x1010);
 reset();c.pc=0x1220;c.sp=0x8000;c.fp=0x8020;
 code[128+2]=0xffbe0010u;code[128+3]=0x03a0f025u;
 stack[0x34/4]=0;stack[0x3c/4]=0x1118;stack[0x5c/4]=0x1018;
 n=tr64_unwind(&c,word,NULL,out,8);
 CHECK(n==3 && out[1]==0x1110 && out[2]==0x1010);
 reset();c.fp=0;c.sp=0x8000;c.pc=0x1320;c.ra=0x1118;
 stack[0x1c/4]=0x1018;stack[0x3c/4]=0;
 code[(0x131c-0x1000)/4]=0;
 code[(0x1318-0x1000)/4]=0x03e00008u;
 n=tr64_unwind(&c,word,NULL,out,8);CHECK(n==3 && out[1]==0x1110);
 c.interrupted=0;n=tr64_unwind(&c,word,NULL,out,8);CHECK(n==1);
 reset();c.pc=0x1220;c.sp=0x7ff8;reads=0;
 CHECK(tr64_unwind(&c,word,NULL,out,8)==1 && !reads);
 c.sp=0x8001;CHECK(tr64_unwind(&c,word,NULL,out,8)==1 && !reads);
 reset();c.sp=0x8000;c.fp=0xffffffffu;code[131]=0x03a0f025u;
 CHECK(tr64_unwind(&c,word,NULL,out,8)==1);
 reset();c.fp=0;c.pc=0x1220;c.interrupted=0;
 for(unsigned i=0;i<100;++i)stack[i*8+7]=0x1228;
 out[TR64_UNWIND_FRAMES]=0xfeedabcd;
 CHECK(tr64_unwind(&c,word,NULL,out,UINT32_MAX)==TR64_UNWIND_FRAMES);
 CHECK(out[TR64_UNWIND_FRAMES]==0xfeedabcd);
 CHECK(reads<=TR64_UNWIND_FRAMES*(TR64_UNWIND_SCAN+2));
 reads=0;CHECK(!tr64_unwind(&c,word,NULL,out,0) && !reads);
 CHECK(!tr64_unwind(NULL,word,NULL,out,8));
 /* A fault in an IRQ handler must visit the interrupted EPC, not skip to its
  * saved RA. Exercise the pinned SDK frame, BD adjustment and truncated frame. */
 reset();c.pc=0x1320;c.sp=0x8000;c.interrupt_begin=0x1300;c.interrupt_end=0x1400;
 stack[312/4]=0x1220;stack[308/4]=0x80000000u;stack[284/4]=0x1018;
 stack[(TR64_UNWIND_EXC_FRAME+0x1c)/4]=0x1118;stack[(TR64_UNWIND_EXC_FRAME+0x3c)/4]=0;
 n=tr64_unwind(&c,word,NULL,out,8);
 CHECK(n==3 && out[0]==0x1320 && out[1]==0x1224 && out[2]==0x1110);
 c.sp=0x8e00;CHECK(tr64_unwind(&c,word,NULL,out,8)==1);
 puts("unwind PASS: actual prologs, FP/alloca, unloaded fetch, leaf, corrupt SP/FP, bounded scans/frames, zero capacity");return 0;
}
