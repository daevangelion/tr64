/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.cpp at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/common.cpp and common.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

void CollisionInfo_setAngle(CollisionInfo *self, int16 value)
{
    self->angle = value;
    self->quadrant = ((uint16)((value + (ANGLE_45)))) >> 14;
}

uint16 worldActionEffectiveEnd(uint16 action, uint16 extra)
{
    return ((uint16)((action & 0x7FFF) | (extra & 0x8000)));
}

int16 angleLerp(int16 a, int16 b, int32 w)
{
    int16 d = (b - a);
    if (d > +w)
        return i32_add(a, w);
    if (d < -w)
        return i32_sub(a, w);
    return b;
}

void anglesFromVector(int32 x, int32 y, int32 z, int16 *angleX, int16 *angleY)
{
    (*angleY) = phd_atan(z, x);
    (*angleX) = phd_atan(phd_sqrt(i32_add(i32_mul(x, x), i32_mul(z, z))), -y);
}

bool boxIntersect(const AABBi *a, const AABBi *b)
{
    return !(a->maxX <= b->minX || a->minX >= b->maxX || a->maxY <= b->minY || a->minY >= b->maxY ||
             a->maxZ <= b->minZ || a->minZ >= b->maxZ);
}

vec3i boxPushOut(const AABBi *a, const AABBi *b)
{
    int32 ax = i32_sub(b->maxX, a->minX);
    int32 bx = i32_sub(a->maxX, b->minX);
    int32 az = i32_sub(b->maxZ, a->minZ);
    int32 bz = i32_sub(a->maxZ, b->minZ);
    return vec3i_create((ax < bx) ? -ax : bx, 0, (az < bz) ? -az : bz);
}

void boxRotateYQ_c(AABBi *box, int32 quadrant)
{
    if (quadrant == 2)
        return;
    int32 minX = box->minX;
    int32 maxX = box->maxX;
    int32 minZ = box->minZ;
    int32 maxZ = box->maxZ;
    if (quadrant == 3)
    {
        box->minX = minZ;
        box->maxX = maxZ;
        box->minZ = -maxX;
        box->maxZ = -minX;
    }
    else if (quadrant == 1)
    {
        box->minX = -maxZ;
        box->maxX = -minZ;
        box->minZ = minX;
        box->maxZ = maxX;
    }
    else if (quadrant == 0)
    {
        box->minX = -maxX;
        box->maxX = -minX;
        box->minZ = -maxZ;
        box->maxZ = -minZ;
    }
}

void boxTranslate_c(AABBi *box, int32 x, int32 y, int32 z)
{
    (box->minX = i32_add(box->minX, x));
    (box->maxX = i32_add(box->maxX, x));
    (box->minY = i32_add(box->minY, y));
    (box->maxY = i32_add(box->maxY, y));
    (box->minZ = i32_add(box->minZ, z));
    (box->maxZ = i32_add(box->maxZ, z));
}

int32 fastLength(int32 dx, int32 dy)
{
    dx = abs(dx);
    dy = abs(dy);
    return (dx > dy) ? (i32_add(dx, dy >> 1)) : (i32_add(dy, dx >> 1));
}

int32 phd_atan(int32 x, int32 y)
{
    if (x == 0 && y == 0)
        return 0;
    int32 o = 0;
    if (x < 0)
    {
        (o = i32_add(o, 4));
        x = -x;
    }
    if (y < 0)
    {
        (o = i32_add(o, 2));
        y = -y;
    }
    if (y > x)
    {
        o++;
        swap_i32(&(x), &(y));
    }
    return abs(i32_add(atanTable[(i32_shl(y, 11)) / x], atanOctant[o]));
}

#if TR64_HAS_SEQUEL
/* tr2-camera.md, Exact signed bearing. For signed 32-bit components
 * (or the widened horizontal length), the ratio fits in uint64_t. */
int32 game_tr2_bearing(int64_t forward,int64_t side)
{
    unsigned octant=(forward<0 ? 4u : 0u) | (side<0 ? 2u : 0u);
    uint64_t major=forward<0 ? -forward : forward;
    uint64_t minor=side<0 ? -side : side;
    if (minor>major) {
        uint64_t tmp=major;
        major=minor;
        minor=tmp;
        ++octant;
    }
    return major ? abs(atanTable[(minor*2048)/major]+atanOctant[octant]) : 0;
}

uint32 game_tr2_horizontal_length(int32 x,int32 z)
{
    uint64_t squared=(uint64_t)((int64_t)x*x)+(uint64_t)((int64_t)z*z);
    if (squared<=UINT32_MAX)
        return phd_sqrt(squared);
    uint64_t root=0;
    for (uint64_t bit=UINT64_C(1)<<62;bit;bit>>=2) {
        if (squared>=root+bit) {
            squared-=root+bit;
            root=(root>>1)+bit;
        } else {
            root>>=1;
        }
    }
    return root;
}
#endif

uint32 phd_sqrt(uint32 x)
{
    uint32 m = 0x40000000;
    uint32 y = 0;
    uint32 z = 0;
    do
    {
        y += m;
        if (y > x)
        {
            y = z;
        }
        else
        {
            x -= y;
            y = z + m;
        }
        z = y >> 1;
    } while (m >>= 2);
    return y;
}

void CollisionInfo_setSide(CollisionInfo *self, CollisionInfo_SideType st, int32 floor,
                           int32 ceiling)
{
    SlantType slantType;
    if (((int8)((gs->gLastFloorSlant) & 0xFF)) == 0 && ((int8)((gs->gLastFloorSlant) >> 8)) == 0)
    {
        slantType = SLANT_NONE;
    }
    else if (abs(((int8)((gs->gLastFloorSlant) & 0xFF))) < 3 &&
             abs(((int8)((gs->gLastFloorSlant) >> 8))) < 3)
    {
        slantType = SLANT_LOW;
    }
    else
    {
        slantType = SLANT_HIGH;
    }
    if (st != CollisionInfo_ST_MIDDLE)
    {
        if (self->stopOnSlant && floor < 0 && slantType == SLANT_HIGH)
            floor = -0x7FFF;
        else if (self->stopOnSlant && floor > 0 && slantType == SLANT_HIGH
#if TR64_HAS_SEQUEL
                 && !self->allowSteepDrop
#endif
        )
            floor = 512;
    }
    CollisionInfo_Side *s;
    switch (st)
    {
    case CollisionInfo_ST_MIDDLE:
        s = &self->m;
        break;
    case CollisionInfo_ST_FRONT:
        s = &self->f;
        break;
    case CollisionInfo_ST_LEFT:
        s = &self->l;
        break;
    case CollisionInfo_ST_RIGHT:
        s = &self->r;
        break;
    default:
        sys_error("game collision side");
    }
    s->slantType = slantType;
    s->floor = floor;
    s->ceiling = ceiling;
}
