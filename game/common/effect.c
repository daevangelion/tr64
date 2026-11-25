/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Source object.h:125-647,652-690; item.h:669-705. */
#include "internal.h"
#include <limits.h>

void Bullet_setAngle(ItemObj *self, int16 angleX, int16 angleY);
void game_init_item(ItemObj *self, Room *room);

bool game_effect_init(ItemObj *self)
{
    switch (self->type)
    {
    case ITEM_EXPLOSION:
    case ITEM_SPLASH:
    case ITEM_BLOOD:
    case ITEM_SMOKE:
    case ITEM_RICOCHET:
    case ITEM_SPARKLES:
        self->animIndex = self->timer = self->hSpeed = self->frameIndex = 0;
        ItemObj_activate(self);
        if (self->type == ITEM_EXPLOSION)
        {
            self->timer = 3;
            self->intensity = 255;
            self->flags |= ITEM_FLAG_ANIMATED;
            self->flags &= ~ITEM_FLAG_COLLISION;
        }
        return true;
    case ITEM_BUBBLE:
        soundPlay(SND_BUBBLE, &self->pos);
        self->frameIndex = rand_draw() % (-gs->level.models[self->type].count);
        self->vSpeed = -(10 + rand_draw() % 6);
        self->angle = vec3s_create(0, 0, ANGLE_90);
        ItemObj_activate(self);
        self->roomFloor = ItemObj_getWaterLevel(self);
        return true;
    case ITEM_MUZZLE_FLASH:
        self->angle = vec3s_create(0, 0, 0);
        self->extraL = NULL;
        self->hSpeed = 0;
        self->hitMask = 0;
        self->timer = 3;
        self->intensity = 255;
        self->flags &= ~ITEM_FLAG_COLLISION;
        ItemObj_activate(self);
        return true;
    case ITEM_NATLA_BULLET:
    case ITEM_MUTANT_BULLET:
    case ITEM_CENTAUR_BULLET:
        self->flags |= ITEM_FLAG_COLLISION;
        self->timer = 0;
        Bullet_setAngle(self, 0, self->angle.y);
        ItemObj_activate(self);
        return true;
    default:
        return false;
    }
}

void ItemObj_fxRicochet(Room *room, const vec3i *pos, bool sound)
{
    ItemObj *ricochet = ItemObj_add(ITEM_RICOCHET, room, pos, 0);
    if (!ricochet)
        return;
    ricochet->timer = 4;
    ricochet->frameIndex = rand_draw() % (-gs->level.models[ricochet->type].count);
    if (sound)
        soundPlay(SND_RICOCHET, &ricochet->pos);
}

void ItemObj_fxBlood(ItemObj *self, const vec3i *pos, int16 angleY, int16 speed)
{
    ItemObj *blood = ItemObj_add(ITEM_BLOOD, self->room, pos, angleY);
    if (!blood)
        return;
    blood->hSpeed = speed;
    blood->timer = 4;
    blood->flags |= ITEM_FLAG_ANIMATED;
}

void ItemObj_fxSmoke(ItemObj *self, const vec3i *pos)
{
    ItemObj *smoke = ItemObj_add(ITEM_SMOKE, self->room, pos, 0);
    if (!smoke)
        return;
    smoke->timer = 3;
    smoke->flags |= ITEM_FLAG_ANIMATED;
}

ItemObj *reserveExplosionSlot(void)
{
    ItemObj *item = gs->firstFree;
    if (item)
        gs->firstFree = item->nextItem;
    return item;
}

ItemObj *initExplosionSlot(ItemObj *item, Room *room, const vec3i *pos, int32 angleY)
{
    ASSERT(item && room);
    ItemObj_resetSlot(item); item->type = ITEM_EXPLOSION;
    item->pos = *pos;
    item->angle.y = wrap_i16((uint32)angleY);
    item->intensity = 128;
    game_init_item(item, room);
    soundPlay(SND_EXPLOSION, &item->pos);
    return item;
}

ItemObj *addExplosion(Room *room, const vec3i *pos, int32 angleY)
{
    ItemObj *item = reserveExplosionSlot();
    return item ? initExplosionSlot(item, room, pos, angleY) : NULL;
}

static ItemObj *flashOwner(const ItemObj *self)
{
    return (ItemObj *)self->extraL;
}

bool MuzzleFlash_ownerIsAlive(const ItemObj *self)
{
    ItemObj *owner = flashOwner(self);
    if (!owner || owner->type != self->hitMask || !(owner->flags & ITEM_FLAG_ACTIVE) || !owner->room)
        return false;
    bool inRoom = false;
    for (ItemObj *item = owner->room->firstItem; item; item = item->nextItem)
        if (item == owner)
        {
            inRoom = true;
            break;
        }
    if (!inRoom)
        return false;
    for (ItemObj *item = gs->firstActive; item; item = item->nextActive)
        if (item == owner)
            return true;
    return false;
}

static void MuzzleFlash_followOwner(ItemObj *self)
{
    ItemObj *owner = flashOwner(self);
    vec3i offset = vec3i_create(self->angle.x, self->angle.y, self->angle.z + 150);
    self->pos = vec3i_add(owner->pos, ItemObj_getJoint(owner, self->hSpeed, &offset));
    Room *next = Room_getRoom(owner->room, self->pos.x, self->pos.y, self->pos.z);
    if (next != self->room)
    {
        Room_remove(self->room, self);
        Room_add(next, self);
    }
}

ItemObj *MuzzleFlash_add(ItemObj *owner, int32 joint, const vec3i *offset)
{
    if (!owner)
        return NULL;
    ASSERT(joint >= 0 && joint <= INT16_MAX);
    ASSERT(offset->x >= INT16_MIN && offset->x <= INT16_MAX);
    ASSERT(offset->y >= INT16_MIN && offset->y <= INT16_MAX);
    ASSERT(offset->z >= INT16_MIN && offset->z <= INT16_MAX);
    ItemObj *flash = ItemObj_add(ITEM_MUZZLE_FLASH, owner->room, offset, 0);
    if (!flash)
        return NULL;
    flash->extraL = (ExtraInfoLara *)owner;
    flash->hSpeed = (int16)joint;
    flash->hitMask = owner->type;
    flash->angle = vec3s_create((int16)offset->x, (int16)offset->y, (int16)offset->z);
    MuzzleFlash_followOwner(flash);
    return flash;
}

static void MuzzleFlash_update(ItemObj *self)
{
    if (!MuzzleFlash_ownerIsAlive(self) || --self->timer <= 0)
    {
        ItemObj_remove(self);
        return;
    }
    self->intensity = self->timer == 2 ? 255 : 170;
    MuzzleFlash_followOwner(self);
}

void Bullet_setAngle(ItemObj *self, int16 angleX, int16 angleY)
{
    self->angle.x = angleX;
    self->angle.y = angleY;
    int32 sx, cx;
    sincos(angleX, sx, cx);
    int32 speed = self->type == ITEM_NATLA_BULLET ? 220 :
                  self->type == ITEM_MUTANT_BULLET ? 250 :
                  self->type == ITEM_CENTAUR_BULLET ? 220 : 200;
    self->hSpeed = (int16)((cx * speed) >> 14);
    self->vSpeed = (int16)(-((sx * speed) >> 14));
}

static void Bullet_moveToRoom(ItemObj *self, Room *next)
{
    if (next != self->room)
    {
        Room_remove(self->room, self);
        Room_add(next, self);
    }
}

static uint64_t sqrt64(uint64_t value)
{
    uint64_t result = 0, bit = (uint64_t)1 << 62;
    while (bit > value)
        bit >>= 2;
    while (bit)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
            result >>= 1;
        bit >>= 2;
    }
    return result;
}

static bool segmentSphereEntry(const vec3i *start, const vec3i *end, const vec3i *center,
                              int32 radius, int64_t *root, int64_t *denominator)
{
    vec3i step = vec3i_sub(*end, *start), origin = vec3i_sub(*start, *center);
    int64_t a = (int64_t)step.x * step.x + (int64_t)step.y * step.y + (int64_t)step.z * step.z;
    int64_t c = (int64_t)origin.x * origin.x + (int64_t)origin.y * origin.y +
                (int64_t)origin.z * origin.z - (int64_t)radius * radius;
    *denominator = a;
    if (c <= 0)
    {
        *root = 0;
        return true;
    }
    if (!a)
        return false;
    int64_t b = (int64_t)origin.x * step.x + (int64_t)origin.y * step.y + (int64_t)origin.z * step.z;
    int64_t discriminant = b * b - a * c;
    if (discriminant < 0)
        return false;
    *root = -b - (int64_t)sqrt64((uint64_t)discriminant);
    return *root >= 0 && *root <= a;
}

static Lara *Bullet_sweepLara(const vec3i *start, const vec3i *end, vec3i *hitPos)
{
    Lara *result = NULL;
    int64_t bestRoot = 0, denominator = 0;
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
        Lara *lara=gs->players[slot];
        if (!lara)break;
        if (gs->players[1] && lara->health<=0)continue;
        Sphere spheres[32];
        int32 count = ItemObj_getSpheres(lara, spheres, true);
        for (int32 i = 0; i < count; ++i)
        {
            if (spheres[i].radius <= 0)
                continue;
            int64_t root, sphereDenominator;
            if (!segmentSphereEntry(start, end, &spheres[i].center, spheres[i].radius,
                                    &root, &sphereDenominator))
                continue;
            if (!result || root < bestRoot)
            {
                result = lara;
                bestRoot = root;
                denominator = sphereDenominator;
            }
        }
    }
    if (result)
    {
        vec3i step = vec3i_sub(*end, *start);
        *hitPos = *start;
        if (denominator)
        {
            hitPos->x = i32_add(hitPos->x, (int32)((int64_t)step.x * bestRoot / denominator));
            hitPos->y = i32_add(hitPos->y, (int32)((int64_t)step.y * bestRoot / denominator));
            hitPos->z = i32_add(hitPos->z, (int32)((int64_t)step.z * bestRoot / denominator));
        }
    }
    return result;
}

static void Bullet_impact(ItemObj *self, Lara *lara, bool directHit)
{
    if (self->type == ITEM_CENTAUR_BULLET)
    {
        if (directHit)
            Lara_hit(lara, 100, &self->pos, 0);
        else
        {
            for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
                Lara *target=gs->players[slot];
                if (!target)break;
                vec3i d = vec3i_sub(self->pos, target->pos);
                if (abs(d.x) < 1024 && abs(d.y) < 1024 && abs(d.z) < 1024)
                {
                    int32 range = 1024 * 1024;
                    int32 dist = i32_add(i32_add(i32_mul(d.x, d.x), i32_mul(d.y, d.y)),
                                          i32_mul(d.z, d.z));
                    if (dist < range)
                        Lara_hit(target, 100 * (range - dist) / range, &self->pos, 0);
                }
            }
        }
        addExplosion(self->room, &self->pos, 0);
    }
    else if (self->type == ITEM_MUTANT_BULLET)
    {
        if (directHit)
            Lara_hit(lara, 30, &self->pos, 0);
        else
        {
            int32 sx, cx, sy, cy;
            sincos(self->angle.x, sx, cx);
            sincos(self->angle.y, sy, cy);
            vec3i ricochet = self->pos;
            ricochet.x = i32_sub(ricochet.x, ((sy * cx >> 14) * 64) >> 14);
            ricochet.y = i32_add(ricochet.y, sx * 64 >> 14);
            ricochet.z = i32_sub(ricochet.z, ((cy * cx >> 14) * 64) >> 14);
            ItemObj_fxRicochet(self->room, &ricochet, true);
        }
    }
    if (directHit)
        soundPlay(lara->waterState == WATER_STATE_UNDER ? SND_HIT_UNDERWATER : SND_HIT, &lara->pos);
    ItemObj_remove(self);
}

void Bullet_collide(ItemObj *self, Lara *lara, CollisionInfo *cinfo)
{
    (void)cinfo;
    if (lara && ItemObj_collideSpheres(self, lara))
        Bullet_impact(self, lara, true);
}

static void Bullet_update(ItemObj *self)
{
    vec3i oldPos = self->pos;
    Room *oldRoom = self->room;
    int32 s, c;
    sincos(self->angle.y, s, c);
    self->pos.x = i32_add(self->pos.x, (s * self->hSpeed) >> 14);
    self->pos.y = i32_add(self->pos.y, self->vSpeed);
    self->pos.z = i32_add(self->pos.z, (c * self->hSpeed) >> 14);
    Location from = {oldRoom, oldPos}, to = {self->room, self->pos};
    bool clear = trace(&from, &to, true);
    vec3i hitPos;
    Lara *lara = Bullet_sweepLara(&oldPos, &to.pos, &hitPos);
    if (lara)
    {
        self->pos = hitPos;
        Bullet_moveToRoom(self, Room_getRoom(oldRoom, self->pos.x, self->pos.y, self->pos.z));
        Bullet_impact(self, lara, true);
        return;
    }
    self->pos = to.pos;
    Bullet_moveToRoom(self, to.room);
    if (!clear)
    {
        Bullet_impact(self, NULL, false);
        return;
    }
    const Sector *sector = Room_getSector(self->room, self->pos.x, self->pos.z);
    self->roomFloor = Sector_getFloor(sector, self->pos.x, self->pos.y, self->pos.z);
    self->timer = wrap_i16((uint32)self->timer + 1);
}

bool game_effect_update(ItemObj *self)
{
    switch (self->type)
    {
    case ITEM_EXPLOSION:
    case ITEM_SPLASH:
    case ITEM_BLOOD:
    case ITEM_SMOKE:
    case ITEM_RICOCHET:
    case ITEM_SPARKLES:
        SpriteEffect_update(self);
        return true;
    case ITEM_BUBBLE:
        Bubble_update(self);
        return true;
    case ITEM_MUZZLE_FLASH:
        MuzzleFlash_update(self);
        return true;
    case ITEM_NATLA_BULLET:
    case ITEM_MUTANT_BULLET:
    case ITEM_CENTAUR_BULLET:
        Bullet_update(self);
        return true;
    default:
        return false;
    }
}
