/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/common.h, item.h, room.h, lara.h, camera.h and game.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_INTERNAL_H
#define TR64_GAME_INTERNAL_H
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "constants.h"
#include "tr64/eng.h"
#include "tr2.h"
#define ASSERT(condition)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
            sys_error("game assertion %s:%d: %s", __FILE__, __LINE__, #condition);                 \
    } while (0)
enum
{
    IN_LEFT = 2,
    IN_RIGHT = 4,
    IN_UP = 8,
    IN_DOWN = 16,
    IN_JUMP = 32,
    IN_WALK = 64,
    IN_ACTION = 128,
    IN_WEAPON = 256,
    IN_LOOK = 512,
    IN_START = 1024,
    IN_SELECT = 2048
};
enum
{
    ITEM_LARA = 0,
    ITEM_LARA_PISTOLS = 1,
    ITEM_LARA_SHOTGUN = 2,
    ITEM_LARA_MAGNUMS = 3,
    ITEM_LARA_UZIS = 4,
    ITEM_LARA_SPEC = 5,
    ITEM_DOPPELGANGER = 6,
    ITEM_WOLF = 7,
    ITEM_BEAR = 8,
    ITEM_BAT = 9,
    ITEM_CROCODILE_LAND = 10,
    ITEM_CROCODILE_WATER = 11,
    ITEM_LION_MALE = 12,
    ITEM_LION_FEMALE = 13,
    ITEM_PUMA = 14,
    ITEM_GORILLA = 15,
    ITEM_RAT_LAND = 16,
    ITEM_RAT_WATER = 17,
    ITEM_REX = 18,
    ITEM_RAPTOR = 19,
    ITEM_MUTANT_1 = 20,
    ITEM_MUTANT_2 = 21,
    ITEM_MUTANT_3 = 22,
    ITEM_CENTAUR = 23,
    ITEM_MUMMY = 24,
    ITEM_UNUSED_1 = 25,
    ITEM_UNUSED_2 = 26,
    ITEM_LARSON = 27,
    ITEM_PIERRE = 28,
    ITEM_SKATEBOARD = 29,
    ITEM_SKATER = 30,
    ITEM_COWBOY = 31,
    ITEM_MR_T = 32,
    ITEM_NATLA = 33,
    ITEM_ADAM = 34,
    ITEM_TRAP_FLOOR = 35,
    ITEM_TRAP_SWING_BLADE = 36,
    ITEM_TRAP_SPIKES = 37,
    ITEM_TRAP_BOULDER = 38,
    ITEM_DART = 39,
    ITEM_TRAP_DART_EMITTER = 40,
    ITEM_DRAWBRIDGE = 41,
    ITEM_TRAP_SLAM = 42,
    ITEM_TRAP_SWORD = 43,
    ITEM_HAMMER_HANDLE = 44,
    ITEM_HAMMER_BLOCK = 45,
    ITEM_LIGHTNING = 46,
    ITEM_MOVING_OBJECT = 47,
    ITEM_BLOCK_1 = 48,
    ITEM_BLOCK_2 = 49,
    ITEM_BLOCK_3 = 50,
    ITEM_BLOCK_4 = 51,
    ITEM_MOVING_BLOCK = 52,
    ITEM_TRAP_CEILING = 53,
    ITEM_TRAP_CEILING_2 = 54,
    ITEM_SWITCH = 55,
    ITEM_SWITCH_WATER = 56,
    ITEM_DOOR_1 = 57,
    ITEM_DOOR_2 = 58,
    ITEM_DOOR_3 = 59,
    ITEM_DOOR_4 = 60,
    ITEM_DOOR_5 = 61,
    ITEM_DOOR_6 = 62,
    ITEM_DOOR_7 = 63,
    ITEM_DOOR_8 = 64,
    ITEM_TRAP_DOOR_1 = 65,
    ITEM_TRAP_DOOR_2 = 66,
    ITEM_TRAP_DOOR_LOD = 67,
    ITEM_BRIDGE_FLAT = 68,
    ITEM_BRIDGE_TILT_1 = 69,
    ITEM_BRIDGE_TILT_2 = 70,
    ITEM_INV_PASSPORT = 71,
    ITEM_INV_COMPASS = 72,
    ITEM_INV_HOME = 73,
    ITEM_GEARS_1 = 74,
    ITEM_GEARS_2 = 75,
    ITEM_GEARS_3 = 76,
    ITEM_CUT_1 = 77,
    ITEM_CUT_2 = 78,
    ITEM_CUT_3 = 79,
    ITEM_CUT_4 = 80,
    ITEM_INV_PASSPORT_CLOSED = 81,
    ITEM_INV_MAP = 82,
    ITEM_CRYSTAL = 83,
    ITEM_PISTOLS = 84,
    ITEM_SHOTGUN = 85,
    ITEM_MAGNUMS = 86,
    ITEM_UZIS = 87,
    ITEM_AMMO_PISTOLS = 88,
    ITEM_AMMO_SHOTGUN = 89,
    ITEM_AMMO_MAGNUMS = 90,
    ITEM_AMMO_UZIS = 91,
    ITEM_EXPLOSIVE = 92,
    ITEM_MEDIKIT_SMALL = 93,
    ITEM_MEDIKIT_BIG = 94,
    ITEM_INV_DETAIL = 95,
    ITEM_INV_SOUND = 96,
    ITEM_INV_CONTROLS = 97,
    ITEM_INV_GAMMA = 98,
    ITEM_INV_PISTOLS = 99,
    ITEM_INV_SHOTGUN = 100,
    ITEM_INV_MAGNUMS = 101,
    ITEM_INV_UZIS = 102,
    ITEM_INV_AMMO_PISTOLS = 103,
    ITEM_INV_AMMO_SHOTGUN = 104,
    ITEM_INV_AMMO_MAGNUMS = 105,
    ITEM_INV_AMMO_UZIS = 106,
    ITEM_INV_EXPLOSIVE = 107,
    ITEM_INV_MEDIKIT_SMALL = 108,
    ITEM_INV_MEDIKIT_BIG = 109,
    ITEM_PUZZLE_1 = 110,
    ITEM_PUZZLE_2 = 111,
    ITEM_PUZZLE_3 = 112,
    ITEM_PUZZLE_4 = 113,
    ITEM_INV_PUZZLE_1 = 114,
    ITEM_INV_PUZZLE_2 = 115,
    ITEM_INV_PUZZLE_3 = 116,
    ITEM_INV_PUZZLE_4 = 117,
    ITEM_PUZZLEHOLE_1 = 118,
    ITEM_PUZZLEHOLE_2 = 119,
    ITEM_PUZZLEHOLE_3 = 120,
    ITEM_PUZZLEHOLE_4 = 121,
    ITEM_PUZZLEHOLE_DONE_1 = 122,
    ITEM_PUZZLEHOLE_DONE_2 = 123,
    ITEM_PUZZLEHOLE_DONE_3 = 124,
    ITEM_PUZZLEHOLE_DONE_4 = 125,
    ITEM_LEADBAR = 126,
    ITEM_INV_LEADBAR = 127,
    ITEM_MIDAS_HAND = 128,
    ITEM_KEY_ITEM_1 = 129,
    ITEM_KEY_ITEM_2 = 130,
    ITEM_KEY_ITEM_3 = 131,
    ITEM_KEY_ITEM_4 = 132,
    ITEM_INV_KEY_ITEM_1 = 133,
    ITEM_INV_KEY_ITEM_2 = 134,
    ITEM_INV_KEY_ITEM_3 = 135,
    ITEM_INV_KEY_ITEM_4 = 136,
    ITEM_KEYHOLE_1 = 137,
    ITEM_KEYHOLE_2 = 138,
    ITEM_KEYHOLE_3 = 139,
    ITEM_KEYHOLE_4 = 140,
    ITEM_UNUSED_5 = 141,
    ITEM_UNUSED_6 = 142,
    ITEM_SCION_PICKUP_QUALOPEC = 143,
    ITEM_SCION_PICKUP_DROP = 144,
    ITEM_SCION_TARGET = 145,
    ITEM_SCION_PICKUP_HOLDER = 146,
    ITEM_SCION_HOLDER = 147,
    ITEM_UNUSED_7 = 148,
    ITEM_UNUSED_8 = 149,
    ITEM_INV_SCION = 150,
    ITEM_EXPLOSION = 151,
    ITEM_UNUSED_9 = 152,
    ITEM_SPLASH = 153,
    ITEM_UNUSED_10 = 154,
    ITEM_BUBBLE = 155,
    ITEM_UNUSED_11 = 156,
    ITEM_UNUSED_12 = 157,
    ITEM_BLOOD = 158,
    ITEM_UNUSED_13 = 159,
    ITEM_SMOKE = 160,
    ITEM_CENTAUR_STATUE = 161,
    ITEM_CABIN = 162,
    ITEM_MUTANT_EGG_SMALL = 163,
    ITEM_RICOCHET = 164,
    ITEM_SPARKLES = 165,
    ITEM_MUZZLE_FLASH = 166,
    ITEM_UNUSED_14 = 167,
    ITEM_UNUSED_15 = 168,
    ITEM_VIEW_TARGET = 169,
    ITEM_WATERFALL = 170,
    ITEM_NATLA_BULLET = 171,
    ITEM_MUTANT_BULLET = 172,
    ITEM_CENTAUR_BULLET = 173,
    ITEM_UNUSED_16 = 174,
    ITEM_UNUSED_17 = 175,
    ITEM_LAVA_PARTICLE = 176,
    ITEM_LAVA_EMITTER = 177,
    ITEM_FLAME = 178,
    ITEM_FLAME_EMITTER = 179,
    ITEM_TRAP_LAVA = 180,
    ITEM_MUTANT_EGG_BIG = 181,
    ITEM_BOAT = 182,
    ITEM_EARTHQUAKE = 183,
    ITEM_UNUSED_18 = 184,
    ITEM_UNUSED_19 = 185,
    ITEM_UNUSED_20 = 186,
    ITEM_UNUSED_21 = 187,
    ITEM_UNUSED_22 = 188,
    ITEM_LARA_BRAID = 189,
    ITEM_GLYPHS = 190,
    TR1_ITEM_MAX = 191,
    ITEM_MAX = 191
};
enum
{
    CAMERA_MODE_FREE = 0,
    CAMERA_MODE_FOLLOW = 1,
    CAMERA_MODE_COMBAT = 2,
    CAMERA_MODE_LOOK = 3,
    CAMERA_MODE_FIXED = 4,
    CAMERA_MODE_OBJECT = 5,
    CAMERA_MODE_CUTSCENE = 6
};
enum
{
    ZONE_GROUND_1 = 0,
    ZONE_GROUND_2 = 1,
    ZONE_FLY = 2,
    ZONE_MAX = 3
};
enum
{
    WEAPON_STATE_FREE = 0,
    WEAPON_STATE_BUSY = 1,
    WEAPON_STATE_DRAW = 2,
    WEAPON_STATE_HOLSTER = 3,
    WEAPON_STATE_READY = 4
};
enum
{
    WEAPON_PISTOLS = 0,
    WEAPON_MAGNUMS = 1,
    WEAPON_UZIS = 2,
    WEAPON_SHOTGUN = 3,
    WEAPON_NONE = 4,
    WEAPON_MAX = 5
};
enum
{
    JOINT_HIPS = 0,
    JOINT_LEG_L1 = 1,
    JOINT_LEG_L2 = 2,
    JOINT_LEG_L3 = 3,
    JOINT_LEG_R1 = 4,
    JOINT_LEG_R2 = 5,
    JOINT_LEG_R3 = 6,
    JOINT_TORSO = 7,
    JOINT_ARM_R1 = 8,
    JOINT_ARM_R2 = 9,
    JOINT_ARM_R3 = 10,
    JOINT_ARM_L1 = 11,
    JOINT_ARM_L2 = 12,
    JOINT_ARM_L3 = 13,
    JOINT_HEAD = 14,
    JOINT_MAX = 15
};
enum
{
    FLOOR_TYPE_NONE = 0,
    FLOOR_TYPE_PORTAL = 1,
    FLOOR_TYPE_FLOOR = 2,
    FLOOR_TYPE_CEILING = 3,
    FLOOR_TYPE_TRIGGER = 4,
    FLOOR_TYPE_LAVA = 5,
    FLOOR_TYPE_CLIMB = 6
};
enum
{
    TRIGGER_TYPE_ACTIVATE = 0,
    TRIGGER_TYPE_PAD = 1,
    TRIGGER_TYPE_SWITCH = 2,
    TRIGGER_TYPE_KEY = 3,
    TRIGGER_TYPE_PICKUP = 4,
    TRIGGER_TYPE_OBJECT = 5,
    TRIGGER_TYPE_ANTIPAD = 6,
    TRIGGER_TYPE_COMBAT = 7,
    TRIGGER_TYPE_DUMMY = 8
};
enum
{
    TRIGGER_ACTION_ACTIVATE_OBJECT = 0,
    TRIGGER_ACTION_ACTIVATE_CAMERA = 1,
    TRIGGER_ACTION_FLOW = 2,
    TRIGGER_ACTION_FLIP = 3,
    TRIGGER_ACTION_FLIP_ON = 4,
    TRIGGER_ACTION_FLIP_OFF = 5,
    TRIGGER_ACTION_CAMERA_TARGET = 6,
    TRIGGER_ACTION_END = 7,
    TRIGGER_ACTION_SOUNDTRACK = 8,
    TRIGGER_ACTION_EFFECT = 9,
    TRIGGER_ACTION_SECRET = 10,
    TRIGGER_ACTION_CLEAR_BODIES = 11,
    TRIGGER_ACTION_FLYBY = 12,
    TRIGGER_ACTION_CUTSCENE = 13
};
enum
{
    SLANT_NONE = 0,
    SLANT_LOW = 1,
    SLANT_HIGH = 2
};
enum
{
    WATER_STATE_ABOVE = 0,
    WATER_STATE_WADE = 1,
    WATER_STATE_SURFACE = 2,
    WATER_STATE_UNDER = 3
};
enum
{
    ANIM_CMD_NONE = 0,
    ANIM_CMD_OFFSET = 1,
    ANIM_CMD_JUMP = 2,
    ANIM_CMD_EMPTY = 3,
    ANIM_CMD_KILL = 4,
    ANIM_CMD_SOUND = 5,
    ANIM_CMD_EFFECT = 6
};
enum
{
    FX_NONE = -1,
    FX_ROTATE_180 = 0,
    FX_FLOOR_SHAKE = 1,
    FX_LARA_NORMAL = 2,
    FX_LARA_BUBBLES = 3,
    FX_FINISH_LEVEL = 4,
    FX_EARTHQUAKE = 5,
    FX_FLOOD = 6,
    FX_UNK1 = 7,
    FX_STAIRS2SLOPE = 8,
    FX_UNK3 = 9,
    FX_UNK4 = 10,
    FX_EXPLOSION = 11,
    FX_LARA_HANDSFREE = 12,
    FX_FLIP_MAP = 13,
    FX_DRAW_RIGHTGUN = 14,
    FX_DRAW_LEFTGUN = 15,
    FX_SHOT_RIGHTGUN = 16,
    FX_SHOT_LEFTGUN = 17,
    FX_MESH_SWAP_1 = 18,
    FX_MESH_SWAP_2 = 19,
    FX_MESH_SWAP_3 = 20,
    FX_INV_ON = 21,
    FX_INV_OFF = 22,
    FX_DYN_ON = 23,
    FX_DYN_OFF = 24,
    FX_STATUE_FX = 25,
    FX_RESET_HAIR = 26,
    FX_BOILER_FX = 27,
    FX_ASSAULT_RESET = 28,
    FX_ASSAULT_STOP = 29,
    FX_ASSAULT_START = 30,
    FX_ASSAULT_FINISH = 31,
    FX_FOOTPRINT = 32,
    FX_TR1_FLICKER = 16
};
enum
{
    UNIQUE = 0,
    REPLAY = 1,
    LOOP = 2
};
enum
{
    SND_NO = 2,
    SND_LANDING = 4,
    SND_DRAW = 6,
    SND_HOLSTER = 7,
    SND_PISTOLS_SHOT = 8,
    SND_SHOTGUN_RELOAD = 9,
    SND_RICOCHET = 10,
    SND_HIT_BEAR = 16,
    SND_HIT_WOLF = 20,
    SND_SCREAM = 30,
    SND_HIT = 27,
    SND_DAMAGE = 31,
    SND_SPLASH = 33,
    SND_BUBBLE = 37,
    SND_UZIS_SHOT = 43,
    SND_MAGNUMS_SHOT = 44,
    SND_SHOTGUN_SHOT = 45,
    SND_EMPTY = 48,
    SND_HIT_UNDERWATER = 50,
    SND_UNDERWATER = 60,
    SND_BOULDER = 70,
    SND_FLOOD = 81,
    SND_HIT_LION = 85,
    SND_HIT_RAT = 95,
    SND_LIGHTNING = 98,
    SND_ROCK = 99,
    SND_SWORD = 103,
    SND_EXPLOSION = 104,
    SND_INV_SPIN = 108,
    SND_INV_HOME = 109,
    SND_INV_CONTROLS = 110,
    SND_INV_SHOW = 111,
    SND_INV_HIDE = 112,
    SND_INV_COMPASS = 113,
    SND_INV_WEAPON = 114,
    SND_INV_PAGE = 115,
    SND_HEALTH = 116,
    SND_STAIRS2SLOPE = 119,
    SND_NATLA_SHOT = 123,
    SND_HIT_SKATER = 132,
    SND_HIT_ADAM = 142,
    SND_STOMP = 147,
    SND_LAVA = 149,
    SND_FLAME = 150,
    SND_DART = 151,
    SND_TNT = 170,
    SND_MUTANT_DEATH = 171,
    SND_SECRET = 173,
    SND_HELICOPTER = 297,
    SND_WINSTON_SCARED = 344,
    SND_WINSTON_WALK = 345,
    SND_WINSTON_PUSH = 346,
    SND_WINSTON_TRAY = 347
};
enum
{
    CT_NONE = 0,
    CT_FRONT = 1,
    CT_LEFT = 2,
    CT_RIGHT = 4,
    CT_CEILING = 8,
    CT_FRONT_CEILING = 16,
    CT_FLOOR_CEILING = 32
};
enum
{
    CollisionInfo_ST_MIDDLE = 0,
    CollisionInfo_ST_FRONT = 1,
    CollisionInfo_ST_LEFT = 2,
    CollisionInfo_ST_RIGHT = 3,
    CollisionInfo_ST_MAX = 4
};
enum
{
    LVL_TR1_TITLE = 0,
    LVL_TR1_GYM = 1,
    LVL_TR1_1 = 2,
    LVL_TR1_2 = 3,
    LVL_TR1_3A = 4,
    LVL_TR1_3B = 5,
    LVL_TR1_CUT_1 = 6,
    LVL_TR1_4 = 7,
    LVL_TR1_5 = 8,
    LVL_TR1_6 = 9,
    LVL_TR1_7A = 10,
    LVL_TR1_7B = 11,
    LVL_TR1_CUT_2 = 12,
    LVL_TR1_8A = 13,
    LVL_TR1_8B = 14,
    LVL_TR1_8C = 15,
    LVL_TR1_10A = 16,
    LVL_TR1_CUT_3 = 17,
    LVL_TR1_10B = 18,
    LVL_TR1_CUT_4 = 19,
    LVL_TR1_10C = 20,
    LVL_TR1_EGYPT = 21,
    LVL_TR1_CAT = 22,
    LVL_TR1_END = 23,
    LVL_TR1_END2 = 24,
    LVL_LOAD = 25,
    LVL_MAX = 26
};
enum
{
    SLOT_LEADBAR = 0,
    SLOT_KEY_ITEM_1 = 1,
    SLOT_KEY_ITEM_2 = 2,
    SLOT_KEY_ITEM_3 = 3,
    SLOT_KEY_ITEM_4 = 4,
    SLOT_PUZZLE_4 = 5,
    SLOT_PUZZLE_3 = 6,
    SLOT_PUZZLE_2 = 7,
    SLOT_PUZZLE_1 = 8,
    SLOT_SCION = 9,
    SLOT_COMPASS = 10,
    SLOT_PISTOLS = 11,
    SLOT_AMMO_PISTOLS = 12,
    SLOT_SHOTGUN = 13,
    SLOT_AMMO_SHOTGUN = 14,
    SLOT_MAGNUMS = 15,
    SLOT_AMMO_MAGNUMS = 16,
    SLOT_UZIS = 17,
    SLOT_AMMO_UZIS = 18,
    SLOT_MEDIKIT_BIG = 19,
    SLOT_MEDIKIT_SMALL = 20,
    SLOT_PASSPORT = 21,
    SLOT_DETAIL = 22,
    SLOT_SOUND = 23,
    SLOT_CONTROLS = 24,
    SLOT_HOME = 25,
    SLOT_MAX = 26
};
enum
{
    INV_STATE_NONE = 0,
    INV_STATE_OPENING = 1,
    INV_STATE_CLOSING = 2,
    INV_STATE_CLOSE = 3,
    INV_STATE_READY = 4,
    INV_STATE_DEATH = 5,
    INV_STATE_SPIN = 6,
    INV_STATE_SELECT = 7,
    INV_STATE_DESELECT = 8,
    INV_STATE_SHOW = 9,
    INV_STATE_PAGE_MAIN = 10,
    INV_STATE_PAGE_KEYS = 11,
    INV_STATE_PAGE_OPTIONS = 12
};
enum
{
    INV_PAGE_TITLE = 0,
    INV_PAGE_DEATH = 1,
    INV_PAGE_END = 2,
    INV_PAGE_STATS = 3,
    INV_PAGE_USE = 4,
    INV_PAGE_MAIN = 5,
    INV_PAGE_KEYS = 6,
    INV_PAGE_OPTIONS = 7
};
enum
{
    Lara_STATE_WALK = 0,
    Lara_STATE_RUN = 1,
    Lara_STATE_STOP = 2,
    Lara_STATE_JUMP = 3,
    Lara_STATE_POSE = 4,
    Lara_STATE_BACK_FAST = 5,
    Lara_STATE_TURN_RIGHT = 6,
    Lara_STATE_TURN_LEFT = 7,
    Lara_STATE_DEATH = 8,
    Lara_STATE_FALL = 9,
    Lara_STATE_HANG = 10,
    Lara_STATE_REACH = 11,
    Lara_STATE_SPLAT = 12,
    Lara_STATE_UW_TREAD = 13,
    Lara_STATE_LAND = 14,
    Lara_STATE_COMPRESS = 15,
    Lara_STATE_BACK = 16,
    Lara_STATE_UW_SWIM = 17,
    Lara_STATE_UW_GLIDE = 18,
    Lara_STATE_HANG_UP = 19,
    Lara_STATE_TURN_FAST = 20,
    Lara_STATE_STEP_RIGHT = 21,
    Lara_STATE_STEP_LEFT = 22,
    Lara_STATE_ROLL_END = 23,
    Lara_STATE_SLIDE = 24,
    Lara_STATE_JUMP_BACK = 25,
    Lara_STATE_JUMP_RIGHT = 26,
    Lara_STATE_JUMP_LEFT = 27,
    Lara_STATE_JUMP_UP = 28,
    Lara_STATE_FALL_BACK = 29,
    Lara_STATE_HANG_LEFT = 30,
    Lara_STATE_HANG_RIGHT = 31,
    Lara_STATE_SLIDE_BACK = 32,
    Lara_STATE_SURF_TREAD = 33,
    Lara_STATE_SURF_SWIM = 34,
    Lara_STATE_UW_DIVE = 35,
    Lara_STATE_BLOCK_PUSH = 36,
    Lara_STATE_BLOCK_PULL = 37,
    Lara_STATE_BLOCK_READY = 38,
    Lara_STATE_PICKUP = 39,
    Lara_STATE_SWITCH_DOWN = 40,
    Lara_STATE_SWITCH_UP = 41,
    Lara_STATE_USE_KEY = 42,
    Lara_STATE_USE_PUZZLE = 43,
    Lara_STATE_DEATH_UW = 44,
    Lara_STATE_ROLL_START = 45,
    Lara_STATE_SPECIAL = 46,
    Lara_STATE_SURF_BACK = 47,
    Lara_STATE_SURF_LEFT = 48,
    Lara_STATE_SURF_RIGHT = 49,
    Lara_STATE_USE_MIDAS = 50,
    Lara_STATE_DEATH_MIDAS = 51,
    Lara_STATE_SWAN_DIVE = 52,
    Lara_STATE_FAST_DIVE = 53,
    Lara_STATE_HANDSTAND = 54,
    Lara_STATE_WATER_OUT = 55,
    Lara_STATE_CLIMB_START = 56,
    Lara_STATE_CLIMB_UP = 57,
    Lara_STATE_CLIMB_LEFT = 58,
    Lara_STATE_CLIMB_END = 59,
    Lara_STATE_CLIMB_RIGHT = 60,
    Lara_STATE_CLIMB_DOWN = 61,
    Lara_STATE_UNUSED_1 = 62,
    Lara_STATE_UNUSED_2 = 63,
    Lara_STATE_UNUSED_3 = 64,
    Lara_STATE_WADE = 65,
    Lara_STATE_ROLL_UW = 66,
    Lara_STATE_PICKUP_FLARE = 67,
    Lara_STATE_ROLL_AIR = 68,
    Lara_STATE_UNUSED_4 = 69,
    Lara_STATE_ZIPLINE = 70,
    Lara_STATE_CROUCH_IDLE = 71,
    Lara_STATE_CROUCH_ROLL = 72,
    Lara_STATE_SPRINT = 73,
    Lara_STATE_SPRINT_ROLL = 74,
    Lara_STATE_MONKEY_IDLE = 75,
    Lara_STATE_MONKEY_FORWARD = 76,
    Lara_STATE_MONKEY_LEFT = 77,
    Lara_STATE_MONKEY_RIGHT = 78,
    Lara_STATE_MONKEY_ROLL = 79,
    Lara_STATE_CRAWL_IDLE = 80,
    Lara_STATE_CRAWL_FORWARD = 81,
    Lara_STATE_MONKEY_TURN_L = 82,
    Lara_STATE_MONKEY_TURN_R = 83,
    Lara_STATE_CRAWL_TURN_L = 84,
    Lara_STATE_CRAWL_TURN_R = 85,
    Lara_STATE_CRAWL_BACK = 86,
    Lara_STATE_CLIMB_TO_CRAWL = 87,
    Lara_STATE_CRAWL_TO_CLIMB = 88,
    Lara_X_MAX = 89
};
enum
{
    Lara_ANIM_PISTOLS_AIM = 0,
    Lara_ANIM_PISTOLS_PICK = 1,
    Lara_ANIM_PISTOLS_DRAW = 2,
    Lara_ANIM_PISTOLS_FIRE = 3,
    Lara_ANIM_SHOTGUN_AIM = 0,
    Lara_ANIM_SHOTGUN_DRAW = 1,
    Lara_ANIM_SHOTGUN_FIRE = 2
};
enum
{
    Lara_ANIM_RUN = 0,
    Lara_ANIM_STAND_LEFT = 2,
    Lara_ANIM_STAND_RIGHT = 3,
    Lara_ANIM_RUN_START = 6,
    Lara_ANIM_STAND = 11,
    Lara_ANIM_LANDING = 24,
    Lara_ANIM_CLIMB_JUMP = 26,
    Lara_ANIM_FALL_HANG = 28,
    Lara_ANIM_SMASH_JUMP = 32,
    Lara_ANIM_FALL_FORTH = 34,
    Lara_ANIM_BACK = 41,
    Lara_ANIM_CLIMB_3 = 42,
    Lara_ANIM_CLIMB_2 = 50,
    Lara_ANIM_SMASH_RUN_LEFT = 53,
    Lara_ANIM_SMASH_RUN_RIGHT = 54,
    Lara_ANIM_RUN_ASCEND_LEFT = 55,
    Lara_ANIM_RUN_ASCEND_RIGHT = 56,
    Lara_ANIM_WALK_ASCEND_LEFT = 57,
    Lara_ANIM_WALK_ASCEND_RIGHT = 58,
    Lara_ANIM_WALK_DESCEND_RIGHT = 59,
    Lara_ANIM_WALK_DESCEND_LEFT = 60,
    Lara_ANIM_BACK_DESCEND_LEFT = 61,
    Lara_ANIM_BACK_DESCEND_RIGHT = 62,
    Lara_ANIM_STEP_LEFT = 65,
    Lara_ANIM_STEP_RIGHT = 67,
    Lara_ANIM_SLIDE_FORTH = 70,
    Lara_ANIM_UW_GLIDE = 87,
    Lara_ANIM_FALL_BACK = 93,
    Lara_ANIM_HANG = 96,
    Lara_ANIM_STAND_NORMAL = 103,
    Lara_ANIM_SLIDE_BACK = 104,
    Lara_ANIM_UNDERWATER = 108,
    Lara_ANIM_WATER_OUT = 111,
    Lara_ANIM_WATER_FALL = 112,
    Lara_ANIM_SURF = 114,
    Lara_ANIM_SURF_SWIM = 116,
    Lara_ANIM_SURF_DIVE = 119,
    Lara_ANIM_BLOCK_READY = 120,
    Lara_ANIM_HIT_FRONT = 125,
    Lara_ANIM_HIT_BACK = 126,
    Lara_ANIM_HIT_LEFT = 127,
    Lara_ANIM_HIT_RIGHT = 128,
    Lara_ANIM_DEATH_BOULDER = 139,
    Lara_ANIM_SURF_BACK = 140,
    Lara_ANIM_SURF_LEFT = 143,
    Lara_ANIM_SURF_RIGHT = 144,
    Lara_ANIM_STAND_ROLL_BEGIN = 146,
    Lara_ANIM_STAND_ROLL_END = 147,
    Lara_ANIM_DEATH_SPIKES = 149,
    Lara_ANIM_HANG_SWING = 150,
    Lara_ANIM_CLIMB_START = 164,
    Lara_ANIM_WADE_SWIM = 176,
    Lara_ANIM_WADE = 177,
    Lara_ANIM_WADE_RUN_LEFT = 178,
    Lara_ANIM_WADE_RUN_RIGHT = 179,
    Lara_ANIM_WADE_STAND = 186,
    Lara_ANIM_HANG_STEP_UP = 187,
    Lara_ANIM_HANG_STEP_DOWN = 188,
    Lara_ANIM_WADE_ASCEND = 190,
    Lara_ANIM_SURF_OUT = 191,
    Lara_ANIM_SWIM_STAND = 192,
    Lara_ANIM_SURF_STAND = 193,
    Lara_ANIM_SWITCH_BIG_DOWN = 195,
    Lara_ANIM_SWITCH_BIG_UP = 196,
    Lara_ANIM_PUSH_BUTTON = 197,
    Lara_ANIM_UW_ROLL = 203
};
enum
{
    Lara_CLIMB_HANG = 0,
    Lara_CLIMB_COLLIDE = 1,
    Lara_CLIMB_OK = 2
};
typedef struct
{
    const Anim *anims;
    const AnimState *animStates;
    const AnimRange *animRanges;
    const FloorData *floors;
    const tr64_i16_be_t *animCommands;
    const tr64_u16_be_t *animFrames;
    const tr64_i32_be_t *nodes;
    const FixedCamera *cameras;
    const SoundInfo *soundsInfo;
    const tr64_i16_be_t *soundMap;
    Model *models;
    const tr64_core_boxes_t *boxes;
    const tr64_u16_be_t *overlaps, *zones;
    uint32 roomsCount, itemsCount, boxesCount, camerasCount, framesCount;
} GameLevel;
typedef struct
{
    const tr64_level_t *core;
    uint32 tick, bytes, textureTicks;
    tr64_game_stop_t stop;
    GameLevel level;
    Room *rooms;
    Sector *sectorState;
    ItemObj *items;
    Lara *players[TR64_GAME_PLAYERS];
    ExtraInfoLara playersExtra[TR64_GAME_PLAYERS];
    ExtraInfoEnemy enemiesExtra[8];
    TargetInfo tinfo;
    vec3i gCameraViewPos;
    LaraHazardState gLaraHazardState[TR64_GAME_PLAYERS];
    ItemObj *interaction[TR64_GAME_PLAYERS];
    InteractionAnim interactionAnim[TR64_GAME_PLAYERS];
    RetiredAction retired[TR64_GAME_PLAYERS];
    bool retireLevel;
    const FloorData *blockTrigger;
    uint32 coopBlocks[(MAX_ITEMS+31)/32];
    JoinPose joinPose[TR64_GAME_PLAYERS];
    vec3i secondaryCollisionOffset;
    Room *roomsList[32];
    ItemObj *firstActive, *firstFree;
    CollisionInfo cinfo;
    const FloorData *gLastFloorData;
    uint16 gLastFloorSlant;
    AABBs tmpBox;
    SaveGame gSaveGame;
    GameInventory inventory;
    GameSettings gSettings;
    int32 pendingLevel;
    bool inventoryOpenPending;
    Lara *inventoryOpenLara;
    int32 inventoryOpenPage,inventoryOpenArg;
    int16 flow_camera[TR64_GAME_PLAYERS];
    FlipMapState flipMaps[10], flipBackup[10];
    bool flipTransaction, flipPending;
    /* Bit i: item i was taken out of its room's item list by Room_remove and not put back by
     * Room_add (a collected pickup, a dead scion target, an item about to be freed). Read by
     * game_world_items_linked instead of walking every room list on each door update. */
    uint32 unlinkedItems[8];
    /* Alternate-room pairs of the level (Core data only, so fixed once built); game/world.c. */
#if TR64_HAS_TR3
    uint8 roomMate[255], roomAlternate[255];
#elif TR64_HAS_TR2
    uint8 roomMate[TR64_LIMIT_rooms_2], roomAlternate[TR64_LIMIT_rooms_2];
#else
    uint8 roomMate[139], roomAlternate[139];
#endif
    bool roomPairsReady;
    EffectState effect;
    uint32 gRandSeedLogic, gRandSeedDraw;
    int32 gLevelID, gCurTrack;
    uint32 musicSerial,musicReason;
    /* CRC32 of the Core tables and of the textures: immutable while the level is bound, so hashed once
     * at level start and reused by every save and load (game_save_identities, save.c). */
    uint32 coreIdentity,textureIdentity;
    int32 brightness;
    bool flow_active[TR64_GAME_PLAYERS];
    Matrix gMatrixStack[8], *gMatrixPtr;
    uint16 *cameraFlags, *boxFlags;
    tr64_game_sound_event_t sounds[64];
    uint32 soundCount;
    vec3i pickupPosition[MAX_ITEMS];
    uint16 pickupType[MAX_ITEMS],pickupCount;
    uint32 pickupTick;
#if !TR64_HAS_TR3
    struct {
        uint16 wadeSwim, wade, wadeStand, wadeAscend;
        uint16 surfOut, swimStand, surfStand, rollUw;
        uint16 tr3Start, crawlIdle, crawlDeath;
        bool hasTR2, hasTR3;
    } extraAnims;
    uint32 extraInput[TR64_GAME_PLAYERS];
    int16 stamina[TR64_GAME_PLAYERS];
    uint8 forcedCrouch[TR64_GAME_PLAYERS];
#endif
#if TR64_HAS_SEQUEL
    GameTR2 *tr2;
#ifdef TR64_HOST
    uint64_t tr2Rules;
#endif
#endif
} GameState;
typedef tr64_game_menu_view_t GameInventoryView;
enum { GAME_INV_OPEN,GAME_INV_UPDATE,GAME_INV_SAVE_RESULT,GAME_INV_VIEW };
enum {
    GAME_INV_ACTION_NEXT_LEVEL=1,GAME_INV_ACTION_SAVE=2,
    GAME_INV_ACTION_SETTINGS=4,GAME_INV_ACTION_VIBRATE=8,
    GAME_INV_ACTION_BACKGROUND=16,GAME_INV_ACTION_RESET_STATS=32
};
typedef struct {
    uint32 op;
    Lara *lara;
    int32 page,arg,frames;
    uint8 saveResult;
    uint32 actions;
    int32 nextLevel,backgroundKind;
    GameInventoryView *view;
} GameInventoryCall;
void game_inventory_open(Lara *lara,int32 page,int32 arg);
void game_inventory_update(int32 frames);
void game_complete_level(Lara *lara);
void game_inventory_queue_open(Lara *lara,int32 page,int32 arg);
void game_inventory_flush_open(void);
void game_prepare_load(void);
uint16 encounterPreparedItemCount(void);
bool encounterRebuildListsV3(void);
void encounterActivateLoadedItem(ItemObj *item);
void game_inventory_fading(int32 frames);
void game_pickup_notify(const ItemObj *item,const Lara *lara);
bool inventory_countLabel(int32 slot,int16 *value,uint8 *suffix);
bool Lara_ammoLabel(const Lara *lara,int16 *value,uint8 *suffix);
bool inventory_medikitHealth(const Lara *lara,int32 slot,bool current,int32 *healthQ8);
void worldTriggerActivate(ItemObj *item);
extern GameState *gs;
#if TR64_HAS_SEQUEL
#if TR64_HAS_TR3
#define TR64_RULE(name) ((TR2_RULES_TR3 & TR2_RULE_##name) != 0)
#define TR64_SECOND_GAME 1
#elif defined(TR64_HOST)
#define TR64_RULE(name) ((gs->tr2Rules & TR2_RULE_##name) != 0)
/* D17: the host selects the game at run time. */
#define TR64_SECOND_GAME (gs->core->game == 2)
#else
#define TR64_RULE(name) ((TR2_RULES_WALL & TR2_RULE_##name) != 0)
/* D17: a cart is one game. This is 1 on the second cart and is not
 * compiled into the first. */
#define TR64_SECOND_GAME TR64_CART_TR2
#endif
#if TR64_HAS_TR3
void game_tr3_s_state(Lara *self);
void game_tr3_c_state(Lara *self);
bool game_tr3_in_swamp(const Lara *self);
bool game_tr3_can_crouch(const Lara *self);
bool game_tr3_can_sprint(const Lara *self);
bool game_tr3_try_monkey_grab(Lara *self);
bool game_tr3_try_climb_to_crawl(Lara *self);
void game_tr3_camera_request_extra(Lara *self);
void game_tr3_swamp_water_status(Lara *self, int32 line, int32 feet);
void game_tr3_swamp_water_air(Lara *self);
bool game_tr3_swamp_sink(Lara *self);
#endif
typedef struct {
    const tr64_u16_be_t *words;
    unsigned extent;
} PackedFrame;
typedef struct {
    PackedFrame a,b;
    int32 delta, rate;
} PackedFramePair;
PackedFrame game_tr2_packed_key(const Anim *, unsigned index, unsigned frame);
PackedFramePair game_tr2_packed_frames(const ItemObj *);
extern bool game_tr2_shotgun_checking;
const tr64_u16_be_t *game_tr2_step_packed(const tr64_u16_be_t *, const tr64_u16_be_t *, bool apply);
const tr64_u16_be_t *tr2FrameRotate(const tr64_u16_be_t *,const tr64_u16_be_t *);
const tr64_u16_be_t *skipPackedRotations(PackedFrame,unsigned count);
void game_tr2_camera_subject(Camera *, int32);
void game_tr2_camera_request(Lara *);
const Sector *game_tr2_room_sector(Room **,int32 x,int32 y,int32 z);
void game_tr2_camera_heights(const Sector *,const vec3i *,bool full,int32 *floor,int32 *ceiling);
void game_tr2_camera_move(Camera *,const Location *previousTarget);
int32 game_tr2_bearing(int64_t forward,int64_t side);
uint32 game_tr2_horizontal_length(int32 x,int32 z);
void game_tr2_camera_end(unsigned kind,const char *reason);
vec3i game_tr2_heading_step(int16 yaw,int16 speed);
bool game_tr2_sight(const Location *,Location *,bool full);
bool game_tr2_activate_item(ItemObj *);
void game_tr2_zipline_release(Lara *, int16 speed, int16 descent);
void game_tr2_validate_world_animations(void);
void game_tr2_admit_counts(const char *name, uint32 rooms, uint32 items);
bool game_tr2_pickup_type(unsigned type);
bool game_tr2_interact(ItemObj *, Lara *);
bool game_tr2_switch_settled(ItemObj *, unsigned timer);
const char *game_tr2_inert_interaction(unsigned type);
void game_tr2_refuse_interaction(const ItemObj *, const char *operation);
void game_tr2_update_arms(Lara *);
bool game_tr2_firearms_camera_requested(void);
int32 game_weapon_spread(int32, bool);
void game_weapon_aim(ExtraInfoLara_Arm *);
void Lara_weaponGetAimPoint(Lara *, ItemObj *, Location *);
bool game_tr2_ray_hit(const ItemObj *, const vec3i *origin, int16 pitch, int16 yaw, vec3i *point);
int64_t game_tr2_sphere_entry(int64_t x, int64_t y, int64_t z, int32 radius);
vec3s game_tr2_pistol_ray_angles(int16 armPitch, int16 armYaw, int16 bodyYaw);
unsigned game_tr2_model_bank_end(unsigned base);
bool game_tr2_creature_type(unsigned type);
TR2Creature *game_tr2_creature_slot(const ItemObj *);
bool game_tr2_creature_wake(ItemObj *, bool force);
unsigned game_tr2_joint_origins(const ItemObj *,vec3i out[32]);
vec3i game_tr2_combat_joint(const ItemObj *,unsigned joint,vec3i offset,int16 extraYaw);
void game_tr2_effects_init(void);
void game_tr2_run_effects(const Lara *);
void game_tr2_campfire_effect(const ItemObj *,bool triggered);
void game_tr2_shot_effect(TR2EffectKind,Room *,vec3i,int16 speed,int16 yaw);
void game_tr2_bite_effect(const ItemObj *,unsigned joint,vec3i offset,int16 extraYaw);
void game_tr2_spider_breakup(ItemObj *);
void game_tr2_update_creature(ItemObj *, Lara *);
const tr64_u16_be_t *game_tr2_creature_zones(const TR2Creature *, bool flipped);
void game_tr2_creature_nav_init(TR2Creature *, int16 box);
void game_tr2_creature_box_destination(TR2Creature *,int box);
void game_tr2_creature_waypoint(TR2Creature *);
void game_tr2_creature_stride(TR2Creature *,int16 turn);
void game_tr2_creature_contact(ItemObj *, Lara *);
uint32 game_tr2_creature_touch(const ItemObj *, const Lara *);
vec3i game_tr2_flare_hand(const Lara *, bool);
void game_tr2_update_world_flare(ItemObj *);
int32 game_tr2_water_height(Room *, int32 x, int32 z);
void game_tr2_water_status(Lara *);
void game_tr2_water_air(Lara *);
void game_tr2_water_move(Lara *);
#else
#define TR64_RULE(name) 0
#define TR64_SECOND_GAME 0
#endif
#if !TR64_HAS_TR3
static inline int32 game_lara_abilities(void)
{
    return gs ? gs->gSettings.controls_abilities : TR64_LARA_ABILITIES_TR1;
}
static inline bool game_lara_has_tr2(void)
{
    return gs && gs->gSettings.controls_abilities >= TR64_LARA_ABILITIES_TR2 &&
           gs->extraAnims.hasTR2;
}
static inline bool game_lara_has_tr3(void)
{
    return gs && gs->gSettings.controls_abilities >= TR64_LARA_ABILITIES_TR3 &&
           gs->extraAnims.hasTR3;
}
static inline uint16 game_lara_anim(uint16 canonical)
{
    if (!gs) return canonical;
    switch (canonical) {
    case Lara_ANIM_WADE_SWIM:   return gs->extraAnims.wadeSwim;
    case Lara_ANIM_WADE:        return gs->extraAnims.wade;
    case Lara_ANIM_WADE_STAND:  return gs->extraAnims.wadeStand;
    case Lara_ANIM_WADE_ASCEND: return gs->extraAnims.wadeAscend;
    case Lara_ANIM_SURF_OUT:    return gs->extraAnims.surfOut;
    case Lara_ANIM_SWIM_STAND:  return gs->extraAnims.swimStand;
    case Lara_ANIM_SURF_STAND:  return gs->extraAnims.surfStand;
    case Lara_ANIM_UW_ROLL:     return gs->extraAnims.rollUw;
    default: return canonical;
    }
}
void game_lara_abilities_init(void);
void game_lara_abilities_refresh(void);
void game_lara_tr3_tick(Lara *self);
bool game_lara_tr3_can_sprint(const Lara *self);
bool game_lara_tr3_can_crouch(const Lara *self);
void game_lara_tr3_s_state(Lara *self);
void game_lara_tr3_c_state(Lara *self);
#endif
extern const uint16 divTable[1025];
extern const uint32 gSinCosTable[4096];
extern const int16 atanTable[2050];
extern const int32 atanOctant[8];
static inline void swap_i32(int32 *a, int32 *b)
{
    int32 c = *a;
    *a = *b;
    *b = c;
}
void Bubble_update(Bubble *self);
void Camera_clip(Camera *self, Location *loc);
Location Camera_getBestLocation(Camera *self, bool clip);
Location Camera_getLocationForAngle(Camera *self, int32 angle, int32 distH, int32 distV);
void Camera_init(Camera *self, ItemObj *lara);
void Camera_lookAt(Camera *self, int32 offset);
void Camera_move(Camera *self, Location *to, int32 speed);
void Camera_toCombat(Camera *self);
void Camera_updateFixed(Camera *self);
void Camera_updateFollow(Camera *self, ItemObj *item);
void Camera_updateLook(Camera *self, ItemObj *item);
void CollisionInfo_setAngle(CollisionInfo *self, int16 value);
void ItemObj_activate(ItemObj *self);
const Anim *ItemObj_animChange(ItemObj *self, const Anim *anim);
bool ItemObj_animIsEnd(const ItemObj *self, int32 offset);
void ItemObj_animProcess(ItemObj *self, bool movement);
const Anim *ItemObj_animSet(ItemObj *self, int32 newAnimIndex, bool resetState, int32 frameOffset);
void ItemObj_collideRoom(const ItemObj *self, int32 height, int32 yOffset);
void ItemObj_deactivate(ItemObj *self);
void ItemObj_fxBubbles(ItemObj *self, Room *fxRoom, int32 fxJoint, const vec3i *fxOffset);
void ItemObj_fxSplash(ItemObj *self);
int32 ItemObj_getBridgeFloor(const ItemObj *self, int32 x, int32 z);
int32 ItemObj_getDrawBridgeFloor(const ItemObj *self, int32 x, int32 z);
void ItemObj_getItemFloorCeiling(const ItemObj *self, int32 x, int32 y, int32 z, int32 *floor,
                                 int32 *ceiling);
vec3i ItemObj_getRelative(const ItemObj *self, const vec3i *point);
int32 ItemObj_getTrapDoorFloor(const ItemObj *self, int32 x, int32 z);
int32 ItemObj_getWaterDepth(const ItemObj *self);
int32 ItemObj_getWaterLevel(const ItemObj *self);
bool ItemObj_isKeyHit(const ItemObj *self, InputState state);
void ItemObj_meshSwap(ItemObj *self, ItemType type, uint32 mask);
void ItemObj_move(ItemObj *self);
void ItemObj_remove(ItemObj *self);
void ItemObj_removeOwnedFlashes(ItemObj *self);
void ItemObj_resetSlot(ItemObj *self);
void ItemObj_updateRoom(ItemObj *self, int32 offset);
bool Lara_alignAngle(Lara *self, int16 *angle, int16 threshold);
void Lara_alignWall(Lara *self, int32 radius);
void Lara_c_STATE_BACK(Lara *self);
void Lara_c_STATE_BACK_FAST(Lara *self);
void Lara_c_STATE_BLOCK_PULL(Lara *self);
void Lara_c_STATE_BLOCK_PUSH(Lara *self);
void Lara_c_STATE_BLOCK_READY(Lara *self);
void Lara_c_STATE_CLIMB_DOWN(Lara *self);
void Lara_c_STATE_CLIMB_END(Lara *self);
void Lara_c_STATE_CLIMB_LEFT(Lara *self);
void Lara_c_STATE_CLIMB_RIGHT(Lara *self);
void Lara_c_STATE_CLIMB_START(Lara *self);
void Lara_c_STATE_CLIMB_UP(Lara *self);
void Lara_c_STATE_COMPRESS(Lara *self);
void Lara_c_STATE_DEATH(Lara *self);
void Lara_c_STATE_DEATH_MIDAS(Lara *self);
void Lara_c_STATE_DEATH_UW(Lara *self);
void Lara_c_STATE_FALL(Lara *self);
void Lara_c_STATE_FALL_BACK(Lara *self);
void Lara_c_STATE_FAST_DIVE(Lara *self);
void Lara_c_STATE_HANDSTAND(Lara *self);
void Lara_c_STATE_HANG(Lara *self);
void Lara_c_STATE_HANG_LEFT(Lara *self);
void Lara_c_STATE_HANG_RIGHT(Lara *self);
void Lara_c_STATE_HANG_UP(Lara *self);
void Lara_c_STATE_JUMP(Lara *self);
void Lara_c_STATE_JUMP_BACK(Lara *self);
void Lara_c_STATE_JUMP_LEFT(Lara *self);
void Lara_c_STATE_JUMP_RIGHT(Lara *self);
void Lara_c_STATE_JUMP_UP(Lara *self);
void Lara_c_STATE_LAND(Lara *self);
void Lara_c_STATE_PICKUP(Lara *self);
void Lara_c_STATE_PICKUP_FLARE(Lara *self);
void Lara_c_STATE_POSE(Lara *self);
void Lara_c_STATE_REACH(Lara *self);
void Lara_c_STATE_ROLL_AIR(Lara *self);
void Lara_c_STATE_ROLL_END(Lara *self);
void Lara_c_STATE_ROLL_START(Lara *self);
void Lara_c_STATE_ROLL_UW(Lara *self);
void Lara_c_STATE_RUN(Lara *self);
void Lara_c_STATE_SLIDE(Lara *self);
void Lara_c_STATE_SLIDE_BACK(Lara *self);
void Lara_c_STATE_SPECIAL(Lara *self);
void Lara_c_STATE_SPLAT(Lara *self);
void Lara_c_STATE_STEP_LEFT(Lara *self);
void Lara_c_STATE_STEP_RIGHT(Lara *self);
void Lara_c_STATE_STOP(Lara *self);
void Lara_c_STATE_SURF_BACK(Lara *self);
void Lara_c_STATE_SURF_LEFT(Lara *self);
void Lara_c_STATE_SURF_RIGHT(Lara *self);
void Lara_c_STATE_SURF_SWIM(Lara *self);
void Lara_c_STATE_SURF_TREAD(Lara *self);
void Lara_c_STATE_SWAN_DIVE(Lara *self);
void Lara_c_STATE_SWITCH_DOWN(Lara *self);
void Lara_c_STATE_SWITCH_UP(Lara *self);
void Lara_c_STATE_TURN_FAST(Lara *self);
void Lara_c_STATE_TURN_LEFT(Lara *self);
void Lara_c_STATE_TURN_RIGHT(Lara *self);
void Lara_c_STATE_UNUSED_1(Lara *self);
void Lara_c_STATE_UNUSED_2(Lara *self);
void Lara_c_STATE_UNUSED_3(Lara *self);
void Lara_c_STATE_UNUSED_4(Lara *self);
void Lara_c_STATE_USE_KEY(Lara *self);
void Lara_c_STATE_USE_MIDAS(Lara *self);
void Lara_c_STATE_USE_PUZZLE(Lara *self);
void Lara_c_STATE_UW_DIVE(Lara *self);
void Lara_c_STATE_UW_GLIDE(Lara *self);
void Lara_c_STATE_UW_SWIM(Lara *self);
void Lara_c_STATE_UW_TREAD(Lara *self);
void Lara_c_STATE_WADE(Lara *self);
void Lara_c_STATE_WALK(Lara *self);
void Lara_c_STATE_WATER_OUT(Lara *self);
void Lara_c_STATE_ZIPLINE(Lara *self);
void Lara_c_angle(Lara *self, int16 angleDelta);
void Lara_c_applyOffset(Lara *self);
bool Lara_c_checkCeiling(Lara *self);
bool Lara_c_checkClimbStart(Lara *self);
bool Lara_c_checkClimbUp(Lara *self);
bool Lara_c_checkDrop(Lara *self);
bool Lara_c_checkFall(Lara *self, int32 height, int32 fallAnimIndex);
bool Lara_c_checkGrab(Lara *self);
bool Lara_c_checkHang(Lara *self);
bool Lara_c_checkLanding(Lara *self);
bool Lara_c_checkSlide(Lara *self);
bool Lara_c_checkSpace(Lara *self);
bool Lara_c_checkSwing(Lara *self);
bool Lara_c_checkWall(Lara *self);
bool Lara_c_checkWallSurf(Lara *self);
bool Lara_c_checkWallUW(Lara *self);
bool Lara_c_checkWaterOut(Lara *self);
ClimbState Lara_c_climbCollide(Lara *self, int32 width);
void Lara_c_climbSide(Lara *self, int32 width);
void Lara_c_default(Lara *self);
void Lara_c_fall(Lara *self);
int32 Lara_c_getDirectCeiling(const Lara *self, const Sector *sector, int32 x, int32 y, int32 z);
int32 Lara_c_getDirectFloor(const Lara *self, const Sector *sector, int32 x, int32 y, int32 z);
void Lara_c_hang(Lara *self, int32 angleDelta);
bool Lara_c_isDirectCeilingFlat(const Lara *self, const Sector *sector);
bool Lara_c_isDirectFloorFlat(const Lara *self, const Sector *sector);
void Lara_c_jump(Lara *self);
void Lara_c_roll(Lara *self);
void Lara_c_slide(Lara *self);
void Lara_c_step(Lara *self);
void Lara_c_surf(Lara *self);
void Lara_c_swim(Lara *self);
void Lara_recordDistance(Lara *self, const vec3i *oldPos);
void Lara_s_STATE_BACK(Lara *self);
void Lara_s_STATE_BACK_FAST(Lara *self);
void Lara_s_STATE_BLOCK_PULL(Lara *self);
void Lara_s_STATE_BLOCK_PUSH(Lara *self);
void Lara_s_STATE_BLOCK_READY(Lara *self);
void Lara_s_STATE_CLIMB_DOWN(Lara *self);
void Lara_s_STATE_CLIMB_END(Lara *self);
void Lara_s_STATE_CLIMB_LEFT(Lara *self);
void Lara_s_STATE_CLIMB_RIGHT(Lara *self);
void Lara_s_STATE_CLIMB_START(Lara *self);
void Lara_s_STATE_CLIMB_UP(Lara *self);
void Lara_s_STATE_COMPRESS(Lara *self);
void Lara_s_STATE_DEATH(Lara *self);
void Lara_s_STATE_DEATH_MIDAS(Lara *self);
void Lara_s_STATE_DEATH_UW(Lara *self);
void Lara_s_STATE_FALL(Lara *self);
void Lara_s_STATE_FALL_BACK(Lara *self);
void Lara_s_STATE_FAST_DIVE(Lara *self);
void Lara_s_STATE_HANDSTAND(Lara *self);
void Lara_s_STATE_HANG(Lara *self);
void Lara_s_STATE_HANG_LEFT(Lara *self);
void Lara_s_STATE_HANG_RIGHT(Lara *self);
void Lara_s_STATE_HANG_UP(Lara *self);
void Lara_s_STATE_JUMP(Lara *self);
void Lara_s_STATE_JUMP_BACK(Lara *self);
void Lara_s_STATE_JUMP_LEFT(Lara *self);
void Lara_s_STATE_JUMP_RIGHT(Lara *self);
void Lara_s_STATE_JUMP_UP(Lara *self);
void Lara_s_STATE_LAND(Lara *self);
void Lara_s_STATE_PICKUP(Lara *self);
void Lara_s_STATE_PICKUP_FLARE(Lara *self);
void Lara_s_STATE_POSE(Lara *self);
void Lara_s_STATE_REACH(Lara *self);
void Lara_s_STATE_ROLL_AIR(Lara *self);
void Lara_s_STATE_ROLL_END(Lara *self);
void Lara_s_STATE_ROLL_START(Lara *self);
void Lara_s_STATE_ROLL_UW(Lara *self);
void Lara_s_STATE_RUN(Lara *self);
void Lara_s_STATE_SLIDE(Lara *self);
void Lara_s_STATE_SLIDE_BACK(Lara *self);
void Lara_s_STATE_SPECIAL(Lara *self);
void Lara_s_STATE_SPLAT(Lara *self);
void Lara_s_STATE_STEP_LEFT(Lara *self);
void Lara_s_STATE_STEP_RIGHT(Lara *self);
void Lara_s_STATE_STOP(Lara *self);
void Lara_s_STATE_SURF_BACK(Lara *self);
void Lara_s_STATE_SURF_LEFT(Lara *self);
void Lara_s_STATE_SURF_RIGHT(Lara *self);
void Lara_s_STATE_SURF_SWIM(Lara *self);
void Lara_s_STATE_SURF_TREAD(Lara *self);
void Lara_s_STATE_SWAN_DIVE(Lara *self);
void Lara_s_STATE_SWITCH_DOWN(Lara *self);
void Lara_s_STATE_SWITCH_UP(Lara *self);
void Lara_s_STATE_TURN_FAST(Lara *self);
void Lara_s_STATE_TURN_LEFT(Lara *self);
void Lara_s_STATE_TURN_RIGHT(Lara *self);
void Lara_s_STATE_UNUSED_1(Lara *self);
void Lara_s_STATE_UNUSED_2(Lara *self);
void Lara_s_STATE_UNUSED_3(Lara *self);
void Lara_s_STATE_UNUSED_4(Lara *self);
void Lara_s_STATE_USE_KEY(Lara *self);
void Lara_s_STATE_USE_MIDAS(Lara *self);
void Lara_s_STATE_USE_PUZZLE(Lara *self);
void Lara_s_STATE_UW_DIVE(Lara *self);
void Lara_s_STATE_UW_GLIDE(Lara *self);
void Lara_s_STATE_UW_SWIM(Lara *self);
void Lara_s_STATE_UW_TREAD(Lara *self);
void Lara_s_STATE_WADE(Lara *self);
void Lara_s_STATE_WALK(Lara *self);
void Lara_s_STATE_WATER_OUT(Lara *self);
void Lara_s_STATE_ZIPLINE(Lara *self);
bool Lara_s_checkDeath(Lara *self, int32 deathState);
bool Lara_s_checkFall(Lara *self);
bool Lara_s_checkFront(Lara *self, int16 angleDelta, int32 radius);
bool Lara_s_checkLook(Lara *self);
bool Lara_s_checkRoll(Lara *self);
void Lara_s_checkWalk(Lara *self, int32 stopState);
void Lara_s_dive(Lara *self);
bool Lara_s_getFront(Lara *self, int16 rot);
void Lara_s_ignoreEnemy(Lara *self);
void Lara_s_rotate(Lara *self, int32 maxSpeed, int32 tilt);
void Lara_s_turnUW(Lara *self);
void Lara_startScreaming(Lara *self);
void Lara_stopScreaming(Lara *self);
void Lara_updateAbove(Lara *self);
void Lara_updateLook(Lara *self);
void Lara_updateSurface(Lara *self);
void Lara_updateUnder(Lara *self);
void Lara_updateWaterState(Lara *self);
void Room_add(Room *self, ItemObj *item);
Room **Room_addNearRoom(Room *self, Room **list, int32 x, int32 y, int32 z);
Room **Room_getNearRooms(Room *self, const vec3i *pos, int32 radius, int32 height);
Room *Room_getRoom(Room *self, int32 x, int32 y, int32 z);
const Sector *Room_getSector(const Room *self, int32 x, int32 z);
const Sector *Room_getWaterSector(const Room *self, int32 x, int32 z);
void Room_remove(Room *self, ItemObj *item);
int32 Sector_getCeiling(const Sector *self, int32 x, int32 y, int32 z);
int32 Sector_getFloor(const Sector *self, int32 x, int32 y, int32 z);
Room *Sector_getNextRoom(const Sector *self);
const Sector *Sector_getSectorAbove(const Sector *self, int32 posX, int32 posZ);
const Sector *Sector_getSectorBelow(const Sector *self, int32 posX, int32 posZ);
void Sector_getTriggerFloorCeiling(const Sector *self, int32 x, int32 y, int32 z, int32 *floor,
                                   int32 *ceiling);
void SpriteEffect_update(SpriteEffect *self);
int32 alignOffset(int32 a, int32 b);
int32 checkHeight(Room *room, int32 x, int32 y, int32 z);
bool checkWall(Room *room, int32 x, int32 y, int32 z);
bool trace(const Location *from, Location *to, bool accurate);
bool traceX(const Location *from, Location *to, bool accurate);
bool traceZ(const Location *from, Location *to, bool accurate);
uint16 worldActionEffectiveEnd(uint16 action, uint16 extra);
int16 angleLerp(int16 a, int16 b, int32 w);
void anglesFromVector(int32 x, int32 y, int32 z, int16 *angleX, int16 *angleY);
bool boxIntersect(const AABBi *a, const AABBi *b);
vec3i boxPushOut(const AABBi *a, const AABBi *b);
void boxRotateYQ_c(AABBi *box, int32 quadrant);
void boxTranslate_c(AABBi *box, int32 x, int32 y, int32 z);
int32 fastLength(int32 dx, int32 dy);
int32 phd_atan(int32 x, int32 y);
uint32 phd_sqrt(uint32 x);
void CollisionInfo_setSide(CollisionInfo *self, CollisionInfo_SideType st, int32 floor,
                           int32 ceiling);
const AABBs *ItemObj_getBoundingBox(const ItemObj *self, bool lerp);
void ItemObj_animCmd(ItemObj *self, bool fx, const Anim *anim);
uint16 ItemObj_ticksToInteractionEvent(const InteractionAnim *cursor,bool levelEnd);
vec3i ItemObj_getJoint(const ItemObj *self, int32 jointIndex, const vec3i *offset);
ItemObj *ItemObj_add(ItemType type, Room *room, const vec3i *pos, int32 angleY);
Room **Room_getAdjRooms(Room *self);
bool Room_collideStatic(Room *self, CollisionInfo *cinfo, const vec3i *p, int32 height);
void Lara_updateState(Lara *self);
void Lara_setWeaponState(Lara *self, WeaponState state);
void Lara_hit(Lara *self, int32 damage, const vec3i *point, int32 sound);
void Camera_update(Camera *self);
int32 rand_logic(void);
int32 rand_draw(void);
void *soundPlay(int16 id, const vec3i *pos);
void soundStop(int16 id);
void game_check_trigger(const FloorData *trigger, ItemObj *lara);
void matrixPush_c(void);
void matrixSetIdentity_c(void);
void matrixTranslateRel_c(int32 x, int32 y, int32 z);
void matrixRotateX_c(int32 angle);
void matrixRotateY_c(int32 angle);
void matrixRotateZ_c(int32 angle);
void matrixRotateYXZ_c(int32 angleX, int32 angleY, int32 angleZ);
void matrixLerp_c(const Matrix *n, int32 pmul, int32 pdiv);
void game_frame_pair(const ItemObj *, const tr64_u16_be_t **, const tr64_u16_be_t **, int32 *,
                     int32 *);
void game_initial_world(void);
/* Oracle hunk 25: raise or restore the rolling block's sector by 2048 (objects-missing.md 2.1). */
void game_moving_block_sector(Room *room,const vec3i *pos,Sector *sector,bool raise);
void game_lara_collision(Lara *self);
void Lara_updateObjectsCollision(Lara *self);
bool Lara_applyFlowCurrent(Lara *self);
static inline bool encounterIsSource(ItemType type)
{
    return type == ITEM_MUTANT_EGG_SMALL || type == ITEM_MUTANT_EGG_BIG ||
           type == ITEM_CENTAUR_STATUE || type == ITEM_HAMMER_HANDLE || type == ITEM_FLAME_EMITTER;
}
#include "weapon.h"
#include "object.h"
void game_init_item(ItemObj *self, Room *room);
/* Only pass a gameplay Lara or the title-ring input actor. */
static inline unsigned Lara_slot(const Lara *lara)
{
    return lara == gs->players[1];
}
bool Lara_claimBlocked(const Lara *lara,const ItemObj *item);
bool Lara_triggerBlocked(const ItemObj *item,unsigned type);
void Lara_claim(Lara *lara,ItemObj *item);
int32 Lara_selectedUse(const Lara *lara);
void game_item_call(GameItemCall *call);
void game_item_activate(ItemObj *self);
void ItemObj_activateBase(ItemObj *self);
void ItemObj_deactivateBase(ItemObj *self);
void ItemObj_hit(ItemObj *self, int32 damage, const vec3i *point, int32 soundId);
void ItemObj_hitBy(ItemObj *self,Lara *shooter,int32 damage,const vec3i *point,int32 soundId);
static inline uint64_t game_distance_squared(vec3i a,vec3i b)
{
    vec3i d=vec3i_sub(a,b);
    return (uint64_t)((int64_t)d.x*d.x) + (uint64_t)((int64_t)d.y*d.y) +
           (uint64_t)((int64_t)d.z*d.z);
}
void worldRequestFlipToggle(void);
void game_world_flip(bool flipped);
bool worldApplyPendingFlip(void);
bool game_interaction_busy(const Lara *actor);
void game_retire_interaction(Lara *actor);
bool game_settle_interaction(Lara *actor);
void game_interaction_triggered(ItemObj *item);
void game_interaction_completed_level(Lara *actor);
void game_refresh_join_pose(Lara *actor);
bool game_world_items_linked(void);
/* A pickup in the state a collected pickup with a pickup trigger under it has (INACTIVE, out of its room's list):
 * the world validator's exemption and the save load's refusal to link it again (oracle hunks 14 and 20). */
bool game_pickup_collected(uint32 type, uint16 flags);
void hazardLavaBurn(ItemObj *lara);
void worldApplyFlipAction(uint8 action, uint16 arg, uint8 mask, bool once, uint8 type);
void worldUpdateEffects(void);
void worldDispatchEffect(uint16 id, Lara *lara, bool flipped);
#include "enemy.h"
void game_load_item_units(void);
/* game/cut.c: cutscene levels have no Lara. */
bool game_is_cutscene_level(int32 level);
void Camera_initCinematic(Camera *self);
void Camera_updateCinematic(Camera *self);
void Cut_construct(ItemObj *self);
void Cut_update(ItemObj *self);
void game_cut_tick(void);
void game_sndPlayTrack(int32 track);
void game_stop_set(uint32 kind, int32 next_level_id, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
int32 game_next_level_id(void);
void game_unload_item_units(void);
#endif
