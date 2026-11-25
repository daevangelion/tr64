/* SPDX-License-Identifier: MIT */
/* Original C engine. TR1 mode and gain facts: OpenLara2333adef
 * src/fixed/item.h36-119 and src/level.h843-847. No game RNG here. */
#include <string.h>
#include "engine/snd/snd.h"
#include "engine/snd/format.h"
#include "engine/snd/spu.h"
#include "engine/snd/spatial.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/io.h"
#define INVALID UINT32_MAX
#define ALIGN16(n) (((n)+15u)&~15u)
#define AMBIENT_SOURCES TR64_LIMIT_sound_emitters_1
#if TR64_HAS_TR2 || TR64_GAME == 3
#define TR64_SND_SEQUEL 1
#else
#define TR64_SND_SEQUEL 0
#endif
typedef struct {
 tr64_io_req_t request;
 uint8_t *data;
 uint32_t block;
 uint8_t valid,pending;
} cache_t;
typedef struct {
 cache_t cache[2];
 uint32_t sample,offset,frames,rate,encoding,pos,fraction,step,mode;
 uint32_t data_start,data_end,bytes,decoded_start;
 int32_t history[2];
 int16_t decoded[28];
 const uint8_t *data;
 uint16_t left,right,base_gain,source;
 int32_t position[3];
 uint8_t active,started,refreshed,positioned;
#if TR64_SND_SEQUEL
 uint16_t sound_id; /* OpenLara src/level.h:843-847 unique key is the sound id. */
 uint32_t born;
#endif
} voice_t;
typedef struct {
 uint8_t *metadata,*resident;
 tr64_pack_stream_block_t *index;
 const tr64_pack_stream_block_t *blocks;
 uint32_t metadata_bytes,block_count,total_bytes,resident_bytes,resident_capacity;
 uint32_t resident_offset[TR64_LIMIT_sound_samples_1];
 voice_t voices[TR64_SND_CHANNELS];
 const tr64_level_t *level;
 uint32_t source_count,ambient_started,ambient_stopped;
 uint16_t source_gain[AMBIENT_SOURCES][2];
 uint8_t source_heard[AMBIENT_SOURCES];
 uint32_t events,played,dropped,unavailable,underruns,clipped,mix_frames,ticks;
#if TR64_SND_SEQUEL
 uint32_t stolen,voice_seq;
#endif
 uint32_t stream_reads,stream_bytes,mix_peak;
 uint64_t mix_us __attribute__((aligned(8)));
 int bound,active;
} sound_t;
static sound_t *s;
#if TR64_SND_SEQUEL
/* Boot slot address. A sequel bank borrows the idle cache; restore this
 * before the next first-game metadata hand-out. */
static uint8_t *boot_metadata;
#endif
/* Explicit scene-workload probes; absent from ordinary shipping links. */
extern const uint32_t tr64_snd_test_disable __attribute__((weak));
extern const uint32_t tr64_snd_test_voices __attribute__((weak));
extern void tr64_snd_test_event(const tr64_game_sound_event_t *) __attribute__((weak));
extern void tr64_snd_test_report(void) __attribute__((weak));
static uint32_t bench_voices;
/* Optional resident music transport shares this output, never another AI. */
extern uint32_t tr64_music_memory_bytes(void) __attribute__((weak));
extern void tr64_music_init(void *,uint32_t) __attribute__((weak));
extern void tr64_music_level_start(void) __attribute__((weak));
extern void tr64_music_level_stop(void) __attribute__((weak));
extern void tr64_music_tick(void) __attribute__((weak));
extern void tr64_music_pump(void) __attribute__((weak));
extern void tr64_music_report(void) __attribute__((weak));
extern const int16_t *tr64_music_mono_pcm(uint32_t,uint32_t) __attribute__((weak));
/* Film sound (engine/media/film_audio.c): shares the music bus; only a video plays it. */
extern const int16_t *tr64_film_mono_pcm(uint32_t,uint32_t) __attribute__((weak));
/* The profiler overlay's switch and I/O account (engine/ui/perf.c); absent from builds without it. */
extern uint8_t tr64_perf_state __attribute__((weak));
extern uint32_t tr64_perf_io_ticks __attribute__((weak));
static int muted;
static void *take(uint8_t **cursor,uint8_t *end,uint32_t bytes)
{
 void *p=*cursor;bytes=ALIGN16(bytes);
 if(bytes>(uint32_t)(end-*cursor))sys_error("audio arena overflow");
 *cursor+=bytes;return p;
}
void tr64_snd_boot(void)
{
 uint8_t *arena,*p,*end;uint32_t i,j,music_bytes=0;
 if(s)return;
 muted=&tr64_snd_test_disable && tr64_snd_test_disable;
 bench_voices=&tr64_snd_test_voices ? tr64_snd_test_voices : 0;
 if(bench_voices>TR64_SND_CHANNELS)sys_error("audio benchmark voice count");
 if(!!tr64_music_memory_bytes!=!!tr64_music_init)sys_error("partial music boot interface");
 if(tr64_music_memory_bytes)music_bytes=tr64_music_memory_bytes();
 if(music_bytes>UINT32_MAX-15u-TR64_SND_ARENA_BYTES)sys_error("music boot size overflow");
 arena=tr64_audio_boot(TR64_SND_ARENA_BYTES+ALIGN16(music_bytes));p=arena;end=p+TR64_SND_ARENA_BYTES;
 s=take(&p,end,sizeof(*s));memset(s,0,sizeof(*s));
 s->metadata=take(&p,end,TR64_SND_METADATA_BYTES);
#if TR64_SND_SEQUEL
 boot_metadata=s->metadata;
#endif
 s->index=take(&p,end,TR64_SND_INDEX_BLOCKS*sizeof(*s->index));
 for(i=0;i<TR64_SND_CHANNELS;++i)for(j=0;j<2;++j) {
  s->voices[i].cache[j].data=take(&p,end,TR64_SND_BLOCK_BYTES);
  s->voices[i].cache[j].block=INVALID;
 }
 s->resident=p;s->resident_capacity=(uint32_t)(end-p);
 for(i=0;i<TR64_LIMIT_sound_samples_1;++i)s->resident_offset[i]=INVALID;
 if(tr64_music_init)tr64_music_init(arena+TR64_SND_ARENA_BYTES,music_bytes);
}
void *tr64_snd_metadata_buffer(const tr64_level_t *level,uint32_t *capacity)
{
 if(!s)sys_error("sound loader before audio boot");
#if TR64_SND_SEQUEL
 if(boot_metadata && !(level && level->game>=2))s->metadata=boot_metadata;
#endif
 /* A render-only level never preloads or plays effects. Borrow that idle
  * cache, preserving the TR1 allocation, capacity and address layout. */
#if TR64_SND_SEQUEL
 if(level && level->game>=2) {
#else
 if(level && level->game==2) {
#endif
  if(s->active || s->bound || s->resident_capacity<TR64_SND_TR2_METADATA_BYTES)
   sys_error("TR2 sound metadata: effect cache unavailable");
  *capacity=TR64_SND_TR2_METADATA_BYTES;return s->resident;
 }
 *capacity=TR64_SND_METADATA_BYTES;return s->metadata;
}
tr64_pack_stream_block_t *tr64_snd_index_buffer(uint32_t *capacity)
{
 if(!s)sys_error("sound index before audio boot");
 *capacity=TR64_SND_INDEX_BLOCKS;return s->index;
}
static uint32_t find_block(uint32_t offset)
{
 uint32_t lo=0,hi=s->block_count;
 while(lo<hi) {
  uint32_t mid=lo+(hi-lo)/2;const tr64_pack_stream_block_t *b=s->blocks+mid;
  if(offset<b->logical_offset)hi=mid;
  else if(offset-b->logical_offset>=b->raw_bytes)lo=mid+1;
  else return mid;
 }
 sys_error("audio stream address outside index offset=%lu",(unsigned long)offset);
}
static void poll_cache(cache_t *c)
{
 if(c->pending && tr64_pack_stream_poll(s->blocks+c->block,&c->request)) {
  c->pending=0;c->valid=1;
 }
}
static void request(cache_t *c,uint32_t block)
{
 int result;
 if(c->pending || (c->valid && c->block==block))return;
 c->request=(tr64_io_req_t){0};
 result=tr64_pack_stream_read(s->blocks+block,&c->request,c->data,TR64_SND_BLOCK_BYTES);
 if(result==TR64_IO_SUBMIT_BUSY)return;
 if(result)sys_error("audio stream submit invalid");
 c->block=block;c->valid=0;c->pending=1;
 ++s->stream_reads;s->stream_bytes+=s->blocks[block].raw_bytes;
}
static void preload(void)
{
 uint32_t i,count=snd_be32(s->metadata+16);
 for(i=0;i<count;++i) {
  const uint8_t *d=snd_sample(s->metadata,i);uint32_t offset=snd_be32(d),size=ALIGN16(snd_be32(d+20)),done=0;
  s->resident_offset[i]=INVALID;
  if(!snd_be32(d+24) || size>s->resident_capacity-s->resident_bytes)continue;
#if TR64_SND_SEQUEL
 if(s->metadata==s->resident)sys_error("TR2 sound: frequent sample would overwrite metadata");
#endif
  s->resident_offset[i]=s->resident_bytes;
  while(done<size) {
   uint32_t n=find_block(offset+done);const tr64_pack_stream_block_t *b=s->blocks+n;
   tr64_io_req_t r={0};
   if(b->logical_offset!=offset+done || b->raw_bytes>size-done)sys_error("audio sample block boundary");
   tr64_io_frame();
   if(tr64_pack_stream_read(b,&r,s->resident+s->resident_bytes+done,size-done))sys_error("audio resident submit");
   while(!tr64_pack_stream_poll(b,&r))tr64_io_pump();
   done+=b->raw_bytes;
  }
  s->resident_bytes+=size;
 }
}
void tr64_snd_bind(const void *metadata,uint32_t metadata_bytes,
                   const tr64_pack_stream_block_t *blocks,uint32_t block_count,uint32_t total)
{
#if TR64_SND_SEQUEL
 if(metadata==s->resident) {
  if(!s || s->active || blocks!=s->index ||
     !block_count || block_count>TR64_SND_INDEX_BLOCKS ||
     metadata_bytes<64 || metadata_bytes>TR64_SND_TR2_METADATA_BYTES ||
     metadata_bytes>s->resident_capacity || total<metadata_bytes)
   sys_error("audio bind metadata/index");
  /* Playback reads s->metadata. Frequent flags stay clear, so preload
   * does not write the borrowed cache that holds this prefix. */
  s->metadata=(uint8_t *)metadata;
  s->metadata_bytes=metadata_bytes;s->blocks=blocks;s->block_count=block_count;s->total_bytes=total;
  s->resident_bytes=0;preload();s->bound=1;
  return;
 }
#endif
 if(!s || s->active || metadata!=s->metadata || blocks!=s->index ||
    !block_count || block_count>TR64_SND_INDEX_BLOCKS ||
    metadata_bytes<64 || metadata_bytes>TR64_SND_METADATA_BYTES || total<metadata_bytes)
  sys_error("audio bind metadata/index");
 s->metadata_bytes=metadata_bytes;s->blocks=blocks;s->block_count=block_count;s->total_bytes=total;
 s->resident_bytes=0;preload();s->bound=1;
}
void tr64_snd_level_start(const tr64_level_t *level)
{
#if !TR64_SND_SEQUEL
 if(level && level->game==2)sys_error("Tomb Raider II sound playback is unsupported");
#endif
 if(!s || !level || !level->ready || !s->bound)sys_error("audio level start before sound readiness");
 uint32_t i;
 s->level=level;s->source_count=tr64_level_table(level,TR64_CORE_sound_sources)->count;
 if(s->source_count>AMBIENT_SOURCES)sys_error("audio ambient source capacity");
 memset(s->source_heard,0,sizeof(s->source_heard));
 memset(s->source_gain,0,sizeof(s->source_gain));s->ambient_started=s->ambient_stopped=0;
 for(i=0;i<s->source_count;++i) {
  uint32_t id=tr64_u16(tr64_level_sound_sources(level,i)->sound);
  int32_t index=tr64_i16(*tr64_level_sound_map(level,id));
  if(index>=0) {
   const tr64_core_sound_details_t *detail=tr64_level_sound_details(level,(uint32_t)index);
   uint32_t flags=tr64_u16(detail->flags);
#if TR64_SND_SEQUEL
   /* All measured TR1 emitters select one sample, chance0,no gain/pitch RNG. */
   if(level->game<3 && (((flags>>2)&15)>1 || (flags&0x6000u) || tr64_u16(detail->chance)))
#else
   /* All measured TR1 emitters select one sample, chance0,no gain/pitch RNG. */
   if(((flags>>2)&15)>1 || (flags&0x6000u) || tr64_u16(detail->chance))
#endif
    sys_error("audio ambient variation outside measured TR1 sources");
  }
 }
 s->active=1;tr64_audio_active(1);
#if TR64_SND_SEQUEL
 if(!level || level->game<2)
#endif
 if(tr64_music_level_start)tr64_music_level_start();
}
void tr64_snd_level_stop(void)
{
 uint32_t i,j;int pending;
 if(!s)return;
 if(tr64_music_level_stop)tr64_music_level_stop();
 s->active=0;tr64_audio_active(0);
 for(i=0;i<TR64_SND_CHANNELS;++i)s->voices[i].active=0;
 /* Release barrier, never play: finish caller-owned DMA before its pack can
  * be rebound. No buffer or request is discarded while engine/io owns it. */
 do {
  pending=0;tr64_io_frame();tr64_io_pump();
  for(i=0;i<TR64_SND_CHANNELS;++i)for(j=0;j<2;++j) {
   cache_t *c=&s->voices[i].cache[j];poll_cache(c);pending|=c->pending;
  }
 } while(pending);
 for(i=0;i<TR64_SND_CHANNELS;++i)for(j=0;j<2;++j) {
  cache_t *c=&s->voices[i].cache[j];c->valid=0;c->block=INVALID;
 }
 s->bound=0;s->blocks=NULL;s->block_count=0;s->metadata_bytes=0;
 s->level=NULL;s->source_count=0;
#if TR64_SND_SEQUEL
 if(boot_metadata)s->metadata=boot_metadata;
#endif
}
static int unavailable(uint32_t sound,uint32_t sample)
{
 uint32_t i,n=snd_be32(s->metadata+28),offset=snd_be32(s->metadata+32);
 for(i=0;i<n;++i) {
  const uint8_t *p=s->metadata+offset+i*8;
  if(snd_be32(p)==sound && snd_be32(p+4)==sample)return 1;
 }
 return 0;
}
static void start_voice(voice_t *v,uint32_t sample,uint32_t mode,uint32_t pitch)
{
 const uint8_t *d=snd_sample(s->metadata,sample);
 v->sample=sample;v->offset=snd_be32(d);v->frames=snd_be32(d+4);
 v->rate=snd_be32(d+8);v->encoding=snd_be32(d+12);v->mode=mode;
 v->pos=0;v->fraction=0;v->step=pitch*v->rate;
 v->bytes=snd_be32(d+20);v->decoded_start=INVALID;v->history[0]=v->history[1]=0;
 v->active=1;v->started=0;v->data=NULL;v->data_start=v->data_end=0;++s->played;
#if TR64_SND_SEQUEL
 v->sound_id=UINT16_MAX;v->born=s->voice_seq++;
#endif
}
static const tr64_core_sound_details_t *source_detail(uint32_t source)
{
 uint32_t sound=tr64_u16(tr64_level_sound_sources(s->level,source)->sound);
 int32_t index=tr64_i16(*tr64_level_sound_map(s->level,sound));
 return index<0 ? NULL : tr64_level_sound_details(s->level,(uint32_t)index);
}
static uint64_t distance_sq(const tr64_game_view_t *view,const int32_t pos[3],unsigned *carry)
{
 uint64_t sum=0;
 *carry=0;
 for(unsigned i=0;i<3;++i) {
  uint64_t d=(uint64_t)((int64_t)pos[i]-view->camera_eye[i]),square=d*d;
  sum+=square;*carry+=sum<square;
 }
 return sum;
}
static const tr64_game_view_t *listener(const tr64_game_view_t *first,const tr64_game_view_t *second,
                                       const int32_t pos[3])
{
 if(!second)return first;
 unsigned ca,cb;
 uint64_t a=distance_sq(first,pos,&ca),b=distance_sq(second,pos,&cb);
 return cb<ca || (cb==ca && b<=a) ? second:first;
}
#if TR64_SND_SEQUEL
static int mode_loop(uint32_t mode);
#endif
static void ambient_tick(const tr64_game_view_t *view,const tr64_game_view_t *second)
{
 uint32_t i,j;
 const tr64_game_settings_t *settings;
 if(muted || !s->source_count)return;
 settings=game_settings_view();
 for(i=0;i<s->source_count;++i) {
  const tr64_core_sound_sources_t *src=tr64_level_sound_sources(s->level,i);
  const tr64_core_sound_details_t *detail=source_detail(i);
  uint32_t flags=tr64_u16(src->flags),mask=flags&192u;
  int32_t position[3]={tr64_i32(src->x),tr64_i32(src->y),tr64_i32(src->z)};
  uint16_t *gain=s->source_gain[i];gain[0]=gain[1]=0;
  if(settings && settings->audio_sfx && detail && (!mask || (flags&(view->flipped ? 64u : 128u)))) {
   uint32_t volume=tr64_u16(detail->volume);if(volume>32767)volume=32767;
   tr64_snd_spatial_gain(listener(view,second,position),position,volume*4096u/32767u,1,gain,gain+1);
  }
 }
 for(i=0;i<TR64_SND_CHANNELS;++i) {
  voice_t *v=s->voices+i;uint16_t *gain;
  if(!v->active || v->source==UINT16_MAX)continue;
  gain=s->source_gain[v->source];
  if(!gain[0] && !gain[1]) { v->active=0;++s->ambient_stopped; }
  else { v->left=gain[0];v->right=gain[1]; }
 }
 for(j=0;j<TR64_SND_CHANNELS;++j)if(!s->voices[j].active) {
  uint32_t best=INVALID,score=0;
  for(i=0;i<s->source_count;++i) {
   const tr64_core_sound_details_t *detail=source_detail(i);
   uint32_t loudness=s->source_gain[i][0]>s->source_gain[i][1] ? s->source_gain[i][0] : s->source_gain[i][1],k;
#if TR64_SND_SEQUEL
   if(!detail || loudness<=score || (s->source_heard[i] && !mode_loop(tr64_u16(detail->flags)&3u)))continue;
#else
   if(!detail || loudness<=score || (s->source_heard[i] && (tr64_u16(detail->flags)&3u)!=2))continue;
#endif
   for(k=0;k<TR64_SND_CHANNELS;++k)
    if(s->voices[k].active && s->voices[k].source==i)break;
   if(k<TR64_SND_CHANNELS)continue;
   best=i;score=loudness;
  }
  if(best==INVALID)break;
  {
   const tr64_core_sound_details_t *detail=source_detail(best);
   uint32_t sample=tr64_u16(detail->sample);voice_t *v=s->voices+j;
   if(sample>=snd_be32(s->metadata+16))sys_error("audio ambient missing sample");
   v->source=(uint16_t)best;v->positioned=0;
   start_voice(v,sample,tr64_u16(detail->flags)&3u,128);
   v->left=s->source_gain[best][0];v->right=s->source_gain[best][1];
   s->source_heard[best]=1;++s->ambient_started;
  }
 }
}
#if TR64_SND_SEQUEL
/* OpenLara src/level.h:843-847. First game: mode 0 unique, 1 replay, 2 loop.
 * Second game onward: mode 1 unique, mode 3 loop and unique, modes 0 and 2 plain. */
static int mode_unique(uint32_t mode)
{
 return s->level && s->level->game>=2 ? mode==1u || mode==3u : mode==0u || mode==2u;
}
static int mode_loop(uint32_t mode)
{
 return s->level && s->level->game>=2 ? mode==3u : mode==2u;
}
#endif
static void events_tick(const tr64_game_sound_event_t *events,uint32_t count,
                        const tr64_game_view_t *view,const tr64_game_view_t *second)
{
 uint32_t i,j;
 if(!s || !s->active)return;
 if(muted) { s->events+=count;return; }
 for(i=0;i<TR64_SND_CHANNELS;++i)s->voices[i].refreshed=0;
 for(i=0;i<count;++i) {
  const tr64_game_sound_event_t *e=events+i;voice_t *v=NULL;
  ++s->events;
  if(tr64_snd_test_event)tr64_snd_test_event(e);
  if(e->sample<0 || (uint32_t)e->sample>=snd_be32(s->metadata+16)) {
   if(e->sound>=0 && e->sample>=0 && unavailable((uint32_t)e->sound,(uint32_t)e->sample)) { ++s->unavailable;continue; }
   sys_error("audio event references absent unlisted sample sound=%d sample=%d",e->sound,e->sample);
  }
#if !TR64_SND_SEQUEL
  for(j=0;j<TR64_SND_CHANNELS;++j) {
   voice_t *candidate=s->voices+j;
   if(candidate->active && candidate->source==UINT16_MAX && candidate->sample==(uint32_t)e->sample) {
    if(e->stop) { candidate->active=0;continue; }
    if(e->mode==0 || e->mode==2) { v=candidate;break; }
    if(e->mode==1) { v=candidate;break; }
   }
  }
#else
  for(j=0;j<TR64_SND_CHANNELS;++j) {
   voice_t *candidate=s->voices+j;
   if(!(candidate->active && candidate->source==UINT16_MAX)) continue;
   if(e->stop) {
    if(candidate->sample==(uint32_t)e->sample) candidate->active=0;
    continue;
   }
   if(s->level && s->level->game>=2) {
    if(mode_unique(e->mode) && candidate->sound_id==(uint16_t)e->sound) { v=candidate;break; }
   } else if(candidate->sample==(uint32_t)e->sample) {
    if(e->mode==0 || e->mode==2) { v=candidate;break; }
    if(e->mode==1) { v=candidate;break; }
   }
  }
#endif
  if(e->stop)continue;
  if(e->volume<=0 || e->volume>64 || e->pitch<=0 || e->pitch>256 || e->mode>3)sys_error("audio invalid game command");
  if(!v)for(j=0;j<TR64_SND_CHANNELS;++j)if(!s->voices[j].active) { v=s->voices+j;break; }
  /* Audible game events take priority over virtualizable ambient loops. */
  if(!v) {
   uint32_t score=UINT32_MAX;
   for(j=0;j<TR64_SND_CHANNELS;++j) {
    voice_t *candidate=s->voices+j;
    uint32_t gain=candidate->left>candidate->right ? candidate->left : candidate->right;
    if(candidate->source!=UINT16_MAX && gain<score) { v=candidate;score=gain; }
   }
   if(v) { v->active=0;++s->ambient_stopped; }
  }
#if TR64_SND_SEQUEL
  /* OpenLara src/platform/n64_magma/n64_api.c:97,656-659: six effect channels,
   * overwrite the oldest. Second game only: oldest plain one-shot (modes 0 and 2).
   * Uniques (modes 1 and 3) are never stolen. */
  if(!v && s->level && s->level->game>=2) {
   uint32_t oldest=UINT32_MAX;
   for(j=0;j<TR64_SND_CHANNELS;++j) {
    voice_t *candidate=s->voices+j;
    if(!candidate->active || candidate->source!=UINT16_MAX)continue;
    if(candidate->mode==1u || candidate->mode==3u)continue;
    if(candidate->born<oldest) { v=candidate;oldest=candidate->born; }
   }
   if(v) { v->active=0;++s->stolen; }
  }
#endif
  if(!v) { ++s->dropped;continue; }
  v->base_gain=(uint16_t)(e->volume*64);v->source=UINT16_MAX;v->refreshed=1;
  v->positioned=e->positioned;memcpy(v->position,e->position,sizeof(v->position));
#if !TR64_SND_SEQUEL
  if(v->active && (e->mode==0 || e->mode==2)) {
   v->step=(uint32_t)e->pitch*v->rate;continue;
  }
#else
  if(v->active && (s->level && s->level->game>=2 ? mode_unique(e->mode) : (e->mode==0 || e->mode==2))) {
   v->step=(uint32_t)e->pitch*v->rate;continue;
  }
#endif
  start_voice(v,(uint32_t)e->sample,e->mode,(uint32_t)e->pitch);
#if TR64_SND_SEQUEL
  v->sound_id=(uint16_t)e->sound;
#endif
 }
 /* Mode2 is maintained by this tick's emitter, not an endless one-shot.
  * Already-owned reads finish asynchronously in pump; no new read is queued. */
 for(i=0;i<TR64_SND_CHANNELS;++i)
#if !TR64_SND_SEQUEL
  if(s->voices[i].active && s->voices[i].source==UINT16_MAX && s->voices[i].mode==2 && !s->voices[i].refreshed)
#else
  if(s->voices[i].active && s->voices[i].source==UINT16_MAX && !s->voices[i].refreshed && mode_loop(s->voices[i].mode))
#endif
   s->voices[i].active=0;
 for(i=0;i<TR64_SND_CHANNELS;++i) {
  voice_t *v=s->voices+i;
  if(!v->active || v->source!=UINT16_MAX)continue;
  if(v->positioned)tr64_snd_spatial_gain(listener(view,second,v->position),v->position,v->base_gain,0,&v->left,&v->right);
  else v->left=v->right=v->base_gain;
 }
}
static uint32_t voice_offset(const voice_t *v)
{ return v->offset+(v->encoding==3 ? v->pos/28*16 : v->pos*v->encoding); }
static void prefetch(voice_t *v)
{
 uint32_t block,next,i,offset;cache_t *a=NULL;
 for(i=0;i<2;++i)poll_cache(v->cache+i);
 if(!v->active || s->resident_offset[v->sample]!=INVALID)return;
 offset=voice_offset(v);block=find_block(offset);
 for(i=0;i<2;++i)if((v->cache[i].valid || v->cache[i].pending) && v->cache[i].block==block)a=v->cache+i;
 if(!a) {
  for(i=0;i<2;++i)if(!v->cache[i].pending) { a=v->cache+i;request(a,block);break; }
 }
 if(!a)return;
 next=block+1;
  if(next>=s->block_count || s->blocks[next].logical_offset>=v->offset+v->bytes) {
#if TR64_SND_SEQUEL
   if(!mode_loop(v->mode))return;
#else
   if(v->mode!=2)return;
#endif
   next=find_block(v->offset);
  }
 if(next!=block)request(a==v->cache ? v->cache+1 : v->cache,next);
}
static int sample_data(voice_t *v)
{
 uint32_t i,offset=voice_offset(v);
 if(s->resident_offset[v->sample]!=INVALID) {
  v->data=s->resident+s->resident_offset[v->sample];v->data_start=0;v->data_end=v->frames;return 1;
 }
 for(i=0;i<2;++i) {
  cache_t *c=v->cache+i;const tr64_pack_stream_block_t *b;
  if(!c->valid)continue;
  b=s->blocks+c->block;
  if(offset>=b->logical_offset && offset-b->logical_offset<b->raw_bytes) {
   v->data=c->data;
   if(v->encoding==3) {
    v->data_start=(b->logical_offset-v->offset)/16*28;
    v->data_end=v->data_start+b->raw_bytes/16*28;
   } else {
    v->data_start=(b->logical_offset-v->offset)/v->encoding;
    v->data_end=v->data_start+b->raw_bytes/v->encoding;
   }
   return 1;
  }
 }
 return 0;
}
static uint32_t span_frames(uint32_t available,uint32_t fraction,uint32_t step,uint32_t remaining)
{
 uint64_t last=(uint64_t)(remaining-1)*step+fraction;
 if(available>(last>>16))return remaining;
 if(available<=32767)return ((available<<16)-fraction+step-1)/step;
 return (uint32_t)((((uint64_t)available<<16)-fraction+step-1)/step);
}
#if TR64_SND_SEQUEL
/* A sequel loop is not resident: the frequent cache holds the borrowed
 * metadata. The last block can be shorter than one mix, so the wrap must
 * finish the start read the way preload waits, or that mix is silence. */
static int finish_loop_block(voice_t *v)
{
 uint32_t start;
 v->data=NULL;v->data_start=0;v->data_end=0;v->decoded_start=INVALID;
 tr64_io_frame();
 start=sys_time_us();
 while(!sample_data(v)) {
  prefetch(v);
  poll_cache(v->cache);poll_cache(v->cache+1);
  if(sample_data(v))break;
  /* A 4 KiB piece is in flight. A poll count returns before the cart DMA;
   * the host read completes inside one pump. */
  if(!(v->cache[0].pending || v->cache[1].pending) || (uint32_t)(sys_time_us()-start)>8000u)return 0;
  tr64_io_pump();
 }
 return 1;
}
#endif
uint32_t tr64_snd_mix(tr64_snd_frame_t *output,uint32_t frames,uint32_t rate)
{
 uint32_t frame,i,count=0,t=sys_profile_ticks();int starved=0,stereo=0;
 const int16_t *music=NULL;
 if(!rate || rate>48000)sys_error("audio invalid output rate");
 memset(output,0,frames*sizeof(*output));
 for(i=0;s && s->active && i<TR64_SND_CHANNELS;++i)
  if(s->voices[i].active && s->voices[i].left!=s->voices[i].right)stereo=1;
 if(stereo)memset(output+frames,0,frames*sizeof(*output));
 if(!s)return 0;
 if(tr64_music_mono_pcm)music=tr64_music_mono_pcm(frames,rate);
 if(!music && tr64_film_mono_pcm)music=tr64_film_mono_pcm(frames,rate);
 if(!s->active && !music)return 0;
 for(i=0;s->active && i<TR64_SND_CHANNELS;++i) {
  voice_t *v=s->voices+i;
  uint32_t pos,fraction,step,start,end,decoded_start,encoding,length,mode;
  const uint8_t *data;int32_t gain,difference;
  if(!v->active)continue;
  ++count;prefetch(v);
  pos=v->pos;fraction=v->fraction;step=(v->step<<9)/rate;
  data=v->data;start=v->data_start;end=v->data_end;decoded_start=v->decoded_start;
  encoding=v->encoding;length=v->frames;mode=v->mode;gain=v->left;difference=(int32_t)v->right-gain;frame=0;
  while(frame<frames) {
   uint32_t limit,n,j,offset;tr64_snd_frame_t *dst=output+frame,*delta=output+frames+frame;
   if(!data || pos<start || pos>=end) {
    v->pos=pos;
    if(!sample_data(v)) { starved|=v->started || frame>0;break; }
    data=v->data;start=v->data_start;end=v->data_end;
   }
   limit=end<length ? end : length;
   if(encoding==3) {
    if(pos<decoded_start || pos-decoded_start>=28) {
     decoded_start=pos/28*28;
     snd_spu_block(v->decoded,data+(decoded_start-start)/28*16,v->history);
    }
    if(limit>decoded_start+28)limit=decoded_start+28;
   }
   n=span_frames(limit-pos,fraction,step,frames-frame);
   if(encoding==1) {
    int32_t scaled=gain*256,delta_scaled=difference*256;offset=pos-start;
    if(difference)for(j=0;j<n;++j) {
     int32_t value=(int8_t)data[offset];
     dst[j].sum+=value*scaled;delta[j].sum+=value*delta_scaled;
     fraction+=step;offset+=fraction>>16;fraction&=65535;
    }
    else for(j=0;j<n;++j) {
     dst[j].sum+=(int32_t)(int8_t)data[offset]*scaled;
     fraction+=step;offset+=fraction>>16;fraction&=65535;
    }
    pos=start+offset;
   } else if(encoding==2) {
    offset=pos-start;
    if(difference)for(j=0;j<n;++j) {
     /* SN64 starts/blocks are16-aligned; PCM16 indices are two-byte aligned. */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
     int32_t value=((const int16_t *)data)[offset];
#else
     int32_t value=tr64_i16(((const tr64_i16_be_t *)data)[offset]);
#endif
     dst[j].sum+=value*gain;delta[j].sum+=value*difference;
     fraction+=step;offset+=fraction>>16;fraction&=65535;
    }
    else for(j=0;j<n;++j) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
     int32_t value=((const int16_t *)data)[offset];
#else
     int32_t value=tr64_i16(((const tr64_i16_be_t *)data)[offset]);
#endif
     dst[j].sum+=value*gain;
     fraction+=step;offset+=fraction>>16;fraction&=65535;
    }
    pos=start+offset;
   } else {
    offset=pos-decoded_start;
    if(difference)for(j=0;j<n;++j) {
     int32_t value=v->decoded[offset];
     dst[j].sum+=value*gain;delta[j].sum+=value*difference;
     fraction+=step;offset+=fraction>>16;fraction&=65535;
    }
    else for(j=0;j<n;++j) {
     dst[j].sum+=(int32_t)v->decoded[offset]*gain;
     fraction+=step;offset+=fraction>>16;fraction&=65535;
    }
    pos=decoded_start+offset;
   }
   frame+=n;
   if(pos>=length) {
#if TR64_SND_SEQUEL
    if(s->level && s->level->game>=2) {
     if(mode_loop(mode)) {
      pos%=length;v->pos=pos;v->fraction=fraction;
      if(!finish_loop_block(v)) { starved|=v->started || frame>0;break; }
      data=v->data;start=v->data_start;end=v->data_end;decoded_start=v->decoded_start;
     } else { v->active=0;break; }
    } else
#endif
    if(mode==2) { pos%=length;data=NULL;decoded_start=INVALID; }
    else { v->active=0;break; }
   }
  }
  if(frame || !v->active)v->started=1;
  v->pos=pos;v->fraction=fraction;v->data=data;v->data_start=start;v->data_end=end;
  v->decoded_start=decoded_start;prefetch(v);
 }
 if(count || music) {
  if(!stereo)for(frame=0;frame<frames;++frame) {
   int32_t sum=output[frame].sum+(music ? (int32_t)music[frame]*4096 : 0);
   int32_t value=sum/(4096*TR64_SND_MASTER_DIVISOR);
   if(value>32767 || value< -32768)++s->clipped;
   value=value>32767 ? 32767 : value< -32768 ? -32768 : value;
   output[frame].stereo[0]=output[frame].stereo[1]=(int16_t)value;
  }
  else for(frame=0;frame<frames;++frame) {
   /* Keep both buses in Q12 through music addition and shared mastering. */
   int32_t left_sum=output[frame].sum+(music ? (int32_t)music[frame]*4096 : 0);
   int32_t right_sum=left_sum+output[frames+frame].sum;
   int32_t left=left_sum/(4096*TR64_SND_MASTER_DIVISOR);
   int32_t right=right_sum/(4096*TR64_SND_MASTER_DIVISOR);
   if(left>32767 || left< -32768 || right>32767 || right< -32768)++s->clipped;
   output[frame].stereo[0]=(int16_t)(left>32767 ? 32767 : left< -32768 ? -32768 : left);
   output[frame].stereo[1]=(int16_t)(right>32767 ? 32767 : right< -32768 ? -32768 : right);
  }
 }
 if(starved)++s->underruns;
 s->mix_frames+=frames;t=sys_profile_ticks()-t;s->mix_us+=sys_profile_us(t);
 if(t>s->mix_peak)s->mix_peak=t;
 return count;
}
void tr64_snd_tick(void)
{
 uint32_t count,i;const tr64_game_sound_event_t *events;tr64_game_view_t view;
 tr64_game_view_t second;const tr64_game_view_t *partner=NULL;
 tr64_game_sound_event_t controlled[TR64_SND_CHANNELS];
 if(!s || !s->active)return;
 events=game_sound_events(&count);game_view_camera(&view);
 if(game_player_mask()&2u) { game_view_camera_player(1,&second);partner=&second; }
 if(bench_voices) {
  if(snd_be32(s->metadata+16)<bench_voices)sys_error("audio benchmark sample domain");
  for(i=0;i<bench_voices;++i)
   controlled[i]=(tr64_game_sound_event_t){.sound=(int16_t)i,.sample=(int16_t)i,.volume=8,.pitch=128,.mode=2};
  events=controlled;count=bench_voices;
 }
 events_tick(events,count,&view,partner);
 ambient_tick(&view,partner);
 if(tr64_music_tick)tr64_music_tick();
 ++s->ticks;tr64_audio_tick();
}
void tr64_snd_pump(void)
{
 uint32_t i;
 if(!s)return;
 if(s->active) {
  for(i=0;i<TR64_SND_CHANNELS;++i)prefetch(s->voices+i);
  /* Each pump advances the ordinary shared engine/io queue, never a private
   * cart read and never a reset of the per-frame play bandwidth allowance. */
  if(&tr64_perf_state && (tr64_perf_state&1u)) {
   uint32_t stamp=sys_profile_ticks();
   for(i=0;i<TR64_SND_CHANNELS*4u;++i)tr64_io_pump();
   tr64_perf_io_ticks+=sys_profile_ticks()-stamp;
  } else
  for(i=0;i<TR64_SND_CHANNELS*4u;++i)tr64_io_pump();
  for(i=0;i<TR64_SND_CHANNELS;++i)prefetch(s->voices+i);
 }
 if(tr64_music_pump)tr64_music_pump();
 tr64_audio_pump();
}
void tr64_snd_report(void)
{
 if(!s)return;
 if(tr64_music_report)tr64_music_report();
 if(tr64_snd_test_report)tr64_snd_test_report();
 log_printf("audio: ticks=%lu events=%lu played=%lu dropped=%lu unavailable=%lu underruns=%lu clipped=%lu frames=%lu resident=%lu resident_capacity=%lu arena=%u stream_reads=%lu stream_bytes=%lu mixer_us=%llu mixer_peak_buffer_us=%lu\n",
  (unsigned long)s->ticks,(unsigned long)s->events,(unsigned long)s->played,(unsigned long)s->dropped,
  (unsigned long)s->unavailable,(unsigned long)s->underruns,(unsigned long)s->clipped,(unsigned long)s->mix_frames,
  (unsigned long)s->resident_bytes,(unsigned long)s->resident_capacity,TR64_SND_ARENA_BYTES,
  (unsigned long)s->stream_reads,(unsigned long)s->stream_bytes,(unsigned long long)s->mix_us,
  (unsigned long)sys_profile_us(s->mix_peak));
 log_printf("audio-ambient: sources=%lu started=%lu stopped=%lu\n",
  (unsigned long)s->source_count,(unsigned long)s->ambient_started,(unsigned long)s->ambient_stopped);
#if TR64_SND_SEQUEL
 log_printf("audio-stolen: count=%lu\n",(unsigned long)s->stolen);
#endif
 tr64_audio_report();
}
