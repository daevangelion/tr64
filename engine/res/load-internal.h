/* SPDX-License-Identifier: MIT */
#ifndef TR64_LOAD_INTERNAL_H
#define TR64_LOAD_INTERNAL_H
#include "tr64/level.h"
#include "tr64/game.h"
#include "engine/core/sys.h"
#define TR64_PACK_RETAINED_ENTRY_BYTES 32u
enum { LOAD_MOUNT,LOAD_POLL,LOAD_KIND,LOAD_UNMOUNT,LOAD_CORE_BEGIN,
       LOAD_CORE_POLL,LOAD_FINISH,LOAD_LEVEL,LOAD_STREAM_INDEX,LOAD_CATALOG_MOUNT,
       LOAD_VALIDATE_TR2,
       LOAD_RENDER_FIRST=0x100 };
typedef struct {
 uint32_t operation,bytes,kind;
 int pool;
 const char *name;
 tr64_pack_t *pack;
 tr64_level_t *level;
 tr64_pack_stream_block_t *blocks;
 uint32_t capacity;
 tr64_pack_stream_t *stream;
#if TR64_HAS_TR2
 const tr64_level_t *readonly_level;
#endif
} tr64_load_call_t;
void load_pack_mount(tr64_pack_t *,uint32_t,const char *);
void load_pack_mount_catalog(tr64_pack_t *,uint32_t);
int load_pack_poll(tr64_pack_t *);
void load_pack_kind(tr64_pack_t *,uint32_t,int);
const void *load_pack_data(const tr64_pack_t *,uint32_t *);
void load_pack_release(tr64_pack_t *);
void load_pack_stream_index(tr64_pack_t *,uint32_t,tr64_pack_stream_block_t *,uint32_t,tr64_pack_stream_t *);
void load_core_begin(tr64_level_t *,tr64_pack_t *);
int load_core_poll(tr64_level_t *);
void load_finish(tr64_level_t *);
void load_validate_core(tr64_level_t *);
#if TR64_HAS_TR2
void load_validate_tr2_gameplay(const tr64_level_t *);
#endif
void load_validate_texture(tr64_level_t *);
void load_sound_prepare(tr64_level_t *,tr64_pack_t *);
void load_sound_validate(const tr64_level_t *);
void load_sound_bind(const tr64_level_t *);
void load_image_prepare(tr64_level_t *,tr64_pack_t *);
void load_io_pump(void);
int load_lz4(const uint8_t *,uint32_t,uint8_t *,uint32_t);
#endif
