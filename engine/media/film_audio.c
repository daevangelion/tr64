/* SPDX-License-Identifier: MIT */
/* Film sound: one MU64 stream (SPU ADPCM, mono) pulled through the same mixer
 * bus as music. Resident, because the mixer calls it. Its state lives in the
 * video arena, so nothing here allocates; transport follows music.c. */
#include "engine/media/film.h"
#include "engine/snd/music_decode.h"
#include "engine/snd/music_format.h"
#include "engine/snd/music.h"
#include "engine/snd/snd.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/io.h"
#include <string.h>
#define WINDOW_BYTES 4096u
#define INDEX_BLOCKS 512u
#define NONE UINT32_MAX
enum { IDLE,QUERY,HEADER,PRIME,READY,PLAY,DONE };
typedef struct {
 uint8_t data[WINDOW_BYTES] __attribute__((aligned(16)));
 tr64_io_req_t request;
 uint32_t block;
 int pending,valid;
} window_t;
typedef struct {
 tr64_pack_stream_block_t blocks[INDEX_BLOCKS];
 tr64_pack_stream_query_t query;
 window_t windows[2];
 int16_t output[TR64_MUSIC_OUTPUT_FRAMES] __attribute__((aligned(16)));
 tr64_music_decoder_t decoder;
 tr64_music_format_t format;
 tr64_pack_stream_source_t source;
 uint32_t phase,pack_bytes,block_count,chunk_bytes;
 uint32_t position,fraction,unit,unit_sample,play_block,within;
 uint32_t underruns,reads,read_bytes;
 int query_started;
} film_audio_t;
static film_audio_t *fa;
uint32_t tr64_film_audio_bytes(void) { return (uint32_t)sizeof(film_audio_t); }
void tr64_film_audio_begin(void *state,const tr64_pack_stream_source_t *source,uint32_t pack_bytes)
{
 uint32_t i;
 if(fa || !state || ((uintptr_t)state&15u) || !source || !source->block_count)
  sys_error("film audio: begin contract");
 fa=state;memset(fa,0,sizeof(*fa));
 fa->source=*source;fa->pack_bytes=pack_bytes;
 for(i=0;i<2;++i)fa->windows[i].block=NONE;
 /* The first window is the wire-table scratch until the header read. */
 tr64_pack_stream_query_begin(&fa->query,&fa->source,pack_bytes,fa->blocks,INDEX_BLOCKS,
  fa->windows[0].data,WINDOW_BYTES);
 fa->query_started=1;fa->phase=QUERY;
}
static void poll_window(window_t *w)
{
 if(!w->pending)return;
 if(!tr64_pack_stream_poll(fa->blocks+w->block,&w->request))return;
 w->pending=0;w->valid=1;
}
static int read_window(window_t *w,uint32_t block)
{
 int rc;
 if(w->pending)return 0;
 w->valid=0;w->block=block;memset(&w->request,0,sizeof(w->request));
 rc=tr64_pack_stream_read(fa->blocks+block,&w->request,w->data,sizeof(w->data));
 if(rc==TR64_IO_SUBMIT_BUSY)return 0;
 if(rc)sys_error("film audio: stream submit %d",rc);
 w->pending=1;++fa->reads;fa->read_bytes+=fa->blocks[block].raw_bytes;
 return 1;
}
static window_t *cached(uint32_t block)
{
 uint32_t i;
 for(i=0;i<2;++i)if(fa->windows[i].valid && fa->windows[i].block==block)return fa->windows+i;
 return NULL;
}
static int scheduled(uint32_t block)
{
 uint32_t i;
 for(i=0;i<2;++i)if((fa->windows[i].valid || fa->windows[i].pending) && fa->windows[i].block==block)return 1;
 return 0;
}
static void prefetch(void)
{
 uint32_t wanted[2],i,j;
 wanted[0]=fa->play_block;wanted[1]=wanted[0]+1;
 for(i=0;i<2;++i) {
  if(wanted[i]>=fa->block_count || scheduled(wanted[i]))continue;
  for(j=0;j<2;++j) {
   window_t *w=fa->windows+j;
   if(!w->pending && (!w->valid || (w->block!=wanted[0] && w->block!=wanted[1]))) {
    (void)read_window(w,wanted[i]);break;
   }
  }
 }
}
static void checked_header(void)
{
 uint32_t i,offset;
 tr64_music_format(fa->windows[0].data,fa->blocks[0].raw_bytes,fa->query.result.total_bytes,
  TR64_MUSIC_FILM_TRACK,&fa->format);
 if(fa->format.flags&TR64_MUSIC_UNAVAILABLE)sys_error("film audio: unavailable stream");
 fa->chunk_bytes=WINDOW_BYTES/fa->format.unit_bytes*fa->format.unit_bytes;
 offset=fa->format.data_offset;
 for(i=1;i<fa->block_count;++i) {
  const tr64_pack_stream_block_t *b=fa->blocks+i;
  uint32_t expected=fa->format.total-offset;
  if(expected>fa->chunk_bytes)expected=fa->chunk_bytes;
  if(b->logical_offset!=offset || b->raw_bytes!=expected || b->raw_bytes%fa->format.unit_bytes)
   sys_error("film audio: physical partition");
  offset+=b->raw_bytes;
 }
 if(offset!=fa->format.total || fa->block_count<2)sys_error("film audio: stream extent");
 tr64_music_decoder_init(&fa->decoder,fa->format.codec);
 fa->windows[0].valid=0;fa->windows[0].block=NONE;
 fa->play_block=1;fa->phase=PRIME;prefetch();
}
int tr64_film_audio_pump(void)
{
 uint32_t i;
 if(!fa)sys_error("film audio: not begun");
 for(i=0;i<2;++i)poll_window(fa->windows+i);
 if(fa->phase==QUERY) {
  if(!tr64_pack_stream_query_poll(&fa->query))return 0;
  fa->query_started=0;fa->block_count=fa->query.result.block_count;
  if(!fa->block_count || fa->blocks[0].logical_offset || fa->blocks[0].raw_bytes!=64)
   sys_error("film audio: metadata partition");
  fa->phase=HEADER;
 }
 if(fa->phase==HEADER) {
  window_t *w=fa->windows;
  if(!w->valid && !w->pending)(void)read_window(w,0);
  if(!w->valid)return 0;
  checked_header();
 }
 if(fa->phase==PRIME) {
  prefetch();
  if(cached(1))fa->phase=READY;
 }
 if(fa->phase==PLAY || fa->phase==READY)prefetch();
 return fa->phase>=READY;
}
void tr64_film_audio_start(void)
{
 if(!fa || fa->phase!=READY)sys_error("film audio: start before primed");
 fa->phase=PLAY;tr64_audio_active(1);
}
uint32_t tr64_film_audio_us(void)
{
 uint64_t frames;
 if(!fa || fa->phase<PLAY)return 0;
 frames=fa->phase==DONE ? fa->format.frames : fa->position;
 return (uint32_t)(frames*1000000u/fa->format.rate);
}
int tr64_film_audio_ended(void) { return !fa || fa->phase==DONE; }
uint32_t tr64_film_audio_underruns(void) { return fa ? fa->underruns : 0; }
void tr64_film_audio_end(void)
{
 uint32_t i;int done;
 if(!fa)return;
 fa->phase=IDLE;
 do {
  done=1;
  if(fa->query_started && !tr64_pack_stream_query_drain(&fa->query))done=0;
  for(i=0;i<2;++i) {
   poll_window(fa->windows+i);
   if(fa->windows[i].pending)done=0;
  }
  if(!done) { tr64_io_frame();tr64_io_pump(); }
 } while(!done);
 log_printf("film-audio: reads=%lu bytes=%lu underruns=%lu frames=%lu rate=%lu\n",
  (unsigned long)fa->reads,(unsigned long)fa->read_bytes,(unsigned long)fa->underruns,
  (unsigned long)fa->position,(unsigned long)fa->format.rate);
 fa=NULL;tr64_audio_active(0);
}
const int16_t *tr64_film_mono_pcm(uint32_t frames,uint32_t rate)
{
 film_audio_t *s=fa;window_t *w;
 uint32_t i,step,position,fraction,unit,sample,block,within,unit_frames,unit_bytes;
 if(!s || s->phase!=PLAY)return NULL;
 if(!frames || frames>TR64_MUSIC_OUTPUT_FRAMES || rate<16000 || rate>48000)
  sys_error("film audio: output extent/rate");
 step=(uint32_t)(((uint64_t)s->format.rate<<16)/rate);
 position=s->position;fraction=s->fraction;unit=s->unit;sample=s->unit_sample;
 block=s->play_block;within=s->within;
 unit_frames=s->format.unit_frames;unit_bytes=s->format.unit_bytes;
 w=cached(block);
 for(i=0;i<frames;++i) {
  if(position>=s->format.frames) {
   memset(s->output+i,0,(frames-i)*sizeof(*s->output));
   s->phase=DONE;break;
  }
  if(!w) { ++s->underruns;memset(s->output+i,0,(frames-i)*sizeof(*s->output));break; }
  if(s->decoder.unit!=unit)
   (void)tr64_music_decoder_sample(&s->decoder,w->data+within,unit,sample);
  s->output[i]=s->decoder.decoded[sample];
  fraction+=step;
  {
   uint32_t advance=fraction>>16;
   position+=advance;sample+=advance;fraction&=65535u;
   if(sample>=unit_frames) {
    sample-=unit_frames;++unit;within+=unit_bytes;
    if(within==s->blocks[block].raw_bytes) { within=0;++block;w=cached(block); }
   }
  }
 }
 s->position=position;s->fraction=fraction;s->unit=unit;s->unit_sample=sample;
 s->play_block=block;s->within=within;
 return s->output;
}
