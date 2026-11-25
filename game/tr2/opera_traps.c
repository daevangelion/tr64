/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h and src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Tomb Raider II Opera House traps and mechanisms (docs/specs/wall/tr2-opera-house.md §§3-8),
 * paged in the `tr2_opera` code unit. */
#include "objects.h"
#include "game/common/object.h"

#if TR64_HAS_TR2

#define OPERA_OS __attribute__((optimize("Os")))

static OPERA_OS void moveItemRoom(ItemObj *item, Room *room)
{
    if (room && room != item->room) {
        Room_remove(item->room, item);
        Room_add(room, item);
    }
}

void OPERA_OS game_tr2_opera_init(void)
{
    GameTR2 *tr2 = gs->tr2;
    tr2->elevatorCount = 0;
    for (unsigned i = 0; i < TR2_ELEVATOR_SLOTS; ++i)
        tr2->elevators[i] = (TR2Elevator){ .itemIndex = -1, .present = 0 };
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        if (gs->items[i].type != TR2_ITEM_ELEVATOR)
            continue;
        if (tr2->elevatorCount >= TR2_ELEVATOR_SLOTS)
            sys_error("Tomb Raider II: elevator pool overflow (item=%u)", i);
        TR2Elevator *el = tr2->elevators + tr2->elevatorCount++;
        el->itemIndex = (int16_t)i;
        el->present = 1;
        el->yStart = gs->items[i].pos.y;
        el->waitCounter = 0;
    }
}

/* §3 Drawbridge (type 63): raised solid barrier (bounds-only push, no sphere test),
 * lowered non-solid with 256-unit slab floor/ceiling via dummy trigger. */
static OPERA_OS void drawbridgeUpdate(ItemObj *item)
{
    item->goalState = Object_isActive(item) ? 1 : 0;
    ItemObj_animProcess(item, true);
    Room *room = Room_getRoom(item->room, item->pos.x, item->pos.y, item->pos.z);
    moveItemRoom(item, room);
    vec3i lightPos = game_tr2_combat_joint(item, 0, vec3i_create(3000, 720, 0), 0);
    game_tr2_add_dyn_light(lightPos, 14, 11);
    if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        ItemObj_deactivateBase(item);
        item->flags |= ITEM_FLAG_ONCE;
    }
}

static OPERA_OS uint32 drawbridgeContact(ItemObj *item, Lara *lara)
{
    if (item->state != 0)
        return TR2_INTERACT_NONE;
    CollisionInfo *c = &gs->cinfo;
    AABBs box = *ItemObj_getBoundingBox(item, false);
    AABBs body = *ItemObj_getBoundingBox(lara, false);
    vec3i d = vec3i_sub(lara->pos, item->pos);
    if (box.maxY - body.minY <= d.y || box.minY - body.maxY >= d.y)
        return TR2_INTERACT_NONE;
    int32 s = tr2_sin(item->angle.y), cs = tr2_sin(item->angle.y + ANGLE_90);
    int32 x = q14((int64_t)cs * d.x - (int64_t)s * d.z);
    int32 z = q14((int64_t)s * d.x + (int64_t)cs * d.z);
    int32 lowerX = box.minX - c->radius, upperX = box.maxX + c->radius;
    int32 lowerZ = box.minZ - c->radius, upperZ = box.maxZ + c->radius;
    if (x < lowerX || x > upperX || z < lowerZ || z > upperZ)
        return TR2_INTERACT_NONE;
    if (!c->enemyPush)
        return TR2_INTERACT_NONE;

    bool bump = item->goalState != item->state && c->enemyHit && (box.maxY - box.minY) > 256;
    if (bump) {
        int32 midX = (box.minX + box.maxX) >> 1, midZ = (box.minZ + box.maxZ) >> 1;
        unsigned bearing = game_tr2_bearing(d.z - q14((int64_t)cs * midZ - (int64_t)s * midX),
                                            d.x - q14((int64_t)cs * midX + (int64_t)s * midZ));
        lara->extraL->hitQuadrant = (uint16)(lara->angle.y - bearing + 40960u) >> 14;
    }

    int32 dx0 = x - lowerX, dx1 = upperX - x, dz1 = upperZ - z, dz0 = z - lowerZ;
    if (dx0 <= dx1 && dx0 <= dz1 && dx0 <= dz0) x = lowerX;
    else if (dx1 <= dz1 && dx1 <= dz0) x = upperX;
    else if (dz1 <= dz0) z = upperZ;
    else z = lowerZ;
    lara->pos.x = i32_add(item->pos.x, q14((int64_t)cs * x + (int64_t)s * z));
    lara->pos.z = i32_add(item->pos.z, q14((int64_t)cs * z - (int64_t)s * x));
    int32 gapPos = c->gapPos, gapNeg = c->gapNeg, gapCeiling = c->gapCeiling;
    int16 angle = c->angle;
    c->gapPos = -WALL;
    c->gapNeg = -LARA_STEP_HEIGHT;
    c->gapCeiling = 0;
    CollisionInfo_setAngle(c, wrap_i16(game_tr2_bearing((int64_t)lara->pos.z - c->pos.z,
                                                        (int64_t)lara->pos.x - c->pos.x)));
    ItemObj_collideRoom(lara, LARA_HEIGHT, 0);
    CollisionInfo_setAngle(c, angle);
    c->gapPos = gapPos; c->gapNeg = gapNeg; c->gapCeiling = gapCeiling;
    if (c->type != CT_NONE) {
        lara->pos.x = c->pos.x;
        lara->pos.z = c->pos.z;
    } else {
        c->pos = lara->pos;
        Room *room = Room_getRoom(lara->room, lara->pos.x, i32_sub(lara->pos.y, 10), lara->pos.z);
        moveItemRoom(lara, room);
    }
    return bump ? TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
}

/* §4 Elevator (type 65): fixed-endpoint vertical travel at 16 units/tick with 90-tick pause. */
static OPERA_OS TR2Elevator *findElevator(const ItemObj *item)
{
    int16_t slot = (int16_t)(item - gs->items);
    for (unsigned i = 0; i < gs->tr2->elevatorCount; ++i)
        if (gs->tr2->elevators[i].present && gs->tr2->elevators[i].itemIndex == slot)
            return gs->tr2->elevators + i;
    return NULL;
}

static OPERA_OS void elevatorUpdate(ItemObj *item)
{
    TR2Elevator *el = findElevator(item);
    if (!el)
        return;
    if (Object_isActive(item)) {
        if (item->pos.y < i32_add(el->yStart, 5616)) {
            if (el->waitCounter < 90) {
                item->goalState = 1;
                ++el->waitCounter;
            } else {
                item->goalState = 0;
                item->pos.y = i32_add(item->pos.y, 16);
            }
        } else {
            item->goalState = 1;
            el->waitCounter = 0;
        }
    } else {
        if (item->pos.y > i32_add(el->yStart, 16)) {
            if (el->waitCounter < 90) {
                item->goalState = 1;
                ++el->waitCounter;
            } else {
                item->goalState = 0;
                item->pos.y = i32_sub(item->pos.y, 16);
            }
        } else {
            item->goalState = 1;
            el->waitCounter = 0;
        }
    }
    ItemObj_animProcess(item, true);
    Room *room = Room_getRoom(item->room, item->pos.x, item->pos.y, item->pos.z);
    moveItemRoom(item, room);
}

/* §5 Swinging pendulum hazards (types 58 and 96). */
static OPERA_OS void pendulumUpdate(ItemObj *item, Lara *lara)
{
    if (item->hitMask) {
        Lara_hit(lara, 50, &item->pos, 0);
        lara->flags |= ITEM_FLAG_INJURED;
        int32 r1 = rand_logic();
        int32 r2 = rand_logic();
        int32 r3 = rand_logic();
        int32 r4 = rand_logic();
        vec3i pt = vec3i_create(
            i32_add(lara->pos.x, (r1 - 16384) / 256),
            i32_sub(lara->pos.y, r3 / 44),
            i32_add(lara->pos.z, (r2 - 16384) / 256));
        int16 yaw = wrap_i16((int32)lara->angle.y + (r4 - 16384) / 8);
        game_tr2_shot_effect(TR2_FX_BLOOD, lara->room, pt, lara->hSpeed, yaw);
    }
    item->hitMask = 0;
    const FloorData *savedFd = gs->gLastFloorData;
    uint16 savedSlant = gs->gLastFloorSlant;
    const Sector *sec = Room_getSector(item->room, item->pos.x, item->pos.z);
    item->roomFloor = (int16)Sector_getFloor(sec, item->pos.x, item->pos.y, item->pos.z);
    gs->gLastFloorData = savedFd;
    gs->gLastFloorSlant = savedSlant;
    ItemObj_animProcess(item, true);
}

/* Shared trap collision for Falling Sandbag (79) and Spinning Fans (76, 95). */
static OPERA_OS uint32 trapContact(ItemObj *item, Lara *lara)
{
    unsigned status = item->flags & ITEM_FLAG_STATUS;
    if (status == ITEM_FLAG_STATUS_ACTIVE) {
        if (ItemObj_collideBounds(item, lara, &gs->cinfo))
            item->hitMask = ItemObj_collideSpheres(item, lara);
        return TR2_INTERACT_NONE;
    }
    if (status != ITEM_FLAG_STATUS_INVISIBLE)
        return game_tr2_solid_contact(item, lara, true, false) == TR2_SOLID_BUMP ?
               TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
    return TR2_INTERACT_NONE;
}

/* §6 Falling sandbag / ceiling (type 79). */
static OPERA_OS void fallingCeilingUpdate(ItemObj *item, Lara *lara)
{
    if (item->state == 0) {
        item->goalState = 1;
        item->flags |= ITEM_FLAG_GRAVITY;
    } else if (item->state == 1 && item->hitMask) {
        Lara_hit(lara, 300, &item->pos, 0);
        lara->flags |= ITEM_FLAG_INJURED;
    }
    item->hitMask = 0;
    ItemObj_animProcess(item, true);
    if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        ItemObj_deactivateBase(item);
        return;
    }
    const FloorData *savedFd = gs->gLastFloorData;
    uint16 savedSlant = gs->gLastFloorSlant;
    Room *room = item->room;
    const Sector *sec = game_tr2_room_sector(&room, item->pos.x, item->pos.y, item->pos.z);
    moveItemRoom(item, room);
    int32 floor = Sector_getFloor(sec, item->pos.x, item->pos.y, item->pos.z);
    item->roomFloor = (int16)floor;
    gs->gLastFloorData = savedFd;
    gs->gLastFloorSlant = savedSlant;
    if (item->state == 1 && item->pos.y >= floor) {
        item->goalState = 2;
        item->pos.y = floor;
        item->vSpeed = 0;
        item->flags &= ~ITEM_FLAG_GRAVITY;
    }
}

/* §7 Spinning blade / duct fans (types 76 and 95, no wind force). */
static OPERA_OS void fanUpdate(ItemObj *item, Lara *lara)
{
    if (!(item->flags & ITEM_FLAG_ONCE) && Object_isActive(item)) {
        item->goalState = 0;
        if (item->hitMask & 0x06u) {
            Lara_hit(lara, 200, &item->pos, 0);
            lara->flags |= ITEM_FLAG_INJURED;
            vec3i pt = vec3i_create(lara->pos.x, i32_sub(lara->pos.y, 512), lara->pos.z);
            int16 yaw = wrap_i16((int32)item->angle.y + ANGLE_90);
            for (unsigned i = 0; i < 3u; ++i)
                game_tr2_shot_effect(TR2_FX_BLOOD, lara->room, pt,
                                     (int16)(rand_draw() / 1024), yaw);
        } else {
            soundPlay(item->type == TR2_ITEM_FAN_PROP ? 213 : 217, &item->pos);
        }
    } else if (item->goalState != 1) {
        if (item->type == TR2_ITEM_FAN_PROP)
            soundPlay(213, &item->pos);
        item->goalState = 1;
    }
    item->hitMask = 0;
    ItemObj_animProcess(item, true);
    if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        ItemObj_deactivateBase(item);
        item->flags &= ~ITEM_FLAG_COLLISION;
    }
}

/* §8 Multi-boulder / rolling crates (type 83). */
static OPERA_OS uint32 rollingCrateUpdate(ItemObj *item)
{
    unsigned status = item->flags & ITEM_FLAG_STATUS;
    if (status == ITEM_FLAG_STATUS_ACTIVE) {
        if (item->goalState == 2) {
            item->hitMask = 0;
            ItemObj_animProcess(item, true);
            return 0;
        }
        if (item->pos.y < item->roomFloor && !(item->flags & ITEM_FLAG_GRAVITY)) {
            item->flags |= ITEM_FLAG_GRAVITY;
            item->vSpeed = -10;
        } else if (item->state == 0) {
            item->goalState = 1;
        }
        int32 x0 = item->pos.x, z0 = item->pos.z;
        Room *oldRoom = item->room;
        item->hitMask = 0;
        ItemObj_animProcess(item, true);
        Room *room = item->room;
        const Sector *sec = game_tr2_room_sector(&room, item->pos.x, item->pos.y, item->pos.z);
        moveItemRoom(item, room);
        int32 floor = Sector_getFloor(sec, item->pos.x, item->pos.y, item->pos.z);
        item->roomFloor = (int16)floor;
        const FloorData *heavy = gs->gLastFloorData;
        if (item->pos.y >= i32_sub(floor, 256)) {
            item->flags &= ~ITEM_FLAG_GRAVITY;
            item->pos.y = floor;
            item->vSpeed = 0;
            soundPlay(222, &item->pos);
            const vec3i *eye = &gs->playersExtra[0].camera.view.pos;
            uint32 dx = (uint32)eye->x - (uint32)item->pos.x;
            uint32 dz = (uint32)eye->z - (uint32)item->pos.z;
            uint32 d = phd_sqrt(dx * dx + dz * dz);
            if (d < 10240u)
                gs->playersExtra[0].camera.bounce = -(int32)(40u * (10240u - d) / 10240u);
        }
        const FloorData *savedFd = gs->gLastFloorData;
        uint16 savedSlant = gs->gLastFloorSlant;
        vec3i ahead = vec3i_add(item->pos, game_tr2_heading_step(item->angle.y, 1024));
        Room *aheadRoom = item->room;
        const Sector *aheadSec = game_tr2_room_sector(&aheadRoom, ahead.x, ahead.y, ahead.z);
        int32 h = Sector_getFloor(aheadSec, ahead.x, ahead.y, ahead.z);
        gs->gLastFloorData = savedFd;
        gs->gLastFloorSlant = savedSlant;
        if (h < item->pos.y) {
            soundPlay(223, &item->pos);
            item->goalState = 2;
            item->pos.y = floor;
            item->pos.x = x0;
            item->pos.z = z0;
            moveItemRoom(item, oldRoom);
            item->hSpeed = 0;
            item->vSpeed = 0;
            item->hitMask = 0;
        }
        return heavy ? 1u + (uint32)(heavy - gs->level.floors) : 0u;
    }
    if (status == ITEM_FLAG_STATUS_INACTIVE && !Object_isActive(item)) {
        unsigned slot = (unsigned)(item - gs->items);
        if (slot < gs->level.itemsCount) {
            const tr64_core_items_t *src = tr64_level_items(gs->core, slot);
            Room *initRoom = gs->rooms + tr64_u16(src->room);
            moveItemRoom(item, initRoom);
            item->pos = vec3i_create(tr64_i32(src->x), tr64_i32(src->y), tr64_i32(src->z));
            item->roomFloor = Room_getSector(initRoom, item->pos.x, item->pos.z)->floor * 256;
        }
        ItemObj_animSet(item, gs->level.models[item->type].animIndex, true, 0);
        item->nextState = 0;
        item->hSpeed = 0;
        item->vSpeed = 0;
        item->flags &= ~(ITEM_FLAG_STATUS | ITEM_FLAG_GRAVITY);
        ItemObj_deactivateBase(item);
    }
    return 0;
}

static OPERA_OS void rollingCrateContact(ItemObj *item, Lara *lara)
{
    const bool active = (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE;
    const bool airborne = (lara->flags & ITEM_FLAG_GRAVITY) != 0;
    if (game_tr2_solid_contact(item, lara, !active || airborne, false) == TR2_SOLID_MISS || !active)
        return;
    if (airborne) {
        Lara_hit(lara, 100, &item->pos, 0);
        vec3i d = vec3i_sub(lara->pos, item->pos);
        d.y = i32_add(d.y, 162);
        uint32 sq = (uint32)d.x * d.x + (uint32)d.y * d.y + (uint32)d.z * d.z;
        int32 dist = phd_sqrt(sq);
        if (dist < 512)
            dist = 512;
        vec3i pt = vec3i_add(item->pos, vec3i_create(d.x * 512 / dist,
                             i32_sub(d.y * 512 / dist, 512), d.z * 512 / dist));
        game_tr2_shot_effect(TR2_FX_BLOOD, item->room, pt, item->hSpeed, item->angle.y);
        return;
    }
    if (lara->health <= 0)
        return;
    lara->health = -1;
    lara->flags |= ITEM_FLAG_INJURED;
    lara->angle.x = lara->angle.z = 0;
    lara->angle.y = item->angle.y;
    ItemObj_animSet(lara, Lara_ANIM_DEATH_BOULDER, true, 0);
    Camera *cam = &lara->extraL->camera;
    cam->targetAngle.y = 30940;
    cam->targetAngle.x = -4550;
    cam->center = true;
    for (unsigned i = 0; i < 15u; ++i) {
        vec3i pt = lara->pos;
        pt.x = i32_add(pt.x, (rand_logic() - 16384) / 256);
        pt.z = i32_add(pt.z, (rand_logic() - 16384) / 256);
        pt.y = i32_sub(pt.y, rand_logic() >> 6);
        int16 yaw = wrap_i16((int32)item->angle.y + (rand_logic() - 16384) / 8);
        game_tr2_shot_effect(TR2_FX_BLOOD, item->room, pt, wrap_i16(2 * item->hSpeed), yaw);
    }
}

uint32 OPERA_OS game_tr2_opera_update(ItemObj *item, Lara *lara)
{
    switch (item->type) {
    case TR2_ITEM_DRAWBRIDGE:
        drawbridgeUpdate(item);
        return 0;
    case TR2_ITEM_ELEVATOR:
        elevatorUpdate(item);
        return 0;
    case TR2_ITEM_SWING_SANDBAG:
    case TR2_ITEM_SWING_CRATE:
        pendulumUpdate(item, lara);
        return 0;
    case TR2_ITEM_FALLING_CEILING:
        fallingCeilingUpdate(item, lara);
        return 0;
    case TR2_ITEM_FAN_PROP:
    case TR2_ITEM_FAN_AIR:
        fanUpdate(item, lara);
        return 0;
    case TR2_ITEM_ROLLING_CRATE:
        return rollingCrateUpdate(item);
    default:
        sys_error("Tomb Raider II: no opera controller for type %u", (unsigned)item->type);
    }
    return 0;
}

uint32 OPERA_OS game_tr2_opera_collide(ItemObj *item, Lara *lara)
{
    switch (item->type) {
    case TR2_ITEM_DRAWBRIDGE:
        return drawbridgeContact(item, lara);
    case TR2_ITEM_ELEVATOR:
        return TR2_INTERACT_NONE;
    case TR2_ITEM_SWING_SANDBAG:
    case TR2_ITEM_SWING_CRATE:
        return game_tr2_solid_contact(item, lara, true, false) == TR2_SOLID_BUMP ?
               TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
    case TR2_ITEM_FALLING_CEILING:
    case TR2_ITEM_FAN_PROP:
    case TR2_ITEM_FAN_AIR:
        return trapContact(item, lara);
    case TR2_ITEM_ROLLING_CRATE:
        rollingCrateContact(item, lara);
        return TR2_INTERACT_NONE;
    default:
        return TR2_INTERACT_NONE;
    }
}

#endif
