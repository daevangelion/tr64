/* SPDX-License-Identifier: MIT */
#ifndef TR64_MEM_H
#define TR64_MEM_H
#include <stdint.h>
#include <stddef.h>
#include "tr64/limits.h"
#ifndef TR64_GAME
#define TR64_GAME 1
#endif
#if TR64_GAME != 1 && TR64_GAME != 2 && TR64_GAME != 3
#error "TR64_GAME must be 1, 2, or 3"
#endif
#if TR64_GAME == 1 || defined(TR64_HOST)
#define TR64_POOL_ROWS "tr64/pools.def"
#elif TR64_GAME == 2
#define TR64_POOL_ROWS "tr64/pools-tr2.def"
#else
#define TR64_POOL_ROWS "tr64/pools-tr3.def"
#endif
enum {
#define TR64_POOL(id, kib, kind) TR64_MEM_##id,
#include TR64_POOL_ROWS
#undef TR64_POOL
 TR64_MEM_COUNT
};
enum { TR64_IMAGE, TR64_FIXED, TR64_CARVE };
enum {
#define TR64_POOL(id, kib, kind) TR64_BYTES_##id = kib * 1024u,
#include TR64_POOL_ROWS
#undef TR64_POOL
 TR64_BYTES_END = 0
};
/* The unused row budget is reserve, independent of linker placement. */
typedef char tr64_declared_pools_exceed_rdram[((UINT64_C(0)
#define TR64_POOL(id, kib, kind) + (kib) * UINT64_C(1024)
#include TR64_POOL_ROWS
#undef TR64_POOL
 ) <= TR64_RDRAM_BYTES) ? 1 : -1];
/* Fixed layout order inside the per-level arena; these are sections, not
 * additional pools or independently reserved worst-case budgets. */
enum {
 TR64_LEVEL_core, TR64_LEVEL_tex, TR64_LEVEL_rooms, TR64_LEVEL_meshes,
 TR64_LEVEL_COUNT
};
typedef struct {
 const char *name; uint8_t kind; uint8_t *base;
 uint32_t budget, used, peak;
} tr64_mem_stats_t;
/* Sole carve entry: all CARVE rows, once in file order. Capacity is checked
 * before any pointer is published. Platform lazy boot calls this at most once. */
void mem_boot_carve(void *region, uint32_t capacity);
#ifdef TR64_HOST
/* The validated directory is the only live I/O allocation at game selection.
 * Return its address in the new layout, before any section-table read. */
uint8_t *mem_host_level_game(unsigned game, uint8_t *directory, uint32_t bytes);
#endif
int mem_is_booted(void);
uint32_t mem_carve_bytes(void);
uint32_t mem_budget(int pool);
void mem_fixed(int pool, void *base, uint32_t bytes);
void *tr64_mem_alloc(int pool, uint32_t bytes);
/* Claim an empty CARVE pool for the boot library allocator; bump/reset/limit
 * are then forbidden, preventing mixed allocators from overlapping it. */
void mem_library_claim(int pool);
/* Named boot-state borrow of level; invalid once level residency begins. */
void *mem_borrow_boot_card(void);
/* Exclusive no-level VIDEO borrow: the full level row, 16-byte aligned.
 * Decoder owns subarena accounting; close it, drain I/O and unload its unit
 * before returning this exact base. Never thaws the library heap. */
void *mem_video_begin(uint32_t *capacity);
void mem_video_end(void *base);
/* Single-scene level residency; mutually exclusive with named arena borrows. */
void mem_level_begin(void);
/* Plan once from validated pack-directory sizes, before any payload is
 * decoded. Core includes its per-kind 16-byte alignment; the other sizes are
 * exact logical-kind decoded sizes. Starts/reservations are aligned to 16.
 * A remount must name the same level and sizes; it never resets allocations. */
void mem_level_plan(const char *name,const uint32_t bytes[TR64_LEVEL_COUNT]);
void *mem_level_alloc(int section,uint32_t bytes);
void mem_level_stats(int section,tr64_mem_stats_t *out);
void tr64_mem_reset(int pool);
void tr64_mem_stats(int pool, tr64_mem_stats_t *out);
void tr64_mem_log(void);
void mem_account(int pool, uint32_t used);
/* Reduce an unused pool by exactly one byte, for a production overflow gate. */
void mem_limit(int pool, uint32_t capacity);
#endif
