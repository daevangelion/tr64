/* SPDX-License-Identifier: MIT */
/* Adapted from MusicReview's real-transport harness; decoder logic is not mocked. */
/* Review harness: real music.c/query.c/stream.c/io.c over cooked SPU tracks.
 * Run from a directory holding pack.bin and dir.bin (see mkpack.py).
 * Usage: harness MODE [dac_rate render_us fake_reads_per_frame big_every frame_us]
 * MODE: loop (T=track env, default 5), cues, latency, edge, idle, storm.
 * Sample matching runs only while step==65536 (dac_rate==format rate). */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/pack.h"
static int hook_read(const tr64_pack_stream_block_t *b,tr64_io_req_t *r,void *buf,uint32_t cap);
#define tr64_pack_stream_read hook_read
#include "engine/snd/music.c"
#undef tr64_pack_stream_read
#include "engine/snd/spu.h"

static uint64_t now_us;static int verbose;static uint32_t last_read[700];static uint32_t rereads,loop_early,loop_late;
static int hook_read(const tr64_pack_stream_block_t *b,tr64_io_req_t *r,void *buf,uint32_t cap)
{ int rc=tr64_pack_stream_read(b,r,buf,cap);uint32_t i=b->logical_offset ? 1+(b->logical_offset-64)/4096 : 0;
  if(!rc && m->phase==PLAY) { if(i==1 && m->play_block>1) { if(m->play_block+2<=m->block_count)++loop_early;else ++loop_late; }
    if(last_read[i] && m->position>0 && i>1 && last_read[i]==m->loops+1)++rereads;
    last_read[i]=m->loops+1; }
  return rc; }
void sys_error(const char *fmt,...)
{ va_list a;va_start(a,fmt);fprintf(stderr,"SYS_ERROR t=%llu: ",(unsigned long long)now_us);
  vfprintf(stderr,fmt,a);fputc('\n',stderr);va_end(a);exit(2); }
void log_printf(const char *fmt,...)
{ va_list a;if(!verbose)return;va_start(a,fmt);vprintf(fmt,a);va_end(a); }
uint32_t sys_profile_ticks(void) { return (uint32_t)now_us; }
uint32_t sys_profile_us(uint32_t e) { return e; }
void sys_io_frame_begin(void) { }
/* PI model: destination poisoned (0xff = invalid SPU predictor) at start,
 * filled at completion; ~5 B/us; each poll costs 1 us. */
static uint8_t *cart;static uint32_t cart_size;
static int dma_active;static uint64_t dma_done_at;static uint8_t *dma_dst;static uint32_t dma_off,dma_n;
int sys_read_start(void *dst,uint32_t off,uint32_t n)
{ if(dma_active)return -1;memset(dst,0xff,n);dma_dst=dst;dma_off=off;dma_n=n;
  dma_active=1;dma_done_at=now_us+n/5+20;return 0; }
int sys_read_poll(void)
{ if(!dma_active)return 0;if(now_us<dma_done_at) { ++now_us;return 0; }
  memcpy(dma_dst,cart+dma_off,dma_n);dma_active=0;return 1; }
static int32_t req_track=-1,req_ambient=-1;static uint32_t req_serial,req_reason;
void game_music_request(tr64_game_music_request_t *o) { memset(o,0,sizeof(*o));o->track=req_track;o->ambient=req_ambient;o->serial=req_serial;o->reason=req_reason; }
static tr64_game_settings_t settings={.audio_music=1};
const tr64_game_settings_t *game_settings_view(void) { return &settings; }
static void request(int32_t t) { req_track=t;req_reason=TR64_GAME_MUSIC_DIRECT;++req_serial; }
static void restore_request(int32_t t) { req_track=t;req_reason=TR64_GAME_MUSIC_RESTORE;++req_serial; }

static int16_t *ref[60];static uint32_t ref_frames[60];
static void build_ref(tr64_pack_t *p)
{
 uint32_t t;
 for(t=2;t<58;++t) {
  tr64_pack_stream_source_t s;char name[]="MUSIC_00";uint32_t i,units,frames,off;int32_t h[2]={0,0};
  const uint8_t *hdr,*e;
  name[6]='0'+t/10;name[7]='0'+t%10;
  if(!tr64_pack_stream_source(p,name,TR64_MUSIC_KIND,&s))continue;
  e=cart+s.table_offset;hdr=cart+tr64_be32(e+8);
  if(tr64_be32(hdr+12))continue;
  frames=tr64_be32(hdr+32);units=(frames+27)/28;
  ref[t]=malloc(units*28*2);ref_frames[t]=frames;off=0;
  for(i=1;i<s.block_count;++i) {
   const uint8_t *b=cart+tr64_be32(e+i*64+8);uint32_t raw=tr64_be32(e+i*64+16),k;
   for(k=0;k<raw;k+=16) { snd_spu_block(ref[t]+off,b+k,h);off+=28; }
  }
  if(off!=units*28) { fprintf(stderr,"ref units %u\n",t);exit(3); }
 }
}

/* Competing FIFO traffic: fake_per_frame 4 KiB reads, optional 96 KiB every big_every frames. */
#define FAKE 13
static tr64_io_req_t fake[FAKE];static uint8_t fake_buf[FAKE][4096] __attribute__((aligned(16)));
static uint32_t fake_per_frame,big_every,frame_no;static tr64_io_req_t big;static uint8_t *big_buf;
static void fake_submit(void)
{
 uint32_t i,n=0;
 for(i=0;i<FAKE && n<fake_per_frame;++i)
  if(fake[i].state!=TR64_IO_QUEUED && fake[i].state!=TR64_IO_ACTIVE) {
   fake[i]=(tr64_io_req_t){fake_buf[i],(uint32_t)(rand()%(cart_size/4096-1))*4096u,4096,0,TR64_IO_IDLE};
   if(tr64_io_submit(fake+i)==0)++n;
  }
 if(big_every && frame_no%big_every==0 && big.state!=TR64_IO_QUEUED && big.state!=TR64_IO_ACTIVE) {
  big=(tr64_io_req_t){big_buf,0,96*1024,0,TR64_IO_IDLE};(void)tr64_io_submit(&big);
 }
}

/* DAC: three 448-frame buffers. */
static uint32_t R=22050;static uint64_t q_end[3];static int q;static uint32_t dac_underruns,started;
static uint64_t mismatches,checked,zero_tail_bad,null_bufs;
static uint32_t seam_underruns,mid_underruns;
static void mix_one(void)
{
 const int16_t *out;uint32_t pos0=m->position,u0=m->underruns,l0=m->loops,i,nvalid=0;
 int32_t cur=m->current;uint32_t ph=m->phase,blk0=m->play_block;
 out=tr64_music_mono_pcm(448,R);
 if(!out) { ++null_bufs;return; }
 if(ph==PLAY && cur>=2 && (m->format.rate==R || (m->format.rate==22050 && R>=22042 && R<=22058))) {
  uint32_t fr=ref_frames[cur];uint64_t adv=(uint64_t)m->position+(uint64_t)(m->loops-l0)*fr-pos0;
  if(m->phase!=PLAY || m->current!=cur)adv=fr-pos0; /* one-shot ended inside buffer */
  nvalid=(uint32_t)adv;if(nvalid>448)nvalid=448;
  for(i=0;i<nvalid;++i) { ++checked;if(out[i]!=ref[cur][(pos0+i)%fr])++mismatches; }
  for(;i<448;++i)if(out[i])++zero_tail_bad;
 }
 if(m->underruns!=u0) {
  if(m->play_block==1 && m->loops!=l0)++seam_underruns;else ++mid_underruns;
  if(verbose>1)printf("underrun t=%llu block=%u->%u loops=%u pending=%u\n",(unsigned long long)now_us,
   blk0,m->play_block,m->loops,tr64_io_pending());
 }
}
static void audio_pump(void)
{
 uint32_t dur=(uint32_t)(448ull*1000000/R);
 while(q && q_end[0]<=now_us) { memmove(q_end,q_end+1,sizeof(q_end[0])*2);--q; }
 if(started && !q)++dac_underruns;
 while(q<3) {
  uint64_t start=q ? q_end[q-1] : now_us;
  mix_one();q_end[q++]=start+dur;started=1;
 }
}
static void snd_pump(void)
{ uint32_t i;for(i=0;i<24;++i)tr64_io_pump();tr64_music_pump();audio_pump(); }
/* play.c shape: io_frame, game tick, snd_tick+pump, draw, then spin pumps until due. */
static uint32_t frame_us=33333,render_us=10000,game_us=3000;
static void frame(void)
{
 uint64_t start=now_us;
 tr64_io_frame();++frame_no;fake_submit();now_us+=game_us;
 tr64_music_tick();snd_pump();
 now_us+=render_us;
 do { tr64_io_pump();snd_pump();now_us+=200; } while(now_us<start+frame_us);
}
static tr64_pack_t pack;static void *arena;
static void level_start(int32_t track,int32_t ambient)
{ tr64_music_catalog_bind(&pack);req_ambient=ambient;request(track);tr64_music_level_start(); }
static void level_stop(void)
{ uint32_t i;tr64_music_level_stop();
  while(tr64_io_pending()) { tr64_io_frame();tr64_io_pump();now_us+=100; }
  for(i=0;i<FAKE;++i)fake[i].state=TR64_IO_IDLE;
  big.state=TR64_IO_IDLE; }
static void report(const char *tag)
{
 printf("%s: loop_early=%u loop_late=%u rereads=%u starts=%u loops=%u unavail=%u underruns=%u (seam=%u mid=%u) reads=%u dac_underruns=%u checked=%llu mismatches=%llu zero_tail_bad=%llu startup_peak_us=%u cur=%d phase=%u\n",
  tag,loop_early,loop_late,rereads,m->starts,m->loops,m->unavailable,m->underruns,seam_underruns,mid_underruns,m->reads,dac_underruns,
  (unsigned long long)checked,(unsigned long long)mismatches,(unsigned long long)zero_tail_bad,
  m->startup_peak_ticks,m->current,m->phase);
}
static void reset_stats(void)
{ m->starts=m->loops=m->unavailable=m->underruns=m->reads=0;m->startup_peak_ticks=0;
  seam_underruns=mid_underruns=dac_underruns=0;checked=mismatches=zero_tail_bad=0; }
int main(int argc,char **argv)
{
 FILE *f;static uint8_t dir[56*48];const char *mode=argc>1?argv[1]:"loop";uint32_t i;
 verbose=getenv("V")?atoi(getenv("V")):0;
 f=fopen("pack.bin","rb");if(!f)return 1;fseek(f,0,SEEK_END);cart_size=ftell(f);rewind(f);
 cart=malloc(cart_size);if(fread(cart,1,cart_size,f)!=cart_size)return 1;fclose(f);
 f=fopen("dir.bin","rb");if(!f || fread(dir,1,sizeof dir,f)!=sizeof dir)return 1;fclose(f);
 big_buf=aligned_alloc(16,96*1024);
 pack.leased=1;pack.directory=dir;pack.levels=56;pack.bytes=cart_size;
 tr64_io_init(cart_size);build_ref(&pack);
 arena=aligned_alloc(16,(tr64_music_memory_bytes()+15)&~15u);tr64_music_init(arena,tr64_music_memory_bytes());
 if(argc>2)R=(uint32_t)atoi(argv[2]);
 if(argc>3)render_us=(uint32_t)atoi(argv[3]);
 if(argc>4)fake_per_frame=(uint32_t)atoi(argv[4]);
 if(argc>5)big_every=(uint32_t)atoi(argv[5]);
 if(argc>6)frame_us=(uint32_t)atoi(argv[6]);
 srand(1);
 if(!strcmp(mode,"loop")) { /* ambient over 2.2 periods */
  int32_t t=getenv("T")?atoi(getenv("T")):5;
  uint32_t period=frame_us>render_us+game_us+200?frame_us:render_us+game_us+200;
  uint32_t frames=(uint32_t)((uint64_t)ref_frames[t]*22/10*1000000/22050/period);
  level_start(t,t);reset_stats();
  for(i=0;i<frames;++i)frame();
  report("loop");level_stop();
 } else if(!strcmp(mode,"cues")) { /* every id 2..59 one-shot, ambient 5 resumes */
  int32_t t;level_start(5,5);
  for(t=2;t<=59;++t) {
   uint32_t n=0;request(t);
   do { frame();++n; } while(!(m->phase==PLAY && m->current==5 && n>3) && n<200000 && !(m->phase==STOPPED && n>3));
   if(verbose)printf("cue %d -> frames %u phase %u cur %d\n",t,n,m->phase,m->current);
  }
  report("cues");level_stop();
 } else if(!strcmp(mode,"latency")) { /* request -> PLAY */
  static const int32_t cues[]={13,2,3,22,23,54,56,5};uint32_t k;
  level_start(5,5);for(i=0;i<200;++i)frame();
  for(k=0;k<sizeof cues/sizeof cues[0];++k) {
   uint64_t t0=now_us;uint32_t n=0;request(cues[k]);
   do { frame();++n; } while(!(m->phase==PLAY && m->current==cues[k]) && n<10000);
   printf("cue %d blocks=%u start_ms=%llu frames=%u\n",cues[k],m->block_count,(unsigned long long)(now_us-t0)/1000,n);
   for(i=0;i<100;++i)frame();
  }
  level_stop();
 } else if(!strcmp(mode,"edge")) { /* cue 13 then ambient; use with --synthetic-13 pack */
  level_start(5,5);for(i=0;i<50;++i)frame();
  request(13);for(i=0;i<400;++i)frame();
  report("edge");level_stop();
 } else if(!strcmp(mode,"edge-cue") || !strcmp(mode,"edge-ambient")) {
  int ambient=!strcmp(mode,"edge-ambient");uint32_t j;
  level_start(13,ambient ? 13 : -1);
  if(ref_frames[13]!=14336)sys_error("edge fixture is not14336 frames");
  for(i=0;i<32;++i) {
   mix_one();now_us+=448ull*1000000/R;tr64_io_frame();
   for(j=0;j<64;++j) { tr64_io_pump();tr64_music_pump(); }
  }
  if(m->position!=m->format.frames)sys_error("edge did not stop exactly at EOF");
  tr64_music_pump();mix_one();tr64_music_pump();
  if(ambient && m->loops!=1)sys_error("edge ambient did not wrap");
  if(!ambient && m->phase!=STOPPED)sys_error("edge cue did not finish");
  report(mode);level_stop();
 } else if(!strcmp(mode,"restore")) {
  uint32_t old;
  level_start(5,5);request(13);for(i=0;i<220;++i)frame();
  if(m->current!=5 || m->phase!=PLAY)sys_error("cue did not resume ambient");
  old=m->starts;restore_request(13);for(i=0;i<8;++i)frame();
  if(m->current!=5 || m->phase!=PLAY || m->starts!=old)
   sys_error("restore replayed a finished cue");
  request(13);for(i=0;i<8;++i)frame();
  if(m->current!=13 || m->phase!=PLAY)sys_error("direct cue did not play");
  restore_request(13);for(i=0;i<8;++i)frame();
  if(m->current!=5 || m->phase!=PLAY || req_track!=13)
   sys_error("restore changed game request or failed ambient");
  report("restore");level_stop();
 } else if(!strcmp(mode,"resample")) {
  int32_t track=atoi(getenv("T"));
  uint32_t pos=0,phase=0,j;
  level_start(track,track);
  uint32_t step=(uint32_t)(((uint64_t)m->format.rate<<16)/R);
  if(step==65536u)sys_error("resample fixture has native output rate");
  for(i=0;i<32;++i) {
   const int16_t *out=tr64_music_mono_pcm(448,R);
   if(!out)sys_error("resample unexpectedly stopped");
   for(j=0;j<448;++j) {
    if(pos>=ref_frames[track])pos-=ref_frames[track];
    ++checked;if(out[j]!=ref[track][pos])++mismatches;
    phase+=step;pos+=phase>>16;phase&=65535u;
   }
   if(m->position!=pos || m->fraction!=phase)sys_error("resampling phase changed");
   now_us+=448ull*1000000/R;tr64_io_frame();
   for(j=0;j<64;++j) { tr64_io_pump();tr64_music_pump(); }
  }
  report("resample");level_stop();
 } else if(!strcmp(mode,"resample-short")) {
  const int32_t track=10;
  level_start(track,track);
  if(ref_frames[track]!=1 || ref[track][0]==ref[track][1])
   sys_error("short resample fixture must expose different padding");
  const int16_t *pcm=tr64_music_mono_pcm(3,16000);
  if(!pcm || m->position!=2 || m->fraction!=8804 || m->loops!=2)
   sys_error("short resample did not overshoot its wrap");
  for(i=0;i<3;++i) { ++checked;if(pcm[i]!=ref[track][0])++mismatches; }
  pcm=tr64_music_mono_pcm(2,48000);
  if(!pcm || m->position!=1 || m->fraction!=3478 || m->loops!=4)
   sys_error("short resample did not wrap before each output");
  for(i=0;i<2;++i) { ++checked;if(pcm[i]!=ref[track][1-i])++mismatches; }
  report("resample-short");level_stop();
 } else if(!strcmp(mode,"idle")) { /* unavailable ambience: reads stay flat */
  uint32_t r0;
  level_start(58,58);r0=m->reads;for(i=0;i<3000;++i)frame();
  printf("idle58: reads delta=%u unavailable=%u phase=%u\n",m->reads-r0,m->unavailable,m->phase);level_stop();
  level_start(57,57);r0=m->reads;for(i=0;i<3000;++i)frame();
  printf("idle57: reads delta=%u unavailable=%u phase=%u\n",m->reads-r0,m->unavailable,m->phase);
  request(13);for(i=0;i<600;++i)frame();
  printf("cue13-in-57: starts=%u unavailable=%u phase=%u cur=%d reads=%u\n",m->starts,m->unavailable,m->phase,m->current,m->reads);
  level_stop();
 } else if(!strcmp(mode,"storm")) { /* random requests + 40 level cycles */
  uint32_t lv;
  static const int32_t amb[]={5,5,5,5,57,58,59,-1,4};
  for(lv=0;lv<40;++lv) {
   int32_t a=amb[lv%9];uint32_t n,len=200+(uint32_t)rand()%1500;
   level_start(a,a);
   for(n=0;n<len;++n) {
    if(rand()%(1+rand()%40)==0)request(-1+rand()%62);
    frame();
   }
   level_stop();
  }
  report("storm");
 } else { fprintf(stderr,"unknown mode\n");return 1; }
 return 0;
}
