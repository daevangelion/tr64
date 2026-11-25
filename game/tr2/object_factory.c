/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/game.h, item.h, lara.h, inventory.h and enemy.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Keeps source constructor order and both deterministic RNG streams. */
#include "objects.h"
#include "boat.h"
#include "../object.h"

bool game_tr2_creature_type(unsigned type)
{
    return game_tr2_wall_creature_type(type) || game_tr2_urban_creature_type(type);
}

TR2Creature *game_tr2_creature_slot(const ItemObj *item)
{
    for (unsigned i=0;i<TR2_CREATURE_SLOTS;++i)
        if (gs->tr2->creatures[i].item==item)
            return gs->tr2->creatures+i;
    return NULL;
}

/* tr2-items-general, placed-item initialization: consume bit 15 as
 * separate cleanup eligibility, not as live source flags or retirement. */
uint16 game_tr2_placement_flags(const tr64_core_items_t *placement, unsigned index)
{
    uint16 flags = tr64_u16(placement->flags);
    if (flags & TR2_PLACED_CLEANUP)
        gs->tr2->cleanupEligible[index >> 5] |= 1u << (index & 31);
    return flags & ~TR2_PLACED_CLEANUP;
}

unsigned game_tr2_gunman_anim_base(unsigned type)
{
    const Model *m = gs->level.models + type;
    if (m->count > 0 && m->animIndex != UINT16_MAX &&
        game_tr2_model_bank_end(m->animIndex) >= m->animIndex + 22u)
        return m->animIndex;
    const Model *m16 = gs->level.models + TR2_ITEM_GUNMAN_1;
    if (m16->count > 0 && m16->animIndex != UINT16_MAX)
        return m16->animIndex;
    return m->animIndex;
}

#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
const TR2BoatHull *game_tr2_boat_hull(const ItemObj *boat)
{
    int16 idx = (int16)(boat - gs->items);
    for (unsigned i = 0; i < gs->tr2->boatCount; ++i)
        if (gs->tr2->boatHulls[i].itemIndex == idx)
            return gs->tr2->boatHulls + i;
    return NULL;
}
#endif

static bool ordinaryPickupType(unsigned type)
{
    return (type >= 135 && type <= 141) ||
           (type >= 143 && type <= 151) ||
           (type >= 174 && type <= 177) ||
           (type >= 190 && type <= 196) ||
           type == 205 || type == 206;
}

/* tr2-venice-creatures.md, Carried keys, weapons and ammunition (Q-VEN-04):
 * ordinary pickups at the exact X/Y/Z and room of a creature become carried
 * by the first matching creature in authored item order and are removed from
 * the room's loose pickups until that carrier retires at death. */
void game_tr2_urban_cargo_init(void)
{
    GameTR2 *tr2 = gs->tr2;
    const unsigned cap = (unsigned)(sizeof(tr2->cargoHead) / sizeof(tr2->cargoHead[0]));
    for (unsigned i = 0; i < cap; ++i)
        tr2->cargoHead[i] = tr2->cargoNext[i] = -1;
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        ItemObj *carrier = gs->items + i;
        if (!carrier->room || !game_tr2_creature_type(carrier->type))
            continue;
        for (ItemObj *it = carrier->room->firstItem, *next; it; it = next) {
            next = it->nextItem;
            if (it == carrier || !ordinaryPickupType(it->type))
                continue;
            if (it->pos.x == carrier->pos.x && it->pos.y == carrier->pos.y &&
                it->pos.z == carrier->pos.z) {
                int16_t slot = (int16_t)(it - gs->items);
                Room_remove(carrier->room, it);
                it->room = NULL;
                tr2->cargoNext[slot] = tr2->cargoHead[i];
                tr2->cargoHead[i] = slot;
            }
        }
    }
}

void game_tr2_urban_cargo_release(ItemObj *corpse)
{
    if (!corpse || !corpse->room)
        return;
    GameTR2 *tr2 = gs->tr2;
    unsigned owner = (unsigned)(corpse - gs->items);
    const unsigned cap = (unsigned)(sizeof(tr2->cargoHead) / sizeof(tr2->cargoHead[0]));
    if (owner >= cap)
        return;
    int16_t slot = tr2->cargoHead[owner];
    tr2->cargoHead[owner] = -1;
    while (slot >= 0) {
        int16_t next = tr2->cargoNext[slot];
        tr2->cargoNext[slot] = -1;
        ItemObj *pickup = gs->items + slot;
        pickup->pos = corpse->pos;
        Room_add(corpse->room, pickup);
        pickup->flags &= ~ITEM_FLAG_STATUS;
        slot = next;
    }
}

/* Q-INT-03: both bounds and spheres use their nearer authored key.
 * A bounds miss preserves the leaf's mask (active advance clears it). */
TR2SolidContact game_tr2_solid_contact(ItemObj *item, Lara *lara, bool push, bool doorBump)
{
    CollisionInfo *c = &gs->cinfo;
    AABBs box = *ItemObj_getBoundingBox(item,false);
    AABBs body = *ItemObj_getBoundingBox(lara,false);
    vec3i d = vec3i_sub(lara->pos,item->pos);
    if (box.maxY-body.minY <= d.y || box.minY-body.maxY >= d.y)
        return TR2_SOLID_MISS;
    int32 s = tr2_sin(item->angle.y), cs = tr2_sin(item->angle.y+ANGLE_90);
    int32 x = q14((int64_t)cs*d.x-(int64_t)s*d.z);
    int32 z = q14((int64_t)s*d.x+(int64_t)cs*d.z);
    int32 lowerX = box.minX-c->radius, upperX = box.maxX+c->radius;
    int32 lowerZ = box.minZ-c->radius, upperZ = box.maxZ+c->radius;
    if (x < lowerX || x > upperX || z < lowerZ || z > upperZ)
        return TR2_SOLID_MISS;
    item->hitMask = ItemObj_collideSpheres(item,lara);
    if (!item->hitMask)
        return TR2_SOLID_MISS;
    if (!push || !c->enemyPush)
        return TR2_SOLID_TOUCH;

    bool bump = doorBump && item->goalState != item->state &&
                c->enemyHit && box.maxY-box.minY > 256;
    if (bump) {
        int32 midX = (box.minX+box.maxX)>>1, midZ = (box.minZ+box.maxZ)>>1;
        unsigned bearing = game_tr2_bearing(d.z-q14((int64_t)cs*midZ-(int64_t)s*midX),
                                            d.x-q14((int64_t)cs*midX+(int64_t)s*midZ));
        lara->extraL->hitQuadrant = (uint16)(lara->angle.y-bearing+40960u)>>14;
    }

    /* Stable tie priority: lower X, higher X, higher Z, lower Z. */
    int32 dx0 = x-lowerX, dx1 = upperX-x, dz1 = upperZ-z, dz0 = z-lowerZ;
    if (dx0 <= dx1 && dx0 <= dz1 && dx0 <= dz0) x = lowerX;
    else if (dx1 <= dz1 && dx1 <= dz0) x = upperX;
    else if (dz1 <= dz0) z = upperZ;
    else z = lowerZ;
    lara->pos.x = i32_add(item->pos.x,q14((int64_t)cs*x+(int64_t)s*z));
    lara->pos.z = i32_add(item->pos.z,q14((int64_t)cs*z-(int64_t)s*x));
    int32 gapPos = c->gapPos, gapNeg = c->gapNeg, gapCeiling = c->gapCeiling;
    int16 angle = c->angle;
    c->gapPos = -WALL;
    c->gapNeg = -LARA_STEP_HEIGHT;
    c->gapCeiling = 0;
    CollisionInfo_setAngle(c,wrap_i16(game_tr2_bearing((int64_t)lara->pos.z-c->pos.z,
                                                      (int64_t)lara->pos.x-c->pos.x)));
    ItemObj_collideRoom(lara,LARA_HEIGHT,0);
    CollisionInfo_setAngle(c,angle);
    c->gapPos = gapPos; c->gapNeg = gapNeg; c->gapCeiling = gapCeiling;
    if (c->type != CT_NONE) {
        lara->pos.x = c->pos.x;
        lara->pos.z = c->pos.z;
    } else {
        c->pos = lara->pos;
        /* Only membership changes here; the ordinary Lara floor lookup
         * records her stored height in its own phase. */
        Room *room = Room_getRoom(lara->room,lara->pos.x,i32_sub(lara->pos.y,10),lara->pos.z);
        if (room != lara->room) {
            Room_remove(lara->room,lara);
            Room_add(room,lara);
        }
    }
    return bump ? TR2_SOLID_BUMP : TR2_SOLID_TOUCH;
}

void game_tr2_add_dyn_light(vec3i pos, uint8_t intensity, uint8_t falloff)
{
    GameTR2 *tr2 = gs->tr2;
    if (tr2->dynLightCount >= TR2_DYN_LIGHT_MAX)
        return;
    tr2->dynLights[tr2->dynLightCount++] = (TR2DynLight){pos, intensity, falloff};
}

static bool inElevatorFootprint(const ItemObj *item, int32 x, int32 z)
{
    int32 ex = item->pos.x >> 10, ez = item->pos.z >> 10;
    int32 tx = x >> 10, tz = z >> 10;
    return (tx == ex || tx == ex - 1) && (tz == ez || tz == ez + 1);
}

/* Resident TR2 dynamic floor/ceiling substitution (no unit re-entry from Sector_getFloor/Ceiling).
 * Handles Drawbridge 63 (§3), Elevator 65 (§4), Trapdoors 114-115, and Collapse 55/57. */
void __attribute__((noinline, optimize("Os")))
game_tr2_item_floor_ceiling(const ItemObj *self, int32 x, int32 y, int32 z, int32 *floor, int32 *ceiling)
{
    unsigned type = self->type;
    if (type == TR2_ITEM_DRAWBRIDGE) {
        if (self->state != 1)
            return;
        int32 dx = (x >> 10) - (self->pos.x >> 10);
        int32 dz = (z >> 10) - (self->pos.z >> 10);
        bool onDeck = ((dx == 0) && (dz == -1 || dz == -2) && self->angle.y == 0) ||
                      ((dx == 0) && (dz == 1 || dz == 2) && self->angle.y == ANGLE_180) ||
                      ((dz == 0) && (dx == -1 || dx == -2) && self->angle.y == ANGLE_90) ||
                      ((dz == 0) && (dx == 1 || dx == 2) && self->angle.y == -ANGLE_90);
        if (!onDeck)
            return;
        if (floor && y <= self->pos.y)
            *floor = self->pos.y;
        if (ceiling && y > self->pos.y)
            *ceiling = i32_add(self->pos.y, 256);
        return;
    }
    if (type == TR2_ITEM_ELEVATOR) {
        const Lara *lara = gs->players[0];
        bool laraIn = lara && inElevatorFootprint(self, lara->pos.x, lara->pos.z);
        bool queryIn = inElevatorFootprint(self, x, z);
        if (!laraIn && !queryIn)
            return;
        int32 Y = self->pos.y;
        int32 ly = lara ? lara->pos.y : y;
        bool closed = self->state == 0;
        int32 f = INT32_MAX, c = INT32_MIN;
        if (laraIn) {
            if (closed && ly > i32_sub(Y, 1024) && ly < i32_add(Y, 256)) {
                if (!queryIn) {
                    f = WALL;
                    c = 32767;
                } else {
                    f = Y;
                    c = i32_sub(Y, 1024);
                }
            } else if (queryIn) {
                if (ly < i32_sub(Y, 1024)) {
                    f = i32_sub(Y, 1280);
                } else if (ly < i32_add(Y, 256)) {
                    f = Y;
                    c = i32_sub(Y, 1024);
                } else {
                    c = i32_add(Y, 256);
                }
            }
        } else {
            if (y <= i32_sub(Y, 1280)) {
                f = i32_sub(Y, 1280);
            } else if (y < i32_add(Y, 256)) {
                if (!closed) {
                    f = Y;
                    c = i32_sub(Y, 1024);
                } else {
                    f = WALL;
                    c = 32767;
                }
            } else {
                c = i32_add(Y, 256);
            }
        }
        if (floor && f != INT32_MAX && f < *floor)
            *floor = f;
        if (ceiling && c != INT32_MIN && c > *ceiling)
            *ceiling = c;
        return;
    }
    ItemObj_getItemFloorCeiling(self, x, y, z, floor, ceiling);
}

/* tr2-venice-objects.md (type 72) & tr2-opera-house.md §10 (grenade passthrough shatter). */
void __attribute__((optimize("Os"))) game_tr2_window_shatter(ItemObj *item)
{
    if (!item || !(item->flags & ITEM_FLAG_COLLISION) || (item->flags & TR2_FLAG_RETIRED))
        return;
    if (item->room) {
        const Sector *sector = Room_getSector(item->room, item->pos.x, item->pos.z);
        if (sector->boxIndex != UINT16_MAX &&
            (gs->boxFlags[sector->boxIndex] & BOX_OVERLAP_BLOCKABLE))
            gs->boxFlags[sector->boxIndex] &= ~BOX_OVERLAP_BLOCKED;
    }
    if (item->flags & ITEM_FLAG_ACTIVE) {
        if (gs->tr2->activeNext == item)
            gs->tr2->activeNext = item->nextActive;
        ItemObj_deactivateBase(item);
    }
    item->flags = (item->flags & ~(ITEM_FLAG_COLLISION | ITEM_FLAG_STATUS)) |
                  TR2_FLAG_RETIRED | ITEM_FLAG_ONCE;
    item->hitMask = 0;
    soundPlay(58, &item->pos);
    game_tr2_shatter(item, item->visibleMask & ~(UINT32_C(1) << 8));
    item->visibleMask = UINT32_C(1) << 8;
}

static void clearTargetIf(const ItemObj *corpse)
{
    Lara *lara = gs->players[0];
    if (!lara || !lara->extraL)
        return;
    if (lara->extraL->armR.target == corpse)
        lara->extraL->armR.target = NULL;
    if (lara->extraL->armL.target == corpse)
        lara->extraL->armL.target = NULL;
}

/* §10 Explosive death retirement: release cargo, shatter all visible meshes, disable AI,
 * remove corpse, and admit to cleanupList if cleanup-eligible. */
void __attribute__((optimize("Os"))) game_tr2_explosive_death(ItemObj *item)
{
    game_tr2_urban_cargo_release(item);
    game_tr2_shatter(item, item->visibleMask);
    TR2Creature *ai = game_tr2_creature_slot(item);
    if (ai)
        ai->item = NULL;
    if (item->flags & ITEM_FLAG_ACTIVE) {
        if (gs->tr2->activeNext == item)
            gs->tr2->activeNext = item->nextActive;
        ItemObj_deactivateBase(item);
    }
    clearTargetIf(item);
    unsigned slot = (unsigned)(item - gs->items);
    if (item->room && !(gs->unlinkedItems[slot >> 5] & (1u << (slot & 31))))
        Room_remove(item->room, item);
    item->room = NULL;
    item->flags = (item->flags & ~(ITEM_FLAG_COLLISION | ITEM_FLAG_STATUS)) |
                  ITEM_FLAG_STATUS_INACTIVE | TR2_FLAG_RETIRED;
    item->health = -16384;
    if (slot < gs->level.itemsCount &&
        (gs->tr2->cleanupEligible[slot >> 5] & (1u << (slot & 31))))
        gs->tr2->cleanupList[slot >> 5] |= 1u << (slot & 31);
}

/* §2 Clear Bodies (trigger action 11): permanently retire all corpses on cleanupList
 * and set bit 15 (TR2_PLACED_CLEANUP) without disturbing released cargo. */
void __attribute__((optimize("Os"))) game_tr2_clear_bodies(Lara *lara)
{
    (void)lara;
    GameTR2 *tr2 = gs->tr2;
    for (unsigned w = 0; w < 256u / 32u; ++w) {
        uint32_t bits = tr2->cleanupList[w];
        tr2->cleanupList[w] = 0;
        while (bits) {
            unsigned b = (unsigned)__builtin_ctz(bits);
            bits &= bits - 1u;
            unsigned slot = w * 32u + b;
            if (slot >= gs->level.itemsCount)
                continue;
            ItemObj *corpse = gs->items + slot;
            if (corpse->flags & ITEM_FLAG_ACTIVE) {
                if (tr2->activeNext == corpse)
                    tr2->activeNext = corpse->nextActive;
                ItemObj_deactivateBase(corpse);
            }
            if (corpse->room && !(gs->unlinkedItems[slot >> 5] & (1u << (slot & 31))))
                Room_remove(corpse->room, corpse);
            corpse->room = NULL;
            clearTargetIf(corpse);
            corpse->flags = (corpse->flags & ~(ITEM_FLAG_STATUS | ITEM_FLAG_COLLISION)) |
                            ITEM_FLAG_STATUS_INACTIVE | TR2_FLAG_RETIRED | TR2_PLACED_CLEANUP;
        }
    }
}
