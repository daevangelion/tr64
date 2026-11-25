/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

static void Doppelganger_resolveMirrorRoom(ItemObj *self);
static void Doppelganger_update(ItemObj *self);

static void Doppelganger_hit(ItemObj *self,Lara *shooter,int32 damage,const vec3i *point,int32 soundId)
{
        Lara *lara=shooter;
        if (!lara && self->extraE)lara=self->extraE->target;
        if (!lara)lara=Enemy_getLara(&self->pos);
        if (lara)
            Lara_hit(lara, i32_mul(damage, Doppelganger_DAMAGE_SCALE), point, soundId);
    }

static void Doppelganger_resolveMirrorRoom(ItemObj *self)
{
        Room * nextRoom = Room_getRoom(self->room, self->pos.x, self->pos.y, self->pos.z);
        const Sector * sector = nextRoom ?
            Room_getSector(nextRoom, self->pos.x, self->pos.z) : NULL;
        int32 floor = sector ?
            Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z) : self->pos.y;
        if (!sector || sector->boxIndex == 0xFFFF ||
            i32_sub(floor, self->pos.y) <= Doppelganger_FALL_HEIGHT)
        {
            nextRoom = NULL;
            for (uint32 i = 0; i < gs->level.roomsCount; i++)
            {
                Room * candidate = gs->rooms + i;
                const RoomInfo * info = candidate->info;
                int32 minX = i32_shl((tr64_i32(info->x) >> 8), 8);
                int32 minZ = i32_shl((tr64_i32(info->z) >> 8), 8);
                int32 maxX = i32_add(minX, (i32_shl(((int32)(tr64_u16(info->x_sectors))), 10)));
                int32 maxZ = i32_add(minZ, (i32_shl(((int32)(tr64_u16(info->z_sectors))), 10)));
                if (self->pos.x < minX || self->pos.x >= maxX ||
                    self->pos.z < minZ || self->pos.z >= maxZ)
                    continue;
                sector = Room_getSector(candidate, self->pos.x, self->pos.z);
                if (sector->boxIndex == 0xFFFF)
                    continue;
                floor = Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z);
                if (i32_sub(floor, self->pos.y) > Doppelganger_FALL_HEIGHT)
                {
                    nextRoom = candidate;
                    break;
                }
            }
        }
        if (nextRoom && nextRoom != self->room)
        {
            Room_remove(self->room, self);
            Room_add(nextRoom, self);
        }
    }

static void Doppelganger_update(ItemObj *self)
{
        if ((self->flags & ((3 << 3))) == ((3 << 3)))
        {
            if (!(self->flags & ((1 << 2))) || !Enemy_enable(self, false))
                return;
            self->flags &= ~((3 << 3));
            self->flags |= ((1 << 3));
        }
        if (!self->extraE)
            return;
        Lara *lara=self->extraE->target;
        if (!lara)lara=self->extraE->target=Enemy_getLara(&self->pos);
        if (!lara)
            return;
        if (!(self->flags & ((1 << 1))))
        {
            self->pos.x = i32_sub(i32_mul(Doppelganger_ROOM_CENTER_X, 2), (lara->pos).x);
            self->pos.y = (lara->pos).y;
            self->pos.z = i32_sub(i32_mul(Doppelganger_ROOM_CENTER_Z, 2), (lara->pos).z);
            (self->angle = lara->angle);
            self->angle.y = wrap_i16((uint32)self->angle.y - (uint32)ANGLE_180);
            Doppelganger_resolveMirrorRoom(self);
        }
        ItemObj_updateRoom(self, 0);
        const Sector * laraSector = Room_getSector(lara->room, (lara->pos).x, (lara->pos).z);
        int32 laraFloor = Sector_getFloor(laraSector, (lara->pos).x, (lara->pos).y, (lara->pos).z);
        int32 laraHeight = i32_sub(laraFloor, (lara->pos).y);
        const Sector * selfSector = Room_getSector(self->room, self->pos.x, self->pos.z);
        self->roomFloor = Sector_getFloor(selfSector, self->pos.x, self->pos.y, self->pos.z);
        int32 selfHeight = i32_sub(self->roomFloor, self->pos.y);
        if (!(self->flags & ((1 << 1))) &&
            !(lara->flags & ((1 << 1))) &&
            selfHeight > Doppelganger_FALL_HEIGHT && laraHeight < 256)
        {
            ItemObj_animSet(self, i32_add((gs->level.models[ITEM_LARA]).animIndex, Lara_ANIM_FALL_FORTH), true, 0);
            self->flags |= ((1 << 1));
            self->vSpeed = self->hSpeed = 0;
        }
        if (self->flags & ((1 << 1)))
        {
            if (selfHeight < Doppelganger_LAND_HEIGHT)
            {
                game_check_trigger(gs->gLastFloorData, NULL);
                if (gs->stop.kind != TR64_GAME_STOP_NONE)
                    return;
                self->flags &= ~((3 << 3));
                self->flags |= ((3 << 3));
                ItemObj_deactivate(self);
                if (self->extraE)
                    Enemy_disable(self);
                return;
            }
            ItemObj_animProcess(self, true);
            return;
        }
        self->animIndex = lara->animIndex;
        self->frameIndex = lara->frameIndex;
    }

uint32 tr64_unit_enemy_doppelganger(uint32 arg)
{
    GameItemCall *call=(GameItemCall *)(uintptr_t)arg;
    ItemObj *self=call->self;
    switch(call->op) {
    case GAME_ITEM_CONSTRUCT:Enemy_construct(self);break;
    case GAME_ITEM_ACTIVATE:Enemy_activate(self);break;
    case GAME_ITEM_UPDATE:Doppelganger_update(self);break;
    case GAME_ITEM_COLLIDE:Enemy_collide(self,call->lara,&gs->cinfo);break;
    case GAME_ITEM_HIT:Doppelganger_hit(self,call->lara,call->damage,call->point,call->soundId);break;
    default:sys_error("game enemy_doppelganger operation: %u",(unsigned)call->op);
    }
    return 0;
}
