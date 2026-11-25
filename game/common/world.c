/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported initial door/block reconstruction from src/fixed/object.h:1697-2088
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "object.h"
#if TR64_HAS_TR2
#include "game/tr2/objects.h"
#endif
static uint32 sectorIndex(uint32 room, int32 x, int32 z)
{
    const RoomInfo *r = tr64_level_rooms(gs->core, room);
#if TR64_HAS_TR2
    int32 sx = i32_sub(x,tr64_i32(r->x)) >> 10, sz = i32_sub(z,tr64_i32(r->z)) >> 10;
#else
    int32 sx = (x - tr64_i32(r->x)) >> 10, sz = (z - tr64_i32(r->z)) >> 10;
#endif
    if (sx < 0 || sx >= tr64_u16(r->x_sectors) || sz < 0 || sz >= tr64_u16(r->z_sectors))
        sys_error("game derived sector outside room");
#if TR64_HAS_TR2
    if (TR64_RULE(WORLD_ITEMS) && (uint32)sx * tr64_u16(r->z_sectors) + sz > UINT16_MAX)
        sys_error("Tomb Raider II: derived sector exceeds door index width");
#endif
    return sx * tr64_u16(r->z_sectors) + sz;
}
static Sector originalSector(uint32 room, uint32 index)
{
    const RoomInfo *r = tr64_level_rooms(gs->core, room);
    const tr64_core_sectors_t *s = tr64_level_sectors(gs->core, tr64_u32(r->sector_start) + index);
    Sector out = {tr64_u16(s->floor_index),
                  tr64_u16(s->box_index),
                  s->room_below,
                  s->floor,
                  s->room_above,
                  s->ceiling};
    return out;
}
/* A cell a door closes: the runtime room slot, sector index and box (object.h WorldDoorRef). */
typedef struct
{
    uint8 room;
    uint16 sector, box;
} DoorRef;
static void roomPairs(const uint8 **mate, const uint8 **alternate)
{
    uint32 i;
    if (!gs->roomPairsReady)
    {
#if TR64_HAS_TR2
        if (gs->level.roomsCount > (TR64_SECOND_GAME ? TR64_LIMIT_rooms_2 : 139))
#else
        if (gs->level.roomsCount > 139)
#endif
            sys_error("game room capacity");
        memset(gs->roomMate, 255, sizeof(gs->roomMate));
        memset(gs->roomAlternate, 0, sizeof(gs->roomAlternate));
        for (i = 0; i < gs->level.roomsCount; ++i)
        {
            int32 other = tr64_i16(tr64_level_rooms(gs->core, i)->alternate_room);
            if (other < 0)
                continue;
            gs->roomMate[i] = other;
            gs->roomMate[other] = i;
            gs->roomAlternate[other] = 1;
        }
        gs->roomPairsReady = true;
    }
    *mate = gs->roomMate;
    *alternate = gs->roomAlternate;
}
#if TR64_HAS_TR2
/* tr2-lara-ground-and-weapons.md, Ordinary doors and timed closing:
 * near and far faces each own their own alternate independently when present,
 * and each face follows its own horizontal portal (if any) for box lookup. */
static uint32 tr2DoorFace(uint32 room, int32 x, int32 z, const uint8 *mate,
                          DoorRef *refs, uint32 *count)
{
    uint32 sector = sectorIndex(room, x, z);
    Sector original = originalSector(room, sector);
    Room *portal = Sector_getNextRoom(&original);
    uint32 linked = portal ? (uint32)(portal - gs->rooms) : 255u;
    refs[*count].room = gs->gSaveGame.flipped && mate[room] != 255 ? mate[room] : room;
    refs[*count].sector = sector;
    refs[*count].box = linked != 255u
        ? originalSector(linked, sectorIndex(linked, x, z)).boxIndex
        : original.boxIndex;
    (*count)++;
    return linked;
}

static uint32 tr2DoorRefs(const ItemObj *door, const uint8 *mate, const uint8 *alternate,
                          DoorRef *refs)
{
    int32 dx = 0, dz = 0;
    if (door->angle.y == 0)
        dz = -1024;
    else if (door->angle.y == ANGLE_180)
        dz = 1024;
    else if (door->angle.y == 16384)
        dx = -1024;
    else if (door->angle.y == -16384)
        dx = 1024;
    else
        sys_error("game derived door rotation");
    uint32 room = (uint32)(door->room - gs->rooms), count = 0;
    if (alternate[room])
        room = mate[room];
    int32 nx = i32_add(door->pos.x, dx), nz = i32_add(door->pos.z, dz);
    uint32 behind = tr2DoorFace(room, nx, nz, mate, refs, &count);
    if (mate[room] != 255)
        (void)tr2DoorFace(mate[room], nx, nz, mate, refs, &count);
    if (behind != 255u) {
        if (alternate[behind])
            behind = mate[behind];
        (void)tr2DoorFace(behind, door->pos.x, door->pos.z, mate, refs, &count);
        if (mate[behind] != 255)
            (void)tr2DoorFace(mate[behind], door->pos.x, door->pos.z, mate, refs, &count);
    }
    return count;
}
#endif
/* object.h worldAddDoorVariant: the cell in front of the door and the cell the portal behind it
 * leads to, in the runtime slots of the current flip state. */
static void doorVariant(const ItemObj *door, uint32 room, bool useAlternate, const uint8 *mate,
                        const uint8 *alternate, DoorRef *refs, uint32 *count)
{
    int32 dx = 0, dz = 0;
    uint32 front, behind;
    Sector original;
    Room *portal;
    if (door->angle.y == 0)
        dz = -1024;
    else if (door->angle.y == ANGLE_180)
        dz = 1024;
    else if (door->angle.y == 16384)
        dx = -1024;
    else if (door->angle.y == -16384)
        dx = 1024;
    else
        sys_error("game derived door rotation");
#if TR64_HAS_TR2
    int32 nx = i32_add(door->pos.x, dx), nz = i32_add(door->pos.z, dz);
    front = sectorIndex(room, nx, nz);
#else
    front = sectorIndex(room, door->pos.x + dx, door->pos.z + dz);
#endif
    original = originalSector(room, front);
    refs[*count].room = gs->gSaveGame.flipped && mate[room] != 255 ? mate[room] : room;
    refs[*count].sector = front;
    refs[(*count)++].box = original.boxIndex;
    portal = Sector_getNextRoom(&original);
    if (!portal)
        return;
    behind = (uint32)(portal - gs->rooms);
    if (useAlternate)
    {
        if (mate[behind] != 255)
            behind = mate[behind];
    }
    else if (alternate[behind])
        behind = mate[behind];
#if TR64_HAS_TR2
    /* Door topology and box topology are different lookups:
     * the portal-room box is under the near query, not the far leaf. */
    if (TR64_RULE(WORLD_ITEMS))
        refs[*count - 1].box =
            originalSector(behind, sectorIndex(behind, nx, nz)).boxIndex;
#endif
    front = sectorIndex(behind, door->pos.x, door->pos.z);
    original = originalSector(behind, front);
    refs[*count].room = gs->gSaveGame.flipped && mate[behind] != 255 ? mate[behind] : behind;
    refs[*count].sector = front;
    refs[(*count)++].box = original.boxIndex;
}
/* object.h worldBuildDoorRefs: the room the door's item sits in, or its original mate if that is
 * the alternate, then the variant in the other room of the pair. */
static uint32 doorRefs(const ItemObj *door, const uint8 *mate, const uint8 *alternate, DoorRef *refs)
{
#if TR64_HAS_TR2
    if (TR64_RULE(WORLD_ITEMS))
        return tr2DoorRefs(door, mate, alternate, refs);
#endif
    uint32 room = (uint32)(door->room - gs->rooms), count = 0;
    if (alternate[room])
        room = mate[room];
    doorVariant(door, room, false, mate, alternate, refs, &count);
    if (mate[room] != 255)
        doorVariant(door, mate[room], true, mate, alternate, refs, &count);
    return count;
}
/* object.h worldDoorSectorIsBlocked. */
static bool sectorIsBlocked(const Sector *s)
{
    return s->floorIndex == 0 && s->boxIndex == 65535 && s->roomBelow == 255 && s->floor == -127 &&
           s->roomAbove == 255 && s->ceiling == -127;
}
/* object.h:1854-1888 worldDoorDerivedStateMatches. */
static bool refsMatch(const DoorRef *refs, uint32 count, bool close)
{
    uint32 i;
    for (i = 0; i < count; ++i)
    {
        if (sectorIsBlocked(gs->rooms[refs[i].room].sectors + refs[i].sector) != close)
            return false;
        if (close && refs[i].box != 65535 &&
            (gs->boxFlags[refs[i].box] & BOX_OVERLAP_BLOCKABLE) &&
            !(gs->boxFlags[refs[i].box] & BOX_OVERLAP_BLOCKED))
            return false;
    }
    return true;
}
/* object.h:2102-2145 doorApplyCollision: closing blocks the cell; opening restores the cell from
 * the original data of the *runtime slot* (roomsInfo[ref.roomIndex]), as the source does. */
static void applyRefs(const DoorRef *refs, uint32 count, bool close)
{
    uint32 i;
    for (i = 0; i < count; ++i)
    {
        Sector *s = gs->rooms[refs[i].room].sectors + refs[i].sector;
        uint16 box = refs[i].box;
        if (close)
            *s = (Sector){0, 65535, 255, -127, 255, -127};
        else
#if TR64_HAS_TR2
            *s = originalSector(TR64_RULE(WORLD_ITEMS) ? gs->rooms[refs[i].room].source : refs[i].room,
                                refs[i].sector);
#else
            *s = originalSector(refs[i].room, refs[i].sector);
#endif
        if (box != 65535 && (gs->boxFlags[box] & BOX_OVERLAP_BLOCKABLE))
        {
            if (close)
                gs->boxFlags[box] |= BOX_OVERLAP_BLOCKED;
            else if (!(tr64_u16(tr64_level_boxes(gs->core, box)->overlap) & BOX_OVERLAP_BLOCKED))
                gs->boxFlags[box] &= ~BOX_OVERLAP_BLOCKED;
        }
    }
}
/* Oracle hunk 25 movingBlockAdjust (docs/specs/wall/objects-missing.md 2.1, D = 2048 = 8 clicks): raise reads the ceiling of
 * the sector found at y - D - 1024, restore at y + D - 1024; a wall takes C -/+ D, else the floor value moves by D and
 * the sector becomes a wall on reaching C. */
void game_moving_block_sector(Room *room,const vec3i *pos,Sector *sector,bool raise)
{
    Room *other=Room_getRoom(room,pos->x,raise ? pos->y-2048-1024 : pos->y+2048-1024,pos->z);
    int32 c=Room_getSector(other,pos->x,pos->z)->ceiling,step=raise ? -8 : 8;
    if (sector->floor == NO_FLOOR) sector->floor=(int8)(c+step);
    else {
        sector->floor=(int8)(sector->floor+step);
        if (sector->floor == c) sector->floor=NO_FLOOR;
    }
}
#if TR64_HAS_TR2
/* The occupied TR2 sector also blocks its authored navigation box. */
void game_tr2_block_occupancy(const ItemObj *item, bool occupy)
{
    Sector *cell = item->room->sectors + sectorIndex(item->room->source,item->pos.x,item->pos.z);
    int32 floor = cell->floor + (occupy ? -4 : 4);
    if (cell->floor == NO_FLOOR || floor <= NO_FLOOR || floor > INT8_MAX)
        game_tr2_refuse_interaction(item,"unrepresentable block occupancy");
    cell->floor = floor;
    if (cell->boxIndex != UINT16_MAX && (gs->boxFlags[cell->boxIndex] & BOX_OVERLAP_BLOCKABLE)) {
        if (occupy)
            gs->boxFlags[cell->boxIndex] |= BOX_OVERLAP_BLOCKED;
        else if (!(tr64_u16(gs->level.boxes[cell->boxIndex].overlap) & BOX_OVERLAP_BLOCKED))
            gs->boxFlags[cell->boxIndex] &= ~BOX_OVERLAP_BLOCKED;
    }
}

/* Great Wall objects, Push/pull block, and Lara ground and weapons,
 * Ordinary doors. Derive geometry from each physical placement, not
 * from the oracle checksum, and repeat after topology reconstruction. */
static void initialTR2World(const uint8 *mate, const uint8 *alternate)
{
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        const ItemObj *item = gs->items + i;
        if (!tr2BlockType(item->type))
            continue;
        if (TR64_RULE(INTERACTIONS)) {
            if (item->room && item->state == 1 &&
                (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE)
                game_tr2_block_occupancy(item,true);
            continue;
        }
        Sector *sector = item->room->sectors + sectorIndex(item->room->source, item->pos.x, item->pos.z);
        if (sector->floor < NO_FLOOR + 4)
            sys_error("Tomb Raider II: resting block has no raisable sector");
        sector->floor -= 4;
        if (sector->boxIndex != UINT16_MAX && (gs->boxFlags[sector->boxIndex] & BOX_OVERLAP_BLOCKABLE))
            gs->boxFlags[sector->boxIndex] |= BOX_OVERLAP_BLOCKED;
    }
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        const ItemObj *item = gs->items + i;
        if (tr2DoorType(item->type)) {
            /* Check all authored cell lookups even if this door starts
             * open: a later tick may close it. This also checks alternates. */
            DoorRef refs[4];
            uint32 count = doorRefs(item, mate, alternate, refs);
            if (item->state == 0)
                applyRefs(refs, count, true);
        }
        /* tr2-venice-objects.md, Intact glass & tr2-venice-level.md,
         * startup box discriminator (pane 46 -> box 182): intact type-72
         * panes block their own room tile's box when marked blockable. */
        else if (item->type == TR2_ITEM_WINDOW && item->room &&
                 (item->flags & ITEM_FLAG_COLLISION)) {
            const Sector *sector =
                item->room->sectors + sectorIndex(item->room->source, item->pos.x, item->pos.z);
            if (sector->boxIndex != UINT16_MAX &&
                (gs->boxFlags[sector->boxIndex] & BOX_OVERLAP_BLOCKABLE))
                gs->boxFlags[sector->boxIndex] |= BOX_OVERLAP_BLOCKED;
        }
    }
}
#endif
void game_initial_world(void)
{
    const uint8 *mate, *alternate;
    DoorRef refs[4];
    uint32 i;
    roomPairs(&mate, &alternate);
#if TR64_HAS_TR2
    if (TR64_RULE(WORLD_ITEMS)) {
        initialTR2World(mate, alternate);
        return;
    }
#endif
    for (i = 0; i < gs->level.itemsCount; ++i)
    {
        ItemObj *item = gs->items + i;
        if (item->type >= ITEM_BLOCK_1 && item->type <= ITEM_BLOCK_4 &&
            (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE &&
            (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE)
        {
            uint32 room = (uint32)(item->room - gs->rooms);
            gs->rooms[room].sectors[sectorIndex(room, item->pos.x, item->pos.z)].floor -= 4;
        }
        else if (item->type == ITEM_MOVING_BLOCK && (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE &&
                 item->state != 2)
        {
            uint32 room = (uint32)(item->room - gs->rooms);
            game_moving_block_sector(gs->rooms + room, &item->pos,
                                     gs->rooms[room].sectors + sectorIndex(room, item->pos.x, item->pos.z), true);
        }
    }
    for (i = 0; i < gs->level.itemsCount; ++i)
    {
        ItemObj *item = gs->items + i;
        if (item->type >= ITEM_DOOR_1 && item->type <= ITEM_DOOR_8 && item->state != 1)
            applyRefs(refs, doorRefs(item, mate, alternate, refs), true);
    }
}
/* object.h:1680-1684 with the oracle's hunk 14 (test/oracle/host.patch): the last check of
 * worldBuildRoomPairs refuses an item that has a room, is in no room's item list and is not
 * INVISIBLE. A collected pickup is legitimately unlinked (Pickup::collide, object.h:1024, and
 * ScionQualopecPickup::collide unlink it and make it INVISIBLE; a pickup trigger under it,
 * Pickup::use object.h:989-996, then makes it INACTIVE), so an INACTIVE pickup is exempt. */
bool game_pickup_collected(uint32 type, uint16 flags)
{
    return (flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE &&
           (Object_isPickup((int32)type) || type == ITEM_SCION_PICKUP_QUALOPEC);
}
static bool unlinkedPickupIsCollected(const ItemObj *item)
{
#if TR64_HAS_TR2
    if (TR64_RULE(INTERACTIONS))
        return (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE &&
               game_tr2_pickup_type(item->type);
#endif
    return game_pickup_collected(item->type, item->flags);
}
#ifdef TR64_GAME_VERIFY_LINK_BITS
/* Test build only (make host HOST_CC="cc -DTR64_GAME_VERIFY_LINK_BITS"): the walk of every room
 * list that the bits replace, as the source does it (object.h:1666-1684). */
static bool itemsLinkedByWalk(void)
{
    uint32 i;
    for (i = 0; i < MAX_ITEMS; ++i)
    {
        const ItemObj *item = gs->items + i, *at;
        if (!item->room || (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE ||
            unlinkedPickupIsCollected(item))
            continue;
        for (at = item->room->firstItem; at && at != item; at = at->nextItem)
            ;
        if (!at)
            return false;
    }
    return true;
}
#endif
/* The same predicate, from the unlink bits Room_add and Room_remove keep, not a walk of every
 * room list: only an item taken out of its list can fail it. */
bool game_world_items_linked(void)
{
    uint32 i, word;
    bool linked = true;
    for (word = 0; word < MAX_ITEMS / 32; ++word)
    {
        if (!gs->unlinkedItems[word])
            continue;
        for (i = word * 32; i < word * 32 + 32; ++i)
        {
            const ItemObj *item = gs->items + i;
            if ((gs->unlinkedItems[word] & (1u << (i & 31))) && item->room &&
                (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE &&
                !unlinkedPickupIsCollected(item))
                linked = false;
        }
    }
#ifdef TR64_GAME_VERIFY_LINK_BITS
    if (linked != itemsLinkedByWalk())
        sys_error("game unlink bits disagree with the room lists");
#endif
    return linked;
}
void game_door_action(ItemObj *door, bool close)
{
    const uint8 *mate, *alternate;
    DoorRef refs[4];
    uint32 count;
    /* object.h:757-764: Door::action asserts that doorApplyCollision applied; the oracle's
     * ASSERT aborts, so does this. */
    if (!game_world_items_linked())
        sys_error("game door update refused: an item is in no room list");
    if (gs->gSaveGame.flipped > 1)
        sys_error("game door world bounds");
    roomPairs(&mate, &alternate);
    count = doorRefs(door, mate, alternate, refs);
    if (refsMatch(refs, count, close))
        return;
    applyRefs(refs, count, close);
}
void game_world_flip(bool flipped)
{
    uint32 i, j;
    if (flipped == (gs->gSaveGame.flipped != 0))
        return;
    for (i = 0; i < gs->level.roomsCount; ++i)
    {
        int32 other = tr64_i16(tr64_level_rooms(gs->core, i)->alternate_room);
        if (other >= 0)
        {
            Room *a = gs->rooms + i, *b = gs->rooms + other;
            const RoomInfo *info = a->info;
            Sector *sectors = a->sectors;
            uint32 source = a->source;
            a->info = b->info;
            a->sectors = b->sectors;
            a->source = b->source;
            b->info = info;
            b->sectors = sectors;
            b->source = source;
        }
    }
    for (i = 0; i < gs->level.roomsCount; ++i)
    {
        Room *r = gs->rooms + i;
        uint32 count = tr64_u16(r->info->x_sectors) * tr64_u16(r->info->z_sectors);
        for (j = 0; j < count; ++j)
            r->sectors[j] = originalSector(r->source, j);
    }
    for (i = 0; i < gs->level.boxesCount; ++i)
        gs->boxFlags[i] = tr64_u16(gs->level.boxes[i].overlap);
    gs->gSaveGame.flipped = flipped;
    game_initial_world();
}
