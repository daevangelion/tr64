/* SPDX-License-Identifier: MIT */
#ifndef TR64_N64_HEAP_H
#define TR64_N64_HEAP_H
#include "tr64/mem.h"
/* N64 boot only, before sys_heap_freeze. Wrap SDK initialization AND the
 * permanent engine buffers. Scope cannot nest; gpu and audio are routable.
 * All allocations in the claimed pool use this allocator: do not mix bump
 * allocation/reset with it. Free/realloc use the original owning pool.
 * Header=16 bytes; positive align16 charge=round_up(payload+16,16).
 * Alignment gaps remain free slots. Bitmaps/descriptors belong to IMAGE.
 * After freeze every malloc/free/realloc attempt is fatal, in every build. */
void sys_heap_boot_pool_begin(int pool);
void sys_heap_boot_pool_end(void);
#endif
