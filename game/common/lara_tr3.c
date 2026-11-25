/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Cross-game TR2/TR3 Lara ability discovery and TR3 Sprint / Sprint-Roll /
 * Crouch / Crawl state handlers shared by TR1 and TR2 builds. */
#include "internal.h"

static inline int32 lara_abs32(int32 v) { return v < 0 ? -v : v; }
static inline int32 lara_clamp32(int32 v, int32 lo, int32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool has_state_change(uint32 animIdx, uint32 animCount, uint16 targetState,
                             uint16 *firstTargetAnim)
{
    if (animIdx >= animCount)
        return false;
    const Anim *a = gs->level.anims + animIdx;
    uint32 scOff = tr64_u16(a->state_offset);
    uint32 scCnt = tr64_u16(a->state_count);
    for (uint32 i = 0; i < scCnt; ++i) {
        const AnimState *st = gs->level.animStates + scOff + i;
        if (tr64_u16(st->state) == targetState && tr64_u16(st->range_count) > 0) {
            if (firstTargetAnim) {
                const AnimRange *r = gs->level.animRanges + tr64_u16(st->range_offset);
                *firstTargetAnim = tr64_u16(r->next_animation);
            }
            return true;
        }
    }
    return false;
}

void game_lara_abilities_init(void)
{
    if (!gs || !gs->level.anims)
        return;
    memset(&gs->extraAnims, 0, sizeof(gs->extraAnims));
    gs->extraInput[0] = gs->extraInput[1] = 0;
    gs->stamina[0] = gs->stamina[1] = 120;
    gs->forcedCrouch[0] = gs->forcedCrouch[1] = 0;

    uint32 animCount = tr64_level_table(gs->core, TR64_CORE_animations)->count;
    if (animCount < 160)
        return;

    if (gs->core->game == 2) {
        gs->extraAnims.wadeSwim   = Lara_ANIM_WADE_SWIM;
        gs->extraAnims.wade       = Lara_ANIM_WADE;
        gs->extraAnims.wadeStand  = Lara_ANIM_WADE_STAND;
        gs->extraAnims.wadeAscend = Lara_ANIM_WADE_ASCEND;
        gs->extraAnims.surfOut    = Lara_ANIM_SURF_OUT;
        gs->extraAnims.swimStand  = Lara_ANIM_SWIM_STAND;
        gs->extraAnims.surfStand  = Lara_ANIM_SURF_STAND;
        gs->extraAnims.rollUw     = Lara_ANIM_UW_ROLL;
        gs->extraAnims.hasTR2     = animCount >= 218;
    } else {
        /* Discover grafted TR2 clips appended past original TR1 animations. */
        uint16 wadeStand = 0;
        has_state_change(Lara_ANIM_STAND_NORMAL, animCount, Lara_STATE_WADE, &wadeStand);
        for (uint32 i = 160; i < animCount; ++i) {
            const Anim *a = gs->level.anims + i;
            uint16 st = tr64_u16(a->state);
            uint16 na = tr64_u16(a->next_animation);
            if (st == Lara_STATE_WADE && na == i && !gs->extraAnims.wade)
                gs->extraAnims.wade = (uint16)i;
            else if (st == Lara_STATE_ROLL_UW && na != Lara_ANIM_UNDERWATER && !gs->extraAnims.rollUw)
                gs->extraAnims.rollUw = (uint16)i;
        }
        if (gs->extraAnims.wade) {
            gs->extraAnims.wadeSwim   = gs->extraAnims.wade;
            gs->extraAnims.wadeStand  = wadeStand ? wadeStand : gs->extraAnims.wade;
            gs->extraAnims.wadeAscend = gs->extraAnims.wade;
            gs->extraAnims.surfOut    = Lara_ANIM_WATER_OUT;
            gs->extraAnims.swimStand  = Lara_ANIM_STAND_NORMAL;
            gs->extraAnims.surfStand  = Lara_ANIM_WATER_OUT;
        }
        gs->extraAnims.hasTR2 = (gs->extraAnims.wade != 0 && gs->extraAnims.rollUw != 0);
    }

    /* Discover grafted TR3 Sprint/Crouch/Crawl clips (valid on both TR1 and TR2). */
    uint16 crouchStart = 0;
    bool hasSprint = has_state_change(Lara_ANIM_RUN, animCount, Lara_STATE_SPRINT, NULL);
    bool hasCrouch = has_state_change(Lara_ANIM_STAND_NORMAL, animCount, Lara_STATE_CROUCH_IDLE, &crouchStart);
    uint32 scanFrom = gs->core->game == 2 ? 217u : 160u;
    for (uint32 i = scanFrom; i < animCount; ++i) {
        const Anim *a = gs->level.anims + i;
        uint16 st = tr64_u16(a->state);
        uint16 na = tr64_u16(a->next_animation);
        if (st == Lara_STATE_CRAWL_IDLE && na == i && !gs->extraAnims.crawlIdle)
            gs->extraAnims.crawlIdle = (uint16)i;
    }
    if (gs->extraAnims.crawlIdle) {
        for (uint32 i = (uint32)gs->extraAnims.crawlIdle + 1u; i < animCount; ++i) {
            const Anim *a = gs->level.anims + i;
            if (tr64_u16(a->state) == Lara_STATE_DEATH && tr64_u16(a->next_animation) == i) {
                gs->extraAnims.crawlDeath = (uint16)i;
                break;
            }
        }
    }
    gs->extraAnims.tr3Start = (hasSprint && hasCrouch && gs->extraAnims.crawlIdle) ? crouchStart : 0;
    gs->extraAnims.hasTR3 = (gs->extraAnims.tr3Start != 0);
}

void game_lara_abilities_refresh(void)
{
    if (!gs)
        return;
#if TR64_HAS_TR2
    if (gs->tr2)
        gs->tr2->climbEnabled = game_lara_abilities() >= TR64_LARA_ABILITIES_TR2;
#endif
    for (unsigned slot = 0; slot < TR64_GAME_PLAYERS; ++slot) {
        Lara *lara = gs->players[slot];
        if (!lara || !lara->extraL)
            continue;
        if (!game_lara_has_tr3() && lara->state >= Lara_STATE_CROUCH_IDLE) {
            ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
            lara->hSpeed = 0;
            lara->vSpeed = 0;
            gs->forcedCrouch[slot] = 0;
            Lara_setWeaponState(lara, WEAPON_STATE_FREE);
        } else if (!game_lara_has_tr2() &&
                   (lara->state == Lara_STATE_WADE || lara->state == Lara_STATE_ROLL_AIR ||
                    lara->state == Lara_STATE_ROLL_UW)) {
            if (lara->waterState == WATER_STATE_UNDER)
                ItemObj_animSet(lara, Lara_ANIM_UNDERWATER, true, 0);
            else
                ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
        }
    }
}

void game_lara_tr3_tick(Lara *self)
{
    if (!gs || !self || !game_lara_has_tr3())
        return;
    unsigned slot = Lara_slot(self);
    if (self->state == Lara_STATE_SPRINT) {
        if (gs->stamina[slot] > 0)
            --gs->stamina[slot];
    } else if (gs->stamina[slot] < 120) {
        ++gs->stamina[slot];
    }
    /* TR3 +10/tick air recovery above deep water. */
    if (self->waterState != WATER_STATE_UNDER && self->timer < 1800) {
        self->timer = (int16)(self->timer + 10);
        if (self->timer > 1800)
            self->timer = 1800;
    }
}

bool game_lara_tr3_can_sprint(const Lara *self)
{
    if (!gs || !self || !game_lara_has_tr3())
        return false;
    if (self->waterState == WATER_STATE_WADE)
        return false;
    unsigned slot = Lara_slot(self);
    if (!(gs->extraInput[slot] & TR64_GAME_EXTRA_SPRINT))
        return false;
    return gs->stamina[slot] >= 120;
}

bool game_lara_tr3_can_crouch(const Lara *self)
{
    if (!gs || !self || !game_lara_has_tr3())
        return false;
    if (self->waterState == WATER_STATE_WADE)
        return false;
    unsigned slot = Lara_slot(self);
    if (!(gs->extraInput[slot] & TR64_GAME_EXTRA_CROUCH) && !gs->forcedCrouch[slot])
        return false;
    if (self->extraL && self->extraL->weaponState == WEAPON_STATE_READY &&
        self->extraL->weapon >= WEAPON_SHOTGUN)
        return false;
    return true;
}

static void update_forced_crouch(Lara *self)
{
    unsigned slot = Lara_slot(self);
    Room *r = self->room;
    const Sector *s = Room_getSector(r, self->pos.x, self->pos.z);
    if (!s) {
        gs->forcedCrouch[slot] = 0;
        return;
    }
    int32 ceil = Sector_getCeiling(s, self->pos.x, i32_sub(self->pos.y, 400), self->pos.z);
    gs->forcedCrouch[slot] = (ceil != WALL && i32_sub(ceil, self->pos.y) > -762) ? 1u : 0u;
}

static bool crawl_probe_ok(const Lara *self, int16 yaw, int32 dist, bool back_check)
{
    int32 sn, cs;
    sincos(yaw, sn, cs);
    int32 nx = i32_add(self->pos.x, (sn * dist) >> 14);
    int32 nz = i32_add(self->pos.z, (cs * dist) >> 14);
    Room *r = self->room;
    const Sector *s = Room_getSector(r, nx, nz);
    if (!s)
        return false;
    const FloorData *savedFd = gs->gLastFloorData;
    uint16 savedSlant = gs->gLastFloorSlant;
    int32 fl = Sector_getFloor(s, nx, i32_sub(self->pos.y, 128), nz);
    uint16 slant = gs->gLastFloorSlant;
    int32 cl = Sector_getCeiling(s, nx, i32_sub(self->pos.y, 128), nz);
    gs->gLastFloorData = savedFd;
    gs->gLastFloorSlant = savedSlant;

    if (fl == WALL)
        return false;
    if (back_check && (cl == WALL || cl > i32_add(self->pos.y, 128)))
        return false;
    if (cl != WALL && i32_sub(fl, cl) < 400)
        return false;
    if (lara_abs32(i32_sub(fl, self->pos.y)) > 254)
        return false;
    int32 sx = (int8)(slant & 0xff), sz = (int8)(slant >> 8);
    if (lara_abs32(sx) > 2 || lara_abs32(sz) > 2)
        return false;
    return true;
}

void game_lara_tr3_s_state(Lara *self)
{
    if (!gs || !self)
        return;
    unsigned slot = Lara_slot(self);
    uint32 extra = gs->extraInput[slot];

    switch (self->state) {
    case Lara_STATE_SPRINT:
        if (self->health <= 0) {
            self->goalState = Lara_STATE_RUN;
            return;
        }
        if (self->input & IN_LEFT) {
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed - LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
            self->angle.z = (int16)lara_clamp32(self->angle.z - 273, -2912, 2912);
        } else if (self->input & IN_RIGHT) {
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed + LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
            self->angle.z = (int16)lara_clamp32(self->angle.z + 273, -2912, 2912);
        }
        if (extra & TR64_GAME_EXTRA_CROUCH) {
            self->goalState = Lara_STATE_CROUCH_IDLE;
        } else if (self->input & IN_JUMP) {
            self->goalState = Lara_STATE_SPRINT_ROLL;
        } else if (!(self->input & IN_UP) || (self->input & IN_WALK) ||
                   !(extra & TR64_GAME_EXTRA_SPRINT) || gs->stamina[slot] <= 0) {
            self->goalState = (self->input & IN_UP) ? Lara_STATE_RUN : Lara_STATE_STOP;
        } else {
            self->goalState = Lara_STATE_SPRINT;
        }
        break;

    case Lara_STATE_SPRINT_ROLL:
        if (self->vSpeed > 131 && self->goalState != Lara_STATE_DEATH &&
            self->goalState != Lara_STATE_STOP && self->goalState != Lara_STATE_RUN)
            self->goalState = Lara_STATE_FALL;
        else if ((self->input & IN_UP) && !(self->input & IN_WALK) && self->waterState != WATER_STATE_WADE)
            self->goalState = Lara_STATE_RUN;
        else
            self->goalState = Lara_STATE_STOP;
        break;

    case Lara_STATE_CROUCH_IDLE:
        update_forced_crouch(self);
        self->extraL->camera.targetAngle.x = ANGLE(-10);
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
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed - LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed + LARA_TURN_ACCEL, -ANGLE(4), ANGLE(4));
        if ((self->input & (IN_UP | IN_DOWN)) && self->extraL->weaponState == WEAPON_STATE_FREE &&
            ((extra & TR64_GAME_EXTRA_CROUCH) || gs->forcedCrouch[slot])) {
            self->extraL->torso.angle = vec3s_create(0, 0, 0);
            self->goalState = Lara_STATE_CRAWL_IDLE;
        } else if (!(extra & TR64_GAME_EXTRA_CROUCH) && !gs->forcedCrouch[slot]) {
            self->goalState = Lara_STATE_STOP;
        } else {
            self->goalState = Lara_STATE_CROUCH_IDLE;
        }
        break;

    case Lara_STATE_CRAWL_IDLE:
        update_forced_crouch(self);
        self->extraL->camera.targetAngle.x = ANGLE(-10);
        if (self->health <= 0) {
            uint16 deathAnim = gs->extraAnims.crawlDeath ? gs->extraAnims.crawlDeath : Lara_ANIM_DEATH_SPIKES;
            ItemObj_animSet(self, deathAnim, true, 0);
            self->state = self->goalState = Lara_STATE_DEATH;
            return;
        }
        Lara_setWeaponState(self, WEAPON_STATE_BUSY);
        if ((!(extra & TR64_GAME_EXTRA_CROUCH) && !gs->forcedCrouch[slot]) || (self->input & IN_WEAPON)) {
            self->goalState = Lara_STATE_CROUCH_IDLE;
            Lara_setWeaponState(self, WEAPON_STATE_FREE);
            return;
        }
        if ((self->input & IN_UP) && crawl_probe_ok(self, self->angle.y, 256, false)) {
            self->goalState = Lara_STATE_CRAWL_FORWARD;
        } else if ((self->input & IN_DOWN) &&
                   crawl_probe_ok(self, wrap_i16((uint16)self->angle.y + 32768u), 300, true)) {
            self->goalState = Lara_STATE_CRAWL_BACK;
        } else if (self->input & IN_LEFT) {
            self->goalState = Lara_STATE_CRAWL_TURN_L;
        } else if (self->input & IN_RIGHT) {
            self->goalState = Lara_STATE_CRAWL_TURN_R;
        } else {
            self->goalState = Lara_STATE_CRAWL_IDLE;
        }
        break;

    case Lara_STATE_CRAWL_FORWARD:
        update_forced_crouch(self);
        self->extraL->camera.targetAngle.x = ANGLE(-10);
        if (self->health <= 0 || !(self->input & IN_UP) ||
            (!(extra & TR64_GAME_EXTRA_CROUCH) && !gs->forcedCrouch[slot])) {
            self->goalState = Lara_STATE_CRAWL_IDLE;
            return;
        }
        if (self->input & IN_LEFT)
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed - 409, -546, 546);
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed + 409, -546, 546);
        self->goalState = Lara_STATE_CRAWL_FORWARD;
        break;

    case Lara_STATE_CRAWL_BACK:
        update_forced_crouch(self);
        self->extraL->camera.targetAngle.x = ANGLE(-10);
        if (self->health <= 0 || !(self->input & IN_DOWN) ||
            (!(extra & TR64_GAME_EXTRA_CROUCH) && !gs->forcedCrouch[slot])) {
            self->goalState = Lara_STATE_CRAWL_IDLE;
            return;
        }
        if (self->input & IN_LEFT)
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed + 409, -546, 546);
        else if (self->input & IN_RIGHT)
            self->turnSpeed = (int16)lara_clamp32(self->turnSpeed - 409, -546, 546);
        self->goalState = Lara_STATE_CRAWL_BACK;
        break;

    case Lara_STATE_CRAWL_TURN_L:
        self->extraL->camera.targetAngle.x = ANGLE(-10);
        self->angle.y = wrap_i16((int32)self->angle.y - 273);
        self->goalState = (self->input & IN_LEFT) ? Lara_STATE_CRAWL_TURN_L : Lara_STATE_CRAWL_IDLE;
        break;

    case Lara_STATE_CRAWL_TURN_R:
        self->extraL->camera.targetAngle.x = ANGLE(-10);
        self->angle.y = wrap_i16((int32)self->angle.y + 273);
        self->goalState = (self->input & IN_RIGHT) ? Lara_STATE_CRAWL_TURN_R : Lara_STATE_CRAWL_IDLE;
        break;

    default:
        break;
    }
}

void game_lara_tr3_c_state(Lara *self)
{
    if (!gs || !self)
        return;

    switch (self->state) {
    case Lara_STATE_SPRINT:
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

    case Lara_STATE_SPRINT_ROLL:
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
            if ((self->state == Lara_STATE_CRAWL_FORWARD || self->state == Lara_STATE_CRAWL_BACK) &&
                gs->extraAnims.crawlIdle)
                ItemObj_animSet(self, gs->extraAnims.crawlIdle, true, 0);
        } else {
            Lara_c_applyOffset(self);
            if (gs->cinfo.type == CT_LEFT)
                self->angle.y = wrap_i16((int32)self->angle.y + 364);
            else if (gs->cinfo.type == CT_RIGHT)
                self->angle.y = wrap_i16((int32)self->angle.y - 364);
            if (gs->cinfo.m.floor != WALL && lara_abs32(gs->cinfo.m.floor) <= 255)
                self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor);
        }
        break;
    }

    default:
        break;
    }
}
