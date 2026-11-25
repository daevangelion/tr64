/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/room.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/room.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

static void setUnlinked(const ItemObj *item, bool unlinked)
{
    uint32 index = (uint32)(item - gs->items);
    if (index >= MAX_ITEMS)
        sys_error("game room list item outside the item pool");
    if (unlinked)
        gs->unlinkedItems[index >> 5] |= 1u << (index & 31);
    else
        gs->unlinkedItems[index >> 5] &= ~(1u << (index & 31));
}

void Room_add(Room *self, ItemObj *item)
{
    ASSERT(item && (item)->nextItem == NULL);
    ;
    item->room = self;
    item->nextItem = self->firstItem;
    self->firstItem = item;
    setUnlinked(item, false);
}

Room **Room_addNearRoom(Room *self, Room **list, int32 x, int32 y, int32 z)
{
    Room *nearRoom = Room_getRoom(self, x, y, z);
    int32 count = list - gs->roomsList;
    for (int32 i = 0; i < count; i++)
    {
        if (gs->roomsList[i] == nearRoom)
            return list;
    }
    if (list - gs->roomsList < 31)
        *list++ = nearRoom;
    return list;
}

Room **Room_getNearRooms(Room *self, const vec3i *pos, int32 radius, int32 height)
{
    Room **list = gs->roomsList;
    *list++ = self;
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), pos->y, i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), pos->y, i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), pos->y, i32_add(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), pos->y, i32_add(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), i32_sub(pos->y, height),
                            i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), i32_sub(pos->y, height),
                            i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), i32_sub(pos->y, height),
                            i32_add(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), i32_sub(pos->y, height),
                            i32_add(pos->z, radius));
    if (list - gs->roomsList < 32)
        *list++ = NULL;
    else
        gs->roomsList[wrap_i32((uint32)(32) - (uint32)(1))] = NULL;
    return gs->roomsList;
}

#if TR64_HAS_TR2
/* tr2-lara-slide-height-and-gaze.md, Sector heights and room boundaries:
 * vertical links select the same world tile directly, without portal clamping. */
static const Sector *verticalSector(const Room *r,int32 x,int32 z)
{
    int32 sx = i32_sub(x,tr64_i32(r->info->x)) >> 10;
    int32 sz = i32_sub(z,tr64_i32(r->info->z)) >> 10;
    int32 nz = tr64_u16(r->info->z_sectors);
    if (sx < 0 || sx >= tr64_u16(r->info->x_sectors) || sz < 0 || sz >= nz)
        sys_error("Tomb Raider II: vertical sector outside linked room");
    return r->sectors + sx * nz + sz;
}

const Sector *game_tr2_room_sector(Room **room,int32 x,int32 y,int32 z)
{
    const Sector *sector = Room_getSector(*room,x,z);
    Room *next;
    while ((next=Sector_getNextRoom(sector))!=NULL) {
        *room=next;
        sector=Room_getSector(next,x,z);
    }
    if (y>=sector->floor*256) {
        while (sector->roomBelow!=NO_ROOM && y>=sector->floor*256) {
            *room=gs->rooms+sector->roomBelow;
            sector=verticalSector(*room,x,z);
        }
    } else {
        while (sector->roomAbove!=NO_ROOM && y<sector->ceiling*256) {
            *room=gs->rooms+sector->roomAbove;
            sector=verticalSector(*room,x,z);
        }
    }
    return sector;
}

/* tr2-water-and-flares, Water status before movement. The click comes
 * from the last sector on the starting side, before the medium changes. */
int32 game_tr2_water_height(Room *room, int32 x, int32 z)
{
    const Sector *sector = Room_getSector(room,x,z);
    Room *next;
    unsigned links = gs->level.roomsCount;
    while ((next = Sector_getNextRoom(sector)) != NULL) {
        if (!links--)
            sys_error("Tomb Raider II: cyclic water-height room links");
        room = next;
        sector = Room_getSector(room,x,z);
    }
    bool wet = (tr64_u16(room->info->flags) & ROOM_FLAG_WATER_MASK) != 0;
    for (;;) {
        unsigned link = wet ? sector->roomAbove : sector->roomBelow;
        if (link == NO_ROOM)
            return WALL;
        if (!links--)
            sys_error("Tomb Raider II: cyclic water-height room links");
        room = gs->rooms + link;
        if (((tr64_u16(room->info->flags) & ROOM_FLAG_WATER_MASK) != 0) != wet)
            return (wet ? sector->ceiling : sector->floor) * 256;
        sector = verticalSector(room,x,z);
    }
}
#endif

Room *Room_getRoom(Room *self, int32 x, int32 y, int32 z)
{
#if TR64_HAS_TR2
    if (TR64_RULE(ROOM_LINKS)) {
        game_tr2_room_sector(&self,x,y,z);
        return self;
    }
#endif
    const Sector *sector = Room_getSector(self, x, z);
    Room *room = self;
    while (1)
    {
        Room *nextRoom = Sector_getNextRoom(sector);
        if (!nextRoom)
            break;
        room = nextRoom;
        sector = Room_getSector(room, x, z);
    };
    while (sector->roomAbove != 0xFF && y < ((sector->ceiling * 256)))
    {
        room = gs->rooms + sector->roomAbove;
        sector = Room_getSector(room, x, z);
    }
    while (sector->roomBelow != 0xFF && y >= ((sector->floor * 256)))
    {
        room = gs->rooms + sector->roomBelow;
        sector = Room_getSector(room, x, z);
    }
    return room;
}

const Sector *Room_getSector(const Room *self, int32 x, int32 z)
{
#if TR64_HAS_TR2
    if (TR64_RULE(ROOM_LINKS)) {
        /* OpenLara src/format.h:1908–1916: the Z border excludes the
         * corner columns; the ordinary interior rows keep the X border. */
        int32 nx = tr64_u16(self->info->x_sectors), nz = tr64_u16(self->info->z_sectors);
        int32 x0 = 0, x1 = nx - 1;
        int32 sx = i32_sub(x,tr64_i32(self->info->x)) >> 10;
        int32 sz = i32_sub(z,tr64_i32(self->info->z)) >> 10;
        if (sz <= 0 || sz >= nz - 1) {
            sz = sz <= 0 ? 0 : nz - 1;
            if (nx >= 3) {
                x0 = 1;
                x1 = nx - 2;
            }
        }
        sx = sx < x0 ? x0 : sx > x1 ? x1 : sx;
        return self->sectors + sx * nz + sz;
    }
#else
#line 91 /* Preserve the cart's assertion IDs below. */
#endif
    int32 sx = (((i32_sub(x, tr64_i32(self->info->x))) >> 10) < (0)
                    ? (0)
                    : (((i32_sub(x, tr64_i32(self->info->x))) >> 10) >
                               ((tr64_u16(self->info->x_sectors) - 1))
                           ? ((tr64_u16(self->info->x_sectors) - 1))
                           : ((i32_sub(x, tr64_i32(self->info->x))) >> 10)));
    int32 sz = (((i32_sub(z, tr64_i32(self->info->z))) >> 10) < (0)
                    ? (0)
                    : (((i32_sub(z, tr64_i32(self->info->z))) >> 10) >
                               ((tr64_u16(self->info->z_sectors) - 1))
                           ? ((tr64_u16(self->info->z_sectors) - 1))
                           : ((i32_sub(z, tr64_i32(self->info->z))) >> 10)));
    return self->sectors + i32_mul(sx, tr64_u16(self->info->z_sectors)) + sz;
}

const Sector *Room_getWaterSector(const Room *self, int32 x, int32 z)
{
    const Room *room = self;
    const Sector *sector = Room_getSector(room, x, z);
    if (((tr64_u16(room->info->flags)) & ROOM_FLAG_WATER_MASK))
    {
        while (sector->roomAbove != 0xFF)
        {
            room = gs->rooms + sector->roomAbove;
            if (!((tr64_u16(room->info->flags)) & ROOM_FLAG_WATER_MASK))
            {
                return sector;
            }
            sector = Room_getSector(room, x, z);
        }
        return sector;
    }
    while (sector->roomBelow != 0xFF)
    {
        room = gs->rooms + sector->roomBelow;
        sector = Room_getSector(room, x, z);
        if (((tr64_u16(room->info->flags)) & ROOM_FLAG_WATER_MASK))
        {
            return sector;
        }
    }
    return NULL;
}

void Room_remove(Room *self, ItemObj *item)
{
    ASSERT(item && item->room == self);
    ;
    ItemObj *prev = NULL;
    ItemObj *curr = self->firstItem;
    while (curr)
    {
        ItemObj *next = curr->nextItem;
        if (curr == item)
        {
            (item)->nextItem = NULL;
            if (prev)
            {
                prev->nextItem = next;
            }
            else
            {
                self->firstItem = next;
            }
            break;
        }
        prev = curr;
        curr = next;
    }
    setUnlinked(item, true);
}

#if TR64_HAS_TR2
static int32 sectorCeiling(const Sector *self,int32 x,int32 y,int32 z,bool flatten)
#else
int32 Sector_getCeiling(const Sector *self, int32 x, int32 y, int32 z)
#endif
{
    const Sector *upperSector = Sector_getSectorAbove(self, x, z);
    int32 ceiling = (upperSector->ceiling * 256);
    if (upperSector->floorIndex)
    {
        const FloorData *fd = gs->level.floors + upperSector->floorIndex;
        uint16 cmd = tr64_u16(*fd++);
        if (((cmd) & 0x1F) == FLOOR_TYPE_FLOOR)
        {
#if TR64_HAS_TR2
            if (TR64_SECOND_GAME && FD_END(cmd))
                cmd=0; /* The following sector program is not a ceiling command. */
            else
#endif
            {
            fd++;
            cmd = tr64_u16(*fd++);
            }
        }
        if (((cmd) & 0x1F) == FLOOR_TYPE_CEILING)
        {
            int32 sx = (FD_SLANT_X((tr64_u16(*fd))));
            int32 sz = (FD_SLANT_Z((tr64_u16(*fd))));
            int32 dx = x & 1023;
            int32 dz = z & 1023;
#if TR64_HAS_TR2
            if (TR64_RULE(SURFACE_ROUND)) {
                /* tr2-lara-slide-height-and-gaze, Sector heights; tr2-camera,
                 * Steep versus full slope height, for the camera query. */
                if (!flatten || (abs(sx)<3 && abs(sz)<3)) {
                    ceiling += sx < 0 ? (sx * (1023 - dx)) >> 2 : -((sx * dx) >> 2);
                    ceiling += sz < 0 ? (sz * dz) >> 2 : -((sz * (1023 - dz)) >> 2);
                }
            } else
#endif
            {
                (ceiling = i32_sub(ceiling, i32_mul(sx, sx < 0 ? ((dx - 1023)) : dx) >> 2));
                (ceiling = i32_add(ceiling, i32_mul(sz, sz < 0 ? dz : ((dz - 1023))) >> 2));
            }
        }
    }
    const Sector *lowerSector = Sector_getSectorBelow(self, x, z);
    if (lowerSector->floorIndex)
    {
        Sector_getTriggerFloorCeiling(lowerSector, x, y, z, NULL, &ceiling);
    }
    return ceiling;
}

#if TR64_HAS_TR2
static int32 sectorFloor(const Sector *self,int32 x,int32 y,int32 z,bool flatten)
#else
int32 Sector_getFloor(const Sector *self, int32 x, int32 y, int32 z)
#endif
{
    gs->gLastFloorData = NULL;
    const Sector *lowerSector = Sector_getSectorBelow(self, x, z);
    int32 floor = (lowerSector->floor * 256);
    gs->gLastFloorSlant = 0;
    if (lowerSector->floorIndex)
    {
        const FloorData *fd = gs->level.floors + lowerSector->floorIndex;
        uint16 cmd = tr64_u16(*fd++);
        if (((cmd) & 0x1F) == FLOOR_TYPE_FLOOR)
        {
            gs->gLastFloorSlant = tr64_u16(*fd);
            int32 sx = (FD_SLANT_X((gs->gLastFloorSlant)));
            int32 sz = (FD_SLANT_Z((gs->gLastFloorSlant)));
            int32 dx = x & 1023;
            int32 dz = z & 1023;
#if TR64_HAS_TR2
            if (TR64_RULE(SURFACE_ROUND)) {
                if (!flatten || (abs(sx)<3 && abs(sz)<3)) {
                    floor += sx < 0 ? -((sx * dx) >> 2) : (sx * (1023 - dx)) >> 2;
                    floor += sz < 0 ? -((sz * dz) >> 2) : (sz * (1023 - dz)) >> 2;
                }
            } else
#endif
            {
                (floor = i32_sub(floor, i32_mul(sx, sx < 0 ? dx : ((dx - 1023))) >> 2));
                (floor = i32_sub(floor, i32_mul(sz, sz < 0 ? dz : ((dz - 1023))) >> 2));
            }
        }
        Sector_getTriggerFloorCeiling(lowerSector, x, y, z, &floor, NULL);
    }
    return floor;
}

#if TR64_HAS_TR2
int32 Sector_getFloor(const Sector *sector,int32 x,int32 y,int32 z)
{
    return sectorFloor(sector,x,y,z,false);
}

int32 Sector_getCeiling(const Sector *sector,int32 x,int32 y,int32 z)
{
    return sectorCeiling(sector,x,y,z,false);
}

void game_tr2_camera_heights(const Sector *sector,const vec3i *pos,bool full,int32 *floor,int32 *ceiling)
{
    bool flatten = TR64_RULE(CAMERA_VIEW) && !full;
    *floor = sectorFloor(sector,pos->x,pos->y,pos->z,flatten);
    *ceiling = sectorCeiling(sector,pos->x,pos->y,pos->z,flatten);
}
#endif

Room *Sector_getNextRoom(const Sector *self)
{
    if (!self->floorIndex)
        return NULL;
    const FloorData *fd = gs->level.floors + self->floorIndex;
    uint16 cmd = tr64_u16(*fd++);
    if (((cmd) & 0x1F) == FLOOR_TYPE_FLOOR)
    {
        if ((FD_END((cmd))))
            return NULL;
        fd++;
        cmd = tr64_u16(*fd++);
    }
    if (((cmd) & 0x1F) == FLOOR_TYPE_CEILING)
    {
        if ((FD_END((cmd))))
            return NULL;
        fd++;
        cmd = tr64_u16(*fd++);
    }
    if (((cmd) & 0x1F) != FLOOR_TYPE_PORTAL)
        return NULL;
#if !TR64_HAS_TR2
#line 241 /* Cart assertion ID from the TR1 path. */
#endif
    ASSERT(tr64_u16(*fd) != 0xFF);
    ;
    return gs->rooms + tr64_u16(*fd);
}

const Sector *Sector_getSectorAbove(const Sector *self, int32 posX, int32 posZ)
{
    if (self->roomAbove == 0xFF)
        return self;
#if TR64_HAS_TR2
    if (TR64_RULE(ROOM_LINKS))
        return Sector_getSectorAbove(verticalSector(gs->rooms+self->roomAbove,posX,posZ),posX,posZ);
#endif
    return Sector_getSectorAbove(Room_getSector(&(gs->rooms[self->roomAbove]), posX, posZ), posX,
                                 posZ);
}

const Sector *Sector_getSectorBelow(const Sector *self, int32 posX, int32 posZ)
{
    if (self->roomBelow == 0xFF)
        return self;
#if TR64_HAS_TR2
    if (TR64_RULE(ROOM_LINKS))
        return Sector_getSectorBelow(verticalSector(gs->rooms+self->roomBelow,posX,posZ),posX,posZ);
#endif
    return Sector_getSectorBelow(Room_getSector(&(gs->rooms[self->roomBelow]), posX, posZ), posX,
                                 posZ);
}

void Sector_getTriggerFloorCeiling(const Sector *self, int32 x, int32 y, int32 z, int32 *floor,
                                   int32 *ceiling)
{
    if (!self->floorIndex)
        return;
    uint16 cmd;
    const FloorData *fd = gs->level.floors + self->floorIndex;
    do
    {
        cmd = tr64_u16(*fd++);
        switch (((cmd) & 0x1F))
        {
        case FLOOR_TYPE_PORTAL:
        case FLOOR_TYPE_FLOOR:
        case FLOOR_TYPE_CEILING:
        {
            fd++;
            break;
        }
        case FLOOR_TYPE_TRIGGER:
        {
            if (floor && !gs->gLastFloorData)
            {
                gs->gLastFloorData = fd - 1;
            }
            fd++;
            uint16 trigger;
#if TR64_HAS_TR2
            if (TR64_SECOND_GAME &&
                (TR64_TR2_FLOOR_CONTROLLER_MASK & (1u << FD_TRIGGER_TYPE(cmd)))) {
                trigger=tr64_u16(*fd++);
                if (FD_END(trigger))
                    break;
            }
#endif
            do
            {
                trigger = tr64_u16(*fd++);
#if TR64_HAS_TR2
                if (TR64_SECOND_GAME) {
                    unsigned op=FD_ACTION(trigger)&TR64_TR2_FLOOR_ACTION_KIND_MASK;
                    if (op==TRIGGER_ACTION_ACTIVATE_OBJECT) {
                        ++gs->tr2->dynamicFloorCalls;
                        if (TR64_RULE(INTERACTIONS))
                            game_tr2_item_floor_ceiling(gs->items+FD_ARGS(trigger),x,y,z,floor,ceiling);
                    }
                    if (op==TRIGGER_ACTION_ACTIVATE_CAMERA)
                        trigger=worldActionEffectiveEnd(trigger,tr64_u16(*fd++));
                    continue;
                }
#endif
                if ((((trigger) >> 10) & 0x1F) == TRIGGER_ACTION_ACTIVATE_OBJECT)
                {
                    ItemObj_getItemFloorCeiling(&(gs->items[(FD_ARGS((trigger)))]), x, y, z, floor,
                                                ceiling);
                }
                if ((((trigger) >> 10) & 0x1F) == TRIGGER_ACTION_ACTIVATE_CAMERA ||
                    (((trigger) >> 10) & 0x1F) == TRIGGER_ACTION_FLYBY)
                    trigger = worldActionEffectiveEnd(trigger, tr64_u16(*fd++));
            } while (!(FD_END((trigger))));
            break;
        }
#if TR64_HAS_TR2
        case FLOOR_TYPE_CLIMB: /* tr2-climbing, Which face Lara can climb. */
            if (TR64_RULE(CLIMB_WALL) && floor && !gs->gLastFloorData)
                gs->gLastFloorData=fd-1;
            else if (TR64_RULE(HARNESS) && !TR64_RULE(CLIMB_WALL))
                ++gs->tr2->climbQueries;
            break;
#endif
        case FLOOR_TYPE_LAVA:
#if TR64_HAS_TR2
            /* Keep the earliest prefix entry for the sequel floor
             * evaluator: lava may follow an eligible climb mark. */
            if (floor && (!TR64_RULE(CLIMB_WALL) || !gs->gLastFloorData))
#else
            if (floor)
#endif
            {
                gs->gLastFloorData = fd - 1;
            }
            break;
        }
    } while (!(FD_END((cmd))));
}

bool trace(const Location *from, Location *to, bool accurate)
{
    int32 dx = abs(i32_sub(to->pos.x, from->pos.x));
    int32 dz = abs(i32_sub(to->pos.z, from->pos.z));
    int32 dy;
    bool res;
    if (dz > dx)
    {
        res = traceX(from, to, accurate);
        if (!traceZ(from, to, accurate))
            return 0;
    }
    else
    {
        res = traceZ(from, to, accurate);
        if (!traceX(from, to, accurate))
            return 0;
    }
    dy = i32_sub(to->pos.y, from->pos.y);
    if (dy)
    {
        const Sector *sector = Room_getSector(to->room, to->pos.x, to->pos.z);
        int32 h = Sector_getFloor(sector, to->pos.x, to->pos.y, to->pos.z);
        if (to->pos.y <= h || from->pos.y >= h)
        {
            h = Sector_getCeiling(sector, to->pos.x, to->pos.y, to->pos.z);
            if (to->pos.y >= h || from->pos.y <= h)
            {
                h = (WALL);
            }
        }
        if (h != (WALL))
        {
            to->pos.y = h;
            (h = i32_sub(h, from->pos.y));
            to->pos.x = i32_add(from->pos.x, i32_mul(i32_sub(to->pos.x, from->pos.x), h) / dy);
            to->pos.z = i32_add(from->pos.z, i32_mul(i32_sub(to->pos.z, from->pos.z), h) / dy);
            return 0;
        }
    }
    return res;
}

bool traceX(const Location *from, Location *to, bool accurate)
{
    vec3i d = vec3i_sub(to->pos, from->pos);
    if (!d.x)
        return 1;
    d.y = (i32_shl(d.y, 10)) / d.x;
    d.z = (i32_shl(d.z, 10)) / d.x;
    vec3i p = from->pos;
    Room *room = from->room;
    if (d.x < 0)
    {
        d.x = 1024;
        p.x &= ~1023;
        (p.y = i32_add(p.y, i32_mul(d.y, i32_sub(p.x, from->pos.x)) >> 10));
        (p.z = i32_add(p.z, i32_mul(d.z, i32_sub(p.x, from->pos.x)) >> 10));
        while (p.x > to->pos.x)
        {
            room = Room_getRoom(room, p.x, p.y, p.z);
            {
                const Sector *sector = Room_getSector(room, p.x, p.z);
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, p.x, p.y, p.z) ||
                        p.y < Sector_getCeiling(sector, p.x, p.y, p.z))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            Room *nextRoom = Room_getRoom(room, i32_sub(p.x, 1), p.y, p.z);
            {
                const Sector *sector = Room_getSector(nextRoom, i32_sub(p.x, 1), p.z);
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, i32_sub(p.x, 1), p.y, p.z) ||
                        p.y < Sector_getCeiling(sector, i32_sub(p.x, 1), p.y, p.z))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            room = nextRoom;
            (p = vec3i_sub(p, d));
        }
    }
    else
    {
        d.x = 1024;
        p.x |= 1023;
        (p.y = i32_add(p.y, i32_mul(d.y, i32_sub(p.x, from->pos.x)) >> 10));
        (p.z = i32_add(p.z, i32_mul(d.z, i32_sub(p.x, from->pos.x)) >> 10));
        while (p.x < to->pos.x)
        {
            room = Room_getRoom(room, p.x, p.y, p.z);
            {
                const Sector *sector = Room_getSector(room, p.x, p.z);
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, p.x, p.y, p.z) ||
                        p.y < Sector_getCeiling(sector, p.x, p.y, p.z))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            Room *nextRoom = Room_getRoom(room, i32_add(p.x, 1), p.y, p.z);
            {
                const Sector *sector = Room_getSector(nextRoom, i32_add(p.x, 1), p.z);
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, i32_add(p.x, 1), p.y, p.z) ||
                        p.y < Sector_getCeiling(sector, i32_add(p.x, 1), p.y, p.z))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            room = nextRoom;
            (p = vec3i_add(p, d));
        }
    }
    to->room = room;
    return 1;
}

bool traceZ(const Location *from, Location *to, bool accurate)
{
    vec3i d = vec3i_sub(to->pos, from->pos);
    if (!d.z)
        return 1;
    d.x = (i32_shl(d.x, 10)) / d.z;
    d.y = (i32_shl(d.y, 10)) / d.z;
    vec3i p = from->pos;
    Room *room = from->room;
    if (d.z < 0)
    {
        d.z = 1024;
        p.z &= ~1023;
        (p.x = i32_add(p.x, i32_mul(d.x, i32_sub(p.z, from->pos.z)) >> 10));
        (p.y = i32_add(p.y, i32_mul(d.y, i32_sub(p.z, from->pos.z)) >> 10));
        while (p.z > to->pos.z)
        {
            room = Room_getRoom(room, p.x, p.y, p.z);
            {
                const Sector *sector = Room_getSector(room, p.x, p.z);
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, p.x, p.y, p.z) ||
                        p.y < Sector_getCeiling(sector, p.x, p.y, p.z))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            Room *nextRoom = Room_getRoom(room, p.x, p.y, i32_sub(p.z, 1));
            {
                const Sector *sector = Room_getSector(nextRoom, p.x, i32_sub(p.z, 1));
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, p.x, p.y, i32_sub(p.z, 1)) ||
                        p.y < Sector_getCeiling(sector, p.x, p.y, i32_sub(p.z, 1)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            room = nextRoom;
            (p = vec3i_sub(p, d));
        }
    }
    else
    {
        d.z = 1024;
        p.z |= 1023;
        (p.x = i32_add(p.x, i32_mul(d.x, i32_sub(p.z, from->pos.z)) >> 10));
        (p.y = i32_add(p.y, i32_mul(d.y, i32_sub(p.z, from->pos.z)) >> 10));
        while (p.z < to->pos.z)
        {
            room = Room_getRoom(room, p.x, p.y, p.z);
            {
                const Sector *sector = Room_getSector(room, p.x, p.z);
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, p.x, p.y, p.z) ||
                        p.y < Sector_getCeiling(sector, p.x, p.y, p.z))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            Room *nextRoom = Room_getRoom(room, p.x, p.y, i32_add(p.z, 1));
            {
                const Sector *sector = Room_getSector(nextRoom, p.x, i32_add(p.z, 1));
                if (accurate)
                {
                    if (p.y > Sector_getFloor(sector, p.x, p.y, i32_add(p.z, 1)) ||
                        p.y < Sector_getCeiling(sector, p.x, p.y, i32_add(p.z, 1)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
                else
                {
                    if (p.y > ((sector->floor * 256)) || p.y < ((sector->ceiling * 256)))
                    {
                        (to->pos = p);
                        to->room = room;
                        return 0;
                    }
                }
            };
            room = nextRoom;
            (p = vec3i_add(p, d));
        }
    }
    to->room = room;
    return 1;
}
