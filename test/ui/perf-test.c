/* SPDX-License-Identifier: MIT */
/* The profiler overlay's accounting (engine/ui/perf.c) against a scripted clock and scripted services:
 * every cell of the first published window is compared with a hand-computed string, the RDP rows show a dash
 * while their registers read zero, and the toggle takes effect only at a frame boundary. `--negative`
 * expects a wrong string to fail. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/ui/perf.h"
#include "engine/ui/ui.h"
#include "tr64/code.h"
#include "engine/core/log.h"

static unsigned failures,negative;
#define CHECK(c) do { if(!(c)) { ++failures;if(!negative) { fprintf(stderr,"perf-test line %d: %s\n",__LINE__,#c);exit(1); } } } while(0)
/* One tick is one microsecond; text calls cost 100 of them, as the overlay's own bill. */
static uint32_t now_us;
uint32_t sys_time_us(void) { return now_us; }
uint32_t sys_profile_ticks(void) { return now_us; }
uint32_t sys_profile_us(uint32_t t) { return t; }
void sys_error(const char *f,...) { va_list ap;va_start(ap,f);vfprintf(stderr,f,ap);va_end(ap);abort(); }
static uint32_t audio_us,audio_frames,music_us,calls,loads;
void tr64_audio_perf(uint32_t *a,uint32_t *b) { *a=audio_us;*b=audio_frames; }
uint32_t tr64_music_perf_us(void) { return music_us; }
void code_stats(uint32_t *c,uint32_t *l) { *c=calls;*l=loads; }
static tr64_render_counts_t counts;
const tr64_render_counts_t *tr64_render_scene_counts(void) { return &counts; }
static char last_log[512];
void log_printf(const char *f,...) { va_list ap;va_start(ap,f);vsnprintf(last_log,sizeof(last_log),f,ap);va_end(ap); }
typedef struct { int x,y;unsigned align;char text[32]; } drawn_t;
static drawn_t drawn[512];
static unsigned drawn_count,begins,ends,shades,glyph_batches,batch_glyphs,batch_sorted;
void tr64_ui_backend_begin(void) { ++begins; }
void tr64_ui_backend_end(void) { ++ends; }
void tr64_ui_shade(int x,int y,int w,int h,unsigned s) { (void)x;(void)y;(void)w;(void)h;(void)s;++shades; }
/* The overlay's two ways to draw text: the plain call (before the first window closes) and the layout the
 * window's text is laid out with, drawn afterwards as one sorted glyph batch. Both are captured. */
static unsigned plain_texts;
int tr64_ui_text(int x,int y,const char *t,unsigned align)
{
 (void)x;(void)y;(void)align;
 CHECK(!strcmp(t,"COLLECTING") || !strcmp(t,"SPLIT  COLLECTING"));++plain_texts;
 now_us+=100;return 1;
}
int tr64_ui_text_glyphs(int x,int y,const char *t,unsigned align,tr64_ui_glyph_t *out,unsigned capacity)
{
 unsigned n=0;
 CHECK(drawn_count<512 && strlen(t)<32);
 drawn[drawn_count++]=(drawn_t){x,y,align,{0}};strcpy(drawn[drawn_count-1].text,t);
 for(const char *p=t;*p;++p) {
  if(*p==' ')continue;
  if(n==capacity)return -1;
  out[n++]=(tr64_ui_glyph_t){(int16_t)x,(int16_t)y,(uint8_t)*p};
 }
 return (int)n;
}
tr64_ui_sprite_t tr64_ui_font_sprite(unsigned glyph) { tr64_ui_sprite_t s={0};(void)glyph;return s; }
void tr64_ui_backend_glyphs(const tr64_ui_glyph_t *g,unsigned count,tr64_ui_sprite_t (*font)(unsigned))
{
 CHECK(font==tr64_ui_font_sprite);
 ++glyph_batches;batch_glyphs=count;batch_sorted=1;
 for(unsigned i=1;i<count;++i)if(g[i-1].glyph>g[i].glyph)batch_sorted=0;
 now_us+=100;
}
/* The text drawn at row `row` (baseline 14+14*row) of a column: label, mean, worst. */
static const char *cell(unsigned column,unsigned row,unsigned which)
{
 static const int label_x[2]={4,164},edge[3]={0,96,156};
 for(unsigned i=0;i<drawn_count;++i) {
  const int y=14+14*(int)row;
  if(drawn[i].y!=y)continue;
  if(!which && drawn[i].x==label_x[column] && drawn[i].align==TR64_UI_LEFT)return drawn[i].text;
  if(which && drawn[i].align==TR64_UI_RIGHT && drawn[i].x==label_x[column]+edge[which]-320)return drawn[i].text;
 }
 return "";
}
static int same(const char *a,const char *b) { return !strcmp(a,b); }
#define ROW(col,row,label,mean,worst) do { \
 if(!same(cell(col,row,0),label) || !same(cell(col,row,1),mean) || !same(cell(col,row,2),worst)) { \
  fprintf(stderr,"%s: got '%s' '%s' '%s', want '%s' '%s' '%s'\n",label,cell(col,row,0),cell(col,row,1),cell(col,row,2),label,mean,worst); \
  ++failures;if(!negative)exit(1); } } while(0)
static void frame(unsigned k,int rdp)
{
 const int slow=k==5;
 const uint32_t start=1000000u+k*31250u;
 now_us=start;
 tr64_perf_frame_begin(slow ? 2 : 1);
 CHECK(TR64_PERF_ON());
 tr64_perf_sim_ticks+=slow ? 5000 : 2000;tr64_perf_io_ticks+=slow ? 900 : 300;
 audio_us+=slow ? 3000 : 1500;audio_frames+=slow ? 896 : 448;music_us+=slow ? 1600 : 800;calls+=slow ? 40 : 10;
 if(k==3)++loads;
 memset(&counts,0,sizeof(counts));
 counts.gpu.cpu_submit_us=slow ? 20000 : 14000;counts.gpu.gpu_done_us=counts.gpu.cpu_submit_us+(slow ? 16000 : 10000);
 counts.gpu.pacing_us=slow ? 0 : 1200;
 counts.gpu.triangles=slow ? 1600 : 1000;counts.gpu.texture_uploads=slow ? 160 : 100;
 counts.gpu.texture_upload_bytes=slow ? 81920 : 51200;counts.gpu.palette_loads=slow ? 32 : 20;
 counts.gpu.matrix_loads=slow ? 90 : 70;counts.anim_ranges=4;counts.visible_rooms=slow ? 15 : 12;
 counts.item_instances=slow ? 6 : 5;
 if(rdp==2) {
  counts.split.split=1;counts.split.matrix_peak=slow?49:32;
  counts.split.rdp_record_spills=1;counts.split.rdp_spill_us=150;
  counts.gpu.rdp_record_bytes=32768;
 }
 if(rdp) {
  counts.gpu.rdp_clock=312500;counts.gpu.rdp_pipebusy=62500;counts.gpu.rdp_tmem=slow ? 125000 : 0;
 }
 if(TR64_PERF_ON())tr64_perf_draw();
 now_us=start+(slow ? 60000u : 30000u);
 tr64_perf_end(&(tr64_perf_frame_t){slow ? 2u : 1u,k>=5 ? 3u : 0u,14,"level1"});
}
int main(int argc,char **argv)
{
 negative=argc==2 && !strcmp(argv[1],"--negative");
 /* Off: nothing runs, nothing draws. */
 now_us=1000000u;
 tr64_perf_frame_begin(1);
 CHECK(!TR64_PERF_ON() && tr64_perf_state==0);
 /* A request is applied by the next frame's begin, not before: this frame is not half measured. */
 tr64_perf_toggle();
 CHECK(!TR64_PERF_ON() && tr64_perf_state==2);
 for(unsigned k=0;k<17;++k) {
  frame(k,1);
  /* Until the first window closes the overlay says so, as plain text, and has no layout. */
  CHECK(plain_texts==k+1 && glyph_batches==0);
  if(k<16)CHECK(drawn_count==0);
 }
 /* The 17th frame closed the first window (half a second of the scripted clock): its text is laid out
  * now, once; the next draw is one batch, sorted by glyph. */
 CHECK(drawn_count>40);
 tr64_perf_draw();
 CHECK(begins==18 && ends==18 && shades==18 && plain_texts==17 && glyph_batches==1 && batch_sorted);
 CHECK(batch_glyphs>200 && batch_glyphs<=400);
 ROW(0,0,"MS","AVG","WST");
 ROW(0,1,"FRM","31.8","60.0");
 ROW(0,2,"SIM","2.2","5.0");
 ROW(0,3,"RND","14.3","19.9");
 ROW(0,4,"GPU","10.4","16.0");
 ROW(0,5,"VSY","1.1","1.2");
 ROW(0,6,"SND","1.6","3.0");
 ROW(0,7,"MUS","0.8","1.6");
 ROW(0,8,"IO","0.3","0.9");
 ROW(0,9,"OVL","0.1","0.1");
 ROW(0,10,"CLK","5.0","5.0");
 ROW(0,11,"BSY","-","-");
 ROW(0,12,"PIP","1.0","1.0");
 ROW(0,13,"TMM","0.1","2.0");
 ROW(1,0,"N/F","AVG","WST");
 ROW(1,1,"FPS","32.0","32.0");
 ROW(1,2,"TKS","1.1","2");
 ROW(1,3,"DRP","3","3");
 ROW(1,4,"TRI","1035","1600");
 ROW(1,5,"TEX","104","160");
 ROW(1,6,"KB","51.8","80.0");
 ROW(1,7,"PAL","21","32");
 ROW(1,8,"ANI","4","4");
 ROW(1,9,"MAT","71","90");
 ROW(1,10,"RMS","12","15");
 ROW(1,11,"ITM","5","6");
 ROW(1,12,"UNT","12","40");
 ROW(1,13,"LDS","1","1");
 ROW(1,14,"ABF","1","2");
 CHECK(same(cell(0,15,0),"LEVEL1 ROOM 14"));
 /* The log line carries the same cells. */
 CHECK(!strncmp(last_log,"perf: FRM=31.8/60.0 SIM=2.2/5.0 RND=14.3/19.9 GPU=10.4/16.0 VSY=1.1/1.2 SND=1.6/3.0 MUS=0.8/1.6 IO=0.3/0.9 OVL=0.1/0.1 CLK=5.0/5.0 BSY=-/- PIP=1.0/1.0 TMM=0.1/2.0 FPS=32.0/32.0 TKS=1.1/2 DRP=3/3 TRI=1035/1600 ",100));
 CHECK(strstr(last_log," ABF=1/2 frames=17 glyphs="));
 /* Toggled off: the next frame's begin switches it off and the overlay stays out of the picture. */
 tr64_perf_toggle();
 now_us=2000000u;tr64_perf_frame_begin(1);
 CHECK(!TR64_PERF_ON() && tr64_perf_state==0);
 if(negative) {
  /* Control: the checks above must be able to fail. */
  failures=0;
  ROW(0,1,"FRM","99.9","60.0");
  if(!failures) { fprintf(stderr,"negative control did not fail\n");return 1; }
  puts("perf-test negative PASS: a wrong cell is rejected");
  return 0;
 }
 tr64_perf_toggle();
 drawn_count=0;
 for(unsigned k=0;k<18;++k)frame(k,2);
 ROW(1,9,"MSL","33","49");
 CHECK(same(cell(0,15,0),"LEVEL1 ROOM 14"));
 puts("perf-test PASS: window statistics, units, dashes, footer, toggle at the frame boundary");
 return 0;
}
