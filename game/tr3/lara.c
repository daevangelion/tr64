/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III unified Lara bridge: binds the shared TR1-TR3 Lara/animation/
 * collision/camera core and implements TR3 movement deltas (sprint/sprint-roll,
 * crouch/crawl/crawl-climb, monkey-swing, quicksand, poison, cold, smooth water
 * currents, TR3 weapons/flares, and 6-segment wind braid) from
 * docs/specs/wall/tr3-jungle-and-movement.md §§2 & 3.8. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "game/internal.h"
#include "game/tr3/tr3.h"
#include "tr64/mem.h"
#include "engine/core/sys.h"

/* Resident shared GameState pointer owned by TR3 when game/game.c is not linked. */
GameState *gs;

static uint32_t tr3_cur_input;

static inline int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

static inline int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool is_rifle_weapon(uint8_t wp)
{
    return wp == TR3_WP_SHOTGUN || wp == TR3_WP_MP5 || wp == TR3_WP_ROCKET ||
           wp == TR3_WP_GRENADE || wp == TR3_WP_HARPOON;
}

static bool is_crouch_or_crawl(uint8_t s)
{
    return s == TR3_ST_CROUCH_IDLE || s == TR3_ST_CROUCH_ROLL ||
           s == TR3_ST_CRAWL_IDLE  || s == TR3_ST_CRAWL_FORWARD ||
           s == TR3_ST_CRAWL_BACK  || s == TR3_ST_CRAWL_TURN_L ||
           s == TR3_ST_CRAWL_TURN_R || s == TR3_ST_CLIMB_TO_CRAWL ||
           s == TR3_ST_CRAWL_TO_CLIMB;
}

/* Water-status enum mapping between shared WATER_STATE_* and TR3_WATER_*. */
static uint8_t water_shared_to_tr3(uint8_t w)
{
    switch (w) {
    case WATER_STATE_UNDER:   return TR3_WATER_UNDER;
    case WATER_STATE_SURFACE: return TR3_WATER_SURFACE;
    case WATER_STATE_WADE:    return TR3_WATER_WADE;
    default:                  return TR3_WATER_ABOVE;
    }
}

static uint8_t water_tr3_to_shared(uint8_t w)
{
    switch (w) {
    case TR3_WATER_UNDER:   return WATER_STATE_UNDER;
    case TR3_WATER_SURFACE: return WATER_STATE_SURFACE;
    case TR3_WATER_WADE:    return WATER_STATE_WADE;
    default:                return WATER_STATE_ABOVE;
    }
}

/* Resolve safe animation index for Lara's model (base + relative clip). */
static uint16_t lara_anim_index(const tr3_state_t *st, uint16_t clip_rel)
{
    const tr64_core_models_t *m = tr3_find_model(st->level, TR3_OBJ_LARA);
    uint32_t base = (m && tr64_u16(m->animation) != 65535u) ? tr64_u16(m->animation) : 0u;
    uint32_t acount = tr64_level_table(st->level, TR64_CORE_animations)->count;
    uint32_t idx = base + clip_rel;
    if (idx >= acount)
        idx = (base + 103u < acount) ? base + 103u : base;
    return (uint16_t)idx;
}

static uint16_t lara_anim_base(void)
{
    return (gs && gs->level.models) ? gs->level.models[ITEM_LARA].animIndex : 0u;
}

/* Shared core dispatch tables (states 0..70) + TR3 extension (71..88). */
static void (*const sHandlers[])(Lara *) = {
    Lara_s_STATE_WALK,        Lara_s_STATE_RUN,          Lara_s_STATE_STOP,
    Lara_s_STATE_JUMP,        Lara_s_STATE_POSE,         Lara_s_STATE_BACK_FAST,
    Lara_s_STATE_TURN_RIGHT,  Lara_s_STATE_TURN_LEFT,    Lara_s_STATE_DEATH,
    Lara_s_STATE_FALL,        Lara_s_STATE_HANG,         Lara_s_STATE_REACH,
    Lara_s_STATE_SPLAT,       Lara_s_STATE_UW_TREAD,     Lara_s_STATE_LAND,
    Lara_s_STATE_COMPRESS,    Lara_s_STATE_BACK,         Lara_s_STATE_UW_SWIM,
    Lara_s_STATE_UW_GLIDE,    Lara_s_STATE_HANG_UP,      Lara_s_STATE_TURN_FAST,
    Lara_s_STATE_STEP_RIGHT,  Lara_s_STATE_STEP_LEFT,    Lara_s_STATE_ROLL_END,
    Lara_s_STATE_SLIDE,       Lara_s_STATE_JUMP_BACK,    Lara_s_STATE_JUMP_RIGHT,
    Lara_s_STATE_JUMP_LEFT,   Lara_s_STATE_JUMP_UP,      Lara_s_STATE_FALL_BACK,
    Lara_s_STATE_HANG_LEFT,   Lara_s_STATE_HANG_RIGHT,   Lara_s_STATE_SLIDE_BACK,
    Lara_s_STATE_SURF_TREAD,  Lara_s_STATE_SURF_SWIM,    Lara_s_STATE_UW_DIVE,
    Lara_s_STATE_BLOCK_PUSH,  Lara_s_STATE_BLOCK_PULL,   Lara_s_STATE_BLOCK_READY,
    Lara_s_STATE_PICKUP,      Lara_s_STATE_SWITCH_DOWN,  Lara_s_STATE_SWITCH_UP,
    Lara_s_STATE_USE_KEY,     Lara_s_STATE_USE_PUZZLE,   Lara_s_STATE_DEATH_UW,
    Lara_s_STATE_ROLL_START,  Lara_s_STATE_SPECIAL,      Lara_s_STATE_SURF_BACK,
    Lara_s_STATE_SURF_LEFT,   Lara_s_STATE_SURF_RIGHT,   Lara_s_STATE_USE_MIDAS,
    Lara_s_STATE_DEATH_MIDAS, Lara_s_STATE_SWAN_DIVE,    Lara_s_STATE_FAST_DIVE,
    Lara_s_STATE_HANDSTAND,   Lara_s_STATE_WATER_OUT,    Lara_s_STATE_CLIMB_START,
    Lara_s_STATE_CLIMB_UP,    Lara_s_STATE_CLIMB_LEFT,   Lara_s_STATE_CLIMB_END,
    Lara_s_STATE_CLIMB_RIGHT, Lara_s_STATE_CLIMB_DOWN,   Lara_s_STATE_UNUSED_1,
    Lara_s_STATE_UNUSED_2,    Lara_s_STATE_UNUSED_3,     Lara_s_STATE_WADE,
    Lara_s_STATE_ROLL_UW,     Lara_s_STATE_PICKUP_FLARE, Lara_s_STATE_ROLL_AIR,
    Lara_s_STATE_UNUSED_4,    Lara_s_STATE_ZIPLINE
};

static void (*const cHandlers[])(Lara *) = {
    Lara_c_STATE_WALK,        Lara_c_STATE_RUN,          Lara_c_STATE_STOP,
    Lara_c_STATE_JUMP,        Lara_c_STATE_POSE,         Lara_c_STATE_BACK_FAST,
    Lara_c_STATE_TURN_RIGHT,  Lara_c_STATE_TURN_LEFT,    Lara_c_STATE_DEATH,
    Lara_c_STATE_FALL,        Lara_c_STATE_HANG,         Lara_c_STATE_REACH,
    Lara_c_STATE_SPLAT,       Lara_c_STATE_UW_TREAD,     Lara_c_STATE_LAND,
    Lara_c_STATE_COMPRESS,    Lara_c_STATE_BACK,         Lara_c_STATE_UW_SWIM,
    Lara_c_STATE_UW_GLIDE,    Lara_c_STATE_HANG_UP,      Lara_c_STATE_TURN_FAST,
    Lara_c_STATE_STEP_RIGHT,  Lara_c_STATE_STEP_LEFT,    Lara_c_STATE_ROLL_END,
    Lara_c_STATE_SLIDE,       Lara_c_STATE_JUMP_BACK,    Lara_c_STATE_JUMP_RIGHT,
    Lara_c_STATE_JUMP_LEFT,   Lara_c_STATE_JUMP_UP,      Lara_c_STATE_FALL_BACK,
    Lara_c_STATE_HANG_LEFT,   Lara_c_STATE_HANG_RIGHT,   Lara_c_STATE_SLIDE_BACK,
    Lara_c_STATE_SURF_TREAD,  Lara_c_STATE_SURF_SWIM,    Lara_c_STATE_UW_DIVE,
    Lara_c_STATE_BLOCK_PUSH,  Lara_c_STATE_BLOCK_PULL,   Lara_c_STATE_BLOCK_READY,
    Lara_c_STATE_PICKUP,      Lara_c_STATE_SWITCH_DOWN,  Lara_c_STATE_SWITCH_UP,
    Lara_c_STATE_USE_KEY,     Lara_c_STATE_USE_PUZZLE,   Lara_c_STATE_DEATH_UW,
    Lara_c_STATE_ROLL_START,  Lara_c_STATE_SPECIAL,      Lara_c_STATE_SURF_BACK,
    Lara_c_STATE_SURF_LEFT,   Lara_c_STATE_SURF_RIGHT,   Lara_c_STATE_USE_MIDAS,
    Lara_c_STATE_DEATH_MIDAS, Lara_c_STATE_SWAN_DIVE,    Lara_c_STATE_FAST_DIVE,
    Lara_c_STATE_HANDSTAND,   Lara_c_STATE_WATER_OUT,    Lara_c_STATE_CLIMB_START,
    Lara_c_STATE_CLIMB_UP,    Lara_c_STATE_CLIMB_LEFT,   Lara_c_STATE_CLIMB_END,
    Lara_c_STATE_CLIMB_RIGHT, Lara_c_STATE_CLIMB_DOWN,   Lara_c_STATE_UNUSED_1,
    Lara_c_STATE_UNUSED_2,    Lara_c_STATE_UNUSED_3,     Lara_c_STATE_WADE,
    Lara_c_STATE_ROLL_UW,     Lara_c_STATE_PICKUP_FLARE, Lara_c_STATE_ROLL_AIR,
    Lara_c_STATE_UNUSED_4,    Lara_c_STATE_ZIPLINE
};

void Lara_updateState(Lara *self)
{
    if (self->state < 71)
        sHandlers[self->state](self);
    else if (self->state < Lara_X_MAX)
        game_tr3_s_state(self);
}

void game_lara_collision(Lara *self)
{
    Lara_updateObjectsCollision(self);
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
    if (self->state < 71)
        cHandlers[self->state](self);
    else if (self->state < Lara_X_MAX)
        game_tr3_c_state(self);
}

/* Shared core platform callbacks wired into TR3 runtime. */
void *soundPlay(int16 id, const vec3i *pos)
{
    if (!tr3_gs) return NULL;
    int32_t p[3];
    if (pos) { p[0] = pos->x; p[1] = pos->y; p[2] = pos->z; }
    tr3_emit_sound(tr3_gs, id, pos ? p : NULL);
    return NULL;
}

void soundStop(int16 id) { (void)id; }
int32 rand_logic(void) { return tr3_gs ? (int32)tr3_rand(tr3_gs) : 0; }
int32 rand_draw(void)  { return tr3_gs ? (int32)tr3_rand(tr3_gs) : 0; }

void Lara_setWeaponState(Lara *self, WeaponState state)
{
    if (self && self->extraL)
        self->extraL->weaponState = (uint8)state;
    if (tr3_gs)
        tr3_gs->lara.hands_busy = (state == WEAPON_STATE_BUSY) ? 1u : 0u;
}

void Lara_hit(Lara *self, int32 damage, const vec3i *point, int32 sound)
{
    (void)point;
    if (!self) return;
    if (self->health > 0) {
        self->health = (int16)(self->health - damage);
        if (self->health < 0) self->health = 0;
    }
    if (sound) soundPlay((int16)sound, &self->pos);
}

void Lara_updateObjectsCollision(Lara *self) { (void)self; }
ItemObj *ItemObj_add(ItemType type, Room *room, const vec3i *pos, int32 angleY)
{
    (void)type; (void)room; (void)pos; (void)angleY;
    return NULL;
}
void ItemObj_activate(ItemObj *self) { (void)self; }
void ItemObj_deactivate(ItemObj *self) { (void)self; }
void game_check_trigger(const FloorData *trigger, ItemObj *lara)
{
    (void)trigger; (void)lara;
}
void game_complete_level(Lara *lara)
{
    (void)lara;
    if (!tr3_gs) return;
    tr3_gs->stop.kind = TR64_GAME_STOP_LEVEL_END;
    tr3_gs->stop.transition = TR64_GAME_TRANSITION_CAMPAIGN;
    tr3_gs->stop.next_level_id = tr3_gs->script ? tr3_gs->script->id + 1 : 0;
    memcpy(tr3_gs->stop.name, "level finish", 13);
}
void game_stop_set(uint32 kind, int32 next_level_id, const char *format, ...)
{
    if (!tr3_gs) return;
    va_list ap;
    va_start(ap, format);
    vsnprintf(tr3_gs->stop.name, sizeof(tr3_gs->stop.name), format, ap);
    va_end(ap);
    tr3_gs->stop.kind = kind;
    tr3_gs->stop.next_level_id = next_level_id;
}
void game_tr2_zipline_release(Lara *lara, int16 speed, int16 descent)
{
    if (!lara) return;
    ItemObj_animSet(lara, lara_anim_base() + 16, true, 0);
    lara->hSpeed = speed;
    lara->vSpeed = descent;
    lara->flags |= ITEM_FLAG_GRAVITY;
    Lara_setWeaponState(lara, WEAPON_STATE_FREE);
}
void game_tr2_shot_effect(TR2EffectKind kind, Room *room, vec3i pos, int16 speed, int16 yaw)
{
    (void)kind; (void)room; (void)pos; (void)speed; (void)yaw;
}
void game_tr2_item_floor_ceiling(const ItemObj *item, int32_t x, int32_t y, int32_t z,
                                 int32_t *floor, int32_t *ceiling)
{
    (void)item; (void)x; (void)y; (void)z; (void)floor; (void)ceiling;
}
TR2Creature *game_tr2_creature_slot(const ItemObj *item)
{
    (void)item;
    return NULL;
}

/* =========================================================================
 * TR3-specific hooks called by shared game/lara.c, game/item.c, game/camera.c
 * ========================================================================= */

bool game_tr3_in_swamp(const Lara *self)
{
    if (!tr3_gs || !self || !self->room)
        return false;
    uint16_t r = (uint16_t)(self->room - gs->rooms);
    return (tr3_room_flags(tr3_gs, r) & TR3_ROOM_SWAMP) != 0;
}

bool game_tr3_can_sprint(const Lara *self)
{
    if (!tr3_gs || !self)
        return false;
    if (self->waterState == WATER_STATE_WADE || game_tr3_in_swamp(self))
        return false;
    if (!(tr3_cur_input & TR3_IN_SPRINT))
        return false;
    /* Full-bar 120 stamina gate to enter Sprint from Run (§2.2). */
    return tr3_gs->lara.stamina >= 120;
}

bool game_tr3_can_crouch(const Lara *self)
{
    if (!tr3_gs || !self)
        return false;
    if (self->state != Lara_STATE_STOP)
        return false;
    if (self->waterState == WATER_STATE_WADE || game_tr3_in_swamp(self))
        return false;
    if (!(tr3_cur_input & TR3_IN_CROUCH) && !tr3_gs->lara.forced_crouch)
        return false;
    if (tr3_gs->lara.guns_drawn && is_rifle_weapon(tr3_gs->lara.weapon))
        return false;
    return true;
}

bool game_tr3_try_monkey_grab(Lara *self)
{
    if (!tr3_gs || !self)
        return false;
    if (!(self->input & IN_ACTION) || self->extraL->weaponState != WEAPON_STATE_FREE ||
        gs->cinfo.staticHit)
        return false;
    tr3_floor_sample_t fs;
    uint16_t r = (uint16_t)(self->room - gs->rooms);
    tr3_floor_eval(tr3_gs, self->pos.x, self->pos.y - 512, self->pos.z, r, false, &fs);
    if (!(fs.climb_flags & TR3_CLIMB_MONKEY) || fs.ceiling_y == TR3_NO_HEIGHT)
        return false;
    uint16_t base = lara_anim_base();
    ItemObj_animSet(self, base + 233, true, 0);
    self->state = self->goalState = Lara_STATE_MONKEY_IDLE;
    self->pos.y = fs.ceiling_y + 704;
    self->hSpeed = 0;
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    Lara_setWeaponState(self, WEAPON_STATE_BUSY);
    return true;
}

bool game_tr3_try_climb_to_crawl(Lara *self)
{
    if (!tr3_gs || !self)
        return false;
    const CollisionInfo *c = &gs->cinfo;
    /* Ledge ahead between -850..-650 with 2-click crawlspace headroom 512..761 (§2.3). */
    if (c->staticHit || c->f.floor < -850 || c->f.floor > -650)
        return false;
    int32_t fh = c->f.floor - c->f.ceiling + LARA_HEIGHT;
    int32_t lh = c->l.floor - c->l.ceiling + LARA_HEIGHT;
    int32_t rh = c->r.floor - c->r.ceiling + LARA_HEIGHT;
    if (fh >= 512 && fh < 762 && lh >= 512 && rh >= 512) {
        self->goalState = Lara_STATE_CLIMB_TO_CRAWL;
        return true;
    }
    return false;
}

void game_tr3_camera_request_extra(Lara *lara)
{
    Camera *c = &lara->extraL->camera;
    switch (lara->state) {
    case Lara_STATE_CRAWL_IDLE:
    case Lara_STATE_CRAWL_FORWARD:
    case Lara_STATE_CRAWL_BACK:
    case Lara_STATE_CRAWL_TURN_L:
    case Lara_STATE_CRAWL_TURN_R:
        c->targetAngle.x = -4186; /* -23 deg (§2.3) */
        break;
    case Lara_STATE_MONKEY_IDLE:
        c->targetAngle.x = -10920; /* -60 deg (§2.4) */
        break;
    case Lara_STATE_MONKEY_FORWARD:
    case Lara_STATE_MONKEY_LEFT:
    case Lara_STATE_MONKEY_RIGHT:
    case Lara_STATE_MONKEY_TURN_L:
    case Lara_STATE_MONKEY_TURN_R:
        c->targetAngle.x = 1820; /* +10 deg (§2.4) */
        break;
    default:
        break;
    }
}

void game_tr3_swamp_water_status(Lara *self, int32 line, int32 feet)
{
    if (line == WALL || feet < 256) {
        if (self->waterState == WATER_STATE_WADE) {
            self->waterState = WATER_STATE_ABOVE;
            if (self->state == Lara_STATE_WADE)
                self->goalState = Lara_STATE_RUN;
        }
        return;
    }
    /* Swamp locks Lara into Wade regardless of depth (§2.5). */
    if (self->waterState != WATER_STATE_WADE) {
        bool airborne = ((self->flags & ITEM_FLAG_GRAVITY) != 0) ||
                        self->state == Lara_STATE_FALL || self->state == Lara_STATE_JUMP ||
                        self->state == Lara_STATE_JUMP_UP || self->state == Lara_STATE_JUMP_BACK ||
                        self->state == Lara_STATE_JUMP_LEFT || self->state == Lara_STATE_JUMP_RIGHT ||
                        self->state == Lara_STATE_REACH || self->state == Lara_STATE_SWAN_DIVE ||
                        self->state == Lara_STATE_FAST_DIVE || self->state == Lara_STATE_FALL_BACK;
        if (self->state == Lara_STATE_SWAN_DIVE || self->state == Lara_STATE_FAST_DIVE)
            self->pos.y = line + 1000;
        self->waterState = WATER_STATE_WADE;
        if (airborne) {
            ItemObj_animSet(self, lara_anim_base() + Lara_ANIM_WADE, true, 0);
            self->flags &= ~ITEM_FLAG_GRAVITY;
            self->vSpeed = 0;
        } else {
            self->goalState = Lara_STATE_STOP;
        }
    }
}

void game_tr3_swamp_water_air(Lara *self)
{
    if (!tr3_gs || !self) return;
    if (game_tr3_in_swamp(self)) {
        tr3_floor_sample_t fs;
        uint16_t r = (uint16_t)(self->room - gs->rooms);
        tr3_floor_eval(tr3_gs, self->pos.x, self->pos.y - 128, self->pos.z, r, false, &fs);
        int32_t depth = (fs.water_y != TR3_NO_HEIGHT) ? (self->pos.y - fs.water_y) : 0;
        if (depth > 775) {
            /* Deep swamp suffocation (>775): -6 air/tick, then -10 HP/tick (§2.5). */
            self->timer = (int16)(self->timer - 6);
            if (self->timer < 0) {
                self->timer = -1;
                self->health = (int16)(self->health - 10);
            }
            return;
        }
    } else if (self->waterState == WATER_STATE_UNDER) {
        if (--self->timer < 0) {
            self->timer = -1;
            self->health = (int16)(self->health - 5);
        }
        return;
    }
    /* TR3 land/wade/surface/shallow-swamp air recovery: +10/tick up to 1800 (§2.6). */
    if (self->timer < 1800) {
        self->timer = (int16)(self->timer + 10);
        if (self->timer > 1800) self->timer = 1800;
    }
}

bool game_tr3_swamp_sink(Lara *self)
{
    if (!self || !game_tr3_in_swamp(self) || self->waterState != WATER_STATE_WADE)
        return false;
    if (gs->cinfo.m.floor == WALL || gs->cinfo.m.floor >= 0) {
        self->pos.y = i32_add(self->pos.y, 2);
        if (gs->cinfo.m.floor != WALL && gs->cinfo.m.floor < 2)
            self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor - 2);
    } else {
        /* Rising floor lifts Lara out normally (§2.5). */
        self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor);
    }
    return true;
}

/* =========================================================================
 * TR3 movement states 71..88 (Sprint, Sprint Roll, Crouch, Crawl, Monkey)
 * ========================================================================= */

static void tr3_update_forced_crouch(Lara *self)
{
    if (!tr3_gs) return;
    tr3_floor_sample_t fs;
    uint16_t r = (uint16_t)(self->room - gs->rooms);
    tr3_floor_eval(tr3_gs, self->pos.x, self->pos.y - 400, self->pos.z, r, false, &fs);
    tr3_gs->lara.forced_crouch = (fs.ceiling_y != TR3_NO_HEIGHT && fs.ceiling_y - self->pos.y > -762) ? 1u : 0u;
}

static bool tr3_crawl_probe_ok(const Lara *self, int16_t yaw, int32_t dist, bool back_check)
{
    if (!tr3_gs) return false;
    int32_t nx = self->pos.x + ((tr3_sin(yaw) * dist) >> 14);
    int32_t nz = self->pos.z + ((tr3_cos(yaw) * dist) >> 14);
    tr3_floor_sample_t fs;
    uint16_t r = (uint16_t)(self->room - gs->rooms);
    tr3_floor_eval(tr3_gs, nx, self->pos.y - 128, nz, r, false, &fs);
    if (fs.floor_y == TR3_NO_HEIGHT) return false;
    if (back_check) {
        if (fs.ceiling_y == TR3_NO_HEIGHT || fs.ceiling_y > self->pos.y + 128)
            return false;
    }
    if (fs.ceiling_y != TR3_NO_HEIGHT && fs.floor_y - fs.ceiling_y < 400)
        return false;
    if (abs32(fs.floor_y - self->pos.y) > 254)
        return false;
    if (fs.height_type == TR3_HEIGHT_BIG_SLOPE || fs.height_type == TR3_HEIGHT_DIAGONAL)
        return false;
    return true;
}

static void tr3_monkey_drop(Lara *self)
{
    uint16_t base = lara_anim_base();
    ItemObj_animSet(self, base + 28, true, 9);
    self->state = self->goalState = Lara_STATE_JUMP_UP;
    self->pos.y = i32_add(self->pos.y, 256);
    self->hSpeed = 2;
    self->vSpeed = 1;
    self->flags |= ITEM_FLAG_GRAVITY;
    Lara_setWeaponState(self, WEAPON_STATE_FREE);
}

void game_tr3_s_state(Lara *self)
{
    if (!tr3_gs || !self) return;
    const uint32_t in = tr3_cur_input;
    const uint16_t base = lara_anim_base();

    switch (self->state) {
    case Lara_STATE_SPRINT: /* 73 (§2.2) */
        if (self->health <= 0) { self->goalState = Lara_STATE_RUN; return; }
        /* Turn capped at +-4 deg (728), roll lean +-16 deg (2912). */
        if (self->input & IN_LEFT) {
            self->turnSpeed = (int16)clamp32(self->turnSpeed - LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
            self->angle.z = (int16)clamp32(self->angle.z - 273, -2912, 2912);
        } else if (self->input & IN_RIGHT) {
            self->turnSpeed = (int16)clamp32(self->turnSpeed + LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
            self->angle.z = (int16)clamp32(self->angle.z + 273, -2912, 2912);
        }
        if (in & TR3_IN_CROUCH) {
            self->goalState = Lara_STATE_STOP;
        } else if (self->input & IN_JUMP) {
            self->goalState = Lara_STATE_SPRINT_ROLL;
        } else if (!(self->input & IN_UP) || (self->input & IN_WALK) ||
                   !(in & TR3_IN_SPRINT) || tr3_gs->lara.stamina <= 0) {
            self->goalState = (self->input & IN_UP) ? Lara_STATE_RUN : Lara_STATE_STOP;
        } else {
            self->goalState = Lara_STATE_SPRINT;
        }
        break;

    case Lara_STATE_SPRINT_ROLL: /* 74 (§2.2) */
        if (self->vSpeed > 131 && self->goalState != Lara_STATE_DEATH &&
            self->goalState != Lara_STATE_STOP && self->goalState != Lara_STATE_RUN)
            self->goalState = Lara_STATE_FALL;
        else if ((self->input & IN_UP) && !(self->input & IN_WALK) && self->waterState != WATER_STATE_WADE)
            self->goalState = Lara_STATE_RUN;
        else
            self->goalState = Lara_STATE_STOP;
        break;

    case Lara_STATE_CROUCH_IDLE: /* 71 (§2.3) */
        tr3_update_forced_crouch(self);
        if (self->health <= 0) {
            self->goalState = Lara_STATE_CRAWL_IDLE;
            return;
        }
        if (self->input & IN_LOOK)
            Lara_s_checkLook(self);
        if (self->waterState == WATER_STATE_WADE) {
            self->goalState = Lara_STATE_STOP;
            return;
        }
        if (self->input & IN_LEFT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed - LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed + LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
        if ((self->input & (IN_UP | IN_DOWN)) && self->extraL->weaponState == WEAPON_STATE_FREE &&
            ((in & TR3_IN_CROUCH) || tr3_gs->lara.forced_crouch)) {
            self->extraL->torso.angle = vec3s_create(0, 0, 0);
            self->goalState = Lara_STATE_CRAWL_IDLE;
        } else if (!(in & TR3_IN_CROUCH) && !tr3_gs->lara.forced_crouch) {
            self->goalState = Lara_STATE_STOP;
        } else {
            self->goalState = Lara_STATE_CROUCH_IDLE;
        }
        break;

    case Lara_STATE_CROUCH_ROLL: /* 72 */
        self->goalState = Lara_STATE_CROUCH_IDLE;
        break;

    case Lara_STATE_CRAWL_IDLE: /* 80 (§2.3) */
        tr3_update_forced_crouch(self);
        if (self->health <= 0) {
            ItemObj_animSet(self, base + 301, true, 0);
            self->state = self->goalState = Lara_STATE_DEATH;
            return;
        }
        Lara_setWeaponState(self, WEAPON_STATE_BUSY);
        if ((!(in & TR3_IN_CROUCH) && !tr3_gs->lara.forced_crouch) || (in & TR3_IN_WEAPON)) {
            self->goalState = Lara_STATE_CROUCH_IDLE;
            Lara_setWeaponState(self, WEAPON_STATE_FREE);
            return;
        }
        if ((self->input & (IN_DOWN | IN_ACTION)) == (IN_DOWN | IN_ACTION)) {
            /* Check Crawl-to-Climb drop > 768 behind Lara (§2.3). */
            int16_t back_yaw = wrap_i16((uint16)self->angle.y + 32768u);
            int32_t bx = self->pos.x + ((tr3_sin(back_yaw) * 300) >> 14);
            int32_t bz = self->pos.z + ((tr3_cos(back_yaw) * 300) >> 14);
            tr3_floor_sample_t bfs;
            tr3_floor_eval(tr3_gs, bx, self->pos.y - 128, bz, (uint16_t)(self->room - gs->rooms), false, &bfs);
            if (bfs.ceiling_y != TR3_NO_HEIGHT && bfs.ceiling_y <= self->pos.y + 128 &&
                (bfs.floor_y == TR3_NO_HEIGHT || bfs.floor_y - self->pos.y > 768)) {
                self->angle.y = wrap_i16((self->angle.y + 8192) & 0xC000);
                self->goalState = Lara_STATE_CRAWL_TO_CLIMB;
                return;
            }
        }
        if ((self->input & IN_UP) && tr3_crawl_probe_ok(self, self->angle.y, 256, false)) {
            self->goalState = Lara_STATE_CRAWL_FORWARD;
        } else if ((self->input & IN_DOWN) &&
                   tr3_crawl_probe_ok(self, wrap_i16((uint16)self->angle.y + 32768u), 300, true)) {
            self->goalState = Lara_STATE_CRAWL_BACK;
        } else if (self->input & IN_LEFT) {
            self->goalState = Lara_STATE_CRAWL_TURN_L;
        } else if (self->input & IN_RIGHT) {
            self->goalState = Lara_STATE_CRAWL_TURN_R;
        } else {
            self->goalState = Lara_STATE_CRAWL_IDLE;
        }
        break;

    case Lara_STATE_CRAWL_FORWARD: /* 81 (§2.3) */
        tr3_update_forced_crouch(self);
        if (self->health <= 0 || !(self->input & IN_UP) ||
            (!(in & TR3_IN_CROUCH) && !tr3_gs->lara.forced_crouch)) {
            self->goalState = Lara_STATE_CRAWL_IDLE;
            return;
        }
        if (self->input & IN_LEFT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed - 409, -546, 546);
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed + 409, -546, 546);
        self->goalState = Lara_STATE_CRAWL_FORWARD;
        break;

    case Lara_STATE_CRAWL_BACK: /* 86 (§2.3) */
        tr3_update_forced_crouch(self);
        if (self->health <= 0 || !(self->input & IN_DOWN) ||
            (!(in & TR3_IN_CROUCH) && !tr3_gs->lara.forced_crouch)) {
            self->goalState = Lara_STATE_CRAWL_IDLE;
            return;
        }
        /* Inverted steering in crawl back (§2.3). */
        if (self->input & IN_LEFT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed + 409, -546, 546);
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed - 409, -546, 546);
        self->goalState = Lara_STATE_CRAWL_BACK;
        break;

    case Lara_STATE_CRAWL_TURN_L: /* 84 */
        self->angle.y = wrap_i16((int32)self->angle.y - 273);
        self->goalState = (self->input & IN_LEFT) ? Lara_STATE_CRAWL_TURN_L : Lara_STATE_CRAWL_IDLE;
        break;

    case Lara_STATE_CRAWL_TURN_R: /* 85 */
        self->angle.y = wrap_i16((int32)self->angle.y + 273);
        self->goalState = (self->input & IN_RIGHT) ? Lara_STATE_CRAWL_TURN_R : Lara_STATE_CRAWL_IDLE;
        break;

    case Lara_STATE_CLIMB_TO_CRAWL: /* 87 */
        self->goalState = Lara_STATE_CROUCH_IDLE;
        break;

    case Lara_STATE_CRAWL_TO_CLIMB: /* 88 */
        if (self->animIndex == base + 302) {
            self->pos.y |= 255;
            self->vSpeed = 512;
            ItemObj_animSet(self, Lara_ANIM_HANG, true, 12);
            self->vSpeed = 0;
            self->hSpeed = 0;
            self->flags &= ~ITEM_FLAG_GRAVITY;
        }
        break;

    case Lara_STATE_MONKEY_IDLE: /* 75 (§2.4) */
        if (self->health <= 0 || !(self->input & IN_ACTION)) {
            tr3_monkey_drop(self);
            return;
        }
        if (self->input & IN_UP)
            self->goalState = Lara_STATE_MONKEY_FORWARD;
        else if (gs->tr2->sidestepInput & TR2_IN_STEP_LEFT)
            self->goalState = Lara_STATE_MONKEY_LEFT;
        else if (gs->tr2->sidestepInput & TR2_IN_STEP_RIGHT)
            self->goalState = Lara_STATE_MONKEY_RIGHT;
        else if (self->input & IN_LEFT)
            self->goalState = Lara_STATE_MONKEY_TURN_L;
        else if (self->input & IN_RIGHT)
            self->goalState = Lara_STATE_MONKEY_TURN_R;
        else
            self->goalState = Lara_STATE_MONKEY_IDLE;
        break;

    case Lara_STATE_MONKEY_FORWARD: /* 76 */
        if (self->health <= 0 || !(self->input & IN_ACTION)) {
            tr3_monkey_drop(self);
            return;
        }
        if (self->input & IN_LEFT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed - 409, -546, 546);
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)clamp32(self->turnSpeed + 409, -546, 546);
        self->goalState = (self->input & IN_UP) ? Lara_STATE_MONKEY_FORWARD : Lara_STATE_MONKEY_IDLE;
        break;

    case Lara_STATE_MONKEY_LEFT: /* 77 */
        if (self->health <= 0 || !(self->input & IN_ACTION)) { tr3_monkey_drop(self); return; }
        self->goalState = (gs->tr2->sidestepInput & TR2_IN_STEP_LEFT) ? Lara_STATE_MONKEY_LEFT : Lara_STATE_MONKEY_IDLE;
        break;

    case Lara_STATE_MONKEY_RIGHT: /* 78 */
        if (self->health <= 0 || !(self->input & IN_ACTION)) { tr3_monkey_drop(self); return; }
        self->goalState = (gs->tr2->sidestepInput & TR2_IN_STEP_RIGHT) ? Lara_STATE_MONKEY_RIGHT : Lara_STATE_MONKEY_IDLE;
        break;

    case Lara_STATE_MONKEY_TURN_L: /* 82 */
        if (self->health <= 0 || !(self->input & IN_ACTION)) { tr3_monkey_drop(self); return; }
        self->angle.y = wrap_i16((int32)self->angle.y - 273);
        self->goalState = (self->input & IN_LEFT) ? Lara_STATE_MONKEY_TURN_L : Lara_STATE_MONKEY_IDLE;
        break;

    case Lara_STATE_MONKEY_TURN_R: /* 83 */
        if (self->health <= 0 || !(self->input & IN_ACTION)) { tr3_monkey_drop(self); return; }
        self->angle.y = wrap_i16((int32)self->angle.y + 273);
        self->goalState = (self->input & IN_RIGHT) ? Lara_STATE_MONKEY_TURN_R : Lara_STATE_MONKEY_IDLE;
        break;

    default:
        break;
    }
}

void game_tr3_c_state(Lara *self)
{
    if (!tr3_gs || !self) return;
    const uint16_t base = lara_anim_base();

    switch (self->state) {
    case Lara_STATE_SPRINT: /* 73 (§2.2) */
        Lara_c_angle(self, 0);
        gs->cinfo.gapPos = -WALL;
        gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
        gs->cinfo.gapCeiling = 0;
        gs->cinfo.stopOnSlant = 1;
        ItemObj_collideRoom(self, LARA_HEIGHT, 0);
        if (Lara_c_checkCeiling(self))
            return;
        if (Lara_c_checkWall(self)) {
            self->angle.z = 0;
            ItemObj_animSet(self, Lara_ANIM_STAND, true, 0);
            return;
        }
        if (Lara_c_checkFall(self, LARA_STEP_HEIGHT, Lara_ANIM_FALL_FORTH))
            return;
        if (gs->cinfo.m.floor >= -LARA_STEP_HEIGHT && gs->cinfo.m.floor < -128) {
            /* Step-up during sprint interrupts to Run step-up (§2.2). */
            ItemObj_animSet(self, (self->frameIndex & 1) ? Lara_ANIM_RUN_ASCEND_RIGHT
                                                         : Lara_ANIM_RUN_ASCEND_LEFT, false, 0);
            self->state = Lara_STATE_RUN;
        }
        if (Lara_c_checkSlide(self))
            return;
        if (gs->cinfo.m.floor >= 50)
            self->pos.y = i32_add(self->pos.y, 50);
        else
            self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor);
        break;

    case Lara_STATE_SPRINT_ROLL: /* 74 (§2.2) */
        Lara_c_angle(self, 0);
        gs->cinfo.gapPos = -WALL;
        gs->cinfo.gapNeg = -256;
        gs->cinfo.gapCeiling = 192;
        gs->cinfo.stopOnSlant = 1;
        ItemObj_collideRoom(self, 570, 0);
        if (Lara_c_checkCeiling(self))
            return;
        if (Lara_c_checkWall(self)) {
            ItemObj_animSet(self, Lara_ANIM_STAND, true, 0);
            return;
        }
        if (self->vSpeed > 0 && gs->cinfo.m.floor <= 0) {
            Lara_c_fall(self);
            return;
        }
        if (Lara_c_checkFall(self, LARA_STEP_HEIGHT, Lara_ANIM_FALL_FORTH))
            return;
        if (gs->cinfo.m.floor != WALL)
            self->pos.y = i32_add(self->pos.y, X_MIN(gs->cinfo.m.floor, 50));
        break;

    case Lara_STATE_CROUCH_IDLE:
    case Lara_STATE_CROUCH_ROLL:
    case Lara_STATE_CRAWL_IDLE:
    case Lara_STATE_CRAWL_FORWARD:
    case Lara_STATE_CRAWL_BACK:
    case Lara_STATE_CRAWL_TURN_L:
    case Lara_STATE_CRAWL_TURN_R: {
        bool back = (self->state == Lara_STATE_CRAWL_BACK);
        gs->cinfo.radius = back ? 250 : 200;
        Lara_c_angle(self, back ? ANGLE_180 : 0);
        gs->cinfo.gapPos = 255;
        gs->cinfo.gapNeg = -255;
        gs->cinfo.gapCeiling = 0;
        gs->cinfo.stopOnSlant = 1;
        gs->cinfo.stopOnLava = 1;
        ItemObj_collideRoom(self, 400, 0);
        if (gs->cinfo.type == CT_FRONT || gs->cinfo.type == CT_FRONT_CEILING ||
            gs->cinfo.type == CT_CEILING || gs->cinfo.type == CT_FLOOR_CEILING) {
            self->pos = gs->cinfo.pos;
            self->hSpeed = 0;
            if (self->state == Lara_STATE_CRAWL_FORWARD || self->state == Lara_STATE_CRAWL_BACK)
                ItemObj_animSet(self, base + 263, true, 0);
        } else {
            Lara_c_applyOffset(self);
            if (gs->cinfo.type == CT_LEFT)
                self->angle.y = wrap_i16((int32)self->angle.y + 364);
            else if (gs->cinfo.type == CT_RIGHT)
                self->angle.y = wrap_i16((int32)self->angle.y - 364);
            if (gs->cinfo.m.floor != WALL && abs32(gs->cinfo.m.floor) <= 255)
                self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor);
        }
        break;
    }

    case Lara_STATE_MONKEY_IDLE:
    case Lara_STATE_MONKEY_FORWARD:
    case Lara_STATE_MONKEY_LEFT:
    case Lara_STATE_MONKEY_RIGHT:
    case Lara_STATE_MONKEY_TURN_L:
    case Lara_STATE_MONKEY_TURN_R: {
        int16_t delta_ang = (self->state == Lara_STATE_MONKEY_LEFT) ? -ANGLE_90 :
                            (self->state == Lara_STATE_MONKEY_RIGHT) ? ANGLE_90 : 0;
        gs->cinfo.radius = 100;
        Lara_c_angle(self, delta_ang);
        gs->cinfo.gapPos = -WALL;
        gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
        gs->cinfo.gapCeiling = 0;
        ItemObj_collideRoom(self, 600, 0);
        tr3_floor_sample_t fs;
        tr3_floor_eval(tr3_gs, self->pos.x, self->pos.y - 512, self->pos.z,
                       (uint16_t)(self->room - gs->rooms), false, &fs);
        if (!(fs.climb_flags & TR3_CLIMB_MONKEY) || fs.ceiling_y == TR3_NO_HEIGHT) {
            self->pos = gs->cinfo.pos;
            ItemObj_animSet(self, base + 234, true, 0);
            self->state = self->goalState = Lara_STATE_MONKEY_IDLE;
            return;
        }
        if (gs->cinfo.type == CT_FRONT || gs->cinfo.type == CT_FRONT_CEILING ||
            abs32(gs->cinfo.m.ceiling - gs->cinfo.f.ceiling) >= 50) {
            self->pos = gs->cinfo.pos;
            if (self->state != Lara_STATE_MONKEY_IDLE) {
                ItemObj_animSet(self, base + 234, true, 0);
                self->state = self->goalState = Lara_STATE_MONKEY_IDLE;
            }
        } else {
            Lara_c_applyOffset(self);
            self->pos.y = fs.ceiling_y + 704;
        }
        break;
    }

    default:
        break;
    }
}

/* =========================================================================
 * Sync helpers between shared gs->players[0] and tr3_gs->lara
 * ========================================================================= */

static void sync_shared_to_tr3(tr3_state_t *st)
{
    if (!gs || !gs->players[0]) return;
    const Lara *src = gs->players[0];
    const ExtraInfoLara *e = src->extraL;
    tr3_lara_t *dst = &st->lara;

    dst->x = src->pos.x;
    dst->y = src->pos.y;
    dst->z = src->pos.z;
    dst->room = (src->room && src->room >= gs->rooms) ? (uint16_t)(src->room - gs->rooms) : 0u;
    dst->rot_x = src->angle.x;
    dst->rot_y = src->angle.y;
    dst->rot_z = src->angle.z;
    dst->turn_rate = src->turnSpeed;
    dst->move_angle = e ? e->moveAngle : src->angle.y;
    dst->speed = src->hSpeed;
    dst->fall_speed = src->vSpeed;
    dst->anim = src->animIndex;
    dst->frame = src->frameIndex;
    dst->state = src->state;
    dst->goal_state = src->goalState;
    dst->water_status = water_shared_to_tr3(src->waterState);
    dst->gravity = (src->flags & ITEM_FLAG_GRAVITY) ? 1u : 0u;
    dst->hp = src->health;
    dst->air = src->timer;
    dst->hands_busy = (e && e->weaponState == WEAPON_STATE_BUSY) ? 1u : 0u;
    if (e) {
        dst->head_x = e->head.angle.x;
        dst->head_y = e->head.angle.y;
        if (!dst->guns_drawn || dst->target_item < 0) {
            dst->torso_x = e->torso.angle.x;
            dst->torso_y = e->torso.angle.y;
        }
    }
}

void tr3_lara_sync_from_tr3(tr3_state_t *st)
{
    if (!gs || !gs->players[0] || !st) return;
    Lara *dst = gs->players[0];
    ExtraInfoLara *e = dst->extraL;
    const tr3_lara_t *src = &st->lara;
    uint16_t r = (src->room < gs->level.roomsCount) ? src->room : 0u;

    if (dst->room && dst->room != gs->rooms + r) {
        Room_remove(dst->room, dst);
        Room_add(gs->rooms + r, dst);
    } else if (!dst->room) {
        Room_add(gs->rooms + r, dst);
    }
    dst->pos = vec3i_create(src->x, src->y, src->z);
    dst->angle = vec3s_create(src->rot_x, src->rot_y, src->rot_z);
    dst->turnSpeed = src->turn_rate;
    dst->hSpeed = src->speed;
    dst->vSpeed = src->fall_speed;
    dst->animIndex = src->anim;
    dst->frameIndex = src->frame;
    dst->state = src->state;
    dst->goalState = src->goal_state;
    dst->waterState = water_tr3_to_shared(src->water_status);
    if (src->gravity) dst->flags |= ITEM_FLAG_GRAVITY;
    else dst->flags &= ~ITEM_FLAG_GRAVITY;
    dst->health = src->hp;
    dst->timer = src->air;
    if (e) {
        e->moveAngle = src->move_angle;
        e->weaponState = src->hands_busy ? WEAPON_STATE_BUSY : WEAPON_STATE_FREE;
    }
}

void tr3_lara_set_anim(tr3_state_t *st, uint16_t clip_rel, uint8_t state)
{
    if (!tr64_level_table(st->level, TR64_CORE_animations)->count) {
        st->lara.state = state;
        st->lara.goal_state = state;
        tr3_lara_sync_from_tr3(st);
        return;
    }
    uint16_t idx = lara_anim_index(st, clip_rel);
    const tr64_core_animations_t *a = tr64_level_animations(st->level, idx);
    st->lara.anim = idx;
    st->lara.frame = tr64_u16(a->frame_start);
    st->lara.state = state;
    st->lara.goal_state = state;
    tr3_lara_sync_from_tr3(st);
}

/* =========================================================================
 * TR3 Braid, Weapons/Flares, Water Currents, Status & Medipack
 * ========================================================================= */

void tr3_lara_use_medipack(tr3_state_t *st, bool large)
{
    tr3_lara_t *lara = &st->lara;
    if (lara->hp <= 0)
        return;
    if (lara->hp >= 1000 && lara->poison == 0)
        return;
    if (large) {
        if (lara->large_medi <= 0)
            return;
        --lara->large_medi;
        lara->hp = 1000;
        st->stats.mediUsed += 2;
    } else {
        if (lara->small_medi <= 0)
            return;
        --lara->small_medi;
        lara->hp = (int16_t)clamp32(lara->hp + 500, 0, 1000);
        ++st->stats.mediUsed;
    }
    lara->poison = 0;
    if (gs && gs->players[0])
        gs->players[0]->health = lara->hp;
    tr3_emit_sound(st, 116, &lara->x);
}

static void lara_update_braid(tr3_state_t *st)
{
    tr3_lara_t *lara = &st->lara;
    int32_t head_h = is_crouch_or_crawl(lara->state) ? 350 : 660;
    int32_t s = tr3_sin(lara->rot_y);
    int32_t c = tr3_cos(lara->rot_y);
    int32_t root_x = lara->x - ((s * 60) >> 14);
    int32_t root_y = lara->y - head_h;
    int32_t root_z = lara->z - ((c * 60) >> 14);

    if (!lara->braid_init) {
        for (int i = 0; i <= TR3_BRAID_SEGS; ++i) {
            lara->braid[i].x = root_x;
            lara->braid[i].y = root_y + i * 36;
            lara->braid[i].z = root_z;
            lara->braid[i].vx = lara->braid[i].vy = lara->braid[i].vz = 0;
            lara->braid[i].rot_x = -16384;
            lara->braid[i].rot_y = lara->rot_y;
        }
        lara->braid_init = 1;
        return;
    }

    lara->braid[0].x = root_x;
    lara->braid[0].y = root_y;
    lara->braid[0].z = root_z;

    const bool windy = (tr3_room_flags(st, lara->room) & TR3_ROOM_WIND) != 0;
    const bool water = (lara->water_status == TR3_WATER_UNDER);
    int32_t wind_x = windy ? ((tr3_sin((int32_t)(st->ticks * 512u)) * 6) >> 14) + 4 : 0;
    int32_t wind_z = windy ? ((tr3_cos((int32_t)(st->ticks * 384u)) * 5) >> 14) : 0;

    for (int i = 1; i <= TR3_BRAID_SEGS; ++i) {
        tr3_braid_node_t *prev = &lara->braid[i - 1];
        tr3_braid_node_t *cur = &lara->braid[i];
        int32_t ox = cur->x, oy = cur->y, oz = cur->z;
        cur->x += ((cur->vx * 3) >> 2) + wind_x;
        cur->y += ((cur->vy * 3) >> 2) + (water ? -2 : 10);
        cur->z += ((cur->vz * 3) >> 2) + wind_z;
        int32_t dx = cur->x - prev->x, dy = cur->y - prev->y, dz = cur->z - prev->z;
        int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz;
        uint32_t dist = (d2 > (int64_t)UINT32_MAX) ? 65535u : tr3_isqrt((uint32_t)d2);
        if (dist > 0) {
            cur->x = prev->x + (int32_t)(((int64_t)dx * 38) / (int64_t)dist);
            cur->y = prev->y + (int32_t)(((int64_t)dy * 38) / (int64_t)dist);
            cur->z = prev->z + (int32_t)(((int64_t)dz * 38) / (int64_t)dist);
        }
        cur->vx = cur->x - ox;
        cur->vy = cur->y - oy;
        cur->vz = cur->z - oz;
        int64_t hd2 = (int64_t)dx * dx + (int64_t)dz * dz;
        int32_t hdist = (hd2 > (int64_t)UINT32_MAX) ? 65535 : (int32_t)tr3_isqrt((uint32_t)hd2);
        cur->rot_y = tr3_atan2(dx, dz);
        cur->rot_x = tr3_atan2(dy, hdist > 0 ? hdist : 1);
    }
}

static void lara_weapons_tick(tr3_state_t *st, uint32_t input)
{
    tr3_lara_t *lara = &st->lara;
    if (lara->muzzle_timer > 0) --lara->muzzle_timer;
    if (lara->fire_cd > 0) --lara->fire_cd;

    if ((input & TR3_IN_FLARE) && !(st->last_input & TR3_IN_FLARE)) {
        if (lara->flare_lit) {
            lara->flare_lit = 0;
            lara->flare_age = 0;
        } else if (lara->flares > 0 && !lara->hands_busy) {
            --lara->flares;
            lara->flare_lit = 1;
            lara->flare_age = 0;
            lara->guns_drawn = 0;
            tr3_emit_sound(st, 11, &lara->x);
        }
    }
    if (lara->flare_lit && ++lara->flare_age >= 900u) {
        lara->flare_lit = 0;
        lara->flare_age = 0;
    }

    if ((input & TR3_IN_SWAP_GUN) && !(st->last_input & TR3_IN_SWAP_GUN)) {
        for (uint8_t step = 1; step < TR3_WP_COUNT; ++step) {
            uint8_t cand = (uint8_t)((lara->weapon + step) % TR3_WP_COUNT);
            if (cand != TR3_WP_NONE && (lara->owned_weapons & (1u << cand))) {
                lara->weapon = cand;
                if (is_crouch_or_crawl(lara->state) && is_rifle_weapon(cand))
                    lara->guns_drawn = 0;
                break;
            }
        }
    }

    if ((input & TR3_IN_WEAPON) && !(st->last_input & TR3_IN_WEAPON)) {
        if (lara->guns_drawn) {
            lara->guns_drawn = 0;
            lara->target_item = -1;
            tr3_emit_sound(st, 6, &lara->x);
        } else if (!lara->hands_busy && lara->water_status != TR3_WATER_UNDER &&
                   lara->water_status != TR3_WATER_SURFACE &&
                   (!is_crouch_or_crawl(lara->state) || !is_rifle_weapon(lara->weapon))) {
            lara->guns_drawn = 1;
            lara->flare_lit = 0;
            tr3_emit_sound(st, 6, &lara->x);
        }
    }

    if (!lara->guns_drawn) {
        lara->target_item = -1;
        return;
    }

    int32_t range = (lara->weapon == TR3_WP_MP5) ? 12288 : 8192;
    int16_t best = -1;
    int32_t best_d = range;
    for (uint16_t i = 0; i < st->item_count; ++i) {
        const tr3_item_t *it = &st->items[i];
        if (!it->active || it->invisible || it->killed || it->hp <= 0) continue;
        if (it->type != TR3_OBJ_TIGER && it->type != TR3_OBJ_MONKEY) continue;
        int32_t dx = it->x - lara->x, dy = it->y - lara->y, dz = it->z - lara->z;
        if (abs32(dx) > range || abs32(dz) > range || abs32(dy) > 4096) continue;
        int32_t dist = (int32_t)tr3_isqrt((uint32_t)(dx * dx + dz * dz));
        if (dist >= best_d) continue;
        int16_t diff = (int16_t)(tr3_atan2(dx, dz) - lara->rot_y);
        if (abs32(diff) <= 10922) { best = (int16_t)i; best_d = dist; }
    }
    lara->target_item = best;
    if (best >= 0) {
        const tr3_item_t *t = &st->items[best];
        lara->torso_y = (int16_t)clamp32((int16_t)(tr3_atan2(t->x - lara->x, t->z - lara->z) - lara->rot_y),
                                         -10922, 10922);
    }

    if ((input & TR3_IN_ACTION) && lara->fire_cd == 0) {
        int32_t dmg = 1; uint8_t cd = 9; int32_t snd = 8;
        switch (lara->weapon) {
        case TR3_WP_SHOTGUN:
            if (lara->ammo[TR3_WP_SHOTGUN] <= 0) return;
            lara->ammo[TR3_WP_SHOTGUN] = clamp32(lara->ammo[TR3_WP_SHOTGUN] - 6, 0, 9999);
            dmg = 18; cd = 24; snd = 45; break;
        case TR3_WP_DEAGLE:
            if (lara->ammo[TR3_WP_DEAGLE] <= 0) return;
            --lara->ammo[TR3_WP_DEAGLE]; dmg = 21; cd = 16; snd = 8; break;
        case TR3_WP_UZIS:
            if (lara->ammo[TR3_WP_UZIS] <= 0) return;
            --lara->ammo[TR3_WP_UZIS]; dmg = 1; cd = 3; snd = 43; break;
        case TR3_WP_MP5:
            if (lara->ammo[TR3_WP_MP5] <= 0) return;
            --lara->ammo[TR3_WP_MP5]; dmg = 3; cd = 4; snd = 43; break;
        default:
            dmg = 1; cd = 9; snd = 8; break;
        }
        lara->fire_cd = cd;
        lara->muzzle_timer = 3;
        ++st->stats.ammoUsed;
        tr3_emit_sound(st, snd, &lara->x);
        if (lara->target_item >= 0)
            tr3_entities_call(st, TR3_ENT_HIT, input, lara->target_item, dmg);
    }
}

static void lara_apply_water_current(tr3_state_t *st, Lara *core_lara)
{
    tr3_lara_t *lara = &st->lara;
    const tr64_level_t *l = st->level;
    const uint32_t ccount = tr64_level_table(l, TR64_CORE_cameras)->count;
    if (lara->current_sink > 0 && (uint32_t)(lara->current_sink - 1) < ccount) {
        const tr64_core_cameras_t *sink = tr64_level_cameras(l, (uint32_t)(lara->current_sink - 1));
        int32_t sx = tr64_i32(sink->x), sy = tr64_i32(sink->y), sz = tr64_i32(sink->z);
        int32_t strength = tr64_i16(sink->room_or_speed);
        int16_t ang = tr3_atan2(sx - core_lara->pos.x, sz - core_lara->pos.z);
        int32_t mag = strength * 1024;
        int32_t tvx = (int32_t)(((int64_t)tr3_sin(ang) * (int64_t)mag) >> 14);
        int32_t tvz = (int32_t)(((int64_t)tr3_cos(ang) * (int64_t)mag) >> 14);
        lara->current_vx = (int16_t)(lara->current_vx + ((tvx - lara->current_vx) >> 4));
        lara->current_vz = (int16_t)(lara->current_vz + ((tvz - lara->current_vz) >> 4));
        core_lara->pos.y += (sy - core_lara->pos.y) >> 4;
    } else {
        int32_t vx = lara->current_vx, vz = lara->current_vz;
        int32_t ax = abs32(vx), az = abs32(vz);
        if (ax < 4) vx = 0;
        else vx -= vx >> (ax > 16 ? 4 : (ax > 8 ? 3 : 2));
        if (az < 4) vz = 0;
        else vz -= vz >> (az > 16 ? 4 : (az > 8 ? 3 : 2));
        lara->current_vx = (int16_t)vx;
        lara->current_vz = (int16_t)vz;
    }
    core_lara->pos.x += lara->current_vx >> 8;
    core_lara->pos.z += lara->current_vz >> 8;
    lara->current_sink = 0;
}

static void lara_tr3_hazards_tick(tr3_state_t *st, Lara *core_lara, const tr3_floor_sample_t *fs)
{
    tr3_lara_t *lara = &st->lara;
    /* Poison pulse every 64 ticks when poison >= 16 (§2.6). */
    if (lara->poison >= 16 && (((st->ticks * 4u) & 255u) == 0u)) {
        if (lara->poison > 256) lara->poison = 256;
        core_lara->health = (int16)(core_lara->health - (lara->poison >> 4));
    }
    /* Cold water exposure in cold levels (§2.6). */
    if (st->script && st->script->cold) {
        if (fs->in_water) {
            int16_t drain = (core_lara->waterState == WATER_STATE_UNDER ||
                             core_lara->waterState == WATER_STATE_SURFACE) ? 2 : 1;
            lara->warmth = (int16_t)(lara->warmth - drain);
            if (lara->warmth < 0) {
                lara->warmth = -1;
                core_lara->health = (int16)(core_lara->health - 10);
            }
        } else if (lara->warmth < 600) {
            ++lara->warmth;
        }
    } else {
        lara->warmth = 600;
    }
    /* Death floor single-flame ignition, 7 HP/tick, quenched in water (§2.6). */
    if (fs->death_floor && !fs->in_water && core_lara->pos.y >= fs->floor_y - 16)
        lara->burning = 1;
    if (fs->in_water || core_lara->waterState == WATER_STATE_UNDER)
        lara->burning = 0;
    if (lara->burning && core_lara->health > 0)
        core_lara->health = (int16)(core_lara->health - 7);
}

/* =========================================================================
 * Lifecycle: init, tick, and shared Camera update
 * ========================================================================= */

void tr3_lara_init(tr3_state_t *st)
{
    const tr64_level_t *core = st->level;
    uint32_t rcount = tr64_level_table(core, TR64_CORE_rooms)->count;
    uint32_t mcount = tr64_level_table(core, TR64_CORE_models)->count;

    /* Allocate shared GameState, GameTR2, Room[], ItemObj[4], Model[16] in TR64_MEM_state. */
    gs = tr64_mem_alloc(TR64_MEM_state, (uint32_t)sizeof(GameState));
    memset(gs, 0, sizeof(*gs));
    gs->core = core;
    gs->tr2 = tr64_mem_alloc(TR64_MEM_state, (uint32_t)sizeof(GameTR2));
    memset(gs->tr2, 0, sizeof(*gs->tr2));
    gs->tr2->vehicle = -1;
    gs->gMatrixPtr = gs->gMatrixStack;
    matrixSetIdentity_c();

    gs->level.anims = tr64_level_table(core, TR64_CORE_animations)->count ? tr64_level_animations(core, 0) : NULL;
    gs->level.animStates = tr64_level_table(core, TR64_CORE_states)->count ? tr64_level_states(core, 0) : NULL;
    gs->level.animRanges = tr64_level_table(core, TR64_CORE_ranges)->count ? tr64_level_ranges(core, 0) : NULL;
    gs->level.floors = tr64_level_table(core, TR64_CORE_floor_data)->count ? tr64_level_floor_data(core, 0) : NULL;
    gs->level.animCommands = tr64_level_table(core, TR64_CORE_commands)->count ? tr64_level_commands(core, 0) : NULL;
    gs->level.animFrames = tr64_level_table(core, TR64_CORE_frames)->count ? tr64_level_frames(core, 0) : NULL;
    gs->level.nodes = tr64_level_table(core, TR64_CORE_bones)->count ? tr64_level_bones(core, 0) : NULL;
    gs->level.cameras = tr64_level_table(core, TR64_CORE_cameras)->count ? tr64_level_cameras(core, 0) : NULL;
    gs->level.soundsInfo = tr64_level_table(core, TR64_CORE_sound_details)->count ? tr64_level_sound_details(core, 0) : NULL;
    gs->level.soundMap = tr64_level_table(core, TR64_CORE_sound_map)->count ? tr64_level_sound_map(core, 0) : NULL;
    gs->level.boxes = tr64_level_table(core, TR64_CORE_boxes)->count ? tr64_level_boxes(core, 0) : NULL;
    gs->level.roomsCount = rcount;
    gs->level.boxesCount = tr64_level_table(core, TR64_CORE_boxes)->count;
    gs->level.camerasCount = tr64_level_table(core, TR64_CORE_cameras)->count;
    gs->level.framesCount = tr64_level_table(core, TR64_CORE_frames)->count;
    gs->tr2->animCount = tr64_level_table(core, TR64_CORE_animations)->count;

    /* Populate compact model table covering Lara & weapon animation banks (types 0..15). */
    enum { TR3_CORE_MODELS = 16 };
    gs->level.models = tr64_mem_alloc(TR64_MEM_state, TR3_CORE_MODELS * (uint32_t)sizeof(Model));
    memset(gs->level.models, 0, TR3_CORE_MODELS * sizeof(Model));
    for (uint32_t i = 0; i < mcount; ++i) {
        const tr64_core_models_t *cm = tr64_level_models(core, i);
        uint32_t ty = tr64_u32(cm->type);
        if (ty < TR3_CORE_MODELS) {
            gs->level.models[ty].type = (uint16)ty;
            gs->level.models[ty].count = (int8)tr64_u16(cm->mesh_count);
            gs->level.models[ty].start = tr64_u16(cm->mesh_start);
            gs->level.models[ty].nodeIndex = (uint16)(tr64_u32(cm->bone_offset) / 4u);
            gs->level.models[ty].animIndex = tr64_u16(cm->animation);
        }
    }

    gs->rooms = tr64_mem_alloc(TR64_MEM_state, (rcount ? rcount : 1u) * (uint32_t)sizeof(Room));
    memset(gs->rooms, 0, (rcount ? rcount : 1u) * sizeof(Room));
    for (uint32_t r = 0; r < rcount; ++r) {
        gs->rooms[r].info = tr64_level_rooms(core, r);
        gs->rooms[r].source = r;
    }

    gs->items = tr64_mem_alloc(TR64_MEM_state, 4u * (uint32_t)sizeof(ItemObj));
    memset(gs->items, 0, 4u * sizeof(ItemObj));
    gs->level.itemsCount = 1;
    Lara *core_lara = &gs->items[0];
    gs->players[0] = core_lara;
    core_lara->type = ITEM_LARA;
    core_lara->extraL = &gs->playersExtra[0];
    core_lara->visibleMask = 0xFFFFFFFFu;
    core_lara->flags = ITEM_FLAG_ACTIVE;
    core_lara->extraL->weapon = WEAPON_PISTOLS;
    core_lara->extraL->goalWeapon = WEAPON_PISTOLS;
    core_lara->extraL->weaponState = WEAPON_STATE_FREE;
    core_lara->extraL->hitQuadrant = -1;

    /* Initialize TR3 player state & retail PC India starting stock (§4.3). */
    tr3_lara_t *lara = &st->lara;
    memset(lara, 0, sizeof(*lara));
    lara->hp = 1000;
    lara->air = 1800;
    lara->stamina = 120;
    lara->warmth = 600;
    lara->poison = 0;
    lara->weapon = TR3_WP_PISTOLS;
    lara->owned_weapons = (uint16_t)(1u << TR3_WP_PISTOLS);
    lara->ammo[TR3_WP_PISTOLS] = -1;
    lara->small_medi = 1;
    lara->large_medi = 1;
    lara->flares = 2;
    lara->target_item = -1;
    lara->zipline_item = -1;

    const uint32_t icount = tr64_level_table(core, TR64_CORE_items)->count;
    for (uint32_t i = 0; i < icount; ++i) {
        const tr64_core_items_t *ci = tr64_level_items(core, i);
        if (tr64_u16(ci->type) == TR3_OBJ_LARA) {
            lara->x = tr64_i32(ci->x);
            lara->y = tr64_i32(ci->y);
            lara->z = tr64_i32(ci->z);
            lara->room = tr64_u16(ci->room);
            lara->rot_y = tr64_i16(ci->angle);
            break;
        }
    }
    lara->prev_x = lara->x;
    lara->prev_y = lara->y;
    lara->prev_z = lara->z;
    lara->move_angle = lara->rot_y;

    tr3_floor_sample_t fs;
    tr3_floor_eval(st, lara->x, lara->y, lara->z, lara->room, false, &fs);
    lara->room = fs.room;
    if (fs.in_water) {
        lara->water_status = TR3_WATER_UNDER;
        tr3_lara_set_anim(st, 108, TR3_ST_TREAD);
    } else {
        tr3_lara_set_anim(st, 103, TR3_ST_STOP);
    }

    Camera_init(&core_lara->extraL->camera, core_lara);
}

void tr3_lara_tick(tr3_state_t *st, uint32_t input)
{
    if (!st || !gs || !gs->players[0] || !gs->level.anims || !gs->level.models[ITEM_LARA].count)
        return;
    tr3_lara_t *lara = &st->lara;
    Lara *core_lara = gs->players[0];
    ExtraInfoLara *e = core_lara->extraL;

    lara->prev_x = lara->x;
    lara->prev_y = lara->y;
    lara->prev_z = lara->z;

    if ((input & TR3_IN_MEDIPACK) && !(st->last_input & TR3_IN_MEDIPACK)) {
        if (lara->small_medi > 0) tr3_lara_use_medipack(st, false);
        else if (lara->large_medi > 0) tr3_lara_use_medipack(st, true);
    }

    /* Pull any external mutations (e.g. entity damage, heal crystal, test overrides). */
    core_lara->health = lara->hp;
    core_lara->timer = lara->air;
    core_lara->pos = vec3i_create(lara->x, lara->y, lara->z);
    if (lara->room < gs->level.roomsCount && core_lara->room != gs->rooms + lara->room) {
        if (core_lara->room) Room_remove(core_lara->room, core_lara);
        Room_add(gs->rooms + lara->room, core_lara);
    }

    /* Weapons & flares, plus grounded Action object interaction. */
    lara_weapons_tick(st, input);
    if ((input & TR3_IN_ACTION) && !(st->last_input & TR3_IN_ACTION) &&
        !lara->guns_drawn && !(core_lara->flags & ITEM_FLAG_GRAVITY)) {
        tr3_entities_call(st, TR3_ENT_INTERACT, input, -1, 0);
        core_lara->health = lara->hp;
    }

    /* Decode inputs into shared Lara input bits + sequel roll/sidestep masks. */
    tr3_cur_input = input;
    uint32_t mask = input;
    if ((mask & (IN_LEFT | IN_RIGHT)) == (IN_LEFT | IN_RIGHT))
        mask &= ~(uint32_t)(IN_LEFT | IN_RIGHT);
    uint32_t steps = mask & (TR2_IN_STEP_LEFT | TR2_IN_STEP_RIGHT);
    uint32_t turn = mask & (IN_LEFT | IN_RIGHT);
    if (!steps && turn && (mask & IN_WALK) &&
        !(mask & (IN_UP | IN_DOWN | IN_JUMP | TR2_IN_ROLL))) {
        steps = (turn == IN_LEFT) ? TR2_IN_STEP_LEFT : TR2_IN_STEP_RIGHT;
        mask &= ~turn;
    }
    gs->tr2->input = input & TR2_INPUT_MASK;
    gs->tr2->sidestepInput = steps;
    e->lastInput = core_lara->input;
    core_lara->input = (uint16)(mask & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN |
                                        IN_JUMP | IN_WALK | IN_ACTION | IN_LOOK));

    /* Sample floor under Lara for climbable wall bit, hazards, and waterline. */
    tr3_floor_sample_t fs;
    tr3_floor_eval(st, core_lara->pos.x, core_lara->pos.y - 128, core_lara->pos.z,
                   (uint16_t)(core_lara->room - gs->rooms), false, &fs);
    int16_t card = wrap_i16((core_lara->angle.y + 8192) & 0xC000);
    uint8_t cbit = (card == 0) ? TR3_CLIMB_NORTH :
                   (card == ANGLE_90) ? TR3_CLIMB_EAST :
                   (card == ANGLE_180) ? TR3_CLIMB_SOUTH : TR3_CLIMB_WEST;
    gs->tr2->climbEnabled = (fs.climb_flags & cbit) != 0;

    /* Run shared TR1-TR3 Lara pipeline (matching game/game.c:1356-1442). */
    vec3i old_pos = core_lara->pos;
    Lara_updateLook(core_lara);
    if (core_lara->waterState == WATER_STATE_WADE)
        e->camera.targetAngle.x = TR2_CAMERA_WET_ELEVATION;
    game_tr2_water_status(core_lara);
    game_tr2_water_air(core_lara);
    lara_tr3_hazards_tick(st, core_lara, &fs);

    if (core_lara->waterState == WATER_STATE_UNDER || core_lara->waterState == WATER_STATE_SURFACE)
        lara_apply_water_current(st, core_lara);

    const unsigned movementWater = core_lara->waterState;
    const bool stroke = (movementWater == WATER_STATE_SURFACE || movementWater == WATER_STATE_UNDER);

    switch (core_lara->waterState) {
    case WATER_STATE_ABOVE:
    case WATER_STATE_WADE:
        Lara_updateAbove(core_lara);
        break;
    case WATER_STATE_SURFACE:
        e->camera.targetAngle.x = TR2_CAMERA_WET_ELEVATION;
        Lara_updateSurface(core_lara);
        break;
    case WATER_STATE_UNDER:
        Lara_updateUnder(core_lara);
        break;
    }

    ItemObj_animProcess(core_lara, !stroke);
    if (stroke)
        game_tr2_water_move(core_lara);
    game_lara_collision(core_lara);

    int32 offset = (movementWater == WATER_STATE_SURFACE) ? 100 :
                   (movementWater == WATER_STATE_UNDER) ? 0 :
                   is_crouch_or_crawl(core_lara->state) ? -200 : -LARA_HEIGHT / 2;
    ItemObj_updateRoom(core_lara, offset);

    /* Never allow Lara to end a dry tick inside a solid -127 wall tile. */
    const Sector *sec = Room_getSector(core_lara->room, core_lara->pos.x, core_lara->pos.z);
    if (sec->floor == -127 && core_lara->waterState != WATER_STATE_UNDER &&
        core_lara->waterState != WATER_STATE_SURFACE) {
        core_lara->pos = old_pos;
        ItemObj_updateRoom(core_lara, offset);
    }

    /* Sprint stamina (§2.2): max 120, -1/tick in Sprint (73), +1/tick otherwise. */
    if (core_lara->state == Lara_STATE_SPRINT) {
        if (lara->stamina > 0) --lara->stamina;
    } else if (lara->stamina < 120) {
        ++lara->stamina;
    }

    /* Death transition & stop flag. */
    if (core_lara->health <= 0) {
        core_lara->health = 0;
        if (core_lara->state != Lara_STATE_DEATH && core_lara->state != Lara_STATE_DEATH_UW) {
            core_lara->hSpeed = 0;
            lara->guns_drawn = 0;
            uint16_t base = lara_anim_base();
            if (core_lara->waterState == WATER_STATE_UNDER)
                ItemObj_animSet(core_lara, base + 124, true, 0);
            else if (is_crouch_or_crawl(core_lara->state))
                ItemObj_animSet(core_lara, base + 301, true, 0);
            else
                ItemObj_animSet(core_lara, base + 138, true, 0);
            st->stop.kind = TR64_GAME_STOP_DEATH;
            memcpy(st->stop.name, "lara died", 10);
        }
    }

    sync_shared_to_tr3(st);
    lara_update_braid(st);
    tr3_test_triggers(st, lara->room, lara->x, lara->y, lara->z, false, -1);

    ++gs->tick;
    ++gs->textureTicks;
    ++gs->gSaveGame.time;
}

/* Exact shared Q14 camera basis from game/view.c:276-283 (TR1-TR3 unified). */
static void tr3_camera_basis(int32_t basis[9], int32_t angleX, int32_t angleY)
{
    uint32_t tx = gSinCosTable[(uint16_t)angleX >> 4];
    uint32_t ty = gSinCosTable[(uint16_t)angleY >> 4];
    int32_t sx = wrap_i32(tx) >> 16, cx = (int16_t)tx;
    int32_t sy = wrap_i32(ty) >> 16, cy = (int16_t)ty;
    basis[0] = cy;             basis[1] = 0;   basis[2] = -sy;
    basis[3] = (sx * sy) >> 14; basis[4] = cx;  basis[5] = (sx * cy) >> 14;
    basis[6] = (cx * sy) >> 14; basis[7] = -sx; basis[8] = (cx * cy) >> 14;
}

void tr3_camera_update(tr3_state_t *st, uint32_t input)
{
    (void)input;
    if (!st || !gs || !gs->players[0]) return;
    if (!gs->level.anims || !gs->level.models[ITEM_LARA].count) {
        tr3_camera_basis(st->camera.basis, 0, 0);
        return;
    }
    tr3_camera_t *out = &st->camera;
    Lara *core_lara = gs->players[0];
    Camera *cam = &core_lara->extraL->camera;

    if (!out->initialized) {
        Camera_init(cam, core_lara);
        out->initialized = 1;
    }

    /* Fixed camera override from TR3 trigger action 1. */
    if (out->fixed_cam >= 0 && out->fixed_timer > 0) {
        --out->fixed_timer;
        const uint32_t ccount = gs->level.camerasCount;
        if ((uint32_t)out->fixed_cam < ccount) {
            const FixedCamera *fc = gs->level.cameras + out->fixed_cam;
            int16_t froom = tr64_i16(fc->room_or_speed);
            if (froom >= 0 && (uint32_t)froom < gs->level.roomsCount) {
                cam->mode = CAMERA_MODE_FIXED;
                cam->index = out->fixed_cam;
                cam->timer = out->fixed_timer;
            }
        }
        if (out->fixed_timer <= 0) {
            out->fixed_cam = -1;
            out->look_item = -1;
        }
    }

    Camera_update(cam);

    out->eye[0] = cam->view.pos.x;
    out->eye[1] = cam->displayY;
    out->eye[2] = cam->view.pos.z;
    out->target[0] = cam->target.pos.x;
    out->target[1] = cam->target.pos.y;
    out->target[2] = cam->target.pos.z;
    int16_t ax = cam->angle.x;
    int16_t ay = cam->angle.y;
    if (out->look_item >= 0 && (uint32_t)out->look_item < st->item_count && out->fixed_cam >= 0) {
        const tr3_item_t *t = &st->items[out->look_item];
        out->target[0] = t->x;
        out->target[1] = t->y - 256;
        out->target[2] = t->z;
        int32_t dx = out->target[0] - out->eye[0];
        int32_t dy = out->target[1] - out->eye[1];
        int32_t dz = out->target[2] - out->eye[2];
        ay = wrap_i16(game_tr2_bearing(dz, dx));
        ax = wrap_i16(game_tr2_bearing(game_tr2_horizontal_length(dx, dz), -dy));
    }
    out->room = (cam->view.room && cam->view.room >= gs->rooms)
                ? (uint16_t)(cam->view.room - gs->rooms) : st->lara.room;
    tr3_camera_basis(out->basis, ax, ay);
}
