/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_RES_H
#define TR64_RES_H
#include <stdint.h>
#define TR64_FILE_MAX 1024u
#define TR64_FILE_HEADER 32u
#define TR64_FILE_ENTRY 16u
typedef struct { const uint8_t *bytes; uint32_t count, cart_size; } tr64_filetable_t;
typedef struct { uint32_t id, offset, size, crc; } tr64_file_t;
uint32_t tr64_crc32(uint32_t crc, const uint8_t *bytes, uint32_t size);
uint32_t tr64_be32(const uint8_t *bytes);
int tr64_filetable_open(tr64_filetable_t *table, const uint8_t *bytes, uint32_t size);
int tr64_file_find(const tr64_filetable_t *table, uint32_t id, tr64_file_t *out);
#endif
