/* SPDX-License-Identifier: MIT */
#ifndef TR64_PACK_H
#define TR64_PACK_H
#include <stdint.h>
#include "tr64/io.h"
#define TR64_PACK_BLOCK 16384u
#define TR64_PACK_TEX 0x10000u
#define TR64_PACK_ROOMS 0x20000u
#define TR64_PACK_MESHES 0x30000u
#define TR64_PACK_SOUND 0x40000u
#define TR64_PACK_IMAGE 0x50000u
#define TR64_PACK_MUSIC 0x60000u
#define TR64_PACK_VIDEO 0x70000u
/* Caller-owned retained stream index; no pointer into mount's I/O lease. */
typedef struct {
 uint32_t logical_offset,rom_offset,raw_bytes,raw_crc32;
} tr64_pack_stream_block_t;
typedef struct {
 const tr64_pack_stream_block_t *blocks;
 uint32_t block_count,total_bytes;
} tr64_pack_stream_t;
/* A catalog retains only this directory reference, not every track's blocks. */
typedef struct {
 uint32_t table_offset,block_count,table_crc32,kind;
} tr64_pack_stream_source_t;
/* Zero-initialize once. Scratch, destination blocks and request remain owned by
 * the caller until READY; only result is published after full wire-table CRC. */
typedef struct {
 tr64_pack_stream_source_t source;
 tr64_pack_stream_t result;
 tr64_pack_stream_block_t *blocks;
 uint8_t *scratch;
 uint32_t pack_bytes,capacity,scratch_bytes,wire_done,total_bytes,crc,state,pending;
 tr64_io_req_t request;
} tr64_pack_stream_query_t;
/* Loading-state object, not retained during play. io is an exclusive temporary
 * lease; unmount releases it. Resident payloads belong to planned level sections. */
typedef struct {
 tr64_io_req_t request;
 uint8_t header[32] __attribute__((aligned(16)));
 uint8_t *directory,*entries,*staging,*output;
 uint32_t bytes,level_index,levels,count,phase,next,end,kind,output_bytes;
 uint32_t pending,io_end,leased,bytes_read,decode_us,hash_us,validation_us;
 uint32_t table_offset,table_read,table_crc,catalog_only;
 uint32_t staging_peak,kind_checksum;
 uint64_t read_us;
 uint32_t read_started,game;
 char name[32];
} tr64_pack_t;
/* Bind the configured campaign byte range, without loading/planning a level.
 * Requires no load scope, pending DMA or io lease; caller has stopped consumers.
 * Returns the real pack extent. Safe at boot or after level release. */
uint32_t tr64_level_pack_bind_streams(void);
/* Caller has bound/initialized engine/io and holds tr64_load_scope_begin()
 * (declared in level.h). Every work operation resolves the cold dispatch;
 * pack_data is a resident pure getter over the completed payload view. */
void tr64_pack_mount(tr64_pack_t *pack,uint32_t bytes,const char *level);
/* Header+directory only: no selected level, retained table, staging or level
 * plan. Export named stream sources, then unmount/unload before video runs. */
void tr64_pack_mount_catalog(tr64_pack_t *pack,uint32_t bytes);
int tr64_pack_poll(tr64_pack_t *pack);
/* Resident kinds use TR64_MEM_level, selecting their planned section by kind;
 * other destination pools remain generic. Mount has already validated/planned
 * the complete resident layout. Caller has entered mem_level_begin(). */
void tr64_pack_begin_kind(tr64_pack_t *pack,uint32_t kind,int pool);
const void *tr64_pack_data(const tr64_pack_t *pack,uint32_t *bytes);
int tr64_pack_has_kind(const tr64_pack_t *pack,uint32_t kind);
/* Loading scope only. Capacity counts records. Rejects compressed blocks;
 * returns an immutable view of caller-owned storage, valid after unmount. */
void tr64_pack_stream_index(tr64_pack_t *pack,uint32_t kind,
 tr64_pack_stream_block_t *blocks,uint32_t capacity,tr64_pack_stream_t *stream);
/* Resident playback helpers; no loading scope or code-unit entry. Buffer must
 * be16-byte aligned, capacity >= even(raw_bytes). Request remains caller-owned;
 * BUSY leaves a prepared IDLE request for retry. Poll terminal completion once. */
int tr64_pack_stream_read(const tr64_pack_stream_block_t *block,tr64_io_req_t *request,
 void *buffer,uint32_t capacity);
int tr64_pack_stream_poll(const tr64_pack_stream_block_t *block,const tr64_io_req_t *request);
/* Directory lookup while mounted, no allocation/dispatch. 1 found,0 absent.
 * Exported references remain tied to this same pack/engine-IO binding. */
int tr64_pack_stream_source(const tr64_pack_t *pack,const char *name,uint32_t kind,
 tr64_pack_stream_source_t *source);
/* Resident nonblocking table lookup; scratch is16-aligned,64-byte-multiple,
 * 64..16384 bytes (normally4096). Capacity counts16-byte records. */
void tr64_pack_stream_query_begin(tr64_pack_stream_query_t *query,
 const tr64_pack_stream_source_t *source,uint32_t pack_bytes,
 tr64_pack_stream_block_t *blocks,uint32_t capacity,void *scratch,uint32_t scratch_bytes);
int tr64_pack_stream_query_poll(tr64_pack_stream_query_t *query);
/*0 while the current DMA is live;1 once invalidated. Never submits a new page.
 * Caller services engine/io/AI while draining; no DMA cancellation exists. */
int tr64_pack_stream_query_drain(tr64_pack_stream_query_t *query);
/* Requires no outstanding request. Releases the exclusive io lease without
 * resetting any resident payload pool. */
void tr64_pack_unmount(tr64_pack_t *pack);
#endif
