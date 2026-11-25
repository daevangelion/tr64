/* SPDX-License-Identifier: MIT */
/* Internal contract between the resident video orchestrator (cinema.c), the
 * `video` code unit (film.c) and the platform decoders (plat/<p>/film.c). */
#ifndef TR64_FILM_H
#define TR64_FILM_H
#include <stddef.h>
#include <stdint.h>
#include "tr64/pack.h"
#include "tr64/video.h"
#define TR64_FILM_FPS 15u
/* Argument of the unit entry; every pointer is valid for the whole call. */
typedef struct {
 uint8_t *arena;uint32_t arena_bytes;
 tr64_pack_stream_source_t video,film;
 uint32_t pack_bytes,max_ticks,result;
 tr64_video_report_t report;
} tr64_film_request_t;
/* Same shape as the SDK's video_allocator_t / video_source_t: the cart backend
 * hands them to libdragon unchanged. */
typedef struct {
 void *context;
 void *(*alloc)(void *context,size_t bytes,size_t alignment);
 void (*free)(void *context,void *memory);
} tr64_film_memory_t;
typedef struct {
 void *context;
 int (*read)(void *context,void *memory,unsigned bytes);
 int (*seek)(void *context,uint32_t offset);
} tr64_film_source_t;
typedef struct { uint32_t width,height;uint32_t fps_x1000; } tr64_film_info_t;
/* Longest the unit waits for anything (an index, a stream block, a display
 * buffer, the RDP, the film clock to advance) before it stops by name. */
#define TR64_FILM_STALL_US 8000000u
/* Decoder backend, called only from inside the unit. Every allocation of the
 * decoder, its pictures and the colour conversion goes through memory; every
 * byte of the stream through source. service() keeps audio and I/O running and
 * is called whenever a backend call would otherwise wait. */
typedef void (*tr64_film_service_t)(void *context);
int tr64_film_decoder_open(const tr64_film_memory_t *memory,const tr64_film_source_t *source,
 tr64_film_info_t *info);
/* One bounded slice of background decoding: 1 = more work remains, 0 = the
 * next picture is complete (or the stream ended). */
int tr64_film_decoder_step(void);
/* Take the next decoded picture, releasing the previous one: 1 taken, 0 end. */
int tr64_film_decoder_next(void);
void tr64_film_decoder_present(tr64_film_service_t service,void *context);
void tr64_film_decoder_close(void);
/* The unit has nothing to do for up to `us` microseconds (the next picture is not
 * due, the next service tick is). Cart: nothing, real time passes. Host: a test
 * clock that skips the time, so a film plays as fast as it decodes. */
void tr64_film_idle(uint32_t us);
/* Film sound beside the music bus (film_audio.c, resident). The state is a
 * caller-provided, 16-byte-aligned block of tr64_film_audio_bytes(). */
uint32_t tr64_film_audio_bytes(void);
void tr64_film_audio_begin(void *state,const tr64_pack_stream_source_t *source,uint32_t pack_bytes);
/* 1 once the stream is primed and may start; polls the transport. */
int tr64_film_audio_pump(void);
void tr64_film_audio_start(void);
/* Position of the samples handed to the mixer, microseconds of film time. */
uint32_t tr64_film_audio_us(void);
int tr64_film_audio_ended(void);
uint32_t tr64_film_audio_underruns(void);
/* Drains outstanding stream DMA; afterwards no pointer into the state remains. */
void tr64_film_audio_end(void);
const int16_t *tr64_film_mono_pcm(uint32_t frames,uint32_t rate);
#endif
