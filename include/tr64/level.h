/* SPDX-License-Identifier: MIT */
#ifndef TR64_LEVEL_H
#define TR64_LEVEL_H
#include <stdint.h>
#include "tr64/pack.h"
/* Naturally aligned encoded words: identical wire layout on host and cart.
 * Cart access is a native load; the host swaps only the scalar being used.
 * There is no host-order shadow copy and no load-time pointer fixup. */
typedef struct { uint16_t raw; } tr64_u16_be_t;
typedef struct { uint16_t raw; } tr64_i16_be_t;
typedef struct { uint32_t raw; } tr64_u32_be_t;
typedef struct { uint32_t raw; } tr64_i32_be_t;
static inline uint16_t tr64_core_be16(uint16_t n)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
 return n;
#else
 return (uint16_t)(n>>8 | n<<8);
#endif
}
static inline uint32_t tr64_core_be32(uint32_t n)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
 return n;
#else
 return n>>24 | (n>>8&0xff00u) | (n<<8&0xff0000u) | n<<24;
#endif
}
static inline uint16_t tr64_u16(tr64_u16_be_t v) { return tr64_core_be16(v.raw); }
static inline int16_t tr64_i16(tr64_i16_be_t v)
{ uint32_t n=tr64_core_be16(v.raw); return (int16_t)((int32_t)(n&32767)-(int32_t)(n&32768)); }
static inline uint32_t tr64_u32(tr64_u32_be_t v) { return tr64_core_be32(v.raw); }
static inline int32_t tr64_i32(tr64_i32_be_t v)
{ uint32_t n=tr64_core_be32(v.raw); return n<=INT32_MAX ? (int32_t)n : -(int32_t)(~n)-1; }
#include "core_schema.h"
/* Native box overlap heads and adjacency words share their 14-bit
 * index. The no-list marker is valid only in a box's head word. */
enum { TR64_BOX_INDEX_MASK=0x3fff, TR64_BOX_OVERLAP_END=0x8000,
       TR64_BOX_NO_OVERLAP=UINT16_MAX };
typedef struct {
 const uint8_t *base, *data;
 uint32_t count, bytes, stride, present;
} tr64_level_table_t;
typedef struct {
 uint64_t bus_us;
 uint32_t total_us,unit_load_us,decode_us,hash_us,validation_us;
 uint32_t bytes_read,staging_peak;
} tr64_load_stats_t;
typedef struct tr64_level {
 tr64_pack_t *pack;
 tr64_level_table_t tables[TR64_CORE_TABLES];
 const uint8_t *textures;
 uint32_t texture_bytes;
 uint16_t loading,game; /* game=0: legacy TR1; game=2: TR2 render-only. */
 uint32_t core_loaded,ready;
 /* Optional native bank and pack index live in caller-owned audio storage. */
 const uint8_t *sound_metadata;
 uint32_t sound_metadata_bytes;
 tr64_pack_stream_t sound_stream;
 const uint8_t *title_image;
 uint32_t title_image_bytes;
 tr64_load_stats_t stats;
} tr64_level_t;
/* Resident-thread lease for cold loading. Balanced scopes may nest. The
 * outer scope loads `load` once and unloads it on end; no unit pointer escapes.
 * Incremental pack/core operations require a caller-held lease. Pure getters
 * remain resident and require no lease after publication. */
void tr64_load_scope_begin(void);
void tr64_load_scope_end(void);
/* Bind a fresh scene and leave its outer lease open through game admission. */
void tr64_scene_load_held(const char *name);
void tr64_level_begin(tr64_level_t *level,tr64_pack_t *pack);
int tr64_level_poll(tr64_level_t *level);
/* After core prefetch: load TEX with pack_begin_kind into tex, then finish.
 * Finish validates both domains and releases the temporary io lease. */
void tr64_level_finish(tr64_level_t *level);
/* The production loading-state driver: back-to-back async I/O, core + TX64,
 * complete validation and io release before it returns. It holds its own
 * nested lease, so standalone calls are valid. The platform must already bind
 * this pack and initialize engine/io to its size. stats.total_us brackets this
 * Core/TX call; unit_load_us is the cost caused by this call (0 if nested).
 * The outer scope's log separately brackets the complete loading transaction. */
void tr64_level_load(tr64_level_t *level,uint32_t bytes,const char *name);
/* Additional native floor-program validation before admitting TR2 gameplay.
 * Render-only bindings do not execute or admit those programs. */
void tr64_level_validate_tr2_gameplay(const tr64_level_t *level);
const tr64_level_t *tr64_level_current(void);
/* Optional validated UI64 span; absent/not-ready returns NULL and zero bytes. */
const uint8_t *tr64_level_title_image(const tr64_level_t *level,uint32_t *bytes);
/* Resource invalidation only, no pool/planner/unit reset. Caller has stopped
 * game use and drained/invalidated renderer references, within the shared
 * non-dispatch code-loading release transaction. Rejects an active resource
 * load lease, pending I/O or retained exclusive I/O scratch. Afterwards current
 * remains an addressable empty view (ready=0), with all resident pointers zero. */
void tr64_level_release(void);
/* Signed C99 quadrant rule used when copying item/instance state. */
static inline uint32_t tr64_level_quadrant(tr64_i16_be_t angle)
{ return (uint32_t)(tr64_i16(angle)/16384+2)&3u; }
/* Count/presence remain separately visible even when an empty table is valid. */
const tr64_level_table_t *tr64_level_table(const tr64_level_t *level,uint32_t kind);
const void *tr64_level_at(const tr64_level_t *level,uint32_t kind,uint32_t index);
const char *tr64_level_table_name(uint32_t kind);
/* mesh_spheres uses exactly mesh_pointers' slot domain (models.mesh_start +
 * bone index), including aliases. x/y/z are original signed local centres;
 * radius preserves the raw source word. The fixed game sign-extends that word
 * for Mesh.radius; it is not a geometry AABB-derived radius. */
#define TR64_CORE_ACCESSOR(name,kind,stride) \
 static inline const tr64_core_##name##_t *tr64_level_##name(const tr64_level_t *l,uint32_t i) \
 { return (const tr64_core_##name##_t *)tr64_level_at(l,kind,i); }
TR64_CORE_SCHEMA(TR64_CORE_ACCESSOR)
#undef TR64_CORE_ACCESSOR
#endif
