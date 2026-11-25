/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III runtime interfaces & constants from docs/specs/wall/tr3-jungle-and-movement.md. */
#ifndef TR64_GAME_TR3_H
#define TR64_GAME_TR3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tr64/game.h"
#include "tr64/level.h"
#include "tr64/limits.h"
#include "tr64/gpu.h"

/* Core units and sentinels (tr3-jungle-and-movement.md). */
enum {
    TR3_NO_HEIGHT        = -32512,
    TR3_TILE             = 1024,
    TR3_CLICK            = 256,
    TR3_NO_ROOM          = 255,
    TR3_MAX_ITEMS        = 256,
    TR3_MAX_DARTS        = 16,
    TR3_MAX_SHOALS       = 4,
    TR3_SHOAL_FISH       = 24,
    TR3_MAX_SOUND_EVENTS = 16,
    TR3_MAX_PICKUPS      = 8,
    TR3_BRAID_SEGS       = 6,
    TR3_MAX_KEYS         = 8
};

/* Room flags (tr3-jungle-and-movement.md §1.4). */
enum {
    TR3_ROOM_WATER   = 0x0001u,
    TR3_ROOM_OUTSIDE = 0x0008u,
    TR3_ROOM_WIND    = 0x0020u,
    TR3_ROOM_INSIDE  = 0x0040u,
    TR3_ROOM_SWAMP   = 0x0080u
};

/* Surface height / slope classification (§1.2). */
enum {
    TR3_HEIGHT_WALL        = 0,
    TR3_HEIGHT_SMALL_SLOPE = 1,
    TR3_HEIGHT_BIG_SLOPE   = 2,
    TR3_HEIGHT_DIAGONAL    = 3,
    TR3_HEIGHT_SPLIT_TRI   = 4
};

/* Floor-data opcodes (§1.2 & §1.3). */
enum {
    TR3_FD_PORTAL         = 0x01,
    TR3_FD_FLOOR_SLOPE    = 0x02,
    TR3_FD_CEIL_SLOPE     = 0x03,
    TR3_FD_TRIGGER        = 0x04,
    TR3_FD_DEATH          = 0x05,
    TR3_FD_CLIMB          = 0x06,
    TR3_FD_SPLIT_F_NWSE   = 0x07,
    TR3_FD_SPLIT_F_NESW   = 0x08,
    TR3_FD_SPLIT_C_NWSE   = 0x09,
    TR3_FD_SPLIT_C_NESW   = 0x0A,
    TR3_FD_SPLIT_FP_SW    = 0x0B,
    TR3_FD_SPLIT_FP_NE    = 0x0C,
    TR3_FD_SPLIT_FP_NW    = 0x0D,
    TR3_FD_SPLIT_FP_SE    = 0x0E,
    TR3_FD_SPLIT_CP_SW    = 0x0F,
    TR3_FD_SPLIT_CP_NE    = 0x10,
    TR3_FD_SPLIT_CP_NW    = 0x11,
    TR3_FD_SPLIT_CP_SE    = 0x12,
    TR3_FD_MONKEY         = 0x13,
    TR3_FD_MINECART_LEFT  = 0x14,
    TR3_FD_MINECART_RIGHT = 0x15
};

/* Trigger subtypes (§1.3). */
enum {
    TR3_TRIG_TRIGGER          = 0,
    TR3_TRIG_PAD              = 1,
    TR3_TRIG_SWITCH           = 2,
    TR3_TRIG_KEY              = 3,
    TR3_TRIG_PICKUP           = 4,
    TR3_TRIG_HEAVY            = 5,
    TR3_TRIG_ANTIPAD          = 6,
    TR3_TRIG_COMBAT           = 7,
    TR3_TRIG_DUMMY            = 8,
    TR3_TRIG_ANTITRIGGER      = 9,
    TR3_TRIG_HEAVY_SWITCH     = 10,
    TR3_TRIG_HEAVY_ANTITRIG   = 11,
    TR3_TRIG_MONKEY           = 12
};

/* Trigger actions (§1.3). */
enum {
    TR3_ACT_OBJECT       = 0,
    TR3_ACT_CAMERA       = 1,
    TR3_ACT_SINK         = 2,
    TR3_ACT_FLIPMAP      = 3,
    TR3_ACT_FLIP_ON      = 4,
    TR3_ACT_FLIP_OFF     = 5,
    TR3_ACT_TARGET       = 6,
    TR3_ACT_FINISH       = 7,
    TR3_ACT_CD           = 8,
    TR3_ACT_FLIPEFFECT   = 9,
    TR3_ACT_SECRET       = 10,
    TR3_ACT_CLEAR_BODIES = 11
};

/* TR3 climbable bitmask bits (§1.3). */
enum {
    TR3_CLIMB_NORTH  = 0x01u,
    TR3_CLIMB_EAST   = 0x02u,
    TR3_CLIMB_SOUTH  = 0x04u,
    TR3_CLIMB_WEST   = 0x08u,
    TR3_CLIMB_MONKEY = 0x10u
};

/* TR3 Lara states (§2.1). */
enum {
    TR3_ST_WALK           = 0,
    TR3_ST_RUN            = 1,
    TR3_ST_STOP           = 2,
    TR3_ST_FORWARD_JUMP   = 3,
    TR3_ST_FAST_BACK      = 5,
    TR3_ST_TURN_R         = 6,
    TR3_ST_TURN_L         = 7,
    TR3_ST_DEATH          = 8,
    TR3_ST_FAST_FALL      = 9,
    TR3_ST_HANG           = 10,
    TR3_ST_REACH          = 11,
    TR3_ST_SPLAT          = 12,
    TR3_ST_TREAD          = 13,
    TR3_ST_COMPRESS       = 15,
    TR3_ST_BACK           = 16,
    TR3_ST_SWIM           = 17,
    TR3_ST_GLIDE          = 18,
    TR3_ST_PULL_UP        = 19,
    TR3_ST_FAST_TURN      = 20,
    TR3_ST_STEP_RIGHT     = 21,
    TR3_ST_STEP_LEFT      = 22,
    TR3_ST_SLIDE          = 24,
    TR3_ST_BACK_JUMP      = 25,
    TR3_ST_RIGHT_JUMP     = 26,
    TR3_ST_LEFT_JUMP      = 27,
    TR3_ST_UP_JUMP        = 28,
    TR3_ST_FALL_BACK      = 29,
    TR3_ST_HANG_LEFT      = 30,
    TR3_ST_HANG_RIGHT     = 31,
    TR3_ST_SLIDE_BACK     = 32,
    TR3_ST_SURF_TREAD     = 33,
    TR3_ST_SURF_SWIM      = 34,
    TR3_ST_DIVE           = 35,
    TR3_ST_PUSH_BLOCK     = 36,
    TR3_ST_PULL_BLOCK     = 37,
    TR3_ST_PP_READY       = 38,
    TR3_ST_PICKUP         = 39,
    TR3_ST_SWITCH_ON      = 40,
    TR3_ST_SWITCH_OFF     = 41,
    TR3_ST_USE_KEY        = 42,
    TR3_ST_USE_PUZZLE     = 43,
    TR3_ST_UW_DEATH       = 44,
    TR3_ST_ROLL           = 45,
    TR3_ST_SWAN_DIVE      = 52,
    TR3_ST_FAST_DIVE      = 53,
    TR3_ST_CLIMB_STANCE   = 56,
    TR3_ST_CLIMBING       = 57,
    TR3_ST_CLIMB_LEFT     = 58,
    TR3_ST_CLIMB_END      = 59,
    TR3_ST_CLIMB_RIGHT    = 60,
    TR3_ST_CLIMB_DOWN     = 61,
    TR3_ST_WADE           = 65,
    TR3_ST_ZIPLINE        = 70,
    TR3_ST_CROUCH_IDLE    = 71,
    TR3_ST_CROUCH_ROLL    = 72,
    TR3_ST_SPRINT         = 73,
    TR3_ST_SPRINT_ROLL    = 74,
    TR3_ST_MONKEY_IDLE    = 75,
    TR3_ST_MONKEY_FORWARD = 76,
    TR3_ST_MONKEY_LEFT    = 77,
    TR3_ST_MONKEY_RIGHT   = 78,
    TR3_ST_MONKEY_ROLL    = 79,
    TR3_ST_CRAWL_IDLE     = 80,
    TR3_ST_CRAWL_FORWARD  = 81,
    TR3_ST_MONKEY_TURN_L  = 82,
    TR3_ST_MONKEY_TURN_R  = 83,
    TR3_ST_CRAWL_TURN_L   = 84,
    TR3_ST_CRAWL_TURN_R   = 85,
    TR3_ST_CRAWL_BACK     = 86,
    TR3_ST_CLIMB_TO_CRAWL = 87,
    TR3_ST_CRAWL_TO_CLIMB = 88
};

/* TR3 object type IDs (§3.1, §3.7, §3.8). */
enum {
    TR3_OBJ_LARA           = 0,
    TR3_OBJ_PISTOLS_ANIM   = 1,
    TR3_OBJ_BRAID          = 2,
    TR3_OBJ_SHOTGUN_ANIM   = 3,
    TR3_OBJ_DEAGLE_ANIM    = 4,
    TR3_OBJ_UZI_ANIM       = 5,
    TR3_OBJ_MP5_ANIM       = 6,
    TR3_OBJ_ROCKET_ANIM    = 7,
    TR3_OBJ_GRENADE_ANIM   = 8,
    TR3_OBJ_HARPOON_ANIM   = 9,
    TR3_OBJ_FLARE_ANIM     = 10,
    TR3_OBJ_TIGER          = 28,
    TR3_OBJ_CARCASS        = 38,
    TR3_OBJ_MONKEY         = 71,
    TR3_OBJ_AI_GUARD       = 74,
    TR3_OBJ_AI_AMBUSH      = 75,
    TR3_OBJ_AI_PATROL1     = 76,
    TR3_OBJ_AI_MODIFY      = 77,
    TR3_OBJ_AI_FOLLOW      = 78,
    TR3_OBJ_AI_PATROL2     = 79,
    TR3_OBJ_AI_X1          = 80,
    TR3_OBJ_AI_X2          = 81,
    TR3_OBJ_AI_X3          = 82,
    TR3_OBJ_FALL_BLOCK1    = 83,
    TR3_OBJ_FALL_BLOCK2    = 84,
    TR3_OBJ_FALL_BLOCK3    = 85,
    TR3_OBJ_SPIKES         = 87,
    TR3_OBJ_BOULDER1       = 88,
    TR3_OBJ_BOULDER4       = 89,
    TR3_OBJ_POISON_DART    = 90,
    TR3_OBJ_DART_EMITTER   = 91,
    TR3_OBJ_PUSHBLOCK1     = 97,
    TR3_OBJ_PUSHBLOCK2     = 98,
    TR3_OBJ_PUSHBLOCK3     = 99,
    TR3_OBJ_PUSHBLOCK4     = 100,
    TR3_OBJ_BOULDER2       = 112,
    TR3_OBJ_SPIKE_WALL     = 114,
    TR3_OBJ_MESH_SWAP2     = 123,
    TR3_OBJ_MESH_SWAP3     = 124,
    TR3_OBJ_BOULDER3       = 126,
    TR3_OBJ_ZIPLINE        = 127,
    TR3_OBJ_SWITCH_BUTTON  = 128,
    TR3_OBJ_SWITCH_NORMAL  = 129,
    TR3_OBJ_SWITCH_UW      = 130,
    TR3_OBJ_DOOR_FIRST     = 131,
    TR3_OBJ_DOOR_LAST      = 138,
    TR3_OBJ_TRAPDOOR_FIRST = 139,
    TR3_OBJ_TRAPDOOR_LAST  = 141,
    TR3_OBJ_PISTOLS_ITEM   = 160,
    TR3_OBJ_SHOTGUN_ITEM   = 161,
    TR3_OBJ_DEAGLE_ITEM    = 162,
    TR3_OBJ_UZI_ITEM       = 163,
    TR3_OBJ_HARPOON_ITEM   = 164,
    TR3_OBJ_MP5_ITEM       = 165,
    TR3_OBJ_ROCKET_ITEM    = 166,
    TR3_OBJ_GRENADE_ITEM   = 167,
    TR3_OBJ_PISTOLS_AMMO   = 168,
    TR3_OBJ_SHOTGUN_AMMO   = 169,
    TR3_OBJ_DEAGLE_AMMO    = 170,
    TR3_OBJ_UZI_AMMO       = 171,
    TR3_OBJ_HARPOON_AMMO   = 172,
    TR3_OBJ_MP5_AMMO       = 173,
    TR3_OBJ_ROCKET_AMMO    = 174,
    TR3_OBJ_GRENADE_AMMO   = 175,
    TR3_OBJ_SMALL_MEDI     = 176,
    TR3_OBJ_LARGE_MEDI     = 177,
    TR3_OBJ_FLARE_BOX      = 178,
    TR3_OBJ_FLARE_ITEM     = 179,
    TR3_OBJ_SAVE_CRYSTAL   = 180,
    TR3_OBJ_KEY_FIRST      = 224,
    TR3_OBJ_KEY4           = 227,
    TR3_OBJ_KEY_LAST       = 227,
    TR3_OBJ_KEYHOLE_FIRST  = 232,
    TR3_OBJ_KEYHOLE4       = 235,
    TR3_OBJ_KEYHOLE_LAST   = 235,
    TR3_OBJ_ARTIFACT_FIRST = 240,
    TR3_OBJ_INFADA_STONE   = 240,
    TR3_OBJ_ARTIFACT_LAST  = 243,
    TR3_OBJ_CAMERA_TARGET  = 304,
    TR3_OBJ_SKIN_JOINTS    = 315,
    TR3_OBJ_BIG_FLAME      = 330,
    TR3_OBJ_SMALL_FLAME    = 331,
    TR3_OBJ_JET_FLAME      = 332,
    TR3_OBJ_SIDE_FLAME     = 333,
    TR3_OBJ_PIRANHAS       = 338,
    TR3_OBJ_TROPICAL_FISH  = 339,
    TR3_OBJ_SKYBOX         = 355
};

/* TR3 inventory ring model IDs (tr3-jungle-and-movement.md §3.1). */
enum {
    TR3_RING_PASSPORT   = 145,
    TR3_RING_STOPWATCH  = 146,
    TR3_RING_DETAIL     = 181,
    TR3_RING_SOUND      = 182,
    TR3_RING_CONTROLS   = 183,
    TR3_RING_PISTOLS    = 185,
    TR3_RING_SHOTGUN    = 186,
    TR3_RING_DEAGLE     = 187,
    TR3_RING_UZIS       = 188,
    TR3_RING_HARPOON    = 189,
    TR3_RING_MP5        = 190,
    TR3_RING_ROCKET     = 191,
    TR3_RING_GRENADE    = 192,
    TR3_RING_SMALL_MEDI = 201,
    TR3_RING_LARGE_MEDI = 202,
    TR3_RING_FLARES     = 203,
    TR3_RING_CRYSTAL    = 204,
    TR3_RING_KEY_FIRST  = 228,
    TR3_RING_KEY4       = 231
};

/* AI marker bitmasks (§3.3). */
enum {
    TR3_AI_GUARD   = 0x01u,
    TR3_AI_AMBUSH  = 0x02u,
    TR3_AI_PATROL1 = 0x04u,
    TR3_AI_MODIFY  = 0x08u,
    TR3_AI_FOLLOW  = 0x10u
};

/* TR3 extended input bits (superset of TR64_GAME_*). */
enum {
    TR3_IN_LEFT       = TR64_GAME_LEFT,
    TR3_IN_RIGHT      = TR64_GAME_RIGHT,
    TR3_IN_FORWARD    = TR64_GAME_FORWARD,
    TR3_IN_BACK       = TR64_GAME_BACK,
    TR3_IN_JUMP       = TR64_GAME_JUMP,
    TR3_IN_WALK       = TR64_GAME_WALK,
    TR3_IN_ACTION     = TR64_GAME_ACTION,
    TR3_IN_WEAPON     = TR64_GAME_WEAPON,
    TR3_IN_LOOK       = TR64_GAME_LOOK,
    TR3_IN_SELECT     = TR64_GAME_SELECT,
    TR3_IN_FLARE      = 1u << 13,
    TR3_IN_ROLL       = 1u << 14,
    TR3_IN_STEP_RIGHT = 1u << 15,
    TR3_IN_STEP_LEFT  = 1u << 16,
    TR3_IN_SPRINT     = 1u << 17,
    TR3_IN_CROUCH     = 1u << 18,
    TR3_IN_MEDIPACK   = 1u << 19,
    TR3_IN_SWAP_GUN   = 1u << 20
};

/* TR3 weapon slots. */
enum {
    TR3_WP_NONE = 0,
    TR3_WP_PISTOLS,
    TR3_WP_SHOTGUN,
    TR3_WP_DEAGLE,
    TR3_WP_UZIS,
    TR3_WP_MP5,
    TR3_WP_HARPOON,
    TR3_WP_GRENADE,
    TR3_WP_ROCKET,
    TR3_WP_COUNT
};

/* Lara water status (§2.5 & §2.6). */
enum {
    TR3_WATER_ABOVE   = 0,
    TR3_WATER_UNDER   = 1,
    TR3_WATER_SURFACE = 2,
    TR3_WATER_WADE    = 3
};

/* Declarative India campaign metadata (§4.2). */
typedef struct {
    int32_t id;
    const char *name;
    const char *title;
    int32_t ambient_track;
    uint8_t rain;
    uint8_t cold;
    uint8_t secrets_total;
    uint16_t finish_pickup_type;
} tr3_script_level_t;

/* Unpacked TR3 16-bit sector box word (§1.1). */
typedef struct {
    uint8_t footstep;
    uint16_t box_index; /* 0x7FF = none */
    uint8_t stopper;
} tr3_box_word_t;

/* Evaluated floor & ceiling sample at (x, y, z) in a room (§1.2). */
typedef struct {
    int32_t floor_y;
    int32_t ceiling_y;
    int32_t water_y;
    int8_t tilt_x;
    int8_t tilt_z;
    uint8_t height_type;   /* TR3_HEIGHT_* */
    uint8_t climb_flags;   /* TR3_CLIMB_* (incl. bit 4 monkey-swing) */
    uint8_t death_floor;
    uint8_t minecart_flags;/* bit 0 left, bit 1 right */
    uint8_t in_swamp;
    uint8_t in_water;
    uint16_t room;
    uint16_t trigger_index;
} tr3_floor_sample_t;

/* Runtime item instance (up to TR3_MAX_ITEMS in state pool). */
typedef struct {
    uint16_t type;
    uint16_t room;
    uint16_t init_room;
    uint16_t flags;       /* placed flags + activation code bits 9..13 */
    int32_t x, y, z;
    int32_t init_x, init_y, init_z;
    int16_t rot_x, rot_y, rot_z;
    int16_t init_rot_y;
    int16_t hp;
    int16_t speed;
    int16_t fall_speed;
    int16_t shade;
    uint16_t anim;
    uint16_t frame;
    uint8_t state;
    uint8_t goal_state;
    uint8_t active;
    uint8_t invisible;
    uint8_t hit_status;
    uint8_t killed;
    uint8_t one_shot_locked;
    uint8_t ai_bits;      /* TR3_AI_* */
    int16_t ai_yaw;
    int16_t timer;
    int16_t carried_item; /* -1 = none */
    int16_t target_item;  /* -1 = none */
    uint8_t hit_latch;
    uint8_t mood;         /* 0 bored, 1 attack, 2 escape, 3 stalk */
    uint8_t mesh_swap;    /* 0 none, 1 swap2(123), 2 swap3(124) */
    uint8_t pad;
} tr3_item_t;

/* Active Poison Dart projectile (§3.7). */
typedef struct {
    uint8_t active;
    uint16_t room;
    int32_t x, y, z;
    int16_t yaw;
    int16_t life;
} tr3_dart_t;

/* Piranha shoal state (§3.5). */
typedef struct {
    uint8_t active;
    uint16_t item_index;
    uint16_t room;
    int32_t anchor_x, anchor_y, anchor_z;
    int32_t range_x, range_y, range_z;
    int32_t fish_x[TR3_SHOAL_FISH];
    int32_t fish_y[TR3_SHOAL_FISH];
    int32_t fish_z[TR3_SHOAL_FISH];
    int16_t fish_yaw[TR3_SHOAL_FISH];
    uint8_t blood_cd;
} tr3_shoal_t;

/* 6-segment physics braid node (§2.6). */
typedef struct {
    int32_t x, y, z;
    int32_t vx, vy, vz;
    int16_t rot_x, rot_y;
} tr3_braid_node_t;

/* Lara player state (§2). */
typedef struct {
    int32_t x, y, z;
    int32_t prev_x, prev_y, prev_z;
    int16_t rot_x, rot_y, rot_z;
    int16_t turn_rate;
    int16_t move_angle;
    int16_t head_x, head_y;
    int16_t torso_x, torso_y;
    int16_t speed;
    int16_t fall_speed;
    uint16_t room;
    uint16_t anim;
    uint16_t frame;
    uint8_t state;
    uint8_t goal_state;
    uint8_t water_status;
    uint8_t gravity;
    uint8_t forced_crouch;
    uint8_t hands_busy;
    uint8_t burning;
    uint8_t on_zipline;
    int16_t zipline_item;
    int16_t hp;           /* 0..1000 */
    int16_t air;          /* -1..1800 */
    int16_t stamina;      /* 0..120 */
    int16_t warmth;       /* -1..600 */
    int16_t poison;       /* 0..256 */
    int16_t current_vx;   /* TR3 smooth water current velocity (10-bit frac) */
    int16_t current_vz;
    int16_t current_sink; /* 0 = none, 1+index = active sink */
    uint8_t weapon;       /* TR3_WP_* */
    uint8_t guns_drawn;
    uint8_t fire_cd;
    uint8_t muzzle_timer;
    int16_t target_item;  /* -1 = none */
    uint8_t flare_lit;
    uint16_t flare_age;   /* 0..900 (30 s burn) */
    uint16_t owned_weapons; /* bitmask (1 << TR3_WP_*) */
    int32_t ammo[TR3_WP_COUNT];
    int16_t small_medi;
    int16_t large_medi;
    int16_t flares;
    int16_t crystals;
    uint8_t keys[TR3_MAX_KEYS];
    uint8_t artifacts;
    tr3_braid_node_t braid[TR3_BRAID_SEGS + 1];
    uint8_t braid_init;
} tr3_lara_t;

/* Third-person chase/look camera. */
typedef struct {
    int32_t eye[3];
    int32_t target[3];
    int32_t basis[9];
    uint16_t room;
    int16_t yaw;
    int16_t pitch;
    int32_t dist;
    int16_t fixed_cam; /* -1 = chase */
    int16_t fixed_timer;
    int16_t look_item; /* -1 = none */
    uint8_t initialized;
} tr3_camera_t;

/* Complete TR3 runtime state (allocated once in TR64_MEM_state). */
typedef struct {
    int32_t state;
    int32_t page;
    int32_t ring_kind;
    int32_t item_count;
    int32_t item_index;
    int32_t option_index;
    int32_t passport_page;
    int32_t rot;
    int32_t rot_target;
    int32_t rot_item;
    int32_t height;
    int32_t radius;
    int32_t sel_dist;
    int32_t frame_index;
    int32_t frame_target;
    int32_t phase_ticks;
    int32_t pending_level;
    uint32_t pending_transition;
    int32_t save_msg_str;
    int32_t save_msg_ticks;
    uint16_t last_input;
    uint16_t item_types[TR64_GAME_MENU_ITEMS];
} tr3_ring_state_t;

typedef struct {
    const tr64_level_t *level;
    const tr3_script_level_t *script;
    char level_name[32];
    uint32_t ticks;
    uint32_t rng;
    uint32_t secrets_mask;
    uint8_t secrets_found;
    uint8_t flipped;
    uint8_t flip_flags[10];
    uint8_t ally_hostile;
    uint8_t cd_played[128];
    uint16_t item_count;
    tr3_item_t items[TR3_MAX_ITEMS];
    tr3_dart_t darts[TR3_MAX_DARTS];
    uint8_t shoal_count;
    tr3_shoal_t shoals[TR3_MAX_SHOALS];
    tr3_lara_t lara;
    tr3_camera_t camera;
    tr3_ring_state_t ring;
    tr64_game_stop_t stop;
    tr64_game_settings_t settings;
    tr64_game_music_request_t music;
    tr64_game_level_stats_t stats;
    tr64_game_sound_event_t sound_events[TR3_MAX_SOUND_EVENTS];
    uint32_t sound_event_count;
    tr64_game_pickup_event_t pickup_events[TR3_MAX_PICKUPS];
    uint32_t pickup_event_count;
    uint32_t last_input;
    uint8_t entities_loaded;
    uint8_t last_height_type;
    uint8_t last_climb_flags;
    uint8_t pad_state;
} tr3_state_t;

/* Paged code-unit dispatch packet for game/tr3/jungle.c (`tr3_entities`). */
enum {
    TR3_ENT_INIT     = 1,
    TR3_ENT_TICK     = 2,
    TR3_ENT_INTERACT = 3,
    TR3_ENT_HIT      = 4
};

typedef struct {
    uint32_t op;
    tr3_state_t *st;
    uint32_t input;
    int32_t target_idx;
    int32_t damage;
} tr3_entity_call_t;

/* Global state pointer (resident in .bss; storage lives in TR64_MEM_state). */
extern tr3_state_t *tr3_gs;

/* Fixed-point trig helpers (Q14, 65536 = 360 deg). */
int32_t tr3_sin(int32_t angle);
int32_t tr3_cos(int32_t angle);
int16_t tr3_atan2(int32_t dx, int32_t dz);
uint32_t tr3_isqrt(uint32_t v);
uint32_t tr3_rand(tr3_state_t *st);

/* Declarative India campaign script lookup (§4.2). */
const tr3_script_level_t *tr3_script_by_name(const char *name);
const tr3_script_level_t *tr3_script_by_id(int32_t id);

/* Model lookup helper. */
const tr64_core_models_t *tr3_find_model(const tr64_level_t *level, uint32_t type);

/* Sound / pickup event helpers (resident in world.c). */
void tr3_emit_sound(tr3_state_t *st, int32_t sound_id, const int32_t *pos);
void tr3_request_music(tr3_state_t *st, int32_t track);
void tr3_record_pickup(tr3_state_t *st, int32_t type, int32_t x, int32_t y, int32_t z);

/* Floor-data engine (game/tr3/floor.c). */
tr3_box_word_t tr3_unpack_box_word(uint16_t raw);
void tr3_eval_split_tri(uint16_t w0, uint16_t w1, int32_t base_y, int32_t dx, int32_t dz,
                        bool chunky_cam, int32_t *out_y, int8_t *out_tx, int8_t *out_tz,
                        uint8_t *out_class, bool *out_portal_open);
uint16_t tr3_physical_room(const tr3_state_t *st, uint16_t room);
uint16_t tr3_room_flags(const tr3_state_t *st, uint16_t room);
const tr64_core_sectors_t *tr3_get_sector(const tr3_state_t *st, int32_t x, int32_t y, int32_t z,
                                          uint16_t *io_room);
void tr3_floor_eval(const tr3_state_t *st, int32_t x, int32_t y, int32_t z,
                    uint16_t room, bool chunky_cam, tr3_floor_sample_t *out);
void tr3_test_triggers(tr3_state_t *st, uint16_t room, int32_t x, int32_t y, int32_t z,
                       bool heavy, int16_t heavy_item_idx);

/* Lara movement, animation, combat & status (game/tr3/lara.c). */
void tr3_lara_init(tr3_state_t *st);
void tr3_lara_sync_from_tr3(tr3_state_t *st);
void tr3_lara_set_anim(tr3_state_t *st, uint16_t clip_rel, uint8_t state);
void tr3_lara_use_medipack(tr3_state_t *st, bool large);
void tr3_lara_tick(tr3_state_t *st, uint32_t input);
void tr3_camera_update(tr3_state_t *st, uint32_t input);

/* Extended TR3 tick & 3D ring menu entries (for play-tr3.c & host tests). */
void game_tr3_tick(uint32_t input_mask);
bool game_tr3_ring_open(int page);
bool game_tr3_ring_update(uint16_t input);
bool game_tr3_ring_active(void);
void tr3_settings_remember(const tr64_game_settings_t *settings);
uint32_t tr3_emit_ring_model(const tr3_state_t *st, uint32_t model_type, uint16_t frame_rel,
                             const int32_t basis[9], const int32_t trans[3],
                             uint32_t inst_id, int32_t light_offset,
                             tr64_game_drawable_t *buf, uint32_t cap);

/* Paged code-unit entry point (game/tr3/jungle.c) & resident wrapper (world.c). */
uint32_t tr64_unit_tr3_entities(uint32_t arg);
uint32_t tr3_entities_call(tr3_state_t *st, uint32_t op, uint32_t input,
                           int32_t target_idx, int32_t damage);

#endif
