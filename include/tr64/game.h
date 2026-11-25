/* SPDX-License-Identifier: MIT */
#ifndef TR64_GAME_H
#define TR64_GAME_H
#include <stdint.h>
#include <stdbool.h>
#include "tr64/gpu.h"
#if !defined(TR64_HOST) && TR64_GAME == 2
#define TR64_CART_TR2 1
#else
#define TR64_CART_TR2 0
#endif
#if defined(TR64_HOST) || TR64_CART_TR2
#define TR64_HAS_TR2 1
#else
#define TR64_HAS_TR2 0
#endif
#if !defined(TR64_HOST) && TR64_GAME == 3
#define TR64_CART_TR3 1
#else
#define TR64_CART_TR3 0
#endif
#if TR64_GAME == 3
#define TR64_HAS_TR3 1
#else
#define TR64_HAS_TR3 0
#endif
struct tr64_level;
/* InputState action bits from the fixed game, independent of platform pads. */
enum {
 TR64_GAME_LEFT=2, TR64_GAME_RIGHT=4, TR64_GAME_FORWARD=8,
 TR64_GAME_BACK=16, TR64_GAME_JUMP=32, TR64_GAME_WALK=64,
 TR64_GAME_ACTION=128, TR64_GAME_WEAPON=256, TR64_GAME_LOOK=512,
 TR64_GAME_SELECT=2048
};
typedef void (*tr64_game_writer_t)(void *context,const char *bytes,uint32_t count);
void game_init(void);
#if TR64_HAS_TR2 || TR64_HAS_TR3
enum { TR2_PUBLIC_TYPES = 265 }; /* Schema 20, Record fields. */
/* Schema-20 wire inputs. The raw pad decoder lives in engine/in. */
enum {
 TR2_IN_FLARE=8192, TR2_IN_ROLL=16384,
 TR2_IN_STEP_RIGHT=32768, TR2_IN_STEP_LEFT=65536,
 TR2_INPUT_MASK=123902
};
#endif
#if TR64_HAS_TR2
void game_tr2_start(const struct tr64_level *,const char *,unsigned schema);
bool game_tr2_place(const int32_t placement[5]);
void game_tr2_tick(uint32_t mask);
uint32_t game_tr2_state_hash(void);
void game_tr2_position(int32_t xyz[3]);
/* Ring presentation does not consume simulation ticks. The input/trace drivers
 * submit the same completed choice; type zero is an ordinary cancellation. */
bool game_tr2_ring_open(bool key_ring);
bool game_tr2_ring_update(uint16_t input);
unsigned game_tr2_ring_choice(void);
void game_tr2_ring_commit(unsigned type,uint32_t pause_ticks);
void game_tr2_bind_key_ring(void (*choose)(void *),void *context);
/* Recorded ring commands: KEY choices must be consumed by an in-reach
 * Action during this update. Inventory's recorded 2048 marker is not a
 * live control. */
enum { TR2_RING_EVENT=1u<<9, TR2_RING_EVENT_KEY=1u<<10 };
void game_tr2_route_update(uint32_t input,unsigned ring_command,uint32_t pause_ticks);

/* Native TR2 inventory displays are edition-specific; shared names retain
 * the original string IDs below this range. */
enum {
 TR64_STR_TR2_WALL=256,TR64_STR_TR2_GUARDHOUSE,
 TR64_STR_TR2_FLARES,TR64_STR_TR2_AUTOMAGS,TR64_STR_TR2_HARPOON,
 TR64_STR_TR2_M16,TR64_STR_TR2_GRENADE,TR64_STR_TR2_AMMO_AUTOMAGS,
 TR64_STR_TR2_AMMO_HARPOON,TR64_STR_TR2_AMMO_M16,TR64_STR_TR2_AMMO_GRENADE,
 TR64_STR_TR2_QUEST,TR64_STR_TR2_VENICE,TR64_STR_TR2_GENERIC,TR64_STR_TR2_END
};
#endif
#if TR64_GAME == 3
/* Native TR3 inventory & India level display names (disjoint from shared IDs). */
enum {
 TR64_STR_TR3_HOUSE=300,TR64_STR_TR3_JUNGLE,TR64_STR_TR3_TEMPLE,
 TR64_STR_TR3_QUADCHAS,TR64_STR_TR3_TONYBOSS,TR64_STR_TR3_DEAGLE,
 TR64_STR_TR3_MP5,TR64_STR_TR3_HARPOON,TR64_STR_TR3_ROCKET,
 TR64_STR_TR3_GRENADE,TR64_STR_TR3_FLARES,TR64_STR_TR3_CRYSTAL,
 TR64_STR_TR3_INDRA_KEY,TR64_STR_TR3_GENERIC,TR64_STR_TR3_END
};
#endif
enum {
 TR64_LARA_ABILITIES_TR1=0, TR64_LARA_ABILITIES_TR2=1, TR64_LARA_ABILITIES_TR3=2
};
enum {
 TR64_GAME_EXTRA_SPRINT = 1u << 17,
 TR64_GAME_EXTRA_CROUCH = 1u << 18
};
typedef struct {
 int32_t video_gamma,video_fog,video_fps,video_vsync;
 int32_t audio_sfx,audio_music,controls_vibration,controls_swap;
 int32_t video_filter,video_auto_dist,controls_abilities;
} tr64_game_settings_t;
/* Borrowed until level release. Settings are platform preferences, not V3 save
 * payload; loading a game retains the current settings just as the source. */
const tr64_game_settings_t *game_settings_view(void);
/* Apply a validated persisted preference record after level start. */
int game_settings_apply(const tr64_game_settings_t *settings);
int32_t game_level_id(void); /* -1 when no game is loaded */
enum { TR64_GAME_MUSIC_DIRECT,TR64_GAME_MUSIC_RESTORE };
typedef struct {
 int32_t track,ambient;
 uint32_t serial,reason;
} tr64_game_music_request_t;
/* Pure resident 16-byte request. Serial changes even for repeated direct
 * requests. RESTORE preserves source track/save/hash state; the engine resumes
 * the level ambient rather than replaying an already completed one-shot cue.
 * Playback completion remains engine-owned. */
void game_music_request(tr64_game_music_request_t *out);
/* Core is borrowed, immutable, and must remain resident. Name is the canonical
 * route/catalog name, never the pack's lexicographic directory ordinal. All
 * writable simulation storage is one allocation in the state pool. */
void game_level_start(const struct tr64_level *core,const char *level_name);
/* Resident canonical campaign names, including TITLE; NULL for unsupported ids. */
const char *game_level_name(int32_t level_id);
/* Caller-owned IN-PROCESS globals across a source-requested Core transition.
 * Capture before teardown, retain outside level/state pools. Includes live
 * ring/header/settings history and diagnostic tick, not navigation/world data.
 * Never persist this native-ABI capsule; cartridge saves use game_save instead.
 * Manual debug/fresh starts use game_level_start. */
enum { TR64_GAME_INVENTORY_SLOTS=26, TR64_GAME_CARRY_GLOBAL_BYTES=936 };
enum {
 TR64_GAME_TRANSITION_CAMPAIGN,TR64_GAME_TRANSITION_RESTART,
 TR64_GAME_TRANSITION_NEW_GAME,TR64_GAME_TRANSITION_TITLE,
 TR64_GAME_TRANSITION_GYM,TR64_GAME_TRANSITION_LOAD
};
typedef struct {
 int32_t from_level_id;
 int32_t counts[TR64_GAME_INVENTORY_SLOTS];
 uint32_t tick;
 uint32_t transition;
 uint8_t source_globals[TR64_GAME_CARRY_GLOBAL_BYTES];
 /* playersExtra[0].camera.target.pos, the sound listener a cutscene level never
  * writes (item.h:66 reads it; the source leaves the previous level's). */
 int32_t camera_target[3];
} tr64_game_inventory_carry_t;
/* Returns zero without writing out if no game, NULL out, or invalid counts. */
int game_inventory_capture(tr64_game_inventory_carry_t *out);
/* Same one initialization path, applying source inventory transition rules
 * before Lara/item constructors. A NULL or invalid carry is a named error. */
void game_level_start_transition(const struct tr64_level *core,const char *level_name,
                                const tr64_game_inventory_carry_t *carry);
void game_tick(uint16_t input_mask);
void game_set_extra_input(uint32_t mask);
void game_set_extra_input_player(unsigned slot,uint32_t mask);
enum { TR64_GAME_PLAYERS = 2 };
/* One action mask per slot. Absent slots receive no input. */
void game_tick_players(const uint16_t input_mask[TR64_GAME_PLAYERS]);
uint32_t game_player_mask(void);       /* present, including dead */
uint32_t game_player_alive_mask(void);
/* Explicit between-tick commands: zero on success, nonzero on refusal. */
int game_player_join(void);
enum { TR64_GAME_LEAVE_OK=0, TR64_GAME_LEAVE_REFUSED=-1, TR64_GAME_LEAVE_BUSY=1 };
int game_player_leave(void);
enum { TR64_GAME_WARP_OK, TR64_GAME_WARP_REFUSED, TR64_GAME_WARP_NO_MEDIPACK, TR64_GAME_WARP_BUSY };
/* Reviving costs one large medipack from shared stock. Living warps are free. */
int game_player_warp(unsigned slot);
enum {
 TR64_GAME_STOP_NONE, TR64_GAME_STOP_DEATH, TR64_GAME_STOP_LEVEL_END,
 TR64_GAME_STOP_NOT_PORTED
};
typedef struct {
 uint32_t kind;
 /* Source campaign id, valid for LEVEL_END only (a cutscene id included). */
 int32_t next_level_id;
 /* Source death page has finished opening; engine may choose an automatic
  * restart. This does not latch DEATH without the source's input condition. */
 uint32_t death_ready;
 uint32_t transition; /* TR64_GAME_TRANSITION_* for LEVEL_END */
 /* Resident copy: no unit-code or unit-rodata pointer survives a stop. */
 char name[96];
} tr64_game_stop_t;
/* Query after every tick, before trace/hash/frame. NULL means no loaded game.
 * A latched stop is stable until release; further ticks do not advance it. */
const tr64_game_stop_t *game_stop_info(void);
/* Nonzero while the loaded level is one of the four in-engine cutscene
 * levels (no Lara, no input, ends with its cinematic frame table). */
int game_cutscene_active(void);
/* Request the end of the running cutscene through the pending-level request
 * the cinematic makes itself at its last frame; the next game_tick latches
 * LEVEL_END with the cutscene's own next level id. Zero (nothing done) when
 * no cutscene runs or the game already stopped. */
int game_cutscene_skip(void);
/* Between frames, outside loading/frame/dispatch scopes. Owns a balanced
 * loading scope and returns CODE_IDLE; clears gs and unloads game units before
 * the engine resets renderer/resources/pools. Does not reset pools itself. */
void game_level_stop(void);
/* Placement consumes no tick. It validates the same room/grid/floor predicates
 * as the oracle, resets Lara, derives water state, then initializes the camera. */
int game_place(uint32_t room,int32_t x,int32_t y,int32_t z,int16_t angle);
int game_place_player(unsigned slot,uint32_t room,int32_t x,int32_t y,int32_t z,int16_t angle);
/* Schema 2 numeric records, including canonical FNV-1a hash. The callback may
 * stream small fragments; no full-record scratch buffer or host I/O in game. */
void game_trace(tr64_game_writer_t write,void *context);
/* Up to capacity bytes of the canonical schema 2 record, without a terminator.
 * Stops traversing once the prefix is filled; returns the bytes written. */
uint32_t game_trace_prefix(char *out,uint32_t capacity);
/* Schema 3 adds inventory/settings/level; 4 adds the live-world summary;
 * 5 exposes both slots and owners. See test/oracle/README.md. */
void game_trace_schema(uint32_t schema,tr64_game_writer_t write,void *context);
uint32_t game_state_hash(void);
/* Pure canonical hash for schemas 2–5; existing game_state_hash remains 2. */
uint32_t game_state_hash_schema(uint32_t schema);
/* Explicit source-V3 persistent state, with source reconstruction on load.
 * Returns required bytes; NULL/zero queries size and insufficient capacity
 * writes nothing. Successful WRITE commits the source save header. The caller
 * buffer must not overlap live game state. */
uint32_t game_save(void *buffer,uint32_t capacity);
/* True while Lara 2 is committed or a dead Lara still owes work. */
int game_save_blocked(void);
/* Current validated Core must match the save. Invalid/truncated data returns
 * zero without changing game state; a successful load returns one. */
int game_load(const void *buffer,uint32_t bytes);
/* Selected sound commands; mixing belongs to the engine's later audio stage.
 * The view lasts until the next tick. stop distinguishes sample-stop events. */
typedef struct {
 int16_t sound,sample,volume,pitch;
 uint8_t mode,stop,positioned;
 int32_t position[3];
} tr64_game_sound_event_t;
const tr64_game_sound_event_t *game_sound_events(uint32_t *count);
typedef struct {
 int32_t health,health_max,air,air_max;
 uint8_t show_health,show_air;
#if TR64_GAME == 3
 int32_t sprint,sprint_max;
 uint8_t show_sprint;
#endif
} tr64_game_hud_view_t;
/* Source drawHUD values/visibility, including the no-inventory world gate.
 * No invented flashing or always-on bars. Returns zero without writing out
 * when no game/Lara is loaded or out is NULL; never allocates or mutates. */
int game_hud_view(tr64_game_hud_view_t *out);
int game_hud_view_player(unsigned slot,tr64_game_hud_view_t *out);
typedef struct {
 uint32_t tick;
 int32_t type,position[3];
 uint8_t player;
} tr64_game_pickup_event_t;
/* Actual successful collection events, not inventory-count differences.
 * Event index/order and copied origin/type are valid until the next game_tick
 * or level release. Queries are pure/resident; invalid index/NULL writes none.
 * Notification timing/layout belongs to UI (OpenLara src/ui.h, pinned2333adef). */
uint32_t game_pickup_event_count(void);
int game_pickup_event(uint32_t index,tr64_game_pickup_event_t *out);
enum { TR64_GAME_MENU_ITEMS=26, TR64_GAME_MENU_OPTIONS=32 };
enum {
 TR64_GAME_MENU_STATE_NONE=0, TR64_GAME_MENU_STATE_OPENING=1,
 TR64_GAME_MENU_STATE_CLOSING=2, TR64_GAME_MENU_STATE_CLOSE=3,
 TR64_GAME_MENU_STATE_READY=4, TR64_GAME_MENU_STATE_DEATH=5,
 TR64_GAME_MENU_STATE_SPIN=6, TR64_GAME_MENU_STATE_SELECT=7,
 TR64_GAME_MENU_STATE_DESELECT=8, TR64_GAME_MENU_STATE_SHOW=9,
 TR64_GAME_MENU_STATE_PAGE_MAIN=10, TR64_GAME_MENU_STATE_PAGE_KEYS=11,
 TR64_GAME_MENU_STATE_PAGE_OPTIONS=12
};
enum {
 TR64_GAME_MENU_PAGE_TITLE=0, TR64_GAME_MENU_PAGE_DEATH=1,
 TR64_GAME_MENU_PAGE_END=2, TR64_GAME_MENU_PAGE_STATS=3,
 TR64_GAME_MENU_PAGE_USE=4, TR64_GAME_MENU_PAGE_MAIN=5,
 TR64_GAME_MENU_PAGE_KEYS=6, TR64_GAME_MENU_PAGE_OPTIONS=7
};
enum {
 TR64_GAME_MENU_OPTION_SPACE=0, TR64_GAME_MENU_OPTION_BUTTON=1,
 TR64_GAME_MENU_OPTION_BAR=2, TR64_GAME_MENU_OPTION_SWITCH=3,
 TR64_GAME_MENU_OPTION_VALUE=4, TR64_GAME_MENU_OPTION_TEXT=5
};
enum {
 TR64_GAME_MENU_ACTION_NONE,TR64_GAME_MENU_ACTION_SAVE_SLOT,
 TR64_GAME_MENU_ACTION_LOAD_SLOT
};
typedef struct {
 int32_t slot,type,animIndex,frameIndex;
 uint32_t visibleMask;
} tr64_game_menu_item_t;
typedef struct { int32_t type,str,value; } tr64_game_menu_option_t;
typedef struct {
 uint32_t time,distance;
 uint16_t mediUsed,ammoUsed,kills;
 uint8_t pickups,secrets;
 uint32_t titleID,secretsMax;
#if TR64_HAS_TR2
 uint32_t shots,hits; /* Edition II's full-width counters. */
#endif
} tr64_game_level_stats_t;
/* Current source statistics, pure resident query; unloaded/NULL writes none. */
int game_stats_view(tr64_game_level_stats_t *out);
/* Source scalar presentation state, never pointers into a code unit.
 * The snapshot is caller-owned; ring models are pulled individually into the
 * same bounded drawable buffer used for world items. No formatted text. */
typedef struct {
 int32_t state,page;
 /* Intent of a new plain ACTION edge; do not combine picker interception
  * with directional/page navigation in the same input. */
 int32_t actionHint;
 uint32_t backgroundKind;
 int32_t titleID,itemID,height,radius,pitch,rot,rotItem,selDist,passportPage;
 int32_t selRotPre,selRotX,selRotY,itemsCount,itemIndex,frameIndex,frameTarget;
 uint32_t itemVisMask;
 tr64_game_menu_item_t items[TR64_GAME_MENU_ITEMS];
 tr64_game_menu_option_t options[TR64_GAME_MENU_OPTIONS];
 int32_t optionIndex,optionsCount,optionsWidth,optionsHeight;
 uint8_t showTitle,showItem,showUp,showDown,showLeft,showRight,showCount,showHealth;
 int16_t countValue;
 uint8_t countSuffix;
 int32_t healthQ8;
 tr64_game_level_stats_t stats;
 int32_t camera_basis[9],camera_eye[3];
} tr64_game_menu_view_t;
/* Zero, with no writes or unit dispatch, when inactive/unloaded/out is NULL.
 * Active snapshots are synchronous and caller-owned. */
int game_menu_view(tr64_game_menu_view_t *out);
/* The active ring's input owner; zero for the title, an inactive ring or no game. */
unsigned game_menu_player(void);
enum {
 TR64_GAME_TEXT_STRING=0,TR64_GAME_TEXT_GLYPH=1,
 TR64_GAME_TEXT_NUMBER=2,TR64_GAME_TEXT_STATS_BLOCK=3,
 TR64_GAME_TEXT_LEFT=0,TR64_GAME_TEXT_CENTER=1,TR64_GAME_TEXT_RIGHT=2
};
typedef struct {
 int32_t value;
 int16_t x,y;
 uint8_t kind,alignment,suffix,line_height;
} tr64_game_menu_text_t;
/* Required count; insufficient capacity writes nothing. Widths are immutable
 * StringID-indexed source font widths, needed only for passport arrows. Stats
 * block formatting uses snapshot.stats; number suffixes are raw glyph IDs.
 * Resident and pure: safe to call from the presentation code unit. */
uint32_t game_menu_text_view(const tr64_game_menu_view_t *snapshot,
 const uint16_t *widths,uint32_t width_count,tr64_game_menu_text_t *buffer,
 uint32_t capacity);
/* Fixed source's N64 ammo HUD policy, raw numeric glyphs at source coordinates.
 * Returns required count (0 or 1), with no insufficient-capacity writes. */
uint32_t game_hud_text_view(tr64_game_menu_text_t *buffer,uint32_t capacity);
uint32_t game_hud_text_view_player(unsigned slot,tr64_game_menu_text_t *buffer,uint32_t capacity);
enum { TR64_GAME_LARA_BONES=15, TR64_GAME_DRAW_MESH=0, TR64_GAME_DRAW_SPRITE=1,
       TR64_GAME_DRAW_CHUNK=64 };
enum { TR64_GAME_DRAW_BLEND_NONE=0, TR64_GAME_DRAW_ADDITIVE=4, TR64_GAME_DRAW_TRANSLUCENT=8 };
#define TR64_GAME_VIEW_END UINT32_MAX
typedef struct {
 /* Core mesh-pointer slot (aliases preserved), or TX64 sprite descriptor. */
 uint32_t mesh_slot,instance_id,room;
 /* Local-to-world row-major Q14 rotation and integer world translation.
  * Translation is rounded separately from basis composition: within one
  * world unit of OpenLara's combined fixed-point shift. */
 int32_t basis[9],translation[3];
 /* Signed native brightness offset before fog. Absolute source dynamic
  * darkness and source static intensity are each normalized by -4096. */
 int32_t light_offset;
 uint8_t water,kind,bone;
 uint8_t blend;
} tr64_game_drawable_t;
/* Source ring item pose, pulled in reverse list order by the caller. One whole
 * item per query; the caller may reuse its world drawable buffer. The snapshot
 * must be from this tick. Mesh identity namespace is distinct from world items;
 * room is UINT32_MAX and water is zero. END/STATS pages intentionally draw none. */
uint32_t game_menu_item_view(const tr64_game_menu_view_t *snapshot,uint32_t list_index,
                            tr64_game_drawable_t *buffer,uint32_t capacity);
/* Non-fixed src/ui.h pickup presentation: inventory-converted first model
 * frame, -15 degree X tilt and caller's fixed-angle Y rotation. Local origin;
 * caller supplies screen placement/fade and assigns its own instance identity.
 * No model, including a source-omitted icon, returns zero. No unit dispatch. */
uint32_t game_pickup_item_view(int32_t type,int16_t angle_y,
                             tr64_game_drawable_t *buffer,uint32_t capacity);
typedef struct {
 int32_t camera_basis[9],camera_eye[3],camera_target[3];
 uint32_t camera_room,flipped,texture_animation_step;
} tr64_game_view_t;
/* No game tick, allocation, RNG draw or source-table write. Animation root
 * positions and matrices use the fixed game's interpolation/rounding rules. */
/* Camera/clock snapshot only; no world-wide drawable traversal or bone work. */
void game_view_camera(tr64_game_view_t *out);
/* Slot 0 is the legacy view, including title and cutscene cameras.
 * An invalid or absent secondary slot does not write out. */
void game_view_camera_player(unsigned slot,tr64_game_view_t *out);
/* Pure snapshot of active dynamic lights (weapon muzzle flash; TR2 held/loose
 * flares) for room geometry illumination. Prioritizes loose flares closest to
 * eye when more than max_lights are active. Zero simulation or RNG mutation. */
uint32_t game_view_lights(const int32_t eye[3],tr64_gpu_light_t *out,uint32_t max_lights);
typedef struct {
 int32_t x,y,z;
 uint16_t rx,rz;
 int16_t yaw;
 uint16_t room;
 uint8_t alpha,pad;
} tr64_game_shadow_t;
/* Pure snapshot of ground shadows for Lara(s), active creatures, and TR2 boats. */
uint32_t game_view_shadows(tr64_game_shadow_t *out,uint32_t capacity);
/* Pull linked items in one logical room. Start cursor at zero; END is final.
 * Returns written <= capacity, preserving whole contiguous item groups.
 * If the first item needs more than capacity, returns its required count and
 * writes neither cursor nor buffer. Positive capacity requires a buffer.
 * Consume each chunk synchronously before reusing the caller-owned buffer. */
uint32_t game_view_room(uint32_t room,uint32_t *cursor,tr64_game_drawable_t *buffer,
                        uint32_t capacity);
/* Core item, first model frame, no AI/controller mutation. Same count contract.
 * Non-model items fail by name; intentionally undrawn model types return zero. */
uint32_t game_rest_item_view(uint32_t coreItemIndex,tr64_game_drawable_t *buffer,uint32_t capacity);
#endif
