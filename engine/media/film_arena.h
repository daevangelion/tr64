/* SPDX-License-Identifier: MIT */
/* The video unit's allocator over the borrowed level arena: a bump allocator
 * (fixed-lifetime state and the decoder's long-lived blocks) plus exact-size
 * reuse of freed small blocks. The decoder allocates, compares and frees a
 * parameter set (960 B of VUI) at every keyframe: without reuse that grew the
 * arena by 960 B for every keyframe in the stream. Header-only so the host test
 * (test/media/arena.c) builds the same code the unit runs. No heap. */
#ifndef TR64_FILM_ARENA_H
#define TR64_FILM_ARENA_H
#include <stddef.h>
#include <stdint.h>
#include "tr64/eng.h"
#define FILM_ARENA_BLOCKS 64u
/* The RSP motion compensation reads rows outside the picture for a motion vector that
 * points off the frame (x264 allows up to 24 rows past the edge, plus the 6-tap margin:
 * 27 luma rows of 320 bytes = 8,640 B). 16 KiB covers that with room; a picture is 115,248 B. */
#define FILM_GUARD 16384u
#define FILM_GUARDED 65536u
typedef struct { uint32_t offset,bytes;uint8_t live; } film_block_t;
typedef struct {
 uint8_t *base;uint32_t size,used,peak,live,allocs,blocks;
 film_block_t table[FILM_ARENA_BLOCKS];
} film_arena_t;
static inline void film_arena_init(film_arena_t *a,void *base,uint32_t size,uint32_t used)
{
 a->base=base;a->size=size;a->used=used;a->peak=used;a->live=a->allocs=a->blocks=0;
}
/* Fixed-lifetime memory: never freed individually, returned with the arena. */
static inline void *film_arena_take(film_arena_t *a,uint32_t bytes,uint32_t alignment)
{
 uint32_t start=(a->used+alignment-1u)&~(alignment-1u);
 if(alignment&(alignment-1u) || start<a->used || bytes>a->size-start)
  sys_error("video arena overflow: used=%lu request=%lu of %lu",(unsigned long)a->used,
   (unsigned long)bytes,(unsigned long)a->size);
 a->used=start+bytes;if(a->used>a->peak)a->peak=a->used;
 return a->base+start;
}
/* video_allocator_t: the decoder's allocations. */
static inline void *film_arena_alloc(void *context,size_t bytes,size_t alignment)
{
 film_arena_t *a=context;void *p;uint32_t i,guard;
 if(!bytes || !alignment || bytes>a->size || alignment>4096u)sys_error("video decoder allocation request");
 /* Blocks of picture size get a FILM_GUARD band before and after. For a motion
  * vector that points past the frame edge the RSP DMAs the rows outside the picture
  * (above the first, below the last) and then overwrites them with the replicated
  * edge row. Whatever the arena holds there is read but never used, yet with the DPB
  * and macroblock tables right next to a picture, which the CPU keeps dirty in its
  * data cache, ares reports "RSP reading ... not cache coherent". Every decoded
  * picture is bit-identical with and without the guard (docs/formats/n64-video.md);
  * the guard keeps the stale data out of the RSP's reach. */
 if(bytes<FILM_GUARDED)
  for(i=0;i<a->blocks;++i)
   if(!a->table[i].live && a->table[i].bytes==bytes && a->table[i].offset%alignment==0) {
    a->table[i].live=1;++a->live;++a->allocs;
    return a->base+a->table[i].offset;
   }
 if(a->blocks==FILM_ARENA_BLOCKS)sys_error("video decoder block table full (%u)",FILM_ARENA_BLOCKS);
 guard=bytes>=FILM_GUARDED ? FILM_GUARD : 0;
 p=(uint8_t *)film_arena_take(a,(uint32_t)bytes+2u*guard,(uint32_t)alignment)+guard;
 a->table[a->blocks].offset=(uint32_t)((uint8_t *)p-a->base);a->table[a->blocks].bytes=(uint32_t)bytes;
 /* A guarded block (a picture) is never reused: its neighbours are its guards. */
 a->table[a->blocks].live=1;++a->blocks;++a->live;++a->allocs;
 return p;
}
static inline void film_arena_free(void *context,void *memory)
{
 film_arena_t *a=context;uint32_t i;
 if(!memory)return;
 for(i=0;i<a->blocks;++i)
  if(a->base+a->table[i].offset==(uint8_t *)memory && a->table[i].live) {
   a->table[i].live=0;--a->live;
   /* A freed guarded block stays dead: the exact-size search below skips it. */
   if(a->table[i].bytes>=FILM_GUARDED)a->table[i].bytes=0;
   return;
  }
 sys_error("video decoder freed foreign memory");
}
#endif
