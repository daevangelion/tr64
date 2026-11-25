/* SPDX-License-Identifier: MIT */
/* Original production-API source-save regression; no whole-state snapshot. */
#include "game/internal.h"
#include "game/save.h"
#include "tr64/res.h"
#include <string.h>
extern uint32_t game_save(void *, uint32_t);
extern int game_load(const void *, uint32_t);
static uint32_t word(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         p[3];
}
static void put(uint8_t *p, uint32_t n) {
  p[0] = (uint8_t)(n >> 24);
  p[1] = (uint8_t)(n >> 16);
  p[2] = (uint8_t)(n >> 8);
  p[3] = (uint8_t)n;
}
static uint32_t liveDigest(void) {
  /* Hash-only negative control: no GS dump is produced or retained. */
  return tr64_crc32(0, (const uint8_t *)gs, gs->bytes);
}
static int rejected(const uint8_t *data, uint32_t bytes) {
  uint32_t before = liveDigest();
  return !game_load(data, bytes) && liveDigest() == before;
}
static uint32_t worldDigest(void) {
  uint32_t count = tr64_level_table(gs->core, TR64_CORE_sectors)->count;
  uint32_t crc =
      tr64_crc32(0, (const uint8_t *)gs->sectorState, count * sizeof(Sector));
  for (uint32_t i = 0; i < gs->level.roomsCount; i++)
    crc = tr64_crc32(crc, (const uint8_t *)&gs->rooms[i].source,
                     sizeof(gs->rooms[i].source));
  return crc;
}
static int changedField(const uint8_t *source, uint8_t *changed, uint32_t size,
                        uint32_t offset, uint16_t value, bool byteField) {
  memcpy(changed, source, size);
  if (byteField)
    changed[offset] = (uint8_t)value;
  else {
    changed[offset] = (uint8_t)(value >> 8);
    changed[offset + 1] = (uint8_t)value;
  }
  put(changed + 12, tr64_crc32(0, changed + 32, size - 32));
  return rejected(changed, size);
}
static int derivedNegatives(const uint8_t *source, uint8_t *changed,
                            uint32_t size) {
  uint32_t offset = GAME_SAVE_HEADER_BYTES + GAME_SAVE_SOURCE_HEADER_BYTES;
  for (uint32_t i = 0; i < gs->level.itemsCount; i++) {
    if (offset > size || size - offset < 3)
      return 0;
    uint32_t type = source[offset],
             payload = game_save_u16(source + offset + 1), base = offset + 3;
    if (payload < GAME_SAVE_BASE_BYTES || payload > size - base)
      return 0;
    if (type >= ITEM_DOOR_1 && type <= ITEM_DOOR_8) {
      const uint32_t fields[] = {0, 4, 8};
      for (uint32_t j = 0; j < 3; j++)
        if (!changedField(source, changed, size, base + fields[j],
                          game_save_u16(source + base + fields[j]) ^ 1u, false))
          return 0;
      if (gs->level.roomsCount > 1 &&
          !changedField(source, changed, size, base + 21,
                        (source[base + 21] + 1u) % gs->level.roomsCount, true))
        return 0;
    }
    if (type >= ITEM_BLOCK_1 && type <= ITEM_BLOCK_4) {
      if (!changedField(source, changed, size, base, UINT16_MAX, false) ||
          !changedField(source, changed, size, base + 4, UINT16_MAX, false))
        return 0;
      const RoomInfo *r = tr64_level_rooms(gs->core, source[base + 21]);
      uint32_t x = tr64_u16(r->x_sectors), z = tr64_u16(r->z_sectors);
      if (x < 32 && !changedField(source, changed, size, base,
                                  (uint16_t)(x << 10), false))
        return 0;
      if (z < 32 && !changedField(source, changed, size, base + 4,
                                  (uint16_t)(z << 10), false))
        return 0;
    }
    offset = base + payload;
  }
  return 1;
}
/* Caller owns >=2*game_save(NULL,0)+32 bytes in an approved idle pool. The
 * oracle comparison of source Save/Load directives is an integration gate;
 * unsaved continuation is deliberately NOT the source save contract. */
int tr64_game_save_check(void *memory, uint32_t capacity) {
  uint8_t *original = memory, *candidate;
  uint32_t size = game_save(NULL, 0), half = capacity / 2, i, before;
  if (!memory ||
      size < GAME_SAVE_HEADER_BYTES + GAME_SAVE_SOURCE_HEADER_BYTES ||
      half < size + 16)
    return 0;
  candidate = original + half;
  memset(memory, 0xA5, capacity);
  before = liveDigest();
  if (game_save(NULL, 0) != size || liveDigest() != before ||
      game_save(original, size - 1) != size || liveDigest() != before)
    return 0;
  for (i = 0; i < capacity; i++)
    if (original[i] != 0xA5)
      return 0;
  if (game_save(original, half) != size || game_save(candidate, half) != size ||
      memcmp(original, candidate, size))
    return 0;
  for (i = size; i < half; i++)
    if (original[i] != 0xA5 || candidate[i] != 0xA5)
      return 0;
  if (word(original + 8) != size)
    return 0;
  for (i = 0; i < GAME_SAVE_HEADER_BYTES; i++) {
    memcpy(candidate, original, size);
    candidate[i] ^= 1;
    if (!rejected(candidate, size))
      return 0;
  }
  for (i = 0; i < size; i += (i < 263 ? 1 : 1024))
    if (!rejected(original, i))
      return 0;
  memcpy(candidate, original, size);
  candidate[size - 1] ^= 1;
  if (!rejected(candidate, size))
    return 0;
  /* Valid CRC but invalid source room reference: validates before reinit. */
  memcpy(candidate, original, size);
  candidate[GAME_SAVE_HEADER_BYTES + GAME_SAVE_SOURCE_HEADER_BYTES + 3 + 21] =
      255;
  put(candidate + 12, tr64_crc32(0, candidate + 32, size - 32));
  if (!rejected(candidate, size))
    return 0;
  /* The unused source inventory slots are required to be zero. */
  memcpy(candidate, original, size);
  candidate[GAME_SAVE_HEADER_BYTES + GAME_SAVE_SOURCE_HEADER_BYTES - 1] = 1;
  put(candidate + 12, tr64_crc32(0, candidate + 32, size - 32));
  if (!rejected(candidate, size))
    return 0;
  if (!derivedNegatives(original, candidate, size))
    return 0;
  if (!game_load(original, size) || game_save(candidate, half) != size ||
      memcmp(original, candidate, size))
    return 0;
  before = worldDigest();
  if (!game_load(original, size) || worldDigest() != before)
    return 0;
  return 1;
}
