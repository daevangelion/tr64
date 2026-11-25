/* SPDX-License-Identifier: MIT */
/* On-cart profiler overlay: docs/perf-overlay.md. */
#include <string.h>
#include "engine/ui/perf.h"
#include "engine/ui/ui.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/code.h"
#include "tr64/render.h"
/* Accessors of the services measured. Weak: a build without the module simply shows zero for it. */
extern void tr64_audio_perf(uint32_t *mix_us,uint32_t *output_frames) __attribute__((weak));
extern uint32_t tr64_music_perf_us(void) __attribute__((weak));

#ifdef __GNUC__
#define PERF_COMPACT __attribute__((cold))
#define PERF_NOINLINE __attribute__((noinline,cold))
#else
#define PERF_COMPACT
#define PERF_NOINLINE
#endif

uint8_t tr64_perf_state;
uint32_t tr64_perf_sim_ticks,tr64_perf_io_ticks,tr64_perf_snd_ticks;

/* Statistics kept per drawn frame (sum and worst over a half second), in the unit they are taken in:
 * CPU Count ticks, microseconds, RDP clock cycles (62.5 MHz), or plain counts. */
enum {
 S_FRAME,S_PERIOD,S_SIM,S_IO,                              /* Count ticks */
 S_RENDER,S_GPU,S_VSYNC,S_SND,S_MUS,S_OVL,S_SPILL_WAIT,    /* microseconds */
 S_CLK,S_BSY,S_PIP,S_TMM,                                  /* RDP clock cycles */
 S_TICKS,S_TRI,S_TEX,S_TXB,S_PAL,S_ANI,S_MAT,S_ROOMS,S_ITEMS,S_UNITS,S_ABUF, /* counts */
 S_TRI_TOP,S_TRI_BOTTOM,S_TEX_TOP,S_TEX_BOTTOM,
 S_RECORD,S_SPILLS,S_MATRIX_PEAK,
 NSTAT,
 FIRST_US=S_RENDER,FIRST_RDP=S_CLK,FIRST_COUNT=S_TICKS
};
typedef struct { uint32_t sum,max; } stat_t;
typedef struct { stat_t s[NSTAT];uint32_t frames,dropped,loads,start_us; } bucket_t;
enum { COLUMNS=2,ROWS=16,FRAME_WINDOW_US=500000,ROW_PITCH=14,TOP=14,GLYPH_CAP=400,MEAN_EDGE=96,WORST_EDGE=156,ABUF_FRAMES=448 };
static struct {
 bucket_t previous,current;
 uint32_t frame_start,previous_start;
 uint32_t audio_us,music_us,audio_frames,calls,loads,dropped;
 uint32_t overlay_ticks;
 const char *level;
 uint32_t room;
 /* The overlay as laid out when its window closed: glyph rectangles sorted by glyph. */
 tr64_ui_glyph_t glyphs[GLYPH_CAP];
 uint16_t glyph_count;
 uint8_t have_previous,published,split;
} perf;

enum {
 M_NORM=1,M_SPLIT=2,M_BOTH=3,
 K_HEAD=0,K_TIME,K_COUNT,K_DEC_INT,K_KB_RND,K_KB_TRN,K_PAIR,K_FPS,K_DRP,K_LDS
};
typedef struct { char label[5];uint8_t col_row;uint8_t stat;uint8_t kind_mode; } row_spec_t;
#define R(col,row,lbl,st,k,m) { lbl,(uint8_t)(((col)<<4)|(row)),(uint8_t)(st),(uint8_t)(((m)<<4)|(k)) }
static const row_spec_t row_specs[]={
 R(0,0,"MS",0,K_HEAD,M_BOTH),
 R(0,1,"FRM",S_FRAME,K_TIME,M_BOTH),
 R(0,2,"SIM",S_SIM,K_TIME,M_BOTH),
 R(0,3,"RND",S_RENDER,K_TIME,M_BOTH),
 R(0,4,"GPU",S_GPU,K_TIME,M_BOTH),
 R(0,5,"VSY",S_VSYNC,K_TIME,M_BOTH),
 R(0,6,"SND",S_SND,K_TIME,M_BOTH),
 R(0,7,"MUS",S_MUS,K_TIME,M_BOTH),
 R(0,8,"IO",S_IO,K_TIME,M_BOTH),
 R(0,9,"OVL",S_OVL,K_TIME,M_BOTH),
 R(0,10,"CLK",S_CLK,K_TIME,M_BOTH),
 R(0,11,"BSY",S_BSY,K_TIME,M_BOTH),
 R(0,12,"PIP",S_PIP,K_TIME,M_BOTH),
 R(0,13,"TMM",S_TMM,K_TIME,M_BOTH),
 R(0,14,"VTRI",S_TRI_TOP,K_PAIR,M_SPLIT),
 R(1,0,"N/F",0,K_HEAD,M_NORM),
 R(1,0,"DUAL",0,K_HEAD,M_SPLIT),
 R(1,1,"FPS",S_PERIOD,K_FPS,M_BOTH),
 R(1,2,"TKS",S_TICKS,K_DEC_INT,M_BOTH),
 R(1,3,"DRP",0,K_DRP,M_BOTH),
 R(1,4,"TRI",S_TRI,K_COUNT,M_BOTH),
 R(1,5,"TEX",S_TEX,K_COUNT,M_BOTH),
 R(1,6,"KB",S_TXB,K_KB_RND,M_BOTH),
 R(1,7,"PAL",S_PAL,K_COUNT,M_BOTH),
 R(1,8,"ANI",S_ANI,K_COUNT,M_NORM),
 R(1,8,"RKB",S_RECORD,K_KB_TRN,M_SPLIT),
 R(1,9,"MAT",S_MAT,K_COUNT,M_NORM),
 R(1,9,"MSL",S_MATRIX_PEAK,K_COUNT,M_SPLIT),
 R(1,10,"RMS",S_ROOMS,K_COUNT,M_BOTH),
 R(1,11,"ITM",S_ITEMS,K_COUNT,M_NORM),
 R(1,11,"SPL",S_SPILLS,K_DEC_INT,M_SPLIT),
 R(1,12,"UNT",S_UNITS,K_COUNT,M_NORM),
 R(1,12,"SWT",S_SPILL_WAIT,K_TIME,M_SPLIT),
 R(1,13,"LDS",0,K_LDS,M_BOTH),
 R(1,14,"ABF",S_ABUF,K_COUNT,M_BOTH),
 R(1,15,"VTEX",S_TEX_TOP,K_PAIR,M_SPLIT)
};
#undef R

static PERF_NOINLINE void collect_baselines(void)
{
 uint32_t mix=0,frames=0;
 if(tr64_audio_perf)tr64_audio_perf(&mix,&frames);
 perf.audio_us=mix;perf.audio_frames=frames;
 perf.music_us=tr64_music_perf_us ? tr64_music_perf_us() : 0u;
 code_stats(&perf.calls,&perf.loads);
}
static void begin_window(uint32_t now_us)
{
 memset(&perf.current,0,sizeof(perf.current));perf.current.start_us=now_us;
}
static PERF_NOINLINE void start(void)
{
 memset(&perf,0,sizeof(perf));
 begin_window(sys_time_us());
}
void PERF_COMPACT tr64_perf_begin(uint32_t due)
{
 (void)due;
 const uint32_t want=(tr64_perf_state>>1)&1u;
 if(want!=(tr64_perf_state&1u)) {
  if(!want) { tr64_perf_state=0;return; }
  start();tr64_perf_state=3;
 }
 /* The frame's own services: what the mixer, music, I/O and unit dispatch do from here to the end of the
  * frame. The idle wait between frames (the loop spins on I/O and audio) is not a frame's cost. */
 collect_baselines();tr64_perf_io_ticks=tr64_perf_snd_ticks=0;
 perf.frame_start=sys_profile_ticks();tr64_perf_sim_ticks=0;
}
static PERF_NOINLINE void add(unsigned stat,uint32_t value)
{
 stat_t *s=&perf.current.s[stat];
 s->sum+=value;if(value>s->max)s->max=value;
}
/* ---- text and layout ---- */
static PERF_NOINLINE char *put_num(char *p,uint32_t v,int fixed)
{
 char t[10];unsigned n=0;
 if(fixed) { if(v>9999u)v=9999u; }
 else if(v>99999u)v=99999u;
 do { t[n++]=(char)('0'+v%10u);v/=10u; } while(v);
 if(fixed) {
  if(n==1)t[n++]='0';
  while(n>1)*p++=t[--n];
  *p++='.';
 }
 while(n)*p++=t[--n];
 *p=0;
 return p;
}
static PERF_NOINLINE uint32_t to_us(unsigned stat,uint32_t v)
{
 if(stat<FIRST_US)return sys_profile_us(v);
 if(stat<FIRST_RDP)return v;
 return (uint32_t)((uint64_t)v*16u/1000u);
}
static PERF_NOINLINE void lay_out(int x,int y,const char *text,unsigned align)
{
 const int n=tr64_ui_text_glyphs(x,y,text,align,perf.glyphs+perf.glyph_count,GLYPH_CAP-perf.glyph_count);
 if(n<0)sys_error("perf overlay text invalid or over %u glyphs",(unsigned)GLYPH_CAP);
 perf.glyph_count=(uint16_t)(perf.glyph_count+(unsigned)n);
}
static PERF_NOINLINE char *append_str(char *p,const char *s)
{
 while(*s)*p++=*s++;
 return p;
}
static PERF_NOINLINE void emit_row(unsigned column,unsigned row,const char *label,
                                   const char *mean,const char *worst,char **lp)
{
 const int lx=column ? 164 : 4,y=TOP+(int)row*ROW_PITCH;
 lay_out(lx,y,label,TR64_UI_LEFT);
 lay_out(lx+MEAN_EDGE-320,y,mean,TR64_UI_RIGHT);
 lay_out(lx+WORST_EDGE-320,y,worst,TR64_UI_RIGHT);
 if(row) {
  char *p=*lp;
  *p++=' ';p=append_str(p,label);
  *p++='=';p=append_str(p,mean);
  *p++='/';p=append_str(p,worst);
  *lp=p;
 }
}
/* Shell sort by glyph, so that the backend loads each distinct glyph once. */
static PERF_NOINLINE void sort_glyphs(void)
{
 unsigned gap,i,j;
 for(gap=perf.glyph_count/2;gap;gap/=2)
  for(i=gap;i<perf.glyph_count;++i) {
   const tr64_ui_glyph_t g=perf.glyphs[i];
   for(j=i;j>=gap && perf.glyphs[j-gap].glyph>g.glyph;j-=gap)perf.glyphs[j]=perf.glyphs[j-gap];
   perf.glyphs[j]=g;
  }
}
static PERF_NOINLINE void publish(uint32_t now_us)
{
 const bucket_t *a=&perf.previous,*b=&perf.current;
 const uint32_t frames=a->frames+b->frames;
 if(frames) {
  const uint32_t window_start=a->frames ? a->start_us : b->start_us;
  uint32_t elapsed=now_us-window_start;
  char line[512],*lp=line;
  unsigned i;
  if(!elapsed)elapsed=1;
  perf.glyph_count=0;
  lp=append_str(lp,"perf:");
  for(i=0;i<sizeof(row_specs)/sizeof(row_specs[0]);++i) {
   const row_spec_t *sp=&row_specs[i];
   const unsigned mode=sp->kind_mode>>4,kind=sp->kind_mode&15u;
   const unsigned col=sp->col_row>>4,row=sp->col_row&15u,st=sp->stat;
   char mean[8],worst[8];
   const char *ms=mean,*ws=worst;
   uint32_t sum=0,mx=0,mv=0,wv=0;
   uint8_t mf=0,wf=0,raw=0;
   if(!(mode&(1u<<perf.split)))continue;
   if(st<NSTAT) {
    sum=a->s[st].sum+b->s[st].sum;
    mx=a->s[st].max>b->s[st].max ? a->s[st].max : b->s[st].max;
   }
   switch(kind) {
   case K_HEAD: ms="AVG";ws="WST";raw=1;break;
   case K_TIME:
    if(st>=FIRST_RDP && !sum) { ms="-";ws="-";raw=1; }
    else { mv=(to_us(st,sum/frames)+50u)/100u;wv=(to_us(st,mx)+50u)/100u;mf=wf=1; }
    break;
   case K_COUNT: mv=(sum+frames/2u)/frames;wv=mx;break;
   case K_DEC_INT: mv=(sum*10u+frames/2u)/frames;wv=mx;mf=1;break;
   case K_KB_RND: mv=(((sum+frames/2u)/frames)*10u+512u)>>10;wv=(mx*10u+512u)>>10;mf=wf=1;break;
   case K_KB_TRN: mv=((sum/frames)*10u+512u)>>10;wv=(mx*10u+512u)>>10;mf=wf=1;break;
   case K_PAIR:
    mv=(sum+frames/2u)/frames;
    wv=(a->s[st+1].sum+b->s[st+1].sum+frames/2u)/frames;
    break;
   case K_FPS: {
    const uint32_t slow=to_us(S_PERIOD,mx);
    mv=(uint32_t)((uint64_t)frames*10000000ull/elapsed);
    wv=slow ? 10000000u/slow : 0u;mf=wf=1;
    break;
   }
   case K_DRP: mv=a->dropped+b->dropped;wv=perf.dropped;break;
   case K_LDS: mv=a->loads+b->loads;wv=perf.loads;break;
   }
   if(!raw) { put_num(mean,mv,mf);put_num(worst,wv,wf); }
   emit_row(col,row,sp->label,ms,ws,&lp);
  }
  /* The footer: level and camera room, as of this window. */
  {
   char footer[32],*p=footer;const char *l=perf.level;unsigned n=0;
   while(l && *l && n<10) { char ch=*l++;*p++=(char)(ch>='a' && ch<='z' ? ch-32 : ch);++n; }
   p=append_str(p," ROOM ");put_num(p,perf.room,0);
   lay_out(4,TOP+(ROWS-1)*ROW_PITCH,footer,TR64_UI_LEFT);
  }
  sort_glyphs();perf.published=1;
  *lp=0;log_printf("%s frames=%lu glyphs=%u\n",line,(unsigned long)frames,(unsigned)perf.glyph_count);
 }
 perf.previous=perf.current;begin_window(now_us);
}
void PERF_COMPACT tr64_perf_end(const tr64_perf_frame_t *f)
{
 const tr64_render_counts_t *c=tr64_render_scene_counts();
 const unsigned split=c && c->split.split;
 /* Don't combine frames from different levels or view layouts in one window. */
 if((perf.level && f->level!=perf.level) || split!=perf.split) {
  const uint32_t frame_start=perf.frame_start,audio_us=perf.audio_us,audio_frames=perf.audio_frames;
  const uint32_t music_us=perf.music_us,calls=perf.calls,loads=perf.loads;
  const uint32_t overlay_ticks=perf.overlay_ticks;
  start();
  perf.frame_start=frame_start;perf.audio_us=audio_us;perf.audio_frames=audio_frames;
  perf.music_us=music_us;perf.calls=calls;perf.loads=loads;
  perf.overlay_ticks=overlay_ticks;perf.split=(uint8_t)split;
 }
 const uint32_t now=sys_profile_ticks(),frame=now-perf.frame_start;
 uint32_t mix=0,frames=0,calls,loads,overlay_us,cpu=0,gpu=0,wait=0;
 if(tr64_audio_perf)tr64_audio_perf(&mix,&frames);
 code_stats(&calls,&loads);
 overlay_us=sys_profile_us(perf.overlay_ticks);perf.overlay_ticks=0;
 if(c) {
  cpu=c->gpu.cpu_submit_us;gpu=c->gpu.gpu_done_us>cpu ? c->gpu.gpu_done_us-cpu : 0u;
  wait=c->gpu.pacing_us;
  uint32_t excluded=overlay_us+c->split.rdp_spill_us;
  cpu=cpu>excluded ? cpu-excluded : 0u;
 }
 add(S_FRAME,frame);add(S_PERIOD,perf.have_previous ? perf.frame_start-perf.previous_start : frame);
 add(S_SIM,tr64_perf_sim_ticks);add(S_IO,tr64_perf_io_ticks);add(S_OVL,overlay_us);
 add(S_RENDER,cpu);add(S_GPU,gpu);add(S_VSYNC,wait);
 add(S_SND,mix-perf.audio_us+sys_profile_us(tr64_perf_snd_ticks)); /* the mixer pumps and the per-tick sound logic */
 add(S_MUS,(tr64_music_perf_us ? tr64_music_perf_us() : 0u)-perf.music_us);
 if(c) {
  static const struct { uint8_t stat;uint8_t off; } cmap[]={
   {S_CLK,(uint8_t)offsetof(tr64_render_counts_t,gpu.rdp_clock)},
   {S_BSY,(uint8_t)offsetof(tr64_render_counts_t,gpu.rdp_bufbusy)},
   {S_PIP,(uint8_t)offsetof(tr64_render_counts_t,gpu.rdp_pipebusy)},
   {S_TMM,(uint8_t)offsetof(tr64_render_counts_t,gpu.rdp_tmem)},
   {S_TRI,(uint8_t)offsetof(tr64_render_counts_t,gpu.triangles)},
   {S_TEX,(uint8_t)offsetof(tr64_render_counts_t,gpu.texture_uploads)},
   {S_TXB,(uint8_t)offsetof(tr64_render_counts_t,gpu.texture_upload_bytes)},
   {S_PAL,(uint8_t)offsetof(tr64_render_counts_t,gpu.palette_loads)},
   {S_ANI,(uint8_t)offsetof(tr64_render_counts_t,anim_ranges)},
   {S_MAT,(uint8_t)offsetof(tr64_render_counts_t,gpu.matrix_loads)},
   {S_ROOMS,(uint8_t)offsetof(tr64_render_counts_t,visible_rooms)},
   {S_ITEMS,(uint8_t)offsetof(tr64_render_counts_t,item_instances)},
   {S_TRI_TOP,(uint8_t)offsetof(tr64_render_counts_t,split.first_triangles)},
   {S_TRI_BOTTOM,(uint8_t)offsetof(tr64_render_counts_t,split.second_triangles)},
   {S_TEX_TOP,(uint8_t)offsetof(tr64_render_counts_t,split.first_texture_uploads)},
   {S_TEX_BOTTOM,(uint8_t)offsetof(tr64_render_counts_t,split.second_texture_uploads)},
   {S_RECORD,(uint8_t)offsetof(tr64_render_counts_t,gpu.rdp_record_bytes)},
   {S_SPILLS,(uint8_t)offsetof(tr64_render_counts_t,split.rdp_record_spills)},
   {S_SPILL_WAIT,(uint8_t)offsetof(tr64_render_counts_t,split.rdp_spill_us)}
  };
  typedef char cmap_fits_u8[sizeof(tr64_render_counts_t)<=256 ? 1 : -1];
  (void)sizeof(cmap_fits_u8);
  const unsigned n=split ? (unsigned)(sizeof(cmap)/sizeof(cmap[0])) : 12u;
  for(unsigned i=0;i<n;++i) {
   uint32_t v;memcpy(&v,(const uint8_t *)c+cmap[i].off,sizeof(v));
   add(cmap[i].stat,v);
  }
  if(split)add(S_MATRIX_PEAK,c->split.matrix_peak);
 }
 add(S_TICKS,f->ticks);add(S_UNITS,calls-perf.calls);add(S_ABUF,(frames-perf.audio_frames)/ABUF_FRAMES);
 perf.current.frames++;perf.current.loads+=loads-perf.loads;
 if(perf.have_previous && f->dropped>perf.dropped)perf.current.dropped+=f->dropped-perf.dropped;
 perf.dropped=f->dropped;
 perf.previous_start=perf.frame_start;perf.have_previous=1;perf.level=f->level;perf.room=f->room;
 {
  const uint32_t now_us=sys_time_us();
  if((uint32_t)(now_us-perf.current.start_us)>=FRAME_WINDOW_US)publish(now_us);
 }
}
void PERF_COMPACT tr64_perf_draw(void)
{
 const uint32_t start=sys_profile_ticks();
 tr64_ui_backend_begin();
 tr64_ui_shade(0,0,320,TOP+ROWS*ROW_PITCH-6,27);
 if(!perf.published)(void)tr64_ui_text(4,TOP,perf.split?"SPLIT  COLLECTING":"COLLECTING",TR64_UI_LEFT);
 else tr64_ui_backend_glyphs(perf.glyphs,perf.glyph_count,tr64_ui_font_sprite);
 tr64_ui_backend_end();
 perf.overlay_ticks=sys_profile_ticks()-start;
}
