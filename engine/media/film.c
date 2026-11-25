/* SPDX-License-Identifier: MIT */
/* The `video` code unit: plays one film with no level resident. One call, one
 * film: stream index, decoder, film sound, presentation and skip all run
 * inside it, servicing engine/io and the audio mixer whenever they wait.
 * Writable state lives in the borrowed arena; nothing is allocated elsewhere. */
#include "engine/media/film.h"
#include "engine/media/film_arena.h"
#include "engine/core/frame.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/in/input.h"
#include "engine/snd/snd.h"
#include "tr64/io.h"
#include <string.h>
#define VIDEO_WINDOW 8u
#define VIDEO_BLOCK TR64_PACK_BLOCK
#define VIDEO_INDEX 256u
#define SCRATCH_BYTES 4096u
/* The mixer is filled ahead of the speaker: two 448-frame buffers at 22,050 Hz
 * are in flight at steady state (docs/formats/n64-video.md). */
#define AUDIO_LEAD_US 40640u
#define STALL_US TR64_FILM_STALL_US
enum { FREE,PENDING,READY };
typedef struct {
 tr64_film_request_t *request;
 film_arena_t arena;
 tr64_pack_stream_block_t *blocks;uint32_t block_count,total_bytes;
 tr64_pack_stream_query_t query;
 uint8_t *scratch,*data;
 tr64_io_req_t requests[VIDEO_WINDOW];
 uint32_t window_block[VIDEO_WINDOW];uint8_t window_state[VIDEO_WINDOW];
 uint32_t next_submit,read_block,read_offset;
 int index_started,index_ready;
 uint32_t io_phase,skip,previous_buttons,polls,audio_done,audio_done_wall;
 uint32_t stream_bytes,source_ticks;
} film_t;
static void stream_service(film_t *f)
{
 uint32_t slot;
 for(slot=0;slot<VIDEO_WINDOW;++slot)
  if(f->window_state[slot]==PENDING &&
     tr64_pack_stream_poll(f->blocks+f->window_block[slot],f->requests+slot))
   f->window_state[slot]=READY;
 if(!f->index_ready)return;
 while(f->next_submit<f->block_count && f->next_submit<f->read_block+VIDEO_WINDOW) {
  int rc;
  slot=f->next_submit%VIDEO_WINDOW;
  if(f->window_state[slot]!=FREE)break;
  rc=tr64_pack_stream_read(f->blocks+f->next_submit,f->requests+slot,f->data+slot*VIDEO_BLOCK,VIDEO_BLOCK);
  if(rc==TR64_IO_SUBMIT_BUSY)break;
  if(rc)sys_error("video stream submit %d",rc);
  f->window_state[slot]=PENDING;f->window_block[slot]=f->next_submit;
  f->stream_bytes+=f->blocks[f->next_submit].raw_bytes;++f->next_submit;
 }
}
/* Everything that must keep running while the caller waits or works. */
static void service(film_t *f)
{
 uint32_t now=sys_time_us();
 if(tr64_frame_due(&f->io_phase,now)) {
  tr64_io_frame();tr64_audio_tick();
  {
   tr64_input_t pad=tr64_input_read();
   if((pad.buttons&TR64_PAD_START) && !(f->previous_buttons&TR64_PAD_START))f->skip=1;
   f->previous_buttons=pad.buttons;++f->polls;
  }
 }
 tr64_io_pump();
 if(f->index_started && !f->index_ready && tr64_pack_stream_query_poll(&f->query)) {
  f->index_ready=1;f->block_count=f->query.result.block_count;f->total_bytes=f->query.result.total_bytes;
 }
 stream_service(f);
 (void)tr64_film_audio_pump();
 tr64_snd_pump();
}
static void service_context(void *context) { service(context); }
static void wait_for(film_t *f,int (*done)(film_t *),const char *what)
{
 uint32_t start=sys_time_us();
 while(!done(f)) {
  service(f);
  if((uint32_t)(sys_time_us()-start)>STALL_US)sys_error("video stalled waiting for %s",what);
 }
}
static int video_index_ready(film_t *f) { return f->index_ready; }
static int audio_ready(film_t *f) { (void)f;return tr64_film_audio_pump(); }
static int source_read_body(film_t *f,void *memory,unsigned bytes)
{
 uint8_t *out=memory;unsigned done=0;
 while(done<bytes && f->read_block<f->block_count) {
  uint32_t slot=f->read_block%VIDEO_WINDOW,raw=f->blocks[f->read_block].raw_bytes,n;
  uint32_t waited=sys_time_us();
  while(f->window_state[slot]!=READY || f->window_block[slot]!=f->read_block) {
   service(f);
   if((uint32_t)(sys_time_us()-waited)>STALL_US)sys_error("video stream stalled at block %lu",(unsigned long)f->read_block);
  }
  n=raw-f->read_offset;if(n>bytes-done)n=bytes-done;
  memcpy(out+done,f->data+slot*VIDEO_BLOCK+f->read_offset,n);
  done+=n;f->read_offset+=n;
  if(f->read_offset==raw) {
   f->window_state[slot]=FREE;++f->read_block;f->read_offset=0;stream_service(f);
  }
 }
 return (int)done;
}
/* The decoder calls this from inside a decode step: the time spent here (waiting for
 * blocks, servicing audio and I/O) is not decoder work and is kept out of decode_us. */
static int source_read(void *context,void *memory,unsigned bytes)
{
 film_t *f=context;uint32_t t0=sys_profile_ticks();
 int n=source_read_body(f,memory,bytes);
 f->source_ticks+=sys_profile_ticks()-t0;
 return n;
}
/* One slice of decoding: returns whether more remains; decode_ticks gains the decoder's
 * own time, the step's less what it spent in source_read. */
static int decode_step(film_t *f,uint32_t *decode_ticks)
{
 uint32_t t0=sys_profile_ticks(),s0=f->source_ticks;
 int more=tr64_film_decoder_step();
 *decode_ticks+=(uint32_t)(sys_profile_ticks()-t0)-(f->source_ticks-s0);
 return more;
}
static int source_seek(void *context,uint32_t offset)
{
 film_t *f=context;
 uint32_t position=f->read_block<f->block_count ?
  f->blocks[f->read_block].logical_offset+f->read_offset : f->total_bytes;
 /* The decoder only rewinds at open, before reading: no other seek exists. */
 return offset==position ? 0 : -1;
}
static uint32_t due_us(uint32_t picture) { return (uint32_t)((uint64_t)picture*1000000u/TR64_FILM_FPS); }
static uint32_t film_clock(film_t *f)
{
 uint32_t us=tr64_film_audio_us();
 /* Sound over before the picture: the film clock carries on with the wall clock. */
 if(tr64_film_audio_ended()) {
  if(!f->audio_done) { f->audio_done=1;f->audio_done_wall=sys_time_us(); }
  us+=(uint32_t)(sys_time_us()-f->audio_done_wall);
 }
 return us>AUDIO_LEAD_US ? us-AUDIO_LEAD_US : 0;
}
static void drain_stream(film_t *f)
{
 uint32_t slot;int pending;
 do {
  pending=0;
  for(slot=0;slot<VIDEO_WINDOW;++slot) {
   if(f->window_state[slot]==PENDING && tr64_pack_stream_poll(f->blocks+f->window_block[slot],f->requests+slot))
    f->window_state[slot]=READY;
   if(f->window_state[slot]==PENDING)pending=1;
  }
  if(f->index_started && !f->index_ready) {
   if(!tr64_pack_stream_query_drain(&f->query))pending=1;
   f->index_started=0;
  }
  if(pending) { tr64_io_frame();tr64_io_pump(); }
 } while(pending);
}
static void play(tr64_film_request_t *request)
{
 film_t *f;void *audio;tr64_film_memory_t memory;tr64_film_source_t source;tr64_film_info_t info;
 tr64_video_report_t *r=&request->report;
 uint32_t slow_run=0,start,picture=0,ticks_limit,decode_ticks=0,decode_us,t0,ready=0,wall_start,clock;
 uint32_t last_clock=0,last_moved;
 int ended=0;
 memset(r,0,sizeof(*r));
 f=(film_t *)request->arena;
 if(((uintptr_t)f&15u) || request->arena_bytes<sizeof(*f))sys_error("video arena contract");
 memset(f,0,sizeof(*f));f->request=request;
 film_arena_init(&f->arena,request->arena,request->arena_bytes,(uint32_t)sizeof(*f));
 start=sys_time_us();
 f->io_phase=start-TR64_FRAME_US;
 f->previous_buttons=tr64_input_read().buttons;
 audio=film_arena_take(&f->arena,tr64_film_audio_bytes(),16);
 f->blocks=film_arena_take(&f->arena,VIDEO_INDEX*(uint32_t)sizeof(*f->blocks),16);
 f->scratch=film_arena_take(&f->arena,SCRATCH_BYTES,16);
 f->data=film_arena_take(&f->arena,VIDEO_WINDOW*VIDEO_BLOCK,16);
 if(request->video.block_count>VIDEO_INDEX)sys_error("video stream index capacity");
 /* Both indexes load concurrently through the shared queue. */
 tr64_pack_stream_query_begin(&f->query,&request->video,request->pack_bytes,f->blocks,VIDEO_INDEX,
  f->scratch,SCRATCH_BYTES);
 f->index_started=1;
 tr64_film_audio_begin(audio,&request->film,request->pack_bytes);
 wait_for(f,video_index_ready,"video index");
 wait_for(f,audio_ready,"film sound");
 r->index_us=(uint32_t)(sys_time_us()-start);
 memory=(tr64_film_memory_t){&f->arena,film_arena_alloc,film_arena_free};
 source=(tr64_film_source_t){f,source_read,source_seek};
 t0=sys_time_us();
 if(!tr64_film_decoder_open(&memory,&source,&info))sys_error("video decoder open failed");
 r->open_us=(uint32_t)(sys_time_us()-t0);
 if(info.width!=TR64_WIDTH || info.height!=TR64_HEIGHT || info.fps_x1000!=TR64_FILM_FPS*1000u)
  sys_error("video stream is %lux%lu at %lu mfps, expected 320x240 at 15",(unsigned long)info.width,
   (unsigned long)info.height,(unsigned long)info.fps_x1000);
 /* First picture before the sound starts, so the two clocks begin together. */
 for(t0=sys_time_us();;) {
  if(!decode_step(f,&decode_ticks))break;
  service(f);
  if((uint32_t)(sys_time_us()-t0)>STALL_US)sys_error("video stalled decoding the first picture");
 }
 ready=1;
 ticks_limit=request->max_ticks;
 tr64_film_audio_start();
 wall_start=sys_time_us();
 f->io_phase=wall_start-TR64_FRAME_US;f->polls=0;
 r->sync_min_us=INT32_MAX;r->sync_max_us=INT32_MIN;last_moved=wall_start;
 while(!ended) {
  service(f);
  if(f->skip) { request->result=TR64_VIDEO_SKIPPED;break; }
  clock=film_clock(f);
  /* The film clock is the audio position: if it stops (the mixer never pulls the film
   * sound, a permanent underrun) nothing else would ever end the loop. */
  if(clock!=last_clock) { last_clock=clock;last_moved=sys_time_us(); }
  else if((uint32_t)(sys_time_us()-last_moved)>STALL_US)
   sys_error("video film clock stalled at %lu us (audio underruns %lu)",(unsigned long)clock,
    (unsigned long)tr64_film_audio_underruns());
  if(ticks_limit && f->polls>=ticks_limit) { request->result=TR64_VIDEO_SKIPPED;break; }
  if(!ready) {
   ready=!decode_step(f,&decode_ticks);
   if(!ready)continue;
  }
  if(clock<due_us(picture)) {
   /* Nothing to decode or show until the next service tick. */
   uint32_t since=(uint32_t)(sys_time_us()-f->io_phase);
   tr64_film_idle(since<TR64_FRAME_US ? TR64_FRAME_US-since : 0);
   continue;
  }
  decode_us=sys_profile_us(decode_ticks);decode_ticks=0;
  if(decode_us>1000000u/TR64_FILM_FPS) {
   ++r->slow_pictures;if(++slow_run>r->slow_run_max)r->slow_run_max=slow_run;
  } else slow_run=0;
  r->decode_us_total+=decode_us;if(decode_us>r->decode_us_max)r->decode_us_max=decode_us;
  if(!tr64_film_decoder_next()) { ended=1;request->result=TR64_VIDEO_DONE;break; }
  ++r->pictures;
  if(clock>=due_us(picture+1u)) {
   ++r->dropped;           /* a whole picture period behind: skip this picture */
  } else {
   uint32_t before=sys_time_us(),late;int32_t sync;
   tr64_film_decoder_present(service_context,f);
   before=sys_time_us()-before;if(before>r->present_us_max)r->present_us_max=before;
   ++r->shown;
   clock=film_clock(f);sync=(int32_t)(clock-due_us(picture));
   if(sync<r->sync_min_us)r->sync_min_us=sync;
   if(sync>r->sync_max_us)r->sync_max_us=sync;
   late=sync>0 ? (uint32_t)sync : 0;if(late>r->late_us_max)r->late_us_max=late;
   if(sync<0 && (uint32_t)-sync>r->early_us_max)r->early_us_max=(uint32_t)-sync;
  }
  ++picture;ready=0;
 }
 r->film_us=tr64_film_audio_us();r->wall_us=(uint32_t)(sys_time_us()-wall_start);
 r->audio_underruns=tr64_film_audio_underruns();
 tr64_film_decoder_close();
 tr64_film_audio_end();
 drain_stream(f);
 if(f->arena.live)sys_error("video decoder leaked %lu allocations",(unsigned long)f->arena.live);
 r->io_bytes=f->stream_bytes;r->arena_peak=f->arena.peak;r->arena_bytes=f->arena.size;r->arena_allocs=f->arena.allocs;
 (void)request->result;
}
uint32_t tr64_unit_video(uint32_t argument)
{
 tr64_film_request_t *request=(tr64_film_request_t *)(uintptr_t)argument;
 if(!request)sys_error("video unit: missing request");
 request->result=TR64_VIDEO_DONE;
 play(request);
 return request->result;
}
