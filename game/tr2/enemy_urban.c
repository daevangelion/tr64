/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "enemy_urban.h"
#include "boat.h"
#include "objects.h"
#include "../object.h"

#if TR64_HAS_TR2

static int16 trackHead(int16 current, int16 target)
{
    int16 next = angleLerp(current, target, 910);
    return X_MAX(-12288, X_MIN(12288, next));
}

/* tr2-venice-creatures.md, Doberman (15). */
enum {
    DOG_WALK = 1, DOG_RUN = 2, DOG_REST = 3,
    DOG_IDLE4 = 4, DOG_IDLE5 = 5, DOG_IDLE6 = 6,
    DOG_BITE = 7, DOG_LUNGE = 8, DOG_EXT_BITE = 9, DOG_DIE = 10,
    DOG_DEATH_CLIP = 13
};
static const uint32 DOG_BITE_MASK = UINT32_C(0x013f7000); /* meshes 12-14, 16-21, 24 */

void game_tr2_doberman_control(TR2Creature *ai, Lara *lara, const Interest *p)
{
    ItemObj *item = ai->item;
    switch (item->state) {
    case DOG_WALK:
        ai->maxTurn = 546;
        if (ai->mood != BORED) {
            item->goalState = DOG_RUN;
        } else {
            int r = rand_logic();
            if (r < 768) {
                item->nextState = DOG_IDLE4;
                item->goalState = DOG_REST;
            } else if (r < 1536) {
                item->nextState = DOG_IDLE5;
                item->goalState = DOG_REST;
            } else if (r < 2816) {
                item->goalState = DOG_REST;
            }
        }
        break;
    case DOG_RUN:
        ai->maxTurn = 1092;
        if (ai->mood == BORED)
            item->goalState = DOG_REST;
        else if (p->distance < 768u * 768u)
            item->goalState = DOG_LUNGE;
        break;
    case DOG_REST:
        ai->maxTurn = 0;
        ai->flags = 0;
        if (ai->mood == BORED) {
            if (item->nextState)
                item->goalState = item->nextState;
            else {
                int r = rand_logic();
                if (r < 768) item->goalState = DOG_IDLE4;
                else if (r < 1536) item->goalState = DOG_IDLE5;
                else if (r < 9728) item->goalState = DOG_WALK;
            }
        } else if (ai->mood == ESCAPE) {
            item->goalState = DOG_RUN;
        } else if (p->ahead && p->distance < 341u * 341u) {
            item->goalState = DOG_BITE;
        } else {
            item->goalState = DOG_RUN;
        }
        break;
    case DOG_IDLE4:
        if (ai->mood != BORED || rand_logic() < 1280)
            item->goalState = DOG_REST;
        break;
    case DOG_IDLE5:
        if (ai->mood != BORED || rand_logic() < 256)
            item->goalState = DOG_REST;
        break;
    case DOG_IDLE6:
        if (ai->mood != BORED || rand_logic() < 512)
            item->goalState = DOG_REST;
        break;
    case DOG_BITE:
        if (ai->flags != DOG_BITE && p->ahead && (item->hitMask & DOG_BITE_MASK)) {
            if (lara->health > 0) {
                lara->health -= 100;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            ai->flags = DOG_BITE;
        }
        if (p->distance > 341u * 341u && p->distance < 682u * 682u)
            item->goalState = DOG_EXT_BITE;
        else
            item->goalState = DOG_REST;
        break;
    case DOG_LUNGE:
        if (ai->flags != DOG_LUNGE && (item->hitMask & DOG_BITE_MASK)) {
            if (lara->health > 0) {
                lara->health -= 200;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            ai->flags = DOG_LUNGE;
        }
        if (p->distance < 341u * 341u)
            item->goalState = DOG_BITE;
        else if (p->distance < 682u * 682u)
            item->goalState = DOG_EXT_BITE;
        break;
    case DOG_EXT_BITE:
        ai->maxTurn = 1092;
        if (ai->flags != DOG_EXT_BITE && (item->hitMask & DOG_BITE_MASK)) {
            if (lara->health > 0) {
                lara->health -= 100;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            ai->flags = DOG_EXT_BITE;
        }
        if (p->distance < 341u * 341u)
            item->goalState = DOG_BITE;
        break;
    default:
        sys_error("Tomb Raider II: Doberman state %u unknown", item->state);
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], p->ahead ? p->yaw : 0);
}

void game_tr2_doberman_death(TR2Creature *ai)
{
    ItemObj *item = ai->item;
    if (item->state != DOG_DIE) {
        ItemObj_animSet(item, gs->level.models[item->type].animIndex + DOG_DEATH_CLIP, false, 0);
        item->state = DOG_DIE;
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], 0);
}

/* tr2-venice-creatures.md, Masked pistol gunmen (16, 17, 18). */
enum {
    GUN_WALK = 1, GUN_RUN = 2, GUN_IDLE = 3, GUN_WAIT = 4, GUN_CROUCH = 5,
    GUN_AIM1 = 6, GUN_FIRE1 = 7, GUN_MOVE_FIRE = 9,
    GUN_AIM2 = 10, GUN_FIRE2 = 11, GUN_DIE1 = 12, GUN_DIE2 = 13,
    GUN_DEATH_CLIP1 = 20, GUN_DEATH_CLIP2 = 21
};

static bool gunmanLineClear(const ItemObj *item, const Lara *lara, Location *fromOut, Location *toOut)
{
    Location from = {
        .room = item->room,
        .pos = vec3i_create(item->pos.x, i32_sub(item->pos.y, 768), item->pos.z)
    };
    Location to = {
        .room = lara->room,
        .pos = vec3i_create(lara->pos.x, i32_sub(lara->pos.y, 768), lara->pos.z)
    };
    const FloorData *fd = gs->gLastFloorData;
    uint16 slant = gs->gLastFloorSlant;
    bool clear = game_tr2_sight(&from, &to, true);
    gs->gLastFloorData = fd;
    gs->gLastFloorSlant = slant;
    if (fromOut) *fromOut = from;
    if (toOut) *toOut = to;
    return clear;
}

static bool gunmanCanShoot(const ItemObj *item, const Lara *lara, const Interest *p)
{
    if (lara->health <= 0 || !p->ahead || p->distance >= UINT64_C(8192) * 8192)
        return false;
    return gunmanLineClear(item, lara, NULL, NULL);
}

static bool gunmanShot(TR2Creature *ai, Lara *lara, const Interest *p, bool canShoot)
{
    ItemObj *item = ai->item;
    Location from, to;
    (void)gunmanLineClear(item, lara, &from, &to);
    (void)game_tr2_window_check_ray(&from, &to);
    if (!canShoot)
        return false;

    int16 bearing = wrap_i16((int32)p->yaw + item->angle.y);
    int32 sideways = ((int64_t)lara->hSpeed *
                      tr2_sin(wrap_i16((int32)lara->angle.y - bearing))) >> 14;
    int32 scaled = (int32)((uint32)sideways * 67108864u);
    int32 lead = scaled / 300;
    int32 metric = (int32)((uint32)((int64_t)lead * lead) + (uint32)p->distance);
    bool hit = false;
    if (metric <= 8192 * 8192) {
        int32 chance = 8192 + (int32)((INT64_C(67108864) - (int64_t)p->distance) / 3276);
        if (rand_logic() < chance)
            hit = true;
    }

    if (hit) {
        lara->health -= 50;
        lara->flags |= ITEM_FLAG_INJURED;
        soundPlay(50, &lara->pos);
        if (TR64_RULE(ROOM_EFFECTS)) {
            unsigned joint = (unsigned)((rand_logic() * JOINT_MAX) >> 15);
            vec3i pt = game_tr2_combat_joint(lara, joint, vec3i_create(0, 0, 0), 0);
            game_tr2_shot_effect(TR2_FX_BLOOD, lara->room, pt, lara->hSpeed, lara->angle.y);
        }
    } else if (TR64_RULE(ROOM_EFFECTS)) {
        const FloorData *fd = gs->gLastFloorData;
        uint16 slant = gs->gLastFloorSlant;
        int32 rx = i32_add(lara->pos.x, ((rand_logic() * 512) >> 15) - 256);
        int32 rz = i32_add(lara->pos.z, ((rand_logic() * 512) >> 15) - 256);
        Room *rr = lara->room;
        const Sector *sec = game_tr2_room_sector(&rr, rx, lara->pos.y, rz);
        int32 ry = Sector_getFloor(sec, rx, lara->pos.y, rz);
        game_tr2_shot_effect(TR2_FX_RICOCHET, rr, vec3i_create(rx, ry, rz), 0, 0);
        gs->gLastFloorData = fd;
        gs->gLastFloorSlant = slant;
    }
    return true;
}

__attribute__((optimize("Os")))
void game_tr2_gunman_control(TR2Creature *ai, Lara *lara, const Interest *p)
{
    ItemObj *item = ai->item;
    bool canShoot = gunmanCanShoot(item, lara, p);
    int16 torso = 0;
    int16 head = p->ahead ? p->yaw : 0;

    switch (item->state) {
    case GUN_IDLE:
        ai->maxTurn = 0;
        if (item->nextState)
            item->goalState = item->nextState;
        break;
    case GUN_WAIT:
        ai->maxTurn = 0;
        if (ai->mood == ESCAPE) {
            item->nextState = GUN_RUN;
            item->goalState = GUN_IDLE;
        } else if (canShoot) {
            item->nextState = rand_logic() < 16384 ? GUN_AIM1 : GUN_AIM2;
            item->goalState = GUN_IDLE;
        } else if (ai->mood == BORED && p->ahead) {
            int r = rand_logic();
            if (r < 1280) {
                item->nextState = GUN_CROUCH;
                item->goalState = GUN_IDLE;
            } else if (r < 2560) {
                item->nextState = GUN_WALK;
                item->goalState = GUN_IDLE;
            }
        } else {
            item->nextState = (ai->mood == BORED || p->distance < 2048u * 2048u) ? GUN_WALK : GUN_RUN;
            item->goalState = GUN_IDLE;
        }
        break;
    case GUN_WALK:
        ai->maxTurn = 910;
        if (ai->mood == ESCAPE) {
            item->goalState = GUN_RUN;
        } else if (canShoot) {
            item->goalState = GUN_IDLE;
            item->nextState = rand_logic() < 16384 ? GUN_AIM1 : GUN_AIM2;
        } else if (p->ahead && p->distance <= 2048u * 2048u) {
            if (ai->mood == BORED && rand_logic() < 768)
                item->goalState = GUN_IDLE;
        } else {
            item->goalState = GUN_RUN;
        }
        break;
    case GUN_RUN:
        ai->maxTurn = 910;
        if (canShoot) {
            if (ai->mood == ESCAPE)
                item->goalState = GUN_MOVE_FIRE;
            else if (p->distance < 2048u * 2048u)
                item->goalState = GUN_IDLE;
            else
                item->goalState = p->sameZone ? GUN_MOVE_FIRE : GUN_IDLE;
        } else if (ai->mood == BORED) {
            item->goalState = GUN_IDLE;
        }
        break;
    case GUN_CROUCH:
        ai->maxTurn = 0;
        if (canShoot) {
            item->nextState = GUN_AIM1;
            item->goalState = GUN_IDLE;
        }
        break;
    case GUN_AIM1:
    case GUN_AIM2:
        ai->maxTurn = 0;
        ai->flags = 0;
        torso = p->ahead ? p->yaw : 0;
        if (canShoot && ai->mood != ESCAPE)
            item->goalState = item->state == GUN_AIM1 ? GUN_FIRE1 : GUN_FIRE2;
        else
            item->goalState = GUN_IDLE;
        break;
    case GUN_FIRE1:
    case GUN_FIRE2:
        ai->maxTurn = 0;
        torso = p->ahead ? p->yaw : 0;
        if (!ai->flags) {
            (void)gunmanShot(ai, lara, p, canShoot);
            ai->flags = 1;
        }
        break;
    case GUN_MOVE_FIRE:
        ai->maxTurn = 910;
        torso = p->ahead ? p->yaw : 0;
        if (!item->nextState) {
            if (!gunmanShot(ai, lara, p, canShoot))
                item->goalState = GUN_RUN;
            item->nextState = GUN_MOVE_FIRE;
        }
        break;
    default:
        sys_error("Tomb Raider II: gunman state %u unknown", item->state);
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], torso);
    ai->jointYaw[1] = trackHead(ai->jointYaw[1], head);
}

void game_tr2_gunman_death(TR2Creature *ai)
{
    ItemObj *item = ai->item;
    if (item->state != GUN_DIE1 && item->state != GUN_DIE2) {
        unsigned base = game_tr2_gunman_anim_base(item->type);
        bool second = rand_logic() >= 16384;
        ItemObj_animSet(item, base + (second ? GUN_DEATH_CLIP2 : GUN_DEATH_CLIP1), false, 0);
        item->state = second ? GUN_DIE2 : GUN_DIE1;
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], 0);
    ai->jointYaw[1] = trackHead(ai->jointYaw[1], 0);
}

/* tr2-opera-house.md §9: Opera House dual-gun boss (type 20). */
enum {
    BOSS_WAIT1 = 1, BOSS_WAIT2 = 2, BOSS_WALK = 3, BOSS_RUN = 4,
    BOSS_AIM_L = 5, BOSS_AIM_R = 6, BOSS_FIRE_L = 7, BOSS_FIRE_R = 8,
    BOSS_AIM_BOTH = 9, BOSS_FIRE_BOTH = 10, BOSS_DIE = 11,
    BOSS_DEATH_CLIP = 32
};

static void bossFireGun(TR2Creature *ai, Lara *lara, const Interest *p, bool canShoot,
                        unsigned joint, vec3i offset, int16 yawOffset)
{
    ItemObj *item = ai->item;
    if (gunmanShot(ai, lara, p, canShoot)) {
        vec3i muzzle = game_tr2_combat_joint(item, joint, offset, 0);
        game_tr2_gun_flash_effect(item->room, muzzle, wrap_i16((int32)item->angle.y + yawOffset));
    }
}

__attribute__((optimize("Os")))
void game_tr2_boss_control(TR2Creature *ai, Lara *lara, const Interest *p, int16 turn)
{
    ItemObj *item = ai->item;
    bool canShoot = gunmanCanShoot(item, lara, p);
    int16 torso = 0;
    int16 head = p->ahead ? p->yaw : 0;
    int16 tilt = 0;

    switch (item->state) {
    case BOSS_WAIT1:
    case BOSS_WAIT2:
        if (ai->mood == BORED && lara->health <= 0) {
            item->goalState = BOSS_WAIT2;
        } else if (canShoot) {
            item->goalState = p->distance <= UINT64_C(3072) * 3072 ? BOSS_AIM_BOTH : BOSS_WALK;
        } else if (ai->mood == ESCAPE) {
            item->goalState = BOSS_RUN;
        } else if (ai->mood == ATTACK) {
            item->goalState = (p->distance > UINT64_C(5120) * 5120 || !p->ahead) ? BOSS_RUN : BOSS_WALK;
        } else if (ai->mood == STALK) {
            item->goalState = BOSS_WALK;
        } else if (!p->ahead) {
            item->goalState = BOSS_WALK;
        }
        break;
    case BOSS_WALK:
        ai->maxTurn = 546;
        if (canShoot) {
            if (p->distance < UINT64_C(3072) * 3072 || !p->sameZone)
                item->goalState = BOSS_WAIT1;
            else
                item->goalState = p->yaw < 0 ? BOSS_AIM_L : BOSS_AIM_R;
        } else if (ai->mood == ESCAPE) {
            item->goalState = BOSS_RUN;
        } else if (ai->mood == ATTACK || ai->mood == STALK) {
            if (p->distance > UINT64_C(5120) * 5120 || !p->ahead)
                item->goalState = BOSS_RUN;
        } else if (ai->mood == BORED) {
            if (lara->health <= 0)
                item->goalState = BOSS_WAIT2;
            else if (p->ahead)
                item->goalState = BOSS_WAIT1;
        }
        break;
    case BOSS_RUN:
        ai->maxTurn = 546;
        tilt = (int16)(turn / 4);
        if (canShoot) {
            if (p->sameZone)
                item->goalState = p->yaw < 0 ? BOSS_AIM_L : BOSS_AIM_R;
            else
                item->goalState = BOSS_WAIT1;
        } else if (ai->mood == BORED) {
            item->goalState = lara->health > 0 ? BOSS_WAIT1 : BOSS_WAIT2;
        } else if (p->ahead && p->distance < UINT64_C(5120) * 5120) {
            item->goalState = BOSS_WALK;
        }
        break;
    case BOSS_AIM_L:
    case BOSS_AIM_R:
        ai->flags = 0;
        torso = p->ahead ? p->yaw : 0;
        if (canShoot)
            item->goalState = item->state == BOSS_AIM_L ? BOSS_FIRE_L : BOSS_FIRE_R;
        else
            item->goalState = BOSS_WALK;
        break;
    case BOSS_FIRE_L:
    case BOSS_FIRE_R:
        torso = p->ahead ? p->yaw : 0;
        if (!ai->flags) {
            bool left = item->state == BOSS_FIRE_L;
            bossFireGun(ai, lara, p, canShoot, left ? 6u : 10u,
                        left ? vec3i_create(-2, 275, 23) : vec3i_create(2, 275, 23),
                        p->ahead ? p->yaw : 0);
            ai->flags = 1;
        }
        break;
    case BOSS_AIM_BOTH:
        ai->flags = 0;
        torso = p->ahead ? p->yaw : 0;
        item->goalState = canShoot ? BOSS_FIRE_BOTH : BOSS_WAIT1;
        break;
    case BOSS_FIRE_BOTH:
        torso = p->ahead ? p->yaw : 0;
        if (!ai->flags) {
            bossFireGun(ai, lara, p, canShoot, 6u, vec3i_create(-2, 275, 23), 0);
            bossFireGun(ai, lara, p, canShoot, 10u, vec3i_create(2, 275, 23), 0);
            ai->flags = 1;
        }
        break;
    default:
        sys_error("Tomb Raider II: Opera boss state %u unknown", item->state);
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], torso);
    ai->jointYaw[1] = trackHead(ai->jointYaw[1], head);
    int32 rollDelta = (int32)tilt * 4 - (int32)item->angle.z;
    item->angle.z = wrap_i16((int32)item->angle.z + X_MAX(-546, X_MIN(546, rollDelta)));
}

void game_tr2_boss_death(TR2Creature *ai)
{
    ItemObj *item = ai->item;
    if (item->state != BOSS_DIE) {
        ItemObj_animSet(item, gs->level.models[item->type].animIndex + BOSS_DEATH_CLIP, false, 0);
        item->state = BOSS_DIE;
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], 0);
    ai->jointYaw[1] = trackHead(ai->jointYaw[1], 0);
}

/* tr2-venice-creatures.md, Canal rat (21). */
enum {
    RAT_WALK = 1, RAT_HALT = 2, RAT_PAUSE1 = 3, RAT_PAUSE2 = 4,
    RAT_BITE = 5, RAT_DIE = 6, RAT_DEATH_CLIP = 9
};
static const uint32 RAT_BITE_MASK = UINT32_C(0x7f); /* meshes 0-6 */

void game_tr2_rat_control(TR2Creature *ai, Lara *lara, const Interest *p)
{
    ItemObj *item = ai->item;
    ai->maxTurn = 1092;
    switch (item->state) {
    case RAT_WALK:
        if (ai->mood == BORED || ai->mood == STALK) {
            int r = rand_logic();
            if (r < 1280) {
                item->nextState = RAT_PAUSE1;
                item->goalState = RAT_HALT;
            } else if (r < 2560) {
                item->goalState = RAT_HALT;
            }
        } else if (p->ahead && p->distance < 341u * 341u) {
            item->goalState = RAT_HALT;
        }
        break;
    case RAT_HALT:
        ai->maxTurn = 0;
        if (item->nextState)
            item->goalState = item->nextState;
        break;
    case RAT_PAUSE1:
        ai->maxTurn = 0;
        if (rand_logic() < 1280)
            item->goalState = RAT_HALT;
        break;
    case RAT_PAUSE2:
        ai->maxTurn = 0;
        if (ai->mood == BORED || ai->mood == STALK) {
            int r = rand_logic();
            if (r < 1280)
                item->nextState = RAT_PAUSE1;
            else if (r > 2560)
                item->nextState = RAT_WALK;
        } else if (p->distance < 341u * 341u) {
            item->nextState = RAT_BITE;
        } else {
            item->nextState = RAT_WALK;
        }
        if (item->nextState)
            item->goalState = RAT_HALT;
        break;
    case RAT_BITE:
        if (!item->nextState && (item->hitMask & RAT_BITE_MASK)) {
            if (lara->health > 0) {
                lara->health -= 20;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            item->nextState = RAT_HALT;
        }
        break;
    default:
        sys_error("Tomb Raider II: canal rat state %u unknown", item->state);
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], p->ahead ? p->yaw : 0);
}

void game_tr2_rat_death(TR2Creature *ai)
{
    ItemObj *item = ai->item;
    if (item->state != RAT_DIE) {
        ItemObj_animSet(item, gs->level.models[item->type].animIndex + RAT_DEATH_CLIP, false, 0);
        item->state = RAT_DIE;
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], 0);
}

/* tr2-venice-creatures.md, Club thug (32). */
enum {
    THUG_REST = 1, THUG_WALK = 2, THUG_MOVE_STRIKE = 3, THUG_MOVE_AIM = 4,
    THUG_GESTURE = 5, THUG_MED_AIM = 6, THUG_CLOSE_AIM = 7,
    THUG_MED_ATK = 8, THUG_CLOSE_ATK = 9, THUG_RUN = 10, THUG_DIE = 11,
    THUG_CLIMB512 = 12, THUG_CLIMB768 = 13, THUG_CLIMB1024 = 14, THUG_DROP1024 = 15,
    THUG_DEATH_CLIP = 26, THUG_CLIMB1024_CLIP = 27, THUG_CLIMB512_CLIP = 28,
    THUG_CLIMB768_CLIP = 29, THUG_DROP1024_CLIP = 30
};
static const uint32 THUG_CLUB_MASK = (UINT32_C(1) << 9) | (UINT32_C(1) << 10);

__attribute__((optimize("Os")))
void game_tr2_thug_control(TR2Creature *ai, Lara *lara, const Interest *p)
{
    ItemObj *item = ai->item;
    bool bite = p->ahead && lara->health > 0 && llabs((int64_t)lara->pos.y - item->pos.y) <= 384;
    switch (item->state) {
    case THUG_REST:
        ai->maxTurn = 0;
        if (ai->mood == ESCAPE) {
            item->goalState = THUG_RUN;
        } else if (ai->mood == BORED) {
            if (item->nextState)
                item->goalState = item->nextState;
            else
                item->goalState = rand_logic() < 16384 ? THUG_WALK : THUG_GESTURE;
        } else if (bite && p->distance < 512u * 512u) {
            item->goalState = THUG_CLOSE_AIM;
        } else if (p->ahead && p->distance < 1024u * 1024u) {
            item->goalState = THUG_MED_AIM;
        } else if (bite && p->distance < 2048u * 2048u) {
            item->goalState = THUG_WALK;
        } else {
            item->goalState = THUG_RUN;
        }
        break;
    case THUG_WALK:
        ai->maxTurn = 910;
        if (ai->mood == ESCAPE) {
            item->goalState = THUG_RUN;
        } else if (ai->mood == BORED) {
            if (rand_logic() < 256) {
                item->nextState = THUG_GESTURE;
                item->goalState = THUG_REST;
            }
        } else if (bite && p->distance < 512u * 512u) {
            item->goalState = THUG_REST;
        } else if (bite && p->distance < 1536u * 1536u) {
            item->goalState = THUG_MOVE_AIM;
        }
        break;
    case THUG_RUN:
        ai->maxTurn = 1092;
        if (ai->mood != ESCAPE &&
            (ai->mood == BORED || (p->ahead && p->distance < 2048u * 2048u)))
            item->goalState = THUG_WALK;
        break;
    case THUG_GESTURE:
        ai->maxTurn = 0;
        if (ai->mood != BORED) {
            item->goalState = THUG_REST;
        } else if (rand_logic() < 256) {
            item->nextState = THUG_WALK;
            item->goalState = THUG_REST;
        }
        break;
    case THUG_MOVE_AIM:
        ai->flags = 0;
        item->goalState = (bite && p->distance < 1536u * 1536u) ? THUG_MOVE_STRIKE : THUG_WALK;
        break;
    case THUG_MED_AIM:
        ai->flags = 0;
        item->goalState = (p->ahead && p->distance < 1024u * 1024u) ? THUG_MED_ATK : THUG_REST;
        break;
    case THUG_CLOSE_AIM:
        ai->flags = 0;
        item->goalState = (bite && p->distance < 512u * 512u) ? THUG_CLOSE_ATK : THUG_REST;
        break;
    case THUG_MED_ATK:
        if (!(ai->flags & 1u) && (item->hitMask & THUG_CLUB_MASK)) {
            if (lara->health > 0) {
                lara->health -= 80;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            ai->flags |= 1u;
        }
        if (p->ahead && p->distance > 1024u * 1024u && p->distance < 1536u * 1536u)
            item->goalState = THUG_MOVE_STRIKE;
        break;
    case THUG_CLOSE_ATK:
        if (!(ai->flags & 1u) && (item->hitMask & THUG_CLUB_MASK)) {
            if (lara->health > 0) {
                lara->health -= 80;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            ai->flags |= 1u;
        }
        break;
    case THUG_MOVE_STRIKE:
        if (!(ai->flags & 2u) && (item->hitMask & THUG_CLUB_MASK)) {
            if (lara->health > 0) {
                lara->health -= 100;
                lara->flags |= ITEM_FLAG_INJURED;
            }
            ai->flags |= 2u;
        }
        break;
    case THUG_CLIMB512: case THUG_CLIMB768: case THUG_CLIMB1024: case THUG_DROP1024:
        ai->maxTurn = 0;
        break;
    default:
        sys_error("Tomb Raider II: club thug state %u unknown", item->state);
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], p->ahead ? p->yaw : 0);
}

void game_tr2_thug_death(TR2Creature *ai)
{
    ItemObj *item = ai->item;
    if (item->state != THUG_DIE) {
        ItemObj_animSet(item, gs->level.models[item->type].animIndex + THUG_DEATH_CLIP, false, 0);
        item->state = THUG_DIE;
    }
    ai->jointYaw[0] = trackHead(ai->jointYaw[0], 0);
}

/* tr2-venice-objects.md, Glass, gondolas and aquatic mines (type 72).
 * game_tr2_window_shatter lives in resident object_factory.c so grenades
 * (tr2_wall) and bullets/raycasts (tr2_urban) share it without unit re-entry. */
bool game_tr2_window_interact(ItemObj *item, Lara *lara)
{
    if (!(item->flags & ITEM_FLAG_COLLISION) || item->animIndex == UINT16_MAX)
        return false;
    return game_tr2_solid_contact(item, lara, true, false) == TR2_SOLID_BUMP;
}

void game_tr2_window_update(ItemObj *item, Lara *lara)
{
    if (!(item->flags & ITEM_FLAG_COLLISION) || (item->flags & TR2_FLAG_RETIRED))
        return;
    if (gs->tr2->vehicle >= 0) {
        int32 dx = i32_sub(item->pos.x, lara->pos.x);
        int32 dz = i32_sub(item->pos.z, lara->pos.z);
        int32 dy = i32_sub(item->pos.y, lara->pos.y);
        if (dx >= -512 && dx <= 512 && dz >= -512 && dz <= 512 &&
            (int64_t)dx * dx + (int64_t)dz * dz <= INT64_C(512) * 512 &&
            dy >= -3072 && dy <= 3072) {
            const AABBs *bounds = ItemObj_getBoundingBox(lara, true);
            if (dy >= bounds->minY && dy <= bounds->maxY + 100) {
                game_tr2_window_shatter(item);
                return;
            }
        }
    } else if (item->hitMask) {
        int32 proj = ((int64_t)lara->hSpeed *
                      tr2_sin(wrap_i16((int32)lara->angle.y - item->angle.y + ANGLE_90))) >> 14;
        if (proj >= 50 || proj <= -50) {
            game_tr2_window_shatter(item);
            return;
        }
    }
    item->hitMask = 0;
}

static bool windowRayHits(const ItemObj *item, const vec3i *from, const vec3i *to)
{
    if (item->animIndex == UINT16_MAX)
        return false;
    const AABBs *b = ItemObj_getBoundingBox(item, false);
    int32 minX, maxX, minZ, maxZ;
    switch (((uint16)(item->angle.y + 8192u)) >> 14) {
    case 0:
        minX = b->minX; maxX = b->maxX; minZ = b->minZ; maxZ = b->maxZ;
        break;
    case 1:
        minX = b->minZ; maxX = b->maxZ; minZ = -b->maxX; maxZ = -b->minX;
        break;
    case 2:
        minX = -b->maxX; maxX = -b->minX; minZ = -b->maxZ; maxZ = -b->minZ;
        break;
    default:
        minX = -b->maxZ; maxX = -b->minZ; minZ = b->minX; maxZ = b->maxX;
        break;
    }
    int32 minY = i32_add(item->pos.y, b->minY);
    int32 maxY = i32_add(item->pos.y, b->maxY);
    minX = i32_add(item->pos.x, minX);
    maxX = i32_add(item->pos.x, maxX);
    minZ = i32_add(item->pos.z, minZ);
    maxZ = i32_add(item->pos.z, maxZ);

    int32 dx = i32_sub(to->x, from->x);
    int32 dy = i32_sub(to->y, from->y);
    int32 dz = i32_sub(to->z, from->z);
    if (llabs((int64_t)dx) >= llabs((int64_t)dz)) {
        if (!dx) return false;
        int side = 0;
        const int32 faces[2] = { minX, maxX };
        for (unsigned f = 0; f < 2; ++f) {
            int32 d = i32_sub(faces[f], from->x);
            if ((dx > 0 && d < 0) || (dx < 0 && d > 0))
                continue;
            int32 y = i32_add(from->y, (int32)((int64_t)dy * d / dx));
            if (y <= minY || y >= maxY)
                continue;
            int32 z = i32_add(from->z, (int32)((int64_t)dz * d / dx));
            int s = z < minZ ? -1 : z > maxZ ? 1 : 0;
            if (!s) return true;
            if (side && side != s) return true;
            side = s;
        }
        return false;
    } else {
        int side = 0;
        const int32 faces[2] = { minZ, maxZ };
        for (unsigned f = 0; f < 2; ++f) {
            int32 d = i32_sub(faces[f], from->z);
            if ((dz > 0 && d < 0) || (dz < 0 && d > 0))
                continue;
            int32 y = i32_add(from->y, (int32)((int64_t)dy * d / dz));
            if (y <= minY || y >= maxY)
                continue;
            int32 x = i32_add(from->x, (int32)((int64_t)dx * d / dz));
            int s = x < minX ? -1 : x > maxX ? 1 : 0;
            if (!s) return true;
            if (side && side != s) return true;
            side = s;
        }
        return false;
    }
}

static void addRayRoom(Room **rooms, unsigned *count, unsigned cap, Room *room)
{
    if (!room || *count >= cap)
        return;
    for (unsigned i = 0; i < *count; ++i)
        if (rooms[i] == room)
            return;
    rooms[(*count)++] = room;
}

static void collectRayRoomsAxis(const Location *from, const Location *to, int axis,
                                Room **rooms, unsigned *count, unsigned cap)
{
    int32 start = axis == 0 ? from->pos.x : from->pos.z;
    int32 finish = axis == 0 ? to->pos.x : to->pos.z;
    int32 dist = i32_sub(finish, start);
    if (!dist)
        return;
    int32 step = dist < 0 ? -1 : 1;
    int32 oStart = axis == 0 ? from->pos.z : from->pos.x;
    int32 oEnd = axis == 0 ? to->pos.z : to->pos.x;
    int64_t across = (int64_t)i32_sub(oEnd, oStart) * 1024 / dist;
    int64_t vertical = (int64_t)i32_sub(to->pos.y, from->pos.y) * 1024 / dist;
    int32 boundary = step < 0 ? (start & ~1023) : (start | 1023);
    int32 elapsed = i32_sub(boundary, start);
    int32 along = boundary;
    int32 side = i32_add(oStart, (int32)((across * elapsed) >> 10));
    int32 y = i32_add(from->pos.y, (int32)((vertical * elapsed) >> 10));
    Room *cur = from->room;
    while (step < 0 ? along > finish : along < finish) {
        int32 x = axis == 0 ? along : side;
        int32 z = axis == 0 ? side : along;
        (void)game_tr2_room_sector(&cur, x, y, z);
        addRayRoom(rooms, count, cap, cur);
        Room *far = cur;
        int32 fx = axis == 0 ? i32_add(along, step) : side;
        int32 fz = axis == 0 ? side : i32_add(along, step);
        (void)game_tr2_room_sector(&far, fx, y, fz);
        addRayRoom(rooms, count, cap, far);
        cur = far;
        along = i32_add(along, 1024 * step);
        side = i32_add(side, (int32)(across * step));
        y = i32_add(y, (int32)(vertical * step));
    }
}

bool game_tr2_window_check_ray(const Location *from, const Location *to)
{
    if (!from->room)
        return false;
    Room *rooms[32];
    unsigned count = 0;
    addRayRoom(rooms, &count, 32, from->room);
    int32 dx = i32_sub(to->pos.x, from->pos.x);
    int32 dz = i32_sub(to->pos.z, from->pos.z);
    int first = (int64_t)dx * dx < (int64_t)dz * dz ? 0 : 1;
    collectRayRoomsAxis(from, to, first, rooms, &count, 32);
    collectRayRoomsAxis(from, to, first ^ 1, rooms, &count, 32);
    addRayRoom(rooms, &count, 32, to->room);

    for (unsigned r = 0; r < count; ++r) {
        for (ItemObj *it = rooms[r]->firstItem; it; it = it->nextItem) {
            if (it->type == TR2_ITEM_WINDOW && (it->flags & ITEM_FLAG_COLLISION) &&
                windowRayHits(it, &from->pos, &to->pos)) {
                game_tr2_window_shatter(it);
                return true;
            }
        }
    }
    return false;
}

#endif
