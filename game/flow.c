/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h:3232-3284 at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
static void applyAxis(int32 *value, int32 target, int32 strength)
{
    int32 delta = i32_sub(target, *value);
    *value = i32_add(*value, delta > strength ? strength : delta < -strength ? -strength : delta);
}
bool Lara_applyFlowCurrent(Lara *self)
{
    const FixedCamera *sink;
    const Sector *sector;
    const tr64_u16_be_t *zones;
    uint32 box, targetBox;
    vec3i sinkPos, target;
    Nav *n = &self->extraL->nav;
    int32 strength;
    unsigned slot=Lara_slot(self);
    int16 camera=gs->flow_camera[slot];
    if (!gs->flow_active[slot] || self->extraL->dozy)
        return false;
    if (camera < 0 || (uint32)camera >= gs->level.camerasCount ||
        !self->room || !gs->level.boxes || !gs->level.overlaps || gs->gSaveGame.flipped > 1)
    {
        gs->flow_active[slot] = false;
        return false;
    }
    sink = gs->level.cameras + camera;
    sector = Room_getSector(self->room, self->pos.x, self->pos.z);
    box = sector->boxIndex;
    targetBox = tr64_u16(sink->flags);
    if (box >= gs->level.boxesCount || targetBox >= gs->level.boxesCount)
        return false;
    zones = Enemy_zoneTable(gs->gSaveGame.flipped, ZONE_FLY);
    if (tr64_u16(zones[box]) != tr64_u16(zones[targetBox]))
        return false;
    n->stepHeight = WALL;
    n->dropHeight = -WALL;
    n->vSpeed = 0;
    Nav_init(n, box);
    n->nextBox = targetBox;
    sinkPos = vec3i_create(tr64_i32(sink->x), tr64_i32(sink->y), tr64_i32(sink->z));
    n->pos = sinkPos;
    target = Nav_getWaypoint(n, box, &self->pos);
    if (box != targetBox)
    {
        int32 here = i32_add(abs(i32_sub(self->pos.x, sinkPos.x)), abs(i32_sub(self->pos.z, sinkPos.z)));
        int32 there = i32_add(abs(i32_sub(target.x, sinkPos.x)), abs(i32_sub(target.z, sinkPos.z)));
        if (there > here)
            target = sinkPos;
    }
    strength = tr64_i16(sink->room_or_speed) * 6;
    applyAxis(&self->pos.x, target.x, strength);
    applyAxis(&self->pos.z, target.z, strength);
    if (self->angle.x <= 0)
        applyAxis(&self->pos.y, target.y, strength);
    gs->flow_active[slot] = false;
    if (self->waterState == WATER_STATE_SURFACE)
        Lara_s_dive(self);
    return true;
}
