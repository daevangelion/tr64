/* SPDX-License-Identifier: MIT */
/* libdragon H.264 decoder behind engine/media/film.h. Part of the `video` unit.
 * Every allocation goes through the caller's arena (video_allocator_t), every
 * stream byte through its source; the heap stays frozen. */
#include <libdragon.h>
#include <h264.h>
#include "engine/media/film.h"
#include "engine/core/sys.h"
#include "plat/n64/gpu-platform.h"
#define INTERLEAVE_BYTES (TR64_WIDTH*TR64_HEIGHT/2)
static video_t *video;
static video_allocator_t allocator;
static video_source_t stream;
static tr64_film_memory_t arena;
static yuv_colorspace_t colorspace;
static void *interleave;
static int registered;
static void blit_done(void *flag) { *(volatile uint32_t *)flag=1; }
int tr64_film_decoder_open(const tr64_film_memory_t *memory,const tr64_film_source_t *source,
 tr64_film_info_t *info)
{
 video_info_t found;
 if(video)sys_error("film decoder already open");
 /* A second registration would link the sole codec to itself. */
 if(!registered) { video_register_codec(&h264_codec);registered=1; }
 arena=*memory;
 allocator=(video_allocator_t){memory->context,memory->alloc,memory->free};
 stream=(video_source_t){source->context,source->read,source->seek};
 yuv_init();
 interleave=memory->alloc(memory->context,INTERLEAVE_BYTES,16);
 yuv_set_interleave_buffer(interleave,INTERLEAVE_BYTES);
 /* One decoded picture waits ahead: poll stops there, so audio keeps running
  * while the next picture is not yet due. The name only selects the codec. */
 video=video_open("film.h264",&(video_parms_t){.buffered_pics=1,.allocator=&allocator,.source=&stream});
 if(!video)return 0;
 found=video_get_info(video);
 colorspace=found.colorspace;
 info->width=found.width;info->height=found.height;
 info->fps_x1000=(uint32_t)(found.framerate*1000.0f+0.5f);
 return 1;
}
int tr64_film_decoder_step(void) { return video_poll(video); }
int tr64_film_decoder_next(void) { return video_next_frame(video); }
void tr64_film_decoder_present(tr64_film_service_t service,void *context)
{
 static volatile uint32_t done;
 yuv_frame_t frame=video_get_frame(video);
 surface_t *surface;
 uint32_t waited=sys_time_us();
 while(!(surface=display_try_get())) {
  service(context);
  if((uint32_t)(sys_time_us()-waited)>TR64_FILM_STALL_US)sys_error("video stalled waiting for a display buffer");
 }
 rdpq_attach(surface,NULL);
 yuv_tex_blit(&frame,0,0,NULL,&colorspace);
 done=0;rdpq_detach_cb(blit_done,(void *)&done);rspq_flush();
 /* The RDP has read the picture once its interrupt fires; only then may the
  * next video_next_frame hand the picture back to the decoder. */
 waited=sys_time_us();
 while(!done) {
  service(context);
  if((uint32_t)(sys_time_us()-waited)>TR64_FILM_STALL_US)sys_error("video stalled waiting for the RDP");
 }
 sys_gpu_present(surface);
}
void tr64_film_decoder_close(void)
{
 if(!video)return;
 rspq_wait();
 video_close(video);video=NULL;
 yuv_set_interleave_buffer(NULL,0);yuv_close();
 arena.free(arena.context,interleave);interleave=NULL;
}
void tr64_film_idle(uint32_t us) { (void)us; }
