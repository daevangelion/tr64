/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h and src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_CONSTANTS_H
#define TR64_GAME_CONSTANTS_H
#include "types.h"
#define FIXED_SHIFT 14
#define FIXED_ONE 0x4000
#define ANGLE_360 0x10000
#define ANGLE_1 (ANGLE_360 / 360)
#define ANGLE_45 (ANGLE_360 / 8)
#define ANGLE_90 (ANGLE_360 / 4)
#define ANGLE_180 (-(ANGLE_360 / 2))
#define ANGLE(x) ((x) * ANGLE_1)
#define NO_ROOM 255
#define NO_BOX 65535
#define NO_FLOOR -127
#define WALL (NO_FLOOR * 256)
#define GRAVITY 6
#define LARA_MAX_HEALTH 1000
#define LARA_MAX_OXYGEN 1800
#define LARA_HANG_SLANT 60
#define LARA_HANG_OFFSET 724
#define LARA_HEIGHT 762
#define LARA_HEIGHT_JUMP 870
#define LARA_HEIGHT_UW 400
#define LARA_HEIGHT_SURF 700
#define LARA_RADIUS 100
#define LARA_RADIUS_WATER 300
#define LARA_RADIUS_CLIMB 220
#define LARA_TURN_ACCEL (ANGLE(9) / 4)
#define LARA_TURN_JUMP ANGLE(3)
#define LARA_TURN_VERY_SLOW ANGLE(2)
#define LARA_TURN_SLOW ANGLE(4)
#define LARA_TURN_MED ANGLE(6)
#define LARA_TURN_FAST ANGLE(8)
#define LARA_TILT_ACCEL (ANGLE(3) / 2)
#define LARA_TILT_MAX ANGLE(11)
#define LARA_STEP_HEIGHT 384
#define LARA_SMASH_HEIGHT 640
#define LARA_FLOAT_UP_SPEED 5
#define LARA_SWIM_FRICTION 6
#define LARA_SWIM_ACCEL 8
#define LARA_SWIM_SPEED_MIN 133
#define LARA_SWIM_SPEED_MAX 200
#define LARA_SWIM_TIMER 10
#define LARA_SURF_FRICTION 4
#define LARA_SURF_ACCEL 8
#define LARA_SURF_SPEED_MAX 60
#define LARA_DIVE_SPEED 80
#define LARA_WADE_MIN_DEPTH 384
#define LARA_WADE_MAX_DEPTH 730
#define LARA_SWIM_MIN_DEPTH 512
#define ITEM_FLAG_GRAVITY (1u << 1)
#define ITEM_FLAG_ACTIVE (1u << 2)
#define ITEM_FLAG_STATUS (3u << 3)
#define ITEM_FLAG_STATUS_ACTIVE (1u << 3)
#define ITEM_FLAG_STATUS_INACTIVE (2u << 3)
#define ITEM_FLAG_STATUS_INVISIBLE (3u << 3)
#define ITEM_FLAG_COLLISION (1u << 5)
#define ITEM_FLAG_ANIMATED (1u << 7)
#define ITEM_FLAG_ONCE (1u << 8)
#define ITEM_FLAG_MASK (31u << 9)
#define ITEM_FLAG_REVERSE (1u << 14)
#define ITEM_FLAG_SHADOW (1u << 15)
#define ITEM_FLAG_INJURED (1u << 6)
#define BOX_OVERLAP_INDEX_MASK 0x3FFFu
#define BOX_OVERLAP_BLOCKED 0x4000u
#define BOX_OVERLAP_BLOCKABLE 0x8000u
#define NODE_FLAG_POP (1u << 0)
#define NODE_FLAG_PUSH (1u << 1)
#define FIXED_CAMERA_FLAG_ONCE (1u << 8)
#define TRACK_FLAG_MASK 31u
#define TRACK_FLAG_ONCE 32u
#define ROOM_FLAG_WATER_MASK 1u
#define STATIC_MESH_FLAG_NO_COLLISION 1u
#define STATIC_MESH_FLAG_VISIBLE 2u
#define SI_MODE(x) ((x) & 3)
#define SI_COUNT(x) (((x) >> 2) & 15)
#define SI_CAMERA(x) (((x) >> 12) & 1)
#define SI_PITCH(x) (((x) >> 13) & 1)
#define SI_GAIN(x) (((x) >> 14) & 1)
#define FD_END(x) ((x) >> 15)
#define FD_FLOOR_TYPE(x) ((x) & 0x1F)
#define FD_TRIGGER_TYPE(x) (((x) >> 8) & 0x7F)
#define FD_TIMER(x) ((x) & 0xFF)
#define FD_ONCE(x) (((x) >> 8) & 1)
#define FD_SPEED(x) (((x) >> 9) & 0x1F)
#define FD_MASK(x) (((x) >> 9) & 0x1F)
#define FD_ACTION(x) (((x) >> 10) & 0x1F)
#define FD_ARGS(x) ((x) & 0x03FF)
#define FD_SLANT_X(x) ((int8)((x) & 0xFF))
#define FD_SLANT_Z(x) ((int8)((x) >> 8))
#define MAX_ITEMS 256
#define MAX_ROOMS 139
#define MAX_ROOM_LIST 32
#define MAX_MATRICES 8
extern const uint32 gSinCosTable[4096];
/* Both components are signed Q14; the low16 angle bits select one turn. */
#define sincos(angle, s, c)                                                                        \
    do                                                                                             \
    {                                                                                              \
        uint32 tr64_sc = gSinCosTable[(uint16)(angle) >> 4];                                       \
        (s) = wrap_i32(tr64_sc) >> 16;                                                             \
        (c) = wrap_i16(tr64_sc);                                                                   \
    } while (0)
static inline int32 phd_sin(int32 angle)
{
    return wrap_i32(gSinCosTable[(uint16)angle >> 4]) >> 16;
}
#if TR64_HAS_TR2 || TR64_HAS_TR3
/* tr2-lara-ground-and-weapons, heading projection: fold the phase
 * before it is quantised by the shared Q14 table. */
static inline int32 tr2_sin(int32 turn)
{
    uint16 a=(uint16)turn;
    int32 phase=a&32767;
    if (phase>16384)
        phase=32768-phase;
    int32 s=phd_sin(phase);
    return (a&32768) ? -s : s;
}
#endif
static inline int32 i32_add(uint32 a, uint32 b)
{
    return wrap_i32(a + b);
}
static inline int32 i32_sub(uint32 a, uint32 b)
{
    return wrap_i32(a - b);
}
static inline int32 i32_mul(uint32 a, uint32 b)
{
    return wrap_i32(a * b);
}
static inline int32 i32_shl(uint32 a, uint32 b)
{
    return wrap_i32(a << b);
}
typedef char tr64_game_requires_arithmetic_right_shift[(-1 >> 1) == -1 ? 1 : -1];
#endif
