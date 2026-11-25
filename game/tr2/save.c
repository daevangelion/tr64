/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/save_v3.h, game.h, item.h and lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Tomb Raider II mid-level persistent state serializer/deserializer, paged in
 * the `save` code unit. Behavioural contract: docs/specs/wall/tr2-death-save-and-finish.md §4.
 * Big-endian wire encoding; zero C struct padding, pointers, or RNG state. */
#include "../internal.h"
#include "../save.h"
#include "objects.h"
#include "tr64/res.h"

#if TR64_HAS_TR2

#define SAVE_OS __attribute__((optimize("Os")))

enum {
    TR2_SAVE_MAGIC = 0x54523253u, /* "TR2S" */
    TR2_SAVE_VERSION = 1u,
    TR2_SAVE_HDR_BYTES = 32u,
    M_POS = 1u << 0,
    M_ANIM = 1u << 1,
    M_HP = 1u << 2,
    M_FLAGS = 1u << 3,
    M_TYPE = 1u << 4,
    M_VIS = 1u << 5,
    M_UNLINKED = 1u << 6,
    M_CLEANUP = 1u << 7
};

typedef struct {
    uint8 *data;
    uint32 bytes, pos;
    bool reading, apply, failed;
} TR2IO;

__attribute__((noinline))
static uint32 ioByte(TR2IO *c, uint32 v)
{
    if (c->pos >= c->bytes) {
        c->failed = true;
        return 0;
    }
    if (c->reading)
        v = c->data[c->pos];
    else if (c->data)
        c->data[c->pos] = (uint8)v;
    ++c->pos;
    return v;
}

__attribute__((noinline))
static uint32 ioWord(TR2IO *c, uint32 v)
{
    uint32 hi = ioByte(c, v >> 8), lo = ioByte(c, v);
    return (hi << 8) | lo;
}

__attribute__((noinline))
static uint32 ioDword(TR2IO *c, uint32 v)
{
    uint32 hi = ioWord(c, v >> 16), lo = ioWord(c, v);
    return (hi << 16) | lo;
}

static uint32 rd32(const uint8 *p)
{
    return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
}

static void wr32(uint8 *p, uint32 v)
{
    p[0] = (uint8)(v >> 24);
    p[1] = (uint8)(v >> 16);
    p[2] = (uint8)(v >> 8);
    p[3] = (uint8)v;
}

static bool roomToIndex(const Room *r, uint8 *out)
{
    if (!r) {
        *out = 0xffu;
        return true;
    }
    uintptr_t p = (uintptr_t)r, b = (uintptr_t)gs->rooms;
    if (p < b || (p - b) % sizeof(Room))
        return false;
    uint32 idx = (uint32)((p - b) / sizeof(Room));
    if (idx >= gs->level.roomsCount || idx >= 0xffu)
        return false;
    *out = (uint8)idx;
    return true;
}

static bool itemIsLinked(unsigned idx)
{
    return (gs->unlinkedItems[idx >> 5] & (1u << (idx & 31))) == 0;
}

static void unlinkItem(ItemObj *item, unsigned idx)
{
    if (item->room && itemIsLinked(idx))
        Room_remove(item->room, item);
}

static void refreshRoomFloor(ItemObj *item)
{
    if (!item->room)
        return;
    const RoomInfo *info = item->room->info;
    int32 sx = i32_sub(item->pos.x, tr64_i32(info->x)) >> 10;
    int32 sz = i32_sub(item->pos.z, tr64_i32(info->z)) >> 10;
    if (sx >= 0 && sz >= 0 &&
        sx < (int32)tr64_u16(info->x_sectors) &&
        sz < (int32)tr64_u16(info->z_sectors)) {
        item->roomFloor = Room_getSector(item->room, item->pos.x, item->pos.z)->floor * 256;
    }
}

__attribute__((noinline))
static void ioVec3i(TR2IO *c, vec3i *v)
{
    v->x = wrap_i32(ioDword(c, (uint32)v->x));
    v->y = wrap_i32(ioDword(c, (uint32)v->y));
    v->z = wrap_i32(ioDword(c, (uint32)v->z));
}

__attribute__((noinline))
static void ioVec3s(TR2IO *c, vec3s *v)
{
    v->x = wrap_i16(ioWord(c, (uint16)v->x));
    v->y = wrap_i16(ioWord(c, (uint16)v->y));
    v->z = wrap_i16(ioWord(c, (uint16)v->z));
}

static void ioArm(TR2IO *c, ExtraInfoLara_Arm *arm, bool writeLive)
{
    ExtraInfoLara_Arm tmp = {0};
    if (!c->reading)
        tmp = *arm;
    tmp.animIndex = (uint16)ioWord(c, tmp.animIndex);
    tmp.frameIndex = (uint16)ioWord(c, tmp.frameIndex);
    ioVec3s(c, &tmp.angle);
    ioVec3s(c, &tmp.angleAim);
    tmp.flash.timer = wrap_i16(ioWord(c, (uint16)tmp.flash.timer));
    tmp.aim = ioByte(c, tmp.aim ? 1u : 0u) != 0;
    if (c->reading && tmp.animIndex != UINT16_MAX && tmp.animIndex >= gs->tr2->animCount)
        c->failed = true;
    if (c->apply && writeLive && !c->failed) {
        arm->animIndex = tmp.animIndex;
        arm->frameIndex = tmp.frameIndex;
        arm->angle = tmp.angle;
        arm->angleAim = tmp.angleAim;
        arm->flash.timer = tmp.flash.timer;
        arm->aim = tmp.aim;
        arm->target = NULL;
    }
}

static SAVE_OS bool validPackedKey(unsigned index, unsigned frame)
{
    if (index >= gs->tr2->animCount)
        return false;
    const Anim *anim = gs->level.anims + index;
    unsigned base = tr64_u32(anim->frame_offset) / 2u, extent = anim->frame_size;
    if (!anim->frame_rate || extent < 9u)
        return false;
    int32 elapsed = (int32)frame - (int32)tr64_u16(anim->frame_start);
    int32 key = elapsed / anim->frame_rate;
    int64_t offset = (int64_t)base + (int64_t)key * extent;
    unsigned n = gs->level.framesCount;
    return offset >= 0 && offset <= (int64_t)n && extent <= n - (unsigned)offset;
}

static SAVE_OS bool validPackedFrame(uint16 index, uint16 frame)
{
    if (index >= gs->tr2->animCount)
        return false;
    const Anim *anim = gs->level.anims + index;
    unsigned base = tr64_u32(anim->frame_offset) / 2u, extent = anim->frame_size;
    int32 rate = anim->frame_rate;
    if (!rate || extent < 9u)
        return false;
    int32 begin = tr64_u16(anim->frame_start), end = tr64_u16(anim->frame_end);
    int32 elapsed = (int32)frame - begin;
    int32 key = rate == 1 ? elapsed : elapsed / rate;
    int32 delta = rate == 1 ? 0 : elapsed - key * rate;
    unsigned n = gs->level.framesCount;
    int64_t offA = (int64_t)base + (int64_t)key * extent;
    if (offA < 0 || offA > (int64_t)n || extent > n - (unsigned)offA)
        return false;
    if (delta) {
        int32 nextFrame = (int32)frame - delta + rate;
        if (TR64_RULE(POSE_TAIL)) {
            if (nextFrame - begin > end)
                rate = end - (nextFrame - begin - rate);
            if (rate <= 0)
                return false;
            int64_t offB = offA + extent;
            if (offB < 0 || offB > (int64_t)n || extent > n - (unsigned)offB)
                return false;
        } else if (frame < (unsigned)begin || frame > (unsigned)end || nextFrame <= end) {
            int64_t offB = offA + extent;
            if (offB < 0 || offB > (int64_t)n || extent > n - (unsigned)offB)
                return false;
        } else {
            unsigned next = tr64_u16(anim->next_animation);
            unsigned target = tr64_u16(anim->next_frame);
            if (!validPackedKey(next, target))
                return false;
        }
    }
    return true;
}

static bool validRingType(uint16 type, unsigned *outPrio)
{
    unsigned prio = 0;
    if (type == 121u) {
        prio = 0u;
    } else if (type >= 157u && type <= 173u && type != 164u) {
        static const uint8 table[] = {
            1, 2, 3, 4, 7, 5, 6,
            0, 2, 3, 4, 7, 5, 6,
            11, 10, 8
        };
        prio = table[type - 157u];
    } else if (type >= 197u && type <= 200u) {
        prio = 101u + (type - 197u);
    } else if (type >= 178u && type <= 181u) {
        prio = 105u + (181u - type);
    } else if (type == 207u || type == 208u) {
        prio = 110u + (208u - type);
    } else {
        return false;
    }
    if (outPrio)
        *outPrio = prio;
    return true;
}

static bool validRuntimeType(uint16 origType, uint16 type)
{
    if (type == origType)
        return true;
    if (origType >= 182u && origType <= 185u && type == origType + 4u)
        return true;
    if (origType >= 136u && origType <= 141u && type == origType + 7u)
        return true;
    return false;
}

static uint32 coreRoomSource(uint32 room, uint8 flipped)
{
    if (flipped) {
        int32 alt = tr64_i16(tr64_level_rooms(gs->core, room)->alternate_room);
        if (alt >= 0 && (uint32)alt < gs->level.roomsCount)
            return (uint32)alt;
    }
    return room;
}

static const tr64_core_sectors_t *coreSectorBase(uint32 room, int32 x, int32 z, uint8 flipped)
{
    if (room >= gs->level.roomsCount)
        return NULL;
    uint32 src = coreRoomSource(room, flipped);
    const RoomInfo *r = tr64_level_rooms(gs->core, src);
    int32 sx = i32_sub(x, tr64_i32(r->x)) >> 10;
    int32 sz = i32_sub(z, tr64_i32(r->z)) >> 10;
    uint32 nx = tr64_u16(r->x_sectors), nz = tr64_u16(r->z_sectors);
    if (sx < 0 || sz < 0 || (uint32)sx >= nx || (uint32)sz >= nz)
        return NULL;
    uint32 idx = (uint32)sx * nz + (uint32)sz;
    if (idx > UINT16_MAX)
        return NULL;
    return tr64_level_sectors(gs->core, tr64_u32(r->sector_start) + idx);
}

static bool creatureHasNavBox(uint32 room, const vec3i *pos, uint8 flipped)
{
    uint32 cur = room;
    for (unsigned step = 0; step < 8u && cur < gs->level.roomsCount; ++step) {
        uint32 src = coreRoomSource(cur, flipped);
        const RoomInfo *r = tr64_level_rooms(gs->core, src);
        int32 nx = tr64_u16(r->x_sectors), nz = tr64_u16(r->z_sectors);
        if (nx <= 0 || nz <= 0)
            return false;
        int32 sx = i32_sub(pos->x, tr64_i32(r->x)) >> 10;
        int32 sz = i32_sub(pos->z, tr64_i32(r->z)) >> 10;
        if (sx < 0) sx = 0; else if (sx >= nx) sx = nx - 1;
        if (sz < 0) sz = 0; else if (sz >= nz) sz = nz - 1;
        const tr64_core_sectors_t *s =
            tr64_level_sectors(gs->core, tr64_u32(r->sector_start) + (uint32)sx * (uint32)nz + (uint32)sz);
        if (s->room_below != NO_ROOM && pos->y >= (int32)s->floor * 256) {
            cur = s->room_below;
            continue;
        }
        if (s->room_above != NO_ROOM && pos->y < (int32)s->ceiling * 256) {
            cur = s->room_above;
            continue;
        }
        return tr64_u16(s->box_index) != UINT16_MAX;
    }
    return false;
}

typedef struct {
    uint16 effType[MAX_ITEMS];
    uint8 effRoom[MAX_ITEMS];
    uint32 unlinked[MAX_ITEMS / 32u];
    uint32 hasBox[MAX_ITEMS / 32u];
} TR2CheckState;

static SAVE_OS void ioWorldAndStats(TR2IO *c, uint8 expectedFlipped)
{
    GameTR2 *tr2 = gs->tr2;
    uint32 time = gs->gSaveGame.time, dist = gs->gSaveGame.distance;
    uint32 shots = tr2->arms.ammoUsed, hits = tr2->ammoHits;
    uint16 kills = gs->gSaveGame.kills, medi = gs->gSaveGame.mediUsed;
    uint8 secrets = gs->gSaveGame.secrets, flipped = (uint8)gs->gSaveGame.flipped;
    uint8 mineChain = tr2->mineChain;
    int16 flipEff = tr2->flipEffect, flipTimer = tr2->flipEffectTimer;

    time = ioDword(c, time);
    dist = ioDword(c, dist);
    shots = ioDword(c, shots);
    hits = ioDword(c, hits);
    kills = (uint16)ioWord(c, kills);
    medi = (uint16)ioWord(c, medi);
    secrets = (uint8)ioByte(c, secrets);
    flipped = (uint8)ioByte(c, flipped);
    mineChain = (uint8)ioByte(c, mineChain);
    flipEff = wrap_i16(ioWord(c, (uint16)flipEff));
    flipTimer = wrap_i16(ioWord(c, (uint16)flipTimer));
    if (c->reading && (flipped > 1u || flipped != expectedFlipped))
        c->failed = true;
    if (c->apply && !c->failed) {
        gs->gSaveGame.time = time;
        gs->gSaveGame.distance = dist;
        tr2->arms.ammoUsed = shots;
        tr2->ammoHits = hits;
        gs->gSaveGame.kills = kills;
        gs->gSaveGame.mediUsed = medi;
        gs->gSaveGame.secrets = secrets;
        tr2->mineChain = mineChain;
        tr2->flipEffect = flipEff;
        tr2->flipEffectTimer = flipTimer;
    }

    for (unsigned i = 0; i < 10; ++i) {
        uint8 m = (uint8)ioByte(c, gs->flipMaps[i].mask);
        uint8 f = (uint8)ioByte(c, gs->flipMaps[i].flags);
        if (c->reading && (m > 31u || (f & ~3u)))
            c->failed = true;
        if (c->apply && !c->failed) {
            gs->flipMaps[i].mask = m;
            gs->flipMaps[i].flags = f;
        }
    }

    /* Compact CD track flags: 8-byte bitmap + 2 B per nonzero entry. */
    uint8 cdBits[8] = {0};
    if (!c->reading) {
        for (unsigned i = 0; i < 64; ++i)
            if (tr2->cdFlags[i])
                cdBits[i >> 3] |= (uint8)(1u << (i & 7));
    }
    for (unsigned i = 0; i < 8; ++i)
        cdBits[i] = (uint8)ioByte(c, cdBits[i]);
    if (c->apply)
        memset(tr2->cdFlags, 0, sizeof(tr2->cdFlags));
    for (unsigned i = 0; i < 64; ++i) {
        if (cdBits[i >> 3] & (1u << (i & 7))) {
            uint16 v = (uint16)ioWord(c, tr2->cdFlags[i]);
            if (c->apply)
                tr2->cdFlags[i] = v;
        }
    }

    uint32 camCount = gs->level.camerasCount;
    for (unsigned i = 0; i < camCount; ++i) {
        uint16 cf = (uint16)ioWord(c, gs->cameraFlags[i]);
        if (c->apply)
            gs->cameraFlags[i] = cf;
    }

    /* Inventory & ammo */
    for (unsigned i = 0; i < 7; ++i) {
        uint32 a = ioDword(c, tr2->ammo[i]);
        if (c->apply)
            tr2->ammo[i] = a;
    }
    uint8 lastW = (uint8)ioByte(c, tr2->lastWeapon);
    uint8 backW = (uint8)ioByte(c, tr2->backWeapon);
    uint8 carriedN = (uint8)ioByte(c, tr2->carriedCount);
    int16 mainSel = wrap_i16(ioWord(c, (uint16)tr2->mainSelection));
    int16 keySel = wrap_i16(ioWord(c, (uint16)tr2->keySelection));
    uint16 selKey = (uint16)ioWord(c, tr2->selectedKey);
    if (c->reading) {
        unsigned skPrio = 0;
        if (carriedN > TR2_CARRIED_CAPACITY || lastW > 8u ||
            (backW != 0u && backW != 3u && backW != 6u && backW != 7u && backW != 8u) ||
            (selKey != 0u && (!validRingType(selKey, &skPrio) || skPrio < 100u))) {
            c->failed = true;
            return;
        }
    }
    if (c->apply) {
        tr2->lastWeapon = lastW;
        tr2->backWeapon = backW;
        tr2->carriedCount = carriedN;
        tr2->mainSelection = mainSel;
        tr2->keySelection = keySel;
        tr2->selectedKey = selKey;
        memset(tr2->carried, 0, sizeof(tr2->carried));
    }
    uint32 seenTypes[8] = {0};
    unsigned prevPrio = 0;
    for (unsigned i = 0; i < carriedN; ++i) {
        uint16 t = (uint16)ioWord(c, tr2->carried[i].type);
        uint16 q = (uint16)ioWord(c, tr2->carried[i].quantity);
        if (c->reading) {
            unsigned prio = 0;
            if (!validRingType(t, &prio) || q == 0u ||
                (seenTypes[t >> 5] & (1u << (t & 31))) ||
                (i > 0 && prio < prevPrio)) {
                c->failed = true;
            } else {
                seenTypes[t >> 5] |= 1u << (t & 31);
                prevPrio = prio;
            }
        }
        if (c->apply && !c->failed) {
            tr2->carried[i].type = t;
            tr2->carried[i].quantity = q;
        }
    }
}

static SAVE_OS void ioLara(TR2IO *c, bool *outHasShotgunPose, int16 *outVehicle)
{
    GameTR2 *tr2 = gs->tr2;
    Lara *lara = gs->players[0];
    if (!lara || !lara->extraL) {
        c->failed = true;
        return;
    }
    ExtraInfoLara *e = lara->extraL;
    uint16 expectedIdx = (uint16)(lara - gs->items);
    uint16 laraIdx = (uint16)ioWord(c, expectedIdx);
    if (c->reading && laraIdx != expectedIdx) {
        c->failed = true;
        return;
    }

    uint8 room = 0;
    if (!c->reading && !roomToIndex(lara->room, &room)) {
        c->failed = true;
        return;
    }
    room = (uint8)ioByte(c, room);
    if (c->reading && room >= gs->level.roomsCount) {
        c->failed = true;
        return;
    }

    vec3i pos = lara->pos;
    vec3s ang = lara->angle;
    int16 hs = lara->hSpeed, vs = lara->vSpeed, hp = lara->health, air = lara->timer;
    uint16 anim = lara->animIndex, frame = lara->frameIndex, fl = lara->flags;
    uint16 lfl = tr2->laraFlags;
    int16 lit = tr2->laraItemTimer, rfl = lara->roomFloor, turn = lara->turnSpeed;
    uint8 st = lara->state, nst = lara->nextState, gst = lara->goalState, wst = lara->waterState;
    uint32 vmask = lara->visibleMask;

    ioVec3i(c, &pos);
    ioVec3s(c, &ang);
    hs = wrap_i16(ioWord(c, (uint16)hs));
    vs = wrap_i16(ioWord(c, (uint16)vs));
    anim = (uint16)ioWord(c, anim);
    frame = (uint16)ioWord(c, frame);
    st = (uint8)ioByte(c, st);
    nst = (uint8)ioByte(c, nst);
    gst = (uint8)ioByte(c, gst);
    wst = (uint8)ioByte(c, wst);
    hp = wrap_i16(ioWord(c, (uint16)hp));
    air = wrap_i16(ioWord(c, (uint16)air));
    fl = (uint16)ioWord(c, fl);
    lfl = (uint16)ioWord(c, lfl);
    lit = wrap_i16(ioWord(c, (uint16)lit));
    rfl = wrap_i16(ioWord(c, (uint16)rfl));
    turn = wrap_i16(ioWord(c, (uint16)turn));
    vmask = ioDword(c, vmask);

    if (c->reading && (!validPackedFrame(anim, frame) || wst > WATER_STATE_UNDER)) {
        c->failed = true;
        return;
    }
    if (c->apply && !c->failed) {
        Room *dst = gs->rooms + room;
        if (lara->room != dst) {
            if (lara->room)
                Room_remove(lara->room, lara);
            Room_add(dst, lara);
        }
        lara->pos = pos;
        lara->angle = ang;
        lara->hSpeed = hs;
        lara->vSpeed = vs;
        lara->animIndex = anim;
        lara->frameIndex = frame;
        lara->state = st;
        lara->nextState = nst;
        lara->goalState = gst;
        lara->waterState = wst;
        lara->health = hp;
        lara->timer = air;
        lara->flags = fl;
        tr2->laraFlags = lfl;
        tr2->laraItemTimer = lit;
        lara->roomFloor = rfl;
        lara->turnSpeed = turn;
        lara->visibleMask = vmask;
    }

    uint8 wstate = e->weaponState, wep = e->weapon, gwep = e->goalWeapon, vhack = e->vSpeedHack;
    int16 swimT = e->swimTimer, moveA = e->moveAngle, hitF = e->hitFrame;
    int8 hitT = e->hitTimer, hitQ = e->hitQuadrant, hpT = e->healthTimer;
    uint8 haz = e->hazardCause;
    vec3s headA = e->head.angle, torsoA = e->torso.angle;

    wstate = (uint8)ioByte(c, wstate);
    wep = (uint8)ioByte(c, wep);
    gwep = (uint8)ioByte(c, gwep);
    vhack = (uint8)ioByte(c, vhack);
    swimT = wrap_i16(ioWord(c, (uint16)swimT));
    moveA = wrap_i16(ioWord(c, (uint16)moveA));
    hitF = wrap_i16(ioWord(c, (uint16)hitF));
    hitT = (int8)ioByte(c, (uint8)hitT);
    hitQ = (int8)ioByte(c, (uint8)hitQ);
    hpT = (int8)ioByte(c, (uint8)hpT);
    haz = (uint8)ioByte(c, haz);
    ioVec3s(c, &headA);
    ioVec3s(c, &torsoA);

    if (c->reading && (wstate > WEAPON_STATE_READY || wep > 8u || gwep > 8u)) {
        c->failed = true;
        return;
    }
    if (c->apply && !c->failed) {
        e->weaponState = wstate;
        e->weapon = wep;
        e->goalWeapon = gwep;
        e->vSpeedHack = vhack;
        e->swimTimer = swimT;
        e->moveAngle = moveA;
        e->hitFrame = hitF;
        e->hitTimer = hitT;
        e->hitQuadrant = hitQ;
        e->healthTimer = hpT;
        e->hazardCause = haz;
        e->head.angle = headA;
        e->torso.angle = torsoA;
    }

    ioArm(c, &e->armR, true);
    ioArm(c, &e->armL, true);
    uint32 meshPtrCount = gs->core ? gs->core->tables[TR64_CORE_mesh_pointers - 1].count : 0u;
    for (unsigned i = 0; i < JOINT_MAX; ++i) {
        uint16 m = (uint16)ioWord(c, e->meshes[i]);
        if (c->reading && m >= meshPtrCount)
            c->failed = true;
        if (c->apply && !c->failed)
            e->meshes[i] = m;
    }

    uint16 fAge = tr2->arms.flareAge, fFrame = tr2->arms.flareFrame;
    uint8 uzi0 = tr2->arms.uziActive[0], uzi1 = tr2->arms.uziActive[1];
    uint8 bits = 0;
    if (!c->reading) {
        bits = (uint8)((tr2->arms.flareLeft ? 1u : 0u) |
                       (tr2->arms.flareLit ? 2u : 0u) |
                       (tr2->arms.gunRequested ? 4u : 0u) |
                       (tr2->climbEnabled ? 8u : 0u) |
                       (tr2->extraAnim ? 16u : 0u) |
                       (tr2->runJumpArmed ? 32u : 0u) |
                       (gs->gLaraHazardState[0].burnTicks ? 64u : 0u) |
                       (tr2->shotgunPose ? 128u : 0u));
    }
    fAge = (uint16)ioWord(c, fAge);
    fFrame = (uint16)ioWord(c, fFrame);
    uzi0 = (uint8)ioByte(c, uzi0);
    uzi1 = (uint8)ioByte(c, uzi1);
    bits = (uint8)ioByte(c, bits);

    int32 wdist = tr2->waterDistance;
    int16 sAng = tr2->slideAngle;
    uint16 dTicks = tr2->deathTicks;
    int16 veh = tr2->vehicle;
    int32 df0 = tr2->distanceFrom[0], df1 = tr2->distanceFrom[1], df2 = tr2->distanceFrom[2];
    uint16 msA = tr2->motionStart.anim, msF = tr2->motionStart.frame;
    uint8 msS = tr2->motionStart.state;

    wdist = wrap_i32(ioDword(c, (uint32)wdist));
    sAng = wrap_i16(ioWord(c, (uint16)sAng));
    dTicks = (uint16)ioWord(c, dTicks);
    veh = wrap_i16(ioWord(c, (uint16)veh));
    df0 = wrap_i32(ioDword(c, (uint32)df0));
    df1 = wrap_i32(ioDword(c, (uint32)df1));
    df2 = wrap_i32(ioDword(c, (uint32)df2));
    msA = (uint16)ioWord(c, msA);
    msF = (uint16)ioWord(c, msF);
    msS = (uint8)ioByte(c, msS);

    if (c->reading) {
        if (veh < -1 || veh >= (int32)gs->level.itemsCount || (veh >= 0 && !tr2->boatUnit))
            c->failed = true;
        if (msA != UINT16_MAX && msA >= tr2->animCount)
            c->failed = true;
    }
    *outVehicle = veh;
    if (c->apply && !c->failed) {
        tr2->arms.flareAge = fAge;
        tr2->arms.flareFrame = fFrame;
        tr2->arms.uziActive[0] = uzi0;
        tr2->arms.uziActive[1] = uzi1;
        tr2->arms.flareLeft = (bits & 1u) != 0;
        tr2->arms.flareLit = (bits & 2u) != 0;
        tr2->arms.gunRequested = (bits & 4u) != 0;
        tr2->climbEnabled = (bits & 8u) != 0;
        tr2->extraAnim = (bits & 16u) != 0;
        tr2->runJumpArmed = (bits & 32u) != 0;
        tr2->savePost.burning = (bits & 64u) != 0;
        tr2->waterDistance = wdist;
        tr2->slideAngle = sAng;
        tr2->deathTicks = dTicks;
        tr2->deathMenuPending = false;
        tr2->vehicle = veh;
        tr2->distanceFrom[0] = df0;
        tr2->distanceFrom[1] = df1;
        tr2->distanceFrom[2] = df2;
        tr2->motionStart.anim = msA;
        tr2->motionStart.frame = msF;
        tr2->motionStart.state = msS;
    }

    bool hasSg = (bits & 128u) != 0;
    *outHasShotgunPose = hasSg;
    if (c->reading) {
        if ((wep == 5u || wep == 7u) && wstate >= WEAPON_STATE_DRAW) {
            c->failed = true;
            return;
        }
        if ((wep == WEAPON_SHOTGUN || wep == TR2_WEAPON_GRENADE) &&
            wstate >= WEAPON_STATE_DRAW && !hasSg) {
            c->failed = true;
            return;
        }
    }
    if (hasSg) {
        const ItemObj *src = tr2->shotgunPose;
        uint16 sgType = src ? src->type : TR2_MODEL_SHOTGUN;
        uint16 sgAnim = src ? src->animIndex : 0;
        uint16 sgFrame = src ? src->frameIndex : 0;
        uint8 sgSt = src ? src->state : 0;
        uint8 sgGst = src ? src->goalState : 0;
        uint8 sgNst = src ? src->nextState : 0;
        uint16 sgFl = src ? src->flags : 0;

        sgType = (uint16)ioWord(c, sgType);
        sgAnim = (uint16)ioWord(c, sgAnim);
        sgFrame = (uint16)ioWord(c, sgFrame);
        sgSt = (uint8)ioByte(c, sgSt);
        sgGst = (uint8)ioByte(c, sgGst);
        sgNst = (uint8)ioByte(c, sgNst);
        sgFl = (uint16)ioWord(c, sgFl);

        if (c->reading) {
            uint16 wantType = (wep == TR2_WEAPON_GRENADE) ? TR2_MODEL_GRENADE :
                              (wep == WEAPON_SHOTGUN) ? TR2_MODEL_SHOTGUN : 0u;
            uint32 bankClips = (sgType == TR2_MODEL_GRENADE) ? 6u : 5u;
            uint16 sgBase = (sgType < TR2_PUBLIC_TYPES) ? gs->level.models[sgType].animIndex : UINT16_MAX;
            if (gs->level.itemsCount >= MAX_ITEMS || !wantType || sgType != wantType ||
                gs->level.models[sgType].count <= 0 || sgBase >= tr2->animCount ||
                sgAnim < sgBase || sgAnim >= sgBase + bankClips ||
                !validPackedFrame(sgAnim, sgFrame))
                c->failed = true;
        }
        if (c->apply && !c->failed) {
            ItemObj *pose = gs->firstFree;
            if (!pose) {
                c->failed = true;
                return;
            }
            gs->firstFree = pose->nextItem;
            ItemObj_resetSlot(pose);
            pose->type = sgType;
            pose->pos = lara->pos;
            pose->animIndex = sgAnim;
            pose->frameIndex = sgFrame;
            pose->state = sgSt;
            pose->goalState = sgGst;
            pose->nextState = sgNst;
            pose->flags = sgFl;
            tr2->shotgunPose = pose;
        }
    }
}

static uint8 computeItemMask(unsigned i, const ItemObj *item)
{
    const tr64_core_items_t *s = tr64_level_items(gs->core, i);
    uint8 mask = M_FLAGS;
    uint8 curRoom = 0xffu;
    (void)roomToIndex(item->room, &curRoom);
    if (curRoom != tr64_u16(s->room) ||
        item->pos.x != tr64_i32(s->x) || item->pos.y != tr64_i32(s->y) ||
        item->pos.z != tr64_i32(s->z) ||
        item->angle.x != 0 || item->angle.z != 0 || item->angle.y != tr64_i16(s->angle) ||
        item->hSpeed != 0 || item->vSpeed != 0)
        mask |= M_POS;
    if (item->animIndex != UINT16_MAX &&
        (game_tr2_item_ticks(item->type) || tr2BlockType(item->type) ||
         (item->flags & (ITEM_FLAG_ACTIVE | ITEM_FLAG_STATUS)) != 0))
        mask |= M_ANIM;
    if (game_tr2_creature_type(item->type) || item->health != -0x4000)
        mask |= M_HP;
    if (item->type != tr64_u16(s->type))
        mask |= M_TYPE;
    if (item->visibleMask != UINT32_MAX)
        mask |= M_VIS;
    if (!item->room || !itemIsLinked(i))
        mask |= M_UNLINKED;
    if (gs->tr2->cleanupEligible[i >> 5] & (1u << (i & 31)))
        mask |= M_CLEANUP;
    return mask;
}

static SAVE_OS void ioPlacedItems(TR2IO *c, TR2CheckState *chk, uint8 expectedFlipped)
{
    GameTR2 *tr2 = gs->tr2;
    uint32 n = gs->level.itemsCount;
    unsigned laraIdx = (unsigned)(gs->players[0] - gs->items);

    if (c->reading && chk)
        memset(chk, 0, sizeof(*chk));
    if (c->apply) {
        /* Rebuild active list cleanly from restored flags. */
        while (gs->firstActive) {
            ItemObj *a = gs->firstActive;
            gs->firstActive = a->nextActive;
            a->nextActive = NULL;
            a->flags &= ~ITEM_FLAG_ACTIVE;
        }
        memset(tr2->cleanupEligible, 0, sizeof(tr2->cleanupEligible));
        memset(tr2->cleanupList, 0, sizeof(tr2->cleanupList));
    }

    for (unsigned i = 0; i < n; ++i) {
        if (i == laraIdx) {
            if (c->reading && chk) {
                chk->effType[i] = 0u;
                chk->effRoom[i] = 0xffu;
            }
            continue;
        }
        ItemObj *item = gs->items + i;
        const tr64_core_items_t *src = tr64_level_items(gs->core, i);
        uint16 origType = tr64_u16(src->type);
        uint8 origRoom = (uint8)tr64_u16(src->room);
        vec3i origPos = {tr64_i32(src->x), tr64_i32(src->y), tr64_i32(src->z)};
        vec3s origAng = {0, tr64_i16(src->angle), 0};
        uint8 mask = c->reading ? 0 : computeItemMask(i, item);
        mask = (uint8)ioByte(c, mask);

        uint16 type = c->reading ? origType : item->type;
        if (mask & M_TYPE) {
            type = (uint16)ioWord(c, item->type);
            if (c->reading && !validRuntimeType(origType, type)) {
                c->failed = true;
                return;
            }
            if (c->apply)
                item->type = type;
        }

        uint8 room = origRoom;
        vec3i pos = c->reading ? origPos : item->pos;
        vec3s ang = c->reading ? origAng : item->angle;
        int16 hs = c->reading ? 0 : item->hSpeed;
        int16 vs = c->reading ? 0 : item->vSpeed;
        if (mask & M_POS) {
            if (!c->reading && !roomToIndex(item->room, &room)) {
                c->failed = true;
                return;
            }
            room = (uint8)ioByte(c, room);
            ioVec3i(c, &pos);
            ioVec3s(c, &ang);
            hs = wrap_i16(ioWord(c, (uint16)hs));
            vs = wrap_i16(ioWord(c, (uint16)vs));
            if (c->reading && room != 0xffu && room >= gs->level.roomsCount) {
                c->failed = true;
                return;
            }
            if (c->reading && tr2DoorType(type) &&
                (room != origRoom || pos.x != origPos.x || pos.y != origPos.y || pos.z != origPos.z ||
                 ang.x != origAng.x || ang.y != origAng.y || ang.z != origAng.z)) {
                c->failed = true;
                return;
            }
        }

        uint16 anim = item->animIndex, frame = item->frameIndex;
        uint8 st = item->state, gst = item->goalState, nst = item->nextState;
        if (mask & M_ANIM) {
            anim = (uint16)ioWord(c, anim);
            frame = (uint16)ioWord(c, frame);
            st = (uint8)ioByte(c, st);
            gst = (uint8)ioByte(c, gst);
            nst = (uint8)ioByte(c, nst);
            if (c->reading && anim != UINT16_MAX && !validPackedFrame(anim, frame)) {
                c->failed = true;
                return;
            }
        }

        int16 hp = item->health;
        if (mask & M_HP)
            hp = wrap_i16(ioWord(c, (uint16)hp));

        uint16 fl = item->flags;
        int16 timer = item->timer;
        if (mask & M_FLAGS) {
            fl = (uint16)ioWord(c, fl);
            timer = wrap_i16(ioWord(c, (uint16)timer));
        }

        uint32 vis = item->visibleMask;
        if (mask & M_VIS)
            vis = ioDword(c, vis);

        if (c->reading && chk) {
            uint8 effR = (mask & M_POS) ? room : origRoom;
            chk->effType[i] = type;
            chk->effRoom[i] = effR;
            if ((mask & M_UNLINKED) || effR == 0xffu)
                chk->unlinked[i >> 5] |= 1u << (i & 31);
            /* Pre-validate world-geometry invariants before game_prepare_tr2_load(). */
            if (tr2BlockType(type) && effR != 0xffu &&
                ((mask & M_ANIM) ? st == 1u : true) &&
                (fl & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE) {
                const tr64_core_sectors_t *sec = coreSectorBase(effR, pos.x, pos.z, expectedFlipped);
                if (!sec || sec->floor == NO_FLOOR || (int32)sec->floor - 4 <= NO_FLOOR) {
                    c->failed = true;
                    return;
                }
            } else if (type == TR2_ITEM_WINDOW && effR != 0xffu && (fl & ITEM_FLAG_COLLISION)) {
                if (!coreSectorBase(effR, pos.x, pos.z, expectedFlipped)) {
                    c->failed = true;
                    return;
                }
            }
            if (game_tr2_creature_type(type) && effR < gs->level.roomsCount &&
                creatureHasNavBox(effR, &pos, expectedFlipped))
                chk->hasBox[i >> 5] |= 1u << (i & 31);
        }

        if (!c->apply || c->failed)
            continue;

        if (mask & M_POS) {
            item->pos = pos;
            item->angle = ang;
            item->hSpeed = hs;
            item->vSpeed = vs;
            if (room == 0xffu) {
                unlinkItem(item, i);
                item->room = NULL;
            } else {
                Room *dst = gs->rooms + room;
                if (mask & M_UNLINKED) {
                    unlinkItem(item, i);
                    item->room = dst;
                    gs->unlinkedItems[i >> 5] |= 1u << (i & 31);
                } else if (item->room != dst || !itemIsLinked(i)) {
                    unlinkItem(item, i);
                    Room_add(dst, item);
                }
            }
            refreshRoomFloor(item);
        } else if (mask & M_UNLINKED) {
            unlinkItem(item, i);
            gs->unlinkedItems[i >> 5] |= 1u << (i & 31);
        } else if (!item->room || !itemIsLinked(i)) {
            if (origRoom < gs->level.roomsCount) {
                unlinkItem(item, i);
                Room_add(gs->rooms + origRoom, item);
                refreshRoomFloor(item);
            }
        }

        if (mask & M_ANIM) {
            item->animIndex = anim;
            item->frameIndex = frame;
            item->state = st;
            item->goalState = gst;
            item->nextState = nst;
        }
        if (mask & M_HP)
            item->health = hp;
        if (mask & M_FLAGS) {
            bool active = (fl & ITEM_FLAG_ACTIVE) != 0;
            item->flags = (uint16)(fl & ~ITEM_FLAG_ACTIVE);
            item->timer = timer;
            if (active && item->room)
                ItemObj_activateBase(item);
        }
        if (mask & M_VIS)
            item->visibleMask = vis;

        /* Type-specific post-restore invariants (tr2-death-save-and-finish.md §4). */
        if ((item->type == 72u || item->type == 73u) && (item->flags & ITEM_FLAG_ONCE))
            item->visibleMask = 0x100u;
        if (item->type == TR2_ITEM_MINE && (item->flags & ITEM_FLAG_ONCE))
            item->visibleMask = 1u;
        if (item->type >= 182u && item->type <= 185u &&
            ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE ||
             (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE))
            item->type = (uint16)(item->type + 4u);

        if (mask & M_CLEANUP)
            tr2->cleanupEligible[i >> 5] |= 1u << (i & 31);
        if (game_tr2_creature_type(item->type) && item->health <= 0 &&
            (tr2->cleanupEligible[i >> 5] & (1u << (i & 31))) &&
            !(item->flags & TR2_PLACED_CLEANUP))
            tr2->cleanupList[i >> 5] |= 1u << (i & 31);
    }
}

static SAVE_OS void ioCreaturesCargoBoatsFlares(TR2IO *c, const TR2CheckState *chk,
                                        bool hasShotgunPose, int16 vehicle)
{
    GameTR2 *tr2 = gs->tr2;
    unsigned laraIdx = (unsigned)(gs->players[0] - gs->items);

    if (c->reading && chk && vehicle >= 0 && chk->effType[vehicle] != TR2_ITEM_BOAT) {
        c->failed = true;
        return;
    }

    /* Active creatures (up to TR2_CREATURE_SLOTS = 5). */
    uint8 cCount = 0;
    if (!c->reading) {
        for (unsigned i = 0; i < TR2_CREATURE_SLOTS; ++i)
            if (tr2->creatures[i].item)
                ++cCount;
    }
    cCount = (uint8)ioByte(c, cCount);
    if (c->reading && (cCount > TR2_CREATURE_SLOTS || (cCount > 0 && !tr2->aiUnit))) {
        c->failed = true;
        return;
    }
    if (c->apply)
        tr2->savePost.creatureCount = cCount;
    if (!c->reading) {
        for (unsigned i = 0; i < TR2_CREATURE_SLOTS; ++i) {
            const TR2Creature *ai = tr2->creatures + i;
            if (!ai->item)
                continue;
            uint16 idx = (uint16)(ai->item - gs->items);
            (void)ioWord(c, idx);
            (void)ioWord(c, (uint16)ai->jointYaw[0]);
            (void)ioWord(c, (uint16)ai->jointYaw[1]);
            (void)ioWord(c, (uint16)ai->maxTurn);
            (void)ioWord(c, ai->flags);
            (void)ioByte(c, ai->mood);
        }
    } else {
        uint32 seenC[MAX_ITEMS / 32u] = {0};
        for (unsigned i = 0; i < cCount; ++i) {
            uint16 idx = (uint16)ioWord(c, 0);
            int16 jy0 = wrap_i16(ioWord(c, 0));
            int16 jy1 = wrap_i16(ioWord(c, 0));
            int16 mt = wrap_i16(ioWord(c, 0));
            uint16 fl = (uint16)ioWord(c, 0);
            uint8 mood = (uint8)ioByte(c, 0);
            if (idx >= gs->level.itemsCount || idx == laraIdx ||
                (seenC[idx >> 5] & (1u << (idx & 31))) ||
                (chk && (!game_tr2_creature_type(chk->effType[idx]) ||
                         chk->effRoom[idx] >= gs->level.roomsCount ||
                         !(chk->hasBox[idx >> 5] & (1u << (idx & 31)))))) {
                c->failed = true;
            } else {
                seenC[idx >> 5] |= 1u << (idx & 31);
            }
            if (c->apply && !c->failed) {
                tr2->savePost.creatures[i].itemIndex = idx;
                tr2->savePost.creatures[i].jointYaw[0] = jy0;
                tr2->savePost.creatures[i].jointYaw[1] = jy1;
                tr2->savePost.creatures[i].maxTurn = mt;
                tr2->savePost.creatures[i].flags = fl;
                tr2->savePost.creatures[i].mood = mood;
            }
        }
    }

    /* Carried drops (cargoHead / cargoNext chains). */
    uint8 carrierCount = 0;
    if (!c->reading) {
        for (unsigned i = 0; i < gs->level.itemsCount; ++i)
            if (tr2->cargoHead[i] >= 0)
                ++carrierCount;
    }
    carrierCount = (uint8)ioByte(c, carrierCount);
    if (c->apply) {
        for (unsigned i = 0; i < 256; ++i)
            tr2->cargoHead[i] = tr2->cargoNext[i] = -1;
    }
    if (!c->reading) {
        for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
            if (tr2->cargoHead[i] < 0)
                continue;
            uint8 len = 0;
            for (int16 p = tr2->cargoHead[i]; p >= 0 && len < 16; p = tr2->cargoNext[p])
                ++len;
            (void)ioByte(c, i);
            (void)ioByte(c, len);
            for (int16 p = tr2->cargoHead[i]; p >= 0 && len; p = tr2->cargoNext[p], --len)
                (void)ioByte(c, (uint8)p);
        }
    } else {
        uint32 seenOwner[MAX_ITEMS / 32u] = {0};
        uint32 seenDrop[MAX_ITEMS / 32u] = {0};
        for (unsigned i = 0; i < carrierCount; ++i) {
            uint8 owner = (uint8)ioByte(c, 0);
            uint8 len = (uint8)ioByte(c, 0);
            if (owner >= gs->level.itemsCount || owner == laraIdx ||
                (seenOwner[owner >> 5] & (1u << (owner & 31))) ||
                len == 0 || len > 16) {
                c->failed = true;
                return;
            }
            seenOwner[owner >> 5] |= 1u << (owner & 31);
            int16 prev = -1;
            for (unsigned j = 0; j < len; ++j) {
                uint8 drop = (uint8)ioByte(c, 0);
                if (drop >= gs->level.itemsCount || drop == laraIdx || drop == owner ||
                    (seenDrop[drop >> 5] & (1u << (drop & 31))) ||
                    (chk && !(chk->unlinked[drop >> 5] & (1u << (drop & 31))))) {
                    c->failed = true;
                    return;
                }
                seenDrop[drop >> 5] |= 1u << (drop & 31);
                if (c->apply) {
                    if (prev < 0)
                        tr2->cargoHead[owner] = (int16)drop;
                    else
                        tr2->cargoNext[prev] = (int16)drop;
                    prev = (int16)drop;
                }
            }
        }
    }

    /* Boat hull dynamics (TR2BoatHull). */
    uint8 bCount = (uint8)ioByte(c, tr2->boatCount);
    if (c->reading && (bCount > TR2_BOAT_SLOTS || (bCount > 0 && !tr2->boatUnit))) {
        c->failed = true;
        return;
    }
    if (c->apply) {
        tr2->boatCount = bCount;
        for (unsigned i = 0; i < TR2_BOAT_SLOTS; ++i)
            tr2->boatHulls[i] = (TR2BoatHull){.itemIndex = -1, .present = 0};
    }
    uint32 seenBoat[MAX_ITEMS / 32u] = {0};
    for (unsigned i = 0; i < bCount; ++i) {
        TR2BoatHull h = tr2->boatHulls[i];
        h.itemIndex = wrap_i16(ioWord(c, (uint16)h.itemIndex));
        h.present = (uint8)ioByte(c, h.present);
        h.turn = wrap_i32(ioDword(c, (uint32)h.turn));
        h.bowVSpeed[0] = wrap_i32(ioDword(c, (uint32)h.bowVSpeed[0]));
        h.bowVSpeed[1] = wrap_i32(ioDword(c, (uint32)h.bowVSpeed[1]));
        h.supportY = wrap_i32(ioDword(c, (uint32)h.supportY));
        h.bank = wrap_i16(ioWord(c, (uint16)h.bank));
        h.terrainYaw = wrap_i16(ioWord(c, (uint16)h.terrainYaw));
        if (c->reading) {
            if (h.present > 1u || h.itemIndex < 0 || h.itemIndex >= (int32)gs->level.itemsCount ||
                (seenBoat[(unsigned)h.itemIndex >> 5] & (1u << ((unsigned)h.itemIndex & 31))) ||
                (chk && chk->effType[h.itemIndex] != TR2_ITEM_BOAT)) {
                c->failed = true;
            } else {
                seenBoat[(unsigned)h.itemIndex >> 5] |= 1u << ((unsigned)h.itemIndex & 31);
            }
        }
        if (c->apply && !c->failed)
            tr2->boatHulls[i] = h;
    }

    /* Elevator per-instance state (TR2Elevator: itemIndex, yStart, waitCounter). */
    uint8 eCount = (uint8)ioByte(c, tr2->elevatorCount);
    if (c->reading) {
        uint8 expectedEl = 0;
        if (chk) {
            for (unsigned j = 0; j < gs->level.itemsCount && expectedEl < TR2_ELEVATOR_SLOTS; ++j)
                if (chk->effType[j] == TR2_ITEM_ELEVATOR)
                    ++expectedEl;
        }
        if (eCount > TR2_ELEVATOR_SLOTS || (eCount > 0 && !tr2->operaUnit) ||
            (chk && eCount != expectedEl)) {
            c->failed = true;
            return;
        }
    }
    if (c->apply) {
        tr2->elevatorCount = eCount;
        for (unsigned i = 0; i < TR2_ELEVATOR_SLOTS; ++i)
            tr2->elevators[i] = (TR2Elevator){.itemIndex = -1, .present = 0};
    }
    uint32 seenEl[MAX_ITEMS / 32u] = {0};
    for (unsigned i = 0; i < eCount; ++i) {
        TR2Elevator el = tr2->elevators[i];
        el.itemIndex = wrap_i16(ioWord(c, (uint16)el.itemIndex));
        el.present = (uint8)ioByte(c, el.present);
        el.yStart = wrap_i32(ioDword(c, (uint32)el.yStart));
        el.waitCounter = wrap_i32(ioDword(c, (uint32)el.waitCounter));
        if (c->reading) {
            if (el.present != 1u || el.waitCounter < 0 || el.waitCounter > 90 ||
                el.itemIndex < 0 || el.itemIndex >= (int32)gs->level.itemsCount ||
                (seenEl[(unsigned)el.itemIndex >> 5] & (1u << ((unsigned)el.itemIndex & 31))) ||
                (chk && chk->effType[el.itemIndex] != TR2_ITEM_ELEVATOR)) {
                c->failed = true;
            } else {
                seenEl[(unsigned)el.itemIndex >> 5] |= 1u << ((unsigned)el.itemIndex & 31);
            }
        }
        if (c->apply && !c->failed)
            tr2->elevators[i] = el;
    }

    /* Active dynamic world flares (TR2_ITEM_LOOSE_FLARE at index >= itemsCount). */
    uint8 flareCount = 0;
    if (!c->reading) {
        for (unsigned i = gs->level.itemsCount; i < MAX_ITEMS; ++i) {
            const ItemObj *it = gs->items + i;
            if (it->room && it->type == TR2_ITEM_LOOSE_FLARE && (it->flags & ITEM_FLAG_ACTIVE))
                ++flareCount;
        }
    }
    flareCount = (uint8)ioByte(c, flareCount);
    uint32 usedSlots = gs->level.itemsCount + (hasShotgunPose ? 1u : 0u);
    if (c->reading && (usedSlots > MAX_ITEMS || flareCount > MAX_ITEMS - usedSlots)) {
        c->failed = true;
        return;
    }
    if (!c->reading) {
        for (unsigned i = gs->level.itemsCount; i < MAX_ITEMS; ++i) {
            const ItemObj *it = gs->items + i;
            if (!it->room || it->type != TR2_ITEM_LOOSE_FLARE || !(it->flags & ITEM_FLAG_ACTIVE))
                continue;
            uint8 room = 0;
            if (!roomToIndex(it->room, &room)) {
                c->failed = true;
                return;
            }
            vec3i pos = it->pos;
            vec3s ang = it->angle;
            (void)ioByte(c, room);
            ioVec3i(c, &pos);
            ioVec3s(c, &ang);
            (void)ioWord(c, (uint16)it->hSpeed);
            (void)ioWord(c, (uint16)it->vSpeed);
            (void)ioWord(c, it->headOffset);
            (void)ioWord(c, it->aggression);
        }
    } else {
        for (unsigned i = 0; i < flareCount; ++i) {
            uint8 room = (uint8)ioByte(c, 0);
            vec3i pos = {0};
            vec3s ang = {0};
            ioVec3i(c, &pos);
            ioVec3s(c, &ang);
            int16 hs = wrap_i16(ioWord(c, 0));
            int16 vs = wrap_i16(ioWord(c, 0));
            uint16 age = (uint16)ioWord(c, 0);
            uint16 lit = (uint16)ioWord(c, 0);
            if (room >= gs->level.roomsCount) {
                c->failed = true;
                return;
            }
            if (c->apply && !c->failed) {
                ItemObj *fl = gs->firstFree;
                if (!fl) {
                    c->failed = true;
                    return;
                }
                gs->firstFree = fl->nextItem;
                ItemObj_resetSlot(fl);
                fl->type = TR2_ITEM_LOOSE_FLARE;
                if (gs->level.models[TR2_ITEM_LOOSE_FLARE].animIndex < tr2->animCount)
                    ItemObj_animSet(fl, gs->level.models[TR2_ITEM_LOOSE_FLARE].animIndex, true, 0);
                fl->health = -16384;
                fl->visibleMask = UINT32_MAX;
                fl->flags = ITEM_FLAG_COLLISION | ITEM_FLAG_STATUS_ACTIVE;
                fl->pos = pos;
                fl->angle = ang;
                fl->hSpeed = hs;
                fl->vSpeed = vs;
                fl->headOffset = age;
                fl->aggression = lit;
                Room_add(gs->rooms + room, fl);
                refreshRoomFloor(fl);
                ItemObj_activateBase(fl);
            }
        }
    }
}

static void serializeAll(TR2IO *c, uint8 expectedFlipped)
{
    bool hasSg = false;
    int16 vehicle = -1;
    TR2CheckState chk;
    TR2CheckState *chkPtr = (c->reading && !c->apply) ? &chk : NULL;
    ioWorldAndStats(c, expectedFlipped);
    if (c->failed) return;
    ioLara(c, &hasSg, &vehicle);
    if (c->failed) return;
    ioPlacedItems(c, chkPtr, expectedFlipped);
    if (c->failed) return;
    ioCreaturesCargoBoatsFlares(c, chkPtr, hasSg, vehicle);
}

static uint32 writeTR2Save(GameSaveCall *call)
{
    if (!gs->players[0] || !gs->players[0]->extraL)
        return 0;
    uint8 flippedFlag = gs->gSaveGame.flipped ? 1u : 0u;
    TR2IO c = {NULL, UINT32_MAX, TR2_SAVE_HDR_BYTES, false, false, false};
    serializeAll(&c, flippedFlag);
    if (c.failed)
        return 0;
    uint32 required = c.pos;
    if (call->op == GAME_SAVE_SIZE || !call->buffer || call->bytes < required)
        return required;

    c = (TR2IO){call->buffer, call->bytes, TR2_SAVE_HDR_BYTES, false, false, false};
    serializeAll(&c, flippedFlag);
    if (c.failed || c.pos != required)
        return 0;

    uint32 words[8] = {
        TR2_SAVE_MAGIC,
        TR2_SAVE_VERSION,
        required,
        tr64_crc32(0, c.data + TR2_SAVE_HDR_BYTES, required - TR2_SAVE_HDR_BYTES),
        gs->coreIdentity,
        gs->textureIdentity,
        (((uint32)(uint16)gs->gLevelID) << 16) | (uint16)gs->level.itemsCount,
        flippedFlag
    };
    for (unsigned i = 0; i < 8; ++i)
        wr32(c.data + i * 4u, words[i]);
    return required;
}

static uint32 loadTR2Save(GameSaveCall *call)
{
    uint8 *data = call->buffer;
    if (!data || call->bytes < TR2_SAVE_HDR_BYTES || !gs->players[0])
        return 0;
    if (rd32(data + 0) != TR2_SAVE_MAGIC ||
        rd32(data + 4) != TR2_SAVE_VERSION ||
        rd32(data + 8) != call->bytes)
        return 0;
    uint32 meta = rd32(data + 24);
    uint32 flippedFlag = rd32(data + 28);
    if ((meta >> 16) != (uint16)gs->gLevelID ||
        (meta & 0xffffu) != (uint16)gs->level.itemsCount ||
        flippedFlag > 1u)
        return 0;

    if (call->phase == GAME_SAVE_CHECK) {
        if (rd32(data + 12) != tr64_crc32(0, data + TR2_SAVE_HDR_BYTES,
                                          call->bytes - TR2_SAVE_HDR_BYTES) ||
            rd32(data + 16) != gs->coreIdentity ||
            rd32(data + 20) != gs->textureIdentity)
            return 0;
        TR2IO c = {data, call->bytes, TR2_SAVE_HDR_BYTES, true, false, false};
        serializeAll(&c, (uint8)flippedFlag);
        if (c.failed || c.pos != call->bytes)
            return 0;
        /* Return 1 for unflipped, 2 for flipped so resident prepare can flip first. */
        return flippedFlag ? 2u : 1u;
    }

    if (call->phase != GAME_SAVE_APPLY)
        return 0;
    memset(&gs->tr2->savePost, 0, sizeof(gs->tr2->savePost));
    TR2IO c = {data, call->bytes, TR2_SAVE_HDR_BYTES, true, true, false};
    serializeAll(&c, (uint8)flippedFlag);
    return !c.failed && c.pos == call->bytes ? 1u : 0u;
}

uint32_t game_tr2_unit_save(void *call_ptr)
{
    GameSaveCall *call = (GameSaveCall *)call_ptr;
    return call->op == GAME_SAVE_LOAD ? loadTR2Save(call) : writeTR2Save(call);
}

#endif
