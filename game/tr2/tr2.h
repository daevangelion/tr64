/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_TR2_H
#define TR64_GAME_TR2_H
#include "tr64/game.h"
#define TR64_HAS_SEQUEL (TR64_HAS_TR2 || TR64_HAS_TR3)
#if TR64_HAS_SEQUEL
#include "tr64/level.h"
#include "game/common/types.h"

/* The sequel rule mask is bound at level entry.
 * Rule                        Source section                         Consumers */
/* Direct layout tags remain at admission/binding boundaries:
 * game/game.c (levelStart), engine/res/{pack,tex,validate,sound}.c,
 * engine/render/{bind_load,scene}.c, engine/snd/{format,snd}.c and
 * engine/save/identity.c. Those predicates select data contracts. */
enum {
    TR2_RULE_HARNESS           = 1u << 0,  /* D16 host boundary           game, sound, room, trigger, frontend */
    TR2_RULE_PACKED_POSE       = 1u << 1,  /* OpenLara animation.h:99–145, format.h:2666–2710; TR2 start, Authored links */
    TR2_RULE_FRESH_START       = 1u << 2,  /* tr2-lara-start, Lara before the first update   game, trace */
    TR2_RULE_ROOM_LINKS        = 1u << 3,  /* slide-height-and-gaze, Sector heights         room */
    TR2_RULE_SURFACE_ROUND     = 1u << 4,  /* slide-height-and-gaze, Integer rounding       room */
    TR2_RULE_BOUNDING_LERP     = 1u << 5,  /* slide-height-and-gaze, Bounds between keys    animation */
    TR2_RULE_SLIDE_ENTRY       = 1u << 6,  /* Lara Ground slopes; Slide travel and facing   lara */
    TR2_RULE_SLIDE_JUMP_VETO   = 1u << 7,  /* slide-height-and-gaze, Jumping out of a slide lara */
    TR2_RULE_RUN_JUMP_WINDOW   = 1u << 8,  /* Lara Ground, Standing or running Jump         lara */
    TR2_RULE_ROLL_CONTROL      = 1u << 9,  /* Lara Ground Roll; trace Wire conventions      game, lara */
    TR2_RULE_AIR_TWIST         = 1u << 10, /* tr2-water-and-flares, Air reversal             lara, animation */
    TR2_RULE_RUN_STEP_RISE     = 1u << 12, /* tr2-water-and-flares, Wade step-up and dry Run front gate (Q-T2W-05): lara */
    TR2_RULE_STAND_CEILING     = 1u << 13, /* Lara Ground, Ceiling or fully blocked         lara */
    TR2_RULE_STAND_DROP        = 1u << 14, /* tr2-lara-start, Standing to fall              lara */
    TR2_RULE_LEDGE_REACH       = 1u << 15, /* Lara Ground, Ordinary vault, grab and shimmy  lara */
    TR2_RULE_LETHAL_LANDING    = 1u << 16, /* Lara Ground, Falling and landing              lara */
    TR2_RULE_JUMP_IMPACT       = 1u << 17, /* Lara Ground, Jump hits a front wall           lara */
    TR2_RULE_SIDE_JUMP_SPACE   = 1u << 18, /* Lara Ground, Separate backward/side jump      lara */
    TR2_RULE_AUTOMATIC_GAZE    = 1u << 19, /* slide-height-and-gaze, Looking toward subject  game, trigger, lara, camera, camera_tick */
    TR2_RULE_FORWARD_LANDING  = 1u << 20, /* tr2-lara-start, Landing and frontal obstruction  lara */
    TR2_RULE_CAMERA_VIEW      = 1u << 21, /* tr2-camera, aim, sight, movement and renewal  camera, camera_tick, room, trigger, view */
    TR2_RULES_ALL = (TR2_RULE_CAMERA_VIEW << 1) - 1u
};

/* Keep the item slice independent of the camera's rule-mask additions. */
enum {
    TR2_RULE_WORLD_ITEMS = 1u << 22, /* tr2-items-general, defaults/campfire/flight; TR2 ordinary doors/blocks; tr2-lara-start, update order   game, world, collision, trace */
    TR2_RULE_ITEM_ACTIONS = 1u << 23, /* tr2-triggers-and-interaction, Floor evaluation and Item masks   trigger, game */
    TR2_RULE_FRESH_STOCK = 1u << 24, /* tr2-items-general, Starting inventory; tr2-lara-start, Equipment and reserves   game, trace */
    TR2_RULES_ITEMS = TR2_RULE_WORLD_ITEMS | TR2_RULE_ITEM_ACTIONS | TR2_RULE_FRESH_STOCK
};

enum {
    TR2_RULE_POSE_TAIL        = 1u << 25, /* slide-height-and-gaze, Pose sampling at a clip's tail; animation */
    TR2_RULE_MANUAL_LOOK      = 1u << 27, /* slide-height-and-gaze, Manual Look eligibility; lara */
    TR2_RULE_MOTION_PROJECTION = 1u << 28, /* tr2-lara-ground-and-weapons, Projecting speed along a heading; item */
    TR2_RULE_DRY_SIDESTEP     = 1u << 29, /* tr2-lara-ground-and-weapons, Sidestep and standing turn; game, lara */
    TR2_RULES_CAMERA = TR2_RULE_POSE_TAIL | TR2_RULE_MANUAL_LOOK |
                       TR2_RULE_MOTION_PROJECTION | TR2_RULE_DRY_SIDESTEP
};

enum {
    TR2_RULE_CLIMB_WALL = 1u << 30, /* tr2-climbing, floor marks, catch, stance (Down after advance), side exits and hang steps: lara, item, room, trigger, trace */
    TR2_RULE_BLOCKED_MOTION = 1u << 31 /* ground-and-weapons, frontal stop, blocked stance, no Forward+Action nudge (Q-T2M-02): lara */
};

enum {
    /* Implementers' choice, no authored box: terrain-only point height
     * or use the endpoint's box; neither box means no side correction. */
    TR2_CAM_NO_BOX_POINT, TR2_CAM_NO_BOX_ORBIT,
    TR2_CAM_INTERACTION, TR2_CAM_OPAQUE_RENEWAL, TR2_CAM_DIAGNOSTICS
};
enum {
    TR2_RULE_ARMS = 1u << 26 /* tr2-lara-arms, Pistol attempts/poses and Held/loose flare; Animation effects (TR2 public list, water/ledge): tr2_arms, pose, animation, game, trace */
};

enum {
    TR2_WEAPON_GRENADE = 6, /* Runtime-only selection outside the shared TR1 selection table. */
    TR2_WEAPON_FLARE = 8,
    TR2_MODEL_PISTOL_ARMS = 1, TR2_MODEL_HAIR = 2,
    TR2_MODEL_AUTO_ARMS = 4, TR2_MODEL_UZI_ARMS = 5,
    TR2_MODEL_GRENADE = 7, TR2_MODEL_FLARE_ARMS = 9,
    TR2_MODEL_LARA_EXTRA = 12,
    TR2_MODEL_GUN_FLASH = 240,
    TR2_RING_GRENADE = 163, TR2_RING_GRENADE_AMMO = 170,
    TR2_RING_FLARES = 173, TR2_ITEM_LOOSE_FLARE = 152,
    TR2_ITEM_GRENADE_PROJ = 248,
    TR2_SPRITE_WAKE = 89, TR2_SPRITE_EXPLOSION = 229, TR2_SPRITE_SPLASH = 230,
    TR2_SPRITE_BUBBLE = 231, TR2_SPRITE_BLOOD = 233, TR2_SPRITE_GLOW = 236,
    TR2_SPRITE_RICOCHET = 238, TR2_SPRITE_FLAME = 252
};
enum { TR2_BRAID_SEGMENTS = 6 };
typedef struct {
    vec3i pos[TR2_BRAID_SEGMENTS + 1];
    vec3i prev[TR2_BRAID_SEGMENTS + 1];
    uint32_t lastTick;
    uint8_t valid;
} TR2Braid;
typedef struct {
    uint32_t ammoUsed, inertBubbleCues;
    uint16_t flareAge, flareFrame;
    uint8_t uziActive[2];
    bool flareLeft, flareLit, firearmsView, gunRequested;
} TR2Arms;

/* tr2-water-and-flares: Water status before movement (Wade-to-surface
 * clears both speeds), Underwater collision (side-hit travel heading),
 * Collision envelopes (cardinal front and leading corners; item),
 * Wade step-up, Continuing poses in shallow water (Q-T2W-07), and
 * Numbered animation effects: game, room, lara, item, animation, trace. */
#define TR2_RULE_WATER (UINT64_C(1) << 32)

/* tr2-camera, Requests in and out of water (Q-T2C-12): game, camera, trace. */
#define TR2_RULE_WET_CAMERA (UINT64_C(1) << 33)
enum { TR2_CAMERA_WET_ELEVATION = -4004 };

/* tr2-camera, Combat view (Q-T2C-13): game, camera, camera_tick, trace. */
#define TR2_RULE_COMBAT_CAMERA (UINT64_C(1) << 34)

/* TR2 interactions: tr2-triggers-and-interaction (floor masks, pickups,
 * levers); tr2-great-wall-level (dragons and reward); tr2-lara-ground-and-
 * weapons (Walk frontal stop and Q-INT-03); tr2-great-wall-objects (slab
 * dependencies and boulder motion); tr2-camera (Bounce and shake). */
#define TR2_RULE_INTERACTIONS (UINT64_C(1) << 35)

/* tr2-oracle-trace, Live statistics (Q-INT-01): game. */
#define TR2_RULE_SAVED_STATS (UINT64_C(1) << 42)

/* tr2-lara-arms, Pistol lock and creature strikes; tr2-oracle-trace,
 * Creature extension in schema 21: tr2/lara_weapon, pose, trace, host admission. */
#define TR2_RULE_COMBAT (UINT64_C(1) << 36)
/* tr2-great-wall-creatures: wake, mood, paths, collision and death:
 * game, collision, tr2/enemy, tr2/nav and tr2/enemy_motion. */
#define TR2_RULE_CREATURES (UINT64_C(1) << 37)
/* Great Wall creatures, end links (Q-T2K-07): item. */
#define TR2_RULE_ITEM_END_GOAL (UINT64_C(1) << 38)
/* Great Wall creatures, shared room-effect capacity and lifetime
 * (Q-T2K-06); Items General, Emitter versus flame: game, combat, effects. */
#define TR2_RULE_ROOM_EFFECTS (UINT64_C(1) << 39)
/* Great Wall objects: nearby contact, spikes, fixed guillotine, rolling
 * ball injury and zip line; Great Wall level, Exit: finish the trigger tick. */
#define TR2_RULE_WALL_HAZARDS (UINT64_C(1) << 43)
#define TR2_RULES_WALL (TR2_RULES_ALL | TR2_RULES_ITEMS | TR2_RULES_CAMERA | \
    TR2_RULE_ARMS | TR2_RULE_CLIMB_WALL | TR2_RULE_BLOCKED_MOTION | \
    TR2_RULE_WATER | TR2_RULE_WET_CAMERA | TR2_RULE_COMBAT_CAMERA | TR2_RULE_COMBAT | \
    TR2_RULE_CREATURES | TR2_RULE_ITEM_END_GOAL | TR2_RULE_ROOM_EFFECTS | \
    TR2_RULE_INTERACTIONS | TR2_RULE_SAVED_STATS | TR2_RULE_WALL_HAZARDS)
#define TR2_RULES_TR3 (TR2_RULE_PACKED_POSE | TR2_RULE_ROOM_LINKS | \
    TR2_RULE_SURFACE_ROUND | TR2_RULE_BOUNDING_LERP | TR2_RULE_SLIDE_ENTRY | \
    TR2_RULE_SLIDE_JUMP_VETO | TR2_RULE_RUN_JUMP_WINDOW | TR2_RULE_ROLL_CONTROL | \
    TR2_RULE_AIR_TWIST | TR2_RULE_RUN_STEP_RISE | TR2_RULE_STAND_CEILING | \
    TR2_RULE_STAND_DROP | TR2_RULE_LEDGE_REACH | TR2_RULE_LETHAL_LANDING | \
    TR2_RULE_JUMP_IMPACT | TR2_RULE_SIDE_JUMP_SPACE | TR2_RULE_AUTOMATIC_GAZE | \
    TR2_RULE_FORWARD_LANDING | TR2_RULE_CAMERA_VIEW | TR2_RULE_POSE_TAIL | \
    TR2_RULE_MANUAL_LOOK | TR2_RULE_MOTION_PROJECTION | TR2_RULE_DRY_SIDESTEP | \
    TR2_RULE_CLIMB_WALL | TR2_RULE_BLOCKED_MOTION | TR2_RULE_WATER | \
    TR2_RULE_WET_CAMERA | TR2_RULE_COMBAT_CAMERA)

enum { TR2_EFFECT_SLOTS = 100 };
typedef enum {
    TR2_FX_FLAME=1, TR2_FX_SHARD, TR2_FX_SPLASH, TR2_FX_BLOOD, TR2_FX_RICOCHET,
    TR2_FX_WAKE, TR2_FX_EXPLOSION, TR2_FX_BUBBLE, TR2_FX_GUN_FLASH
} TR2EffectKind;
typedef struct {
    struct Room *room;
    vec3i pos;
    vec3s angle;
    int16_t speed, descent;
    union { uint16_t mesh, spriteRoll; } asset;
    uint8_t kind, next, age;
} TR2Effect;
typedef struct { int16_t link, queued; uint16_t generation, candidate; } TR2RouteNode;
enum { TR2_CREATURE_SLOTS = 5 };
typedef struct {
    TR2RouteNode *route;
    struct ItemObj *item, *enemy;
    int16_t jointYaw[2], maxTurn, box, targetBox, requiredBox, nextBox, headBox, tailBox;
    int16_t step, drop, fly, zoneCount;
    uint16_t flags, search, boxSearch, blockMask;
    uint8_t mood;
    int32_t destination[3], waypoint[3];
} TR2Creature;

/* Public PC IDs: docs/specs/tr2-tr3-differences.md §3.2. */
enum {
    TR2_ITEM_SPIDER = 36, TR2_ITEM_CROW = 38, TR2_ITEM_TIGER = 39,
    TR2_ITEM_BLOCK = 67, TR2_ITEM_BLOCK_FIRST = 67, TR2_ITEM_BLOCK_LAST = 70,
    TR2_ITEM_GIANT_SPIDER = 37, TR2_ITEM_REX = 214,
    TR2_ITEM_DOOR_FIRST = 106, TR2_ITEM_DOOR_LAST = 113,
    TR2_ITEM_DETONATOR = 227,
    TR2_ITEM_CAMERA_TARGET = 243,
    TR2_ITEM_CAMPFIRE = 253, TR2_ITEM_HELICOPTER = 259,
    TR2_ITEM_WALL_SWITCH = 104,
    TR2_ITEM_COLLAPSE = 55, TR2_ITEM_COLLAPSE_HIGH = 57,
    TR2_ITEM_TRAPDOOR = 114, TR2_ITEM_TRAPDOOR_FIRST = 114, TR2_ITEM_TRAPDOOR_LAST = 115,
    TR2_CARRIED_START_COUNT = 6,
    TR2_CARRIED_CAPACITY = 24
};
enum {
    TR2_ITEM_SPIKES=59, TR2_ITEM_BOULDER=60, TR2_ITEM_DISC=61, TR2_ITEM_LAUNCHER=62,
    TR2_ITEM_ROLLING_BLADE=80, TR2_ITEM_GUILLOTINE=81, TR2_ITEM_STATUE_BLADE=82,
    TR2_ITEM_SPIKE_WALL=85,
    TR2_ITEM_ZIPLINE=102, TR2_ZIP_RIDE_MARK=256,
    TR2_LARA_ZIP_MOUNT=215
};
enum {
    TR2_MODEL_LARA_BOAT=11,
    TR2_ITEM_BOAT=14,
    TR2_ITEM_DOBERMAN=15, TR2_ITEM_GUNMAN_1=16, TR2_ITEM_GUNMAN_2=17, TR2_ITEM_GUNMAN_3=18,
    TR2_ITEM_OPERA_BOSS=20,
    TR2_ITEM_RAT=21, TR2_ITEM_GONDOLA=24, TR2_ITEM_THUG=32,
    TR2_ITEM_SWING_SANDBAG=58, TR2_ITEM_DRAWBRIDGE=63, TR2_ITEM_ELEVATOR=65,
    TR2_ITEM_WINDOW=72, TR2_ITEM_FAN_PROP=76, TR2_ITEM_FALLING_CEILING=79,
    TR2_ITEM_ROLLING_CRATE=83,
    TR2_ITEM_SMALL_SWITCH=93, TR2_ITEM_FAN_AIR=95, TR2_ITEM_SWING_CRATE=96,
    TR2_ITEM_PUSH_BUTTON=103, TR2_ITEM_UW_SWITCH=105,
    TR2_ITEM_BIRDS=215, TR2_ITEM_RACE_CLOCK=216, TR2_ITEM_MINE=222,
    TR2_ITEM_WATERFALL=244
};
enum { TR2_BOAT_SLOTS = 4, TR2_ELEVATOR_SLOTS = 4, TR2_DYN_LIGHT_MAX = 8 };
typedef struct {
    int16_t itemIndex;
    uint8_t present;
    int32_t yStart, waitCounter;
} TR2Elevator;
typedef struct {
    vec3i pos;
    uint8_t intensity, falloff;
} TR2DynLight;
typedef struct {
    int16_t itemIndex;
    uint8_t present;
    int32_t turn, bowVSpeed[2], supportY;
    int16_t bank, terrainYaw;
} TR2BoatHull;
/* Great Wall creatures: hardwired local clips and joints. */
enum {
    TR2_SPIDER_LEAP_CLIP=2, TR2_CROW_FALL_CLIP=1, TR2_CROW_PERCH_CLIP=14, TR2_TIGER_DEATH_CLIP=11,
    TR2_MODEL_SHOTGUN=3, TR2_RING_SHOTGUN=158, TR2_RING_PISTOLS=157,
    TR2_RING_AUTO=159, TR2_RING_UZIS=160,
    TR2_SPIDER_BITE_JOINT=1, TR2_CROW_BITE_JOINT=14, TR2_TIGER_HEAD_JOINT=22, TR2_TIGER_BITE_JOINT=26
};
/* Authored long-gun animation bank, used by both arms and admission. */
enum { SHOTGUN_AIM, SHOTGUN_DRAW, SHOTGUN_RECOIL, SHOTGUN_STOW, SHOTGUN_LOWER, SHOTGUN_CLIPS };
enum { TR2_SHOTGUN_ANY = 0xff };
enum { TR2_SG_GOAL = 0, TR2_SG_EXIT = 1, TR2_SG_FRAME0 = 2, TR2_SG_CITED4 = 3, TR2_SG_LOOP0 = 4 };
enum {
    TR2_ANIM_TURN = 0, TR2_ANIM_BUBBLES = 3, TR2_ANIM_HANDS_FREE = 12,
    TR2_ANIM_RESERVED_16 = 16, TR2_ANIM_RESERVED_17 = 17
};
enum {
    TR2_VALIDATE_WATER_LINKS = 1,
    TR2_VALIDATE_CREATURE_ASSETS,
    TR2_VALIDATE_WORLD_ANIMATIONS,
    TR2_VALIDATE_ARMS,
    TR2_VALIDATE_INTERACTIONS,
    TR2_VALIDATE_INIT_PLACEMENT,
    TR2_VALIDATE_EXTRA_OBJECTS
};
typedef struct { uint8_t from, to, kind; const char *cite; } TR2ShotgunReq;
const TR2ShotgunReq *game_tr2_shotgun_reqs(unsigned *count);
uint8_t game_tr2_shotgun_clip_state(unsigned clip);
unsigned game_tr2_shotgun_pick(unsigned from, unsigned to, unsigned kind);
bool game_tr2_shotgun_goal(unsigned from, unsigned to, unsigned kind, unsigned frameIndex, uint8 *goal);
enum { TR2_FLAG_RETIRED = 1u }; /* Internal item flag, not a schema-20 source flag. */
enum { TR2_PLACED_CLEANUP = 1u << 15 };
static inline bool tr2DoorType(unsigned type)
{
    return type >= TR2_ITEM_DOOR_FIRST && type <= TR2_ITEM_DOOR_LAST;
}
static inline bool tr2BlockType(unsigned type)
{
    return type >= TR2_ITEM_BLOCK_FIRST && type <= TR2_ITEM_BLOCK_LAST;
}
static inline bool tr2TrapdoorType(unsigned type)
{
    return type >= TR2_ITEM_TRAPDOOR_FIRST && type <= TR2_ITEM_TRAPDOOR_LAST;
}

/* Deferred contact uses spheres only to name a real dependency. */
static inline bool tr2DeferredSphereType(unsigned type)
{
    return type == 59 || type == 60 || type == 61 ||
           type == 80 || type == 81 || type == 85;
}
typedef struct {
    uint16_t type, quantity;
} TR2RingEntry;

typedef struct {
    uint32_t input;
    uint32_t triggerCalls, dynamicFloorCalls, soundCalls, climbQueries;
    uint32_t badFrameQueries;
    uint32_t campfireRangeTicks;
    uint32_t inertLaraItemActions;
    uint32_t inertAction[32], inertLookTargets;
    uint32_t flareTicks, drawTicks, rollTicks, sidestepTicks, waterTicks, ambiguousInputTicks;
    uint16_t inertByType[TR2_PUBLIC_TYPES];
    int32_t waterDistance;
    int16_t slideAngle;
    uint16_t laraFlags;
    int16_t laraItemTimer; /* Item countdown, independent of Lara's air. */
    uint32_t ammo[7];
    bool runJumpArmed;
    uint16_t keyReplayChoice; /* Zero or public selected type plus one (cancel = 1). */
    uint16_t ringInput,ringPreviousInput;
    void (*chooseKey)(void *);
    void *chooseKeyContext;
    bool ringFlare;
    int32_t distanceFrom[3]; /* Last sampled origin; seeded before a placed start. */
    uint32_t animCount;
    uint32_t keyPauseTicks; /* Recorded menu interval, applied at the lock. */
    uint8_t lastWeapon, backWeapon;
    TR2RingEntry carried[TR2_CARRIED_CAPACITY];
    uint8_t carriedCount;
    uint16_t selectedKey;
    uint32_t pickupCommits, switchUses, interactionRefusals;
    int16_t mainSelection, keySelection;
    uint32_t cleanupEligible[256 / 32];
    uint32_t cleanupList[256 / 32];
    vec3i refusalPos;
    bool refusalValid;
    uint32_t cameraChecks[TR2_CAM_DIAGNOSTICS];
    uint32_t cameraNoBoxReported[2];
    uint32_t cameraUnavailableFrom;
    const char *cameraUnavailableReason;
    bool lookYawDenied;
    uint32_t sidestepInput;
    struct { uint16_t anim, frame; uint8_t state; } motionStart;
    bool climbEnabled;
    TR2Arms arms;
    struct ItemObj *shotgunPose; /* Reserved, not in a room or active list. */
    TR2Creature creatures[TR2_CREATURE_SLOTS];
    TR2Effect effects[TR2_EFFECT_SLOTS];
    uint8_t effectFree, effectActive;
    uint8_t campfireEffect[256]; /* One pool reference per possible placed emitter. */
    uint32_t ammoHits;
    uint16_t deathTicks;
    bool deathMenuPending;
    bool finishRequested;
    bool startMenu;
    uint8_t traceSchema;
    bool wallUnit;
    bool aiUnit;
    bool creatureUnit;
    bool boatUnit;
    bool urbanUnit;
    bool operaUnit;
    uint8_t scriptIndex;
    uint8_t mineChain;
    uint8_t boatCount;
    uint8_t elevatorCount;
    uint8_t dynLightCount;
    int16_t vehicle;
    uint16_t boatInput;
    bool boatTick;
    bool extraAnim;
    uint16_t cdFlags[64];
    int16_t pendingTrack;
    int16_t flipEffect, flipEffectTimer;
    struct ItemObj *activeNext;
    TR2BoatHull boatHulls[TR2_BOAT_SLOTS];
    TR2Elevator elevators[TR2_ELEVATOR_SLOTS];
    TR2DynLight dynLights[TR2_DYN_LIGHT_MAX];
    int16_t cargoHead[256];
    int16_t cargoNext[256];
    TR2Braid braid;
    struct {
        uint8_t burning;
        uint8_t creatureCount;
        struct {
            uint16_t itemIndex;
            int16_t jointYaw[2], maxTurn;
            uint16_t flags;
            uint8_t mood;
        } creatures[TR2_CREATURE_SLOTS];
    } savePost;
} GameTR2;

enum {
    TR2_LVL_BASE = 100,
    TR2_LVL_TITLE = 100,
    TR2_LVL_HOME = 101,
    TR2_LVL_WALL = 102,
    TR2_LVL_BOAT = 103,
    TR2_LVL_VENICE = 104,
    TR2_LVL_OPERA = 105,
    TR2_LVL_RIG = 106,
    TR2_LVL_PLATFORM = 107,
    TR2_LVL_UNWATER = 108,
    TR2_LVL_KEEL = 109,
    TR2_LVL_LIVING = 110,
    TR2_LVL_DECK = 111,
    TR2_LVL_SKIDOO = 112,
    TR2_LVL_MONASTRY = 113,
    TR2_LVL_CATACOMB = 114,
    TR2_LVL_ICECAVE = 115,
    TR2_LVL_EMPRTOMB = 116,
    TR2_LVL_FLOATING = 117,
    TR2_LVL_XIAN = 118,
    TR2_LVL_HOUSE = 119,
    TR2_LVL_GENERIC = 120,
    TR2_LVL_NEW_GAME = 121
};

/* Runtime WEAPON_PISTOLS or WEAPON_SHOTGUN. False, changing nothing, when
 * the level or Lara's hands record is missing, Lara is dead, the weapon is
 * not a carried ring gun, or a shotgun is asked for while no shotgun pose
 * slot is free now. A choice made while drawing, putting away, busy or out
 * of gun access is kept as the goal (Q-ARM-02, assumed); the slot is taken
 * at the draw, which still stops by name if none is free then. The caller
 * owns ring ordinals and pause; the arms perform the draw, holstering a
 * different readied gun first. */
bool game_tr2_request_ring_weapon(unsigned weapon);

enum { TR2_RING_PASSPORT_OPEN=120, TR2_RING_STOPWATCH=121, TR2_RING_HOME=122,
       TR2_RING_PASSPORT=133, TR2_RING_SMALL_MEDI=171, TR2_RING_LARGE_MEDI=172 };
unsigned game_tr2_ring_priority(unsigned type);
TR2RingEntry *game_tr2_ring_find(unsigned type);
unsigned game_tr2_ring_quantity(unsigned type);
bool game_tr2_ring_has_keys(void);
void game_tr2_ring_debit(TR2RingEntry *);
const char *game_tr2_level_name(int32_t level_id);
int32_t game_tr2_level_script_id(void);
int32_t game_tr2_level_script_next_id(void);
int32_t game_tr2_level_script_ambient_track(int32_t level_id);
uint32_t game_tr2_level_script_secrets_max(void);
int32_t game_tr2_level_script_lethal_y(void);
int game_tr2_capture_carry(tr64_game_inventory_carry_t *out);
void game_tr2_apply_carry(const tr64_game_inventory_carry_t *carry);
void game_tr2_attach_lara_flame(struct ItemObj *lara);
void game_tr2_add_dyn_light(vec3i pos, uint8_t intensity, uint8_t falloff);
void game_tr2_gun_flash_effect(struct Room *, vec3i pos, int16_t yaw);
vec3i game_tr2_lara_right_hand(const struct ItemObj *lara, const struct ItemObj *pose, vec3i offset);
void game_tr2_item_floor_ceiling(const struct ItemObj *item, int32_t x, int32_t y, int32_t z,
                                 int32_t *floor, int32_t *ceiling);
uint32_t game_tr2_unit_save(void *call_ptr);
void game_prepare_tr2_load(void);
void game_tr2_post_restore(void);
void game_tr2_report(void);
void game_trace_tr2(tr64_game_writer_t, void *);
void game_trace_tr2_end(tr64_game_writer_t, void *, uint32_t);
#endif
#endif
