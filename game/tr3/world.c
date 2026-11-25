/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III world lifecycle, declarative India campaign metadata, math helpers,
 * audio/music hooks, and tr64/game.h exports from docs/specs/wall/tr3-jungle-and-movement.md §4. */
#include <stddef.h>
#include <string.h>
#include "game/tr3/tr3.h"
#include "tr64/code.h"
#include "tr64/mem.h"
#include "tr64/res.h"
#include "engine/core/sys.h"

tr3_state_t *tr3_gs;

/* Declarative India campaign script table (tr3-jungle-and-movement.md §4.2).
 * Only campaign metadata lives here; all gameplay mechanics are data-driven. */
static const tr3_script_level_t india_script[] = {
    { 0, "house",    "Lara's House",    2,  0, 0, 0, 0 },
    { 1, "jungle",   "Jungle",          34, 1, 0, 6, 0 },
    { 2, "temple",   "Temple Ruins",    34, 0, 0, 4, 0 },
    { 3, "quadchas", "The River Ganges",34, 1, 0, 5, 0 },
    { 4, "tonyboss", "Caves Of Kaliya", 30, 0, 0, 0, TR3_OBJ_INFADA_STONE }
};

enum { INDIA_SCRIPT_COUNT = (int)(sizeof(india_script) / sizeof(india_script[0])) };

static int ascii_icmp(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return (int)ca - (int)cb;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

const tr3_script_level_t *tr3_script_by_name(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < INDIA_SCRIPT_COUNT; ++i) {
        if (!ascii_icmp(name, india_script[i].name))
            return &india_script[i];
    }
    return NULL;
}

const tr3_script_level_t *tr3_script_by_id(int32_t id)
{
    for (int i = 0; i < INDIA_SCRIPT_COUNT; ++i) {
        if (india_script[i].id == id)
            return &india_script[i];
    }
    return NULL;
}

/* Quarter-wave Q14 sine lookup (65536 = 360 deg). */
int32_t tr3_sin(int32_t angle)
{
    static const int16_t qsin[33] = {
        0, 804, 1606, 2404, 3196, 3981, 4756, 5520,
        6270, 7005, 7723, 8423, 9102, 9760, 10394, 11003,
        11585, 12140, 12665, 13160, 13623, 14053, 14449, 14811,
        15137, 15426, 15679, 15893, 16069, 16207, 16305, 16364, 16384
    };
    uint32_t u = (uint32_t)angle & 65535u;
    uint32_t q = u >> 14;
    uint32_t p = u & 16383u;
    if (q & 1u) p = 16384u - p;
    uint32_t idx = p >> 9;
    uint32_t frac = p & 511u;
    int32_t v = (idx >= 32u) ? 16384
        : (int32_t)qsin[idx] + (((int32_t)(qsin[idx + 1] - qsin[idx]) * (int32_t)frac) >> 9);
    return (q >= 2u) ? -v : v;
}

int32_t tr3_cos(int32_t angle)
{
    return tr3_sin(angle + 16384);
}

uint32_t tr3_isqrt(uint32_t v)
{
    uint32_t res = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* Octant integer atan2 returning TR3 angle (0 = +Z, +16384 = +X). */
int16_t tr3_atan2(int32_t dx, int32_t dz)
{
    if (dx == 0 && dz == 0)
        return 0;
    int32_t ax = dx < 0 ? -dx : dx;
    int32_t az = dz < 0 ? -dz : dz;
    int32_t ratio = (ax < az) ? (int32_t)(((int64_t)ax << 10) / (az ? az : 1))
                              : (int32_t)(((int64_t)az << 10) / (ax ? ax : 1));
    /* First-octant approximation: 45 deg = 8192 units, cubic correction for < 0.3 deg accuracy. */
    int32_t ang = (ratio * 8192) >> 10;
    int32_t corr = (((1024 - ratio) * ratio) >> 10) * 1024 >> 10;
    ang += corr;
    if (ax >= az)
        ang = 16384 - ang;
    if (dz < 0)
        ang = 32768 - ang;
    if (dx < 0)
        ang = -ang;
    return (int16_t)ang;
}

uint32_t tr3_rand(tr3_state_t *st)
{
    st->rng = st->rng * 1103515245u + 12345u;
    return (st->rng >> 16) & 0x7FFFu;
}

const tr64_core_models_t *tr3_find_model(const tr64_level_t *level, uint32_t type)
{
    if (!level)
        return NULL;
    const uint32_t count = tr64_level_table(level, TR64_CORE_models)->count;
    for (uint32_t i = 0; i < count; ++i) {
        const tr64_core_models_t *m = tr64_level_models(level, i);
        if (tr64_u32(m->type) == type)
            return m;
    }
    return NULL;
}

void tr3_emit_sound(tr3_state_t *st, int32_t sound_id, const int32_t *pos)
{
    if (!st || !st->level || !st->level->sound_metadata || sound_id < 0)
        return;
    if (st->sound_event_count >= TR3_MAX_SOUND_EVENTS)
        return;
    const tr64_level_t *l = st->level;
    uint32_t sm_count = tr64_level_table(l, TR64_CORE_sound_map)->count;
    if ((uint32_t)sound_id >= sm_count)
        return;
    int16_t detail_idx = tr64_i16(*tr64_level_sound_map(l, (uint32_t)sound_id));
    if (detail_idx < 0 || (uint32_t)detail_idx >= tr64_level_table(l, TR64_CORE_sound_details)->count)
        return;
    const tr64_core_sound_details_t *d = tr64_level_sound_details(l, (uint32_t)detail_idx);
    uint32_t sample = tr64_u16(d->sample);
    uint32_t bank_samples = tr64_be32(l->sound_metadata + 16u);
    if (sample >= bank_samples)
        return;

    tr64_game_sound_event_t *ev = &st->sound_events[st->sound_event_count++];
    memset(ev, 0, sizeof(*ev));
    ev->sound = (int16_t)sound_id;
    ev->sample = (int16_t)sample;
    uint32_t vol = (tr64_u16(d->volume) * 64u) / 32767u;
    if (vol < 1u) vol = 1u;
    if (vol > 64u) vol = 64u;
    ev->volume = (int16_t)vol;
    ev->pitch = 128;
    ev->mode = (uint8_t)(tr64_u16(d->flags) & 3u);
    if (pos) {
        ev->positioned = 1;
        ev->position[0] = pos[0];
        ev->position[1] = pos[1];
        ev->position[2] = pos[2];
    }
}

void tr3_request_music(tr3_state_t *st, int32_t track)
{
    if (!st)
        return;
    st->music.track = track;
    ++st->music.serial;
    st->music.reason = TR64_GAME_MUSIC_DIRECT;
}

void tr3_record_pickup(tr3_state_t *st, int32_t type, int32_t x, int32_t y, int32_t z)
{
    if (!st || st->pickup_event_count >= TR3_MAX_PICKUPS)
        return;
    tr64_game_pickup_event_t *p = &st->pickup_events[st->pickup_event_count++];
    p->tick = st->ticks;
    p->type = type;
    p->position[0] = x;
    p->position[1] = y;
    p->position[2] = z;
    p->player = 0;
}

uint32_t tr3_entities_call(tr3_state_t *st, uint32_t op, uint32_t input,
                           int32_t target_idx, int32_t damage)
{
    if (!st || !st->entities_loaded)
        return 0;
    tr3_entity_call_t call = {
        .op = op, .st = st, .input = input, .target_idx = target_idx, .damage = damage
    };
    return code_call("tr3_entities", (uint32_t)(uintptr_t)&call);
}

static tr64_game_settings_t persistent_settings = {
    .video_gamma = 16, .video_fog = 8, .video_fps = 0, .video_vsync = 1,
    .audio_sfx = 1, .audio_music = 1, .controls_vibration = 0, .controls_swap = 0,
    .video_filter = 0, .video_auto_dist = 0, .controls_abilities = TR64_LARA_ABILITIES_TR3
};

void tr3_settings_remember(const tr64_game_settings_t *settings)
{
    if (settings) persistent_settings = *settings;
}

void game_init(void)
{
    /* State is allocated inside TR64_MEM_state on game_level_start. */
}

void game_level_start(const struct tr64_level *core, const char *level_name)
{
    if (!core || !core->ready || !level_name)
        sys_error("tr3: level start requires ready core");

    tr3_state_t *st = tr64_mem_alloc(TR64_MEM_state, (uint32_t)sizeof(tr3_state_t));
    memset(st, 0, sizeof(*st));
    tr3_gs = st;
    st->level = core;
    st->script = tr3_script_by_name(level_name);
    size_t n = strlen(level_name);
    if (n >= sizeof(st->level_name)) n = sizeof(st->level_name) - 1u;
    memcpy(st->level_name, level_name, n);
    st->rng = 0x54523331u;
    st->settings = persistent_settings;
    st->camera.fixed_cam = -1;
    st->camera.look_item = -1;

    if (st->script) {
        st->music.ambient = st->script->ambient_track;
        st->music.track = st->script->ambient_track;
        st->stats.secretsMax = st->script->secrets_total;
        st->stats.titleID = (uint32_t)(TR64_STR_TR3_HOUSE + st->script->id);
    } else {
        st->music.ambient = 34;
        st->music.track = 34;
        st->stats.titleID = (uint32_t)TR64_STR_TR3_GENERIC;
    }
    st->music.serial = 1;
    st->music.reason = TR64_GAME_MUSIC_RESTORE;

    /* Load paged entities code unit inside a balanced loading scope. */
    code_loading_begin();
    if (!code_is_loaded("tr3_entities"))
        code_load("tr3_entities");
    code_loading_end();
    st->entities_loaded = 1;

    tr3_lara_init(st);
    tr3_entities_call(st, TR3_ENT_INIT, 0, -1, 0);
    tr3_camera_update(st, 0);
}

void game_level_start_transition(const struct tr64_level *core, const char *level_name,
                                 const tr64_game_inventory_carry_t *carry)
{
    game_level_start(core, level_name);
    if (!carry || !tr3_gs)
        return;
    if (carry->transition == TR64_GAME_TRANSITION_CAMPAIGN) {
        /* Carry forward owned weapons, ammo, medipacks, flares, crystals, artifacts (§4.3).
         * Health resets to 1000, poison resets to 0, level-local keys reset to 0. */
        tr3_lara_t *lara = &tr3_gs->lara;
        lara->owned_weapons = (uint16_t)(carry->counts[0] | (1u << TR3_WP_PISTOLS));
        for (int w = 1; w < TR3_WP_COUNT && w < 10; ++w)
            lara->ammo[w] = carry->counts[w];
        lara->small_medi = (int16_t)carry->counts[10];
        lara->large_medi = (int16_t)carry->counts[11];
        lara->flares     = (int16_t)carry->counts[12];
        lara->crystals   = (int16_t)carry->counts[13];
        lara->artifacts  = (uint8_t)carry->counts[14];
        lara->hp = 1000;
        lara->poison = 0;
        memset(lara->keys, 0, sizeof(lara->keys));
        tr3_lara_sync_from_tr3(tr3_gs);
    }
}

void game_level_stop(void)
{
    if (tr3_gs && tr3_gs->entities_loaded) {
        code_loading_begin();
        if (code_is_loaded("tr3_entities"))
            code_unload("tr3_entities");
        code_loading_end();
    }
    tr3_gs = NULL;
}

void game_tr3_tick(uint32_t input_mask)
{
    if (!tr3_gs)
        return;
    tr3_state_t *st = tr3_gs;
    if (st->stop.kind != TR64_GAME_STOP_NONE)
        return;

    st->sound_event_count = 0;
    st->pickup_event_count = 0;
    ++st->ticks;
    st->stats.time = st->ticks;

    tr3_lara_tick(st, input_mask);
    tr3_entities_call(st, TR3_ENT_TICK, input_mask, -1, 0);
    tr3_camera_update(st, input_mask);

    int32_t dx = st->lara.x - st->lara.prev_x;
    int32_t dz = st->lara.z - st->lara.prev_z;
    int64_t d2 = (int64_t)dx * (int64_t)dx + (int64_t)dz * (int64_t)dz;
    st->stats.distance += (d2 > (int64_t)UINT32_MAX) ? 65535u : tr3_isqrt((uint32_t)d2);
    st->last_input = input_mask;
}

void game_tick(uint16_t input_mask)
{
    game_tr3_tick((uint32_t)input_mask);
}

void game_tick_players(const uint16_t input_mask[TR64_GAME_PLAYERS])
{
    game_tr3_tick(input_mask ? (uint32_t)input_mask[0] : 0u);
}

int game_place(uint32_t room, int32_t x, int32_t y, int32_t z, int16_t angle)
{
    if (!tr3_gs || !tr3_gs->level)
        return 0;
    if (room >= tr64_level_table(tr3_gs->level, TR64_CORE_rooms)->count)
        return 0;
    tr3_lara_t *lara = &tr3_gs->lara;
    lara->room = (uint16_t)room;
    lara->x = lara->prev_x = x;
    lara->y = lara->prev_y = y;
    lara->z = lara->prev_z = z;
    lara->rot_y = lara->move_angle = angle;
    lara->speed = 0;
    lara->fall_speed = 0;
    lara->gravity = 0;
    lara->braid_init = 0;
    tr3_lara_sync_from_tr3(tr3_gs);
    tr3_gs->camera.initialized = 0;
    tr3_camera_update(tr3_gs, 0);
    return 1;
}

int game_place_player(unsigned slot, uint32_t room, int32_t x, int32_t y, int32_t z, int16_t angle)
{
    return slot == 0 ? game_place(room, x, y, z, angle) : 0;
}

const tr64_game_stop_t *game_stop_info(void)
{
    return tr3_gs ? &tr3_gs->stop : NULL;
}

int game_cutscene_active(void) { return 0; }
int game_cutscene_skip(void)   { return 0; }

int32_t game_level_id(void)
{
    if (!tr3_gs) return -1;
    return tr3_gs->script ? tr3_gs->script->id : 1;
}

const char *game_level_name(int32_t level_id)
{
    const tr3_script_level_t *s = tr3_script_by_id(level_id);
    return s ? s->name : NULL;
}

uint32_t game_player_mask(void)       { return tr3_gs ? 1u : 0u; }
uint32_t game_player_alive_mask(void) { return (tr3_gs && tr3_gs->lara.hp > 0) ? 1u : 0u; }
int game_player_join(void)            { return -1; }
int game_player_leave(void)           { return TR64_GAME_LEAVE_REFUSED; }
int game_player_warp(unsigned slot)   { (void)slot; return TR64_GAME_WARP_REFUSED; }

const tr64_game_settings_t *game_settings_view(void)
{
    return tr3_gs ? &tr3_gs->settings : &persistent_settings;
}

void game_set_extra_input(uint32_t mask) { (void)mask; }
void game_set_extra_input_player(unsigned slot, uint32_t mask) { (void)slot; (void)mask; }

int game_settings_apply(const tr64_game_settings_t *settings)
{
    if (!tr3_gs || !settings) return 0;
    tr3_gs->settings = *settings;
    persistent_settings = *settings;
    return 1;
}

void game_music_request(tr64_game_music_request_t *out)
{
    if (!out) return;
    if (!tr3_gs) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = tr3_gs->music;
}

const tr64_game_sound_event_t *game_sound_events(uint32_t *count)
{
    if (!tr3_gs) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = tr3_gs->sound_event_count;
    return tr3_gs->sound_events;
}

int game_stats_view(tr64_game_level_stats_t *out)
{
    if (!tr3_gs || !out) return 0;
    *out = tr3_gs->stats;
    out->secrets = (uint8_t)tr3_gs->secrets_mask;
    return 1;
}

int game_inventory_capture(tr64_game_inventory_carry_t *out)
{
    if (!tr3_gs || !out) return 0;
    memset(out, 0, sizeof(*out));
    out->from_level_id = game_level_id();
    out->tick = tr3_gs->ticks;
    out->counts[0] = (int32_t)tr3_gs->lara.owned_weapons;
    for (int w = 1; w < TR3_WP_COUNT && w < 10; ++w)
        out->counts[w] = tr3_gs->lara.ammo[w];
    out->counts[10] = tr3_gs->lara.small_medi;
    out->counts[11] = tr3_gs->lara.large_medi;
    out->counts[12] = tr3_gs->lara.flares;
    out->counts[13] = tr3_gs->lara.crystals;
    out->counts[14] = tr3_gs->lara.artifacts;
    memcpy(out->camera_target, tr3_gs->camera.target, sizeof(out->camera_target));
    return 1;
}

/* Compact TR3 save serialization (fits well within SRAM 8 KiB slot). */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t crc;
    int32_t level_id;
    uint32_t ticks;
    uint32_t secrets_mask;
    uint8_t secrets_found;
    uint8_t flipped;
    uint8_t ally_hostile;
    uint8_t pad;
    tr3_lara_t lara;
    tr64_game_level_stats_t stats;
} tr3_save_blob_t;

static uint32_t tr3_save_crc(const tr3_save_blob_t *blob)
{
    const uint8_t *p = (const uint8_t *)&blob->level_id;
    size_t len = sizeof(*blob) - offsetof(tr3_save_blob_t, level_id);
    return tr64_crc32(0, p, len);
}

uint32_t game_save(void *buffer, uint32_t capacity)
{
    if (!tr3_gs) return 0;
    const uint32_t need = (uint32_t)sizeof(tr3_save_blob_t);
    if (!buffer || capacity < need) return need;
    tr3_save_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = 0x54523353u;
    blob.version = 1u;
    blob.level_id = game_level_id();
    blob.ticks = tr3_gs->ticks;
    blob.secrets_mask = tr3_gs->secrets_mask;
    blob.secrets_found = tr3_gs->secrets_found;
    blob.flipped = tr3_gs->flipped;
    blob.ally_hostile = tr3_gs->ally_hostile;
    blob.lara = tr3_gs->lara;
    blob.stats = tr3_gs->stats;
    blob.crc = tr3_save_crc(&blob);
    memcpy(buffer, &blob, need);
    return need;
}

int game_save_blocked(void)
{
    return (!tr3_gs || tr3_gs->lara.hp <= 0) ? 1 : 0;
}

int game_load(const void *buffer, uint32_t bytes)
{
    if (!tr3_gs || !tr3_gs->level || !buffer || bytes != sizeof(tr3_save_blob_t))
        return 0;
    tr3_save_blob_t blob;
    memcpy(&blob, buffer, sizeof(blob));
    if (blob.magic != 0x54523353u || blob.version != 1u ||
        blob.level_id != game_level_id() || blob.crc != tr3_save_crc(&blob))
        return 0;
    const tr64_level_t *l = tr3_gs->level;
    uint32_t rcount = tr64_level_table(l, TR64_CORE_rooms)->count;
    uint32_t acount = tr64_level_table(l, TR64_CORE_animations)->count;
    uint32_t ccount = tr64_level_table(l, TR64_CORE_cameras)->count;
    if (blob.flipped > 1u || blob.ally_hostile > 1u ||
        blob.lara.room >= rcount || blob.lara.anim >= acount ||
        blob.lara.weapon >= TR3_WP_COUNT ||
        blob.lara.target_item < -1 || blob.lara.target_item >= (int32_t)tr3_gs->item_count ||
        blob.lara.zipline_item < -1 || blob.lara.zipline_item >= (int32_t)tr3_gs->item_count ||
        blob.lara.current_sink < 0 || (uint32_t)blob.lara.current_sink > ccount ||
        blob.lara.hp < -1 || blob.lara.hp > 1000 ||
        blob.lara.air < -1 || blob.lara.air > 1800 ||
        blob.lara.stamina < 0 || blob.lara.stamina > 120 ||
        blob.lara.warmth < -1 || blob.lara.warmth > 600 ||
        blob.lara.poison < 0 || blob.lara.poison > 256)
        return 0;
    /* Transactional commit after all validation passes. */
    tr3_gs->ticks = blob.ticks;
    tr3_gs->secrets_mask = blob.secrets_mask;
    tr3_gs->secrets_found = blob.secrets_found;
    tr3_gs->flipped = blob.flipped;
    tr3_gs->ally_hostile = blob.ally_hostile;
    tr3_gs->lara = blob.lara;
    tr3_gs->stats = blob.stats;
    tr3_lara_sync_from_tr3(tr3_gs);
    tr3_gs->camera.initialized = 0;
    tr3_camera_update(tr3_gs, 0);
    return 1;
}

static uint32_t fnv32(uint32_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t game_state_hash(void)
{
    if (!tr3_gs) return 0;
    uint32_t h = 2166136261u;
    h = fnv32(h, &tr3_gs->ticks, sizeof(tr3_gs->ticks));
    h = fnv32(h, &tr3_gs->lara.x, sizeof(tr3_gs->lara.x));
    h = fnv32(h, &tr3_gs->lara.y, sizeof(tr3_gs->lara.y));
    h = fnv32(h, &tr3_gs->lara.z, sizeof(tr3_gs->lara.z));
    h = fnv32(h, &tr3_gs->lara.room, sizeof(tr3_gs->lara.room));
    h = fnv32(h, &tr3_gs->lara.rot_y, sizeof(tr3_gs->lara.rot_y));
    h = fnv32(h, &tr3_gs->lara.state, sizeof(tr3_gs->lara.state));
    h = fnv32(h, &tr3_gs->lara.hp, sizeof(tr3_gs->lara.hp));
    h = fnv32(h, &tr3_gs->lara.stamina, sizeof(tr3_gs->lara.stamina));
    h = fnv32(h, &tr3_gs->lara.poison, sizeof(tr3_gs->lara.poison));
    h = fnv32(h, &tr3_gs->secrets_mask, sizeof(tr3_gs->secrets_mask));
    return h;
}

uint32_t game_state_hash_schema(uint32_t schema)
{
    (void)schema;
    return game_state_hash();
}

void game_trace(tr64_game_writer_t write, void *context)
{
    (void)write;
    (void)context;
}

void game_trace_schema(uint32_t schema, tr64_game_writer_t write, void *context)
{
    (void)schema; (void)write; (void)context;
}

uint32_t game_trace_prefix(char *out, uint32_t capacity)
{
    (void)out; (void)capacity;
    return 0;
}

int game_hud_view(tr64_game_hud_view_t *out)
{
    if (!tr3_gs || !out) return 0;
    const tr3_lara_t *lara = &tr3_gs->lara;
    out->health = lara->hp;
    out->health_max = 1000;
    out->air = lara->air;
    out->air_max = 1800;
    out->show_health = (lara->guns_drawn || lara->hp < 1000 || lara->poison > 0) ? 1u : 0u;
    out->show_air = (lara->water_status == TR3_WATER_UNDER || lara->air < 1800) ? 1u : 0u;
    out->sprint = lara->stamina;
    out->sprint_max = 120;
    out->show_sprint = (lara->stamina < 120 || lara->state == TR3_ST_SPRINT) ? 1u : 0u;
    return 1;
}

int game_hud_view_player(unsigned slot, tr64_game_hud_view_t *out)
{
    return slot == 0 ? game_hud_view(out) : 0;
}

uint32_t game_pickup_event_count(void)
{
    return tr3_gs ? tr3_gs->pickup_event_count : 0u;
}

int game_pickup_event(uint32_t index, tr64_game_pickup_event_t *out)
{
    if (!tr3_gs || !out || index >= tr3_gs->pickup_event_count) return 0;
    *out = tr3_gs->pickup_events[index];
    return 1;
}
