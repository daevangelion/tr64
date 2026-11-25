/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h and inventory.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/common.h and inventory.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_TYPES_H
#define TR64_GAME_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "tr64/level.h"
#include "tr64/game.h"
typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef tr64_u16_be_t FloorData;
typedef struct
{
    int16 x, y, z;
} vec3s;
typedef struct
{
    int32 x, y, z;
} vec3i;
typedef struct
{
    int16 minX, maxX, minY, maxY, minZ, maxZ;
} AABBs;
typedef struct
{
    int32 minX, maxX, minY, maxY, minZ, maxZ;
} AABBi;
typedef struct
{
    int32 e00, e01, e02, e03, e10, e11, e12, e13, e20, e21, e22, e23;
} Matrix;
typedef struct ItemObj ItemObj;
typedef ItemObj Lara;
typedef ItemObj SpriteEffect;
typedef ItemObj Bubble;
typedef struct Room Room;
typedef struct
{
    Room *room;
    vec3i pos;
} Location;
typedef struct
{
    uint16 floorIndex, boxIndex;
    uint8 roomBelow;
    int8 floor;
    uint8 roomAbove;
    int8 ceiling;
} Sector;
typedef tr64_core_rooms_t RoomInfo;
typedef tr64_core_animations_t Anim;
typedef tr64_core_states_t AnimState;
typedef tr64_core_ranges_t AnimRange;
typedef tr64_core_cameras_t FixedCamera;
typedef tr64_core_sound_details_t SoundInfo;
/* Bindings and linked item heads, not a copy of immutable room metadata. */
struct Room
{
    ItemObj *firstItem;
    const RoomInfo *info;
    Sector *sectors;
    uint32 source;
};
typedef struct
{
#if TR64_HAS_TR2 || TR64_HAS_TR3
    uint16 type;
#else
    uint8 type;
#endif
    int8 count;
    uint16 start, nodeIndex, animIndex;
} Model;
typedef struct
{
    Location view, target;
    int32 targetDist;
    vec3s targetAngle, angle;
    ItemObj *laraItem, *lastItem, *lookAtItem;
    int32 speed, timer, index, lastIndex;
    int16 bounce;
    int32 mode;
    bool lastFixed, center;
#if TR64_HAS_TR2 || TR64_HAS_TR3
    int32 displayY; /* Sequel view clearance; simulation and trace keep raw Y. */
    int32 followRadius; /* Horizontal radius of the last sequel Follow projection. */
    bool fullSlopes;
#endif
} Camera;
typedef struct
{
    uint16 boxIndex, weight, end, next;
} Nav_Cell;
typedef struct
{
    Nav_Cell *cells;
    uint32 cellsCount, zoneType, weight, endBox, nextBox, headBox, tailBox;
    int32 stepHeight, dropHeight, vSpeed;
    uint32 mask;
    vec3i pos;
} Nav;
typedef struct
{
    int16 rotHead, rotNeck, maxTurn, _reserved;
    ItemObj *target; /* nearest (Doppelganger retains the target it woke with) */
    uint32 contact[TR64_GAME_PLAYERS];
    ItemObj *enemy;
    Nav nav;
} ExtraInfoEnemy;
typedef struct
{
    ItemObj *target;
    vec3i waypoint, pos;
    uint32 dist;
    int16 angle, rotHead, tilt, turn;
    uint16 boxIndex, boxIndexTarget, zoneIndex, zoneIndexTarget;
    bool front, behind, canAttack;
} TargetInfo;
typedef struct
{
    Location at;
    int16 yaw;
    uint8 water;
} JoinPose;
typedef struct
{
    Lara *actor;
    uint16 ticks;
} RetiredAction;
typedef struct { uint16 anim, frame; uint8 state, goal; } InteractionAnim;
typedef enum
{
    GAME_ITEM_CONSTRUCT,
    GAME_ITEM_ACTIVATE,
    GAME_ITEM_UPDATE,
    GAME_ITEM_COLLIDE,
    GAME_ITEM_HIT,
    GAME_ITEM_GEOMETRY
} GameItemOp;
/* Synchronous resident packet. Unit code and rodata never escape the call. */
typedef struct
{
    GameItemOp op;
    ItemObj *self, *lara;
    int32 damage, soundId;
    const vec3i *point;
    int32 x, y, z, *floor, *ceiling;
    uint32 result;
} GameItemCall;
typedef struct
{
    uint16 burnTicks, damageCooldown;
    int16 flameItemIndex;
    uint8 flags, reserved;
} LaraHazardState;
typedef struct { uint8 mask, flags; } FlipMapState;
typedef struct
{
    Lara *lara;
    int16 id;
    uint16 timer;
    int16 cameraBounce;
} EffectState;
typedef struct
{
    vec3s angle;
} ExtraInfoLara_Head;
typedef struct
{
    vec3s angle;
} ExtraInfoLara_Torso;
typedef struct
{
    int16 timer, angle, offset, intensity;
} ExtraInfoLara_Arm_Flash;
typedef struct
{
    vec3s angle, angleAim;
    uint16 animIndex, frameIndex;
    ExtraInfoLara_Arm_Flash flash;
    ItemObj *target;
    bool aim, useBasis;
} ExtraInfoLara_Arm;
typedef struct
{
    int16 swimTimer;
    uint8 weaponState, vSpeedHack;
    int16 moveAngle, hitFrame;
    int8 hitTimer, hitQuadrant;
    uint8 weapon, goalWeapon, hazardCause;
    ExtraInfoLara_Head head;
    ExtraInfoLara_Torso torso;
    ExtraInfoLara_Arm armR, armL;
    Camera camera;
    uint16 meshes[15];
    Nav nav;
    uint16 lastInput;
    int8 healthTimer;
    bool dozy;
} ExtraInfoLara;
/* The source's anonymous unions use their canonical member here: oxygen is
 * timer, mood is input, gymTimer is aggression, and effect tick is animIndex.
 * There is one value, not duplicated alias storage. */
struct ItemObj
{
    Room *room;
    vec3i pos;
    vec3s angle;
    uint16 flags;
    int16 vSpeed, hSpeed;
    uint16 animIndex, frameIndex;
    uint8 state, nextState, goalState, waterState;
    uint16 headOffset, aggression;
    int16 health, timer;
    uint16 input;
    int16 turnSpeed;
#if TR64_HAS_TR2 || TR64_HAS_TR3
    uint16 type; /* PC TR2 placed object IDs extend past 255. */
    uint8 intensity;
#else
    uint8 type, intensity;
#endif
    int16 roomFloor;
    uint32 hitMask, visibleMask;
    ExtraInfoLara *extraL;
    ExtraInfoEnemy *extraE;
    ItemObj *nextItem, *nextActive;
};
typedef struct
{
    uint32 version, dataSize;
    uint16 itemCount;
    uint8 level;
    int8 track;
    uint8 secrets, pickups;
    uint32 time, distance, randSeedLogic, randSeedDraw;
    uint16 mediUsed, ammoUsed, kills, flipped;
    uint8 tracks[64];
    uint16 invSlots[64];
} SaveGame;
typedef struct
{
    int32 floor, ceiling, slantType;
} CollisionInfo_Side;
typedef struct
{
    const FloorData *trigger;
    CollisionInfo_Side m, f, l, r;
    int32 radius, gapPos, gapNeg, gapCeiling;
    vec3i offset, pos;
    int16 angle;
    uint16 quadrant;
    int32 type;
    int8 slantX, slantZ;
    bool enemyPush, enemyHit, staticHit, stopOnSlant, stopOnLava;
#if TR64_HAS_TR2 || TR64_HAS_TR3
    bool allowSteepDrop;
#endif
} CollisionInfo;
typedef tr64_game_menu_option_t GameInventoryOption;
typedef tr64_game_settings_t GameSettings;
typedef struct
{
    int32 counts[26], numKeys, useSlot, state, page;
    int32 timer, nextState, frameIndex, frameTarget;
    int32 height,radius,selDist;
    int16 pitch,rot,rotItem,selRotPre,selRotX,selRotY;
    ItemObj *lara;
    int32 heightTarget,heightInc,radiusTarget,radiusInc;
    int32 pitchTarget,pitchInc,rotTarget,rotInc,rotItemInc;
    int32 selDistTarget,selDistInc,selRotPreTarget,selRotPreInc;
    int32 selRotXTarget,selRotXInc,selRotYTarget,selRotYInc;
    int32 itemsList[26],itemsCount,itemIndex,nextIndex;
    int32 optionIndex,optionsCount,optionsWidth,optionsHeight;
    uint32 itemVisMask;
    int32 passportPage,statsNextLevel;
    GameInventoryOption options[32];
    uint32 backgroundKind;
} GameInventory;
/* Source enum parameters have the source's 32-bit enum representation. */
typedef int32 InputState;
typedef int32 ItemType;
typedef int32 CameraMode;
typedef int32 WeaponState;
typedef int32 CollisionType;
typedef int32 CollisionInfo_SideType;
typedef int32 SlantType;
typedef int32 ClimbState;
typedef int32 Weapon;
static inline int32 wrap_i32(uint32 n)
{
    return n <= INT32_MAX ? (int32)n : -(int32)(~n) - 1;
}
static inline int16 wrap_i16(uint32 n)
{
    return (int16)((int32)(n & 32767u) - (int32)(n & 32768u));
}
static inline vec3i vec3i_create(int32 x, int32 y, int32 z)
{
    vec3i r = {x, y, z};
    return r;
}
static inline vec3s vec3s_create(int16 x, int16 y, int16 z)
{
    vec3s r = {x, y, z};
    return r;
}
static inline vec3i vec3i_add(vec3i a, vec3i b)
{
    return vec3i_create(wrap_i32((uint32)a.x + (uint32)b.x), wrap_i32((uint32)a.y + (uint32)b.y),
                        wrap_i32((uint32)a.z + (uint32)b.z));
}
static inline vec3i vec3i_sub(vec3i a, vec3i b)
{
    return vec3i_create(wrap_i32((uint32)a.x - (uint32)b.x), wrap_i32((uint32)a.y - (uint32)b.y),
                        wrap_i32((uint32)a.z - (uint32)b.z));
}
static inline vec3i vec3i_scale(vec3i a, int32 b)
{
    return vec3i_create(wrap_i32((uint32)a.x * (uint32)b), wrap_i32((uint32)a.y * (uint32)b),
                        wrap_i32((uint32)a.z * (uint32)b));
}
static inline bool vec3i_equal(vec3i a, vec3i b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static inline AABBi AABBi_from_short(AABBs a)
{
    AABBi b = {a.minX, a.maxX, a.minY, a.maxY, a.minZ, a.maxZ};
    return b;
}
#endif
