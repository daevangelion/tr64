/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/item.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#if TR64_HAS_SEQUEL
#include "game/tr2/objects.h"
#endif
#line 6

void Bubble_update(Bubble *self)
{
    (self->pos.y = i32_add(self->pos.y, self->vSpeed));
    if (self->roomFloor > self->pos.y)
    {
        ItemObj_remove(self);
        return;
    }
    self->angle.x += (ANGLE(9));
    self->angle.z += (ANGLE(13));
    int32 dx = (phd_sin(self->angle.x));
    int32 dz = (phd_sin(self->angle.z));
    (self->pos.x = i32_add(self->pos.x, (dx * 11) >> 14));
    (self->pos.z = i32_add(self->pos.z, (dz * 8) >> 14));
    Room *nextRoom = Room_getRoom(self->room, self->pos.x, self->pos.y, self->pos.z);
    if (nextRoom != self->room)
    {
        Room_remove(self->room, self);
        Room_add(nextRoom, self);
    }
}

void ItemObj_activateBase(ItemObj *self)
{
    if (self->flags & ITEM_FLAG_ACTIVE)
        return;
    self->flags |= ITEM_FLAG_ACTIVE;
    self->nextActive = gs->firstActive;
    gs->firstActive = self;
}
#line 36 /* Preserve cart assertion IDs below. */
const Anim *ItemObj_animChange(ItemObj *self, const Anim *anim)
{
    if (self->goalState == self->state || !tr64_u16(anim->state_count))
        return anim;
    const AnimState *animState = gs->level.animStates + tr64_u16(anim->state_offset);
    for (int32 i = 0; i < tr64_u16(anim->state_count); i++)
    {
        if (self->goalState == tr64_u16(animState->state))
        {
            const AnimRange *animRange = gs->level.animRanges + tr64_u16(animState->range_offset);
            for (int32 j = 0; j < tr64_u16(animState->range_count); j++)
            {
                if ((self->frameIndex >= tr64_u16(animRange->low)) &&
                    (self->frameIndex <= tr64_u16(animRange->high)))
                {
                    self->frameIndex = tr64_u16(animRange->next_frame);
                    self->animIndex = tr64_u16(animRange->next_animation);
                    anim = gs->level.anims + tr64_u16(animRange->next_animation);
                    self->state = ((uint8)(tr64_u16(anim->state)));
                    return anim;
                }
                animRange++;
            }
        }
        animState++;
    }
    return anim;
}

bool ItemObj_animIsEnd(const ItemObj *self, int32 offset)
{
    return self->frameIndex ==
           i32_sub(tr64_u16(gs->level.anims[self->animIndex].frame_end), offset);
}

void ItemObj_animProcess(ItemObj *self, bool movement)
{
    ASSERT(gs->level.models[self->type].count > 0);
    const Anim *anim = gs->level.anims + self->animIndex;
    self->frameIndex++;
#if TR64_HAS_SEQUEL
    const Anim *before = anim;
    anim = ItemObj_animChange(self, anim);
    if ((self->type != ITEM_LARA) && (self->nextState == self->state) &&
        (!TR64_RULE(ITEM_END_GOAL) || anim != before))
    {
        self->nextState = 0;
    }
#else
    anim = ItemObj_animChange(self, anim);
    if ((self->type != ITEM_LARA) && (self->nextState == self->state))
    {
        self->nextState = 0;
    }
#endif
    if (self->frameIndex > tr64_u16(anim->frame_end))
    {
        ItemObj_animCmd(self, 0, anim);
        self->frameIndex = tr64_u16(anim->next_frame);
        self->animIndex = tr64_u16(anim->next_animation);
        anim = gs->level.anims + tr64_u16(anim->next_animation);
#if TR64_HAS_SEQUEL
        const uint8 next = (uint8)tr64_u16(anim->state);
        const bool keep = (TR64_RULE(ITEM_END_GOAL) && next == self->state) ||
                          (TR64_RULE(INTERACTIONS) && tr2DoorType(self->type));
        self->state = next;
        if (self->type != ITEM_LARA)
        {
            if (!keep)
                self->goalState = self->state;
#else
        self->state = ((uint8)(tr64_u16(anim->state)));
        if (self->type != ITEM_LARA)
        {
            self->goalState = self->state;
#line 91 /* Preserve cart assertion IDs below. */
#endif
            if (self->nextState == self->state)
            {
                self->nextState = 0;
            }
        }
    }
    ItemObj_animCmd(self, 1, anim);
    if (movement)
    {
        ItemObj_move(self);
    }
}

const Anim *ItemObj_animSet(ItemObj *self, int32 newAnimIndex, bool resetState, int32 frameOffset)
{
    const Anim *anim = gs->level.anims + newAnimIndex;
    self->animIndex = newAnimIndex;
    self->frameIndex = i32_add(tr64_u16(anim->frame_start), frameOffset);
    if (resetState)
    {
        self->state = self->goalState = ((uint8)(tr64_u16(anim->state)));
    }
    return anim;
}

void ItemObj_collideRoom(const ItemObj *self, int32 height, int32 yOffset)
{
    gs->cinfo.type = CT_NONE;
    (gs->cinfo.offset = vec3i_create(0, 0, 0));
    vec3i p = self->pos;
    (p.y = i32_add(p.y, yOffset));
    int32 y = i32_sub(p.y, height);
    int32 cy = i32_sub(y, 160);
    int32 floor;
    int32 ceiling;
    Room *nextRoom = self->room;
    {
        nextRoom = Room_getRoom(nextRoom, p.x, cy, p.z);
        const Sector *sector = Room_getSector(nextRoom, p.x, p.z);
        floor = Sector_getFloor(sector, p.x, cy, p.z);
        if (floor != (WALL))
            (floor = i32_sub(floor, p.y));
        ceiling = Sector_getCeiling(sector, p.x, cy, p.z);
        if (ceiling != (WALL))
            (ceiling = i32_sub(ceiling, y));
    };
    gs->cinfo.trigger = gs->gLastFloorData;
    gs->cinfo.slantX = ((int8)((gs->gLastFloorSlant) & 0xFF));
    gs->cinfo.slantZ = ((int8)((gs->gLastFloorSlant) >> 8));
    CollisionInfo_setSide(&(gs->cinfo), CollisionInfo_ST_MIDDLE, floor, ceiling);
    vec3i f;
    vec3i l;
    vec3i r;
    int32 R = gs->cinfo.radius;
    int32 s;
    int32 c;
    sincos(gs->cinfo.angle, s, c);
    switch (gs->cinfo.quadrant)
    {
    case 0:
    {
        (f = vec3i_create((i32_mul(R, s)) >> 14, 0, R));
        (l = vec3i_create(-R, 0, R));
        (r = vec3i_create(R, 0, R));
        break;
    }
    case 1:
    {
        (f = vec3i_create(R, 0, (i32_mul(R, c)) >> 14));
        (l = vec3i_create(R, 0, R));
        (r = vec3i_create(R, 0, -R));
        break;
    }
    case 2:
    {
        (f = vec3i_create((i32_mul(R, s)) >> 14, 0, -R));
        (l = vec3i_create(R, 0, -R));
        (r = vec3i_create(-R, 0, -R));
        break;
    }
    case 3:
    {
        (f = vec3i_create(-R, 0, (i32_mul(R, c)) >> 14));
        (l = vec3i_create(-R, 0, -R));
        (r = vec3i_create(-R, 0, R));
        break;
    }
    default:
    {
        f.x = f.y = f.z = 0;
        l.x = l.y = l.z = 0;
        r.x = r.y = r.z = 0;
        ASSERT(false);
        ;
    }
    }
    (f = vec3i_add(f, p));
    (l = vec3i_add(l, p));
    (r = vec3i_add(r, p));
    vec3i delta;
    delta.x = i32_sub(gs->cinfo.pos.x, p.x);
    delta.y = i32_sub(gs->cinfo.pos.y, p.y);
    delta.z = i32_sub(gs->cinfo.pos.z, p.z);
    {
        nextRoom = Room_getRoom(nextRoom, f.x, cy, f.z);
        const Sector *sector = Room_getSector(nextRoom, f.x, f.z);
        floor = Sector_getFloor(sector, f.x, cy, f.z);
        if (floor != (WALL))
            (floor = i32_sub(floor, p.y));
        ceiling = Sector_getCeiling(sector, f.x, cy, f.z);
        if (ceiling != (WALL))
            (ceiling = i32_sub(ceiling, y));
    };
    CollisionInfo_setSide(&(gs->cinfo), CollisionInfo_ST_FRONT, floor, ceiling);
    {
        nextRoom = Room_getRoom(nextRoom, l.x, cy, l.z);
        const Sector *sector = Room_getSector(nextRoom, l.x, l.z);
        floor = Sector_getFloor(sector, l.x, cy, l.z);
        if (floor != (WALL))
            (floor = i32_sub(floor, p.y));
        ceiling = Sector_getCeiling(sector, l.x, cy, l.z);
        if (ceiling != (WALL))
            (ceiling = i32_sub(ceiling, y));
    };
    CollisionInfo_setSide(&(gs->cinfo), CollisionInfo_ST_LEFT, floor, ceiling);
    {
        nextRoom = Room_getRoom(nextRoom, r.x, cy, r.z);
        const Sector *sector = Room_getSector(nextRoom, r.x, r.z);
        floor = Sector_getFloor(sector, r.x, cy, r.z);
        if (floor != (WALL))
            (floor = i32_sub(floor, p.y));
        ceiling = Sector_getCeiling(sector, r.x, cy, r.z);
        if (ceiling != (WALL))
            (ceiling = i32_sub(ceiling, y));
    };
    CollisionInfo_setSide(&(gs->cinfo), CollisionInfo_ST_RIGHT, floor, ceiling);
    Room_collideStatic(self->room, &(gs->cinfo), &(p), height);
    if (gs->cinfo.m.floor == (WALL))
    {
        (gs->cinfo.offset = delta);
        gs->cinfo.type = CT_FRONT;
        return;
    }
    if (gs->cinfo.m.floor <= gs->cinfo.m.ceiling)
    {
        (gs->cinfo.offset = delta);
        gs->cinfo.type = CT_FLOOR_CEILING;
        return;
    }
    if (gs->cinfo.m.ceiling >= 0)
    {
        gs->cinfo.offset.y = gs->cinfo.m.ceiling;
        gs->cinfo.type = CT_CEILING;
    }
    if (gs->cinfo.f.floor > gs->cinfo.gapPos || gs->cinfo.f.floor < gs->cinfo.gapNeg ||
        gs->cinfo.f.ceiling > gs->cinfo.gapCeiling)
    {
        if (gs->cinfo.quadrant & 1)
        {
            gs->cinfo.offset.x = alignOffset(f.x, p.x);
            gs->cinfo.offset.z = delta.z;
        }
        else
        {
            gs->cinfo.offset.x = delta.x;
            gs->cinfo.offset.z = alignOffset(f.z, p.z);
        }
        gs->cinfo.type = CT_FRONT;
        return;
    }
    if (gs->cinfo.f.ceiling >= gs->cinfo.gapCeiling
#if TR64_HAS_SEQUEL
        /* Q-T2M-03: exact contact at the upward-jump tolerance is clear. */
        && !(TR64_RULE(CLIMB_WALL) && self->type==ITEM_LARA && self->state==Lara_STATE_JUMP_UP)
#endif
    )
#ifndef TR64_HOST
#line 262 /* Preserve the cart assertion ID after this block. */
#endif
    {
        (gs->cinfo.offset = delta);
        gs->cinfo.type = CT_FRONT_CEILING;
        return;
    }
    if (gs->cinfo.l.floor > gs->cinfo.gapPos || gs->cinfo.l.floor < gs->cinfo.gapNeg)
    {
        if (gs->cinfo.quadrant & 1)
        {
            gs->cinfo.offset.z = alignOffset(l.z, f.z);
        }
        else
        {
            gs->cinfo.offset.x = alignOffset(l.x, f.x);
        }
        gs->cinfo.type = CT_LEFT;
        return;
    }
    if (gs->cinfo.r.floor > gs->cinfo.gapPos || gs->cinfo.r.floor < gs->cinfo.gapNeg)
    {
        if (gs->cinfo.quadrant & 1)
        {
            gs->cinfo.offset.z = alignOffset(r.z, f.z);
        }
        else
        {
            gs->cinfo.offset.x = alignOffset(r.x, f.x);
        }
        gs->cinfo.type = CT_RIGHT;
        return;
    }
}

void ItemObj_deactivateBase(ItemObj *self)
{
    ItemObj *prev = NULL;
    ItemObj *curr = gs->firstActive;
    while (curr)
    {
        ItemObj *next = curr->nextActive;
        if (curr == self)
        {
            self->flags &= ~ITEM_FLAG_ACTIVE;
            self->nextActive = NULL;
            if (prev)
            {
                prev->nextActive = next;
            }
            else
            {
                gs->firstActive = next;
            }
            break;
        }
        prev = curr;
        curr = next;
    }
}

void ItemObj_fxBubbles(ItemObj *self, Room *fxRoom, int32 fxJoint, const vec3i *fxOffset)
{
    int32 count = rand_draw() % 3;
    if (!count)
        return;
    vec3i fxPos = vec3i_add(self->pos, ItemObj_getJoint(self, fxJoint, fxOffset));
    for (int32 i = 0; i < count; i++)
    {
        ItemObj_add(ITEM_BUBBLE, fxRoom, &(fxPos), 0);
    }
}

void ItemObj_fxSplash(ItemObj *self)
{
    vec3i fxPos = self->pos;
    fxPos.y = ItemObj_getWaterLevel(self);
    for (int32 i = 0; i < 10; i++)
    {
        ItemObj *splash =
            ItemObj_add(ITEM_SPLASH, self->room, &(fxPos), (i32_sub(rand_draw(), ANGLE_90) * 2));
        if (!splash)
            return;
        splash->hSpeed = ((int16)(rand_draw() >> 8));
        splash->flags |= ITEM_FLAG_ANIMATED;
    }
}

int32 ItemObj_getBridgeFloor(const ItemObj *self, int32 x, int32 z)
{
    if (self->type == ITEM_BRIDGE_FLAT)
        return self->pos.y;
    int32 h;
    if (self->angle.y == 0)
    {
        h = i32_sub(1024, x);
    }
    else if (self->angle.y == ANGLE_180)
    {
        h = x;
    }
    else if (self->angle.y == (ANGLE_90))
    {
        h = z;
    }
    else
    {
        h = i32_sub(1024, z);
    }
    h &= 1023;
    return i32_add(self->pos.y, (self->type == ITEM_BRIDGE_TILT_1) ? (h >> 2) : (h >> 1));
}

int32 ItemObj_getDrawBridgeFloor(const ItemObj *self, int32 x, int32 z)
{
    /* Repaired defects (oracle host.patch hunk 18): item minus query put the covered sectors
     * behind the item; the 90/270 degree rows tested dz for the second sector. */
    int32 dx = ((x >> 10) - (self->pos.x >> 10));
    int32 dz = ((z >> 10) - (self->pos.z >> 10));
    if (((dx == 0) && ((dz == -1) || (dz == -2)) && (self->angle.y == 0)) ||
        ((dx == 0) && ((dz == 1) || (dz == 2)) && (self->angle.y == ANGLE_180)) ||
        ((dz == 0) && ((dx == -1) || (dx == -2)) && (self->angle.y == (ANGLE_90))) ||
        ((dz == 0) && ((dx == 1) || (dx == 2)) && (self->angle.y == -(ANGLE_90))))
    {
        return self->pos.y;
    }
    return (WALL);
}

void ItemObj_getItemFloorCeiling(const ItemObj *self, int32 x, int32 y, int32 z, int32 *floor,
                                 int32 *ceiling)
{
    int32 h = (WALL);
    unsigned type = self->type;
    int32 collapseHeight = 512;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(INTERACTIONS)) {
        /* Both editions use the same plane substitution. Bind the
         * sequel's IDs here, not in the trapdoor footprint helper. */
        if (tr2TrapdoorType(type))
            type = ITEM_TRAP_DOOR_1;
        else if (type == TR2_ITEM_COLLAPSE || type == TR2_ITEM_COLLAPSE_HIGH) {
            collapseHeight = tr2CollapseHeight(self);
            type = ITEM_TRAP_FLOOR;
        } else
            return;
    }
#endif
#if !TR64_HAS_SEQUEL
#line 393 /* Preserve the original TR1 assertion ID after this function. */
#endif
    switch (type)
    {
    case ITEM_TRAP_FLOOR:
    {
        if (self->state == 0 || self->state == 1)
        {
            h = i32_sub(self->pos.y, collapseHeight);
        }
        break;
    }
    case ITEM_DRAWBRIDGE:
    {
        if (self->state == 1)
        {
            h = ItemObj_getDrawBridgeFloor(self, x, z);
        }
        break;
    }
    case ITEM_BRIDGE_FLAT:
    case ITEM_BRIDGE_TILT_1:
    case ITEM_BRIDGE_TILT_2:
    {
        h = ItemObj_getBridgeFloor(self, x, z);
        break;
    }
    case ITEM_TRAP_DOOR_1:
    case ITEM_TRAP_DOOR_2:
    {
        if (self->state != 0)
            return;
        h = ItemObj_getTrapDoorFloor(self, x, z);
        if ((floor && (h >= *floor)) || (ceiling && (h <= *ceiling)))
            return;
    }
    }
    if (h == (WALL))
        return;
    if (floor && (y <= h))
    {
        *floor = h;
    }
    if (ceiling && (y > h))
    {
        *ceiling = i32_add(h, 256);
    }
}

vec3i ItemObj_getRelative(const ItemObj *self, const vec3i *point)
{
    matrixPush_c();
    Matrix *m = gs->gMatrixPtr;
    matrixSetIdentity_c();
    matrixRotateYXZ_c(self->angle.x, self->angle.y, self->angle.z);
    vec3i p;
    p.x =
        i32_add(self->pos.x, (i32_add(i32_add(i32_mul(m->e00, point->x), i32_mul(m->e01, point->y)),
                                      i32_mul(m->e02, point->z))) >>
                                 14);
    p.y =
        i32_add(self->pos.y, (i32_add(i32_add(i32_mul(m->e10, point->x), i32_mul(m->e11, point->y)),
                                      i32_mul(m->e12, point->z))) >>
                                 14);
    p.z =
        i32_add(self->pos.z, (i32_add(i32_add(i32_mul(m->e20, point->x), i32_mul(m->e21, point->y)),
                                      i32_mul(m->e22, point->z))) >>
                                 14);
    gs->gMatrixPtr--;
    ASSERT(gs->gMatrixPtr >= gs->gMatrixStack);
    ;
    ;
    return p;
}

int32 ItemObj_getTrapDoorFloor(const ItemObj *self, int32 x, int32 z)
{
    int32 dx = ((self->pos.x >> 10) - (x >> 10));
    int32 dz = ((self->pos.z >> 10) - (z >> 10));
    if (((dx == 0) && (dz == 0)) || ((dx == 0) && (dz == 1) && (self->angle.y == 0)) ||
        ((dx == 0) && (dz == -1) && (self->angle.y == ANGLE_180)) ||
        ((dx == 1) && (dz == 0) && (self->angle.y == (ANGLE_90))) ||
        ((dx == -1) && (dz == 0) && (self->angle.y == -(ANGLE_90))))
    {
        return self->pos.y;
    }
    return (WALL);
}

int32 ItemObj_getWaterDepth(const ItemObj *self)
{
    const Sector *sector = Room_getWaterSector(self->room, self->pos.x, self->pos.z);
    if (sector)
        return i32_sub(Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z),
                       (sector->ceiling * 256));
    return (WALL);
}

int32 ItemObj_getWaterLevel(const ItemObj *self)
{
    const Sector *sector = Room_getWaterSector(self->room, self->pos.x, self->pos.z);
    if (sector)
    {
        if (sector->roomAbove == 0xFF)
        {
            return Sector_getCeiling(sector, self->pos.x, self->pos.y, self->pos.z);
        }
        else
        {
            return (sector->ceiling * 256);
        }
    }
    return (WALL);
}

bool ItemObj_isKeyHit(const ItemObj *self, InputState state)
{
    return (self->input & state) && !(self->extraL->lastInput & state);
}

void ItemObj_meshSwap(ItemObj *self, ItemType type, uint32 mask)
{
    int32 start = gs->level.models[type].start;
    for (int32 i = 0; i < JOINT_MAX && mask; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            self->extraL->meshes[i] = i32_add(start, i);
        }
    }
}

#if TR64_HAS_SEQUEL
/* tr2-lara-ground-and-weapons, Projecting speed along a heading. */
vec3i game_tr2_heading_step(int16 yaw,int16 speed)
{
    return vec3i_create(i32_mul(tr2_sin(yaw),speed)>>14,0,
                        i32_mul(tr2_sin(yaw+ANGLE_90),speed)>>14);
}
#endif

void ItemObj_move(ItemObj *self)
{
    const Anim *anim = gs->level.anims + self->animIndex;
    int32 sp = tr64_i32(anim->speed);
    if (self->flags & ITEM_FLAG_GRAVITY)
    {
        (sp = i32_add(sp, i32_mul(tr64_i32(anim->acceleration),
                                  ((self->frameIndex - tr64_u16(anim->frame_start)) - 1))));
        self->hSpeed -= sp >> 16;
        (sp = i32_add(sp, tr64_i32(anim->acceleration)));
        self->hSpeed += sp >> 16;
        self->vSpeed += (self->vSpeed < 128) ? 6 : 1;
#if TR64_HAS_TR3
        if (self->type == ITEM_LARA && game_tr3_in_swamp(self))
        {
            /* TR3 quicksand airborne drag (§2.5): 1/8 horiz, 1/4 vert min 4. */
            if (self->hSpeed < 8)
            {
                self->hSpeed = 0;
                self->flags &= ~ITEM_FLAG_GRAVITY;
            }
            else
            {
                self->hSpeed -= self->hSpeed >> 3;
            }
            if (self->vSpeed > 128)
                self->vSpeed >>= 1;
            self->vSpeed -= self->vSpeed >> 2;
            if (self->vSpeed < 4)
                self->vSpeed = 4;
        }
#endif
        (self->pos.y = i32_add(self->pos.y, self->vSpeed));
    }
    else
    {
#if TR64_HAS_TR3
        if (self->type == ITEM_LARA && self->waterState == WATER_STATE_WADE &&
            game_tr3_in_swamp(self))
        {
            /* TR3 grounded swamp wade: half base speed + quarter accel (§2.5). */
            int32 rel = self->frameIndex - tr64_u16(anim->frame_start);
            sp = (sp >> 1) + ((tr64_i32(anim->acceleration) * rel) >> 2);
            self->hSpeed = sp >> 16;
        }
        else
#endif
        {
            (sp = i32_add(sp, i32_mul(tr64_i32(anim->acceleration),
                                      (self->frameIndex - tr64_u16(anim->frame_start)))));
            self->hSpeed = sp >> 16;
        }
    }
    int16 realAngle = (self->type == ITEM_LARA) ? self->extraL->moveAngle : self->angle.y;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(MOTION_PROJECTION)) {
        const vec3i step=game_tr2_heading_step(realAngle,self->hSpeed);
        self->pos.x=i32_add(self->pos.x,step.x);
        self->pos.z=i32_add(self->pos.z,step.z);
        return;
    }
#endif
    int32 s;
    int32 c;
    sincos(realAngle, s, c);
    (self->pos.x = i32_add(self->pos.x, i32_mul(s, self->hSpeed) >> 14));
    (self->pos.z = i32_add(self->pos.z, i32_mul(c, self->hSpeed) >> 14));
}

void ItemObj_removeOwnedFlashes(ItemObj *self)
{
    ItemObj *owned = gs->firstActive;
    while (owned)
    {
        ItemObj *next = owned->nextActive;
        if (owned->type == ITEM_MUZZLE_FLASH && (void *)owned->extraL == (void *)self)
        {
            ItemObj_remove(owned);
        }
        owned = next;
    }
}
void ItemObj_remove(ItemObj *self)
{
    if (!self->room)
        return;
    ItemObj_removeOwnedFlashes(self);
    ItemObj_deactivate(self);
    Room_remove(self->room, self);
    for (unsigned i=0;i<TR64_GAME_PLAYERS;++i)
    {
        if (((gs->playersExtra[i]).armR).target == self)
            ((gs->playersExtra[i]).armR).target = NULL;
        if (((gs->playersExtra[i]).armL).target == self)
            ((gs->playersExtra[i]).armL).target = NULL;
        if (gs->interaction[i]==self)
            gs->interaction[i]=NULL;
    }
    self->room = NULL;
#if TR64_HAS_SEQUEL
    /* Placed TR2 items (e.g. broken spiders, destroyed boats) own fixed
     * slots < itemsCount and must never re-enter firstFree; reusing them
     * for dynamic spawns (discs, flares, weapon poses) corrupts placed-item
     * identity and breaks save/load validation. */
    if (TR64_SECOND_GAME && (uint32)(self - gs->items) < gs->level.itemsCount)
    {
        self->flags |= TR2_FLAG_RETIRED;
        return;
    }
#endif
    self->nextItem = gs->firstFree;
    gs->firstFree = self;
}

void ItemObj_resetSlot(ItemObj *self)
{
    self->room = NULL;
    (self->pos = vec3i_create(0, 0, 0));
    (self->angle = vec3s_create(0, 0, 0));
    self->flags = 0;
    self->vSpeed = 0;
    self->hSpeed = 0;
    self->animIndex = 0;
    self->frameIndex = 0;
    self->state = 0;
    self->nextState = 0;
    self->goalState = 0;
    self->waterState = 0;
    self->headOffset = 0;
    self->aggression = 0;
    self->health = 0;
    self->timer = 0;
    self->input = 0;
    self->turnSpeed = 0;
    self->type = 0;
    self->intensity = 0;
    self->roomFloor = 0;
    self->hitMask = 0;
    self->visibleMask = 0;
    ((*self)).extraL = NULL;
    self->extraE = NULL;
    self->nextItem = NULL;
    self->nextActive = NULL;
}

void ItemObj_updateRoom(ItemObj *self, int32 offset)
{
    Room *nextRoom =
        Room_getRoom(self->room, self->pos.x, i32_add(self->pos.y, offset), self->pos.z);
    if (self->room != nextRoom)
    {
        Room_remove(self->room, self);
        Room_add(nextRoom, self);
    }
    const Sector *sector = Room_getSector(self->room, self->pos.x, self->pos.z);
    self->roomFloor = Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z);
}

void SpriteEffect_update(SpriteEffect *self)
{
    self->animIndex++;
    if (self->animIndex >= self->timer)
    {
        self->animIndex = 0;
        if (self->flags & ITEM_FLAG_ANIMATED)
        {
            self->frameIndex++;
            if (self->frameIndex >= -gs->level.models[self->type].count)
            {
                ItemObj_remove(self);
                return;
            }
        }
        else
        {
            ItemObj_remove(self);
            return;
        }
    }
    if (self->hSpeed)
    {
        int32 s;
        int32 c;
        sincos(self->angle.y, s, c);
        (self->pos.x = i32_add(self->pos.x, i32_mul(s, self->hSpeed) >> 14));
        (self->pos.z = i32_add(self->pos.z, i32_mul(c, self->hSpeed) >> 14));
    }
}

int32 alignOffset(int32 a, int32 b)
{
    int32 ca = a >> 10;
    int32 cb = b >> 10;
    if (ca == cb)
    {
        return 0;
    }
    a &= 1023;
    if (ca < cb)
    {
        return i32_sub(1025, a);
    }
    return -(i32_add(a, 1));
}
