/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

void Nav_init(Nav *self, uint32 boxIndex)
{
    switch (self->stepHeight)
    {
        case 256 : self->zoneType = ZONE_GROUND_1; break;
        case 512 : self->zoneType = ZONE_GROUND_2; break;
        default : self->zoneType = ZONE_FLY;
    }
    self->weight = 0;
    self->endBox = 0xFFFF;
    self->nextBox = 0xFFFF;
    self->headBox = 0xFFFF;
    self->tailBox = 0xFFFF;
    self->mask = BOX_OVERLAP_BLOCKED;
    for (uint32 i = 0; i < gs->level.boxesCount; i++)
    {
        (self->cells[i]).end = 0xFFFF;
        (self->cells[i]).next = 0xFFFF;
        (self->cells[i]).weight = 0;
    }
    const tr64_u16_be_t * defZones = Enemy_zoneTable(0, self->zoneType);
    const tr64_u16_be_t * altZones = Enemy_zoneTable(1, self->zoneType);
    uint16 defZone = tr64_u16(defZones[boxIndex]);
    uint16 altZone = tr64_u16(altZones[boxIndex]);
    self->cellsCount = 0;
    Nav_Cell * cell = self->cells;
    for (uint32 i = 0; i < gs->level.boxesCount; i++)
    {
        if ((defZone == tr64_u16(defZones[i])) || (altZone == tr64_u16(altZones[i])))
        {
            ((*cell++)).boxIndex = i;
            self->cellsCount++;
        }
    }
    ASSERT((self->cellsCount > 0));;
}

vec3i Nav_getWaypoint(Nav *self, uint32 boxIndex, const vec3i * from)
{
    if (self->nextBox != 0xFFFF && self->nextBox != self->endBox)
    {
        self->endBox = self->nextBox;
        Nav_Cell * cell = &(self->cells[self->endBox]);
        if (((*cell)).next == 0xFFFF && self->tailBox != self->endBox)
        {
            ((*cell)).next = self->headBox;
            if (self->headBox == 0xFFFF) {
                self->tailBox = self->endBox;
            }
            self->headBox = self->endBox;
        }
        self->weight++;
        ((*cell)).weight = self->weight;
        ((*cell)).end = 0xFFFF;
    }
    if (self->headBox != 0xFFFF)
    {
        const tr64_u16_be_t * zones = Enemy_zoneTable(gs->gSaveGame.flipped, self->zoneType);
        uint16 zone = tr64_u16(zones[self->headBox]);
        for (int32 i = 0; (i < 5) && (self->headBox != 0xFFFF); i++)
        {
            Nav_search(self, zone, zones);
        }
    }
    if (boxIndex == self->endBox)
        return self->pos;
    vec3i wp = (*from);
    if (boxIndex == 0xFFFF)
        return wp;
    const tr64_core_boxes_t * box = Enemy_boxes() + boxIndex;
    int32 bMinX = (i32_shl(((tr64_i32(box->min_x)) >> 10), 10));
    int32 bMaxX = i32_sub((i32_shl(((tr64_i32(box->max_x) + 1) >> 10), 10)), 1);
    int32 bMinZ = (i32_shl(((tr64_i32(box->min_z)) >> 10), 10));
    int32 bMaxZ = i32_sub((i32_shl(((tr64_i32(box->max_z) + 1) >> 10), 10)), 1);
    int32 minX = bMinX;
    int32 maxX = bMaxX;
    int32 minZ = bMinZ;
    int32 maxZ = bMaxZ;
    enum ClipFlag {
        CLIP_MIN_X = ((1 << 0)),
        CLIP_MAX_X = ((1 << 1)),
        CLIP_MIN_Z = ((1 << 2)),
        CLIP_MAX_Z = ((1 << 3)),
        CLIP_DONE = ((1 << 4)),
        CLIP_ALL = (CLIP_MIN_X | CLIP_MAX_X | CLIP_MIN_Z | CLIP_MAX_Z)
    };
    int32 clip = CLIP_ALL;
    do {
        box = Enemy_boxes() + boxIndex;
        bMinX = (i32_shl(((tr64_i32(box->min_x)) >> 10), 10));
        bMaxX = i32_sub((i32_shl(((tr64_i32(box->max_x) + 1) >> 10), 10)), 1);
        bMinZ = (i32_shl(((tr64_i32(box->min_z)) >> 10), 10));
        bMaxZ = i32_sub((i32_shl(((tr64_i32(box->max_z) + 1) >> 10), 10)), 1);
        if (((*from)).x >= bMinX && ((*from)).x <= bMaxX && ((*from)).z >= bMinZ && ((*from)).z <= bMaxZ)
        {
            minX = bMinX;
            maxX = bMaxX;
            minZ = bMinZ;
            maxZ = bMaxZ;
        } else {
            if (((*from)).x < bMinX)
            {
                if ((clip & CLIP_MIN_X) && ((*from)).z >= bMinZ && ((*from)).z <= bMaxZ)
                {
                    (wp).x = (X_MAX(((wp).x), (i32_add(bMinX, 512))));
                    if (clip & CLIP_DONE)
                        return wp;
                    minZ = (X_MAX((minZ), (bMinZ)));
                    maxZ = (X_MIN((maxZ), (bMaxZ)));
                    clip = CLIP_MIN_X;
                } else {
                    if (clip != CLIP_MIN_X)
                    {
                        (wp).x = i32_sub(maxX, 512);
                        if (clip != CLIP_ALL)
                            return wp;
                        clip = CLIP_DONE;
                    }
                }
            }
            if (((*from)).x > bMaxX)
            {
                if ((clip & CLIP_MAX_X) && ((*from)).z >= bMinZ && ((*from)).z <= bMaxZ)
                {
                    (wp).x = (X_MIN(((wp).x), (i32_sub(bMaxX, 512))));
                    if (clip & CLIP_DONE)
                        return wp;
                    minZ = (X_MAX((minZ), (bMinZ)));
                    maxZ = (X_MIN((maxZ), (bMaxZ)));
                    clip = CLIP_MAX_X;
                } else {
                    if (clip != CLIP_MAX_X)
                    {
                        (wp).x = i32_add(minX, 512);
                        if (clip != CLIP_ALL)
                            return wp;
                        clip = CLIP_DONE;
                    }
                }
            }
            if (((*from)).z < bMinZ)
            {
                if ((clip & CLIP_MIN_Z) && ((*from)).x >= bMinX && ((*from)).x <= bMaxX)
                {
                    (wp).z = (X_MAX(((wp).z), (i32_add(bMinZ, 512))));
                    if (clip & CLIP_DONE)
                        return wp;
                    minX = (X_MAX((minX), (bMinX)));
                    maxX = (X_MIN((maxX), (bMaxX)));
                    clip = CLIP_MIN_Z;
                } else {
                    if (clip != CLIP_MIN_Z)
                    {
                        (wp).z = i32_sub(maxZ, 512);
                        if (clip != CLIP_ALL)
                            return wp;
                        clip = CLIP_DONE;
                    }
                }
            }
            if (((*from)).z > bMaxZ)
            {
                if ((clip & CLIP_MAX_Z) && ((*from)).x >= bMinX && ((*from)).x <= bMaxX)
                {
                    (wp).z = (X_MIN(((wp).z), (i32_sub(bMaxZ, 512))));
                    if (clip & CLIP_DONE)
                        return wp;
                    minX = (X_MAX((minX), (bMinX)));
                    maxX = (X_MIN((maxX), (bMaxX)));
                    clip = CLIP_MAX_Z;
                } else {
                    if (clip != CLIP_MAX_Z)
                    {
                        (wp).z = i32_add(minZ, 512);
                        if (clip != CLIP_ALL)
                            return wp;
                        clip = CLIP_DONE;
                    }
                }
            }
        }
        if (boxIndex == self->endBox)
        {
            if (clip & (CLIP_MIN_X | CLIP_MAX_X)) {
                (wp).x = self->pos.x;
            } else if (!(clip & CLIP_DONE)) {
                (wp).x = (((wp).x) < (i32_add(bMinX, 512)) ? (i32_add(bMinX, 512)) : (X_MIN(((wp).x), (i32_sub(bMaxX, 512)))));
            }
            if (clip & (CLIP_MIN_Z | CLIP_MAX_Z)) {
                (wp).z = self->pos.z;
            } else if (!(clip & CLIP_DONE)) {
                (wp).z = (((wp).z) < (i32_add(bMinZ, 512)) ? (i32_add(bMinZ, 512)) : (X_MIN(((wp).z), (i32_sub(bMaxZ, 512)))));
            }
            (wp).y = self->pos.y;
            return wp;
        }
        boxIndex = (self->cells[boxIndex]).end;
    } while ((boxIndex != 0xFFFF) && !(Enemy_boxOverlap(&(Enemy_boxes()[boxIndex])) & self->mask));
    if (!(clip & CLIP_DONE))
    {
        (wp).x = (((wp).x) < (i32_add(bMinX, 512)) ? (i32_add(bMinX, 512)) : (X_MIN(((wp).x), (i32_sub(bMaxX, 512)))));
        (wp).z = (((wp).z) < (i32_add(bMinZ, 512)) ? (i32_add(bMinZ, 512)) : (X_MIN(((wp).z), (i32_sub(bMaxZ, 512)))));
    }
    (wp).y = i32_sub(tr64_i16(box->floor), ((self->zoneType == ZONE_FLY) ? 384 : 0));
    return wp;
}

void Nav_search(Nav *self, uint16 zone, const tr64_u16_be_t * zones)
{
    Nav_Cell * curr = &(self->cells[self->headBox]);
    const tr64_core_boxes_t * b = &(Enemy_boxes()[self->headBox]);
    uint16 overlapIndex = Enemy_boxOverlap(&((*b))) & BOX_OVERLAP_INDEX_MASK;
    bool end = false;
    do {
        uint16 boxIndex = tr64_u16(Enemy_overlaps()[overlapIndex++]);
        end = (boxIndex & BOX_OVERLAP_LIST_END) != 0;
        if (end) {
            boxIndex &= BOX_OVERLAP_INDEX_MASK;
        }
        if (zone != tr64_u16(zones[boxIndex]))
            continue;
        int32 diff = i32_sub(tr64_i16((Enemy_boxes()[boxIndex]).floor), tr64_i16(((*b)).floor));
        if (diff > self->stepHeight || diff < self->dropHeight)
            continue;
        Nav_Cell * next = &(self->cells[boxIndex]);
        uint16 cWeight = ((*curr)).weight & 0x7FFF;
        uint16 nWeight = ((*next)).weight & 0x7FFF;
        if (cWeight < nWeight)
            continue;
        if (((*curr)).weight & 0x8000)
        {
            if (cWeight == nWeight)
                continue;
            ((*next)).weight = ((*curr)).weight;
        }
        else
        {
            if ((cWeight == nWeight) && !(((*next)).weight & 0x8000))
                continue;
            if (Enemy_boxOverlap(&(Enemy_boxes()[boxIndex])) & self->mask)
            {
                ((*next)).weight = ((*curr)).weight | 0x8000;
            }
            else
            {
                ((*next)).weight = ((*curr)).weight;
                ((*next)).end = self->headBox;
            }
        }
        if (((*next)).next == 0xFFFF && boxIndex != self->tailBox)
        {
            (self->cells[self->tailBox]).next = boxIndex;
            self->tailBox = boxIndex;
        }
    } while (!end);
    self->headBox = ((*curr)).next;
    ((*curr)).next = 0xFFFF;
}
