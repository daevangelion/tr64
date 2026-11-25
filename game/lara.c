/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/lara.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

bool Lara_alignAngle(Lara *self, int16 *angle, int16 threshold)
{
    (void)self;
    if ((*angle) >= -threshold && (*angle) <= threshold)
    {
        (*angle) = 0;
    }
    else if ((*angle) >= ((ANGLE_90)-threshold) && (*angle) <= ((ANGLE_90) + threshold))
    {
        (*angle) = (ANGLE_90);
    }
    else if ((*angle) >= ((-(ANGLE_90)) - threshold) && (*angle) <= ((-(ANGLE_90)) + threshold))
    {
        (*angle) = -(ANGLE_90);
    }
    else if ((*angle) >= -((-32767 + threshold)) || (*angle) <= ((-32767 + threshold)))
    {
        (*angle) = ANGLE_180;
    }
    return ((*angle) & (ANGLE_90 - 1)) > 0;
}

void Lara_alignWall(Lara *self, int32 radius)
{
    int x = self->pos.x & ~1023;
    int z = self->pos.z & ~1023;
    switch (self->angle.y)
    {
    case 0:
        self->pos.z = i32_sub(i32_add(z, 1024), radius);
        break;
    case (ANGLE_90):
        self->pos.x = i32_sub(i32_add(x, 1024), radius);
        break;
    case -(ANGLE_90):
        self->pos.x = i32_add(x, radius);
        break;
    case ANGLE_180:
        self->pos.z = i32_add(z, radius);
        break;
    default:
        ASSERT(false);
        ;
    }
}

void Lara_c_STATE_BACK(Lara *self)
{
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    Lara_c_angle(self, ANGLE_180);
    gs->cinfo.gapPos = (self->waterState == WATER_STATE_WADE) ? -(WALL) : LARA_STEP_HEIGHT;
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    if (Lara_c_checkCeiling(self))
        return;
    if (Lara_c_checkWall(self))
    {
        ItemObj_animSet(self, Lara_ANIM_STAND, 1, 0);
    }
    if (gs->cinfo.m.floor > 128 && gs->cinfo.m.floor < LARA_STEP_HEIGHT)
    {
        bool right = self->frameIndex >= 568;
#if TR64_HAS_SEQUEL
        if (TR64_RULE(WATER))
            right = self->frameIndex >= 964 && self->frameIndex <= 993;
#endif
        if (!right)
        {
            ItemObj_animSet(self, Lara_ANIM_BACK_DESCEND_LEFT, 0, 0);
        }
        else
        {
            ItemObj_animSet(self, Lara_ANIM_BACK_DESCEND_RIGHT, 0, 0);
        }
    }
    if (Lara_c_checkSlide(self))
        return;
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_STATE_BACK_FAST(Lara *self)
{
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    Lara_c_angle(self, ANGLE_180);
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    if (Lara_c_checkCeiling(self))
        return;
    if (Lara_c_checkFall(self, 200, Lara_ANIM_FALL_BACK))
        return;
    if (Lara_c_checkWall(self))
    {
        ItemObj_animSet(self, Lara_ANIM_STAND, 0, 0);
    }
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_STATE_BLOCK_PULL(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_BLOCK_PUSH(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_BLOCK_READY(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

#if TR64_HAS_SEQUEL
/* Authored WALL.TR2 ladder clip identifiers (tr2-climbing.md). */
enum {
    LADDER_MOUNT=160, LADDER_ASCEND=161, LADDER_DESCEND=168,
    LADDER_PULL_UP=174, LADDER_STEP_OFF=102
};
static void tr2LadderStance(Lara *self);
static void tr2LadderVertical(Lara *self,bool up);
static void tr2LadderSide(Lara *self,int direction);
static void tr2AttachedFloor(const Lara *self, int32 above);
static bool tr2LadderReleased(Lara *self);
static bool tr2GroundClimb(Lara *self);
static bool tr2AirCatch(Lara *self);
static void tr2HangPullUp(Lara *self);
#endif

void Lara_c_STATE_CLIMB_DOWN(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        tr2LadderVertical(self,false);
#endif
    (void)self;
}

void Lara_c_STATE_CLIMB_END(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        (void)tr2LadderReleased(self);
#endif
    (void)self;
}

void Lara_c_STATE_CLIMB_LEFT(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        tr2LadderSide(self,-1);
#endif
    (void)self;
}

void Lara_c_STATE_CLIMB_RIGHT(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        tr2LadderSide(self,1);
#endif
    (void)self;
}

void Lara_c_STATE_CLIMB_START(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        tr2LadderStance(self);
#endif
    (void)self;
}

void Lara_c_STATE_CLIMB_UP(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        tr2LadderVertical(self,true);
#endif
    (void)self;
}

void Lara_c_STATE_COMPRESS(Lara *self)
{
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = (WALL);
    gs->cinfo.gapCeiling = 0;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    if (gs->cinfo.m.ceiling > -100)
    {
        ItemObj_animSet(self, Lara_ANIM_STAND, 1, 0);
        (self->pos = gs->cinfo.pos);
        self->hSpeed = 0;
        self->vSpeed = 0;
    }
}

void Lara_c_STATE_DEATH(Lara *self)
{
    gs->cinfo.radius = 400;
    Lara_c_angle(self, 0);
    Lara_c_default(self);
    Lara_c_applyOffset(self);
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
#if TR64_HAS_SEQUEL
    /* Great Wall Objects, Q-HAZ-04: the death pose, not the loss of
     * health itself, sets air on this update. */
    if (TR64_RULE(WALL_HAZARDS))
        self->health = self->timer = -1;
#endif
}

void Lara_c_STATE_DEATH_MIDAS(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_DEATH_UW(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WALL_HAZARDS)) {
        self->health = self->timer = -1; /* Q-HAZ-04, underwater death. */
        self->extraL->weaponState = WEAPON_STATE_BUSY;
    } else
#endif
    {
        self->health = 0;
        self->timer = 0;
    }
    int16 waterLevel = ItemObj_getWaterLevel(self);
    if (waterLevel != (WALL) && waterLevel < i32_sub(self->pos.y, 100))
    {
        (self->pos.y = i32_sub(self->pos.y, 5));
    }
    Lara_c_swim(self);
}

void Lara_c_STATE_FALL(Lara *self)
{
    self->flags |= ITEM_FLAG_GRAVITY;
    Lara_c_jump(self);
}

void Lara_c_STATE_FALL_BACK(Lara *self)
{
    Lara_c_angle(self, ANGLE_180);
    Lara_c_jump(self);
}

void Lara_c_STATE_FAST_DIVE(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_jump(self);
}

void Lara_c_STATE_HANDSTAND(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_HANG(Lara *self)
{
    Lara_c_hang(self, 0);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        return;
#endif
    if ((self->input & IN_UP) && self->goalState == Lara_STATE_HANG)
    {
        if (abs(gs->cinfo.f.floor) >= 850)
            return;
        if (Lara_c_checkSpace(self) || gs->cinfo.staticHit)
            return;
        if (self->input & IN_WALK)
        {
            self->goalState = Lara_STATE_HANDSTAND;
        }
        else
        {
            self->goalState = Lara_STATE_HANG_UP;
        }
    }
}

void Lara_c_STATE_HANG_LEFT(Lara *self)
{
    Lara_c_hang(self, -(ANGLE_90));
}

void Lara_c_STATE_HANG_RIGHT(Lara *self)
{
    Lara_c_hang(self, (ANGLE_90));
}

void Lara_c_STATE_HANG_UP(Lara *self)
{
    Lara_c_angle(self, 0);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL) &&
        (self->animIndex==LADDER_PULL_UP || self->animIndex==LADDER_STEP_OFF)) {
        /* Top-out translation is authored; keep floor evaluation live. */
        tr2AttachedFloor(self,LARA_HEIGHT+160);
        return;
    }
    /* Q-HAZ-05: a zip mount is this state. After the pose advances, and
     * on the tick attachment installs it, the ordinary middle sample is
     * 922 above the origin. The advance into state 70 leaves this handler. */
#endif
    Lara_c_default(self);
}

void Lara_c_STATE_JUMP(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_jump(self);
}

void Lara_c_STATE_JUMP_BACK(Lara *self)
{
    Lara_c_angle(self, ANGLE_180);
    Lara_c_jump(self);
}

void Lara_c_STATE_JUMP_LEFT(Lara *self)
{
    Lara_c_angle(self, -(ANGLE_90));
    Lara_c_jump(self);
}

void Lara_c_STATE_JUMP_RIGHT(Lara *self)
{
    Lara_c_angle(self, (ANGLE_90));
    Lara_c_jump(self);
}

void Lara_c_STATE_JUMP_UP(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_jump(self);
}

void Lara_c_STATE_LAND(Lara *self)
{
    Lara_c_STATE_STOP(self);
}

void Lara_c_STATE_PICKUP(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_PICKUP_FLARE(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_POSE(Lara *self)
{
    Lara_c_STATE_STOP(self);
}

void Lara_c_STATE_REACH(Lara *self)
{
    self->flags |= ITEM_FLAG_GRAVITY;
    Lara_c_angle(self, 0);
    Lara_c_jump(self);
}

void Lara_c_STATE_ROLL_AIR(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_ROLL_END(Lara *self)
{
    Lara_c_angle(self, ANGLE_180);
    Lara_c_roll(self);
}

void Lara_c_STATE_ROLL_START(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_roll(self);
}

void Lara_c_STATE_ROLL_UW(Lara *self)
{
#if TR64_HAS_TR3
    if (TR64_RULE(WATER)) {
        Lara_c_swim(self);
        return;
    }
#else
    if (game_lara_has_tr2()) {
        Lara_c_swim(self);
        return;
    }
#endif
    (void)self;
}

#if TR64_HAS_SEQUEL
/* tr2-lara-ground-and-weapons, blocked movement from a standing pose.
 * The first attempted step has already consumed its collision offset. */
static void tr2RecoverGroundStop(Lara *self)
{
    unsigned start=gs->tr2->motionStart.state;
    switch (start) {
    case Lara_STATE_STOP: case Lara_STATE_TURN_LEFT: case Lara_STATE_TURN_RIGHT:
    case Lara_STATE_TURN_FAST:
        self->state=start;
        self->animIndex=gs->tr2->motionStart.anim;
        self->frameIndex=gs->tr2->motionStart.frame;
        self->goalState=(self->input&IN_LEFT) ? Lara_STATE_TURN_LEFT
                      : (self->input&IN_RIGHT) ? Lara_STATE_TURN_RIGHT : Lara_STATE_STOP;
        ItemObj_animProcess(self,true);
        break;
    default:
        ItemObj_animSet(self,Lara_ANIM_STAND,self->state!=Lara_STATE_SPLAT,0);
        break;
    }
}

/* Running frontal stop: sample a single point on the nearest cardinal
 * axis from the corrected origin, resolving the origin at the test Y. */
static bool tr2RunSplat(Lara *self)
{
    if (self->animIndex==6 || self->frameIndex>=22)
        return false;
    int32 y=i32_sub(self->pos.y,640);
    Room *room=self->room;
    game_tr2_room_sector(&room,self->pos.x,y,self->pos.z);
    int16 axis=wrap_i16((self->angle.y+8192)&0xc000);
    vec3i ahead=game_tr2_heading_step(axis,256);
    int32 x=i32_add(self->pos.x,ahead.x),z=i32_add(self->pos.z,ahead.z);
    const Sector *s=game_tr2_room_sector(&room,x,y,z);
    int32 floor=Sector_getFloor(s,x,y,z),ceil=Sector_getCeiling(s,x,y,z);
    if (floor!=WALL && y<floor && y>ceil)
        return false;
    ItemObj_animSet(self,self->frameIndex<10 ? Lara_ANIM_SMASH_RUN_LEFT
                                           : Lara_ANIM_SMASH_RUN_RIGHT,false,0);
    self->state=Lara_STATE_SPLAT;
    return true;
}
#endif

void Lara_c_STATE_RUN(Lara *self)
{
    Lara_c_angle(self, 0);
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    if (Lara_c_checkCeiling(self))
        return;
    if (Lara_c_checkClimbUp(self))
        return;
    if (Lara_c_checkWall(self))
    {
        self->angle.z = 0;
#if TR64_HAS_SEQUEL
        if (TR64_RULE(BLOCKED_MOTION)) {
            if (tr2RunSplat(self))
                return;
            tr2RecoverGroundStop(self);
        } else
#endif
        {
            if (gs->cinfo.f.slantType == SLANT_NONE && gs->cinfo.f.floor < -LARA_SMASH_HEIGHT &&
                self->frameIndex < 22)
            {
                ItemObj_animSet(
                    self, self->frameIndex < 10 ? Lara_ANIM_SMASH_RUN_LEFT : Lara_ANIM_SMASH_RUN_RIGHT,
                    0, 0);
                self->state = Lara_STATE_SPLAT;
                return;
            }
            ItemObj_animSet(self, Lara_ANIM_STAND, 1, 0);
        }
    }
    if (Lara_c_checkFall(self, LARA_STEP_HEIGHT, Lara_ANIM_FALL_FORTH))
        return;
    if (gs->cinfo.m.floor >= -LARA_STEP_HEIGHT && gs->cinfo.m.floor < -128)
    {
#if TR64_HAS_SEQUEL
        if (TR64_RULE(RUN_STEP_RISE) && gs->cinfo.f.floor <= -LARA_STEP_HEIGHT)
            return;
#endif
        if (self->frameIndex >= 3 && self->frameIndex <= 14)
        {
            ItemObj_animSet(self, Lara_ANIM_RUN_ASCEND_RIGHT, 0, 0);
        }
        else
        {
            ItemObj_animSet(self, Lara_ANIM_RUN_ASCEND_LEFT, 0, 0);
        }
    }
    if (Lara_c_checkSlide(self))
        return;
    if (gs->cinfo.m.floor >= 50)
    {
        (self->pos.y = i32_add(self->pos.y, 50));
        return;
    }
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_STATE_SLIDE(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_slide(self);
}

void Lara_c_STATE_SLIDE_BACK(Lara *self)
{
    Lara_c_angle(self, ANGLE_180);
    Lara_c_slide(self);
}

void Lara_c_STATE_SPECIAL(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_SPLAT(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
    Lara_c_applyOffset(self);
}

void Lara_c_STATE_STEP_LEFT(Lara *self)
{
    Lara_c_angle(self, -(ANGLE_90));
    Lara_c_step(self);
}

void Lara_c_STATE_STEP_RIGHT(Lara *self)
{
    Lara_c_angle(self, +(ANGLE_90));
    Lara_c_step(self);
}

void Lara_c_STATE_STOP(Lara *self)
{
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    Lara_c_angle(self, 0);
    if (!TR64_RULE(BLOCKED_MOTION) && self->input==(IN_UP|IN_ACTION))
    {
        int32 s;
        int32 c;
        sincos(gs->cinfo.angle, s, c);
        (self->pos.x = i32_add(self->pos.x, (i32_mul(s, 4)) >> 14));
        (self->pos.z = i32_add(self->pos.z, (i32_mul(c, 4)) >> 14));
    }
    Lara_c_default(self);
#if TR64_HAS_SEQUEL
    /* Ground, slopes and airtime: ordinary Stand restores on enclosure. */
    if (TR64_RULE(STAND_CEILING) && Lara_c_checkCeiling(self))
        return;
#endif
    if (Lara_c_checkClimbUp(self))
        return;
    int32 drop = 100;
#if TR64_HAS_SEQUEL
    /* tr2-lara-start.md, Standing to fall: the dry threshold is 384. */
    if (TR64_RULE(STAND_DROP))
        drop = LARA_STEP_HEIGHT;
#endif
    if (Lara_c_checkFall(self, drop, Lara_ANIM_FALL_FORTH))
        return;
    if (Lara_c_checkSlide(self))
        return;
    Lara_c_applyOffset(self);
#if TR64_HAS_TR3
    if (game_tr3_swamp_sink(self))
        return;
#endif
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_STATE_SURF_BACK(Lara *self)
{
    Lara_c_angle(self, ANGLE_180);
    Lara_c_surf(self);
}

void Lara_c_STATE_SURF_LEFT(Lara *self)
{
    Lara_c_angle(self, -(ANGLE_90));
    Lara_c_surf(self);
}

void Lara_c_STATE_SURF_RIGHT(Lara *self)
{
    Lara_c_angle(self, (ANGLE_90));
    Lara_c_surf(self);
}

void Lara_c_STATE_SURF_SWIM(Lara *self)
{
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    Lara_c_angle(self, 0);
    Lara_c_surf(self);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WATER))
        return;
#endif
    Lara_c_checkWaterOut(self);
}

void Lara_c_STATE_SURF_TREAD(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_surf(self);
}

void Lara_c_STATE_SWAN_DIVE(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_jump(self);
}

void Lara_c_STATE_SWITCH_DOWN(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_SWITCH_UP(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_TURN_FAST(Lara *self)
{
    Lara_c_STATE_STOP(self);
}

void Lara_c_STATE_TURN_LEFT(Lara *self)
{
    Lara_c_STATE_TURN_RIGHT(self);
}

void Lara_c_STATE_TURN_RIGHT(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
    if (Lara_c_checkFall(self, 100, Lara_ANIM_FALL_FORTH))
        return;
    if (Lara_c_checkSlide(self))
        return;
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_STATE_UNUSED_1(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_UNUSED_2(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_UNUSED_3(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_UNUSED_4(Lara *self)
{
    (void)self;
}

void Lara_c_STATE_USE_KEY(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_USE_MIDAS(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_USE_PUZZLE(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_UW_DIVE(Lara *self)
{
    Lara_c_swim(self);
}

void Lara_c_STATE_UW_GLIDE(Lara *self)
{
    Lara_c_swim(self);
}

void Lara_c_STATE_UW_SWIM(Lara *self)
{
    Lara_c_swim(self);
}

void Lara_c_STATE_UW_TREAD(Lara *self)
{
    Lara_c_swim(self);
}

void Lara_c_STATE_WADE(Lara *self)
{
#if TR64_HAS_TR3
    if (TR64_RULE(WATER)) {
#else
    if (game_lara_has_tr2()) {
#endif
        /* Water and flares, Wade on the floor: reject front rises
         * over 384, and follow a drop by at most 50 in this update. */
        Lara_c_angle(self, 0);
        gs->cinfo.gapPos = -WALL;
        gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
        gs->cinfo.gapCeiling = 0;
        gs->cinfo.stopOnSlant = 1;
#if TR64_HAS_SEQUEL
        gs->cinfo.allowSteepDrop = true;
#endif
        ItemObj_collideRoom(self, LARA_HEIGHT, 0);
#if TR64_HAS_SEQUEL
        gs->cinfo.allowSteepDrop = false;
#endif
        if (Lara_c_checkCeiling(self) || Lara_c_checkClimbUp(self))
            return;
        if (Lara_c_checkWall(self)) {
            self->angle.z = 0;
            /* Q-T2W-05: a high flat Wade impact requests state 12 even
             * when the absolute frame has no authored splat clip. */
            if (gs->cinfo.f.slantType == SLANT_NONE && gs->cinfo.f.floor < -640) {
                self->state=Lara_STATE_SPLAT;
                if (self->frameIndex<22) {
                    ItemObj_animSet(self,self->frameIndex<10 ? Lara_ANIM_SMASH_RUN_LEFT
                                                           : Lara_ANIM_SMASH_RUN_RIGHT,false,0);
                    return;
                }
            }
#if TR64_HAS_SEQUEL
            if (gs->tr2)
                tr2RecoverGroundStop(self);
            else
#endif
                ItemObj_animSet(self, Lara_ANIM_STAND, true, 0);
        }
        if (gs->cinfo.m.floor >= -384 && gs->cinfo.m.floor < -128)
            ItemObj_animSet(self,self->frameIndex>=3 && self->frameIndex<=14
                                 ? Lara_ANIM_RUN_ASCEND_RIGHT : Lara_ANIM_RUN_ASCEND_LEFT,false,0);
        if (Lara_c_checkSlide(self))
            return;
#if TR64_HAS_TR3
        if (game_tr3_swamp_sink(self))
            return;
#endif
        self->pos.y = i32_add(self->pos.y, X_MIN(gs->cinfo.m.floor, 50));
        return;
    }
    (void)self;
}

void Lara_c_STATE_WALK(Lara *self)
{
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    gs->cinfo.stopOnLava = 1;
    Lara_c_angle(self, 0);
    Lara_c_default(self);
    if (Lara_c_checkCeiling(self))
        return;
    if (Lara_c_checkClimbUp(self))
        return;
    if (Lara_c_checkWall(self))
    {
        const bool tr2Stop = TR64_RULE(INTERACTIONS);
        if (self->frameIndex >= 29 && self->frameIndex <= 47)
        {
            ItemObj_animSet(self, Lara_ANIM_STAND_RIGHT, tr2Stop, 0);
        }
        else if ((self->frameIndex >= 22 && self->frameIndex <= 28) ||
                 (self->frameIndex >= 48 && self->frameIndex <= 57))
        {
            ItemObj_animSet(self, Lara_ANIM_STAND_LEFT, tr2Stop, 0);
        }
        else
        {
#if TR64_HAS_SEQUEL
            if (tr2Stop)
            {
                tr2RecoverGroundStop(self);
                return;
            }
#endif
            ItemObj_animSet(self, Lara_ANIM_STAND, 0, 0);
        }
        if (tr2Stop)
        {
            self->state = self->goalState = Lara_STATE_STOP;
            return;
        }
    }
    if (Lara_c_checkFall(self, LARA_STEP_HEIGHT, Lara_ANIM_FALL_FORTH))
        return;
    if (gs->cinfo.m.floor > 128)
    {
        if (self->frameIndex >= 28 && self->frameIndex <= 45)
        {
            ItemObj_animSet(self, Lara_ANIM_WALK_DESCEND_RIGHT, 0, 0);
        }
        else
        {
            ItemObj_animSet(self, Lara_ANIM_WALK_DESCEND_LEFT, 0, 0);
        }
    }
    if (gs->cinfo.m.floor >= -LARA_STEP_HEIGHT && gs->cinfo.m.floor < -128)
    {
        if (self->frameIndex >= 27 && self->frameIndex <= 44)
        {
            ItemObj_animSet(self, Lara_ANIM_WALK_ASCEND_RIGHT, 0, 0);
        }
        else
        {
            ItemObj_animSet(self, Lara_ANIM_WALK_ASCEND_LEFT, 0, 0);
        }
    }
    if (Lara_c_checkSlide(self))
        return;
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_STATE_WATER_OUT(Lara *self)
{
    Lara_c_angle(self, 0);
    Lara_c_default(self);
}

void Lara_c_STATE_ZIPLINE(Lara *self)
{
    (void)self;
}

void Lara_c_angle(Lara *self, int16 angleDelta)
{
    angleDelta += self->angle.y;
    self->extraL->moveAngle = angleDelta;
    CollisionInfo_setAngle(&(gs->cinfo), angleDelta);
}

void Lara_c_applyOffset(Lara *self)
{
    (self->pos = vec3i_add(self->pos, gs->cinfo.offset));
    (gs->cinfo.offset = vec3i_create(0, 0, 0));
}

bool Lara_c_checkCeiling(Lara *self)
{
    if (gs->cinfo.type != CT_CEILING && gs->cinfo.type != CT_FLOOR_CEILING)
    {
        return 0;
    }
    ItemObj_animSet(self, Lara_ANIM_STAND, 1, 0);
    self->goalState = self->state;
    self->hSpeed = 0;
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    (self->pos = gs->cinfo.pos);
    return 1;
}

bool Lara_c_checkClimbStart(Lara *self)
{
    (void)self;
    return 0;
}

#if TR64_HAS_SEQUEL
/* tr2-climbing, Clearance and side-to-side agreement. Use a cardinal
 * basis at the face, then sample exactly the near and far hand spots. */
enum { HAND_BLOCKED, HAND_WALL, HAND_EDGE };
typedef struct { int kind; int32 shift, floor; } ClimbHand;

static int16 tr2Cardinal(int16 yaw)
{
    return wrap_i16((yaw+8192)&0xc000);
}

static bool tr2NearCardinal(int16 yaw,int16 axis,int tolerance)
{
    return abs(wrap_i16((uint16)yaw-(uint16)axis))<=tolerance+(axis==ANGLE_180);
}

static ClimbHand tr2ClimbHand(const Lara *self,int16 axis,int side,int32 y,int32 span,bool up)
{
    int forward=axis==0 || axis==ANGLE_90 ? 1 : -1;
    bool alongX=axis==ANGLE_90 || axis==-ANGLE_90;
    vec3i p=self->pos;
    p.x=i32_add(p.x,forward*(alongX ? 100 : side));
    p.z=i32_add(p.z,forward*(alongX ? -side : 100));
    Room *room=self->room;
    int32 top=up ? i32_sub(y,768) : y;
    int32 nearY=up ? i32_sub(top,256) : top;
    const Sector *s=game_tr2_room_sector(&room,p.x,nearY,p.z);
    int32 nearCeil=Sector_getCeiling(s,p.x,nearY,p.z);
    int32 shift=nearCeil==WALL ? 0 : i32_sub(nearCeil,nearY);
    if (shift>70)
        return (ClimbHand){HAND_BLOCKED,0,0};
    if (shift<0)
        shift=0;
    int32 nearFloor=up ? WALL : Sector_getFloor(s,p.x,nearY,p.z);
    if (!up) {
        if (nearFloor==WALL)
            return (ClimbHand){HAND_BLOCKED,0,0};
        int32 shortfall=i32_sub(nearFloor,i32_add(top,span+128));
        if (shortfall < -70 || (shortfall<0 && shift>0))
            return (ClimbHand){HAND_BLOCKED,0,0};
        if (shortfall<0)
            shift=shortfall;
    }

    if (alongX)
        p.x=i32_add(p.x,2*forward);
    else
        p.z=i32_add(p.z,2*forward);
    s=game_tr2_room_sector(&room,p.x,top,p.z);
    int32 farFloor=Sector_getFloor(s,p.x,top,p.z);
    int32 floor=i32_sub(farFloor,top);
    int32 bottom=i32_add(top,up ? 512 : span);
    s=game_tr2_room_sector(&room,p.x,bottom,p.z);
    int32 farCeil=Sector_getCeiling(s,p.x,bottom,p.z);
    int32 ceil=i32_sub(farCeil,top);

    if (up) {
        if (farFloor==WALL)
            return (ClimbHand){HAND_WALL,shift,0};
        if (floor<=128) {
            if (farCeil==WALL || farCeil<=farFloor || ceil>=512) {
                if (floor>shift) shift=floor;
                return (ClimbHand){HAND_WALL,shift,floor};
            }
        } else if (farCeil!=WALL && ceil>=512) {
            return (ClimbHand){HAND_WALL,shift,floor};
        } else if (farCeil!=WALL && (int64_t)farFloor-farCeil>=763) {
            return (ClimbHand){HAND_EDGE,shift,floor};
        }
        return (ClimbHand){HAND_BLOCKED,0,floor};
    }

    if (farFloor==WALL || floor<=70) {
        if (farFloor!=WALL && floor>0) {
            if (shift<0)
                return (ClimbHand){HAND_BLOCKED,0,floor};
            if (floor>shift) shift=floor;
        }
        if (farCeil==WALL || (farFloor!=WALL && farCeil<=farFloor) || ceil>=512)
            return (ClimbHand){HAND_WALL,shift,floor};
    } else if (farCeil!=WALL && ceil>=512) {
        return (ClimbHand){HAND_WALL,shift,floor};
    }
    if (farCeil!=WALL && ceil>=443 && ceil<512 && shift<=0) {
        int32 fit=ceil-512;
        return (ClimbHand){HAND_WALL,fit<shift ? fit : shift,floor};
    }
    if ((int64_t)nearFloor-top-128>=900 &&
        (farFloor==WALL || ceil>0 || (ceil>-70 && shift<=0))) {
        if (ceil<=0 && farCeil!=WALL && ceil<shift) shift=ceil;
        return (ClimbHand){HAND_EDGE,shift,floor};
    }
    return (ClimbHand){HAND_BLOCKED,0,floor};
}

static bool tr2HandAgreement(ClimbHand a,ClimbHand b,int32 *shift)
{
    if (!a.kind || !b.kind || (int64_t)a.shift*b.shift<0)
        return false;
    *shift=llabs(a.shift)>llabs(b.shift) ? a.shift : b.shift;
    return true;
}

static bool tr2ClimbPair(const Lara *self,int16 axis,int32 y,int32 span,bool up,
                         ClimbHand *right,ClimbHand *left,int32 *shift)
{
    *right=tr2ClimbHand(self,axis,220,y,span,up);
    *left=tr2ClimbHand(self,axis,-180,y,span,up);
    return tr2HandAgreement(*right,*left,shift);
}

static bool tr2GroundClimb(Lara *self)
{
    CollisionInfo *c=&gs->cinfo;
    if (!(self->input&IN_ACTION) || self->extraL->weaponState!=WEAPON_STATE_FREE ||
        c->type!=CT_FRONT)
        return false;
    int16 axis=tr2Cardinal(self->angle.y);
    if (!tr2NearCardinal(self->angle.y,axis,5460))
        return false;

    int64_t lip=-(int64_t)c->f.floor;
    bool even=llabs((int64_t)c->r.floor-c->l.floor)<60;
    int32 pending=0;
    if (lip>=384 && lip<=896) {
        if (!even || Lara_c_checkSpace(self))
            return false;
        Lara_setWeaponState(self,WEAPON_STATE_BUSY);
        ItemObj_animSet(self,lip<=640 ? Lara_ANIM_CLIMB_2 : Lara_ANIM_CLIMB_3,true,0);
        self->state=Lara_STATE_HANG_UP;
        self->pos.y=i32_add(self->pos.y,(lip<=640 ? 512 : 768)-lip);
    } else if (lip>=897 && lip<=1920 && even) {
        pending=3+phd_sqrt(12*lip-9600);
    } else if (!gs->tr2->climbEnabled) {
        return false;
    } else if (lip>=1920 && c->l.floor<=-1920 && c->r.floor<=-2048 &&
               c->m.ceiling<=-(1158-LARA_HEIGHT) && self->waterState!=WATER_STATE_WADE) {
        pending=116;
    } else {
        if (c->f.ceiling>LARA_HEIGHT-518 ||
            (lip<=1024 && (int64_t)c->f.floor-c->f.ceiling+LARA_HEIGHT<506))
            return false;
        vec3i old=self->pos;
        self->pos=vec3i_add(self->pos,c->offset);
        ClimbHand left,right;
        int32 shift;
        if (!tr2ClimbPair(self,axis,i32_sub(self->pos.y,700),512,false,&right,&left,&shift) ||
            right.kind!=HAND_WALL || left.kind!=HAND_WALL) {
            self->pos=old;
            return false;
        }
        self->pos.y=i32_add(self->pos.y,shift);
        self->angle.y=axis;
        c->offset=vec3i_create(0,0,0);
        Lara_setWeaponState(self,WEAPON_STATE_BUSY);
        ItemObj_animSet(self,LADDER_MOUNT,true,0);
        return true;
    }
    if (pending) {
        ItemObj_animSet(self,Lara_ANIM_STAND,true,0);
        self->goalState=Lara_STATE_JUMP_UP;
        self->extraL->vSpeedHack=pending;
        ItemObj_animProcess(self,true);
    }
    self->angle.y=axis;
    Lara_c_applyOffset(self);
    return true;
}

static bool tr2AirCatch(Lara *self)
{
    CollisionInfo *c=&gs->cinfo;
    bool reach=self->state==Lara_STATE_REACH;
    if (!(self->input&IN_ACTION) || self->extraL->weaponState!=WEAPON_STATE_FREE ||
        c->type!=CT_FRONT || c->staticHit || c->m.ceiling>-384 ||
        (reach && c->m.floor<200))
        return false;
    int16 axis=tr2Cardinal(self->angle.y);
    if (!tr2NearCardinal(self->angle.y,axis,6370))
        return false;
    const AABBs *bounds=ItemObj_getBoundingBox(self,true);
    int32 h=i32_sub(c->f.floor,bounds->minY);
    int32 v=i32_add(h,self->vSpeed);
    bool marked=(h<0 && v<0) || (h>0 && v>0);
    int32 y=i32_add(self->pos.y,c->f.floor);
    if (marked) {
        int32 top=i32_add(self->pos.y,bounds->minY),next=i32_add(top,self->vSpeed);
        /* Q-T2M-01: a rising pose can catch a real lip, not a marked band. */
        if (!gs->tr2->climbEnabled || self->vSpeed<=0 || (top>>8)==(next>>8))
            return false;
        vec3i old=self->pos;
        if (axis==ANGLE_90 || axis==-ANGLE_90)
            self->pos.x=i32_add(self->pos.x,c->offset.x);
        else
            self->pos.z=i32_add(self->pos.z,c->offset.z);
        for (int side=-100;side<=100;side+=100) {
            ClimbHand hand=tr2ClimbHand(self,axis,side,top,bounds->maxY-bounds->minY,false);
            if (hand.kind!=HAND_WALL) {
                self->pos=old;
                return false;
            }
        }
        y=next&~255;
    } else if (llabs((int64_t)c->r.floor-c->l.floor)>=60) {
        return false;
    }
    self->angle.y=axis;
#if TR64_HAS_TR3
    int anim=Lara_ANIM_HANG;
#else
    int anim=reach && Lara_c_checkSwing(self) ? Lara_ANIM_HANG_SWING : Lara_ANIM_HANG;
#endif
    ItemObj_animSet(self,anim,true,reach ? 0 : 12);
    Lara_setWeaponState(self,WEAPON_STATE_BUSY);
    self->pos.y=i32_sub(y,ItemObj_getBoundingBox(self,true)->minY);
    if (!marked || !reach) {
        self->pos.x=i32_add(self->pos.x,c->offset.x);
        self->pos.z=i32_add(self->pos.z,c->offset.z);
    }
    c->offset=vec3i_create(0,0,0);
    if (reach) {
        self->hSpeed=2;
        self->vSpeed=1;
        self->flags|=ITEM_FLAG_GRAVITY;
    } else {
        self->hSpeed=self->vSpeed=0;
        self->flags&=~ITEM_FLAG_GRAVITY;
    }
    return true;
}

/* Climbing and zip travel both retain a linked-floor program independently
 * of later obstacle probes. The attached pose chooses the sample height. */
static void tr2AttachedFloor(const Lara *self, int32 above)
{
    const vec3i *p=&self->pos;
    Room *room=self->room;
    int32 y=i32_sub(p->y,above);
    const Sector *s=game_tr2_room_sector(&room,p->x,y,p->z);
    Sector_getFloor(s,p->x,y,p->z);
    gs->cinfo.trigger=gs->gLastFloorData;
}

static bool tr2LadderReleased(Lara *self)
{
    tr2AttachedFloor(self,LARA_HEIGHT+160);
    if (!Lara_c_checkDrop(self))
        return false;
    Lara_setWeaponState(self,WEAPON_STATE_FREE);
    return true;
}

static unsigned tr2LadderSideRequest(const Lara *self)
{
    if ((self->input&IN_LEFT) || (gs->tr2->sidestepInput&TR2_IN_STEP_LEFT))
        return Lara_STATE_CLIMB_LEFT;
    if ((self->input&IN_RIGHT) || (gs->tr2->sidestepInput&TR2_IN_STEP_RIGHT))
        return Lara_STATE_CLIMB_RIGHT;
    return Lara_STATE_CLIMB_START;
}

static void tr2LadderStance(Lara *self)
{
    if (tr2LadderReleased(self) || self->animIndex!=Lara_ANIM_CLIMB_START ||
        self->goalState==Lara_STATE_HANG_UP)
        return;
    bool up=(self->input&IN_UP)!=0;
    bool down=!up && (self->input&IN_DOWN) && gs->tr2->climbEnabled;
    if (up || down) {
        int16 axis=tr2Cardinal(self->angle.y);
        int32 y=up ? i32_sub(self->pos.y,256) : i32_sub(self->pos.y,512);
        ClimbHand right,left;
        int32 shift;
        if (!tr2ClimbPair(self,axis,y,512,up,&right,&left,&shift))
            return;
        if (right.kind==HAND_EDGE || left.kind==HAND_EDGE) {
            if (up && llabs((int64_t)right.floor-left.floor)<=120) {
                self->pos.y=i32_add(self->pos.y,((int64_t)right.floor+left.floor)/2-512);
                self->goalState=Lara_STATE_HANG_UP;
            } else if (!up) {
                self->goalState=Lara_STATE_HANG;
            }
        } else {
            self->pos.y=i32_add(self->pos.y,shift);
            self->goalState=up ? Lara_STATE_CLIMB_UP : Lara_STATE_CLIMB_DOWN;
        }
    }
}

static void tr2LadderVertical(Lara *self,bool up)
{
    if (tr2LadderReleased(self) || self->animIndex!=(up ? LADDER_ASCEND : LADDER_DESCEND))
        return;
    int at=self->frameIndex-tr64_u16(gs->level.anims[self->animIndex].frame_start);
    int trial;
    switch (at) {
    case 0: trial=0; break;
    case 28: case 29: trial=256; break;
    case 57: trial=512; break;
    default: return;
    }
    bool held=(self->input&(up ? IN_UP : IN_DOWN))!=0;
    int32 y=i32_add(self->pos.y,up ? -trial : trial);
    int32 sample=i32_sub(y,256);
    ClimbHand right,left;
    int32 shift;
    bool pair=(up || gs->tr2->climbEnabled) &&
              tr2ClimbPair(self,tr2Cardinal(self->angle.y),sample,512,up,&right,&left,&shift);
    bool edge=pair && (right.kind==HAND_EDGE || left.kind==HAND_EDGE);
    if (held && pair && !edge) {
        self->pos.y=i32_add(self->pos.y,shift);
        return;
    }
    self->goalState=Lara_STATE_CLIMB_START;
    if (at) {
        self->pos.y=y;
        ItemObj_animProcess(self,true);
    }
    if (held && edge) {
        if (up) {
            if (llabs((int64_t)right.floor-left.floor)<=120) {
                self->pos.y=i32_add(self->pos.y,((int64_t)right.floor+left.floor)/2-256);
                self->goalState=Lara_STATE_HANG_UP;
            }
        } else {
            self->goalState=Lara_STATE_HANG;
        }
    }
}

/* Q-T2M-03: the side loop can reach its Hang link only in the
 * authored frame window. Advance animation, not the lateral stride. */
static void tr2LadderEdge(Lara *self)
{
    unsigned clip=self->animIndex;
    const Anim *anim=gs->level.anims+clip;
    unsigned frames=tr64_u16(anim->frame_end)-tr64_u16(anim->frame_start)+1;
    self->goalState=Lara_STATE_HANG;
    do {
        ItemObj_animProcess(self,false);
    } while (self->animIndex==clip && --frames);
    if (self->state!=Lara_STATE_HANG)
        sys_error("Tomb Raider II: ladder side clip has no authored hang exit");
    self->pos.x=gs->cinfo.pos.x;
    self->pos.z=gs->cinfo.pos.z;
    self->hSpeed=0;
}

static void tr2LadderSide(Lara *self,int direction)
{
    if (tr2LadderReleased(self))
        return;
    int16 axis=tr2Cardinal(self->angle.y);
    self->extraL->moveAngle=wrap_i16((uint16)axis+direction*ANGLE_90);
    ClimbHand hand={0};
    if (gs->tr2->climbEnabled)
        hand=tr2ClimbHand(self,axis,direction<0 ? -180 : 220,
                          i32_sub(self->pos.y,512),512,false);
    if (hand.kind==HAND_WALL) {
        self->pos.y=i32_add(self->pos.y,hand.shift);
        if (!(self->input&(direction<0 ? IN_LEFT : IN_RIGHT)))
            self->goalState=Lara_STATE_CLIMB_START;
        return;
    }
    if (hand.kind==HAND_EDGE) {
        tr2LadderEdge(self);
        return;
    }
    self->pos.x=gs->cinfo.pos.x;
    self->pos.z=gs->cinfo.pos.z;
    self->hSpeed=0;
    if (gs->tr2->motionStart.state==Lara_STATE_CLIMB_START) {
        self->state=self->goalState=Lara_STATE_CLIMB_START;
        self->animIndex=gs->tr2->motionStart.anim;
        self->frameIndex=gs->tr2->motionStart.frame;
        ItemObj_animProcess(self,true);
    } else {
        ItemObj_animSet(self,Lara_ANIM_CLIMB_START,true,0);
    }
}

/* Q-T2M-04: the pull-up needs 762 units of world clearance at
 * every sampled side. Collision stores floor relative to the feet
 * and ceiling relative to the top of the 762-unit probe. */
static bool tr2HangClear(const CollisionInfo *c)
{
    return c->f.floor>=-849 && c->f.floor<=-651 && !c->staticHit &&
           c->f.floor>=c->f.ceiling && c->l.floor>=c->l.ceiling && c->r.floor>=c->r.ceiling;
}

static bool tr2HangGrip(const Lara *self)
{
    const AABBs *box=ItemObj_getBoundingBox(self,true);
    int32 top=i32_add(self->pos.y,box->minY),span=box->maxY-box->minY;
    int16 axis=tr2Cardinal(self->angle.y);
    for (int side=-100;side<=100;side+=100)
        if (tr2ClimbHand(self,axis,side,top,span,false).kind==HAND_BLOCKED)
            return false;
    return true;
}

static void tr2HangPullUp(Lara *self)
{
    if (self->state!=Lara_STATE_HANG || self->goalState!=Lara_STATE_HANG ||
        self->animIndex!=Lara_ANIM_HANG || self->frameIndex!=1514)
        return;
    bool marked=gs->tr2->climbEnabled;
    if (marked) {
        ClimbHand right,left;
        int32 shift;
        if (tr2ClimbPair(self,tr2Cardinal(self->angle.y),i32_sub(self->pos.y,700),512,false,
                         &right,&left,&shift) && right.kind==HAND_WALL && left.kind==HAND_WALL) {
            self->pos.y=i32_add(self->pos.y,shift);
            self->goalState=Lara_STATE_CLIMB_START;
            return;
        }
    }
    if (self->input&IN_UP) {
        if (tr2HangClear(&gs->cinfo))
            self->goalState=self->input&IN_WALK ? Lara_STATE_HANDSTAND : Lara_STATE_HANG_UP;
#if TR64_HAS_TR3
        else if (game_tr3_try_climb_to_crawl(self))
            return;
#endif
        else if (marked && gs->cinfo.m.ceiling<=-256)
            ItemObj_animSet(self,Lara_ANIM_HANG_STEP_UP,true,0);
    } else if (marked && (self->input&IN_DOWN)) {
        ItemObj_animSet(self,Lara_ANIM_HANG_STEP_DOWN,true,0);
    }
}
#endif

bool Lara_c_checkClimbUp(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL))
        return tr2GroundClimb(self);
#endif
    if (gs->cinfo.f.floor == (WALL))
        return 0;
    if (Lara_c_checkGrab(self))
        return 0;
    int16 realAngle = self->angle.y;
    if (Lara_alignAngle(self, &(realAngle), (ANGLE(30))))
        return 0;
    if (gs->cinfo.f.floor >= -LARA_SMASH_HEIGHT && gs->cinfo.f.floor <= -LARA_STEP_HEIGHT)
    {
        if (Lara_c_checkSpace(self))
            return 0;
        Lara_setWeaponState(self, WEAPON_STATE_BUSY);
        ItemObj_animSet(self, Lara_ANIM_CLIMB_2, 1, 0);
        self->state = Lara_STATE_HANG_UP;
        (self->pos.y = i32_add(self->pos.y, i32_add(512, gs->cinfo.f.floor)));
    }
    else if (gs->cinfo.f.floor >= -896 && gs->cinfo.f.floor <= -LARA_SMASH_HEIGHT)
    {
        if (Lara_c_checkSpace(self))
            return 0;
        Lara_setWeaponState(self, WEAPON_STATE_BUSY);
        ItemObj_animSet(self, Lara_ANIM_CLIMB_3, 1, 0);
        self->state = Lara_STATE_HANG_UP;
        (self->pos.y = i32_add(self->pos.y, i32_add(768, gs->cinfo.f.floor)));
    }
    else if (gs->cinfo.f.floor >= -1920 && gs->cinfo.f.floor <= -896)
    {
        ItemObj_animSet(self, Lara_ANIM_STAND, 1, 0);
        self->goalState = Lara_STATE_JUMP_UP;
        self->extraL->vSpeedHack =
            ((int32)(phd_sqrt(i32_mul(-12, i32_add(gs->cinfo.f.floor, 800))) + 3));
        ItemObj_animProcess(self, 1);
    }
    else
    {
        return 0;
    }
    self->angle.y = realAngle;
    Lara_c_applyOffset(self);
    return 1;
}

bool Lara_c_checkDrop(Lara *self)
{
    if ((self->health > 0) && (self->input & IN_ACTION))
    {
        self->flags &= ~ITEM_FLAG_GRAVITY;
        self->vSpeed = 0;
        return 0;
    }
    self->flags |= ITEM_FLAG_GRAVITY;
    self->hSpeed = 2;
    self->vSpeed = 1;
    ItemObj_animSet(self, Lara_ANIM_FALL_FORTH, 1, 0);
    return 1;
}

bool Lara_c_checkFall(Lara *self, int32 height, int32 fallAnimIndex)
{
    if (self->waterState == WATER_STATE_WADE)
        return 0;
    if (gs->cinfo.m.floor <= height)
        return 0;
    ItemObj_animSet(self, fallAnimIndex, 1, 0);
    self->vSpeed = 0;
    self->flags |= ITEM_FLAG_GRAVITY;
    return 1;
}

bool Lara_c_checkGrab(Lara *self)
{
    return (self->extraL->weaponState != WEAPON_STATE_FREE) || !(self->input & IN_ACTION) ||
           (gs->cinfo.type != CT_FRONT) ||
           (abs(i32_sub(gs->cinfo.r.floor, gs->cinfo.l.floor)) >= 60);
}

bool Lara_c_checkHang(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(LEDGE_REACH))
        return tr2AirCatch(self);
#endif
    if (Lara_c_checkGrab(self))
        return 0;
    if ((gs->cinfo.f.ceiling > 0) || (gs->cinfo.m.ceiling > -LARA_STEP_HEIGHT) ||
        (gs->cinfo.m.floor < 200 && self->state == Lara_STATE_REACH))
    {
        return 0;
    }
    int32 h = i32_sub(gs->cinfo.f.floor, ItemObj_getBoundingBox(self, 1)->minY);
    int32 v = i32_add(h, self->vSpeed);
    if ((h < 0 && v < 0) || (h > 0 && v > 0))
        return 0;
    if (Lara_alignAngle(self, &(self->angle.y), (ANGLE(35))))
        return 0;
    if (self->state == Lara_STATE_REACH)
    {
        if (Lara_c_checkSwing(self))
        {
            ItemObj_animSet(self, Lara_ANIM_HANG_SWING, 1, 0);
        }
        else
        {
            ItemObj_animSet(self, Lara_ANIM_HANG, 1, 0);
        }
    }
    else
    {
        ItemObj_animSet(self, Lara_ANIM_HANG, 1, 12);
    }
    Lara_setWeaponState(self, WEAPON_STATE_BUSY);
    gs->cinfo.offset.y = i32_sub(gs->cinfo.f.floor, ItemObj_getBoundingBox(self, 1)->minY);
    Lara_c_applyOffset(self);
    self->flags &= ~ITEM_FLAG_GRAVITY;
    self->hSpeed = 0;
    self->vSpeed = 0;
    return 1;
}

bool Lara_c_checkLanding(Lara *self)
{
    if ((self->state == Lara_STATE_FAST_DIVE || self->state == Lara_STATE_ROLL_AIR) &&
        self->vSpeed > 133)
    {
        Lara_hit(self, 1000, &(self->pos), 0);
        return 1;
    }
    int32 y = self->pos.y;
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
    self->roomFloor = self->pos.y;
    game_check_trigger(gs->cinfo.trigger, self);
    self->pos.y = y;
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return false;
    if (self->vSpeed <= 140)
        return 0;
    if (self->vSpeed > 154)
    {
        Lara_hit(self, 1000, &(self->pos), 0);
#if TR64_HAS_SEQUEL
        /* TR2 Falling and landing: lethal threshold stores −1 directly. */
        if (TR64_RULE(LETHAL_LANDING))
            self->health = -1;
#endif
    }
    else
    {
        Lara_hit(self, (i32_mul(((self->vSpeed - 140) * (self->vSpeed - 140)), 1000)) / 196,
                 &(self->pos), 0);
    }
    return self->health <= 0;
}

#if TR64_HAS_SEQUEL
static void tr2SlideDirection(Lara *self)
{
    /* TR2 Ground, slopes and airtime: dominant component, X on a tie. */
    int32 sx = gs->cinfo.slantX, sz = gs->cinfo.slantZ;
    int16 downhill = abs(sx) >= abs(sz) ? (sx > 0 ? -ANGLE_90 : ANGLE_90)
                                       : (sz > 0 ? ANGLE_180 : 0);
    bool forward = abs(wrap_i16((uint16)downhill - (uint16)self->angle.y)) <= ANGLE_90;
    if (self->state == (forward ? Lara_STATE_SLIDE : Lara_STATE_SLIDE_BACK) &&
        gs->tr2->slideAngle == downhill)
        return;

    /* Slide travel versus facing: side deflection does not reorient a
     * settled slide. Only a new entry sets both angles here. */
    ItemObj_animSet(self,forward ? Lara_ANIM_SLIDE_FORTH : Lara_ANIM_SLIDE_BACK,true,0);
    gs->tr2->slideAngle = downhill;
    self->extraL->moveAngle = downhill;
    self->angle.y = wrap_i16((uint16)downhill + (forward ? 0u : 32768u));
}
#endif

bool Lara_c_checkSlide(Lara *self)
{
    if (self->waterState == WATER_STATE_WADE)
        return 0;
#if TR64_HAS_TR3
    if (game_tr3_in_swamp(self))
        return 0;
#endif
    if (gs->cinfo.m.slantType != SLANT_HIGH)
        return 0;
    Lara_c_applyOffset(self);
    int16 realAngle;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(SLIDE_ENTRY)) {
        tr2SlideDirection(self);
        return true;
    }
#endif
    if (gs->cinfo.slantX > 2)
    {
        realAngle = -(ANGLE_90);
    }
    else if (gs->cinfo.slantX < -2)
    {
        realAngle = (ANGLE_90);
    }
    else if (gs->cinfo.slantZ > 2)
    {
        realAngle = ANGLE_180;
    }
    else
    {
        realAngle = 0;
    }
    if (abs((realAngle - self->angle.y)) <= (ANGLE_90))
    {
        if (self->state != Lara_STATE_SLIDE)
        {
            ItemObj_animSet(self, Lara_ANIM_SLIDE_FORTH, 1, 0);
        }
        self->extraL->moveAngle = realAngle;
        self->angle.y = realAngle;
    }
    else
    {
        if (self->state != Lara_STATE_SLIDE_BACK)
        {
            ItemObj_animSet(self, Lara_ANIM_SLIDE_BACK, 1, 0);
        }
        self->extraL->moveAngle = realAngle;
        self->angle.y = (realAngle + (ANGLE_180));
    }
    return 1;
}

bool Lara_c_checkSpace(Lara *self)
{
    (void)self;
    return (gs->cinfo.f.floor < gs->cinfo.f.ceiling || gs->cinfo.l.floor < gs->cinfo.l.ceiling ||
            gs->cinfo.r.floor < gs->cinfo.r.ceiling);
}

bool Lara_c_checkSwing(Lara *self)
{
    int32 x = self->pos.x;
    int32 y = self->pos.y;
    int32 z = self->pos.z;
    switch (self->angle.y)
    {
    case 0:
        (z = i32_add(z, 256));
        break;
    case (ANGLE_90):
        (x = i32_add(x, 256));
        break;
    case -(ANGLE_90):
        (x = i32_sub(x, 256));
        break;
    case ANGLE_180:
        (z = i32_sub(z, 256));
        break;
    }
    Room *roomBelow = Room_getRoom(self->room, x, y, z);
    const Sector *sector = Room_getSector(roomBelow, x, z);
    int32 floor = Sector_getFloor(sector, x, y, z);
    if (floor != (WALL))
    {
        int32 ceiling = Sector_getCeiling(sector, x, y, z);
        (floor = i32_sub(floor, y));
        (ceiling = i32_sub(ceiling, y));
        if (floor > 0 && ceiling < -LARA_HEIGHT_UW)
            return 1;
    }
    return 0;
}

bool Lara_c_checkWall(Lara *self)
{
    if (gs->cinfo.type == CT_FRONT || gs->cinfo.type == CT_FRONT_CEILING)
    {
        Lara_c_applyOffset(self);
        self->goalState = Lara_STATE_STOP;
        self->hSpeed = 0;
        self->flags &= ~ITEM_FLAG_GRAVITY;
        return 1;
    }
    if (gs->cinfo.type == CT_LEFT)
    {
        Lara_c_applyOffset(self);
        self->angle.y += (ANGLE(5));
        self->angle.z = angleLerp(self->angle.z, 0, (ANGLE(2)));
    }
    else if (gs->cinfo.type == CT_RIGHT)
    {
        Lara_c_applyOffset(self);
        self->angle.y -= (ANGLE(5));
        self->angle.z = angleLerp(self->angle.z, 0, (ANGLE(2)));
    }
    return 0;
}

bool Lara_c_checkWallSurf(Lara *self)
{
    if ((gs->cinfo.m.floor < 0 && gs->cinfo.m.slantType == SLANT_HIGH) ||
        (gs->cinfo.type & (CT_FRONT | CT_CEILING | CT_FRONT_CEILING | CT_FLOOR_CEILING)))
    {
        (self->pos = gs->cinfo.pos);
        self->vSpeed = 0;
    }
    else if (gs->cinfo.type == CT_LEFT)
    {
        self->angle.y += (ANGLE(5));
    }
    else if (gs->cinfo.type == CT_RIGHT)
    {
        self->angle.y -= (ANGLE(5));
    }
    return 1;
}

bool Lara_c_checkWallUW(Lara *self)
{
    if (gs->cinfo.type == CT_FRONT)
    {
        if (self->angle.x > (ANGLE(35)))
        {
            self->angle.x += (ANGLE(2));
        }
        else if (self->angle.x < (ANGLE(-35)))
        {
            self->angle.x -= (ANGLE(2));
        }
        else
        {
            self->vSpeed = 0;
        }
    }
    else if (gs->cinfo.type == CT_CEILING)
    {
        int32 waterLevel = ItemObj_getWaterLevel(self);
        if (self->room && ((tr64_u16(self->room->info->flags)) & ROOM_FLAG_WATER_MASK) &&
            waterLevel != (WALL) && abs(i32_sub(self->pos.y, waterLevel)) < 768)
        {
        }
        else if (self->angle.x >= (ANGLE(-45)))
        {
            self->angle.x -= (ANGLE(2));
        }
    }
    else if (gs->cinfo.type == CT_FRONT_CEILING)
    {
        self->vSpeed = 0;
    }
    else if (gs->cinfo.type == CT_LEFT)
    {
        self->angle.y += (ANGLE(5));
    }
    else if (gs->cinfo.type == CT_RIGHT)
    {
        self->angle.y -= (ANGLE(5));
    }
    else if (gs->cinfo.type == CT_FLOOR_CEILING)
    {
        (self->pos = gs->cinfo.pos);
        self->vSpeed = 0;
        return 1;
    }
    if (gs->cinfo.m.floor < 0)
    {
        (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
        self->angle.x += (ANGLE(2));
    }
    int32 waterDepth = ItemObj_getWaterDepth(self);
    if (waterDepth == (WALL))
    {
        if (!self->room || !((tr64_u16(self->room->info->flags)) & ROOM_FLAG_WATER_MASK))
        {
            self->vSpeed = 0;
            (self->pos = gs->cinfo.pos);
        }
    }
    else if (waterDepth >= 0 && waterDepth <= 512)
    {
        self->waterState = WATER_STATE_WADE;
        ItemObj_animSet(self, Lara_ANIM_SWIM_STAND, 1, 0);
        self->goalState = Lara_STATE_STOP;
        self->angle.x = 0;
        self->angle.z = 0;
        self->hSpeed = 0;
        self->vSpeed = 0;
        self->flags &= ~ITEM_FLAG_GRAVITY;
    }
    return 0;
}

bool Lara_c_checkWaterOut(Lara *self)
{
    Room *waterRoom = NULL;
    int32 h;
    if (gs->cinfo.type == CT_FRONT)
    {
        if ((gs->cinfo.f.ceiling > 0) || (gs->cinfo.m.ceiling > -LARA_STEP_HEIGHT) ||
            (abs(i32_sub(gs->cinfo.r.floor, gs->cinfo.l.floor)) >= 60))
        {
            return 0;
        }
        h = i32_add(gs->cinfo.f.floor, LARA_HEIGHT_SURF);
    }
    else
    {
        if (gs->cinfo.type != CT_NONE || !self->room)
            return 0;
        int32 s;
        int32 c;
        sincos(gs->cinfo.angle, s, c);
        int32 x = i32_add(self->pos.x, (i32_mul(gs->cinfo.radius, s)) >> 14);
        int32 z = i32_add(self->pos.z, (i32_mul(gs->cinfo.radius, c)) >> 14);
        waterRoom = Room_getRoom(self->room, x, i32_add(self->pos.y, 1), z);
        if (!waterRoom || !((tr64_u16(waterRoom->info->flags)) & ROOM_FLAG_WATER_MASK))
            return 0;
        const Sector *water = Room_getSector(waterRoom, x, z);
        if (water->roomAbove == 0xFF || !Lara_c_isDirectCeilingFlat(self, water) ||
            Lara_c_getDirectCeiling(self, water, x, self->pos.y, z) != self->pos.y)
            return 0;
        Room *airRoom = gs->rooms + water->roomAbove;
        const Sector *air = Room_getSector(airRoom, x, z);
        if (((tr64_u16(airRoom->info->flags)) & ROOM_FLAG_WATER_MASK) ||
            !Lara_c_isDirectFloorFlat(self, air) ||
            Lara_c_getDirectFloor(self, air, x, self->pos.y, z) != self->pos.y)
            return 0;
        h = 0;
    }
    if (!(self->input & IN_ACTION) || h <= -512 || h > 316)
        return 0;
    if (Lara_alignAngle(self, &(self->angle.y), (ANGLE(35))))
        return 0;
    (self->pos.y = i32_add(self->pos.y, i32_sub(h, 5)));
    ItemObj_updateRoom(self, -LARA_HEIGHT / 2);
    Lara_alignWall(self, -100);
    if ((h < -128))
    {
        ItemObj_animSet(self, Lara_ANIM_WATER_OUT, 1, 0);
    }
    else if (h < 128)
    {
        ItemObj_animSet(self, Lara_ANIM_SURF_OUT, 1, 0);
    }
    else
    {
        ItemObj_animSet(self, Lara_ANIM_SURF_STAND, 1, 0);
    }
    ItemObj_animSet(self, Lara_ANIM_WATER_OUT, 1, 0);
    Lara_setWeaponState(self, WEAPON_STATE_BUSY);
    self->waterState = WATER_STATE_ABOVE;
    self->goalState = Lara_STATE_STOP;
    self->angle.x = 0;
    self->angle.z = 0;
    self->hSpeed = 0;
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    return 1;
}

ClimbState Lara_c_climbCollide(Lara *self, int32 width)
{
    (void)self;
    (void)width;
    return Lara_CLIMB_OK;
}

void Lara_c_climbSide(Lara *self, int32 width)
{
    if (Lara_c_checkDrop(self))
        return;
    switch (Lara_c_climbCollide(self, width))
    {
    case Lara_CLIMB_HANG:
    {
        self->pos.x = gs->cinfo.pos.x;
        self->pos.z = gs->cinfo.pos.z;
        self->goalState = Lara_STATE_HANG;
        break;
    }
    case Lara_CLIMB_COLLIDE:
    {
        self->pos.x = gs->cinfo.pos.x;
        self->pos.z = gs->cinfo.pos.z;
        ItemObj_animSet(self, Lara_ANIM_CLIMB_START, 1, 0);
        break;
    }
    case Lara_CLIMB_OK:
    {
        if (self->input & IN_LEFT)
        {
            self->goalState = Lara_STATE_CLIMB_LEFT;
        }
        else if (self->input & IN_RIGHT)
        {
            self->goalState = Lara_STATE_CLIMB_RIGHT;
        }
        else
        {
            self->goalState = Lara_STATE_CLIMB_START;
        }
        (self->pos.y = i32_add(self->pos.y, gs->cinfo.offset.y));
        break;
    }
    }
}

void Lara_c_default(Lara *self)
{
    gs->cinfo.gapPos = LARA_STEP_HEIGHT;
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
}

void Lara_c_fall(Lara *self)
{
    if (self->vSpeed <= 0 || gs->cinfo.m.floor > 0)
        return;
    bool dead = Lara_c_checkLanding(self);
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
#if TR64_HAS_SEQUEL
    /* tr2-lara-start.md, Standing to fall: frontal reaction is still
     * part of the forward-airborne path on its landing tick. */
    const bool reaction = TR64_RULE(FORWARD_LANDING) && self->state == Lara_STATE_LAND;
#endif
    if (dead)
    {
        self->goalState = Lara_STATE_DEATH;
    }
    else if ((self->state == Lara_STATE_JUMP
#if TR64_HAS_SEQUEL
              || reaction
#endif
             ) && (self->input & IN_UP) && !(self->input & IN_WALK))
    {
        self->goalState = Lara_STATE_RUN;
    }
    else if (self->state == Lara_STATE_FALL)
    {
        ItemObj_animSet(self, Lara_ANIM_LANDING, 1, 0);
    }
    else
    {
        self->goalState = Lara_STATE_STOP;
    }
    Lara_stopScreaming(self);
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    if (self->state == Lara_STATE_JUMP
#if TR64_HAS_SEQUEL
        || reaction
#endif
    )
    {
#if TR64_HAS_SEQUEL
        if (TR64_RULE(FORWARD_LANDING))
            self->hSpeed = 0; /* The selected clip supplies its own grounded speed. */
#endif
        ItemObj_animProcess(self, 1);
    }
}

int32 Lara_c_getDirectCeiling(const Lara *self, const Sector *sector, int32 x, int32 y, int32 z)
{
    (void)self;
    int32 ceiling = (sector->ceiling * 256);
    if (!sector->floorIndex)
        return ceiling;
    const FloorData *fd = gs->level.floors + sector->floorIndex;
    uint16 cmd = tr64_u16(*fd++);
    if (((cmd) & 0x1F) == FLOOR_TYPE_FLOOR)
    {
        if (((cmd) >> 15))
            return ceiling;
        fd++;
        cmd = tr64_u16(*fd++);
    }
    if (((cmd) & 0x1F) == FLOOR_TYPE_CEILING)
    {
        int32 sx = ((int8)((tr64_u16(*fd)) & 0xFF));
        int32 sz = ((int8)((tr64_u16(*fd)) >> 8));
        int32 dx = x & 1023;
        int32 dz = z & 1023;
        (ceiling = i32_sub(ceiling, i32_mul(sx, sx < 0 ? ((dx - 1023)) : dx) >> 2));
        (ceiling = i32_add(ceiling, i32_mul(sz, sz < 0 ? dz : ((dz - 1023))) >> 2));
    }
    Sector_getTriggerFloorCeiling(sector, x, y, z, NULL, &ceiling);
    return ceiling;
}

int32 Lara_c_getDirectFloor(const Lara *self, const Sector *sector, int32 x, int32 y, int32 z)
{
    (void)self;
    int32 floor = (sector->floor * 256);
    if (!sector->floorIndex)
        return floor;
    const FloorData *fd = gs->level.floors + sector->floorIndex;
    uint16 cmd = tr64_u16(*fd++);
    if (((cmd) & 0x1F) == FLOOR_TYPE_FLOOR)
    {
        int32 sx = ((int8)((tr64_u16(*fd)) & 0xFF));
        int32 sz = ((int8)((tr64_u16(*fd)) >> 8));
        int32 dx = x & 1023;
        int32 dz = z & 1023;
        (floor = i32_sub(floor, i32_mul(sx, sx < 0 ? dx : ((dx - 1023))) >> 2));
        (floor = i32_sub(floor, i32_mul(sz, sz < 0 ? dz : ((dz - 1023))) >> 2));
    }
    Sector_getTriggerFloorCeiling(sector, x, y, z, &floor, NULL);
    return floor;
}

void Lara_c_hang(Lara *self, int32 angleDelta)
{
    Lara_c_angle(self, angleDelta);
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = (WALL);
    gs->cinfo.gapCeiling = 0;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    bool noFloor = ((gs->cinfo).f).floor < 200;
    Lara_c_angle(self, 0);
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 0;
    switch (gs->cinfo.quadrant)
    {
#if TR64_HAS_TR3
    case 0:
        (self->pos.z = i32_add(self->pos.z, 4));
        break;
    case 1:
        (self->pos.x = i32_add(self->pos.x, 4));
        break;
    case 2:
        (self->pos.z = i32_sub(self->pos.z, 4));
        break;
    case 3:
        (self->pos.x = i32_sub(self->pos.x, 4));
        break;
#else
    case 0:
        (self->pos.z = i32_add(self->pos.z, 2));
        break;
    case 1:
        (self->pos.x = i32_add(self->pos.x, 2));
        break;
    case 2:
        (self->pos.z = i32_sub(self->pos.z, 2));
        break;
    case 3:
        (self->pos.x = i32_sub(self->pos.x, 2));
        break;
#endif
    }
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    self->extraL->moveAngle = i32_add(self->angle.y, angleDelta);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CLIMB_WALL)) {
        CollisionInfo *c=&gs->cinfo;
        bool marked=gs->tr2->climbEnabled;
        bool release=self->health<=0 || !(self->input&IN_ACTION) || (!marked && c->f.floor>0);
        if (release) {
            if (gs->tr2->climbEnabled) {
                const vec3i zero={0};
                /* ItemObj_getJoint returns the rotated offset from the origin. */
                vec3i root=ItemObj_getJoint(self,0,&zero);
                if (c->quadrant&1)
                    self->pos.z=i32_add(self->pos.z,root.z);
                else
                    self->pos.x=i32_add(self->pos.x,root.x);
                self->pos.y=i32_add(self->pos.y,256);
                ItemObj_animSet(self,Lara_ANIM_FALL_FORTH,true,0);
            } else {
                ItemObj_animSet(self,Lara_ANIM_FALL_HANG,true,9);
                c->offset.y=ItemObj_getBoundingBox(self,true)->maxY;
                Lara_c_applyOffset(self);
            }
            self->hSpeed=2;
            self->vSpeed=1;
            self->flags|=ITEM_FLAG_GRAVITY;
            Lara_setWeaponState(self,WEAPON_STATE_FREE);
            return;
        }
        self->vSpeed=0;
        self->flags&=~ITEM_FLAG_GRAVITY;
        int32 h=i32_sub(c->f.floor,ItemObj_getBoundingBox(self,true)->minY);
        if (!marked && (noFloor || c->type!=CT_FRONT || c->staticHit || c->m.ceiling>=0 ||
                       llabs((int64_t)c->r.floor-c->l.floor)>=60 || llabs(h)>60)) {
            if (self->state!=Lara_STATE_HANG)
                ItemObj_animSet(self,Lara_ANIM_HANG,true,21);
            self->pos=c->pos;
            return;
        }
        if (c->quadrant&1)
            self->pos.x=i32_add(self->pos.x,c->offset.x);
        else
            self->pos.z=i32_add(self->pos.z,c->offset.z);
        if (marked && !tr2HangGrip(self)) {
            ItemObj_animSet(self,Lara_ANIM_HANG,true,21);
            self->pos=c->pos;
            return;
        }
        if (!marked)
            self->pos.y=i32_add(self->pos.y,h);
        if (!angleDelta)
            tr2HangPullUp(self);
        return;
    }
#endif
    if (self->health <= 0 || !(self->input & IN_ACTION))
    {
        ItemObj_animSet(self, Lara_ANIM_FALL_HANG, 1, 9);
        gs->cinfo.offset.y =
            i32_add(i32_sub(gs->cinfo.f.floor, ItemObj_getBoundingBox(self, 1)->minY), 2);
        Lara_c_applyOffset(self);
        self->hSpeed = 2;
        self->vSpeed = 1;
        self->flags |= ITEM_FLAG_GRAVITY;
        Lara_setWeaponState(self, WEAPON_STATE_FREE);
        return;
    }
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    if (noFloor || (gs->cinfo.type != CT_FRONT) || (gs->cinfo.m.ceiling >= 0) ||
        abs(i32_sub(gs->cinfo.r.floor, gs->cinfo.l.floor)) >= 60)
    {
        if (self->state != Lara_STATE_HANG)
        {
            ItemObj_animSet(self, Lara_ANIM_HANG, 1, 21);
        }
        (self->pos = gs->cinfo.pos);
        return;
    }
    if (gs->cinfo.quadrant & 1)
    {
        (self->pos.x = i32_add(self->pos.x, gs->cinfo.offset.x));
    }
    else
    {
        (self->pos.z = i32_add(self->pos.z, gs->cinfo.offset.z));
    }
    int32 h = i32_sub(gs->cinfo.f.floor, ItemObj_getBoundingBox(self, 1)->minY);
    if (abs(h) <= 256)
    {
        (self->pos.y = i32_add(self->pos.y, h));
    }
}

bool Lara_c_isDirectCeilingFlat(const Lara *self, const Sector *sector)
{
    (void)self;
    if (!sector->floorIndex)
        return 1;
    const FloorData *fd = gs->level.floors + sector->floorIndex;
    uint16 cmd = tr64_u16(*fd++);
    if (((cmd) & 0x1F) == FLOOR_TYPE_FLOOR)
    {
        if (((cmd) >> 15))
            return 1;
        fd++;
        cmd = tr64_u16(*fd++);
    }
    return ((cmd) & 0x1F) != FLOOR_TYPE_CEILING ||
           (((int8)((tr64_u16(*fd)) & 0xFF)) == 0 && ((int8)((tr64_u16(*fd)) >> 8)) == 0);
}

bool Lara_c_isDirectFloorFlat(const Lara *self, const Sector *sector)
{
    (void)self;
    if (!sector->floorIndex)
        return 1;
    const FloorData *fd = gs->level.floors + sector->floorIndex;
    return ((tr64_u16(*fd)) & 0x1F) != FLOOR_TYPE_FLOOR ||
           (((int8)((tr64_u16(fd[1])) & 0xFF)) == 0 && ((int8)((tr64_u16(fd[1])) >> 8)) == 0);
}

void Lara_c_jump(Lara *self)
{
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = (self->state == Lara_STATE_REACH) ? 0 : -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 192;
    ItemObj_collideRoom(self, self->state == Lara_STATE_JUMP_UP ? LARA_HEIGHT_JUMP : LARA_HEIGHT,
                        0);
    if ((self->state == Lara_STATE_REACH || self->state == Lara_STATE_JUMP_UP) &&
        Lara_c_checkHang(self))
        return;
    Lara_c_applyOffset(self);
    bool slide = (self->state == Lara_STATE_FALL) || (self->state == Lara_STATE_REACH) ||
                 (self->state == Lara_STATE_JUMP_UP);
#if TR64_HAS_SEQUEL
    /* Lara Start, Frontal obstruction: the marked wall's two-unit
     * release still takes the grid correction, not jump recoil. */
    if (TR64_RULE(CLIMB_WALL) && gs->tr2->climbEnabled &&
        self->state==Lara_STATE_JUMP && self->hSpeed==2)
        slide=true;
#endif
    if ((gs->cinfo.type == CT_CEILING) || (slide && (gs->cinfo.type == CT_FRONT_CEILING)))
    {
#if TR64_HAS_TR3
        if ((self->state == Lara_STATE_JUMP_UP || self->state == Lara_STATE_REACH) &&
            game_tr3_try_monkey_grab(self))
            return;
#endif
        if (self->vSpeed <= 0)
        {
            self->vSpeed = 1;
        }
    }
    else if (!slide && ((gs->cinfo.type == CT_FRONT) || (gs->cinfo.type == CT_FRONT_CEILING)))
    {
#if TR64_HAS_SEQUEL
        if (TR64_RULE(JUMP_IMPACT)) {
            /* TR2 Ground, slopes and airtime: impact clip depends on
             * current support, independently of its quarter-speed recoil. */
            if (gs->cinfo.m.floor > 512)
                ItemObj_animSet(self, Lara_ANIM_SMASH_JUMP, true, 1);
            else if (gs->cinfo.m.floor <= 128) {
                bool forward = self->state == Lara_STATE_JUMP;
                ItemObj_animSet(self, 31, true, 0);
                /* tr2-lara-start.md, Frontal obstruction before landing:
                 * the current reaction state overrides the clip default. */
                if (TR64_RULE(FORWARD_LANDING) && forward)
                    self->state = self->goalState = Lara_STATE_LAND;
            }
        } else
#endif
            ItemObj_animSet(self, Lara_ANIM_SMASH_JUMP, 1, 1);
        self->extraL->moveAngle += ANGLE_180;
#if TR64_HAS_SEQUEL
        if (TR64_RULE(JUMP_IMPACT))
            self->hSpeed /= 4;
        else
#endif
        self->hSpeed >>= 2;
        if (self->vSpeed <= 0)
        {
            self->vSpeed = 1;
        }
    }
    else if (gs->cinfo.type == CT_FLOOR_CEILING)
    {
        int32 s;
        int32 c;
        sincos(gs->cinfo.angle, s, c);
        (self->pos.x = i32_sub(self->pos.x, (i32_mul(s, 100)) >> 14));
        (self->pos.z = i32_sub(self->pos.z, (i32_mul(c, 100)) >> 14));
        gs->cinfo.m.floor = 0;
        self->hSpeed = 0;
        if (self->vSpeed <= 0)
        {
            self->vSpeed = 16;
        }
    }
    else if (gs->cinfo.type == CT_LEFT)
    {
        if (!TR64_RULE(CLIMB_WALL) || self->state!=Lara_STATE_JUMP_UP)
            self->angle.y += (ANGLE(5));
    }
    else if (gs->cinfo.type == CT_RIGHT)
    {
        if (!TR64_RULE(CLIMB_WALL) || self->state!=Lara_STATE_JUMP_UP)
            self->angle.y -= (ANGLE(5));
    }
    Lara_c_fall(self);
#if TR64_HAS_SEQUEL
    /* TR2's side/back jumps apply the separate body clearance check
     * after ordinary contact and landing. Differences are relative to
     * feet and body top, not to the same world Y. */
    if (TR64_RULE(SIDE_JUMP_SPACE) && (self->state == Lara_STATE_JUMP_BACK ||
        self->state == Lara_STATE_JUMP_LEFT || self->state == Lara_STATE_JUMP_RIGHT) &&
        gs->cinfo.m.floor != WALL && gs->cinfo.m.ceiling != WALL &&
        abs(gs->cinfo.m.floor - gs->cinfo.m.ceiling) < LARA_HEIGHT) {
        ItemObj_animSet(self, Lara_ANIM_SMASH_JUMP, true, 1);
        self->hSpeed >>= 2;
        self->extraL->moveAngle += ANGLE_180;
        if (self->vSpeed < 1)
            self->vSpeed = 1;
    }
#endif
}

void Lara_c_roll(Lara *self)
{
    self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    if (Lara_c_checkCeiling(self))
        return;
    if (Lara_c_checkSlide(self))
        return;
    if (Lara_c_checkFall(self, 200,
                         self->state == Lara_STATE_ROLL_START ? Lara_ANIM_FALL_FORTH
                                                              : Lara_ANIM_FALL_BACK))
        return;
    Lara_c_applyOffset(self);
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

void Lara_c_slide(Lara *self)
{
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -512;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    if (Lara_c_checkCeiling(self))
        return;
    Lara_c_checkWall(self);
    if (Lara_c_checkFall(self, 200,
                         self->state == Lara_STATE_SLIDE ? Lara_ANIM_FALL_FORTH
                                                         : Lara_ANIM_FALL_BACK))
        return;
    Lara_c_checkSlide(self);
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
    if (gs->cinfo.m.slantType != SLANT_HIGH)
    {
        self->goalState = Lara_STATE_STOP;
    }
}

void Lara_c_step(Lara *self)
{
    gs->cinfo.gapPos = (self->waterState == WATER_STATE_WADE) ? -(WALL) : 128;
    gs->cinfo.gapNeg = -128;
    gs->cinfo.gapCeiling = 0;
    gs->cinfo.stopOnSlant = 1;
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(BLOCKED_MOTION) && gs->cinfo.type==CT_FRONT_CEILING) {
        self->pos=gs->cinfo.pos;
        self->hSpeed=self->vSpeed=0;
        self->flags&=~ITEM_FLAG_GRAVITY;
        ItemObj_animSet(self,Lara_ANIM_STAND,true,0);
        return;
    }
#endif
    if (Lara_c_checkCeiling(self))
        return;
    if (Lara_c_checkWall(self)) {
#if TR64_HAS_SEQUEL
        if (TR64_RULE(BLOCKED_MOTION))
            tr2RecoverGroundStop(self);
        else
#endif
            ItemObj_animSet(self,Lara_ANIM_STAND,true,0);
    }
    if (Lara_c_checkSlide(self))
        return;
    (self->pos.y = i32_add(self->pos.y, gs->cinfo.m.floor));
}

#if TR64_HAS_SEQUEL
static void tr2CollideSurface(Lara *);
static void tr2CollideUnder(Lara *);
#endif

void Lara_c_surf(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WATER)) {
        tr2CollideSurface(self);
        return;
    }
#endif
    ItemObj_collideRoom(self, 800, LARA_HEIGHT_SURF);
    Lara_c_applyOffset(self);
    Lara_c_checkWallSurf(self);
    if (self->state == Lara_STATE_SURF_TREAD)
    {
        if (self->frameIndex == 0)
        {
        }
    }
    else
    {
        if (self->frameIndex % 4 == 0)
        {
        }
    }
    int32 waterLevel = ItemObj_getWaterLevel(self);
    if (i32_sub(waterLevel, self->pos.y) <= -100)
    {
        Lara_s_dive(self);
        return;
    }
}

void Lara_c_swim(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WATER)) {
        tr2CollideUnder(self);
        return;
    }
#endif
    Lara_c_angle(self, 0);
    ItemObj_collideRoom(self, LARA_HEIGHT_UW, LARA_HEIGHT_UW / 2);
    if (gs->cinfo.type == CT_CEILING && self->room &&
        ((tr64_u16(self->room->info->flags)) & ROOM_FLAG_WATER_MASK))
    {
        int32 waterLevel = ItemObj_getWaterLevel(self);
        if (waterLevel != (WALL) && abs(i32_sub(self->pos.y, waterLevel)) < 768)
            gs->cinfo.offset.y = 0;
    }
    Lara_c_applyOffset(self);
    if (Lara_c_checkWallUW(self))
        return;
}

void Lara_recordDistance(Lara *self, const vec3i *oldPos)
{
    int32 dx = i32_sub(self->pos.x, oldPos->x);
    int32 dy = i32_sub(self->pos.y, oldPos->y);
    int32 dz = i32_sub(self->pos.z, oldPos->z);
    uint32 distanceQ =
        ((uint32)(((int64_t)(dx)) * dx + ((int64_t)(dy)) * dy + ((int64_t)(dz)) * dz));
    uint32 distance = phd_sqrt(distanceQ);
    gs->gSaveGame.distance += distance;
}

/* Q-T2W-07: the water mode has already been resolved for this tick. */
static bool tr2Wading(const Lara *self)
{
#if TR64_HAS_TR3
    return TR64_RULE(WATER) && self->waterState==WATER_STATE_WADE;
#else
    return game_lara_has_tr2() && self->waterState==WATER_STATE_WADE;
#endif
}

void Lara_s_STATE_BACK(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
    unsigned held = IN_WALK | IN_DOWN;
    if (tr2Wading(self))
        held = IN_DOWN;
    if ((self->input & held) != held)
    {
        self->goalState = Lara_STATE_STOP;
    }
    else
    {
        self->goalState = Lara_STATE_BACK;
    }
    Lara_s_rotate(self, (ANGLE(4)), 0);
}

void Lara_s_STATE_BACK_FAST(Lara *self)
{
    Lara_s_rotate(self, (ANGLE(6)), 0);
    self->goalState = Lara_STATE_STOP;
}

void Lara_s_STATE_BLOCK_PULL(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(35);
        self->extraL->camera.center = true;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_BLOCK_PUSH(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(35);
        self->extraL->camera.center = true;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_BLOCK_READY(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.y = ANGLE(75);
    Lara_s_ignoreEnemy(self);
    if (!(self->input & IN_ACTION))
    {
        self->goalState = Lara_STATE_STOP;
    }
}

void Lara_s_STATE_CLIMB_DOWN(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_CLIMB_END(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_CLIMB_LEFT(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_CLIMB_RIGHT(Lara *self)
{
    (void)self;
}

#if TR64_HAS_SEQUEL
/* tr2-lara-slide-height-and-gaze.md, Manual Look eligibility on dry ground. */
static void tr2LookTurn(Lara *self,bool pitch)
{
    ExtraInfoLara *e=self->extraL;
    int16 *head=pitch ? &e->head.angle.x : &e->head.angle.y;
    int16 *torso=pitch ? &e->torso.angle.x : &e->torso.angle.y;
    uint16 negative=pitch ? IN_UP : IN_LEFT,positive=pitch ? IN_DOWN : IN_RIGHT;
    int lower=pitch ? -7644 : -8008,upper=pitch ? 4004 : 8008;
    e->camera.mode=CAMERA_MODE_LOOK;
    e->camera.lookAtItem=NULL;
    if (self->input & negative) {
        if (*head>lower) *head-=364;
    } else if (self->input & positive) {
        if (*head<upper) *head+=364;
    }
    self->input &= ~(negative|positive);
    /* Vehicle control never enters the ordinary dry harness. */
    if (e->weaponState!=WEAPON_STATE_BUSY)
        *torso=*head;
}
#endif

void Lara_s_STATE_CLIMB_START(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(MANUAL_LOOK) && (self->input & IN_LOOK))
        tr2LookTurn(self,true);
    if (TR64_RULE(CLIMB_WALL) && self->health>0 && (self->input&IN_ACTION)) {
        unsigned side=tr2LadderSideRequest(self);
        if (side!=Lara_STATE_CLIMB_START)
            self->goalState=side;
        else if (self->animIndex==Lara_ANIM_CLIMB_START && (self->input&IN_JUMP)) {
            self->goalState=Lara_STATE_JUMP_BACK;
            self->extraL->moveAngle=wrap_i16((uint16)self->angle.y+32768);
            Lara_setWeaponState(self,WEAPON_STATE_FREE);
        }
    }
#endif
    (void)self;
}

void Lara_s_STATE_CLIMB_UP(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_COMPRESS(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WATER) && self->waterState == WATER_STATE_WADE) {
        /* Q-HAZ-03, Standing or running Jump: wading skips the direction
         * tests. Descent over 131 still requests fast fall through
         * Lara_s_checkFall. The text does not number that state. */
        Lara_s_checkFall(self);
        return;
    }
#endif
    if ((self->input & IN_UP) && Lara_s_getFront(self, 0))
    {
        self->goalState = Lara_STATE_JUMP;
    }
    else if ((self->input & IN_LEFT) && Lara_s_getFront(self, -(ANGLE_90)))
    {
        self->goalState = Lara_STATE_JUMP_LEFT;
    }
    else if ((self->input & IN_RIGHT) && Lara_s_getFront(self, (ANGLE_90)))
    {
        self->goalState = Lara_STATE_JUMP_RIGHT;
    }
    else if ((self->input & IN_DOWN) && Lara_s_getFront(self, ANGLE_180))
    {
        self->goalState = Lara_STATE_JUMP_BACK;
    }
    /* Q-HAZ-03, Standing or running Jump: no eligible direction keeps the
     * goal. Descent over 131 requests fast fall through Lara_s_checkFall.
     * The text does not number that state. */
    Lara_s_checkFall(self);
}

void Lara_s_STATE_DEATH(Lara *self)
{
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_DEATH_MIDAS(Lara *self)
{
    Lara_s_ignoreEnemy(self);
    self->flags &= ~ITEM_FLAG_GRAVITY;
}

void Lara_s_STATE_DEATH_UW(Lara *self)
{
    self->vSpeed = (((self->vSpeed - 8)) > (0) ? ((self->vSpeed - 8)) : (0));
    self->angle.x = angleLerp(self->angle.x, 0, (ANGLE(2)));
}

void Lara_s_STATE_FALL(Lara *self)
{
    self->hSpeed = ((self->hSpeed * 95)) / 100;
    if (self->vSpeed >= 154)
    {
        Lara_startScreaming(self);
    }
}

void Lara_s_STATE_FALL_BACK(Lara *self)
{
    Lara_s_checkFall(self);
    if ((self->input & IN_ACTION) && (self->extraL->weaponState == WEAPON_STATE_FREE))
    {
        self->goalState = Lara_STATE_REACH;
    }
}

void Lara_s_STATE_FAST_DIVE(Lara *self)
{
    gs->cinfo.enemyPush = 1;
    gs->cinfo.enemyHit = 0;
    self->hSpeed = ((self->hSpeed * 95)) / 100;
    Lara_s_checkRoll(self);
}

void Lara_s_STATE_HANDSTAND(Lara *self)
{
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_HANG(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.x = ANGLE(-60);
    Lara_s_ignoreEnemy(self);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(MANUAL_LOOK) && (self->input & IN_LOOK))
        tr2LookTurn(self,true);
#endif
    if (self->input & IN_LEFT)
    {
        self->goalState = Lara_STATE_HANG_LEFT;
    }
    else if (self->input & IN_RIGHT)
    {
        self->goalState = Lara_STATE_HANG_RIGHT;
    }
}

void Lara_s_STATE_HANG_LEFT(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.x = ANGLE(-60);
    Lara_s_ignoreEnemy(self);
    if (!(self->input & IN_LEFT))
    {
        self->goalState = Lara_STATE_HANG;
    }
}

void Lara_s_STATE_HANG_RIGHT(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.x = ANGLE(-60);
    Lara_s_ignoreEnemy(self);
    if (!(self->input & IN_RIGHT))
    {
        self->goalState = Lara_STATE_HANG;
    }
}

void Lara_s_STATE_HANG_UP(Lara *self)
{
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_JUMP(Lara *self)
{
    if (self->goalState == Lara_STATE_SWAN_DIVE || self->goalState == Lara_STATE_REACH)
    {
        self->goalState = Lara_STATE_JUMP;
    }
    if (self->goalState != Lara_STATE_DEATH && self->goalState != Lara_STATE_STOP &&
        self->goalState != Lara_STATE_RUN)
    {
        if (self->extraL->weaponState == WEAPON_STATE_FREE)
        {
            if (self->input & IN_ACTION)
            {
                self->goalState = Lara_STATE_REACH;
            }
            if (self->input & IN_WALK)
            {
                self->goalState = Lara_STATE_SWAN_DIVE;
            }
        }
        Lara_s_checkRoll(self);
#if TR64_HAS_TR3
        /* TR2 Air reversal: first ask for the authored transition (state
         * 68); the animation windows and effects decide when it commits. */
        if (TR64_RULE(AIR_TWIST) && self->goalState != Lara_STATE_SWAN_DIVE &&
            ((self->input & IN_DOWN) || (gs->tr2->input & TR2_IN_ROLL)))
            self->goalState = Lara_STATE_ROLL_AIR;
#else
        if (game_lara_has_tr2() && self->goalState != Lara_STATE_SWAN_DIVE &&
            ((self->input & IN_DOWN)
#if TR64_HAS_SEQUEL
             || (gs->tr2 && (gs->tr2->input & TR2_IN_ROLL))
#endif
            ))
            self->goalState = Lara_STATE_ROLL_AIR;
#endif
        Lara_s_checkFall(self);
    }
    Lara_s_rotate(self, (ANGLE(3)), 0);
}

void Lara_s_STATE_JUMP_BACK(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.y = ANGLE(135);
    if (Lara_s_checkFall(self))
        return;
    if (self->goalState == Lara_STATE_RUN)
    {
        self->goalState = Lara_STATE_STOP;
    }
#if TR64_HAS_TR3
    if (TR64_RULE(AIR_TWIST) && self->goalState != Lara_STATE_STOP &&
        ((self->input & IN_UP) || (gs->tr2->input & TR2_IN_ROLL)))
        self->goalState = Lara_STATE_ROLL_AIR;
#else
    if (game_lara_has_tr2() && self->goalState != Lara_STATE_STOP &&
        ((self->input & IN_UP)
#if TR64_HAS_SEQUEL
         || (gs->tr2 && (gs->tr2->input & TR2_IN_ROLL))
#endif
        ))
        self->goalState = Lara_STATE_ROLL_AIR;
#endif
}

void Lara_s_STATE_JUMP_LEFT(Lara *self)
{
    Lara_s_checkFall(self);
}

void Lara_s_STATE_JUMP_RIGHT(Lara *self)
{
    Lara_s_checkFall(self);
}

void Lara_s_STATE_JUMP_UP(Lara *self)
{
    Lara_s_checkFall(self);
}

void Lara_s_STATE_LAND(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_PICKUP(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-15);
        self->extraL->camera.targetAngle.y = ANGLE(-130);
        self->extraL->camera.targetDist = 1024;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_PICKUP_FLARE(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_POSE(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_REACH(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.y = ANGLE(85);
    Lara_s_checkFall(self);
}

void Lara_s_STATE_ROLL_AIR(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_ROLL_END(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_ROLL_START(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_ROLL_UW(Lara *self)
{
    (void)self;
}

#if TR64_HAS_SEQUEL
/* Ground, slopes and airtime: clip 6 disarms; standing frame 4 of the
 * running loop arms the request but the data decides the launch. */
static bool tr2RunJumpArmed(const Lara *self)
{
    if (self->animIndex == Lara_ANIM_RUN_START)
        gs->tr2->runJumpArmed = false;
    else if (self->animIndex == Lara_ANIM_RUN && self->frameIndex == 4)
        gs->tr2->runJumpArmed = true;
    return gs->tr2->runJumpArmed;
}
#endif

void Lara_s_STATE_RUN(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH))
        return;
    if (Lara_s_checkRoll(self))
        return;
    Lara_s_rotate(self, (ANGLE(8)), 1);
#if TR64_HAS_SEQUEL
    bool armed = !TR64_RULE(RUN_JUMP_WINDOW) || tr2RunJumpArmed(self);
    if ((self->input & IN_JUMP) && !(self->flags & ITEM_FLAG_GRAVITY) && armed)
#else
    if ((self->input & IN_JUMP) && !(self->flags & ITEM_FLAG_GRAVITY))
#endif
    {
        self->goalState = Lara_STATE_JUMP;
    }
    else
    {
        if (tr2Wading(self) && (self->input&IN_UP)) {
            self->goalState=Lara_STATE_WADE;
            return;
        }
#if TR64_HAS_TR3
        if ((self->input & IN_UP) && !(self->input & IN_WALK) && game_tr3_can_sprint(self)) {
            self->goalState = Lara_STATE_SPRINT;
            return;
        }
#else
        if (game_lara_tr3_can_crouch(self)) {
            self->goalState = Lara_STATE_CROUCH_IDLE;
            return;
        }
        if ((self->input & IN_UP) && !(self->input & IN_WALK) && game_lara_tr3_can_sprint(self)) {
            self->goalState = Lara_STATE_SPRINT;
            return;
        }
#endif
        if (TR64_RULE(BLOCKED_MOTION) && (self->input&IN_UP) && !(self->input&IN_WALK))
            self->goalState=Lara_STATE_RUN;
        else
            Lara_s_checkWalk(self,Lara_STATE_STOP);
    }
}

void Lara_s_STATE_SLIDE(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.targetAngle.x = ANGLE(-45);
#if TR64_HAS_SEQUEL
    /* tr2-lara-slide-height-and-gaze.md, Jumping out of a slide. */
    if (TR64_RULE(SLIDE_JUMP_VETO) && (self->input & IN_DOWN))
        return;
#endif
    if (self->input & IN_JUMP)
    {
        self->goalState = Lara_STATE_JUMP;
    }
}

void Lara_s_STATE_SLIDE_BACK(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(SLIDE_JUMP_VETO) && (self->input & IN_UP))
        return;
#endif
    if (self->input & IN_JUMP)
    {
        self->goalState = Lara_STATE_JUMP_BACK;
    }
}

void Lara_s_STATE_SPECIAL(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(170);
        self->extraL->camera.center = true;
    }
}

void Lara_s_STATE_SPLAT(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_STEP_LEFT(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(DRY_SIDESTEP)) {
        Lara_s_rotate(self,728,0);
        if (!(gs->tr2->sidestepInput&TR2_IN_STEP_LEFT))
            self->goalState=Lara_STATE_STOP;
        return;
    }
#endif
    if ((self->input & (IN_WALK | IN_LEFT)) != (IN_WALK | IN_LEFT))
    {
        self->goalState = Lara_STATE_STOP;
    }
}

void Lara_s_STATE_STEP_RIGHT(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(DRY_SIDESTEP)) {
        Lara_s_rotate(self,728,0);
        if (!(gs->tr2->sidestepInput&TR2_IN_STEP_RIGHT))
            self->goalState=Lara_STATE_STOP;
        return;
    }
#endif
    if ((self->input & (IN_WALK | IN_RIGHT)) != (IN_WALK | IN_RIGHT))
    {
        self->goalState = Lara_STATE_STOP;
    }
}

void Lara_s_STATE_STOP(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH))
        return;
    if (Lara_s_checkRoll(self))
        return;
    self->goalState = Lara_STATE_STOP;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(MANUAL_LOOK)) {
        if (self->input & IN_LOOK)
            tr2LookTurn(self,true);
    } else
#endif
    if (Lara_s_checkLook(self))
        return;
#if TR64_HAS_TR3
    if (game_tr3_can_crouch(self)) {
        self->goalState = Lara_STATE_CROUCH_IDLE;
        return;
    }
#else
    if (game_lara_tr3_can_crouch(self)) {
        self->goalState = Lara_STATE_CROUCH_IDLE;
        return;
    }
#endif
    /* Water and flares, Standing inputs in shallow water (Q-T2W-06).
     * Forward and Back precede Jump only in Wade. Walk selects the
     * steering cap here, not a different forward gait. */
    if (tr2Wading(self)) {
        if (self->input & IN_UP) {
            const bool walk = (self->input & IN_WALK) != 0;
            Lara_s_rotate(self, ANGLE(walk ? 4 : 8), !walk);
            self->goalState = Lara_STATE_WADE;
            return;
        }
        if (self->input & IN_DOWN) {
            self->goalState = Lara_STATE_BACK;
            return;
        }
    }
#if TR64_HAS_SEQUEL
    if (TR64_RULE(DRY_SIDESTEP) && gs->tr2->sidestepInput
#if TR64_HAS_TR3
        && !game_tr3_in_swamp(self)
#endif
    ) {
        bool left=(gs->tr2->sidestepInput&TR2_IN_STEP_LEFT)!=0;
        /* The sequel attempts the gait before checking the new pose. */
        self->goalState=left ? Lara_STATE_STEP_LEFT : Lara_STATE_STEP_RIGHT;
    } else
#endif
    if (!TR64_RULE(DRY_SIDESTEP) && (self->input & IN_WALK))
    {
        if ((self->input & IN_LEFT) && Lara_s_checkFront(self, -(ANGLE_90), 116))
        {
            self->goalState = Lara_STATE_STEP_LEFT;
        }
        else if ((self->input & IN_RIGHT) && Lara_s_checkFront(self, (ANGLE_90), 116))
        {
            self->goalState = Lara_STATE_STEP_RIGHT;
        }
    }
    else
    {
        if (self->input & IN_LEFT)
        {
            self->goalState = Lara_STATE_TURN_LEFT;
        }
        else if (self->input & IN_RIGHT)
        {
            self->goalState = Lara_STATE_TURN_RIGHT;
        }
    }
    if ((self->input & IN_JUMP)
#if TR64_HAS_TR3
        && !game_tr3_in_swamp(self) && self->waterState != WATER_STATE_WADE
#endif
    )
    {
        self->goalState = Lara_STATE_COMPRESS;
    }
#if TR64_HAS_SEQUEL
    else if (TR64_RULE(INTERACTIONS) &&
             (self->animIndex == Lara_ANIM_STAND_LEFT || self->animIndex == Lara_ANIM_STAND_RIGHT) &&
             (self->input & (IN_UP|IN_WALK)) == (IN_UP|IN_WALK))
    {
        /* Ground and weapons, Walk's frontal stop: an in-flight stop
         * remains Stand, with held Walk retained as its pending goal. */
        Lara_s_rotate(self,ANGLE(4),0);
        self->goalState = Lara_STATE_WALK;
    }
#endif
    else if ((self->input & IN_UP) &&
             ((TR64_RULE(BLOCKED_MOTION) && !(self->input&IN_WALK)) ||
              Lara_s_checkFront(self,0,104)))
    {
        if (self->input & IN_WALK)
        {
            Lara_s_STATE_WALK(self);
        }
        else
        {
            Lara_s_STATE_RUN(self);
        }
    }
    else if ((self->input & IN_DOWN) && Lara_s_checkFront(self, ANGLE_180, 104))
    {
        if (self->input & IN_WALK)
        {
            Lara_s_STATE_BACK(self);
        }
        else
        {
            self->goalState = Lara_STATE_BACK_FAST;
        }
    }
}

void Lara_s_STATE_SURF_BACK(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    self->extraL->swimTimer = 0;
    if (self->input & IN_LEFT)
    {
        self->angle.y -= (ANGLE(2));
    }
    else if (self->input & IN_RIGHT)
    {
        self->angle.y += (ANGLE(2));
    }
    if (!(self->input & IN_DOWN))
    {
        self->goalState = Lara_STATE_SURF_TREAD;
    }
    self->vSpeed = (((self->vSpeed + 8)) < (60) ? ((self->vSpeed + 8)) : (60));
}

void Lara_s_STATE_SURF_LEFT(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    self->extraL->swimTimer = 0;
    if ((self->input & (IN_WALK | IN_LEFT)) != (IN_WALK | IN_LEFT))
    {
        self->goalState = Lara_STATE_SURF_TREAD;
    }
    self->vSpeed = (((self->vSpeed + 8)) < (60) ? ((self->vSpeed + 8)) : (60));
}

void Lara_s_STATE_SURF_RIGHT(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    self->extraL->swimTimer = 0;
    if ((self->input & (IN_WALK | IN_RIGHT)) != (IN_WALK | IN_RIGHT))
    {
        self->goalState = Lara_STATE_SURF_TREAD;
    }
    self->vSpeed = (((self->vSpeed + 8)) < (60) ? ((self->vSpeed + 8)) : (60));
}

void Lara_s_STATE_SURF_SWIM(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    self->extraL->swimTimer = 0;
    if (self->input & IN_LEFT)
    {
        self->angle.y -= (ANGLE(4));
    }
    else if (self->input & IN_RIGHT)
    {
        self->angle.y += (ANGLE(4));
    }
    if (!(self->input & IN_UP) || (self->input & IN_JUMP))
    {
        self->goalState = Lara_STATE_SURF_TREAD;
    }
    self->vSpeed = (((self->vSpeed + 8)) < (60) ? ((self->vSpeed + 8)) : (60));
}

void Lara_s_STATE_SURF_TREAD(Lara *self)
{
    self->vSpeed = (((self->vSpeed - 4)) > (0) ? ((self->vSpeed - 4)) : (0));
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    if (Lara_s_checkLook(self))
        return;
    if (self->input & IN_LEFT)
    {
        self->angle.y -= (ANGLE(4));
    }
    else if (self->input & IN_RIGHT)
    {
        self->angle.y += (ANGLE(4));
    }
    if (self->input & IN_UP)
    {
        self->goalState = Lara_STATE_SURF_SWIM;
    }
    else if (self->input & IN_DOWN)
    {
        self->goalState = Lara_STATE_SURF_BACK;
    }
    else if ((self->input & (IN_WALK | IN_LEFT)) == (IN_WALK | IN_LEFT))
    {
        self->goalState = Lara_STATE_SURF_LEFT;
    }
    else if ((self->input & (IN_WALK | IN_RIGHT)) == (IN_WALK | IN_RIGHT))
    {
        self->goalState = Lara_STATE_SURF_RIGHT;
    }
    if (self->input & IN_JUMP)
    {
        self->extraL->swimTimer++;
        if (self->extraL->swimTimer == 10)
        {
            Lara_s_dive(self);
        }
    }
    else
    {
        self->extraL->swimTimer = 0;
    }
}

void Lara_s_STATE_SWAN_DIVE(Lara *self)
{
    gs->cinfo.enemyPush = 1;
    gs->cinfo.enemyHit = 0;
    Lara_s_checkFall(self);
}

void Lara_s_STATE_SWITCH_DOWN(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(80);
        self->extraL->camera.targetDist = 1024;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_SWITCH_UP(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(80);
        self->extraL->camera.targetDist = 1024;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_TURN_FAST(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
    if (!TR64_RULE(DRY_SIDESTEP) && (self->input & IN_LOOK))
    {
        self->goalState = Lara_STATE_STOP;
        return;
    }
    if (self->turnSpeed < 0)
    {
        self->turnSpeed = -(ANGLE(8));
        if (!(self->input & IN_LEFT))
        {
            self->goalState = Lara_STATE_STOP;
        }
    }
    else
    {
        self->turnSpeed = (ANGLE(8));
        if (!(self->input & IN_RIGHT))
        {
            self->goalState = Lara_STATE_STOP;
        }
    }
}

#if TR64_HAS_SEQUEL
static void tr2StandingTurn(Lara *self,int sign)
{
    int rate=sign*self->turnSpeed+LARA_TURN_ACCEL;
    if (self->extraL->weaponState==WEAPON_STATE_READY)
        self->goalState=Lara_STATE_TURN_FAST;
    else if (rate>728) {
        if (self->input&IN_WALK)
            rate=728;
        else
            self->goalState=Lara_STATE_TURN_FAST;
    }
    self->turnSpeed=sign*rate;
    unsigned held=sign<0 ? IN_LEFT : IN_RIGHT;
    if (tr2Wading(self) && (self->input&IN_UP)) {
        self->goalState=Lara_STATE_WADE;
        return;
    }
    if (TR64_RULE(BLOCKED_MOTION) && (self->input&IN_UP) && !(self->input&IN_WALK))
        self->goalState=Lara_STATE_RUN;
    else
        Lara_s_checkWalk(self,(self->input&held) ? self->goalState : Lara_STATE_STOP);
}
#endif

void Lara_s_STATE_TURN_LEFT(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(DRY_SIDESTEP)) {
        tr2StandingTurn(self,-1);
        return;
    }
#endif
    if (self->input & IN_LOOK)
    {
        self->goalState = Lara_STATE_STOP;
        return;
    }
    self->turnSpeed -= (LARA_TURN_ACCEL);
    if ((self->turnSpeed < -(ANGLE(4)) || self->extraL->weaponState == WEAPON_STATE_READY) &&
        (self->waterState != WATER_STATE_WADE) && !(self->input & IN_WALK))
    {
        self->goalState = Lara_STATE_TURN_FAST;
    }
    if (self->goalState == self->state)
    {
        self->turnSpeed = ((self->turnSpeed) > (-(ANGLE(4))) ? (self->turnSpeed) : (-(ANGLE(4))));
    }
    Lara_s_checkWalk(self, (self->input & IN_LEFT) ? self->goalState : Lara_STATE_STOP);
}

void Lara_s_STATE_TURN_RIGHT(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(DRY_SIDESTEP)) {
        tr2StandingTurn(self,1);
        return;
    }
#endif
    if (self->input & IN_LOOK)
    {
        self->goalState = Lara_STATE_STOP;
        return;
    }
    self->turnSpeed += (LARA_TURN_ACCEL);
    if ((self->turnSpeed > (ANGLE(4)) || self->extraL->weaponState == WEAPON_STATE_READY) &&
        (self->waterState != WATER_STATE_WADE) && !(self->input & IN_WALK))
    {
        self->goalState = Lara_STATE_TURN_FAST;
    }
    if (self->goalState == self->state)
    {
        self->turnSpeed = ((self->turnSpeed) < ((ANGLE(4))) ? (self->turnSpeed) : ((ANGLE(4))));
    }
    Lara_s_checkWalk(self, (self->input & IN_RIGHT) ? self->goalState : Lara_STATE_STOP);
}

void Lara_s_STATE_UNUSED_1(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_UNUSED_2(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_UNUSED_3(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_UNUSED_4(Lara *self)
{
    (void)self;
}

void Lara_s_STATE_USE_KEY(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(-80);
        self->extraL->camera.targetDist = 1024;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_USE_MIDAS(Lara *self)
{
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_USE_PUZZLE(Lara *self)
{
    if (!TR64_RULE(CAMERA_VIEW)) {
        self->extraL->camera.targetAngle.x = ANGLE(-25);
        self->extraL->camera.targetAngle.y = ANGLE(-80);
        self->extraL->camera.targetDist = 1024;
    }
    Lara_s_ignoreEnemy(self);
}

void Lara_s_STATE_UW_DIVE(Lara *self)
{
    if (self->input & IN_UP)
    {
        self->angle.x -= (ANGLE_1);
    }
}

void Lara_s_STATE_UW_GLIDE(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    if (Lara_s_checkRoll(self))
        return;
    Lara_s_turnUW(self);
    if (self->input & IN_JUMP)
    {
        self->goalState = Lara_STATE_UW_SWIM;
    }
    self->vSpeed = (((self->vSpeed - 6)) > (0) ? ((self->vSpeed - 6)) : (0));
    if (self->vSpeed <= 133)
    {
        self->goalState = Lara_STATE_UW_TREAD;
    }
}

void Lara_s_STATE_UW_SWIM(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    if (Lara_s_checkRoll(self))
        return;
    Lara_s_turnUW(self);
    self->vSpeed = (((self->vSpeed + 8)) < (200) ? ((self->vSpeed + 8)) : (200));
    if (!(self->input & IN_JUMP))
    {
        self->goalState = Lara_STATE_UW_GLIDE;
    }
}

void Lara_s_STATE_UW_TREAD(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW))
        return;
    if (Lara_s_checkRoll(self))
        return;
    Lara_s_turnUW(self);
    if (self->input & IN_JUMP)
    {
        self->goalState = Lara_STATE_UW_SWIM;
    }
    self->vSpeed = (((self->vSpeed - 6)) > (0) ? ((self->vSpeed - 6)) : (0));
}

void Lara_s_STATE_WADE(Lara *self)
{
#if TR64_HAS_TR3
    if (TR64_RULE(WATER)) {
#else
    if (game_lara_has_tr2()) {
#endif
        if (!Lara_s_checkDeath(self, Lara_STATE_DEATH)) {
            Lara_s_rotate(self, ANGLE(4), 0);
            self->goalState = !(self->input & IN_UP) ? Lara_STATE_STOP :
                              self->waterState == WATER_STATE_WADE ? Lara_STATE_WADE : Lara_STATE_RUN;
        }
        return;
    }
    (void)self;
}

void Lara_s_STATE_WALK(Lara *self)
{
    if (Lara_s_checkDeath(self, Lara_STATE_STOP))
        return;
    Lara_s_rotate(self, (ANGLE(4)), 0);
    if (tr2Wading(self) && (self->input&IN_UP)) {
        self->goalState=Lara_STATE_WADE;
        return;
    }
    Lara_s_checkWalk(self, Lara_STATE_STOP);
}

void Lara_s_STATE_WATER_OUT(Lara *self)
{
    Lara_s_ignoreEnemy(self);
    if (!TR64_RULE(CAMERA_VIEW))
        self->extraL->camera.center = true;
}

void Lara_s_STATE_ZIPLINE(Lara *self)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WALL_HAZARDS)) {
        /* Q-HAZ-05: retain the full-origin program before release or
         * other motion. A changed collision pose may replace it. */
        tr2AttachedFloor(self,0);
        /* The ordinary pose step still follows this first advance,
         * even if the release window denied the request. */
        if (!(self->input&IN_ACTION))
            game_tr2_zipline_release(self,100,40);
    }
#endif
    (void)self;
}

bool Lara_s_checkDeath(Lara *self, int32 deathState)
{
    if (self->health <= 0)
    {
        self->goalState = deathState;
        return 1;
    }
    return 0;
}

bool Lara_s_checkFall(Lara *self)
{
    if (self->vSpeed > 131)
    {
        if (self->state == Lara_STATE_SWAN_DIVE)
        {
            self->goalState = Lara_STATE_FAST_DIVE;
        }
        else
        {
            self->goalState = Lara_STATE_FALL;
        }
        return 1;
    }
    return 0;
}

bool Lara_s_checkFront(Lara *self, int16 angleDelta, int32 radius)
{
    CollisionInfo tmpInfo = gs->cinfo;
    int16 tmpAngle = self->extraL->moveAngle;
    Lara_c_angle(self, angleDelta);
    gs->cinfo.radius = radius;
    gs->cinfo.type = CT_NONE;
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -LARA_STEP_HEIGHT;
    gs->cinfo.stopOnSlant = 1;
    gs->cinfo.gapCeiling = 0;
    if ((angleDelta == ANGLE_180) &&
        ((self->input & IN_WALK) || (self->waterState == WATER_STATE_WADE)))
    {
        gs->cinfo.gapPos = LARA_STEP_HEIGHT;
        gs->cinfo.stopOnLava = 1;
    }
    ItemObj_collideRoom(self, LARA_HEIGHT, 0);
    bool collide = ((gs->cinfo).type == CT_FRONT) || ((gs->cinfo).type == CT_FRONT_CEILING);
    (gs->cinfo = tmpInfo);
    self->extraL->moveAngle = tmpAngle;
    return !collide;
}

bool Lara_s_checkLook(Lara *self)
{
    if (self->input & IN_LOOK)
    {
        self->extraL->camera.mode = CAMERA_MODE_LOOK;
        return 1;
    }
    if (self->extraL->camera.mode == CAMERA_MODE_LOOK)
    {
        self->extraL->camera.mode = CAMERA_MODE_FOLLOW;
    }
    return 0;
}

bool Lara_s_checkRoll(Lara *self)
{
    if ((self->waterState != WATER_STATE_ABOVE) && (self->waterState != WATER_STATE_UNDER))
    {
        return 0;
    }
    bool roll = (self->input & (IN_UP | IN_DOWN)) == (IN_UP | IN_DOWN);
#if TR64_HAS_SEQUEL
    /* TR2 Ground Roll: dedicated control also enters authored clip 146. */
    if (TR64_RULE(ROLL_CONTROL) && (gs->tr2->input & TR2_IN_ROLL))
        roll = true;
#endif
#if TR64_HAS_TR3
    if (TR64_RULE(WATER) && self->waterState == WATER_STATE_UNDER &&
        (gs->tr2->input & TR2_IN_ROLL) &&
        (self->state == Lara_STATE_UW_SWIM || self->state == Lara_STATE_UW_GLIDE ||
         self->state == Lara_STATE_UW_TREAD)) {
        ItemObj_animSet(self, Lara_ANIM_UW_ROLL, true, 0);
        return true;
    }
#else
    if (game_lara_has_tr2() && self->waterState == WATER_STATE_UNDER &&
        roll &&
        (self->state == Lara_STATE_UW_SWIM || self->state == Lara_STATE_UW_GLIDE ||
         self->state == Lara_STATE_UW_TREAD)) {
        ItemObj_animSet(self, game_lara_anim(Lara_ANIM_UW_ROLL), true, 0);
        return true;
    }
#endif
    if ((self->waterState == WATER_STATE_ABOVE) && roll)
    {
        if ((self->state == Lara_STATE_RUN) || (self->state == Lara_STATE_STOP))
        {
            ItemObj_animSet(self, Lara_ANIM_STAND_ROLL_BEGIN, 1, 2);
            self->goalState = Lara_STATE_STOP;
            return 1;
        }
    }
    return 0;
}

void Lara_s_checkWalk(Lara *self, int32 stopState)
{
    if ((self->input & IN_UP) && Lara_s_checkFront(self, 0, 104))
    {
        if (self->input & IN_WALK)
        {
            self->goalState = Lara_STATE_WALK;
        }
        else
        {
            self->goalState = Lara_STATE_RUN;
        }
    }
    else
    {
        self->goalState = stopState;
    }
}

void Lara_s_dive(Lara *self)
{
    ItemObj_animSet(self, Lara_ANIM_SURF_DIVE, 1, 0);
    self->angle.x = (ANGLE(-45));
    self->vSpeed = 80;
    self->waterState = WATER_STATE_UNDER;
}

bool Lara_s_getFront(Lara *self, int16 rot)
{
    rot += self->angle.y;
    int32 s;
    int32 c;
    sincos(rot, s, c);
    int32 x = i32_add(self->pos.x, s >> (6));
    int32 y = i32_sub(self->pos.y, LARA_HEIGHT);
    int32 z = i32_add(self->pos.z, c >> (6));
    Room *roomFront = Room_getRoom(self->room, x, y, z);
    const Sector *sector = Room_getSector(roomFront, x, z);
    int32 floor = Sector_getFloor(sector, x, y, z);
    if (floor != (WALL))
    {
        (floor = i32_sub(floor, self->pos.y));
    }
    return floor >= -LARA_STEP_HEIGHT;
}

void Lara_s_ignoreEnemy(Lara *self)
{
    (void)self;
    gs->cinfo.enemyPush = 0;
    gs->cinfo.enemyHit = 0;
}

void Lara_s_rotate(Lara *self, int32 maxSpeed, int32 tilt)
{
    (tilt = i32_mul(tilt, LARA_TILT_ACCEL));
    if (self->input & IN_LEFT)
    {
        self->turnSpeed = (((self->turnSpeed - (LARA_TURN_ACCEL))) > (-maxSpeed)
                               ? ((self->turnSpeed - (LARA_TURN_ACCEL)))
                               : (-maxSpeed));
        self->angle.z =
            ((i32_sub(self->angle.z, tilt)) > (-(ANGLE(11))) ? (i32_sub(self->angle.z, tilt))
                                                             : (-(ANGLE(11))));
    }
    else if (self->input & IN_RIGHT)
    {
        self->turnSpeed = (((self->turnSpeed + (LARA_TURN_ACCEL))) < (maxSpeed)
                               ? ((self->turnSpeed + (LARA_TURN_ACCEL)))
                               : (maxSpeed));
        self->angle.z =
            ((i32_add(self->angle.z, tilt)) < ((ANGLE(11))) ? (i32_add(self->angle.z, tilt))
                                                            : ((ANGLE(11))));
    }
}

void Lara_s_turnUW(Lara *self)
{
    if (self->input & IN_UP)
    {
        self->angle.x -= (ANGLE(2));
    }
    else if (self->input & IN_DOWN)
    {
        self->angle.x += (ANGLE(2));
    }
    if (self->input & IN_LEFT)
    {
        self->turnSpeed = (((self->turnSpeed - (LARA_TURN_ACCEL))) > (-(ANGLE(6)))
                               ? ((self->turnSpeed - (LARA_TURN_ACCEL)))
                               : (-(ANGLE(6))));
        self->angle.z -= 546;
    }
    else if (self->input & IN_RIGHT)
    {
        self->turnSpeed = (((self->turnSpeed + (LARA_TURN_ACCEL))) < ((ANGLE(6)))
                               ? ((self->turnSpeed + (LARA_TURN_ACCEL)))
                               : ((ANGLE(6))));
        self->angle.z += 546;
    }
}

void Lara_startScreaming(Lara *self)
{
    soundPlay(SND_SCREAM, &self->pos);
}

void Lara_stopScreaming(Lara *self)
{
    (void)self;
    soundStop(SND_SCREAM);
}

void Lara_updateAbove(Lara *self)
{
    (gs->cinfo).trigger = NULL;
    gs->cinfo.radius = 100;
    (gs->cinfo.pos = self->pos);
    gs->cinfo.enemyPush = 1;
    gs->cinfo.enemyHit = 1;
    gs->cinfo.stopOnSlant = 0;
    gs->cinfo.stopOnLava = 0;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(BLOCKED_MOTION) || TR64_RULE(CLIMB_WALL)) {
        gs->tr2->motionStart.state=self->state;
        gs->tr2->motionStart.anim=self->animIndex;
        gs->tr2->motionStart.frame=self->frameIndex;
    }
    if (TR64_RULE(CAMERA_VIEW))
        game_tr2_camera_request(self);
#endif
#if !TR64_HAS_TR3
    game_lara_tr3_tick(self);
#endif
    Lara_updateState(self);
    /* II also uses the fixed game's dry 182/364 damping. */
    self->angle.z = angleLerp(self->angle.z, 0, (ANGLE(1)));
    self->turnSpeed = angleLerp(self->turnSpeed, 0, (ANGLE(2)));
    self->angle.y += self->turnSpeed;
}

#if TR64_HAS_SEQUEL
static bool tr2LookYawDenied(uint8 state)
{
    switch (state) {
    case Lara_STATE_DEATH: case Lara_STATE_SPLAT:
    case Lara_STATE_STEP_RIGHT: case Lara_STATE_STEP_LEFT:
    case Lara_STATE_JUMP_RIGHT: case Lara_STATE_JUMP_LEFT:
    case Lara_STATE_BLOCK_PUSH: case Lara_STATE_BLOCK_PULL: case Lara_STATE_PICKUP:
    case Lara_STATE_SWITCH_UP: case Lara_STATE_SWITCH_DOWN:
    case Lara_STATE_USE_KEY: case Lara_STATE_USE_PUZZLE:
        return true;
    default:
        return false;
    }
}
#endif

void Lara_updateLook(Lara *self)
{
    ExtraInfoLara_Arm *R = &(self->extraL->armR);
    ExtraInfoLara_Arm *L = &(self->extraL->armL);
    vec3s *H = &(self->extraL->head.angle);
    vec3s *T = &(self->extraL->torso.angle);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(MANUAL_LOOK)) {
        const bool yaw= !gs->tr2->lookYawDenied && (self->input & IN_LOOK) &&
                        self->extraL->camera.mode!=CAMERA_MODE_CUTSCENE;
        gs->tr2->lookYawDenied=tr2LookYawDenied(self->state);
        if (yaw)
            tr2LookTurn(self,false);
        else if (self->extraL->camera.mode!=CAMERA_MODE_LOOK) {
            H->x=T->x=abs(H->x)<364 ? 0 : H->x-H->x/8;
            H->y=T->y=abs(H->y)<364 ? 0 : H->y-H->y/8;
        }
        return;
    }
#endif
    if (self->health <= 0)
    {
        ((*H) = ((*T) = vec3s_create(0, 0, 0)));
        return;
    }
    if (R->target || L->target)
    {
        if (self->extraL->weapon < WEAPON_SHOTGUN)
        {
            int32 aX = (R->angle.x + L->angle.x);
            int32 aY = (R->angle.y + L->angle.y);
            if (R->aim && L->aim)
            {
                H->x = T->x = aX >> 2;
                H->y = T->y = aY >> 2;
            }
            else
            {
                H->x = T->x = aX >> 1;
                H->y = T->y = aY >> 1;
            }
        }
        else
        {
            T->x = R->angle.x;
            T->y = R->angle.y;
            H->x = H->y = 0;
        }
        return;
    }
    if ((self->input & IN_LOOK) && self->extraL->camera.mode != CAMERA_MODE_FIXED)
    {
        self->extraL->camera.lookAtItem = NULL;
        if (self->input & IN_UP)
        {
            H->x -= (ANGLE(2));
        }
        if (self->input & IN_DOWN)
        {
            H->x += (ANGLE(2));
        }
        if (self->input & IN_LEFT)
        {
            H->y -= (ANGLE(2));
        }
        if (self->input & IN_RIGHT)
        {
            H->y += (ANGLE(2));
        }
        H->x = T->x = ((H->x) < ((ANGLE(-42))) ? ((ANGLE(-42)))
                                               : ((H->x) > ((ANGLE(22))) ? ((ANGLE(22))) : (H->x)));
        H->y = T->y = ((H->y) < (-(ANGLE(44))) ? (-(ANGLE(44)))
                                               : ((H->y) > ((ANGLE(44))) ? ((ANGLE(44))) : (H->y)));
        self->input &= ~(IN_RIGHT | IN_LEFT | IN_UP | IN_DOWN);
        return;
    }
    if (self->extraL->camera.lastItem != NULL)
        return;
    H->x = T->x = angleLerp(H->x, 0, abs(H->x) >> 3);
    H->y = T->y = angleLerp(H->y, 0, abs(H->y) >> 3);
}

#if TR64_HAS_SEQUEL
/* Water and flares, Surface strokes and tread: controls operate on
 * the starting pose; collision acts on the advanced pose. */
static void tr2SurfaceControls(Lara *self)
{
    uint8 state = self->state;
    switch (state) {
    case Lara_STATE_SURF_TREAD: case Lara_STATE_SURF_SWIM: case Lara_STATE_SURF_BACK:
    case Lara_STATE_SURF_LEFT: case Lara_STATE_SURF_RIGHT:
        break;
    default:
    #if !TR64_HAS_TR3
    game_lara_tr3_tick(self);
#endif
    Lara_updateState(self);
        return;
    }
    const bool tread = state == Lara_STATE_SURF_TREAD;
    if (tread)
        self->vSpeed = X_MAX(self->vSpeed - 4, 0);
    if (Lara_s_checkDeath(self, Lara_STATE_DEATH_UW) || (tread && Lara_s_checkLook(self)))
        return;
    int16 turn = tread || state == Lara_STATE_SURF_SWIM ? 728 : 364;
    uint16 input = self->input;
    if (input & IN_LEFT)
        self->angle.y = wrap_i16((int32)self->angle.y - turn);
    else if (input & IN_RIGHT)
        self->angle.y = wrap_i16((int32)self->angle.y + turn);
    uint32 steps = gs->tr2->sidestepInput;
    bool left = (steps & TR2_IN_STEP_LEFT) != 0;
    bool right = (steps & TR2_IN_STEP_RIGHT) != 0;
    ExtraInfoLara *e = self->extraL;
    if (tread) {
        if (input & IN_UP)
            self->goalState = Lara_STATE_SURF_SWIM;
        else if (input & IN_DOWN)
            self->goalState = Lara_STATE_SURF_BACK;
        else if (left || right)
            self->goalState = left ? Lara_STATE_SURF_LEFT : Lara_STATE_SURF_RIGHT;
        if (!(input & IN_JUMP))
            e->swimTimer = 0;
        else if (++e->swimTimer == 10)
            self->goalState = Lara_STATE_UW_SWIM;
    } else {
        e->swimTimer = 0;
        bool stroke = state == Lara_STATE_SURF_SWIM ? (input & IN_UP) && !(input & IN_JUMP) :
                      state == Lara_STATE_SURF_BACK ? (input & IN_DOWN) != 0 :
                      state == Lara_STATE_SURF_LEFT ? left : right;
        if (!stroke)
            self->goalState = Lara_STATE_SURF_TREAD;
        self->vSpeed = X_MIN(self->vSpeed + 8, 60);
    }
}
#endif

void Lara_updateSurface(Lara *self)
{
    (gs->cinfo).trigger = NULL;
    gs->cinfo.radius = 100;
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -128;
    gs->cinfo.gapCeiling = 100;
    (gs->cinfo.pos = self->pos);
    gs->cinfo.enemyPush = 0;
    gs->cinfo.enemyHit = 0;
    gs->cinfo.stopOnSlant = 0;
    gs->cinfo.stopOnLava = 0;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WATER)) {
        tr2SurfaceControls(self);
        self->angle.z = angleLerp(self->angle.z, 0, ANGLE(2));
        return;
    }
#endif
#if !TR64_HAS_TR3
    game_lara_tr3_tick(self);
#endif
    Lara_updateState(self);
    self->angle.z = angleLerp(self->angle.z, 0, (ANGLE(2)));
    (void)Lara_applyFlowCurrent(self);
    if (self->waterState != WATER_STATE_SURFACE)
        return;
    int32 s;
    int32 c;
    sincos(self->extraL->moveAngle, s, c);
    (self->pos.x = i32_add(self->pos.x, (i32_mul(s, self->vSpeed)) >> 16));
    (self->pos.z = i32_add(self->pos.z, (i32_mul(c, self->vSpeed)) >> 16));
    self->extraL->camera.targetAngle.x = (ANGLE(-22));
}

void Lara_updateUnder(Lara *self)
{
    (gs->cinfo).trigger = NULL;
    gs->cinfo.radius = 300;
    gs->cinfo.gapPos = -(WALL);
    gs->cinfo.gapNeg = -LARA_HEIGHT_UW;
    gs->cinfo.gapCeiling = LARA_HEIGHT_UW;
    (gs->cinfo.pos = self->pos);
    gs->cinfo.enemyPush = 0;
    gs->cinfo.enemyHit = 0;
    gs->cinfo.stopOnSlant = 0;
    gs->cinfo.stopOnLava = 0;
#if !TR64_HAS_TR3
    game_lara_tr3_tick(self);
#endif
    Lara_updateState(self);
    self->angle.z = angleLerp(self->angle.z, 0, (ANGLE(2)));
    self->turnSpeed = angleLerp(self->turnSpeed, 0, (ANGLE(2)));
    self->angle.y += self->turnSpeed;
#if TR64_HAS_SEQUEL
    if (TR64_RULE(WATER)) {
        self->angle.x = X_MAX(-15470, X_MIN(15470, self->angle.x));
        self->angle.z = X_MAX(-4004, X_MIN(4004, self->angle.z));
        return;
    }
#endif
    self->angle.x = ((self->angle.x) < ((ANGLE(-85)))
                         ? ((ANGLE(-85)))
                         : ((self->angle.x) > ((ANGLE(85))) ? ((ANGLE(85))) : (self->angle.x)));
    self->angle.z = ((self->angle.z) < ((ANGLE(-22)))
                         ? ((ANGLE(-22)))
                         : ((self->angle.z) > ((ANGLE(22))) ? ((ANGLE(22))) : (self->angle.z)));
    (void)Lara_applyFlowCurrent(self);
    int32 sx;
    int32 cx;
    int32 sy;
    int32 cy;
    sincos(self->angle.x, sx, cx);
    sincos(self->angle.y, sy, cy);
    (self->pos.y = i32_sub(self->pos.y, (i32_mul(sx, self->vSpeed)) >> 16));
    (self->pos.x = i32_add(self->pos.x, (i32_mul(cx, (i32_mul(sy, self->vSpeed)) >> 16)) >> 14));
    (self->pos.z = i32_add(self->pos.z, (i32_mul(cx, (i32_mul(cy, self->vSpeed)) >> 16)) >> 14));
}

void Lara_updateWaterState(Lara *self)
{
    int32 waterLevel = ItemObj_getWaterLevel(self);
    int32 waterDist = (WALL);
    if (waterLevel != (WALL))
    {
        waterDist = i32_sub(self->pos.y, waterLevel);
    }
    switch (self->waterState)
    {
    case WATER_STATE_ABOVE:
    {
        if (waterDist == (WALL) || waterDist < LARA_STEP_HEIGHT)
        {
            break;
        }
        int32 waterDepth = ItemObj_getWaterDepth(self);
        if (waterDepth > 730)
        {
            if (!((tr64_u16(self->room->info->flags)) & ROOM_FLAG_WATER_MASK))
                break;
            self->waterState = WATER_STATE_UNDER;
            self->flags &= ~ITEM_FLAG_GRAVITY;
            self->timer = 1800;
            (self->pos.y = i32_add(self->pos.y, 100));
            ItemObj_updateRoom(self, 0);
            Lara_stopScreaming(self);
            if (self->state == Lara_STATE_SWAN_DIVE)
            {
                self->angle.x = (ANGLE(-45));
                self->goalState = Lara_STATE_UW_DIVE;
                ItemObj_animProcess(self, 1);
                self->vSpeed *= 2;
            }
            else if (self->state == Lara_STATE_FAST_DIVE)
            {
                self->angle.x = (ANGLE(-85));
                self->goalState = Lara_STATE_UW_DIVE;
                ItemObj_animProcess(self, 1);
                self->vSpeed *= 2;
            }
            else
            {
                self->angle.x = (ANGLE(-45));
                ItemObj_animSet(self, Lara_ANIM_WATER_FALL, 1, 0);
                self->state = Lara_STATE_UW_DIVE;
                self->goalState = Lara_STATE_UW_SWIM;
                self->vSpeed = (self->vSpeed * 3) / 2;
            }
            ItemObj_fxSplash(self);
        }
        else if (waterDist > LARA_STEP_HEIGHT)
        {
            self->waterState = WATER_STATE_WADE;
            if (!(self->flags & ITEM_FLAG_GRAVITY))
            {
                self->goalState = Lara_STATE_STOP;
            }
        }
        break;
    }
    case WATER_STATE_SURFACE:
    {
        if (((tr64_u16(self->room->info->flags)) & ROOM_FLAG_WATER_MASK))
            break;
        if (waterDist > LARA_STEP_HEIGHT)
        {
            self->waterState = WATER_STATE_WADE;
            ItemObj_animSet(self, Lara_ANIM_STAND_NORMAL, 1, 0);
            self->goalState = Lara_STATE_WADE;
            ItemObj_animProcess(self, 1);
        }
        else
        {
            self->waterState = WATER_STATE_ABOVE;
            ItemObj_animSet(self, Lara_ANIM_FALL_FORTH, 1, 0);
            self->hSpeed = self->vSpeed / 4;
            self->flags |= ITEM_FLAG_GRAVITY;
        }
        self->vSpeed = 0;
        self->angle.x = self->angle.z = 0;
        break;
    }
    case WATER_STATE_UNDER:
    {
        /* oracle hunk 29: she leaves UNDER only once her room is no longer a water room (the fork also surfaced
         * her inside a water room, up to 256 early and under a solid ceiling, without zeroing pitch and roll) */
        if (((tr64_u16(self->room->info->flags)) & ROOM_FLAG_WATER_MASK) || self->extraL->dozy)
            break;
        if ((ItemObj_getWaterDepth(self) != (WALL)) && abs(waterDist) < 256)
        {
            self->waterState = WATER_STATE_SURFACE;
            (self->pos.y = i32_sub(self->pos.y, i32_sub(waterDist, 1)));
            ItemObj_animSet(self, Lara_ANIM_SURF, 1, 0);
            self->vSpeed = 0;
            self->extraL->swimTimer = 11;
            ItemObj_updateRoom(self, -LARA_HEIGHT / 2);
        }
        else
        {
            self->waterState = WATER_STATE_ABOVE;
            ItemObj_animSet(self, Lara_ANIM_FALL_FORTH, 1, 0);
            self->hSpeed = self->vSpeed / 4;
            self->vSpeed = 0;
            self->flags |= ITEM_FLAG_GRAVITY;
        }
        self->angle.x = self->angle.z = 0;
        break;
    }
    case WATER_STATE_WADE:
    {
        if (waterDist < LARA_STEP_HEIGHT)
        {
            self->waterState = WATER_STATE_ABOVE;
            if (self->state == Lara_STATE_WADE)
            {
                self->goalState = Lara_STATE_RUN;
            }
        }
        else if (waterDist > 730)
        {
            self->waterState = WATER_STATE_SURFACE;
            (self->pos.y = i32_sub(self->pos.y, i32_sub(waterDist, 1)));
            if (self->state == Lara_STATE_BACK)
            {
                ItemObj_animSet(self, Lara_ANIM_SURF_BACK, 1, 0);
            }
            else if (self->state == Lara_STATE_STEP_RIGHT)
            {
                ItemObj_animSet(self, Lara_ANIM_SURF_RIGHT, 1, 0);
            }
            else if (self->state == Lara_STATE_STEP_LEFT)
            {
                ItemObj_animSet(self, Lara_ANIM_SURF_LEFT, 1, 0);
            }
            else
            {
                ItemObj_animSet(self, Lara_ANIM_SURF_SWIM, 1, 0);
            }
            self->extraL->swimTimer = 0;
            self->vSpeed = 0;
            self->flags &= ~ITEM_FLAG_GRAVITY;
            self->angle.x = self->angle.z = 0;
            ItemObj_updateRoom(self, 0);
        }
        break;
    }
    }
}

#if TR64_HAS_SEQUEL
static bool tr2RoomWet(const Room *room)
{
    return (tr64_u16(room->info->flags) & ROOM_FLAG_WATER_MASK) != 0;
}

/* A water-column lookup must not replace the collision's pending
 * trigger or slant. The reference height is a click, but the floor is live. */
static int32 tr2WaterFloor(const Lara *self)
{
    const vec3i *p = &self->pos;
    Room *room = self->room;
    const Sector *sector = game_tr2_room_sector(&room, p->x, p->y, p->z);
    const FloorData *last = gs->gLastFloorData;
    uint16 slant = gs->gLastFloorSlant;
    int32 height = Sector_getFloor(sector, p->x, p->y, p->z);
    gs->gLastFloorData = last;
    gs->gLastFloorSlant = slant;
    return height;
}

/* tr2-water-and-flares, Water status before movement. Do not sample
 * the record's waterDistance again after applying a mode or pose. */
void game_tr2_water_status(Lara *self)
{
    ExtraInfoLara *e = self->extraL;
    int32 line = game_tr2_water_height(self->room, self->pos.x, self->pos.z);
    gs->tr2->waterDistance = line == WALL ? -WALL : i32_sub(line, self->pos.y);
    gs->tr2->waterTicks += line != WALL;
    bool wet = tr2RoomWet(self->room);
    int32 feet = i32_sub(self->pos.y, line);
    switch (self->waterState) {
    case WATER_STATE_ABOVE: {
#if TR64_HAS_TR3
        if (game_tr3_in_swamp(self)) {
            game_tr3_swamp_water_status(self, line, feet);
            return;
        }
        if (line == WALL || feet < 256)
            return;
        int32 floor = tr2WaterFloor(self);
        if (floor != WALL && i32_sub(floor, line) <= 474) {
            if (feet > 256) {
                self->waterState = WATER_STATE_WADE;
                if (!(self->flags & ITEM_FLAG_GRAVITY))
                    self->goalState = Lara_STATE_STOP;
            }
            return;
        }
#else
        if (line == WALL || feet < 384)
            return;
        int32 floor = tr2WaterFloor(self);
        if (floor != WALL && i32_sub(floor, line) <= 474) {
            if (game_lara_has_tr2() && feet > 384) {
                self->waterState = WATER_STATE_WADE;
                if (!(self->flags & ITEM_FLAG_GRAVITY))
                    self->goalState = Lara_STATE_STOP;
            }
            return;
        }
#endif
        if (!wet)
            return;
        self->waterState = WATER_STATE_UNDER;
        self->timer = 1800;
        self->flags &= ~ITEM_FLAG_GRAVITY;
        self->pos.y = i32_add(self->pos.y, 100);
        ItemObj_updateRoom(self, 0);
        e->head.angle = e->torso.angle = vec3s_create(0, 0, 0);
        Lara_stopScreaming(self);
        if (self->state == Lara_STATE_SWAN_DIVE || self->state == Lara_STATE_FAST_DIVE) {
            self->angle.x = self->state == Lara_STATE_SWAN_DIVE ? -8190 : -15470;
            self->goalState = Lara_STATE_UW_DIVE;
            ItemObj_animProcess(self, true);
            self->vSpeed *= 2;
        } else {
            ItemObj_animSet(self, Lara_ANIM_WATER_FALL, true, 0);
            self->state = Lara_STATE_UW_DIVE;
            self->goalState = Lara_STATE_UW_SWIM;
            self->angle.x = -8190;
            self->hSpeed = 0;
            self->vSpeed = (self->vSpeed * 3) / 2;
        }
        {
            vec3i sp = vec3i_create(self->pos.x, line, self->pos.z);
            for (int i = 0; i < 10 && gs->tr2->effectFree; ++i) {
                int16 yaw = wrap_i16((rand_draw() - ANGLE_90) * 2);
                int16 spd = (int16)(rand_draw() >> 8);
                game_tr2_shot_effect(TR2_FX_SPLASH, self->room, sp, spd, yaw);
            }
        }
        break;
    }
    case WATER_STATE_WADE: {
#if TR64_HAS_TR3
        if (game_tr3_in_swamp(self)) {
            game_tr3_swamp_water_status(self, line, feet);
            return;
        }
        if (line == WALL || feet < 256) {
#else
        if (line == WALL || feet < 384) {
#endif
            self->waterState = WATER_STATE_ABOVE;
            if (self->state == Lara_STATE_WADE)
                self->goalState = Lara_STATE_RUN;
            return;
        }
        if (feet <= 730)
            return;
        unsigned anim = self->state == Lara_STATE_BACK ? Lara_ANIM_SURF_BACK :
                        self->state == Lara_STATE_STEP_LEFT ? Lara_ANIM_SURF_LEFT :
                        self->state == Lara_STATE_STEP_RIGHT ? Lara_ANIM_SURF_RIGHT :
                        Lara_ANIM_SURF_SWIM;
        self->waterState = WATER_STATE_SURFACE;
        self->pos.y = i32_add(line, 1);
        ItemObj_animSet(self, anim, true, 0);
        e->swimTimer = 0;
        self->hSpeed = self->vSpeed = 0;
        self->angle.x = self->angle.z = 0;
        self->flags &= ~ITEM_FLAG_GRAVITY;
        ItemObj_updateRoom(self, -LARA_HEIGHT / 2);
        break;
    }
    case WATER_STATE_SURFACE:
        if (wet)
            return;
        if (line != WALL && feet > 384) {
            self->waterState = WATER_STATE_WADE;
            ItemObj_animSet(self, Lara_ANIM_STAND_NORMAL, true, 0);
            self->goalState = Lara_STATE_WADE;
            self->hSpeed = 0;
        } else {
            self->waterState = WATER_STATE_ABOVE;
            ItemObj_animSet(self, Lara_ANIM_FALL_FORTH, true, 0);
            self->hSpeed = self->vSpeed / 4;
            self->flags |= ITEM_FLAG_GRAVITY;
        }
        self->vSpeed = 0;
        self->angle.x = self->angle.z = 0;
        break;
    case WATER_STATE_UNDER:
        if (wet)
            return;
        if (line != WALL && feet > -256 && feet < 256) {
            self->waterState = WATER_STATE_SURFACE;
            self->pos.y = i32_add(line, 1);
            ItemObj_animSet(self, Lara_ANIM_SURF, true, 0);
            e->swimTimer = 11;
            self->vSpeed = 0;
            self->angle.x = self->angle.z = 0;
            ItemObj_updateRoom(self, -LARA_HEIGHT / 2);
            soundPlay(36, &self->pos); /* Q-T2W: breath sound on return. */
        } else {
            self->waterState = WATER_STATE_ABOVE;
            ItemObj_animSet(self, Lara_ANIM_FALL_FORTH, true, 0);
            self->hSpeed = self->vSpeed / 4;
            self->vSpeed = 0;
            self->flags |= ITEM_FLAG_GRAVITY;
            self->angle.x = self->angle.z = 0;
        }
        break;
    }
}

void game_tr2_water_air(Lara *self)
{
#if TR64_HAS_TR3
    game_tr3_swamp_water_air(self);
    return;
#endif
    /* Q-HAZ-04: dry updates refill even after death. Only a death pose
     * that writes air can replace this value later in the same tick. */
    if (self->health <= 0 && self->waterState != WATER_STATE_ABOVE &&
        self->waterState != WATER_STATE_WADE)
        return;
    switch (self->waterState) {
    case WATER_STATE_ABOVE:
    case WATER_STATE_WADE:
        self->timer = 1800;
        break;
    case WATER_STATE_SURFACE:
        self->timer = X_MIN(self->timer + 10, 1800);
        break;
    case WATER_STATE_UNDER:
        if (--self->timer < 0) {
            self->timer = -1;
            self->health -= 5;
        }
        break;
    }
}

/* tr2-water-and-flares, Surface strokes and Underwater strokes.
 * Preserve two separately rounded products for X/Z below the
 * waterline. The pose has already advanced. */
void game_tr2_water_move(Lara *self)
{
    const bool surface = self->waterState == WATER_STATE_SURFACE;
    int32 yaw = surface ? self->extraL->moveAngle : self->angle.y;
    int32 speed = self->vSpeed;
    int32 x = (speed * tr2_sin(yaw)) >> 16;
    int32 z = (speed * tr2_sin(yaw + ANGLE_90)) >> 16;
    if (!surface) {
        int32 pitch = self->angle.x;
        int32 cx = tr2_sin(pitch + ANGLE_90);
        x = (x * cx) >> 14;
        z = (z * cx) >> 14;
        self->pos.y = i32_sub(self->pos.y, (speed * tr2_sin(pitch)) >> 16);
    }
    self->pos.x = i32_add(self->pos.x, x);
    self->pos.z = i32_add(self->pos.z, z);
}

/* Water and flares. Climb a ledge from the surface on a forward stroke
 * with Action and frontal contact. Lara_c_checkWaterOut is not called:
 * after the three-way choice it always installs clip 111, and it does
 * not reject the highest front grade or a busy hand. */
static bool tr2SurfaceClimb(Lara *self)
{
    const CollisionInfo *c = &gs->cinfo;
    ExtraInfoLara *e = self->extraL;
    bool readyFlare = e->weapon == TR2_WEAPON_FLARE &&
                      (e->weaponState == WEAPON_STATE_FREE ||
                       e->weaponState == WEAPON_STATE_READY);
    /* The stroke sets gapNeg to -384. The shared probe sets CT_FRONT
     * only when the front floor is below that, so a rise of exactly
     * 384 stays untyped. Admit that one floor. Do not widen the probe. */
    bool front = c->type == CT_FRONT ||
                 (c->type == CT_NONE && c->f.floor == -LARA_STEP_HEIGHT);
    int32 h;
    if (self->state != Lara_STATE_SURF_SWIM || !front ||
        !(self->input & IN_ACTION) ||
        (e->weaponState != WEAPON_STATE_FREE && !readyFlare))
        return false;
    /* Front ceiling at most level with the body top; middle ceiling at
     * least 384 farther up; side floors differ by at most 59; the
     * highest grade does not qualify in front. */
    if (c->f.ceiling > 0 || c->m.ceiling > -LARA_STEP_HEIGHT ||
        abs(i32_sub(c->r.floor, c->l.floor)) >= 60 ||
        c->f.slantType == SLANT_HIGH || c->f.floor == WALL)
        return false;
    h = i32_add(c->f.floor, LARA_HEIGHT_SURF);
    if (h < -511 || h > 316)
        return false;
    if (Lara_alignAngle(self, &self->angle.y, ANGLE(35)))
        return false;
    self->pos.y = i32_add(self->pos.y, i32_sub(h, 5));
    ItemObj_updateRoom(self, -LARA_HEIGHT / 2);
    Lara_alignWall(self, -100);
    ItemObj_animSet(self, h < -128 ? Lara_ANIM_WATER_OUT :
                    h < 128 ? Lara_ANIM_SURF_OUT : Lara_ANIM_SURF_STAND, true, 0);
    Lara_setWeaponState(self, WEAPON_STATE_BUSY);
    self->waterState = WATER_STATE_ABOVE;
    self->goalState = Lara_STATE_STOP;
    self->angle.x = self->angle.z = 0;
    self->hSpeed = self->vSpeed = 0;
    self->flags &= ~ITEM_FLAG_GRAVITY;
    return true;
}

/* Water and flares, Dive from the surface and Step out. The surface
 * counter and travel have already been spent on this tick. */
static void tr2CollideSurface(Lara *self)
{
    ItemObj_collideRoom(self, 800, LARA_HEIGHT_SURF);
    Lara_c_applyOffset(self);
    Lara_c_checkWallSurf(self);
    int32 line = game_tr2_water_height(self->room, self->pos.x, self->pos.z);
    const CollisionInfo *c = &gs->cinfo;
    if (tr2SurfaceClimb(self))
        return;
    if ((self->state == Lara_STATE_SURF_TREAD && self->goalState == Lara_STATE_UW_SWIM) ||
        (line != WALL && i32_sub(self->pos.y, line) >= 100)) {
        Lara_s_dive(self);
        self->goalState = Lara_STATE_UW_SWIM;
    } else if (!(c->type & (CT_FRONT | CT_FRONT_CEILING | CT_FLOOR_CEILING)) &&
               c->f.floor != WALL && c->m.slantType != SLANT_HIGH && c->m.floor < 0) {
        unsigned anim = Lara_ANIM_WADE;
        if (c->m.floor < -128)
            anim = Lara_ANIM_WADE_ASCEND;
        else if (self->state == Lara_STATE_SURF_LEFT)
            anim = Lara_ANIM_STEP_LEFT;
        else if (self->state == Lara_STATE_SURF_RIGHT)
            anim = Lara_ANIM_STEP_RIGHT;
        ItemObj_animSet(self, anim, true, 0);
        self->waterState = WATER_STATE_WADE;
        self->pos.y = i32_add(self->pos.y, i32_add(c->f.floor, 695));
        self->hSpeed = self->vSpeed = 0;
        self->angle.x = self->angle.z = 0;
        self->flags &= ~ITEM_FLAG_GRAVITY;
    }
}

/* Water and flares, Underwater collision: size the probe with the
 * pitched body and use the floor relative to that reference. */
static void tr2CollideUnder(Lara *self)
{
    int32 pitch = self->angle.x;
    int32 height = abs((tr2_sin(pitch) * LARA_HEIGHT) >> 14);
    height = X_MAX(height, 200);
    Lara_c_angle(self, abs(pitch) > ANGLE_90 ? ANGLE_180 : 0);
    gs->cinfo.gapNeg = -height;
    ItemObj_collideRoom(self, height, height / 2);
    const CollisionInfo *c = &gs->cinfo;
    if (c->type == CT_FLOOR_CEILING) {
        self->pos = c->pos;
        self->vSpeed = 0;
    } else {
        Lara_c_applyOffset(self);
        switch (c->type) {
        case CT_FRONT:
            if (abs(pitch) > 6370)
                self->angle.x += pitch > 0 ? 364 : -364;
            else
                self->vSpeed = 0;
            break;
        case CT_FRONT_CEILING:
            self->vSpeed = 0;
            break;
        case CT_LEFT:
            self->angle.y = wrap_i16((int32)self->angle.y + 910);
            break;
        case CT_RIGHT:
            self->angle.y = wrap_i16((int32)self->angle.y - 910);
            break;
        case CT_CEILING:
            if (pitch >= -8190)
                self->angle.x -= 364;
            break;
        }
        if (c->m.floor != WALL && c->m.floor < 0) {
            self->pos.y = i32_add(self->pos.y, c->m.floor);
            self->angle.x += 364;
        }
    }
    const vec3i *p = &self->pos;
    Room *room = Room_getRoom(self->room, p->x, p->y, p->z);
    int32 line = game_tr2_water_height(room, p->x, p->z);
    if (line == WALL && tr2RoomWet(room))
        return; /* A roof with no link to air is deep water. */
    int32 floor = tr2WaterFloor(self);
    if (line == WALL || floor == WALL) {
        self->pos = c->pos;
        self->vSpeed = 0;
    } else if (i32_sub(floor, line) <= 512) {
        ItemObj_animSet(self, Lara_ANIM_SWIM_STAND, true, 0);
        self->goalState = Lara_STATE_STOP;
        self->waterState = WATER_STATE_WADE;
        self->pos.y = floor;
        self->angle.x = self->angle.z = 0;
        self->hSpeed = self->vSpeed = 0;
        self->flags &= ~ITEM_FLAG_GRAVITY;
    }
}
#endif
