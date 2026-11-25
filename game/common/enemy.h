/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_ENEMY_H
#define TR64_GAME_ENEMY_H
#define MAX_ENEMIES 8
#define NAV_STEPS 5
#define BOX_OVERLAP_LIST_END 0x8000u
#define NOT_ENEMY (-0x4000)
#define X_MIN(a,b) ((a)<(b)?(a):(b))
#define X_MAX(a,b) ((a)>(b)?(a):(b))
#define X_CLAMP(x,a,b) X_MIN(X_MAX((x),(a)),(b))
enum { MOOD_SLEEP, MOOD_STALK, MOOD_ATTACK, MOOD_ESCAPE };
enum { AGGRESSION_LVL_1=0x400,AGGRESSION_LVL_2=0x2000,AGGRESSION_LVL_3=0x4000,AGGRESSION_LVL_MAX=0x7fff };
typedef tr64_core_boxes_t Box;
static inline const Box *Enemy_boxes(void)
{
    return gs->level.boxes;
}
static inline const tr64_u16_be_t *Enemy_overlaps(void)
{
    return gs->level.overlaps;
}
static inline const tr64_u16_be_t *Enemy_zoneTable(uint32 flipped,uint32 zoneType)
{
    const tr64_u16_be_t *zones=gs->level.zones;
    ASSERT(flipped<2 && zoneType<ZONE_MAX);
    return zones+(flipped*ZONE_MAX+zoneType)*gs->level.boxesCount;
}
static inline uint16 Enemy_boxOverlap(const Box *box)
{
    return gs->boxFlags[box-Enemy_boxes()];
}
void Nav_init(Nav *self,uint32 boxIndex);
void Nav_search(Nav *self,uint16 zone,const tr64_u16_be_t *zones);
vec3i Nav_getWaypoint(Nav *self,uint32 boxIndex,const vec3i *from);
void Enemy_construct(ItemObj *self);
void Enemy_initExtra(ItemObj *self);
bool Enemy_updateBegin(ItemObj *self);
void Enemy_updateEnd(ItemObj *self);
Lara *Enemy_getLara(const vec3i *pos);
void Enemy_explode(ItemObj *self, uint32 jointMask);
void Enemy_setExtra(ItemObj *self, ExtraInfoEnemy * extra);
bool Enemy_enable(ItemObj *self, bool forced);
void Enemy_disable(ItemObj *self);
void Enemy_updateTargetInfo(ItemObj *self);
void Enemy_updateMood(ItemObj *self);
void Enemy_moodSleep(ItemObj *self);
void Enemy_moodStalk(ItemObj *self);
void Enemy_moodAttack(ItemObj *self);
void Enemy_moodEscape(ItemObj *self);
void Enemy_updateNavigation(ItemObj *self);
void Enemy_setNextBox(ItemObj *self, int16 boxIndex);
uint32 Enemy_getMoodWild(ItemObj *self);
uint32 Enemy_getMoodNormal(ItemObj *self);
bool Enemy_checkZone(ItemObj *self);
int32 Enemy_getRandomBox(ItemObj *self);
bool Enemy_checkZoneBox(ItemObj *self, int16 boxIndex);
bool Enemy_checkStalk(ItemObj *self, int16 boxIndex);
bool Enemy_checkEscape(ItemObj *self, int16 boxIndex);
const tr64_u16_be_t * Enemy_getZones(ItemObj *self);
void Enemy_bite(ItemObj *self, int32 damage, const vec3i * offset, int32 joint);
bool Enemy_targetIsVisible(ItemObj *self, int32 maxDist);
void Enemy_shot(ItemObj *self, ItemType bulletType, int32 joint, const vec3i * offset, int16 angleX, int16 angleY);
void Enemy_shotSpread(ItemObj *self, ItemType bulletType, int32 joint, const vec3i * offset, int16 spreadX, int16 spreadY);
void Enemy_updateLocation(ItemObj *self);
void Enemy_activate(ItemObj *self);
void Enemy_hit(ItemObj *self, int32 damage, const vec3i * point, int32 soundId);
void Enemy_collide(ItemObj *self, Lara * lara, CollisionInfo * cinfo);
bool Enemy_humanWaitSelected(ItemObj *self);
bool Enemy_humanFire(ItemObj *self,int32 gunJoint,const vec3i *muzzleOffset,int32 damage);
bool encounterPrecreateChildren(void);
bool encounterResolvePreparedChild(const ItemObj *source, ItemType *expectedType, uint16 *expectedIndex);
void Encounter_construct(ItemObj *self);
void Encounter_activate(ItemObj *self);
ItemObj *Skater_getBoard(ItemObj *self);
ItemObj *Skater_ensureBoard(ItemObj *self);
void Skater_syncBoard(ItemObj *self, ItemObj *board);
void Skater_activate(ItemObj *self);
void Skater_deactivate(ItemObj *self);
void game_sndPlayTrack(int32 track);
enum {
    Doppelganger_ROOM_CENTER_X = 36864,
    Doppelganger_ROOM_CENTER_Z = 60416,
    Doppelganger_DAMAGE_SCALE = 10,
    Doppelganger_FALL_HEIGHT = 1024,
    Doppelganger_LAND_HEIGHT = 128
};
enum {
    Wolf_HIT_MASK = 30543,
    Wolf_DIST_STALK = 3069,
    Wolf_DIST_BITE = 345,
    Wolf_DIST_ATTACK = 1536,
    Wolf_DAMAGE_BITE = 100,
    Wolf_DAMAGE_ATTACK = 50,
    Wolf_ANIM_DEATH = 20,
    Wolf_ANIM_DEATH_RUN = 21,
    Wolf_ANIM_DEATH_JUMP = 22,
    Wolf_STATE_NONE = 0,
    Wolf_STATE_STOP = 1,
    Wolf_STATE_WALK = 2,
    Wolf_STATE_RUN = 3,
    Wolf_STATE_JUMP = 4,
    Wolf_STATE_STALK = 5,
    Wolf_STATE_ATTACK = 6,
    Wolf_STATE_HOWL = 7,
    Wolf_STATE_SLEEP = 8,
    Wolf_STATE_GROWL = 9,
    Wolf_STATE_TURN = 10,
    Wolf_STATE_DEATH = 11,
    Wolf_STATE_BITE = 12
};
enum {
    Bear_HIT_MASK = 147564,
    Bear_DAMAGE_FALL = 200,
    Bear_DAMAGE_BITE = 200,
    Bear_DAMAGE_ATTACK = 400,
    Bear_STATE_NONE = 0,
    Bear_STATE_WALK = 0,
    Bear_STATE_STOP = 1,
    Bear_STATE_HIND = 2,
    Bear_STATE_RUN = 3,
    Bear_STATE_HOWL = 4,
    Bear_STATE_GROWL = 5,
    Bear_STATE_BITE = 6,
    Bear_STATE_ATTACK = 7,
    Bear_STATE_EAT = 8,
    Bear_STATE_DEATH = 9
};
enum {
    Bat_DAMAGE_ATTACK = 2,
    Bat_STATE_NONE = 0,
    Bat_STATE_AWAKE = 1,
    Bat_STATE_FLY = 2,
    Bat_STATE_ATTACK = 3,
    Bat_STATE_CIRCLING = 4,
    Bat_STATE_DEATH = 5
};
enum {
    Crocodile_HIT_MASK = 1020,
    Crocodile_DIST_BITE = 435,
    Crocodile_DIST_TURN = 3072,
    Crocodile_DAMAGE_BITE = 25,
    Crocodile_LIFT_SPEED = 32,
    Crocodile_TURN_FAST = 1092,
    Crocodile_ANIM_DEATH_LAND = 11,
    Crocodile_ANIM_DEATH_WATER = 4,
    Crocodile_STATE_NONE = 0,
    Crocodile_STATE_STOP = 1,
    Crocodile_STATE_RUN = 2,
    Crocodile_STATE_WALK = 3,
    Crocodile_STATE_TURN = 4,
    Crocodile_STATE_BITE = 5,
    Crocodile_STATE_UNUSED = 6,
    Crocodile_STATE_DEATH = 7,
    Crocodile_STATE_WATER_SWIM = 1,
    Crocodile_STATE_WATER_BITE = 2,
    Crocodile_STATE_WATER_DEATH = 3
};
enum {
    Lion_HIT_MASK = 3670118,
    Lion_DIST_ATTACK = 1024,
    Lion_DAMAGE_ATTACK = 150,
    Lion_DAMAGE_BITE = 250,
    Lion_TURN_FAST = 910,
    Lion_TURN_SLOW = 364,
    Lion_ANIM_DEATH_LION = 7,
    Lion_ANIM_DEATH_PUMA = 4,
    Lion_STATE_NONE = 0,
    Lion_STATE_STOP = 1,
    Lion_STATE_WALK = 2,
    Lion_STATE_RUN = 3,
    Lion_STATE_ATTACK = 4,
    Lion_STATE_DEATH = 5,
    Lion_STATE_ROAR = 6,
    Lion_STATE_BITE = 7
};
enum {
    Gorilla_HIT_MASK = 65280,
    Gorilla_DIST_ATTACK = 430,
    Gorilla_DIST_AGGRESSION = 2048,
    Gorilla_DAMAGE_ATTACK = 200,
    Gorilla_TURN_FAST = 910,
    Gorilla_ANIM_DEATH = 7,
    Gorilla_ANIM_CLIMB = 19,
    Gorilla_STATE_NONE = 0,
    Gorilla_STATE_STOP = 1,
    Gorilla_STATE_UNUSED = 2,
    Gorilla_STATE_RUN = 3,
    Gorilla_STATE_ATTACK = 4,
    Gorilla_STATE_DEATH = 5,
    Gorilla_STATE_IDLE_1 = 6,
    Gorilla_STATE_IDLE_2 = 7,
    Gorilla_STATE_LEFT = 8,
    Gorilla_STATE_RIGHT = 9,
    Gorilla_STATE_JUMP = 10,
    Gorilla_STATE_CLIMB = 11,
    Gorilla_FLAG_ATTACK = 1,
    Gorilla_FLAG_LEFT = 2,
    Gorilla_FLAG_RIGHT = 4,
    Gorilla_FLAG_AI_MASK = 7
};
enum {
    Rat_HIT_MASK = 50332047,
    Rat_DIST_BITE = 341,
    Rat_DIST_ATTACK = 1536,
    Rat_DAMAGE_BITE = 20,
    Rat_WAIT_RANDOM = 328,
    Rat_TURN_FAST = 1092,
    Rat_ANIM_DEATH_LAND = 8,
    Rat_ANIM_DEATH_WATER = 2,
    Rat_STATE_NONE = 0,
    Rat_STATE_STOP = 1,
    Rat_STATE_ATTACK = 2,
    Rat_STATE_RUN = 3,
    Rat_STATE_BITE = 4,
    Rat_STATE_DEATH = 5,
    Rat_STATE_WAIT = 6,
    Rat_STATE_WATER_SWIM = 1,
    Rat_STATE_WATER_BITE = 2,
    Rat_STATE_WATER_DEATH = 3
};
enum {
    Rex_HIT_MASK = 12288,
    Rex_DIST_BITE = 1500,
    Rex_DIST_WALK = 4096,
    Rex_DIST_RUN = 5120,
    Rex_DAMAGE_RUN = 10,
    Rex_DAMAGE_WALK = 1,
    Rex_DAMAGE_FATAL = 1000,
    Rex_STATE_NONE = 0,
    Rex_STATE_STOP = 1,
    Rex_STATE_WALK = 2,
    Rex_STATE_RUN = 3,
    Rex_STATE_UNUSED = 4,
    Rex_STATE_DEATH = 5,
    Rex_STATE_ROAR = 6,
    Rex_STATE_BITE = 7,
    Rex_STATE_FATAL = 8
};
enum {
    Raptor_HIT_MASK = 16743424,
    Raptor_DIST_BITE = 680,
    Raptor_DIST_ATTACK = 1536,
    Raptor_DAMAGE_ATTACK = 100,
    Raptor_ANIM_DEATH_1 = 9,
    Raptor_ANIM_DEATH_2 = 10,
    Raptor_STATE_NONE = 0,
    Raptor_STATE_DEATH = 0,
    Raptor_STATE_STOP = 1,
    Raptor_STATE_WALK = 2,
    Raptor_STATE_RUN = 3,
    Raptor_STATE_ATTACK_1 = 4,
    Raptor_STATE_UNUSED = 5,
    Raptor_STATE_ROAR = 6,
    Raptor_STATE_ATTACK_2 = 7,
    Raptor_STATE_BITE = 8
};
enum {
    Mutant_HIT_MASK = 1656,
    Mutant_DIST_ATTACK_1 = 600,
    Mutant_DIST_ATTACK_2 = 2560,
    Mutant_DIST_ATTACK_3 = 300,
    Mutant_DIST_SHOT = 3840,
    Mutant_DIST_STALK = 4608,
    Mutant_MAX_SHOT_DIST = 65536,
    Mutant_DAMAGE_ATTACK_1 = 150,
    Mutant_DAMAGE_ATTACK_2 = 100,
    Mutant_DAMAGE_ATTACK_3 = 200,
    Mutant_JOINT_ATTACK = 10,
    Mutant_JOINT_BULLET_1 = 9,
    Mutant_JOINT_BULLET_2 = 14,
    Mutant_TURN_FAST = 1092,
    Mutant_TURN_SLOW = 364,
    Mutant_LIFT_SPEED = 16,
    Mutant_STATE_NONE = 0,
    Mutant_STATE_STOP = 1,
    Mutant_STATE_WALK = 2,
    Mutant_STATE_RUN = 3,
    Mutant_STATE_ATTACK_1 = 4,
    Mutant_STATE_DEATH = 5,
    Mutant_STATE_LOOKING = 6,
    Mutant_STATE_ATTACK_2 = 7,
    Mutant_STATE_ATTACK_3 = 8,
    Mutant_STATE_AIM_1 = 9,
    Mutant_STATE_AIM_2 = 10,
    Mutant_STATE_FIRE = 11,
    Mutant_STATE_IDLE = 12,
    Mutant_STATE_FLY = 13,
    Mutant_FLAG_FLY = 1,
    Mutant_FLAG_BULLET_1 = 2,
    Mutant_FLAG_BULLET_2 = 4,
    Mutant_FLAG_DEATH_STARTED = 8,
    Mutant_MODE_FLY_RANGED = 0,
    Mutant_MODE_GROUND_RANGED = 1,
    Mutant_MODE_GROUND_MELEE = 2
};
enum {
    Centaur_HIT_MASK = 197017,
    Centaur_DIST_RUN = 1536,
    Centaur_DIST_SHOT = 65536,
    Centaur_DAMAGE_MELEE = 200,
    Centaur_RUN_IDLE_CHANCE = 96,
    Centaur_TURN_FAST = 728,
    Centaur_SHOT_SPREAD = 273,
    Centaur_ANIM_DEATH = 8,
    Centaur_JOINT_MELEE = 5,
    Centaur_JOINT_FIRE = 13,
    Centaur_STATE_NONE = 0,
    Centaur_STATE_STOP = 1,
    Centaur_STATE_FIRE = 2,
    Centaur_STATE_RUN = 3,
    Centaur_STATE_AIM = 4,
    Centaur_STATE_DEATH = 5,
    Centaur_STATE_IDLE = 6
};
enum {
    Mummy_STATE_NONE = 0,
    Mummy_STATE_IDLE = 1,
    Mummy_STATE_FALL = 2
};
enum {
    Human_STATE_NONE = 0,
    Human_STATE_STOP = 1,
    Human_STATE_WALK = 2,
    Human_STATE_RUN = 3,
    Human_STATE_AIM = 4,
    Human_STATE_DEATH = 5,
    Human_STATE_WAIT = 6,
    Human_STATE_FIRE = 7,
    Human_DIST_WALK = 3072,
    Human_DIST_SHOT = 7168,
    Human_WAIT_CHANCE = 328,
    Human_TURN_WALK = 546,
    Human_TURN_RUN = 1092
};
enum {
    Larson_DAMAGE = 50,
    Larson_ANIM_DEATH = 15,
    Larson_JOINT_GUN = 14
};
enum {
    Pierre_DAMAGE = 25,
    Pierre_MIN_HEALTH = 40,
    Pierre_RETREAT_FRAMES = 12,
    Pierre_ANIM_DEATH = 12,
    Pierre_JOINT_GUN = 11
};
enum {
    Skater_STATE_STOP = 0,
    Skater_STATE_STAND_FIRE = 1,
    Skater_STATE_MOVE = 2,
    Skater_STATE_STEP = 3,
    Skater_STATE_MOVE_FIRE = 4,
    Skater_STATE_DEATH = 5,
    Skater_DAMAGE_STAND = 50,
    Skater_DAMAGE_MOVE = 40,
    Skater_DIST_MIN = 2560,
    Skater_DIST_MAX = 4096,
    Skater_ANIM_DEATH = 13,
    Skater_JOINT_GUN_LEFT = 7,
    Skater_JOINT_GUN_RIGHT = 4,
    Skater_TRACK_DAMAGE = 56
};
enum {
    Cowboy_DAMAGE = 70,
    Cowboy_ANIM_DEATH = 7,
    Cowboy_JOINT_GUN_RIGHT = 8,
    Cowboy_JOINT_GUN_LEFT = 5
};
enum {
    MrT_DAMAGE = 150,
    MrT_DIST_WALK = 4096,
    MrT_ANIM_DEATH = 14,
    MrT_JOINT_GUN = 9
};
enum {
    Natla_STATE_NONE = 0,
    Natla_STATE_STOP = 1,
    Natla_STATE_FLY = 2,
    Natla_STATE_RUN = 3,
    Natla_STATE_AIM = 4,
    Natla_STATE_FAINT = 5,
    Natla_STATE_FIRE = 6,
    Natla_STATE_FALL = 7,
    Natla_STATE_STAND = 8,
    Natla_STATE_DEATH = 9,
    Natla_HALF_HEALTH = 200,
    Natla_FAINT_HEALTH = -8192,
    Natla_FAINT_FRAMES = 480,
    Natla_FLY_SHOT_FRAMES = 30,
    Natla_GROUND_SHOT_FRAMES = 20,
    Natla_DIST_SHOT = 7168,
    Natla_LIFT_SPEED = 17,
    Natla_FIRE_ANGLE = 5460,
    Natla_SPREAD_SMALL = 273,
    Natla_SPREAD_WIDE = 4550,
    Natla_JOINT_GUN = 4,
    Natla_ANIM_FLY_LOOP = 5,
    Natla_ANIM_FALL = 6,
    Natla_ANIM_FAINT = 8,
    Natla_ANIM_STAND_GETUP = 10,
    Natla_ANIM_DEATH = 13,
    Natla_ANIM_STAND_IDLE = 14,
    Natla_CLOCK_FLY = 32768,
    Natla_CLOCK_FAINT = 16384,
    Natla_CLOCK_MASK = 16383
};
enum {
    Adam_HIT_MASK_HAND = 67076096,
    Adam_HIT_MASK_HANDS = 67108848,
    Adam_DIST_ATTACK = 2600,
    Adam_DIST_FATAL = 2250,
    Adam_DAMAGE_WALK = 5,
    Adam_DAMAGE_ATTACK = 500,
    Adam_DAMAGE_FATAL = 1000,
    Adam_ANIM_DEATH = 13,
    Adam_STATE_NONE = 0,
    Adam_STATE_STOP = 1,
    Adam_STATE_TURN_LEFT = 2,
    Adam_STATE_TURN_RIGHT = 3,
    Adam_STATE_ATTACK_1 = 4,
    Adam_STATE_ATTACK_2 = 5,
    Adam_STATE_ATTACK_3 = 6,
    Adam_STATE_WALK = 7,
    Adam_STATE_BORN = 8,
    Adam_STATE_FALL = 9,
    Adam_STATE_UNUSED = 10,
    Adam_STATE_FATAL = 11
};
#endif
