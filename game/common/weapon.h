/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_WEAPON_H
#define TR64_GAME_WEAPON_H
/* Included by internal.h after the shared types and GameState. */
typedef struct { vec3i center; int32 radius; } Sphere;
#if TR64_HAS_TR2
const char *game_tr2_walk_packed_spheres(unsigned type, PackedFrame key,
                                         const ItemObj *item, Sphere *spheres,
                                         Matrix *stack, bool world, unsigned *joint);
#endif
enum {
    JOINT_MASK_LEG_L1 = 1u << JOINT_LEG_L1,
    JOINT_MASK_LEG_R1 = 1u << JOINT_LEG_R1,
    JOINT_MASK_TORSO = 1u << JOINT_TORSO,
    JOINT_MASK_ARM_R3 = 1u << JOINT_ARM_R3,
    JOINT_MASK_ARM_L3 = 1u << JOINT_ARM_L3,
    JOINT_MASK_HEAD = 1u << JOINT_HEAD
};

enum { WEAPON_SHOTGUN_PELLETS = 6 };

static inline bool game_weapon_aim_eligible(const ExtraInfoLara_Arm *arm, bool action)
{
    return (action && !arm->target) || arm->aim;
}

/* Takes local lvalues. Preserve the TR1 field assignment order and
 * signed arithmetic while the TR2 movement domain wraps its coordinate. */
#define GAME_WEAPON_ORIGIN(from, lara, height) do { \
    (from).pos.x = (lara)->pos.x; \
    (from).pos.y = TR64_RULE(ARMS) ? i32_sub((lara)->pos.y,(height)) : (lara)->pos.y-(height); \
    (from).pos.z = (lara)->pos.z; \
    (from).room = (lara)->room; \
} while (0)

static inline void game_weapon_pose_pair(ExtraInfoLara *extra,uint16 anim,uint16 frame)
{
    extra->armR.animIndex=extra->armL.animIndex=anim;
    extra->armR.frameIndex=extra->armL.frameIndex=frame;
}

enum {
    HAZARD_HIT_NONE, HAZARD_HIT_SPIKES, HAZARD_HIT_BOULDER, HAZARD_HIT_SLAM,
    HAZARD_HIT_SWORD, HAZARD_HIT_THOR, HAZARD_HIT_LIGHTNING, HAZARD_HIT_CEILING,
    HAZARD_HIT_FLAME, HAZARD_HIT_LAVA
};

void matrixSetView(const vec3i *pos, int32 angleX, int32 angleY);
int32 ItemObj_getSpheres(const ItemObj *self, Sphere *spheres, bool world);
uint32 ItemObj_collideSpheres(const ItemObj *self, Lara *lara);
void Lara_updateWeapon(Lara *self);
void Lara_setWeaponState(Lara *self, WeaponState state);
void Lara_meshSwapPistols(Lara *self, uint32 weaponMask, uint32 bodyMask);
void Lara_meshSwapShotgun(Lara *self, bool armed);
void Lara_changeWeapon(Lara *self, Weapon weapon);
bool Lara_useItem(Lara *self, int32 slot);
bool Lara_applyWeaponHit(Lara *self,ItemObj *target,int32 damage,const vec3i *point);
void Lara_restore(Lara *self);
void hazardClearLaraBurn(Lara *lara);
void hazardKillLara(Lara *lara, int32 cause, const vec3i *point);
void hazardDamageLara(Lara *lara, int32 damage, int32 cause, const vec3i *point);
bool hazardLaraIsBurning(const Lara *lara);
void hazardIgniteLara(Lara *lara, uint16 flameIndex);
void hazardUpdateLaraBurn(Lara *lara);

bool game_effect_init(ItemObj *self);
bool game_effect_update(ItemObj *self);
void ItemObj_fxRicochet(Room *room, const vec3i *pos, bool sound);
void ItemObj_fxBlood(ItemObj *self, const vec3i *pos, int16 angleY, int16 speed);
void ItemObj_fxSmoke(ItemObj *self, const vec3i *pos);
ItemObj *reserveExplosionSlot(void);
ItemObj *initExplosionSlot(ItemObj *item, Room *room, const vec3i *pos, int32 angleY);
ItemObj *addExplosion(Room *room, const vec3i *pos, int32 angleY);
ItemObj *MuzzleFlash_add(ItemObj *owner, int32 joint, const vec3i *offset);
bool MuzzleFlash_ownerIsAlive(const ItemObj *self);
void Bullet_setAngle(ItemObj *self, int16 angleX, int16 angleY);
void Bullet_collide(ItemObj *self, Lara *lara, CollisionInfo *cinfo);
#endif
