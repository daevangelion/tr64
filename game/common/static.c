/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/room.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/room.h:284-372,602-616 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

Room **Room_getAdjRooms(Room *self)
{
    Room **list = gs->roomsList;
    uint32 i, start = tr64_u32(self->info->portal_start);
    *list++ = self;
    for (i = 0; i < tr64_u32(self->info->portal_count); ++i)
    {
        if (list - gs->roomsList >= 31)
            break;
        *list++ = gs->rooms + tr64_u16(tr64_level_portals(gs->core, start + i)->room);
    }
    *list = NULL;
    return gs->roomsList;
}
static const tr64_core_static_meshes_t *findStatic(uint32 id)
{
    uint32 i, count = tr64_level_table(gs->core, TR64_CORE_static_meshes)->count;
    for (i = 0; i < count; ++i)
    {
        const tr64_core_static_meshes_t *s = tr64_level_static_meshes(gs->core, i);
        if (tr64_u32(s->id) == id)
            return s;
    }
    sys_error("game static definition %lu missing", (unsigned long)id);
}
bool Room_collideStatic(Room *self, CollisionInfo *cinfo, const vec3i *p, int32 height)
{
    Room **nearRoom;
    AABBi objBox = {-cinfo->radius, cinfo->radius, -height, 0, -cinfo->radius, cinfo->radius};
    cinfo->staticHit = false;
    cinfo->offset = vec3i_create(0, 0, 0);
    nearRoom = Room_getNearRooms(self, p, cinfo->radius, height);
    while (*nearRoom)
    {
        const Room *room = *nearRoom++;
        int32 ox = tr64_i32(room->info->x), oz = tr64_i32(room->info->z);
        int32 rx = p->x - ox, ry = p->y, rz = p->z - oz;
        uint32 i, start = tr64_u32(room->info->instance_start);
        for (i = 0; i < tr64_u32(room->info->instance_count); ++i)
        {
            const tr64_core_static_instances_t *mesh =
                tr64_level_static_instances(gs->core, start + i);
            const tr64_core_static_meshes_t *s = findStatic(tr64_u16(mesh->mesh));
            int32 x = (int16)(tr64_i32(mesh->x) - ox) - rx;
            int32 y = (int16)tr64_i32(mesh->y) - ry;
            int32 z = (int16)(tr64_i32(mesh->z) - oz) - rz;
            AABBi meshBox;
            bool flip;
            if (tr64_u16(s->flags) & STATIC_MESH_FLAG_NO_COLLISION)
                continue;
            if (abs(x) > 5120 || abs(y) > 5120 || abs(z) > 5120)
                continue;
            meshBox.minX = tr64_i16(s->collision_min_x);
            meshBox.maxX = tr64_i16(s->collision_max_x);
            meshBox.minY = tr64_i16(s->collision_min_y);
            meshBox.maxY = tr64_i16(s->collision_max_y);
            meshBox.minZ = tr64_i16(s->collision_min_z);
            meshBox.maxZ = tr64_i16(s->collision_max_z);
            boxRotateYQ_c(&meshBox, tr64_level_quadrant(mesh->angle));
            boxTranslate_c(&meshBox, x, y, z);
            if (!boxIntersect(&meshBox, &objBox))
                continue;
            cinfo->offset = boxPushOut(&meshBox, &objBox);
            flip = cinfo->quadrant > 1;
            if (cinfo->quadrant & 1)
            {
                if (abs(cinfo->offset.z) > cinfo->radius)
                {
                    cinfo->offset.z = cinfo->pos.z - p->z;
                    if ((cinfo->offset.x < 0 && cinfo->quadrant == 1) ||
                        (cinfo->offset.x > 0 && cinfo->quadrant == 3))
                        cinfo->type = CT_FRONT;
                }
                else if (cinfo->offset.z)
                {
                    cinfo->offset.x = 0;
                    cinfo->type = ((cinfo->offset.z > 0) ^ flip) ? CT_RIGHT : CT_LEFT;
                }
                else
                    cinfo->offset = vec3i_create(0, 0, 0);
            }
            else
            {
                if (abs(cinfo->offset.x) > cinfo->radius)
                {
                    cinfo->offset.x = cinfo->pos.x - p->x;
                    if ((cinfo->offset.z < 0 && cinfo->quadrant == 0) ||
                        (cinfo->offset.z > 0 && cinfo->quadrant == 2))
                        cinfo->type = CT_FRONT;
                }
                else if (cinfo->offset.x)
                {
                    cinfo->offset.z = 0;
                    cinfo->type = ((cinfo->offset.x > 0) ^ flip) ? CT_LEFT : CT_RIGHT;
                }
                else
                    cinfo->offset = vec3i_create(0, 0, 0);
            }
            cinfo->staticHit = cinfo->offset.x != 0 || cinfo->offset.z != 0;
            return true;
        }
    }
    return false;
}
