/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/save_v3.h, game.h and item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_SAVE_PRIVATE_H
#define TR64_GAME_SAVE_PRIVATE_H
#include "types.h"
#include "constants.h"
enum
{
    GAME_SAVE_SIZE,
    GAME_SAVE_WRITE,
    GAME_SAVE_LOAD
};
enum
{
    GAME_SAVE_CHECK,
    GAME_SAVE_APPLY,
    GAME_SAVE_FINISH
};
enum
{
    GAME_SAVE_HEADER_BYTES = 32,
    GAME_SAVE_FORMAT_VERSION = 3,
    GAME_SAVE_SOURCE_HEADER_BYTES = 230,
    GAME_SAVE_BASE_BYTES = 22
};
typedef struct
{
    int16 x, y, z, ax, ay;
    uint16 animIndex, frameIndex, flags, timer;
    uint8 state, nextState, goalState, roomIndex;
} GameSaveItem;
typedef struct
{
    SaveGame header;
    uint16 itemOffsets[MAX_ITEMS];
    uint16 flipOffset, childOffset, childSize;
    uint32 totalBytes, coreIdentity, textureIdentity;
} GameSavePlan;
typedef struct
{
    uint32 op;
    void *buffer;
    uint32 bytes, result;
    uint32 phase;
    GameSavePlan *plan;
} GameSaveCall;
/* CHECK has no GS side effects and fills a caller-owned resident plan.
 * Resident orchestration then validates derived doors/blocks, prepares the
 * current Core in place using the source save-preparation scope, and invokes
 * APPLY. Rebuild source derived state/lists/activation outside the unit before
 * FINISH restores track and both RNG seeds. No unit invokes another unit.
 * Header is TRS3/version3/32B/total/CRC32/CoreCRC32/TXCRC32/sourceHeaderBytes.
 * All fields are BE and source-persistent only; no snapshot/v1/v2 shim. */
uint32_t tr64_unit_save(uint32_t argument);
/* Pure resident accessor over a CHECKed file, for game.h:220-291 validation. */
static inline uint16 game_save_u16(const uint8 *p)
{
    return (uint16)((uint16)p[0] << 8 | p[1]);
}
static inline bool game_save_item_base(const void *data, uint32 bytes, uint32 offset,
                                       GameSaveItem *out)
{
    const uint8 *p;
    if (!data || !out || offset > bytes || GAME_SAVE_BASE_BYTES > bytes - offset)
        return false;
    p = (const uint8 *)data + offset;
    out->x = wrap_i16(game_save_u16(p));
    out->y = wrap_i16(game_save_u16(p + 2));
    out->z = wrap_i16(game_save_u16(p + 4));
    out->ax = wrap_i16(game_save_u16(p + 6));
    out->ay = wrap_i16(game_save_u16(p + 8));
    out->animIndex = game_save_u16(p + 10);
    out->frameIndex = game_save_u16(p + 12);
    out->flags = game_save_u16(p + 14);
    out->timer = game_save_u16(p + 16);
    out->state = p[18];
    out->nextState = p[19];
    out->goalState = p[20];
    out->roomIndex = p[21];
    return true;
}
/* The two identities a save header carries: CRC32 of all Core tables, CRC32 of the texture block. Defined in game.c
 * (resident): the `save` unit is paged and levelStart runs before it is loaded. */
void game_save_identities(const tr64_level_t *core,uint32 *tables,uint32 *textures);
#endif
