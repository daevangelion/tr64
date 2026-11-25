/* SPDX-License-Identifier: MIT */
#ifndef TR64_SAVE_INTERNAL_H
#define TR64_SAVE_INTERNAL_H
#include "engine/save/save.h"
enum { SAVE_SCAN=1,SAVE_WRITE,SAVE_READ,SAVE_SETTINGS_WRITE,SAVE_SETTINGS_READ };
typedef struct {
 uint32_t operation,slot,bytes,capacity;
 const void *source;
 void *destination;
 const tr64_save_info_t *input_info;
 tr64_save_info_t *output_info;
 uint32_t *output_bytes;
} tr64_save_call_t;
typedef struct {
 uint32_t scanned,valid_slots;
 /* The newest valid directory: the region of each slot (0xff: none), its sequence and copy. */
 uint8_t region[TR64_SAVE_SLOTS];
 uint32_t sequence;
 unsigned copy;
 int directory_status,slot_status[TR64_SAVE_SLOTS];
 tr64_save_info_t info[TR64_SAVE_SLOTS];
} tr64_save_cache_t;
extern tr64_save_cache_t tr64_save_cache;
#ifdef TR64_HOST
extern const char *tr64_save_host_path;
#endif
/* Platform transfers are exact, even-sized and16B source/destination aligned.
 * Offsets are guest bytes in the external32KiB device, not ROM offsets. */
int tr64_save_platform_init(void);
int tr64_save_platform_read(uint32_t offset,void *data,uint32_t bytes);
int tr64_save_platform_write(uint32_t offset,const void *data,uint32_t bytes);
#endif
