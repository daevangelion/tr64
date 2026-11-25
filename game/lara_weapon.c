/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include <limits.h>

typedef struct {
    int32 modelType, animType, damage, spread, range, height, soundId, reloadTimer;
    int32 flashOffset, flashTimer, flashIntensity, aimX, aimY, armX, armMinY, armMaxY;
} WeaponParams;
enum { ANIM_PISTOLS_AIM, ANIM_PISTOLS_PICK, ANIM_PISTOLS_DRAW, ANIM_PISTOLS_FIRE };
enum { ANIM_SHOTGUN_AIM, ANIM_SHOTGUN_DRAW, ANIM_SHOTGUN_FIRE };
static const WeaponParams weaponParams[WEAPON_MAX] = {
    { // WEAPON_PISTOLS
        ITEM_LARA_PISTOLS,  // modelType
        ITEM_LARA_PISTOLS,  // animType
        1,                  // damage
        ANGLE(8),           // spread
        8192,               // range
        650,                // height
        SND_PISTOLS_SHOT,   // soundId
        9,                  // reloadTimer
        155,                // flashOffset
        3,                  // flashTimer
        20,                 // flashIntensity
        ANGLE(60),          // aimX
        ANGLE(60),          // aimY
        ANGLE(80),          // armX
        ANGLE(-60),         // armMinY
        ANGLE(170),         // armMaxY
    },
    { // WEAPON_MAGNUMS
        ITEM_LARA_MAGNUMS,  // modelType
        ITEM_LARA_PISTOLS,  // animType
        2,                  // damage
        ANGLE(8),           // spread
        8192,               // range
        650,                // height
        SND_MAGNUMS_SHOT,   // soundId
        9,                  // reloadTimer
        155,                // flashOffset
        3,                  // flashTimer
        16,                 // flashIntensity
        ANGLE(60),          // aimX
        ANGLE(60),          // aimY
        ANGLE(80),          // armX
        ANGLE(-60),         // armMinY
        ANGLE(170),         // armMaxY
    },
    { // WEAPON_UZIS
        ITEM_LARA_UZIS,     // modelType
        ITEM_LARA_PISTOLS,  // animType
        1,                  // damage
        ANGLE(8),           // spread
        8192,               // range
        650,                // height
        SND_UZIS_SHOT,      // soundId
        3,                  // reloadTimer
        180,                // flashOffset
        2,                  // flashTimer
        10,                 // flashIntensity
        ANGLE(60),          // aimX
        ANGLE(60),          // aimY
        ANGLE(80),          // armX
        ANGLE(-60),         // armMinY
        ANGLE(170),         // armMaxY
    },
    { // WEAPON_SHOTGUN
        ITEM_LARA_SHOTGUN,  // modelType
        ITEM_LARA_SHOTGUN,  // animType
        4,                  // damage
        ANGLE(20),          // spread
        8192,               // range
        500,                // height
        SND_SHOTGUN_SHOT,   // soundId
        26,                 // reloadTimer
        0,                  // flashOffset
        0,                  // flashTimer
        0,                  // flashIntensity
        ANGLE(55),          // aimX
        ANGLE(60),          // aimY
        ANGLE(65),          // armX
        ANGLE(-80),         // armMinY
        ANGLE(80),          // armMaxY
    },
};

void Lara_meshSwapPistols(Lara *, uint32, uint32);
void Lara_meshSwapShotgun(Lara *, bool);
void matrixSetView(const vec3i *, int32, int32);

bool Lara_applyWeaponHit(Lara *self, ItemObj *target, int32 damage, const vec3i *point)
{
    if (!target || target->health <= 0)
        return false;
    int32 healthBefore = target->health;
    ItemObj_hitBy(target,self,damage,point,0);
    if (healthBefore > 0 && target->health <= 0)
    {
        gs->gSaveGame.kills++;
    }
    return true;
}

#if TR64_HAS_TR2
int32 game_weapon_spread(int32 spread, bool truncate)
{
    int32 product = (int32)(rand_logic() - 0x4000) * spread;
    return truncate ? product / 65536 : product >> 16;
}
#endif

bool Lara_weaponFire(Lara *self, const ExtraInfoLara_Arm* arm)
{
    int32 ammoUsed = 0;
    if (!inventory_consumeAmmo((Weapon)self->extraL->weapon, &ammoUsed))
    {
        soundPlay(SND_EMPTY, &self->pos);
        self->extraL->goalWeapon = WEAPON_PISTOLS;
        return false;
    }
    gs->gSaveGame.ammoUsed =
        (uint16)(gs->gSaveGame.ammoUsed + ammoUsed);
    if (!inventory_hasAmmo((Weapon)self->extraL->weapon) &&
        inventory_canSelectWeapon(WEAPON_PISTOLS))
        self->extraL->goalWeapon = WEAPON_PISTOLS;


    const WeaponParams *params = &weaponParams[self->extraL->weapon]; 

    Location from;
    GAME_WEAPON_ORIGIN(from,self,params->height);

    int32 count = (self->extraL->weapon == WEAPON_SHOTGUN) ? WEAPON_SHOTGUN_PELLETS : 1;

    for (int32 i = 0; i < count; i++)
    {
#if TR64_HAS_TR2
        int32 aimX = game_weapon_spread(params->spread, false);
        int32 aimY = game_weapon_spread(params->spread, false);
#else
        int32 aimX = (int32)(rand_logic() - 0x4000) * params->spread >> 16;
        int32 aimY = (int32)(rand_logic() - 0x4000) * params->spread >> 16;
#endif

        aimX += arm->angle.x;
        aimY += arm->angle.y;
        aimY += self->angle.y;

        matrixSetView(&from.pos, aimX, aimY);

        int32 minDist = INT_MAX;

        if (arm->target && arm->target->health > 0)
        {
            Sphere spheres[32];
            int32 spheresCount = ItemObj_getSpheres(arm->target, spheres, false);

            for (int32 i = 0; i < spheresCount; i++)
            {
                const Sphere *s = &spheres[i];

                if (abs(s->center.x) >= s->radius)
                    continue;

                if (abs(s->center.y) >= s->radius)
                    continue;

                if (s->center.z <= s->radius)
                    continue;

                if (fastLength(s->center.x, s->center.y) > s->radius)
                    continue;

                int32 dist = s->center.z - s->radius;

                if (dist < minDist) {
                    minDist = dist;
                }
            }
        }

        vec3i dir = vec3i_create(gs->gMatrixPtr->e20, gs->gMatrixPtr->e21, gs->gMatrixPtr->e22);

        Location to = from;

        if (minDist != INT_MAX)
        {
            dir = vec3i_scale(dir, minDist);
            to.pos.x += dir.x >> FIXED_SHIFT;
            to.pos.y += dir.y >> FIXED_SHIFT;
            to.pos.z += dir.z >> FIXED_SHIFT;

            Lara_applyWeaponHit(self, arm->target, params->damage, &to.pos);
        } else {
            to.pos = vec3i_add(to.pos, dir);

            trace(&from, &to, true);
            ItemObj_fxRicochet(to.room, &to.pos, true);
        }
    }

    soundPlay(params->soundId, &self->pos);

    return true;
}

void Lara_setWeaponState(Lara *self, WeaponState weaponState)
{
    if (weaponState == self->extraL->weaponState)
        return;
    self->extraL->weaponState = weaponState;

    ExtraInfoLara_Arm *R = &self->extraL->armR;
    ExtraInfoLara_Arm *L = &self->extraL->armL;

    if (weaponState == WEAPON_STATE_DRAW)
    {
        const WeaponParams *params = &weaponParams[self->extraL->weapon];
        int32 anim = (self->extraL->weapon == WEAPON_SHOTGUN) ? ANIM_SHOTGUN_DRAW : ANIM_PISTOLS_PICK;
        R->animIndex = L->animIndex = gs->level.models[params->animType].animIndex + anim;
        R->frameIndex = L->frameIndex = 0;
    }

    if (weaponState == WEAPON_STATE_HOLSTER)
    {
        R->target = L->target = NULL;
    }

    if (weaponState == WEAPON_STATE_FREE)
    {
        R->useBasis = L->useBasis = false;
        R->animIndex = L->animIndex = 0;
        R->frameIndex = L->frameIndex = 0;
    }
}

#if !TR64_HAS_TR2
static
#endif
void game_weapon_aim(ExtraInfoLara_Arm *arm)
{
    if (arm->aim) {
        arm->angle.x = angleLerp(arm->angle.x, arm->angleAim.x, ANGLE(10));
        arm->angle.y = angleLerp(arm->angle.y, arm->angleAim.y, ANGLE(10));
    } else {
        arm->angle.x = angleLerp(arm->angle.x, 0, ANGLE(10));
        arm->angle.y = angleLerp(arm->angle.y, 0, ANGLE(10));
    }

}

#line 238 /* Preserve TR1 cart assertion IDs. */
void Lara_weaponDrawPistols(Lara *self)
{
    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    ExtraInfoLara_Arm* arm = &self->extraL->armR;

    const Anim* animPtr = gs->level.anims + arm->animIndex;
    int32 animLength = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
    int32 frame = arm->frameIndex + 1;
    int32 anim = arm->animIndex - gs->level.models[params->animType].animIndex;

    if (frame > animLength)
    {
        anim++;

        if (anim == ANIM_PISTOLS_DRAW) {
            Lara_meshSwapPistols(self, JOINT_MASK_ARM_R3 | JOINT_MASK_ARM_L3, JOINT_MASK_LEG_R1 | JOINT_MASK_LEG_L1);
            soundPlay(SND_DRAW, &self->pos);
        } else if (anim == ANIM_PISTOLS_FIRE) {
            anim = ANIM_PISTOLS_AIM;
            Lara_setWeaponState(self, WEAPON_STATE_READY);
        }

        frame = 0;
    }

    self->extraL->armR.angle = self->extraL->armL.angle = vec3s_create(0, 0, 0);
    self->extraL->armR.animIndex = self->extraL->armL.animIndex = anim + gs->level.models[params->animType].animIndex;
    self->extraL->armR.frameIndex = self->extraL->armL.frameIndex = frame;
}

void Lara_weaponHolsterPistols(Lara *self)
{
    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    for (int32 i = 0; i < 2; i++)
    {
        ExtraInfoLara_Arm* arm = i == 0 ? &self->extraL->armR : &self->extraL->armL;

        if (!arm->animIndex)
            continue;

        int32 frame = arm->frameIndex;
        int32 anim = arm->animIndex - gs->level.models[params->animType].animIndex;

        if (frame)
        {
            if (anim == ANIM_PISTOLS_AIM) {
                arm->angle.x -= arm->angle.x / frame; // @DIV
                arm->angle.y -= arm->angle.y / frame; // @DIV
            }

            if (anim == ANIM_PISTOLS_FIRE) {
                frame = 0;
            } else {
                frame--;
            }
        } else {
            if (anim == ANIM_PISTOLS_AIM) {
                anim = ANIM_PISTOLS_DRAW;
            } else if (anim == ANIM_PISTOLS_PICK) {
                arm->animIndex = 0;
                continue;
            } else if (anim == ANIM_PISTOLS_DRAW) {
                anim = ANIM_PISTOLS_PICK;
                if (i == 0) {
                    Lara_meshSwapPistols(self, JOINT_MASK_LEG_R1, JOINT_MASK_ARM_R3);
                } else {
                    Lara_meshSwapPistols(self, JOINT_MASK_LEG_L1, JOINT_MASK_ARM_L3);
                }
                soundPlay(SND_HOLSTER, &self->pos);
            } else if (anim == ANIM_PISTOLS_FIRE) {
                anim = ANIM_PISTOLS_AIM;
            }

            arm->animIndex = anim + gs->level.models[params->animType].animIndex;
            frame = tr64_u16(gs->level.anims[arm->animIndex].frame_end) - tr64_u16(gs->level.anims[arm->animIndex].frame_start);
        }

        arm->frameIndex = frame;
    }

    if (!self->extraL->armR.animIndex && !self->extraL->armL.animIndex) {
        Lara_setWeaponState(self, WEAPON_STATE_FREE);
    }
}

void Lara_weaponDrawShotgun(Lara *self)
{
    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    ExtraInfoLara_Arm *arm = &self->extraL->armR;

    const Anim* animPtr = gs->level.anims + arm->animIndex;
    int32 animLength = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
    int32 frame = arm->frameIndex + 1;
    int32 anim = arm->animIndex - gs->level.models[params->animType].animIndex;

    ASSERT(anim == ANIM_SHOTGUN_DRAW);

    if (frame == 10) {
        Lara_meshSwapShotgun(self, true);
        soundPlay(SND_DRAW, &self->pos);
    }

    if (frame == animLength) {
        Lara_setWeaponState(self, WEAPON_STATE_READY);
    }

    self->extraL->armR.angle = self->extraL->armL.angle = vec3s_create(0, 0, 0);
    /* Publish both arm channels from the same draw pose. */
    game_weapon_pose_pair(self->extraL,anim + gs->level.models[params->animType].animIndex,frame);
}

void Lara_weaponHolsterShotgun(Lara *self)
{
    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    ExtraInfoLara_Arm *arm = &self->extraL->armR;

    int32 frame = arm->frameIndex;
    int32 anim = arm->animIndex - gs->level.models[params->animType].animIndex;

    if (anim == ANIM_SHOTGUN_AIM) {
        if (frame == 0) {
            anim = ANIM_SHOTGUN_DRAW;
            const Anim* animPtr = gs->level.anims + gs->level.models[params->animType].animIndex + anim;
            frame = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
        } else {
            frame--;
        }
    } else if (anim == ANIM_SHOTGUN_FIRE) {
        frame++;
        if (frame > 12) {
            anim = ANIM_SHOTGUN_DRAW;
            const Anim* animPtr = gs->level.anims + gs->level.models[params->animType].animIndex + anim;
            frame = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
        }
    } else if (anim == ANIM_SHOTGUN_DRAW) {
        if (frame == 0) {
            Lara_setWeaponState(self, WEAPON_STATE_FREE);
            return;
        } else {
            if (frame == 10) {
                Lara_meshSwapShotgun(self, false);
                soundPlay(SND_HOLSTER, &self->pos);
            }
            frame--;
        }
    }

    self->extraL->armR.angle = self->extraL->armL.angle = vec3s_create(0, 0, 0);
    /* The reverse draw also exposes one pose to both arms. */
    game_weapon_pose_pair(self->extraL,anim + gs->level.models[params->animType].animIndex,frame);
}

void Lara_weaponDraw(Lara *self)
{
    switch (self->extraL->weapon)
    {
        case WEAPON_PISTOLS:
        case WEAPON_MAGNUMS:
        case WEAPON_UZIS:
            Lara_weaponDrawPistols(self);
            break;
        case WEAPON_SHOTGUN:
            Lara_weaponDrawShotgun(self);
            break;
        default: ASSERT(false);
    }
}

void Lara_weaponHolster(Lara *self)
{
    ItemObj_meshSwap(self, ITEM_LARA, JOINT_MASK_HEAD);

    switch (self->extraL->weapon)
    {
        case WEAPON_PISTOLS:
        case WEAPON_MAGNUMS:
        case WEAPON_UZIS:
            Lara_weaponHolsterPistols(self);
            break;
        case WEAPON_SHOTGUN:
            Lara_weaponHolsterShotgun(self);
            break;
        default: ASSERT(false);
    }
}

void Lara_weaponUpdatePistols(Lara *self)
{
    ExtraInfoLara_Arm *R = &self->extraL->armR;
    ExtraInfoLara_Arm *L = &self->extraL->armL;

    game_weapon_aim(R);
    game_weapon_aim(L);

    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    for (int32 i = 0; i < 2; i++)
    {
        ExtraInfoLara_Arm* arm = i == 0 ? &self->extraL->armR : &self->extraL->armL;

        const Anim* animPtr = gs->level.anims + arm->animIndex;
        int32 animLength = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
        int32 frame = arm->frameIndex;
        int32 anim = arm->animIndex - gs->level.models[params->animType].animIndex;

        if (((self->input & IN_ACTION) && !arm->target) || arm->aim)
        {
            if (anim == ANIM_PISTOLS_AIM)
            {
                if (frame == animLength)
                {
                    if ((self->input & IN_ACTION) && Lara_weaponFire(self, arm))
                    {
                        anim = ANIM_PISTOLS_FIRE;
                        frame = 0;

                        arm->flash.timer = params->flashTimer;
                        arm->flash.angle = wrap_i16((uint32)rand_draw() << 1);
                        arm->flash.offset = params->flashOffset;
                        arm->flash.intensity = params->flashIntensity << 8;
                    }
                } else {
                    frame++;
                }
            } else { // ANIM_DUAL_FIRE
                frame++;
                if (frame == params->reloadTimer)
                {
                    anim = ANIM_PISTOLS_AIM;
                    const Anim* animPtr = gs->level.anims + anim + gs->level.models[params->animType].animIndex;
                    frame = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
                }
            }
        } else {
            if (anim == ANIM_PISTOLS_FIRE)
            {
                anim = ANIM_PISTOLS_AIM;
                const Anim* animPtr = gs->level.anims + anim + gs->level.models[params->animType].animIndex;
                frame = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
            } else if (frame) {
                frame--;
            };
        }

        arm->animIndex = anim + gs->level.models[params->animType].animIndex;
        arm->frameIndex = frame;
        arm->useBasis = (anim == ANIM_PISTOLS_AIM && frame) || (anim == ANIM_PISTOLS_FIRE);
    }
}

void Lara_weaponUpdateShotgun(Lara *self)
{
    ExtraInfoLara_Arm *R = &self->extraL->armR;
    ExtraInfoLara_Arm *L = &self->extraL->armL;

    game_weapon_aim(R);

    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    ExtraInfoLara_Arm* arm = &self->extraL->armR;

    const Anim* animPtr = gs->level.anims + arm->animIndex;
    int32 animLength = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
    int32 frame = arm->frameIndex;
    int32 anim = arm->animIndex - gs->level.models[params->animType].animIndex;

    bool aim = game_weapon_aim_eligible(arm, (self->input & IN_ACTION) != 0);

    switch (anim)
    {
        case ANIM_SHOTGUN_FIRE:
        {
            frame++;
            if (frame == 10) {
                soundPlay(SND_SHOTGUN_RELOAD, &self->pos);
            } else if (frame == params->reloadTimer) {
                anim = ANIM_SHOTGUN_AIM;
                animPtr = gs->level.anims + gs->level.models[params->animType].animIndex + anim;
                frame = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
            } else if ((animLength - frame < 10) && !aim) {
                anim = ANIM_SHOTGUN_AIM;
                frame = animLength - frame; // how many frames left for fire animation
                animPtr = gs->level.anims + gs->level.models[params->animType].animIndex + anim;
                frame = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start) - frame; // offset aim frames from the end
            }
            break;
        }
        case ANIM_SHOTGUN_DRAW:
        {
            if (aim)
            {
                anim = ANIM_SHOTGUN_AIM;
                frame = 1;
            }
            break;
        }
        case ANIM_SHOTGUN_AIM:
        {
            if (aim)
            {
                if (frame == animLength)
                {
                    if ((self->input & IN_ACTION) && Lara_weaponFire(self, arm))
                    {
                        frame = 1;
                        anim = ANIM_SHOTGUN_FIRE;
                    }
                } else {
                    frame++;
                }
            } else {
                if (frame == 0) {
                    anim = ANIM_SHOTGUN_DRAW;
                    animPtr = gs->level.anims + gs->level.models[params->animType].animIndex + anim;
                    animLength = tr64_u16(animPtr->frame_end) - tr64_u16(animPtr->frame_start);
                    frame = animLength;
                } else {
                    frame--;
                }
            }
            break;
        }
    }

    R->useBasis = L->useBasis = false;
    R->animIndex = L->animIndex = anim + gs->level.models[params->animType].animIndex;
    R->frameIndex = L->frameIndex = frame;
}

void Lara_weaponUpdateState(Lara *self)
{
    bool change = false;
    if (self->waterState == WATER_STATE_ABOVE || self->waterState == WATER_STATE_WADE)
    {
        if (self->extraL->weapon != self->extraL->goalWeapon)
        {
            if (self->extraL->weaponState == WEAPON_STATE_FREE) {
                change = true;
            } else if (self->extraL->weaponState == WEAPON_STATE_READY) {
                change = true;
            }
        } else if (self->input & IN_WEAPON) {
            change = true;
        }
    } else if (self->extraL->weaponState == WEAPON_STATE_READY) {
        change = true;
    }

    if (!change)
        return;

    if (self->extraL->weaponState == WEAPON_STATE_FREE)
    {
        if (!inventory_canSelectWeapon((Weapon)self->extraL->goalWeapon))
        {
            if (!inventory_canSelectWeapon(WEAPON_PISTOLS))
                return;
            self->extraL->goalWeapon = WEAPON_PISTOLS;
        }
        self->extraL->weapon = self->extraL->goalWeapon;
        Lara_setWeaponState(self, WEAPON_STATE_DRAW);
    }
        
    if (self->extraL->weaponState == WEAPON_STATE_READY)
    {
        Lara_setWeaponState(self, WEAPON_STATE_HOLSTER);
    }
}

void Lara_weaponGetAimPoint(Lara *self, ItemObj* target, Location *point)
{
    (void)self;
    const AABBs *box = ItemObj_getBoundingBox(target, false);
    vec3i p;
    p.x = (box->minX + box->maxX) >> 1;
    p.y = box->minY + (box->maxY - box->minY) / 3; // @DIV
    p.z = (box->minZ + box->maxZ) >> 1;
    int32 s, c;
    sincos(target->angle.y, s, c);
    int32 x = i32_add(i32_mul(p.x, c), i32_mul(p.z, s)) >> FIXED_SHIFT;
    p.z = i32_sub(i32_mul(p.z, c), i32_mul(p.x, s)) >> FIXED_SHIFT;
    p.x = x;

    point->pos = vec3i_add(target->pos, p);
    point->room = target->room;
}

void Lara_weaponTrackTargets(Lara *self)
{
    ExtraInfoLara_Arm *arm = &self->extraL->armR;

    if (arm->target && arm->target->health <= 0)
    {
        arm->target = NULL;
    }

    if (!arm->target)
    {
        self->extraL->armR.aim = self->extraL->armL.aim = false;
        return;
    }

    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    Location from;
    from.pos.x = self->pos.x;
    from.pos.y = self->pos.y - params->height;
    from.pos.z = self->pos.z;
    from.room = self->room;

    Location to;
    Lara_weaponGetAimPoint(self, arm->target, &to);

    vec3i dir = vec3i_sub(to.pos, from.pos);
    vec3s angleAim = vec3s_create(0, 0, 0);

    anglesFromVector(dir.x, dir.y, dir.z, &angleAim.x, &angleAim.y);

    angleAim.x -= self->angle.x;
    angleAim.y -= self->angle.y;

    if (trace(&from, &to, false))
    {
        if (abs(angleAim.x) <= params->aimX && abs(angleAim.y) <= params->aimY) {
            self->extraL->armR.aim = self->extraL->armL.aim = true;
        } else {
            self->extraL->armR.aim = self->extraL->armR.aim && (abs(angleAim.x) <= params->armX) && (angleAim.y >=  params->armMinY) && (angleAim.y <=  params->armMaxY);
            self->extraL->armL.aim = self->extraL->armL.aim && (abs(angleAim.x) <= params->armX) && (angleAim.y >= -params->armMaxY) && (angleAim.y <= -params->armMinY);
        }
    } else {
        self->extraL->armR.aim = self->extraL->armL.aim = false;
    }

    self->extraL->armR.angleAim = self->extraL->armL.angleAim = angleAim;
}

void Lara_weaponFindTargets(Lara *self)
{
    if (!gs->firstActive)
        return;

    const WeaponParams *params = &weaponParams[self->extraL->weapon];
    int32 range = params->range;
    int32 rangeQ = (range * range);
    int32 minAimY = params->aimY;

    Location from;
    from.pos.x = self->pos.x;
    from.pos.y = self->pos.y - params->height;
    from.pos.z = self->pos.z;
    from.room = self->room;

    ItemObj* item = gs->firstActive;
    do
    {
        if (item->health <= 0)
            continue;

        if ((item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE)
            continue;

        vec3i d = vec3i_sub(item->pos, self->pos);
        int32 distQ = i32_add(i32_add(i32_mul(d.x, d.x), i32_mul(d.y, d.y)), i32_mul(d.z, d.z));

        if (distQ > rangeQ)
            continue;

        Location to;
        Lara_weaponGetAimPoint(self, item, &to);

        if (!trace(&from, &to, false))
            continue;

        vec3i dir = vec3i_sub(to.pos, from.pos);
        vec3s angleAim;

        anglesFromVector(dir.x, dir.y, dir.z, &angleAim.x, &angleAim.y);

        angleAim.x -= self->angle.x + self->extraL->torso.angle.x;
        angleAim.y -= self->angle.y + self->extraL->torso.angle.y;

        angleAim.x = abs(angleAim.x);
        angleAim.y = abs(angleAim.y);

        if (angleAim.x > params->aimX || angleAim.y > params->aimY || angleAim.y > minAimY)
            continue;

        minAimY = angleAim.y;
        self->extraL->armR.target = item;
    } while ((item = item->nextActive) != NULL);
}

void Lara_weaponUpdateTargets(Lara *self)
{
    if (self->input & IN_ACTION) {
        ItemObj_meshSwap(self, ITEM_LARA_UZIS, JOINT_MASK_HEAD);
    } else {
        ItemObj_meshSwap(self, ITEM_LARA, JOINT_MASK_HEAD);
        self->extraL->armR.target = NULL;
    }

    if (self->extraL->armR.target == NULL) {
        Lara_weaponFindTargets(self);
    }

    Lara_weaponTrackTargets(self);

    self->extraL->armL.target = self->extraL->armR.target;
}

void Lara_updateWeapon(Lara *self)
{
    if (self->extraL->armR.flash.timer) {
        self->extraL->armR.flash.timer--;
    }

    if (self->extraL->armL.flash.timer) {
        self->extraL->armL.flash.timer--;
    }

    if (self->extraL->weapon == WEAPON_MAX)
        return;

    if (self->health <= 0)
    {
        self->extraL->armR.animIndex = self->extraL->armL.animIndex = 0;
        self->extraL->armR.useBasis = self->extraL->armL.useBasis = false;
        return;
    }

    Lara_weaponUpdateState(self);

    switch (self->extraL->weaponState)
    {
        case WEAPON_STATE_DRAW:
        {
            Camera_toCombat(&self->extraL->camera);
            Lara_weaponDraw(self);
            break;
        }

        case WEAPON_STATE_HOLSTER:
        {
            Lara_weaponHolster(self);
            break;
        }

        case WEAPON_STATE_READY:
        {
            Camera_toCombat(&self->extraL->camera);
            Lara_weaponUpdateTargets(self);

            if (self->extraL->weapon < WEAPON_SHOTGUN) {
                Lara_weaponUpdatePistols(self);
            } else {
                Lara_weaponUpdateShotgun(self);
            }
        }

        default: ;
    }
}

void Lara_changeWeapon(Lara *self, Weapon weapon)
{
    self->extraL->goalWeapon = weapon;
    if ((self->extraL->weaponState == WEAPON_STATE_FREE) && (self->extraL->goalWeapon == self->extraL->weapon))
    {
        self->extraL->weapon = WEAPON_NONE;
    }
}

bool Lara_useItem(Lara *self, int32 slot)
{
    #if TR64_HAS_TR2
        /* tr2-triggers-and-interaction, Medipacks (Q-CARTUI-01). Living and
         * below full only; the sequel ring is the stock. Heal, statistic,
         * timer and sound stay this routine's. */
        if (TR64_RULE(FRESH_STOCK)) {
            if (slot != 171 && slot != 172)
                return false;
            TR2RingEntry *entry;
            if (self->health <= 0 || self->health >= LARA_MAX_HEALTH)
                return false;
            entry = game_tr2_ring_find((unsigned)slot);
            if (!entry || !entry->quantity)
                return false;
            game_tr2_ring_debit(entry);
            self->health += slot == 172 ? LARA_MAX_HEALTH : (LARA_MAX_HEALTH >> 1);
            if (self->health > LARA_MAX_HEALTH)
                self->health = LARA_MAX_HEALTH;
            gs->gSaveGame.mediUsed = (uint16)(gs->gSaveGame.mediUsed + (slot == 172 ? 2 : 1));
            self->extraL->healthTimer = 40;
            soundPlay(SND_HEALTH, &self->pos);
            return true;
        }
    #endif
        switch (slot)
        {
        case SLOT_PISTOLS:
            if (!inventory_canSelectWeapon(WEAPON_PISTOLS))
                return false;
            Lara_changeWeapon(self, WEAPON_PISTOLS);
            break;
        case SLOT_SHOTGUN:
            if (!inventory_canSelectWeapon(WEAPON_SHOTGUN))
                return false;
            Lara_changeWeapon(self, WEAPON_SHOTGUN);
            break;
        case SLOT_MAGNUMS:
            if (!inventory_canSelectWeapon(WEAPON_MAGNUMS))
                return false;
            Lara_changeWeapon(self, WEAPON_MAGNUMS);
            break;
        case SLOT_UZIS:
            if (!inventory_canSelectWeapon(WEAPON_UZIS))
                return false;
            Lara_changeWeapon(self, WEAPON_UZIS);
            break;
        case SLOT_MEDIKIT_BIG:
        case SLOT_MEDIKIT_SMALL:
            if (self->health >= LARA_MAX_HEALTH ||
                !inventory_consumeCount(slot, 1))
                return false;
            self->health += (slot == SLOT_MEDIKIT_BIG) ?
                LARA_MAX_HEALTH : (LARA_MAX_HEALTH >> 1);
            if (self->health > LARA_MAX_HEALTH)
                self->health = LARA_MAX_HEALTH;
            gs->gSaveGame.mediUsed = (uint16)(gs->gSaveGame.mediUsed +
                (slot == SLOT_MEDIKIT_BIG ? 2 : 1));
            self->extraL->healthTimer = 40;
            soundPlay(SND_HEALTH, &self->pos);
            return true;
        default: return false;
    }
    return true;
}
void Lara_meshSwapPistols(Lara *self, uint32 weaponMask, uint32 bodyMask)
{
    const WeaponParams *params = &weaponParams[self->extraL->weapon];

    ItemObj_meshSwap(self, ITEM_LARA, bodyMask);
    ItemObj_meshSwap(self, params->modelType, weaponMask);
}

void Lara_meshSwapShotgun(Lara *self, bool armed)
{
#if TR64_HAS_TR2
    /* TR2 arms: the hands use the sequel model; the separate back
     * model is not a swap of Lara's torso. */
    if (TR64_RULE(ARMS)) {
        gs->tr2->backWeapon = armed ? 0 : TR2_MODEL_SHOTGUN;
        ItemObj_meshSwap(self, armed ? TR2_MODEL_SHOTGUN : ITEM_LARA, JOINT_MASK_ARM_R3 | JOINT_MASK_ARM_L3);
        return;
    }
#endif
    const WeaponParams *params = &weaponParams[WEAPON_SHOTGUN];

    if (armed) {
        ItemObj_meshSwap(self, ITEM_LARA, JOINT_MASK_TORSO);
        ItemObj_meshSwap(self, params->modelType, JOINT_MASK_ARM_R3 | JOINT_MASK_ARM_L3);
    } else {
        ItemObj_meshSwap(self, ITEM_LARA, JOINT_MASK_ARM_R3 | JOINT_MASK_ARM_L3);
        ItemObj_meshSwap(self, params->modelType, JOINT_MASK_TORSO);
    }
}
