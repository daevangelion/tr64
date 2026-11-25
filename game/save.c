/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/save_v3.h, game.h, item.h, lara.h, enemy.h, inventory.h, world_state.h and encounter_spawners.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Source V3 persistent records only. Native BE envelope/field encoding has no
 * C padding, pointers, navigation snapshot, ring state or compressor. Source
 * preparation/derived-world/list/activation stages are resident orchestration. */
#include "internal.h"
#include "save.h"
#include "tr64/res.h"

static bool overlap(const void *a, uint32 n, const void *b, uint32 m)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < n : x - y < m;
}
#if !TR64_CART_TR2
enum
{
    SAVE_MAGIC = 0x54525333u,
    SOURCE_VERSION = 3,
    EXT_FLIPS = 1,
    EXT_CHILDREN = 2,
    EXT_END = 255
};
typedef struct
{
    uint8 *data;
    uint32 bytes, pos;
    bool reading, apply, failed;
} IO;
static uint32 byte(IO *c, uint32 value)
{
    if (c->pos >= c->bytes)
    {
        c->failed = true;
        return 0;
    }
    if (c->reading)
        value = c->data[c->pos];
    else if (c->data)
        c->data[c->pos] = (uint8)value;
    ++c->pos;
    return value;
}
static uint32 word(IO *c, uint32 v)
{
    uint32 hi = byte(c, v >> 8), lo = byte(c, v);
    return hi << 8 | lo;
}
static uint32 dword(IO *c, uint32 v)
{
    uint32 hi = word(c, v >> 16), lo = word(c, v);
    return hi << 16 | lo;
}
static uint32 get32(const uint8 *p)
{
    return (uint32)p[0] << 24 | (uint32)p[1] << 16 | (uint32)p[2] << 8 | p[3];
}
static void put32(uint8 *p, uint32 n)
{
    p[0] = (uint8)(n >> 24);
    p[1] = (uint8)(n >> 16);
    p[2] = (uint8)(n >> 8);
    p[3] = (uint8)n;
}
static bool isEnemy(uint32 t)
{
    switch (t)
    {
    case ITEM_DOPPELGANGER:
    case ITEM_WOLF:
    case ITEM_BEAR:
    case ITEM_BAT:
    case ITEM_CROCODILE_LAND:
    case ITEM_CROCODILE_WATER:
    case ITEM_LION_MALE:
    case ITEM_LION_FEMALE:
    case ITEM_PUMA:
    case ITEM_GORILLA:
    case ITEM_RAT_LAND:
    case ITEM_RAT_WATER:
    case ITEM_REX:
    case ITEM_RAPTOR:
    case ITEM_MUTANT_1:
    case ITEM_MUTANT_2:
    case ITEM_MUTANT_3:
    case ITEM_CENTAUR:
    case ITEM_MUMMY:
    case ITEM_LARSON:
    case ITEM_PIERRE:
    case ITEM_SKATER:
    case ITEM_COWBOY:
    case ITEM_MR_T:
    case ITEM_NATLA:
    case ITEM_ADAM:
        return true;
    default:
        return false;
    }
}
static bool controlTail(uint32 t)
{
    return (t >= ITEM_MUTANT_1 && t <= ITEM_MUTANT_3) || t == ITEM_PIERRE || t == ITEM_SKATER ||
           t == ITEM_NATLA;
}
static uint32 itemSize(uint32 t)
{
    if (t > ITEM_MAX)
        return 0;
    if (t == ITEM_LARA)
        return GAME_SAVE_BASE_BYTES + 64;
    if (t == ITEM_TRAP_BOULDER)
        return GAME_SAVE_BASE_BYTES + 4;
    if (t == ITEM_TRAP_SWORD || t == ITEM_SCION_TARGET)
        return GAME_SAVE_BASE_BYTES + 2;
    return GAME_SAVE_BASE_BYTES + (isEnemy(t) ? 8u : 0u) + (controlTail(t) ? 2u : 0u);
}
static uint32 childType(uint32 t, uint16 flags)
{
    if (t == ITEM_CENTAUR_STATUE)
        return ITEM_CENTAUR;
    if (t == ITEM_HAMMER_HANDLE)
        return ITEM_HAMMER_BLOCK;
    if (t == ITEM_FLAME_EMITTER)
        return ITEM_FLAME;
    switch ((flags >> 9) & 31)
    {
    case 1:
        return ITEM_MUTANT_2;
    case 2:
        return ITEM_CENTAUR;
    case 4:
        return ITEM_ADAM;
    case 8:
        return ITEM_MUTANT_3;
    default:
        return ITEM_MUTANT_1;
    }
}
static uint32 puzzleType(uint32 t, uint16 flags)
{
    if ((flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE)
        return t;
    switch (t)
    {
    case ITEM_PUZZLEHOLE_1:
        return ITEM_PUZZLEHOLE_DONE_1;
    case ITEM_PUZZLEHOLE_2:
        return ITEM_PUZZLEHOLE_DONE_2;
    case ITEM_PUZZLEHOLE_3:
        return ITEM_PUZZLEHOLE_DONE_3;
    case ITEM_PUZZLEHOLE_4:
        return ITEM_PUZZLEHOLE_DONE_4;
    default:
        return t;
    }
}
static bool binarySlot(uint32 i)
{
    return i == SLOT_COMPASS || i == SLOT_PISTOLS || i == SLOT_SHOTGUN || i == SLOT_MAGNUMS ||
           i == SLOT_UZIS || i == SLOT_PASSPORT || i == SLOT_DETAIL || i == SLOT_SOUND ||
           i == SLOT_CONTROLS || i == SLOT_HOME;
}
static bool slotsValid(const uint16 *slots)
{
    uint32 i;
    for (i = 0; i < 64; i++)
    {
        uint32 n = slots[i];
        if (i >= SLOT_MAX)
        {
            if (n)
                return false;
            continue;
        }
        if (n == UINT16_MAX)
        {
            if (i != SLOT_AMMO_PISTOLS)
                return false;
        }
        else if (n > 32767u || (binarySlot(i) && n > 1))
            return false;
    }
    return true;
}
static bool buildHeader(SaveGame *out)
{
    uint32 i;
    *out = gs->gSaveGame;
    out->version = SOURCE_VERSION;
    out->dataSize = 0;
    out->itemCount = (uint16)gs->level.itemsCount;
    out->level = (uint8)gs->gLevelID;
    out->track = (int8)gs->gCurTrack;
    out->randSeedLogic = gs->gRandSeedLogic;
    out->randSeedDraw = gs->gRandSeedDraw;
    memset(out->invSlots, 0, sizeof(out->invSlots));
    for (i = 0; i < SLOT_MAX; i++)
    {
        int32 n = gs->inventory.counts[i];
        if (!((n == -1 && i == SLOT_AMMO_PISTOLS) ||
              (n >= 0 && n <= 32767 && (!binarySlot(i) || n <= 1))))
            return false;
        out->invSlots[i] = (uint16)n;
    }
    return gs->gSaveGame.flipped <= 1 &&
           gs->level.itemsCount == tr64_level_table(gs->core, TR64_CORE_items)->count;
}
#define HEADER(kind, f)                                                                            \
    do                                                                                             \
    {                                                                                              \
        uint32 v = kind(c, (uint32)s->f);                                                          \
        if (c->reading)                                                                            \
            s->f = v;                                                                              \
    } while (0)
static void header(IO *c, SaveGame *s)
{
    uint32 i;
    HEADER(dword, version);
    HEADER(dword, dataSize);
    HEADER(word, itemCount);
    HEADER(byte, level);
    s->track = (int8)(int32)((int8)byte(c, (uint8)s->track));
    HEADER(byte, secrets);
    HEADER(byte, pickups);
    HEADER(dword, time);
    HEADER(dword, distance);
    HEADER(dword, randSeedLogic);
    HEADER(dword, randSeedDraw);
    HEADER(word, mediUsed);
    HEADER(word, ammoUsed);
    HEADER(word, kills);
    HEADER(word, flipped);
    for (i = 0; i < 64; i++)
    {
        uint32 v = byte(c, s->tracks[i]);
        if (c->reading)
            s->tracks[i] = (uint8)v;
    }
    for (i = 0; i < 64; i++)
    {
        uint32 v = word(c, s->invSlots[i]);
        if (c->reading)
            s->invSlots[i] = (uint16)v;
    }
}
#undef HEADER
static void base(IO *c, GameSaveItem *r)
{
    r->x = wrap_i16(word(c, (uint16)r->x));
    r->y = wrap_i16(word(c, (uint16)r->y));
    r->z = wrap_i16(word(c, (uint16)r->z));
    r->ax = wrap_i16(word(c, (uint16)r->ax));
    r->ay = wrap_i16(word(c, (uint16)r->ay));
    r->animIndex = (uint16)word(c, r->animIndex);
    r->frameIndex = (uint16)word(c, r->frameIndex);
    r->flags = (uint16)word(c, r->flags);
    r->timer = (uint16)word(c, r->timer);
    r->state = (uint8)byte(c, r->state);
    r->nextState = (uint8)byte(c, r->nextState);
    r->goalState = (uint8)byte(c, r->goalState);
    r->roomIndex = (uint8)byte(c, r->roomIndex);
}
static bool roomIndex(const Room *r, uint32 *out)
{
    uintptr_t p = (uintptr_t)r, b = (uintptr_t)gs->rooms;
    if (!r || p < b || (p - b) % sizeof(Room) || (p - b) / sizeof(Room) >= gs->level.roomsCount)
        return false;
    *out = (uint32)((p - b) / sizeof(Room));
    return true;
}
static bool hazardValid(const LaraHazardState *s)
{
    return !s->reserved && !s->damageCooldown && !(s->flags & ~1u) &&
           s->flameItemIndex < MAX_ITEMS &&
           (s->flags ? (s->burnTicks && s->flameItemIndex >= 0)
                     : (!s->burnTicks && s->flameItemIndex == -1));
}
static bool swordValid(const GameSaveItem *r, int16 spin)
{
    int32 x = (int8)r->nextState, z = (int8)r->goalState;
    uint16 status = r->flags & ITEM_FLAG_STATUS;
    bool active = (r->flags & ITEM_FLAG_ACTIVE) != 0;
    if (r->state > 3 || x < -47 || x > 47 || z < -47 || z > 47 || spin < -1092 || spin > 1092)
        return false;
    switch (r->state)
    {
    case 0:
        return !x && !z && !r->timer && status == ITEM_FLAG_STATUS_ACTIVE && active;
    case 1:
        return r->timer == 50 && status == ITEM_FLAG_STATUS_ACTIVE && active;
    case 2:
        return r->timer >= 56 && r->timer <= 32766 &&
               (r->timer >= 128 || (r->timer - 56) % 6 == 0) && status == ITEM_FLAG_STATUS_ACTIVE &&
               active;
    case 3:
        return r->timer >= 56 && r->timer <= 32767 && status == ITEM_FLAG_STATUS_INACTIVE &&
               !active;
    }
    return false;
}
static void laraRecord(IO *c, ItemObj *item)
{
    ExtraInfoLara *e = item->extraL;
    uint32 i, room = 0;
    int16 v = 0, h = 0, health = 0, view[3] = {0, 0, 0};
    uint8 state = 0, weapon = 0, goal = 0, water = 0, last = 0;
    uint16 ar[4] = {0}, meshes[15] = {0};
    LaraHazardState hazard = {0};
    if (!c->reading)
    {
        if (!e || !roomIndex(e->camera.view.room, &room) || item->waterState > WATER_STATE_UNDER ||
            e->hazardCause > HAZARD_HIT_LAVA || !hazardValid(gs->gLaraHazardState))
        {
            c->failed = true;
            return;
        }
        v = item->vSpeed;
        h = item->hSpeed;
        health = item->health;
        state = e->weaponState;
        weapon = e->weapon;
        goal = e->goalWeapon;
        water = (uint8)(item->waterState | (e->hazardCause << 4));
        hazard = gs->gLaraHazardState[0];
        ar[0] = e->armR.animIndex;
        ar[1] = e->armR.frameIndex;
        ar[2] = e->armL.animIndex;
        ar[3] = e->armL.frameIndex;
        last = (uint8)e->camera.lastIndex;
        const RoomInfo *info = e->camera.view.room->info;
        view[0] = wrap_i16((uint32)e->camera.view.pos.x - (uint32)tr64_i32(info->x));
        view[1] = wrap_i16((uint32)e->camera.view.pos.y - (uint32)tr64_i32(info->y_top));
        view[2] = wrap_i16((uint32)e->camera.view.pos.z - (uint32)tr64_i32(info->z));
        memcpy(meshes, e->meshes, sizeof(meshes));
    }
    v = wrap_i16(word(c, (uint16)v));
    h = wrap_i16(word(c, (uint16)h));
    health = wrap_i16(word(c, (uint16)health));
    state = (uint8)byte(c, state);
    weapon = (uint8)byte(c, weapon);
    goal = (uint8)byte(c, goal);
    water = (uint8)byte(c, water);
    hazard.burnTicks = (uint16)word(c, hazard.burnTicks);
    hazard.damageCooldown = (uint16)word(c, hazard.damageCooldown);
    hazard.flameItemIndex = wrap_i16(word(c, (uint16)hazard.flameItemIndex));
    hazard.flags = (uint8)byte(c, hazard.flags);
    hazard.reserved = (uint8)byte(c, hazard.reserved);
    for (i = 0; i < 4; i++)
        ar[i] = (uint16)word(c, ar[i]);
    room = byte(c, room);
    last = (uint8)byte(c, last);
    for (i = 0; i < 3; i++)
        view[i] = wrap_i16(word(c, (uint16)view[i]));
    for (i = 0; i < 15; i++)
        meshes[i] = (uint16)word(c, meshes[i]);
    if (room >= gs->level.roomsCount || (water & 15) > WATER_STATE_UNDER ||
        (water >> 4) > HAZARD_HIT_LAVA || !hazardValid(&hazard))
        c->failed = true;
    if (c->apply && !c->failed)
    {
        item->vSpeed = v;
        item->hSpeed = h;
        item->health = health;
        e->weaponState = state;
        e->weapon = weapon;
        e->goalWeapon = goal;
        item->waterState = water & 15;
        e->hazardCause = water >> 4;
        gs->gLaraHazardState[0] = hazard;
        e->armR.animIndex = ar[0];
        e->armR.frameIndex = ar[1];
        e->armL.animIndex = ar[2];
        e->armL.frameIndex = ar[3];
        Camera_init(&e->camera, item);
        Room *r = gs->rooms + room;
        e->camera.view.room = r;
        e->camera.lastIndex = last;
        e->camera.view.pos = vec3i_create(i32_add(view[0], tr64_i32(r->info->x)),
                                          i32_add(view[1], tr64_i32(r->info->y_top)),
                                          i32_add(view[2], tr64_i32(r->info->z)));
        memcpy(e->meshes, meshes, sizeof(meshes));
    }
}
static void record(IO *c, ItemObj *item, uint32 type)
{
    GameSaveItem r = {0};
    uint32 room = 0;
    int16 v = 0, h = 0, health = 0, control = 0;
    uint8 mood = 0, water = 0;
    if (!c->reading)
    {
        if (!roomIndex(item->room, &room))
        {
            c->failed = true;
            return;
        }
        r.x = wrap_i16((uint32)item->pos.x - (uint32)tr64_i32(item->room->info->x));
        r.y = wrap_i16((uint32)item->pos.y - (uint32)tr64_i32(item->room->info->y_top));
        r.z = wrap_i16((uint32)item->pos.z - (uint32)tr64_i32(item->room->info->z));
        r.ax = item->angle.x;
        r.ay = item->angle.y;
        r.animIndex = item->animIndex;
        r.frameIndex = item->frameIndex;
        r.flags = item->flags;
        r.timer = (uint16)item->timer;
        r.state = item->state;
        r.nextState = item->nextState;
        r.goalState = item->goalState;
        r.roomIndex = (uint8)room;
        v = item->vSpeed;
        h = item->hSpeed;
        health = item->health;
        control = item->turnSpeed;
        mood = (uint8)item->input;
        water = item->waterState;
    }
    base(c, &r);
    if (r.roomIndex >= gs->level.roomsCount)
    {
        c->failed = true;
        return;
    }
    if (c->apply)
    {
        if (item->room)
            Room_remove(item->room, item);
        /* Oracle hunk 20 (item.h, ItemObj::load): a collected pickup is not linked again. */
        if (game_pickup_collected(type, r.flags))
            item->room = gs->rooms + r.roomIndex;
        else
            Room_add(gs->rooms + r.roomIndex, item);
        item->pos = vec3i_create(i32_add(r.x, tr64_i32(item->room->info->x)),
                                 i32_add(r.y, tr64_i32(item->room->info->y_top)),
                                 i32_add(r.z, tr64_i32(item->room->info->z)));
        item->angle.x = r.ax;
        item->angle.y = r.ay;
        item->animIndex = r.animIndex;
        item->frameIndex = r.frameIndex;
        item->flags = r.flags;
        item->timer = wrap_i16(r.timer);
        item->state = r.state;
        item->nextState = r.nextState;
        item->goalState = r.goalState;
    }
    if (type == ITEM_LARA)
    {
        laraRecord(c, item);
        return;
    }
    if (isEnemy(type))
    {
        v = wrap_i16(word(c, (uint16)v));
        h = wrap_i16(word(c, (uint16)h));
        health = wrap_i16(word(c, (uint16)health));
        mood = (uint8)byte(c, mood);
        water = (uint8)byte(c, water);
        if (controlTail(type))
            control = wrap_i16(word(c, (uint16)control));
        if (c->apply)
        {
            item->vSpeed = v;
            item->hSpeed = h;
            item->health = health;
            item->input = mood;
            item->waterState = water;
            if (controlTail(type))
                item->turnSpeed = control;
        }
    }
    else if (type == ITEM_TRAP_BOULDER)
    {
        v = wrap_i16(word(c, (uint16)v));
        h = wrap_i16(word(c, (uint16)h));
        if (c->apply)
        {
            item->vSpeed = v;
            item->hSpeed = h;
            ItemObj_updateRoom(item, 0);
        }
    }
    else if (type == ITEM_TRAP_SWORD)
    {
        int16 spin = wrap_i16(word(c, (uint16)item->hSpeed));
        if (c->reading && !swordValid(&r, spin))
            c->failed = true;
        if (c->apply && !c->failed)
            item->hSpeed = spin;
    }
    else if (type == ITEM_SCION_TARGET)
    {
        health = wrap_i16(word(c, (uint16)health));
        if (c->apply)
        {
            item->health = health;
            if (health <= 0 && item->room &&
                (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
                Room_remove(item->room, item);
        }
    }
}
static void extension(IO *c, uint32 tag, uint32 bytes)
{
    if (byte(c, tag) != tag || word(c, bytes) != bytes)
        c->failed = true;
}
static void payload(IO *c, GameSavePlan *p)
{
    uint32 n = gs->level.itemsCount, i, ordinal = 0, children = 0;
    for (i = 0; i < n; i++)
    {
        uint32 type = tr64_u16(tr64_level_items(gs->core, i)->type), size = itemSize(type), before;
        if (byte(c, type) != type || word(c, size) != size || !size || c->pos > UINT16_MAX)
        {
            c->failed = true;
            return;
        }
        if (c->reading && !c->apply)
            p->itemOffsets[i] = (uint16)c->pos;
        if (!c->reading && gs->items[i].type != puzzleType(type, gs->items[i].flags))
        {
            c->failed = true;
            return;
        }
        if (c->apply)
            gs->items[i].type = (uint8)type;
        before = c->pos;
        record(c, gs->items + i, type);
        if (c->pos - before != size)
            c->failed = true;
    }
    extension(c, EXT_FLIPS, 21);
    if (c->reading && !c->apply)
        p->flipOffset = (uint16)c->pos;
    if (byte(c, 10) != 10)
        c->failed = true;
    for (i = 0; i < 10; i++)
    {
        uint32 mask = byte(c, gs->flipMaps[i].mask), flags = byte(c, gs->flipMaps[i].flags);
        if (mask > 31 || (flags & ~3u))
            c->failed = true;
        if (c->apply)
        {
            gs->flipMaps[i].mask = (uint8)mask;
            gs->flipMaps[i].flags = (uint8)flags;
        }
    }
    for (i = 0; i < n; i++)
    {
        const tr64_core_items_t *info = tr64_level_items(gs->core, i);
        uint32 type = tr64_u16(info->type);
        if (encounterIsSource(type))
            children += 8 + itemSize(childType(type, tr64_u16(info->flags)));
    }
    extension(c, EXT_CHILDREN, children);
    if (c->reading && !c->apply)
    {
        p->childOffset = (uint16)c->pos;
        p->childSize = (uint16)children;
    }
    for (i = 0; i < n; i++)
    {
        const tr64_core_items_t *info = tr64_level_items(gs->core, i);
        uint32 source = tr64_u16(info->type), index = n + ordinal, type, size, before;
        if (!encounterIsSource(source))
            continue;
        if (index >= MAX_ITEMS)
        {
            c->failed = true;
            return;
        }
        /* Precreation consumes the parent's trigger mask. Child species is
         * the immutable encounter layout, as in encounterResolvePreparedChild. */
        uint16 flags = tr64_u16(info->flags);
        type = childType(source, flags);
        size = itemSize(type);
        if (word(c, i) != i || word(c, index) != index || word(c, type) != type ||
            word(c, size) != size)
        {
            c->failed = true;
            return;
        }
        if (!c->reading && (gs->items[i].headOffset != index || gs->items[index].type != type))
        {
            c->failed = true;
            return;
        }
        before = c->pos;
        record(c, gs->items + index, type);
        if (c->pos - before != size)
            c->failed = true;
        if (c->apply)
            gs->items[i].headOffset = (uint16)index;
        ordinal++;
    }
    extension(c, EXT_END, 0);
}
static uint32 writeSave(GameSaveCall *call)
{
    SaveGame candidate;
    IO c;
    uint32 required, i;
    GameSavePlan unused;
    if (!buildHeader(&candidate))
        return 0;
    c = (IO){NULL, UINT32_MAX, GAME_SAVE_HEADER_BYTES, false, false, false};
    header(&c, &candidate);
    payload(&c, &unused);
    if (c.failed)
        return 0;
    required = c.pos;
    candidate.dataSize = required - GAME_SAVE_HEADER_BYTES - GAME_SAVE_SOURCE_HEADER_BYTES;
    if (call->op == GAME_SAVE_SIZE || !call->buffer || call->bytes < required)
        return required;
    c = (IO){call->buffer, call->bytes, GAME_SAVE_HEADER_BYTES, false, false, false};
    header(&c, &candidate);
    payload(&c, &unused);
    if (c.failed || c.pos != required)
        return 0;
    const uint32 words[8] = {
        SAVE_MAGIC,
        GAME_SAVE_FORMAT_VERSION,
        required,
        tr64_crc32(0, c.data + GAME_SAVE_HEADER_BYTES, required - GAME_SAVE_HEADER_BYTES),
        gs->coreIdentity,
        gs->textureIdentity,
        GAME_SAVE_SOURCE_HEADER_BYTES,
        0};
    for (i = 0; i < 8; i++)
        put32(c.data + i * 4, words[i]);
    gs->gSaveGame = candidate;
    return required;
}
static uint32 loadSave(GameSaveCall *call)
{
    GameSavePlan *p = call->plan;
    uint8 *data = call->buffer;
    IO c;
    uint32 i;
    if (!p || !data || call->bytes < GAME_SAVE_HEADER_BYTES + GAME_SAVE_SOURCE_HEADER_BYTES)
        return 0;
    if (call->phase == GAME_SAVE_FINISH)
    {
        gs->gCurTrack = p->header.track;
        gs->gRandSeedLogic = p->header.randSeedLogic;
        gs->gRandSeedDraw = p->header.randSeedDraw;
        return 1;
    }
    if (get32(data) != SAVE_MAGIC || get32(data + 4) != GAME_SAVE_FORMAT_VERSION ||
        get32(data + 8) != call->bytes || get32(data + 24) != GAME_SAVE_SOURCE_HEADER_BYTES ||
        get32(data + 28))
        return 0;
    if (call->phase == GAME_SAVE_CHECK)
    {
        if (get32(data + 12) != tr64_crc32(0, data + 32, call->bytes - 32) ||
            get32(data + 16) != gs->coreIdentity ||
            get32(data + 20) != gs->textureIdentity)
            return 0;
        memset(p, 0, sizeof(*p));
        p->totalBytes = call->bytes;
        p->coreIdentity = get32(data + 16);
        p->textureIdentity = get32(data + 20);
        c = (IO){data, call->bytes, GAME_SAVE_HEADER_BYTES, true, false, false};
        header(&c, &p->header);
        if (c.failed || p->header.version != SOURCE_VERSION ||
            p->header.dataSize !=
                call->bytes - GAME_SAVE_HEADER_BYTES - GAME_SAVE_SOURCE_HEADER_BYTES ||
            p->header.itemCount != gs->level.itemsCount || p->header.level != (uint8)gs->gLevelID ||
            p->header.flipped > 1 || !slotsValid(p->header.invSlots))
            return 0;
        payload(&c, p);
        return !c.failed && c.pos == call->bytes;
    }
    if (call->phase != GAME_SAVE_APPLY || p->totalBytes != call->bytes)
        return 0;
    gs->gSaveGame = p->header;
    gs->inventory.numKeys = 0;
    for (i = 0; i < SLOT_MAX; i++)
    {
        gs->inventory.counts[i] = wrap_i16(p->header.invSlots[i]);
        if (i < SLOT_COMPASS)
            gs->inventory.numKeys += gs->inventory.counts[i];
    }
    c = (IO){data, call->bytes, GAME_SAVE_HEADER_BYTES + GAME_SAVE_SOURCE_HEADER_BYTES,
             true, true,        false};
    payload(&c, p);
    return !c.failed && c.pos == call->bytes;
}
#endif
uint32_t tr64_unit_save(uint32_t argument)
{
    GameSaveCall *call = (GameSaveCall *)(uintptr_t)argument;
    uint32 result = 0;
    if (!call)
        return 0;
    if (gs && gs->core && gs->core->ready && call->op <= GAME_SAVE_LOAD &&
        (!call->buffer || ((uintptr_t)call->buffer <= UINTPTR_MAX - call->bytes &&
                           !overlap(call->buffer, call->bytes, gs, gs->bytes))))
    {
#if TR64_CART_TR2
        result = game_tr2_unit_save(call);
#elif TR64_HAS_TR2
        result = gs->tr2 ? game_tr2_unit_save(call)
                         : (call->op == GAME_SAVE_LOAD ? loadSave(call) : writeSave(call));
#else
        result = call->op == GAME_SAVE_LOAD ? loadSave(call) : writeSave(call);
#endif
    }
    call->result = result;
    return result;
}
