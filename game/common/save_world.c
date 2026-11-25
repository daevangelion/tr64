/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h, object.h, world_state.h, world_persistence.h and fixed_capacity.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "save_world.h"
/* The oracle's non-GBA, original-campaign bounds. Native backing sectors are
 * allocated once for the whole Core; the source admission limit still applies
 * to the union of rooms that require derived mutable geometry. */
enum { SAVE_WORLD_MAX_DYN_SECTORS=10240, SAVE_WORLD_MAX_BOXES=1024 };
typedef struct { uint8 room; uint16 sector,box; } SaveDoorRef;
typedef struct {
    uint8 mate[MAX_ROOMS],alternate[MAX_ROOMS];
    bool touched[MAX_ROOMS];
    uint8 boxes[(SAVE_WORLD_MAX_BOXES+7)/8];
} SaveWorld;
static bool doorType(uint32 type)
{
    return type >= ITEM_DOOR_1 && type <= ITEM_DOOR_8;
}
static bool blockType(uint32 type)
{
    return type >= ITEM_BLOCK_1 && type <= ITEM_BLOCK_4;
}
static bool blockAffectsFloor(uint16 flags)
{
    uint16 status=flags&ITEM_FLAG_STATUS;
    return status != ITEM_FLAG_STATUS_INVISIBLE && status != ITEM_FLAG_STATUS_ACTIVE;
}
/* A pushable block, or the rolling block (oracle hunk 25) outside its slide, that holds its sector raised. */
static bool floorBlock(const ItemObj *item)
{
    if (item->type == ITEM_MOVING_BLOCK)
        return (item->flags&ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE && item->state != 2;
    return blockType(item->type) && blockAffectsFloor(item->flags);
}
static bool cardinal(int16 angle)
{
    return angle == 0 || angle == ANGLE_180 || angle == ANGLE_90 || angle == -ANGLE_90;
}
static bool sectorIndex(uint32 room,int32 x,int32 z,uint16 *index)
{
    const RoomInfo *info=tr64_level_rooms(gs->core,room);
    int32 sx=i32_sub(x,tr64_i32(info->x))>>10;
    int32 sz=i32_sub(z,tr64_i32(info->z))>>10;
    uint32 nx=tr64_u16(info->x_sectors),nz=tr64_u16(info->z_sectors);
    if (sx < 0 || sz < 0 || (uint32)sx >= nx || (uint32)sz >= nz) return false;
    uint32 offset=(uint32)sx*nz+(uint32)sz;
    if (offset > UINT16_MAX) return false;
    *index=(uint16)offset;
    return true;
}
static Sector originalSector(uint32 room,uint16 sector)
{
    const RoomInfo *info=tr64_level_rooms(gs->core,room);
    const tr64_core_sectors_t *s=tr64_level_sectors(gs->core,tr64_u32(info->sector_start)+sector);
    return (Sector){tr64_u16(s->floor_index),tr64_u16(s->box_index),s->room_below,s->floor,s->room_above,s->ceiling};
}
static bool buildPairs(SaveWorld *world)
{
    uint32 rooms=gs->level.roomsCount;
    uint32 sectors=tr64_level_table(gs->core,TR64_CORE_sectors)->count;
    uint32 floors=tr64_level_table(gs->core,TR64_CORE_floor_data)->count;
    if (rooms > MAX_ROOMS || gs->level.itemsCount > MAX_ITEMS || gs->level.boxesCount > SAVE_WORLD_MAX_BOXES)
        return false;
    memset(world,0,sizeof(*world));
    memset(world->mate,NO_ROOM,sizeof(world->mate));
    for (uint32 i=0;i<rooms;i++) {
        const RoomInfo *info=tr64_level_rooms(gs->core,i);
        uint32 nx=tr64_u16(info->x_sectors),nz=tr64_u16(info->z_sectors);
        uint32 start=tr64_u32(info->sector_start),count=nx*nz;
        if (!nx || !nz || start > sectors || count > sectors-start) return false;
        for (uint32 j=0;j<count;j++) {
            const tr64_core_sectors_t *s=tr64_level_sectors(gs->core,start+j);
            uint32 box=tr64_u16(s->box_index),floor=tr64_u16(s->floor_index);
            if ((box != NO_BOX && box >= gs->level.boxesCount) ||
                (s->room_below != NO_ROOM && s->room_below >= rooms) ||
                (s->room_above != NO_ROOM && s->room_above >= rooms) ||
                (floor && floor >= floors)) return false;
        }
        int32 other=tr64_i16(info->alternate_room);
        if (other == -1) continue;
        if (other < 0 || (uint32)other >= rooms || (uint32)other == i ||
            world->mate[i] != NO_ROOM || world->mate[other] != NO_ROOM) return false;
        const RoomInfo *pair=tr64_level_rooms(gs->core,(uint32)other);
        if (tr64_i16(pair->alternate_room) != -1 || tr64_i32(info->x) != tr64_i32(pair->x) ||
            tr64_i32(info->z) != tr64_i32(pair->z) || nx != tr64_u16(pair->x_sectors) || nz != tr64_u16(pair->z_sectors))
            return false;
        world->mate[i]=(uint8)other;
        world->mate[other]=(uint8)i;
        world->alternate[other]=1;
    }
    return true;
}
static bool portalRoom(const Sector *sector,uint8 *room)
{
    *room=NO_ROOM;
    if (!sector->floorIndex) return true;
    uint32 pos=sector->floorIndex;
    uint32 count=tr64_level_table(gs->core,TR64_CORE_floor_data)->count;
    if (pos >= count) return false;
    uint16 command=tr64_u16(gs->level.floors[pos++]);
    if (FD_FLOOR_TYPE(command) == FLOOR_TYPE_FLOOR) {
        if (FD_END(command)) return true;
        if (pos >= count || count-pos < 2) return false;
        pos++;
        command=tr64_u16(gs->level.floors[pos++]);
    }
    if (FD_FLOOR_TYPE(command) == FLOOR_TYPE_CEILING) {
        if (FD_END(command)) return true;
        if (pos >= count || count-pos < 2) return false;
        pos++;
        command=tr64_u16(gs->level.floors[pos++]);
    }
    if (FD_FLOOR_TYPE(command) != FLOOR_TYPE_PORTAL) return true;
    if (pos >= count) return false;
    uint16 target=tr64_u16(gs->level.floors[pos]);
    if (target == NO_ROOM || target >= gs->level.roomsCount) return false;
    *room=(uint8)target;
    return true;
}
static void touchRef(SaveWorld *world,const SaveDoorRef *ref)
{
    world->touched[ref->room]=true;
    if (ref->box != NO_BOX) world->boxes[ref->box>>3]|=(uint8)(1u<<(ref->box&7));
}
static bool addDoorVariant(SaveWorld *world,uint32 logical,bool alternate,uint8 flipped,
                           int32 x,int32 z,int16 angle,SaveDoorRef refs[4],uint32 *count)
{
    if (logical >= gs->level.roomsCount || *count >= 4) return false;
    int32 dx=0,dz=0;
    if (angle == 0) dz=-1024;
    else if (angle == ANGLE_180) dz=1024;
    else if (angle == ANGLE_90) dx=-1024;
    else dx=1024;
    uint16 index;
    if (!sectorIndex(logical,i32_add(x,dx),i32_add(z,dz),&index)) return false;
    Sector front=originalSector(logical,index);
    uint32 runtime=flipped && world->mate[logical] != NO_ROOM ? world->mate[logical] : logical;
    refs[(*count)++]=(SaveDoorRef){(uint8)runtime,index,front.boxIndex};
    uint8 behind;
    if (!portalRoom(&front,&behind)) return false;
    if (behind == NO_ROOM) return true;
    if (alternate) {
        if (world->mate[behind] != NO_ROOM) behind=world->mate[behind];
    } else if (world->alternate[behind]) behind=world->mate[behind];
    if (!sectorIndex(behind,x,z,&index) || *count >= 4) return false;
    Sector back=originalSector(behind,index);
    runtime=flipped && world->mate[behind] != NO_ROOM ? world->mate[behind] : behind;
    refs[(*count)++]=(SaveDoorRef){(uint8)runtime,index,back.boxIndex};
    return true;
}
static bool doorRefs(SaveWorld *world,uint32 room,int32 x,int32 z,int16 angle,uint8 flipped,
                     SaveDoorRef refs[4],uint32 *count)
{
    if (room >= gs->level.roomsCount || !cardinal(angle)) return false;
    uint32 base=world->alternate[room] ? world->mate[room] : room;
    *count=0;
    if (!addDoorVariant(world,base,false,flipped,x,z,angle,refs,count)) return false;
    if (world->mate[base] != NO_ROOM && !addDoorVariant(world,world->mate[base],true,flipped,x,z,angle,refs,count))
        return false;
    for (uint32 i=0;i<*count;i++) touchRef(world,refs+i);
    return true;
}
static bool sectorBudget(const SaveWorld *world,uint8 flipped)
{
    uint32 required=0;
    for (uint32 i=0;i<gs->level.roomsCount;i++) {
        if (!world->touched[i]) continue;
        uint32 selected=flipped && world->mate[i] != NO_ROOM ? world->mate[i] : i;
        const RoomInfo *info=tr64_level_rooms(gs->core,selected);
        uint32 nx=tr64_u16(info->x_sectors),nz=tr64_u16(info->z_sectors),count=nx*nz;
        if (!nx || !nz || required > SAVE_WORLD_MAX_DYN_SECTORS || count > SAVE_WORLD_MAX_DYN_SECTORS-required)
            return false;
        required+=count;
    }
    return true;
}
static bool contextValid(void)
{
    return gs && gs->core && gs->core->ready && gs->rooms && gs->sectorState &&
        gs->items && gs->boxFlags && gs->level.roomsCount <= MAX_ROOMS &&
        gs->level.itemsCount <= MAX_ITEMS && gs->level.boxesCount <= SAVE_WORLD_MAX_BOXES &&
        gs->level.roomsCount == tr64_level_table(gs->core,TR64_CORE_rooms)->count &&
        gs->level.itemsCount == tr64_level_table(gs->core,TR64_CORE_items)->count &&
        gs->level.boxesCount == tr64_level_table(gs->core,TR64_CORE_boxes)->count;
}
bool game_save_validate_world(const void *data,uint32 bytes,const GameSavePlan *plan)
{
    SaveWorld world;
    if (!contextValid() || !data || !plan || plan->totalBytes != bytes ||
        plan->header.flipped > 1 || plan->header.itemCount != gs->level.itemsCount ||
        !buildPairs(&world)) return false;
    uint8 flipped=(uint8)plan->header.flipped;
    for (uint32 i=0;i<gs->level.itemsCount;i++) {
        const tr64_core_items_t *source=tr64_level_items(gs->core,i);
        uint32 type=tr64_u16(source->type);
        if (!doorType(type) && !blockType(type)) continue;
        GameSaveItem record;
        if (!game_save_item_base(data,bytes,plan->itemOffsets[i],&record) ||
            record.roomIndex >= gs->level.roomsCount) return false;
        const RoomInfo *bounds=tr64_level_rooms(gs->core,record.roomIndex);
        if (doorType(type)) {
            uint32 room=tr64_u16(source->room);
            if (room >= gs->level.roomsCount || record.roomIndex != room) return false;
            const RoomInfo *origin=tr64_level_rooms(gs->core,room);
            int16 x=wrap_i16(i32_sub(tr64_i32(source->x),tr64_i32(origin->x)));
            int16 z=wrap_i16(i32_sub(tr64_i32(source->z),tr64_i32(origin->z)));
            int16 angle=((int32)tr64_level_quadrant(source->angle)-2)*ANGLE_90;
            if (record.x != x || record.z != z || record.ay != angle || !cardinal(record.ay))
                return false;
            SaveDoorRef refs[4];
            uint32 count;
            if (!doorRefs(&world,room,i32_add(x,tr64_i32(origin->x)),
                          i32_add(z,tr64_i32(origin->z)),angle,flipped,refs,&count))
                return false;
        } else {
            if (record.x < 0 || record.z < 0 ||
                (uint32)(record.x>>10) >= tr64_u16(bounds->x_sectors) ||
                (uint32)(record.z>>10) >= tr64_u16(bounds->z_sectors)) return false;
            if (blockAffectsFloor(record.flags)) world.touched[record.roomIndex]=true;
        }
    }
    return sectorBudget(&world,flipped);
}
static bool logicalRoom(const ItemObj *item,uint32 *index)
{
    uintptr_t base=(uintptr_t)gs->rooms,room=(uintptr_t)item->room;
    if (!item->room || room < base || (room-base)%sizeof(Room) ||
        (room-base)/sizeof(Room) >= gs->level.roomsCount) return false;
    *index=(uint32)((room-base)/sizeof(Room));
    return true;
}
static bool preflightRuntime(SaveWorld *world,uint8 flipped)
{
    if (!buildPairs(world)) return false;
    for (uint32 i=0;i<gs->level.itemsCount;i++) {
        const ItemObj *item=gs->items+i;
        uint32 room;
        if (doorType(item->type)) {
            SaveDoorRef refs[4];
            uint32 count;
            if (!logicalRoom(item,&room) ||
                !doorRefs(world,room,item->pos.x,item->pos.z,item->angle.y,flipped,refs,&count))
                return false;
        } else if (floorBlock(item)) {
            uint16 sector;
            if (!logicalRoom(item,&room) || !sectorIndex(room,item->pos.x,item->pos.z,&sector))
                return false;
            world->touched[room]=true;
        }
    }
    return sectorBudget(world,flipped);
}
bool game_save_rebuild_world(void)
{
    SaveWorld world;
    if (!contextValid() || gs->gSaveGame.flipped > 1) return false;
    uint8 flipped=(uint8)gs->gSaveGame.flipped;
    if (!preflightRuntime(&world,flipped)) return false;

    /* Bind from immutable identity, never toggle relative to the already
     * restored header flag: it can equal the target while room bindings don't. */
    uint32 sectors=tr64_level_table(gs->core,TR64_CORE_sectors)->count;
    for (uint32 i=0;i<sectors;i++) {
        const tr64_core_sectors_t *s=tr64_level_sectors(gs->core,i);
        gs->sectorState[i]=(Sector){tr64_u16(s->floor_index),tr64_u16(s->box_index),
            s->room_below,s->floor,s->room_above,s->ceiling};
    }
    for (uint32 i=0;i<gs->level.roomsCount;i++) {
        uint32 selected=flipped && world.mate[i] != NO_ROOM ? world.mate[i] : i;
        Room *room=gs->rooms+i;
        room->info=tr64_level_rooms(gs->core,selected);
        room->source=selected;
        room->sectors=gs->sectorState+tr64_u32(room->info->sector_start);
    }
    for (uint32 i=0;i<gs->level.itemsCount;i++) {
        const ItemObj *item=gs->items+i;
        if (!floorBlock(item)) continue;
        uint32 room=0;
        uint16 index=0;
        bool valid=logicalRoom(item,&room) && sectorIndex(room,item->pos.x,item->pos.z,&index);
        ASSERT(valid);
        Sector *sector=gs->rooms[room].sectors+index;
        if (item->type == ITEM_MOVING_BLOCK) {
            game_moving_block_sector(gs->rooms+room,&item->pos,sector,true);
            continue;
        }
        uint32 floor=(uint32)(sector->floor-4)&255u;
        sector->floor=(int8)((int32)(floor&127u)-(int32)(floor&128u));
    }
    for (uint32 i=0;i<gs->level.boxesCount;i++) {
        if (!(world.boxes[i>>3]&(uint8)(1u<<(i&7)))) continue;
        uint16 baseline=tr64_u16(tr64_level_boxes(gs->core,i)->overlap);
        gs->boxFlags[i]=(gs->boxFlags[i]&~BOX_OVERLAP_BLOCKED)|(baseline&BOX_OVERLAP_BLOCKED);
    }
    for (uint32 i=0;i<gs->level.itemsCount;i++) {
        const ItemObj *item=gs->items+i;
        if (!doorType(item->type) || item->state == 1) continue;
        SaveDoorRef refs[4];
        uint32 count=0,room=0;
        bool valid=logicalRoom(item,&room) &&
            doorRefs(&world,room,item->pos.x,item->pos.z,item->angle.y,flipped,refs,&count);
        ASSERT(valid);
        for (uint32 j=0;j<count;j++) {
            const SaveDoorRef *ref=refs+j;
            gs->rooms[ref->room].sectors[ref->sector]=(Sector){0,NO_BOX,NO_ROOM,NO_FLOOR,NO_ROOM,NO_FLOOR};
            if (ref->box != NO_BOX && (gs->boxFlags[ref->box]&BOX_OVERLAP_BLOCKABLE))
                gs->boxFlags[ref->box]|=BOX_OVERLAP_BLOCKED;
        }
    }
    for (uint32 i=0;i<gs->level.itemsCount;i++) {
        uint32 type=tr64_u16(tr64_level_items(gs->core,i)->type);
        if ((gs->items[i].flags&ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE &&
            type >= ITEM_PUZZLEHOLE_1 && type <= ITEM_PUZZLEHOLE_4)
            gs->items[i].type=(uint8)(type+ITEM_PUZZLEHOLE_DONE_1-ITEM_PUZZLEHOLE_1);
    }
    gs->gSaveGame.flipped=flipped;
    return true;
}
