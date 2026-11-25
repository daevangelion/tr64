/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "boat.h"
#include "../object.h"

#if TR64_HAS_TR2

enum {
    TR2_LARA_JUMP_OFF = 77
};

/* Model 11 (LARA_BOAT) clip offsets from base 259, tr2-venice-objects.md. */
enum {
    BOAT_CLIP_MOUNT_NEG = 0,   /* 259 (0..99) */
    BOAT_CLIP_SIT = 1,         /* 260 (100..159), state 1 */
    BOAT_CLIP_DRIVE = 2,       /* 261 (160..219), state 2 */
    BOAT_CLIP_JUMP_LEFT = 5,   /* 264 (264..288), state 4 */
    BOAT_CLIP_MOUNT_DRY = 6,   /* 265 (289..327), state 0 */
    BOAT_CLIP_JUMP_RIGHT = 7,  /* 266 (328..352), state 3 */
    BOAT_CLIP_MOUNT_POS = 8,   /* 267 (353..452), state 0 */
    BOAT_CLIP_HIT_R = 11,      /* 270 (483..494) */
    BOAT_CLIP_HIT_L = 12,      /* 271 (495..506) */
    BOAT_CLIP_HIT_B = 13,      /* 272 (507..525) */
    BOAT_CLIP_HIT_F = 14,      /* 273 (526..545) */
    BOAT_CLIP_FALL = 15,       /* 274 (546..556), state 6 */
    BOAT_CLIP_DEATH = 18       /* 277 (572..615), state 8 */
};

static vec3i localDelta(vec3i d, int16 yaw)
{
    int32 c = tr2_sin(yaw + ANGLE_90), s = tr2_sin(yaw);
    return vec3i_create(q14((int64_t)c * d.x - (int64_t)s * d.z),
                        d.y,
                        q14((int64_t)s * d.x + (int64_t)c * d.z));
}

static void moveItemRoom(ItemObj *item, Room *room)
{
    if (item->room != room) {
        Room_remove(item->room, item);
        Room_add(room, item);
    }
}

/* tr2-venice-objects.md, Bell, expiry and gate animation. */
void game_tr2_race_clock_update(ItemObj *item, const Lara *lara)
{
    bool active = Object_isActive(item);
    if (!active || item->timer == -1) {
        item->flags &= ~(ITEM_FLAG_MASK | ITEM_FLAG_STATUS);
        ItemObj_deactivateBase(item);
        vec3i at = vec3i_create(
            i32_add(lara->pos.x, i32_sub(item->pos.x, lara->pos.x) >> 6),
            i32_add(lara->pos.y, i32_sub(item->pos.y, lara->pos.y) >> 6),
            i32_add(lara->pos.z, i32_sub(item->pos.z, lara->pos.z) >> 6));
        soundPlay(208, &at);
        return;
    }
    if (item->timer > 0 && (item->timer % 60) == 59) {
        vec3i at = vec3i_create(
            i32_add(lara->pos.x, i32_sub(item->pos.x, lara->pos.x) >> 6),
            i32_add(lara->pos.y, i32_sub(item->pos.y, lara->pos.y) >> 6),
            i32_add(lara->pos.z, i32_sub(item->pos.z, lara->pos.z) >> 6));
        soundPlay(208, &at);
    }
}

/* tr2-venice-objects.md, Sinking gondola (24). */
void game_tr2_gondola_update(ItemObj *item)
{
    if (item->state == 3) {
        item->pos.y = i32_add(item->pos.y, 50);
        Room *room = item->room;
        const Sector *s = game_tr2_room_sector(&room, item->pos.x, item->pos.y, item->pos.z);
        int32 floor = Sector_getFloor(s, item->pos.x, item->pos.y, item->pos.z);
        item->roomFloor = floor;
        if (floor != WALL && item->pos.y >= floor) {
            item->pos.y = floor;
            item->goalState = 4;
        }
    }
    ItemObj_animProcess(item, true);
    if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        ItemObj_deactivateBase(item);
}

void game_tr2_boat_init(void)
{
    gs->tr2->boatCount = 0;
    for (unsigned i = 0; i < TR2_BOAT_SLOTS; ++i)
        gs->tr2->boatHulls[i] = (TR2BoatHull){ .itemIndex = -1, .present = 0 };
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        if (gs->items[i].type != TR2_ITEM_BOAT)
            continue;
        if (gs->tr2->boatCount >= TR2_BOAT_SLOTS)
            sys_error("Tomb Raider II: boat hull pool overflow (slot=%u)", i);
        TR2BoatHull *h = gs->tr2->boatHulls + gs->tr2->boatCount++;
        h->itemIndex = (int16_t)i;
        h->present = 1;
    }
}

static TR2BoatHull *findBoatHull(const ItemObj *boat)
{
    int16_t slot = (int16_t)(boat - gs->items);
    for (unsigned i = 0; i < gs->tr2->boatCount; ++i)
        if (gs->tr2->boatHulls[i].itemIndex == slot)
            return gs->tr2->boatHulls + i;
    return NULL;
}

static void removeBoat(ItemObj *boat)
{
    TR2BoatHull *hull = findBoatHull(boat);
    if (hull)
        hull->present = 0;
    if (gs->tr2->activeNext == boat)
        gs->tr2->activeNext = boat->nextActive;
    ItemObj_remove(boat);
}

/* tr2-venice-objects.md, Arming mines, First blast, Subsequent blasts. */
void game_tr2_mine_update_post(ItemObj *item, Lara *lara, bool killedRider)
{
    if (killedRider) {
        game_tr2_shatter(lara, UINT32_MAX);
        lara->health = 0;
        lara->visibleMask = 0;
        gs->tr2->vehicle = -1;
    }

    vec3i blastPos = vec3i_create(item->pos.x, i32_sub(item->pos.y, 1024), item->pos.z);
    Room *blastRoom = Room_getRoom(item->room, blastPos.x, blastPos.y, blastPos.z);
    game_tr2_explosion_effect(blastRoom, blastPos);
    game_tr2_splash_effect(blastRoom, blastPos);
    soundPlay(105, &item->pos);

    item->flags |= ITEM_FLAG_ONCE;
    item->flags &= ~ITEM_FLAG_COLLISION;
    item->visibleMask = 1;
}

uint32 game_tr2_mine_update_phase1(ItemObj *item, Lara *lara, TR2BoatCall *call)
{
    if (item->flags & ITEM_FLAG_ONCE)
        return TR2_BOAT_RES_DONE;

    if (!gs->tr2->mineChain) {
        Room *above = Room_getRoom(item->room, item->pos.x, i32_sub(item->pos.y, 2048), item->pos.z);
        ItemObj *hitBoat = NULL;
        for (ItemObj *it = above->firstItem; it; it = it->nextItem) {
            if (it->type != TR2_ITEM_BOAT)
                continue;
            int32 dx = i32_sub(it->pos.x, item->pos.x);
            int32 dz = i32_sub(it->pos.z, item->pos.z);
            if (abs(dx) < 512 && abs(dz) < 512) {
                hitBoat = it;
                break;
            }
        }
        if (!hitBoat)
            return TR2_BOAT_RES_DONE;

        gs->tr2->mineChain = 1;
        /* tr2-venice-objects.md: removed boat briefly takes type 221 for
         * breakup before returning to type 14; then if Lara rides it she
         * shatters, goes to zero health and disappears. */
        hitBoat->type = 221;
        game_tr2_shatter(hitBoat, UINT32_MAX);
        hitBoat->type = TR2_ITEM_BOAT;
        bool killedRider = gs->tr2->vehicle == (int16)(hitBoat - gs->items);
        removeBoat(hitBoat);

        Room *mineRoom = item->room;
        const Sector *s = game_tr2_room_sector(&mineRoom, item->pos.x, item->pos.y, item->pos.z);
        (void)Sector_getFloor(s, item->pos.x, item->pos.y, item->pos.z);
        if (gs->gLastFloorData) {
            call->trigger = gs->gLastFloorData;
            call->killedRider = killedRider;
            return TR2_BOAT_RES_MINE_TRIGGER;
        }
        game_tr2_mine_update_post(item, lara, killedRider);
        return TR2_BOAT_RES_DONE;
    } else {
        if (rand_logic() < 30720)
            return TR2_BOAT_RES_DONE;
    }

    game_tr2_mine_update_post(item, lara, false);
    return TR2_BOAT_RES_DONE;
}

static void syncBoatPoseFromLara(ItemObj *boat, const Lara *lara)
{
    if (lara->health <= 0)
        return;
    unsigned laraBase = gs->level.models[TR2_MODEL_LARA_BOAT].animIndex;
    unsigned boatBase = gs->level.models[TR2_ITEM_BOAT].animIndex;
    if (lara->animIndex < laraBase || lara->animIndex >= laraBase + 18u)
        return;
    unsigned rel = lara->animIndex - laraBase;
    const Anim *la = gs->level.anims + lara->animIndex;
    const Anim *ba = gs->level.anims + boatBase + rel;
    int32 off = (int32)lara->frameIndex - (int32)tr64_u16(la->frame_start);
    int32 maxOff = (int32)tr64_u16(ba->frame_end) - (int32)tr64_u16(ba->frame_start);
    if (off < 0) off = 0;
    else if (off > maxOff) off = maxOff;
    boat->animIndex = (uint16)(boatBase + rel);
    boat->frameIndex = (uint16)(tr64_u16(ba->frame_start) + off);
}

/* tr2-venice-objects.md, Riding and driving the speedboat (14): boarding. */
bool game_tr2_boat_interact(ItemObj *boat, Lara *lara)
{
    if (lara->health <= 0)
        return false;
    if (gs->tr2->vehicle == (int16)(boat - gs->items))
        return false;
    if (!ItemObj_collideBounds(boat, lara, &gs->cinfo))
        return false;
    boat->hitMask = ItemObj_collideSpheres(boat, lara);
    if (!boat->hitMask)
        return false;

    int mountClip = -1;
    if (gs->tr2->vehicle < 0 && lara->extraL->weaponState == WEAPON_STATE_FREE) {
        vec3i d = localDelta(vec3i_sub(lara->pos, boat->pos), boat->angle.y);
        if (d.z <= 200) {
            int16 diff = wrap_i16((int32)boat->angle.y - lara->angle.y);
            if (lara->waterState == WATER_STATE_SURFACE || lara->waterState == WATER_STATE_WADE) {
                if (!(lara->flags & ITEM_FLAG_GRAVITY) && boat->hSpeed == 0 &&
                    (lara->input & IN_ACTION)) {
                    if (diff > 8192 && diff < 24576)
                        mountClip = BOAT_CLIP_MOUNT_POS;
                    else if (diff < -8192 && diff > -24576)
                        mountClip = BOAT_CLIP_MOUNT_NEG;
                }
            } else if (lara->waterState == WATER_STATE_ABOVE) {
                if (diff > -24576 && diff < 24576) {
                    if (lara->vSpeed > 0) {
                        if (boat->pos.y < lara->pos.y)
                            mountClip = BOAT_CLIP_MOUNT_DRY;
                    } else if (lara->vSpeed == 0) {
                        if (vec3i_equal(lara->pos, boat->pos))
                            mountClip = BOAT_CLIP_SIT;
                        else
                            mountClip = BOAT_CLIP_MOUNT_DRY;
                    }
                }
            }
        }
    }

    if (mountClip >= 0) {
        uint32 touch = boat->hitMask;
        game_tr2_activate_item(boat);
        boat->hitMask = touch;
        gs->tr2->vehicle = (int16)(boat - gs->items);
        moveItemRoom(lara, boat->room);
        lara->pos = vec3i_create(boat->pos.x, i32_sub(boat->pos.y, 5), boat->pos.z);
        lara->angle = vec3s_create(0, boat->angle.y, 0);
        lara->flags &= ~ITEM_FLAG_GRAVITY;
        lara->hSpeed = 0;
        lara->vSpeed = 0;
        lara->waterState = WATER_STATE_ABOVE;
        unsigned base = gs->level.models[TR2_MODEL_LARA_BOAT].animIndex;
        ItemObj_animSet(lara, base + (unsigned)mountClip, true, 0);
        ItemObj_animProcess(lara, false);
        return false;
    }

    return game_tr2_solid_contact(boat, lara, true, false) == TR2_SOLID_BUMP;
}

static int32 boatSampleOffset(Room *room, int32 x, int32 y, int32 z)
{
    const Sector *s = game_tr2_room_sector(&room, x, y, z);
    int32 wh = game_tr2_water_height(room, x, z);
    if (wh != WALL)
        return wh - 5;
    int32 fh = Sector_getFloor(s, x, y, z);
    return fh != WALL ? fh - 5 : WALL;
}

static int32 boatSampleCentreRaw(Room *room, int32 x, int32 y, int32 z)
{
    const Sector *s = game_tr2_room_sector(&room, x, y, z);
    int32 wh = game_tr2_water_height(room, x, z);
    if (wh != WALL)
        return wh;
    return Sector_getFloor(s, x, y, z);
}

static vec3i boatWorldPoint(vec3i origin, int16 pitch, int16 yaw, int16 roll, int32 lx, int32 lz)
{
    int32 sy = tr2_sin(yaw), cy = tr2_sin(yaw + ANGLE_90);
    int32 sp = tr2_sin(pitch), sr = tr2_sin(roll);
    return vec3i_create(
        i32_add(origin.x, q14((int64_t)lz * sy + (int64_t)lx * cy)),
        i32_add(origin.y, i32_sub(q14((int64_t)lx * sr), q14((int64_t)lz * sp))),
        i32_add(origin.z, q14((int64_t)lz * cy - (int64_t)lx * sy)));
}

static int32 boatDrift(int16 angle, int32 scale)
{
    int32 d = (tr2_sin(angle) * scale) / 16384;
    if (!d && angle)
        d = angle > 0 ? 1 : -1;
    return d;
}

/* tr2-venice-objects.md, Same-tile, straight or diagonal correction and deflection. */
static int32 boatCheckPoint(ItemObj *boat, Room *oldRoom, vec3i oldPt, int32 oldTarget,
                            vec3i newPt, int32 newTarget, bool corner)
{
    int32 oldRef = oldPt.y < oldTarget ? oldPt.y : oldTarget;
    if (oldRef - newTarget <= 128)
        return 0;

    int32 ox = oldPt.x, oz = oldPt.z, nx = newPt.x, nz = newPt.z;
    int32 otx = ox >> 10, otz = oz >> 10, ntx = nx >> 10, ntz = nz >> 10;
    int32 offX = i32_sub(nx, boat->pos.x), offZ = i32_sub(nz, boat->pos.z);

    if (ntx == otx && ntz == otz) {
        boat->pos.x = i32_add(boat->pos.x, i32_sub(ox, nx));
        boat->pos.z = i32_add(boat->pos.z, i32_sub(oz, nz));
        return 0;
    }

    int32 shiftX = 0, shiftZ = 0;
    bool blockX = false, blockZ = false;
    if (ntx != otx && ntz != otz) {
        int32 sx = boatSampleOffset(oldRoom, nx, oldPt.y, oz);
        int32 sz = boatSampleOffset(oldRoom, ox, oldPt.y, nz);
        blockX = (oldRef - sx) > 256;
        blockZ = (oldRef - sz) > 256;
        if (!blockX && !blockZ) {
            boat->pos.x = i32_add(boat->pos.x, i32_sub(ox, nx));
            boat->pos.z = i32_add(boat->pos.z, i32_sub(oz, nz));
            return 0;
        }
    } else if (ntx != otx) {
        blockX = true;
    } else {
        blockZ = true;
    }

    if (blockX)
        shiftX = ntx > otx ? -((nx & 1023) + 1) : (1024 - (nx & 1023));
    if (blockZ)
        shiftZ = ntz > otz ? -((nz & 1023) + 1) : (1024 - (nz & 1023));

    boat->pos.x = i32_add(boat->pos.x, shiftX);
    boat->pos.z = i32_add(boat->pos.z, shiftZ);

    if (!corner || (blockX && blockZ))
        return 0;
    if (blockX)
        return shiftX > 0 ? offZ : -offZ;
    return shiftZ > 0 ? -offX : offX;
}

static int32 boatStepSuspension(int32 y, int32 *vSpeed, int32 target)
{
    if (y < target) {
        y = i32_add(y, *vSpeed);
        if (y > target) {
            y = target;
            *vSpeed = 0;
        } else {
            *vSpeed += 6;
        }
    } else {
        *vSpeed += ((target - y) - *vSpeed) >> 3;
        if (*vSpeed < -20)
            *vSpeed = -20;
        if (y > target)
            y = target;
    }
    return y;
}

static int16 easeAngleHalf(int16 cur, int16 target)
{
    if (target == 0 && abs(cur) < 4)
        return 0;
    return wrap_i16(cur + ((target - cur) >> 1));
}

static void boatEmitWake(ItemObj *boat, int32 waterH)
{
    /* 8. Wake emission using post-collision, post-control signed speed. */
    if (boat->hSpeed != 0 && waterH != WALL && boat->pos.y == waterH - 5) {
        (void)rand_draw(); /* Group presentation draw even if pool is full. */
        static const int32 lat[3] = {-300, 0, 300};
        static const int16 yawOff[3] = {-10920, 0, 10920};
        for (int i = 0; i < 3; ++i) {
            vec3i wp = boatWorldPoint(boat->pos, 0, boat->angle.y, 0, lat[i], -700);
            wp.y = boat->pos.y;
            game_tr2_wake_effect(boat->room, wp,
                                 wrap_i16(boat->angle.y + ANGLE_180 + yawOff[i]),
                                 boat->hSpeed);
        }
    }
}

void game_tr2_boat_update_post(ItemObj *boat, Lara *lara, int32 waterH)
{
    unsigned laraBase = gs->level.models[TR2_MODEL_LARA_BOAT].animIndex;
    /* Advance rider animation, then release on final frame of jump-off clip. */
    uint8 prevState = lara->state;
    ItemObj_animProcess(lara, false);
    if (lara->state != prevState)
        lara->goalState = lara->state;
    bool isJumpR = lara->animIndex == laraBase + BOAT_CLIP_JUMP_RIGHT;
    bool isJumpL = lara->animIndex == laraBase + BOAT_CLIP_JUMP_LEFT;
    if ((isJumpR || isJumpL) &&
        lara->frameIndex == tr64_u16(gs->level.anims[lara->animIndex].frame_end)) {
        int32 seatX = boat->pos.x, seatY = boat->pos.y, seatZ = boat->pos.z;
        int16 outYaw = wrap_i16(boat->angle.y + (isJumpR ? ANGLE_90 : -ANGLE_90));
        lara->angle = vec3s_create(0, outYaw, 0);
        ItemObj_animSet(lara, TR2_LARA_JUMP_OFF, true, 0);
        lara->flags |= ITEM_FLAG_GRAVITY;
        lara->vSpeed = -40;
        lara->hSpeed = 20;
        lara->extraL->moveAngle = outYaw;
        lara->pos = vec3i_create(seatX, i32_sub(seatY, 90), seatZ);

        vec3i step = game_tr2_heading_step(outYaw, 360);
        int32 dx = i32_add(seatX, step.x), dz = i32_add(seatZ, step.z);
        Room *destRoom = lara->room;
        const Sector *ds = game_tr2_room_sector(&destRoom, dx, lara->pos.y, dz);
        int32 df = Sector_getFloor(ds, dx, lara->pos.y, dz);
        if (df != WALL && (seatY - df) <= 346) {
            lara->pos.x = dx;
            lara->pos.z = dz;
            moveItemRoom(lara, destRoom);
        } else {
            moveItemRoom(lara, boat->room);
        }
        gs->tr2->vehicle = -1;
        ItemObj_animSet(boat, gs->level.models[TR2_ITEM_BOAT].animIndex, true, 0);
    } else {
        moveItemRoom(lara, boat->room);
        lara->pos = boat->pos;
        lara->angle = boat->angle;
        syncBoatPoseFromLara(boat, lara);
    }
    boatEmitWake(boat, waterH);
}

/* tr2-venice-objects.md, Riding and driving + Hull motion, collision and departure. */
uint32 game_tr2_boat_update_phase1(ItemObj *boat, Lara *lara, TR2BoatCall *call)
{
    TR2BoatHull *hull = findBoatHull(boat);
    if (!hull)
        return TR2_BOAT_RES_DONE;
    bool ridden = gs->tr2->vehicle == (int16)(boat - gs->items);

    /* Strip prior steering bank to recover terrain roll. */
    int16 terrainRoll = wrap_i16(boat->angle.z - hull->bank);
    int16 oldPitch = boat->angle.x;
    int16 oldYaw = boat->angle.y;
    vec3i oldPos = boat->pos;
    Room *oldRoom = boat->room;
    int16 oldSpeed = boat->hSpeed;

    /* 1. Old structural samples before movement. */
    static const int32 cornerL[4][2] = {
        {-300, 750}, {300, 750}, {-300, -750}, {300, -750}
    };
    vec3i oldCornerPt[4];
    int32 oldCornerH[4];
    for (int i = 0; i < 4; ++i) {
        oldCornerPt[i] = boatWorldPoint(oldPos, oldPitch, oldYaw, terrainRoll,
                                        cornerL[i][0], cornerL[i][1]);
        oldCornerH[i] = boatSampleOffset(oldRoom, oldCornerPt[i].x, oldCornerPt[i].y, oldCornerPt[i].z);
    }
    vec3i oldBowPt = boatWorldPoint(oldPos, oldPitch, oldYaw, terrainRoll, 0, 1000);
    int32 oldBowH = boatSampleOffset(oldRoom, oldBowPt.x, oldBowPt.y, oldBowPt.z);

    /* 2. Apply prior turn + terrain deflection, store bank, and move hull. */
    boat->angle.y = wrap_i16(boat->angle.y + hull->turn + hull->terrainYaw);
    hull->bank = (int16)(hull->turn * 6);

    int32 sy = tr2_sin(boat->angle.y), cy = tr2_sin(boat->angle.y + ANGLE_90);
    int32 pitchDrift = boatDrift(oldPitch, 10);
    int32 rollDrift = boatDrift(terrainRoll, 30);
    int32 forward = (int32)boat->hSpeed - pitchDrift;

    boat->pos.x = i32_add(boat->pos.x, q14((int64_t)forward * sy + (int64_t)rollDrift * cy));
    boat->pos.z = i32_add(boat->pos.z, q14((int64_t)forward * cy - (int64_t)rollDrift * sy));
    vec3i attemptedPos = boat->pos;

    /* 3. Terrain collision on 4 corners, extra bow (when pitch == 0), and centre. */
    int32 turnSum = 0;
    for (int i = 0; i < 4; ++i) {
        vec3i np = boatWorldPoint(boat->pos, oldPitch, boat->angle.y, terrainRoll,
                                  cornerL[i][0], cornerL[i][1]);
        int32 nh = boatSampleOffset(oldRoom, np.x, np.y, np.z);
        turnSum += boatCheckPoint(boat, oldRoom, oldCornerPt[i], oldCornerH[i], np, nh, true);
    }
    if (oldPitch == 0) {
        vec3i np = boatWorldPoint(boat->pos, oldPitch, boat->angle.y, terrainRoll, 0, 1000);
        int32 nh = boatSampleOffset(oldRoom, np.x, np.y, np.z);
        (void)boatCheckPoint(boat, oldRoom, oldBowPt, oldBowH, np, nh, false);
    }
    {
        int32 ch = boatSampleCentreRaw(oldRoom, boat->pos.x, boat->pos.y, boat->pos.z);
        if (boat->pos.y - ch > 128)
            (void)boatCheckPoint(boat, oldRoom, oldPos, oldPos.y, boat->pos, ch, false);
    }
    hull->terrainYaw = (int16)turnSum;

    /* 4. Same-room empty hull or idle gondola interaction. */
    for (ItemObj *other = boat->room->firstItem; other; other = other->nextItem) {
        if (other == boat)
            continue;
        if (other->type == TR2_ITEM_BOAT) {
            if (gs->tr2->vehicle == (int16)(other - gs->items))
                continue;
            int32 dx = i32_sub(boat->pos.x, other->pos.x);
            int32 dz = i32_sub(boat->pos.z, other->pos.z);
            int64_t sq = (int64_t)dx * dx + (int64_t)dz * dz;
            if (sq > 0 && sq < 1000LL * 1000LL) {
                int32 dist = phd_sqrt((uint32)sq);
                if (dist > 0) {
                    boat->pos.x = i32_add(other->pos.x, (int32)((int64_t)dx * 1000 / dist));
                    boat->pos.z = i32_add(other->pos.z, (int32)((int64_t)dz * 1000 / dist));
                }
            }
        } else if (other->type == TR2_ITEM_GONDOLA && other->state == 1) {
            int32 gsy = tr2_sin(other->angle.y), gcy = tr2_sin(other->angle.y + ANGLE_90);
            int32 gx = i32_sub(other->pos.x, q14(512 * gsy));
            int32 gz = i32_sub(other->pos.z, q14(512 * gcy));
            int32 dx = i32_sub(boat->pos.x, gx);
            int32 dz = i32_sub(boat->pos.z, gz);
            int64_t sq = (int64_t)dx * dx + (int64_t)dz * dz;
            if (sq > 0 && sq < 1000LL * 1000LL) {
                if (boat->hSpeed < 80) {
                    int32 dist = phd_sqrt((uint32)sq);
                    if (dist > 0) {
                        boat->pos.x = i32_add(gx, (int32)((int64_t)dx * 1000 / dist));
                        boat->pos.z = i32_add(gz, (int32)((int64_t)dz * 1000 / dist));
                    }
                } else if (i32_sub(other->pos.y, boat->pos.y) < 2048) {
                    other->goalState = 2;
                    game_tr2_shatter(other, 0xf0u);
                    other->visibleMask &= 0x0fu;
                }
            }
        }
    }

    /* 5. Projected speed, crash damage and collision shift. */
    int32 shiftX = i32_sub(boat->pos.x, attemptedPos.x);
    int32 shiftZ = i32_sub(boat->pos.z, attemptedPos.z);
    bool collided = (shiftX != 0 || shiftZ != 0);
    if (collided || pitchDrift != 0) {
        int32 netX = i32_sub(boat->pos.x, oldPos.x);
        int32 netZ = i32_sub(boat->pos.z, oldPos.z);
        int32 proj = q14((int64_t)netX * sy + (int64_t)netZ * cy);
        if (oldSpeed > 0) {
            if (proj < boat->hSpeed)
                boat->hSpeed = (int16)(proj < 0 ? 0 : proj);
        } else if (oldSpeed < 0) {
            if (proj > boat->hSpeed)
                boat->hSpeed = (int16)(proj > 0 ? 0 : proj);
        } else if (pitchDrift != 0) {
            boat->hSpeed = (int16)proj;
        }
    }
    int32 speedLoss = (int32)oldSpeed - (int32)boat->hSpeed;
    if (ridden && oldSpeed >= 96 && speedLoss > 10) {
        lara->health = (int16)(lara->health - (speedLoss >> 1));
        lara->flags |= ITEM_FLAG_INJURED;
        soundPlay(31, &lara->pos);
    }

    /* 6. Final centre room, water height, supportY, roomFloor and suspension. */
    Room *newRoom = boat->room;
    const Sector *centreSector = game_tr2_room_sector(&newRoom, boat->pos.x, boat->pos.y, boat->pos.z);
    int32 centreFloor = Sector_getFloor(centreSector, boat->pos.x, boat->pos.y, boat->pos.z);
    const FloorData *centreTrigger = gs->gLastFloorData;
    int32 waterH = game_tr2_water_height(newRoom, boat->pos.x, boat->pos.z);
    boat->roomFloor = centreFloor - 5;
    hull->supportY = (waterH != WALL) ? (waterH - 5) : centreFloor;
    moveItemRoom(boat, newRoom);

    vec3i preSuspPos = vec3i_create(boat->pos.x, oldPos.y, boat->pos.z);
    vec3i bowL = boatWorldPoint(preSuspPos, oldPitch, boat->angle.y, terrainRoll, -300, 750);
    vec3i bowR = boatWorldPoint(preSuspPos, oldPitch, boat->angle.y, terrainRoll, 300, 750);
    int32 targetBowL = boatSampleOffset(newRoom, bowL.x, bowL.y, bowL.z);
    int32 targetBowR = boatSampleOffset(newRoom, bowR.x, bowR.y, bowR.z);

    int32 centreVS = boat->vSpeed;
    boat->pos.y = boatStepSuspension(boat->pos.y, &centreVS, hull->supportY);
    boat->vSpeed = (int16)centreVS;

    int32 yBowL = boatStepSuspension(bowL.y, &hull->bowVSpeed[0], targetBowL);
    int32 yBowR = boatStepSuspension(bowR.y, &hull->bowVSpeed[1], targetBowR);
    int32 meanBowY = (yBowL + yBowR) >> 1;
    int16 targetPitch = wrap_i16(game_tr2_bearing(750, i32_sub(boat->pos.y, meanBowY)));
    int16 targetRoll = wrap_i16(game_tr2_bearing(300, i32_sub(yBowR, meanBowY)));
    boat->angle.x = easeAngleHalf(oldPitch, targetPitch);
    terrainRoll = easeAngleHalf(terrainRoll, targetRoll);
    boat->angle.z = wrap_i16(terrainRoll + hull->bank);

    /* 7. Rider controls & animation OR empty/dead-rider coasting. */
    unsigned laraBase = gs->level.models[TR2_MODEL_LARA_BOAT].animIndex;
    if (ridden) {
        uint16 in = gs->tr2->boatInput;
        bool alive = lara->health > 0;
        bool airborne = (hull->supportY - boat->pos.y > 128) && boat->vSpeed > 0;

        if (!alive) {
            if (lara->state != 8)
                ItemObj_animSet(lara, laraBase + BOAT_CLIP_DEATH, true, 0);
            /* Dead rider: hull coasts like an empty boat. */
            if (boat->hSpeed >= 2) boat->hSpeed -= 1;
            else boat->hSpeed = 0;
            if (hull->turn < -45) hull->turn += 45;
            else if (hull->turn > 45) hull->turn -= 45;
            else hull->turn = 0;
        } else {
            if (airborne && lara->state != 6 && lara->state != 0 &&
                lara->state != 3 && lara->state != 4) {
                ItemObj_animSet(lara, laraBase + BOAT_CLIP_FALL, true, 0);
            } else if (collided && lara->state != 5 && lara->state != 6 &&
                       lara->state != 0 && lara->state != 3 && lara->state != 4) {
                int32 locX = q14((int64_t)cy * shiftX - (int64_t)sy * shiftZ);
                int32 locZ = q14((int64_t)sy * shiftX + (int64_t)cy * shiftZ);
                unsigned clip = abs(locX) >= abs(locZ)
                                    ? (locX >= 0 ? BOAT_CLIP_HIT_R : BOAT_CLIP_HIT_L)
                                    : (locZ < 0 ? BOAT_CLIP_HIT_B : BOAT_CLIP_HIT_F);
                ItemObj_animSet(lara, laraBase + clip, false, 0);
                lara->state = 5;
                lara->goalState = 5;
            }

            bool canDrive = (lara->state == 1 || lara->state == 2 || lara->state == 5) &&
                            waterH != WALL && (waterH - boat->pos.y) <= 128;
            bool suppressThrottle = (in & (IN_JUMP | IN_LOOK)) != 0;

            if (canDrive && !suppressThrottle) {
                if (in & IN_DOWN) {
                    if (boat->hSpeed > -20)
                        boat->hSpeed -= 5;
                } else if (in & IN_UP) {
                    int32 maxSpd = (in & IN_ACTION) ? 140 : (in & IN_WALK) ? 30 : 90;
                    if (boat->hSpeed < maxSpd)
                        boat->hSpeed = (int16)(boat->hSpeed + 2 + (5 * boat->hSpeed) / (2 * maxSpd));
                    else if (boat->hSpeed > maxSpd + 1)
                        boat->hSpeed -= 1;
                } else if (in & (IN_LEFT | IN_RIGHT)) {
                    if (boat->hSpeed >= 0 && boat->hSpeed < 20)
                        boat->hSpeed = 20;
                    else if (boat->hSpeed >= 20)
                        boat->hSpeed -= 1;
                    else
                        boat->hSpeed = 0;
                } else {
                    if (boat->hSpeed >= 2)
                        boat->hSpeed -= 1;
                    else
                        boat->hSpeed = 0;
                }

                int steer = (in & IN_LEFT) ? -1 : (in & IN_RIGHT) ? 1 : 0;
                if (in & IN_DOWN)
                    steer = -steer;
                if (steer < 0) {
                    if (hull->turn > 0) hull->turn -= 45;
                    else {
                        hull->turn -= 22;
                        if (hull->turn < -728) hull->turn = -728;
                    }
                } else if (steer > 0) {
                    if (hull->turn < 0) hull->turn += 45;
                    else {
                        hull->turn += 22;
                        if (hull->turn > 728) hull->turn = 728;
                    }
                } else {
                    if (hull->turn < -45) hull->turn += 45;
                    else if (hull->turn > 45) hull->turn -= 45;
                    else hull->turn = 0;
                }
            } else {
                if (hull->turn < -45) hull->turn += 45;
                else if (hull->turn > 45) hull->turn -= 45;
                else hull->turn = 0;
                if (canDrive && (in & IN_JUMP)) {
                    if (in & IN_RIGHT) lara->goalState = 3;
                    else if (in & IN_LEFT) lara->goalState = 4;
                }
            }

            if (lara->state == 1) {
                if (boat->hSpeed > 0)
                    lara->goalState = 2;
            } else if (lara->state == 2) {
                if (!suppressThrottle && boat->hSpeed <= 0)
                    lara->goalState = 1;
            } else if (lara->state == 6) {
                if (boat->pos.y >= hull->supportY || (hull->supportY - boat->pos.y) <= 128)
                    lara->goalState = 2;
            }
        }

        /* Evaluate ordinary and then heavy sector at boat's centre before alignment/release.
         * Return to resident dispatch when a trigger list exists to avoid unit re-entry. */
        if (centreTrigger) {
            call->trigger = centreTrigger;
            call->waterH = waterH;
            return TR2_BOAT_RES_TRIGGER;
        }
        if (TR64_RULE(CLIMB_WALL))
            gs->tr2->climbEnabled = false;
        game_tr2_boat_update_post(boat, lara, waterH);
        return TR2_BOAT_RES_DONE;
    } else {
        if (boat->hSpeed >= 2) boat->hSpeed -= 1;
        else boat->hSpeed = 0;
        if (hull->turn < -45) hull->turn += 45;
        else if (hull->turn > 45) hull->turn -= 45;
        else hull->turn = 0;
    }

    boatEmitWake(boat, waterH);
    return TR2_BOAT_RES_DONE;
}

void game_tr2_boat_camera(Lara *lara)
{
    Camera *c = &lara->extraL->camera;
    c->targetAngle.x = -3640;
    c->targetDist = 2048;
}

#endif
