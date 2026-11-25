/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h and src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Tomb Raider II Grenade projectile (type 248, docs/specs/wall/tr2-opera-house.md §10),
 * paged in `tr2_wall` so it is available in every level where Lara owns the launcher. */
#include "objects.h"
#include "boat.h"
#include "game/common/object.h"

#if TR64_HAS_TR2

void __attribute__((optimize("Os"))) game_tr2_grenade_update(ItemObj *item)
{
    vec3i old = item->pos;
    if (item->hSpeed < 190)
        item->vSpeed = wrap_i16((int32)item->vSpeed + 1);
    item->hSpeed = wrap_i16((int32)item->hSpeed - 1);

    int32 s = item->hSpeed;
    int32 v = item->vSpeed;
    int32 sp = tr2_sin(item->angle.x), cp = tr2_sin(item->angle.x + ANGLE_90);
    int32 sy = tr2_sin(item->angle.y), cy = tr2_sin(item->angle.y + ANGLE_90);
    int32 h = q14((int64_t)cp * s);
    item->pos.y = i32_sub(i32_add(old.y, v), q14((int64_t)sp * s));
    item->pos.z = i32_add(old.z, q14((int64_t)cy * h));
    item->pos.x = i32_add(old.x, q14((int64_t)sy * h));

    const FloorData *savedFd = gs->gLastFloorData;
    uint16 savedSlant = gs->gLastFloorSlant;
    Room *room = item->room;
    const Sector *sec = game_tr2_room_sector(&room, item->pos.x, item->pos.y, item->pos.z);
    int32 floor = Sector_getFloor(sec, item->pos.x, item->pos.y, item->pos.z);
    int32 ceiling = Sector_getCeiling(sec, item->pos.x, item->pos.y, item->pos.z);
    gs->gLastFloorData = savedFd;
    gs->gLastFloorSlant = savedSlant;

    item->roomFloor = (int16)floor;
    if (room != item->room) {
        Room_remove(item->room, item);
        Room_add(room, item);
    }

    bool detonate = false;
    int32 radius = 0;
    if (item->pos.y >= floor || item->pos.y <= ceiling) {
        detonate = true;
        radius = 512;
    }

    /* Room-scoped scan skipping Lara unconditionally (zero self-damage). */
    for (ItemObj *cand = item->room->firstItem, *next; cand; cand = next) {
        next = cand->nextItem;
        if (cand == item || cand->type == ITEM_LARA || !(cand->flags & ITEM_FLAG_COLLISION))
            continue;
        bool isWindow = cand->type == TR2_ITEM_WINDOW;
        bool isCreature = game_tr2_creature_type(cand->type) &&
                          (cand->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE;
        if ((!isWindow && !isCreature) || cand->animIndex == UINT16_MAX)
            continue;

        const AABBs *b = ItemObj_getBoundingBox(cand, false);
        if (i32_add(item->pos.y, radius) < i32_add(cand->pos.y, b->minY) ||
            i32_sub(item->pos.y, radius) > i32_add(cand->pos.y, b->maxY))
            continue;

        int32 sc = tr2_sin(cand->angle.y + ANGLE_90), ss = tr2_sin(cand->angle.y);
        int32 dx0 = i32_sub(old.x, cand->pos.x), dz0 = i32_sub(old.z, cand->pos.z);
        int32 dx1 = i32_sub(item->pos.x, cand->pos.x), dz1 = i32_sub(item->pos.z, cand->pos.z);
        int32 lx0 = q14((int64_t)sc * dx0 - (int64_t)ss * dz0);
        int32 lz0 = q14((int64_t)ss * dx0 + (int64_t)sc * dz0);
        int32 lx1 = q14((int64_t)sc * dx1 - (int64_t)ss * dz1);
        int32 lz1 = q14((int64_t)ss * dx1 + (int64_t)sc * dz1);
        int32 minX = b->minX - radius, maxX = b->maxX + radius;
        int32 minZ = b->minZ - radius, maxZ = b->maxZ + radius;
        if ((lx0 < minX && lx1 < minX) || (lx0 > maxX && lx1 > maxX) ||
            (lz0 < minZ && lz1 < minZ) || (lz0 > maxZ && lz1 > maxZ))
            continue;

        if (isWindow) {
            /* Shatters type 72 glass without detonating so the grenade continues flying. */
            game_tr2_window_shatter(cand);
            continue;
        }

        detonate = true;
        if ((cand->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE) {
            if (cand->health > 0 && cand->health <= 30)
                ++gs->gSaveGame.kills;
            cand->health = X_MAX(INT16_MIN, cand->health - 30);
            cand->flags |= ITEM_FLAG_INJURED;
            ++gs->tr2->ammoHits;
            if (cand->health <= 0) {
                ++gs->gSaveGame.kills; /* Retail double-count on lethal grenade hit (§10). */
                if (cand->type != 22 && cand->type != 46)
                    game_tr2_explosive_death(cand);
            }
        }
    }

    if (detonate) {
        game_tr2_explosion_effect(item->room, old);
        soundPlay(105, NULL);
        if (gs->tr2->activeNext == item)
            gs->tr2->activeNext = item->nextActive;
        ItemObj_deactivateBase(item);
        if (item->room)
            Room_remove(item->room, item);
        item->room = NULL;
        item->flags = 0;
        item->nextItem = gs->firstFree;
        gs->firstFree = item;
    }
}

#endif
