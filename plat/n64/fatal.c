/* SPDX-License-Identifier: MIT */
/* Diagnostic3x5 glyphs: MIT ac3n64 04134cc, as used by tr64's boot card.
 * VI timing/register layout reference: libdragon61d9cab77966 vi.c/exception.h.
 * No display/console/inspector, symbolizer, queues or heap on the fatal path. */
#include <libdragon.h>
#include <vi.h>
#include <stddef.h>
#include "engine/core/fmt.h"
#include <string.h>
#include "build_id.h"
#include "tr64/limits.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/core/unwind.h"
#include "engine/core/qr.h"
#include "tr64/game.h"
#include "plat/n64/fatal.h"
#include "plat/n64/harness.h"
/* Resident diagnostic code: size over speed. */
#pragma GCC optimize ("Os")
typedef struct { uint64_t gpr[32];uint32_t sr,cr,epc; } fatal_regs_t;
_Static_assert(offsetof(fatal_regs_t,sr)==256 && offsetof(fatal_regs_t,epc)==264,"fatal capture ABI");
_Static_assert(offsetof(reg_block_t,gpr)==0 && offsetof(reg_block_t,sr)==272 && offsetof(reg_block_t,badvaddr)==544,"SDK exception ABI");
extern void sys_fatal_capture(fatal_regs_t *);
/* Absent from builds without the game (fatal probes, boot scenes): weak. */
extern int32_t game_level_id(void) __attribute__((weak));
extern int game_stats_view(tr64_game_level_stats_t *) __attribute__((weak));
extern uintptr_t sys_fb_base(void),sys_stack_base(void);
extern void (*__assert_func_ptr)(const char *,int,const char *,const char *);
static fatal_regs_t live;
extern uint32_t inthandler[],inthandler_end[];
static unsigned reporting;
/* exception.o's diagnostic name arrays otherwise extract inspector.o before
 * section GC, including its kept constructor. Supply the exact SDK ABI here;
 * unused dump tables can then be collected without pulling an interactive UI. */
const char *__mips_gpr[34]={
 "zr","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7",
 "s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","s8","ra","lo","hi"
};
const char *__mips_fpreg[32]={
 "$f0","$f1","$f2","$f3","$f4","$f5","$f6","$f7","$f8","$f9","$f10","$f11","$f12","$f13","$f14","$f15",
 "$f16","$f17","$f18","$f19","$f20","$f21","$f22","$f23","$f24","$f25","$f26","$f27","$f28","$f29","$f30","$f31"
};
static const char glyph_names[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-/:";
static const uint16_t glyph_bits[]={
 31599,11415,29671,29647,23497,31183,31215,29257,31727,31695,
 11245,27566,31015,27502,31207,31204,31083,23533,29847,4718,
 23469,18727,24429,24445,31599,31716,31609,31661,31183,29842,
 23407,23402,23421,23213,23186,29351,448,4772,1040
};
#define G(a,b,c,d,e) ((a)<<12|(b)<<9|(c)<<6|(d)<<3|(e))
static uint16_t glyph(unsigned char c)
{
 const char *p;if(c>='a' && c<='z')c=(unsigned char)(c-'a'+'A');
 p=strchr(glyph_names,c);if(p && c)return glyph_bits[p-glyph_names];
 switch(c) {
 case ' ':return 0;case '_':return G(0,0,0,0,7);case '.':return G(0,0,0,0,2);
 case ',':return G(0,0,0,2,4);case '=':return G(0,7,0,7,0);case '+':return G(0,2,7,2,0);
 case '!':return G(2,2,2,0,2);case '?':return G(7,1,3,0,2);case '(':return G(1,2,2,2,1);
 case ')':return G(4,2,2,2,4);case '[':return G(3,2,2,2,3);case ']':return G(6,2,2,2,6);
 case '<':return G(1,2,4,2,1);case '>':return G(4,2,1,2,4);case '*':return G(0,5,2,5,0);
 case '&':return G(2,5,2,5,3);case '|':return G(2,2,2,2,2);case '%':return G(5,1,2,4,5);
 case '#':return G(5,7,5,7,5);case '\\':return G(4,4,2,1,1);case '"':return G(5,5,0,0,0);
 case '\'':return G(2,2,0,0,0);case ';':return G(0,2,0,2,4);case '^':return G(2,5,0,0,0);
 case '~':return G(0,3,6,0,0);case '@':return G(7,5,7,4,7);case '$':return G(3,6,3,6,2);
 case '{':return G(3,2,6,2,3);case '}':return G(6,2,3,2,6);case '`':return G(4,2,0,0,0);
 default:return G(7,1,3,0,2);
 }
}
#undef G
/* Text stays in the left strip so the QR symbol (bottom right) is never
 * overlapped: WIDE for the header rows above it, STRIP below. */
#define QR_SCALE 3u
#define QR_X (TR64_WIDTH-TR64_QR_EDGE*QR_SCALE)
#define QR_Y (TR64_HEIGHT-TR64_QR_EDGE*QR_SCALE)
#define STRIP (QR_X-4u)
static unsigned text(uint16_t *fb,unsigned y,const char *s,unsigned rows,uint16_t colour,unsigned right)
{
 unsigned x=4,end=y+6*rows;
 for(unsigned n=0;s && *s && n<512 && y<end && y+5<TR64_HEIGHT;++s,++n) {
  unsigned char c=(unsigned char)*s;uint16_t bits;
  if(c=='\r')continue;
  if(c=='\n' || x+3>right) { x=4;y+=6;if(c=='\n')continue; }
  if(y>=end || y+5>=TR64_HEIGHT)break;
  bits=glyph(c=='\t' ? ' ' : c);
  for(unsigned r=0;r<5;++r)for(unsigned col=0;col<3;++col)
   if(bits&(1u<<(14-r*3-col)))fb[(y+r)*TR64_WIDTH+x+col]=colour;
  x+=4;
 }
 return y+6;
}
static unsigned row(uint16_t *fb,unsigned y,uint16_t colour,const char *fmt,...) __attribute__((format(printf,4,5)));
static unsigned row(uint16_t *fb,unsigned y,uint16_t colour,const char *fmt,...)
{
 char line[40];va_list ap;
 va_start(ap,fmt);tr64_vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
 return text(fb,y,line,1,colour,STRIP);
}
static uint32_t unit_mask(void)
{
 uint32_t mask=0;
 /* Unit slot n maps at 0xC1000000+n*0x80000 (tlbcode directory, 32 slots). */
 for(unsigned n=0;n<32;++n)if(tlbcode_address_valid(0xc1000000u+(n<<19)))mask|=1u<<n;
 return mask;
}
static __attribute__((noinline)) void qr_paint(uint16_t *fb,const tr64_qr_record_t *rec)
{
 char symbol[TR64_QR_CHARS+1];unsigned n=tr64_qr_pack(symbol,rec);
 tr64_qr_draw(fb,TR64_WIDTH,QR_X,QR_Y,QR_SCALE,symbol,n,0x0001u,0xffffu);
 log_printf("fatal-qr: %s\n",symbol);log_flush();
}
static int read_word(void *opaque,uint32_t at,int code,uint32_t *word)
{
 (void)opaque;
 if((at&3u) || (code ? !tlbcode_address_valid(at) :
    at<sys_stack_base() || at>0x80400000u-4u))return 0;
 *word=*(const volatile uint32_t *)(uintptr_t)at;return 1;
}
static void show_vi(uint16_t *fb)
{
 const vi_timing_preset_t *p=&VI_TIMING_NTSC;int tv=get_tv_type();
 if(tv==TV_PAL)p=&VI_TIMING_PAL;else if(tv==TV_MPAL)p=&VI_TIMING_MPAL;
 /* Fatal context cannot allocate callback nodes or wait on an interrupt/RSP.
  * Program the same public SDK timing preset directly, in progressive16bpp. */
 *VI_CTRL=0;*VI_ORIGIN=PhysicalAddr(fb);*VI_WIDTH=TR64_WIDTH;*VI_V_INTR=2;
 *VI_BURST=p->vi_burst;*VI_V_TOTAL=p->vi_v_total;*VI_H_TOTAL=p->vi_h_total;
 *VI_H_TOTAL_LEAP=p->vi_h_total_leap;*VI_V_BURST=p->vi_v_burst;
 *VI_H_VIDEO=VI_H_VIDEO_SET(p->display.x0,p->display.x0+p->display.width);
 *VI_V_VIDEO=VI_V_VIDEO_SET(p->display.y0,p->display.y0+p->display.height);
 *VI_X_SCALE=(TR64_WIDTH<<10)/p->display.width;
 *VI_Y_SCALE=(TR64_HEIGHT<<11)/p->display.height;
 /* Same NTSC320/16bpp no-AA workaround as SDK display.c. */
 if(p->display.x0<128 && *VI_X_SCALE==0x200)*VI_X_SCALE=0x201;
 __asm__ volatile("sync":::"memory");
 *VI_CTRL=VI_PIXEL_ADVANCE_DEFAULT|VI_AA_MODE_NONE|VI_CTRL_TYPE_16_BPP;
}
typedef struct {
 unsigned kind;const char *message,*detail;const uint64_t *gpr;
 uint32_t pc,sr,cause,origin,ra,epc;int interrupted;uint64_t badvaddr;int has_badvaddr;
} fatal_t;
static int narrow(uint64_t v) { return (uint32_t)(v>>32)==((v&0x80000000u)?0xffffffffu:0u); }
/* Exception-entry stash written by sys_vec_common (fatal-capture.S). */
typedef struct { uint32_t vec,cause,sr,epc;uint64_t badvaddr,context,xcontext,entryhi;uint32_t sp,ra,pad[2]; } vec_slot_t;
typedef struct { uint32_t count,vector,pad[2];vec_slot_t slot[2]; } vec_stash_t;
_Static_assert(sizeof(vec_slot_t)==64 && offsetof(vec_stash_t,slot)==16 && offsetof(vec_slot_t,epc)==12 &&
 offsetof(vec_slot_t,badvaddr)==16 && offsetof(vec_slot_t,entryhi)==40 && offsetof(vec_slot_t,ra)==52,"vector stash ABI");
vec_stash_t sys_vec_stash __attribute__((aligned(8)));
extern const uint32_t sys_vec_template[16];
/* TLB access on the fatal path: probes and reads only, nothing faults. */
static void tlb_read(unsigned i,tr64_qr_tlb_t *e)
{
 uint64_t hi,lo0,lo1;uint32_t mask;unsigned tries=0;
 /* An FPGA core can skip a TLBR while a lookup of its own is in flight, leaving the
  * registers as they were: EntryHi.ASID is preset to 0xFF (no entry has it; tlbr loads the
  * entry's own ASID) and the read repeated while it is still there. */
 do {
  __asm__ volatile("mtc0 %5,$10; mtc0 %4,$0; nop; nop; tlbr; nop; nop; nop; dmfc0 %0,$10; dmfc0 %1,$2; dmfc0 %2,$3; mfc0 %3,$5"
   :"=r"(hi),"=r"(lo0),"=r"(lo1),"=r"(mask):"r"(i),"r"(0xffu));
 } while((hi&0xffu)==0xffu && ++tries<16);
 e->index=(uint8_t)i;e->hi=hi;e->lo0=(uint32_t)lo0;e->lo1=(uint32_t)lo1;e->mask=mask;
}
static int tlb_find(uint32_t va,tr64_qr_tlb_t *e)
{
 uint32_t index;uint64_t wide=(uint64_t)(int64_t)(int32_t)va;
 e->index=0xff;e->hi=0;e->lo0=e->lo1=e->mask=0;
 __asm__ volatile("dmtc0 %1,$10; nop; nop; tlbp; nop; nop; nop; mfc0 %0,$0":"=r"(index):"r"(wide));
 if(index&0x80000000u)return 0;
 tlb_read(index&31u,e);return 1;
}
/* Physical address of va: 1 for kseg0/kseg1, 2 through a valid TLB entry (e filled), else 0. */
static int translate(uint32_t va,tr64_qr_tlb_t *e,uint32_t *pa)
{
 e->index=0xff;
 if(va>=0x80000000u && va<0xc0000000u) { *pa=va&0x1fffffffu;return 1; }
 if(va<0xc0000000u || va>=0xe0000000u || !tlb_find(va,e))return 0;
 {
  uint32_t off=(e->mask|0x1fffu)>>1,lo=(va&(off+1u)) ? e->lo1 : e->lo0;
  if(!(lo&2u))return 0;
  *pa=((lo&0x3fffffc0u)<<6)|(va&off);return 2;
 }
}
/* Four words at va read from RDRAM through kseg1 (what the instruction cache fills from). */
static int read_kseg1(uint32_t va,uint32_t *w,tr64_qr_tlb_t *e)
{
 uint32_t pa;
 if(!translate(va,e,&pa) || pa>0x007ffff0u)return 0;
 for(unsigned i=0;i<4;++i)w[i]=*(volatile uint32_t *)(uintptr_t)(0xa0000000u|(pa+4u*i));
 return 1;
}
static uint32_t unit_slot(uint32_t a)
{ return a>=0xc1000000u && a<0xc1000000u+(32u<<19) ? (a-0xc1000000u)>>19 : 32u; }
/* Every fact of the record except the walk. Reads CP0 first, then the TLB. */
static __attribute__((noinline)) void collect(tr64_qr_record_t *rec,const fatal_t *f,uint64_t live_badv)
{
 tr64_game_level_stats_t stats;tr64_qr_tlb_t scratch;
 uint32_t slot,mask=unit_mask(),count=0,i;
 const vec_slot_t *s=&sys_vec_stash.slot[0];
 int mapped;
 memset(rec,0,sizeof(*rec));
 rec->kind=(uint8_t)f->kind;rec->code=f->kind==TR64_QR_EXCEPTION ? (uint8_t)((f->cause>>2)&31u) : 0;
 rec->build=TR64_BUILD_ID;rec->message=f->detail;
 rec->pc=f->pc;rec->ra=f->ra;rec->sr=f->sr;rec->cause=f->cause;rec->epc=f->epc;
 rec->level=game_level_id ? game_level_id() : -1;
 rec->tick=game_stats_view && game_stats_view(&stats) ? stats.time : 0;
 rec->units=mask;
 rec->badvaddr=f->has_badvaddr ? f->badvaddr : live_badv; /* the SDK saved it at exception entry */
 __asm__ volatile("dmfc0 %0,$10":"=r"(rec->entryhi));
 __asm__ volatile("dmfc0 %0,$4":"=r"(rec->context));
 if(f->kind==TR64_QR_EXCEPTION && sys_vec_stash.count) {
  /* The entry-time values: the SDK handler and the code before it have used the TLB registers since. */
  rec->flags|=TR64_QR_F_STASH;rec->vec=(uint8_t)s->vec;
  rec->entryhi=s->entryhi;rec->context=s->context;
  if(sys_vec_stash.count>1) {
   rec->flags|=TR64_QR_F_NESTED;
   /* A second exception with EXL already set leaves EPC as the first one set it. */
   if(sys_vec_stash.slot[1].epc==s->epc)rec->flags|=TR64_QR_F_EXL;
  }
 }
 rec->count=(uint8_t)(sys_vec_stash.count>255u ? 255u : sys_vec_stash.count);
 for(i=1;i<32;++i) {
  rec->gpr[i]=(uint32_t)f->gpr[i];
  if(!narrow(f->gpr[i]))rec->gpr_wide|=1u<<i;
 }
 if(f->kind==TR64_QR_EXCEPTION && read_kseg1(f->epc-4u,rec->epc_words,&scratch))rec->flags|=TR64_QR_F_EPC_WORDS;
 if(tlb_find(f->pc,&rec->pc_entry))rec->flags|=TR64_QR_F_PC_MAPPED;
 slot=unit_slot(f->pc);if(slot==32)slot=unit_slot(f->ra);
 if(slot==32)for(i=0;i<32;++i)if(mask>>i&1u) { slot=i;break; }
 rec->unit=(uint8_t)(slot==32 ? 0xff : slot);
 rec->unit_entry.index=0xff;
 if(slot<32) {
  uint32_t base=0xc1000000u+(slot<<19),pa;
  mapped=translate(base,&rec->unit_entry,&pa);
  if(mapped==2) {
   rec->flags|=TR64_QR_F_UNIT_MAPPED;rec->frame=pa&~0xfffu;
   for(i=0;i<4;++i)rec->mapped_words[i]=*(volatile uint32_t *)(uintptr_t)(base+4u*i);
   rec->flags|=TR64_QR_F_MAPPED_WORDS;
  }
  if(mapped && read_kseg1(base,rec->entry_words,&scratch))rec->flags|=TR64_QR_F_ENTRY_WORDS;
 }
 for(i=0;i<32;++i) {
  tr64_qr_tlb_t row;
  tlb_read(i,&row);
  if((row.lo0|row.lo1)&2u)++count;
 }
 rec->tlb_valid=(uint8_t)count;
}
static void show(const fatal_t *f) __attribute__((noreturn));
static void show(const fatal_t *f)
{
 uint16_t *fb;tr64_qr_record_t rec;
 uint32_t trace[TR64_UNWIND_FRAMES],count=0,sp_status,dp_status,ai_before,ai_stopped;
 uint64_t badvaddr;
 unsigned y=4;disable_interrupts();
 if(++reporting>2)for(;;)__asm__ volatile("nop");
 fb=UncachedAddr((void *)sys_fb_base());
 /* Stop DMA immediately, without audio_close's wait/free/IRQ machinery.
  * Keep the DAC clock: ares emits zero for disabled DMA; DACRATE=0 would
  * instead request a ~48MHz audio stream while the fatal screen is shown. */
 ai_before=*(volatile uint32_t *)0xa450000cu;
 *(volatile uint32_t *)0xa4500008u=0;
 *(volatile uint32_t *)0xa450000cu=0;
 __asm__ volatile("sync":::"memory");
 ai_stopped=*(volatile uint32_t *)0xa4500004u;
 if(reporting==2) {
  static const char nested[]="fatal-nested: first report retained; backtrace aborted\n";
  text(fb,TR64_HEIGHT-14,"NESTED FATAL -",1,0xf801u,STRIP);
  text(fb,TR64_HEIGHT-8,"FIRST REPORT RETAINED",1,0xf801u,STRIP);
  log_flush();sys_log_write(nested,sizeof(nested)-1);
  show_vi(fb);
  if(th_present()) { th_screenshot(fb,TR64_WIDTH,TR64_HEIGHT,2);th_exit(1); }
  for(;;)__asm__ volatile("nop");
 }
 __asm__ volatile("dmfc0 %0,$8":"=r"(badvaddr));
 sp_status=*SP_STATUS;dp_status=*DP_STATUS;
 *SP_STATUS=SP_WSTATUS_SET_HALT;*DP_STATUS=DP_WSTATUS_SET_FREEZE|DP_WSTATUS_SET_FLUSH;
 log_flush();
 for(unsigned i=0;i<TR64_WIDTH*TR64_HEIGHT;++i)fb[i]=0x1001u;
 collect(&rec,f,badvaddr);
 y=text(fb,y,"TR64 FATAL",1,0xf801u,STRIP);
 y=text(fb,y,"BUILD " TR64_BUILD_ID,1,0xffffu,STRIP);
 y=text(fb,y,f->message,5,0xffffu,STRIP);
 y=row(fb,y,0xafffu,"PC %08lX",(unsigned long)f->pc);
 y=row(fb,y,0xafffu,"RA %08lX",(unsigned long)f->ra);
 y=row(fb,y,0xafffu,"SP %08lX",(unsigned long)f->gpr[29]);
 y=row(fb,y,0xafffu,"SR %08lX",(unsigned long)f->sr);
 y=row(fb,y,0xafffu,"CAUSE %08lX",(unsigned long)f->cause);
 y=row(fb,y,0xafffu,"EPC %08lX",(unsigned long)rec.epc);
 y=row(fb,y,0xafffu,"BVA %016llX",(unsigned long long)rec.badvaddr);
 y=row(fb,y,0xafffu,"EHI %016llX",(unsigned long long)rec.entryhi);
 y=row(fb,y,0xafffu,"CTX %016llX",(unsigned long long)rec.context);
 y=row(fb,y,0xafffu,"V%u N%u T%u U%02X F%02X",(unsigned)rec.vec,(unsigned)rec.count,(unsigned)rec.tlb_valid,(unsigned)rec.unit,(unsigned)rec.flags);
 y=row(fb,y,0xffffu,"E %08lX %08lX",(unsigned long)rec.epc_words[0],(unsigned long)rec.epc_words[1]);
 y=row(fb,y,0xffffu,"E %08lX %08lX",(unsigned long)rec.epc_words[2],(unsigned long)rec.epc_words[3]);
 y=row(fb,y,0xffffu,"K %08lX %08lX",(unsigned long)rec.entry_words[0],(unsigned long)rec.entry_words[1]);
 y=row(fb,y,0xffffu,"K %08lX %08lX",(unsigned long)rec.entry_words[2],(unsigned long)rec.entry_words[3]);
 y=row(fb,y,0xffffu,"M %08lX %08lX",(unsigned long)rec.mapped_words[0],(unsigned long)rec.mapped_words[1]);
 y=row(fb,y,0xffffu,"M %08lX %08lX",(unsigned long)rec.mapped_words[2],(unsigned long)rec.mapped_words[3]);
 y=row(fb,y,0xafffu,"PM %02X %08lX",(unsigned)rec.pc_entry.index,(unsigned long)rec.pc_entry.hi);
 y=row(fb,y,0xafffu,"   %08lX %08lX",(unsigned long)rec.pc_entry.lo0,(unsigned long)rec.pc_entry.lo1);
 y=row(fb,y,0xafffu,"UM %02X %08lX",(unsigned)rec.unit_entry.index,(unsigned long)rec.unit_entry.hi);
 y=row(fb,y,0xafffu,"   %08lX %08lX",(unsigned long)rec.unit_entry.lo0,(unsigned long)rec.unit_entry.lo1);
 y=row(fb,y,0xafffu,"SPSTAT %08lX",(unsigned long)sp_status);
 y=row(fb,y,0xafffu,"DPSTAT %08lX",(unsigned long)dp_status);
 log_printf("tr64 fatal: %s\nfatal-context: pc=%08lx origin=%08lx sr=%08lx cause=%08lx badvaddr=%08lx sp_status=%08lx dp_status=%08lx\n",
  f->message,(unsigned long)f->pc,(unsigned long)f->origin,(unsigned long)f->sr,(unsigned long)f->cause,(unsigned long)rec.badvaddr,(unsigned long)sp_status,(unsigned long)dp_status);
 log_printf("fatal-cp0: epc=%08lx badvaddr=%016llx entryhi=%016llx context=%016llx vec=%u count=%u tlb_valid=%u unit=%u flags=%02x frame=%08lx\n",
  (unsigned long)rec.epc,(unsigned long long)rec.badvaddr,(unsigned long long)rec.entryhi,(unsigned long long)rec.context,
  (unsigned)rec.vec,(unsigned)rec.count,(unsigned)rec.tlb_valid,(unsigned)rec.unit,(unsigned)rec.flags,(unsigned long)rec.frame);
 log_printf("fatal-words: epc=%08lx %08lx %08lx %08lx entry=%08lx %08lx %08lx %08lx mapped=%08lx %08lx %08lx %08lx\n",
  (unsigned long)rec.epc_words[0],(unsigned long)rec.epc_words[1],(unsigned long)rec.epc_words[2],(unsigned long)rec.epc_words[3],
  (unsigned long)rec.entry_words[0],(unsigned long)rec.entry_words[1],(unsigned long)rec.entry_words[2],(unsigned long)rec.entry_words[3],
  (unsigned long)rec.mapped_words[0],(unsigned long)rec.mapped_words[1],(unsigned long)rec.mapped_words[2],(unsigned long)rec.mapped_words[3]);
 log_printf("fatal-tlb: pc index=%u hi=%016llx lo0=%08lx lo1=%08lx mask=%08lx unit index=%u hi=%016llx lo0=%08lx lo1=%08lx mask=%08lx\n",
  (unsigned)rec.pc_entry.index,(unsigned long long)rec.pc_entry.hi,(unsigned long)rec.pc_entry.lo0,(unsigned long)rec.pc_entry.lo1,(unsigned long)rec.pc_entry.mask,
  (unsigned)rec.unit_entry.index,(unsigned long long)rec.unit_entry.hi,(unsigned long)rec.unit_entry.lo0,(unsigned long)rec.unit_entry.lo1,(unsigned long)rec.unit_entry.mask);
 /* The log keeps all 64 bits of every register; the record has the low 32 bits
  * and a mask of the registers whose upper half is not the sign extension. */
 for(unsigned i=0;i<32;i+=2) {
  const uint64_t *g=f->gpr;
  if(i==26) {
   log_printf("fatal-register: R26 NOT SAVED R27 NOT SAVED\n");continue;
  }
  log_printf("fatal-register: R%02u %016llX R%02u %016llX\n",i,(unsigned long long)g[i],i+1,(unsigned long long)g[i+1]);
 }
 show_vi(fb);log_flush();
 /* The QR symbol first carries every fact but the walk, so a fault inside the
  * unwinder (nested fatal) still leaves a decodable first report. */
 qr_paint(fb,&rec);
 {
  tr64_unwind_context_t c={f->pc,(uint32_t)f->gpr[29],(uint32_t)f->gpr[30],(uint32_t)f->gpr[31],
   (uint32_t)sys_stack_base(),0x80400000u,f->interrupted,
   (uint32_t)(uintptr_t)inthandler,(uint32_t)(uintptr_t)inthandler_end};
  count=tr64_unwind(&c,read_word,NULL,trace,TR64_UNWIND_FRAMES);
 }
 y=text(fb,y,"BACKTRACE (GUESS)",1,0xafffu,STRIP);
 for(uint32_t i=0;i<count;++i)y=row(fb,y,0xffffu,"%lu %08lX",(unsigned long)i,(unsigned long)trace[i]);
 for(uint32_t i=0;i<count;++i)log_printf("fatal-backtrace: frame=%lu pc=%08lx\n",(unsigned long)i,(unsigned long)trace[i]);
 if(!count)text(fb,y,"UNAVAILABLE: INVALID STACK",1,0xffffu,STRIP);
 if(count>1) {
  /* trace[0] is the PC already in the record; the rest are call sites. */
  rec.nframes=(uint8_t)(count-1);
  for(uint32_t i=1;i<count;++i)rec.frames[i-1]=trace[i];
  qr_paint(fb,&rec);
 }
 log_flush();
 log_printf("fatal-audio: status_before=%08lx status_after=%08lx length_stopped=%lu length_done=%lu\n",
  (unsigned long)ai_before,(unsigned long)*(volatile uint32_t *)0xa450000cu,
  (unsigned long)ai_stopped,(unsigned long)*(volatile uint32_t *)0xa4500004u);
 log_printf("fatal-screen: buffer=%08lx vi_origin=%08lx width=%lu frames=%lu\n",
  (unsigned long)(uintptr_t)fb,(unsigned long)*VI_ORIGIN,(unsigned long)*VI_WIDTH,(unsigned long)count);log_flush();
 log_report();
 if(th_present()) { th_screenshot(fb,TR64_WIDTH,TR64_HEIGHT,2);th_exit(1); }
 for(;;)__asm__ volatile("nop");
}
static void fatal_message(unsigned kind,const char *message,uint32_t origin) __attribute__((noreturn));
static void fatal_message(unsigned kind,const char *message,uint32_t origin)
{
 if(reporting)show(NULL);
 disable_interrupts();sys_fatal_capture(&live);
 {
  /* gpr[31] is only the capture call's own return address here: the useful
   * return address is where the failing call came from (origin). The QR
   * message of an assert starts at the expression: file, line and function
   * come from decode.py (PC and RA) and would eat the room. */
  const char *detail=message;
  /* No strstr: it would link newlib's 3 KB version into the resident image. */
  if(kind==TR64_QR_ASSERT)for(const char *p=message;*p;++p)
   if(p[0]=='A' && p[1]=='S' && p[2]=='S' && p[3]=='E' && p[4]=='R' && p[5]=='T' && p[6]==' ') { detail=p+7;break; }
  fatal_t f={kind,message,detail,live.gpr,live.epc,live.sr,live.cr,origin,origin,0,0,0,0};
  show(&f);
 }
}
void sys_fatal_message(const char *message,uint32_t origin)
{ fatal_message(TR64_QR_ERROR,message,origin); }
void sys_fatal_exception(exception_t *ex)
{
 if(reporting)show(NULL);
 char message[256];uint32_t pc=ex->regs->epc;
 tr64_snprintf(message,sizeof(message),"EXCEPTION %u EPC %08lX: %s",(unsigned)ex->code,(unsigned long)pc,ex->info);
 /* Exception frames are8-byte aligned by inthandler.S; gpr is the initial
  * uint64_t array. No incompatible-struct alias or copy of unused FPRs. */
 {
  fatal_t f={TR64_QR_EXCEPTION,message,ex->info,(const uint64_t *)(const void *)ex->regs,
   pc+((ex->regs->cr&0x80000000u)?4u:0u),ex->regs->sr,ex->regs->cr,pc,(uint32_t)ex->regs->gpr[31],pc,1,ex->regs->badvaddr,1};
  show(&f);
 }
}
void exception_default_handler(exception_t *ex) { sys_fatal_exception(ex); }
void __inspector_exception(exception_t *ex) { sys_fatal_exception(ex); }
static void diagnostic(char *dst,size_t capacity,const char *fmt,va_list ap)
{
 if(!fmt) { dst[0]=0;return; }
 if(tr64_debug_vsnprintf(dst,capacity,fmt,ap)<0)
  tr64_snprintf(dst,capacity,"UNSUPPORTED SDK FORMAT: %s",fmt);
}
void __inspector_assertion(const char *expression,const char *fmt,va_list ap)
{
 char detail[160],message[256];diagnostic(detail,sizeof(detail),fmt,ap);
 tr64_snprintf(message,sizeof(message),"ASSERT %s\n%s",expression,detail);
 fatal_message(TR64_QR_ASSERT,message,(uint32_t)(uintptr_t)__builtin_return_address(0));
}
void __wrap_debug_assert_func_f(const char *file,int number,const char *function,
                              const char *expression,const char *fmt,...)
{
 char detail[128],message[256];va_list ap;
 va_start(ap,fmt);diagnostic(detail,sizeof(detail),fmt,ap);va_end(ap);
 tr64_snprintf(message,sizeof(message),"%s:%d %s\nASSERT %s\n%s",file,number,function?function:"",expression,detail);
 fatal_message(TR64_QR_ASSERT,message,(uint32_t)(uintptr_t)__builtin_return_address(0));
}
static void assertion(const char *file,int number,const char *function,const char *expression)
{ __wrap_debug_assert_func_f(file,number,function,expression,NULL); }
void sys_fatal_install(void)
{
 unsigned v;
 __assert_func_ptr=assertion;
 /* Exception-entry stash: our four vector stubs over the SDK's. Flush the
  * vector lines first so a dirty one cannot overwrite the new words. */
 disable_interrupts();
 data_cache_hit_writeback_invalidate((void *)0x80000000u,0x200);
 for(v=0;v<4;++v)memcpy((void *)(0xa0000000u+v*0x80u),&sys_vec_template[v*4],16);
 inst_cache_hit_invalidate((void *)0x80000000u,0x200);
 enable_interrupts();
}
void __rsp_crash(const char *file,int number,const char *function,const char *fmt,...)
{
 char detail[128],message[256];va_list ap;
 va_start(ap,fmt);diagnostic(detail,sizeof(detail),fmt,ap);va_end(ap);
 tr64_snprintf(message,sizeof(message),"RSP FATAL %s:%d %s\n%s",file,number,function?function:"",detail);
 sys_fatal_message(message,(uint32_t)(uintptr_t)__builtin_return_address(0));
}
