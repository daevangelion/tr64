/* SPDX-License-Identifier: MIT */
/* Directed controls for the second-game hardening review.
 * A case that must refuse passes only when sys_error names the defect.
 * A clean return of that case is the failure on the unfixed tree. */
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "game/internal.h"
#include "tr64/mem.h"

#include "tr2/creature.h"
uint32 game_tr2_boulder_update(ItemObj *item, int32 listener_x, int32 listener_z);
uint32_t code_call(const char *name, uint32_t argument) { (void)name; (void)argument; return 0; }
int16 angleLerp(int16 a, int16 b, int32 w) { (void)b; (void)w; return a; }
static GameState state;
static jmp_buf fail_jmp;
static char fail_msg[256];
static int fail_armed;
static int legacy_floor_calls;

GameState *gs;

void sys_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(fail_msg, sizeof fail_msg, fmt, ap);
    va_end(ap);
    if (fail_armed)
        longjmp(fail_jmp, 1);
    fprintf(stderr, "FAIL %s\n", fail_msg);
    exit(1);
}

static int expect(const char *needle, void (*fn)(void))
{
    fail_msg[0] = 0;
    fail_armed = 1;
    if (setjmp(fail_jmp) == 0) {
        fn();
        fprintf(stderr, "accepted\n");
        return 1;
    }
    if (!strstr(fail_msg, needle)) {
        fprintf(stderr, "wrong refusal: %s\n", fail_msg);
        return 1;
    }
    printf("refused: %s\n", fail_msg);
    return 0;
}

static void put_u16(void *dst, unsigned v)
{
    uint8_t *p = dst;
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(void *dst, unsigned v)
{
    uint8_t *p = dst;
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void game_item_call(GameItemCall *call) { (void)call; }
void *soundPlay(int16 id, const vec3i *pos) { (void)id; (void)pos; return NULL; }
Room **Room_getAdjRooms(Room *room) { (void)room; return NULL; }
bool game_tr2_creature_type(unsigned type) { (void)type; return false; }
bool game_tr2_interact(ItemObj *item, Lara *lara) { (void)item; (void)lara; return false; }
const char *game_tr2_inert_interaction(unsigned type) { (void)type; return "world item"; }
void game_tr2_validate_sphere_key(unsigned type, unsigned clip, unsigned frame, PackedFrame key)
{
    (void)type; (void)clip; (void)frame; (void)key;
}
void game_tr2_creature_strike(TR2Creature *ai, Lara *lara, int damage, uint32 mask)
{
    (void)ai; (void)lara; (void)damage; (void)mask;
}
int32 rand_logic(void) { return 0; }
bool Object_isActive(ItemObj *item) { (void)item; return true; }
void game_tr2_refuse_interaction(const ItemObj *item, const char *op)
{
    (void)item;
    sys_error("refuse %s", op);
}
void ItemObj_animProcess(ItemObj *item, bool fx) { (void)item; (void)fx; }
void ItemObj_updateRoom(ItemObj *item, int32 h) { (void)item; (void)h; }
uint32 phd_sqrt(uint32 x)
{
    uint32 r = 0;
    for (uint32 bit = 1u << 15; bit; bit >>= 1) {
        uint32 t = r + bit;
        if (t * t <= x)
            r = t;
    }
    return r;
}
void ItemObj_getItemFloorCeiling(const ItemObj *item, int32 x, int32 y, int32 z,
                                 int32 *floor, int32 *ceiling)
{
    (void)item; (void)x; (void)y; (void)z; (void)floor; (void)ceiling;
    ++legacy_floor_calls;
}
void game_tr2_item_surface(const ItemObj *item, int32 x, int32 y, int32 z,
                           int32 *floor, int32 *ceiling)
{
    (void)item; (void)x; (void)y; (void)z; (void)floor; (void)ceiling;
}
uint16 worldActionEffectiveEnd(uint16 trigger, uint16 extra)
{
    (void)extra;
    return trigger;
}

static tr64_core_portals_t portal;
const void *tr64_level_at(const tr64_level_t *level, uint32_t kind, uint32_t index)
{
    (void)level; (void)kind; (void)index;
    return &portal;
}
static tr64_level_table_t sound_table = {.count = 4};
const tr64_level_table_t *tr64_level_table(const tr64_level_t *level, uint32_t kind)
{
    (void)level; (void)kind;
    return &sound_table;
}

void tr64_mem_stats(int pool, tr64_mem_stats_t *out)
{
    (void)pool;
    memset(out, 0, sizeof *out);
    out->budget = 4096;
}

void *tr64_mem_alloc(int pool, uint32_t bytes)
{
    static uint8_t buf[4096];
    (void)pool;
    if (bytes > sizeof buf)
        sys_error("scratch");
    return buf;
}

void tr64_mem_reset(int pool) { (void)pool; }

static int rooms(void)
{
    static Room cells[161];
    static RoomInfo info;
    static ExtraInfoLara extra;
    static Lara lara;
    static tr64_level_t core;
    put_u16(&portal.room, 160);
    put_u32(&info.portal_count, 1);
    cells[0].info = &info;
    lara.health = 100;
    lara.extraL = &extra;
    lara.room = &cells[0];
    core.game = 2;
    state.core = &core;
    state.rooms = cells;
    state.tr2Rules = TR2_RULE_CREATURES;
    gs = &state;
    Lara_updateObjectsCollision(&lara);
    printf("rooms: visited room 160 without a room-bitmap fault\n");
    return 0;
}

static void capacity_over(void)
{
    game_tr2_admit_counts("wall", 256, 0);
}

static void items_over(void)
{
    game_tr2_admit_counts("wall", 1, 257);
}

static Anim clips[2];
static tr64_i16_be_t commands[4];
static ItemObj item;
static GameTR2 tr2;
static tr64_level_t core;

static void plant_clip(unsigned state_id, unsigned next, unsigned frame, unsigned commands_n)
{
    memset(clips, 0, sizeof clips);
    memset(&item, 0, sizeof item);
    memset(&tr2, 0, sizeof tr2);
    memset(&state, 0, sizeof state);
    put_u16(&clips[0].state, state_id);
    put_u16(&clips[0].frame_end, 1);
    put_u16(&clips[0].next_animation, next);
    put_u16(&clips[0].next_frame, frame);
    put_u16(&clips[0].command_count, commands_n);
    put_u16(&clips[1].frame_end, 1);
    put_u16(&clips[1].next_animation, 1);
    item.type = TR2_ITEM_HELICOPTER;
    tr2.animCount = 32;
    core.game = 2;
    state.core = &core;
    state.tr2 = &tr2;
    state.items = &item;
    state.level.itemsCount = 1;
    state.level.anims = clips;
    state.level.animCommands = commands;
    gs = &state;
}

static void bad_link(void)
{
    plant_clip(1, 1, 9, 0);
    game_tr2_validate_world_animations();
}

static void wide_state(void)
{
    plant_clip(257, 0, 0, 0);
    game_tr2_validate_world_animations();
}

static void bad_sound(void)
{
    put_u16(commands + 0, 5); /* ANIM_CMD_SOUND */
    put_u16(commands + 2, sound_table.count);
    plant_clip(1, 0, 0, 1);
    game_tr2_validate_world_animations();
}
static void bad_zip_sound(void)
{
    put_u16(commands + 0, 5); /* ANIM_CMD_SOUND */
    put_u16(commands + 2, sound_table.count);
    plant_clip(1, 0, 0, 1);
    item.type = TR2_ITEM_ZIPLINE;
    state.tr2Rules = TR2_RULE_INTERACTIONS | TR2_RULE_WALL_HAZARDS;
    game_tr2_validate_world_animations();
}

static TR2Creature ai;

static void tiger_unknown(void)
{
    memset(&item, 0, sizeof item);
    item.state = 0;
    item.goalState = 1;
    ai.item = &item;
    Interest p = {0};
    game_tr2_tiger_control(&ai, NULL, &p);
}

static void crow_unknown(void)
{
    memset(&item, 0, sizeof item);
    item.state = 0;
    item.goalState = 1;
    ai.item = &item;
    game_tr2_crow_control(&ai, NULL, NULL);
}

static void spider_unknown(void)
{
    memset(&item, 0, sizeof item);
    item.state = 0;
    item.goalState = 1;
    ai.item = &item;
    game_tr2_spider_control(&ai, NULL, NULL);
}

static int boulder(void)
{
    memset(&item, 0, sizeof item);
    memset(&state, 0, sizeof state);
    item.state = 1;
    item.roomFloor = 0;
    item.pos.y = 0;
    gs = &state;
    state.playersExtra[0].camera.bounce = 0;
    game_tr2_boulder_update(&item, 65536, 0);
    if (state.playersExtra[0].camera.bounce != -40) {
        fprintf(stderr, "boulder wrap bounce %d\n", state.playersExtra[0].camera.bounce);
        return 1;
    }
    state.playersExtra[0].camera.bounce = 7;
    game_tr2_boulder_update(&item, 10240, 0);
    if (state.playersExtra[0].camera.bounce != 7) {
        fprintf(stderr, "boulder threshold bounce %d\n", state.playersExtra[0].camera.bounce);
        return 1;
    }
    printf("boulder: 65536 wraps to bounce -40; 10240 does not shake\n");
    return 0;
}

static int harness(void)
{
    static FloorData floors[4];
    static Sector sector;
    static tr64_level_t level;
    int32 floor = 0, ceiling = 0;
    memset(&state, 0, sizeof state);
    memset(&tr2, 0, sizeof tr2);
    put_u16(&floors[1], 0x8004); /* trigger, end */
    put_u16(&floors[3], 0x8000); /* activate object, end */
    sector.floorIndex = 1;
    level.game = 2;
    state.core = &level;
    state.tr2 = &tr2;
    state.tr2Rules = 0; /* diagnostic bit clear */
    state.level.floors = floors;
    gs = &state;
    legacy_floor_calls = 0;
    Sector_getTriggerFloorCeiling(&sector, 0, 0, 0, &floor, &ceiling);
    if (tr2.dynamicFloorCalls != 1 || legacy_floor_calls != 0) {
        fprintf(stderr, "harness selected gameplay: dynamic %u legacy %d\n",
                tr2.dynamicFloorCalls, legacy_floor_calls);
        return 1;
    }
    printf("harness: game 2 floor query ignored the diagnostic bit\n");
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
#if defined(HARDEN_ROOMS)
    return rooms();
#elif defined(HARDEN_CAPACITY)
    return expect("room capacity", capacity_over) ||
           expect("item capacity", items_over);
#elif defined(HARDEN_LINKS)
    return expect("animation link outside clip", bad_link);
#elif defined(HARDEN_STATE)
    return expect("does not fit", wide_state);
#elif defined(HARDEN_SOUND)
    return expect("outside map", bad_sound);
#elif defined(HARDEN_ZIP)
    return expect("outside map", bad_zip_sound);
#elif defined(HARDEN_TIGER)
    return expect("tiger state 0 unknown", tiger_unknown);
#elif defined(HARDEN_CROW)
    return expect("crow state 0 unknown", crow_unknown);
#elif defined(HARDEN_SPIDER)
    return expect("spider state 0 unknown", spider_unknown);
#elif defined(HARDEN_BOULDER)
    return boulder();
#elif defined(HARDEN_HARNESS)
    return harness();
#else
    fprintf(stderr, "no case selected\n");
    return 2;
#endif
}
