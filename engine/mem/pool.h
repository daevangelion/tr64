/* SPDX-License-Identifier: MIT */
#ifndef TR64_POOL_H
#define TR64_POOL_H
#include <stdint.h>
/* Fixed slots; bitmap storage is caller-owned resident state. */
typedef struct {
 int pool; uint8_t *base; uint32_t slot_bytes, count, used, cursor;
 uint32_t *bits;
} mem_slots_t;
void mem_slots_init(mem_slots_t *slots,int pool,uint32_t slot_bytes,uint32_t *bits,uint32_t words);
void *mem_slots_take(mem_slots_t *slots,uint32_t count,uint32_t alignment);
/* Align base+offset while charging only count slots (e.g. allocation header). */
void *mem_slots_take_offset(mem_slots_t *slots,uint32_t count,uint32_t alignment,uint32_t offset);
/* Stable name for pool diagnostics, including during native level admission. */
const char *mem_level_name(void);
void mem_slots_release(mem_slots_t *slots,void *base,uint32_t count);
/* Internal final step of eng_level_release, after published views, planner and
 * unit mappings have been invalidated. Never a public game reset shortcut. */
void mem_level_end(void);
#endif
