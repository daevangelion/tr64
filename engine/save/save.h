/* SPDX-License-Identifier: MIT */
#ifndef TR64_SAVE_H
#define TR64_SAVE_H
#include <stdint.h>
#include "tr64/game.h"
/* Device layout, version 3 (docs/formats/n64-ui.md): four regions for three slots (the spare one
 * makes a save atomic), two copies of the directory, two copies of the settings. */
enum {
 TR64_SAVE_BYTES=32768, TR64_SAVE_SLOTS=3, TR64_SAVE_REGIONS=4, TR64_SAVE_REGION_BYTES=6144,
 TR64_SAVE_HEADER_BYTES=64, TR64_SAVE_PAYLOAD_BYTES=6080,
 TR64_SAVE_DIRECTORY_OFFSET=24576, TR64_SAVE_DIRECTORY_BYTES=128, /* per copy: two, back to back */
 TR64_SAVE_SETTINGS_OFFSET=24832, TR64_SAVE_SETTINGS_COPY_BYTES=3840, TR64_SAVE_SETTINGS_HEADER_BYTES=32,
 TR64_SAVE_SETTINGS_BYTES=3808
};
enum {
 TR64_SAVE_OK, TR64_SAVE_EMPTY, TR64_SAVE_CORRUPT, TR64_SAVE_IO,
 TR64_SAVE_UNAVAILABLE, TR64_SAVE_SPACE, TR64_SAVE_ARGUMENT,
 TR64_SAVE_VERSION, /* a valid slot of an older device layout: not readable */
 TR64_SAVE_FOREIGN  /* intact, but this build's pack cannot load it */
};
typedef struct {
 int32_t level_id;
 tr64_game_level_stats_t stats;
 uint32_t bytes,crc;
 /* Content identity of `level_id` in the pack that wrote the slot (see
  * tr64_save_level_identity); never zero in a slot written by the game. */
 uint32_t identity;
} tr64_save_info_t;
/* Cold medium code is preloaded only in the engine's loading scope. No API
 * below loads/pages implicitly, and medium code never calls Game serializers. */
void tr64_save_unit_load(void);
int tr64_save_scan(void);
/* Native payload pointers are16B aligned. Odd-length payloads are written
 * with a separate padded last halfword; no byte beyond input is read. */
int tr64_save_write(unsigned slot,const tr64_save_info_t *info,const void *data,uint32_t bytes);
/* Buffer capacity covers16B-rounded bytes; pointers are16B aligned on both
 * targets. Return metadata only after both header and payload checks pass. */
int tr64_save_read(unsigned slot,void *data,uint32_t capacity,tr64_save_info_t *info);
int tr64_save_settings_write(const void *data,uint32_t bytes);
int tr64_save_settings_read(void *data,uint32_t capacity,uint32_t *bytes);
/* Pure resident cache: no paging, I/O, mutation or pointer into a unit. */
int tr64_save_any(void);
int tr64_save_cached(unsigned slot,tr64_save_info_t *info);
/* At the loading boundary (no level resident, io pool empty): read the pack
 * directory and record the identity of each named level. Identity of a level
 * in the pack this cart carries is the directory's section-table CRC for the
 * level named by game_level_name(level_id), zero when the pack has no such
 * level. Implemented by the engine (identity.c). */
void tr64_save_identity_bind(const char *const *names,unsigned count);
uint32_t tr64_save_level_identity(int32_t level_id);
/* Cache status of a slot, judged against this pack: OK only for a slot whose
 * recorded level this pack carries with the identity the save recorded and
 * that the game has not already refused to load (tr64_save_refuse). Otherwise
 * EMPTY, CORRUPT, VERSION, FOREIGN or UNAVAILABLE. info is written on OK. */
int tr64_save_slot_status(unsigned slot,tr64_save_info_t *info);
int tr64_save_usable_any(void);
/* The game's own load check rejected this slot's contents: it is FOREIGN until
 * the slot is written again. Survives rescans. */
void tr64_save_refuse(unsigned slot);
#ifdef TR64_HOST
/* Host device path is explicit and borrowed until process exit. Bind before
 * the first scan; no implicit current-directory/user save path is invented. */
int tr64_save_host_bind(const char *path);
#endif
#endif
