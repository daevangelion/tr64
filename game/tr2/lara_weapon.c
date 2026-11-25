/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "flare.h"
#include "enemy_urban.h"
#include "tr64/code.h"

static bool carriedWeapon(unsigned type)
{
    for (unsigned i=0;i<gs->tr2->carriedCount;++i)
        if (gs->tr2->carried[i].type==type && gs->tr2->carried[i].quantity)
            return true;
    return false;
}

static unsigned pistolClip(unsigned frame)
{
    return gs->level.models[TR2_MODEL_PISTOL_ARMS].animIndex +
           (frame >= PISTOL_RECOIL ? 3 : frame >= PISTOL_UNHOLSTER ? 2 : frame >= PISTOL_DRAW_START);
}

static unsigned flareClip(unsigned frame)
{
    return gs->level.models[TR2_MODEL_FLARE_ARMS].animIndex +
           (frame >= FLARE_RAISE ? 4 : frame >= FLARE_IGNITION ? 3 :
            frame >= FLARE_DRAW_START ? 2 : frame != FLARE_REST);
}

bool game_tr2_firearms_camera_requested(void)
{
    return gs && TR64_RULE(ARMS) && gs->tr2->arms.firearmsView;
}

static unsigned rememberedGun(void)
{
    switch (gs->tr2->lastWeapon) {
    case 2: return WEAPON_MAGNUMS;
    case 3: return WEAPON_UZIS;
    case 4: return WEAPON_SHOTGUN;
    case 6: return TR2_WEAPON_GRENADE;
    default: return WEAPON_PISTOLS;
    }
}

static uint8_t pairedPublicWeapon(unsigned weapon)
{
    return weapon == WEAPON_MAGNUMS ? 2u : weapon == WEAPON_UZIS ? 3u : 1u;
}

static unsigned pairedModel(unsigned weapon)
{
    return weapon == WEAPON_MAGNUMS ? TR2_MODEL_AUTO_ARMS :
           weapon == WEAPON_UZIS ? TR2_MODEL_UZI_ARMS : TR2_MODEL_PISTOL_ARMS;
}

static uint32_t *pairedReserve(unsigned weapon)
{
    return weapon == WEAPON_MAGNUMS ? &gs->tr2->ammo[1] :
           weapon == WEAPON_UZIS ? &gs->tr2->ammo[2] : &gs->tr2->ammo[0];
}

static void meshSwapPaired(Lara *lara, uint32 weaponMask, uint32 bodyMask)
{
    unsigned model = pairedModel(lara->extraL->weapon);
    ItemObj_meshSwap(lara, ITEM_LARA, bodyMask);
    ItemObj_meshSwap(lara, model, weaponMask);
}

static int gunDepth(unsigned weapon)
{
    return (weapon == WEAPON_SHOTGUN || weapon == TR2_WEAPON_GRENADE) ? 500 : 650;
}

static bool gunCanDraw(const Lara *lara)
{
    return lara->waterState == WATER_STATE_ABOVE ||
           (lara->waterState == WATER_STATE_WADE && gs->tr2->waterDistance > -gunDepth(lara->extraL->weapon));
}

/* tr2-lara-arms, Select and draw; tr2-lara-ground-and-weapons, Gun
 * access. Selection requests a draw; it doesn't run an arm update.
 * The text covers a choice made with free hands or a ready gun ("a
 * pending weapon change ... selects holster"); it does not cover a
 * choice made while a gun or flare is being drawn or put away, or
 * while gun access is closed (surface, submerged, deep wade): "changing guns
 * during draw" is not exercised. Assumed, not in the text (Q-ARM-02):
 * such a choice is kept as the goal, as the first game's
 * Lara_changeWeapon keeps it, and the arm update below acts on it when
 * the hands are next ready (holster first) or free (draw once access
 * opens). A later gun choice replaces it; so does a flare request made
 * with free hands while gun access is closed and flares are in stock.
 * Unlike the first game, choosing the gun being put away, or the gun
 * already selected while the hands are busy, draws it again once free. */
bool game_tr2_request_ring_weapon(unsigned weapon)
{
    if (!gs || !TR64_RULE(ARMS) || !gs->tr2) return false;
    Lara *lara=gs->players[0];
    /* extraL is the hands record. A missing one, or a shotgun pose slot
     * that is absent or already live now, must not change the request. */
    if (!lara || !lara->extraL || lara->health<=0) return false;
    ExtraInfoLara *e=lara->extraL;
    unsigned ringType;
    if (weapon==WEAPON_PISTOLS) ringType=TR2_RING_PISTOLS;
    else if (weapon==WEAPON_MAGNUMS) ringType=TR2_RING_AUTO;
    else if (weapon==WEAPON_UZIS) ringType=TR2_RING_UZIS;
    else if (weapon==WEAPON_SHOTGUN) ringType=TR2_RING_SHOTGUN;
    else if (weapon==TR2_WEAPON_GRENADE) ringType=TR2_RING_GRENADE;
    else return false;
    if (!carriedWeapon(ringType)) return false;
    if ((weapon==WEAPON_SHOTGUN || weapon==TR2_WEAPON_GRENADE) && !gs->tr2->shotgunPose) {
        ItemObj *slot=gs->firstFree;
        if (!slot || slot->room || (slot->flags&ITEM_FLAG_ACTIVE)) return false;
    }
    e->goalWeapon=weapon;
    /* Drawing or ready: only a different gun is a change (holster, then
     * draw). Free, busy or putting away: the choice is a draw once free. */
    gs->tr2->arms.gunRequested=(e->weaponState!=WEAPON_STATE_DRAW &&
        e->weaponState!=WEAPON_STATE_READY) || e->weapon!=weapon;
    return true;
}

static void setPistolPose(ExtraInfoLara_Arm *arm, unsigned frame)
{
    arm->animIndex = pistolClip(frame);
    arm->frameIndex = frame;
}

static void setPairedLook(ExtraInfoLara *e, int32 x, int32 y)
{
    e->head.angle.x = e->torso.angle.x = x;
    e->head.angle.y = e->torso.angle.y = y;
}

static void clearPistolAim(ExtraInfoLara *e)
{
    e->armR.aim = e->armL.aim = false;
    e->armR.target = e->armL.target = NULL;
}

static void drawPistols(Lara *lara, bool start)
{
    ExtraInfoLara *e = lara->extraL;
    unsigned frame = start ? PISTOL_DRAW_START : e->armR.frameIndex + 1u;
    if (start) {
        clearPistolAim(e);
        e->armR.flash.timer = e->armL.flash.timer = 0;
        gs->tr2->arms.uziActive[0] = gs->tr2->arms.uziActive[1] = 0;
    }
    e->armR.angle = e->armL.angle = vec3s_create(0, 0, 0);
    if (frame == PISTOL_UNHOLSTER) {
        meshSwapPaired(lara, JOINT_MASK_ARM_R3 | JOINT_MASK_ARM_L3,
                       JOINT_MASK_LEG_R1 | JOINT_MASK_LEG_L1);
        soundPlay(SND_DRAW, &lara->pos);
    }
    if (frame == PISTOL_READY) {
        e->weaponState = WEAPON_STATE_READY;
        frame = 0;
    }
    setPistolPose(&e->armR, frame);
    setPistolPose(&e->armL, frame);
}

static void holsterPistols(Lara *lara, bool looking)
{
    ExtraInfoLara *e = lara->extraL;
    ExtraInfoLara_Arm *arms[] = {&e->armR, &e->armL};
    ItemObj_meshSwap(lara, ITEM_LARA, JOINT_MASK_HEAD);
    gs->tr2->arms.uziActive[0] = gs->tr2->arms.uziActive[1] = 0;
    for (unsigned i = 0; i < 2; ++i) {
        ExtraInfoLara_Arm *arm = arms[i];
        unsigned frame = arm->frameIndex;
        if (frame >= PISTOL_RECOIL)
            frame = PISTOL_AIM_END;
        else if (frame == 0) {
            arm->angle = vec3s_create(0, 0, 0);
            frame = PISTOL_READY;
        } else if (frame <= PISTOL_AIM_END) {
            arm->angle.x -= arm->angle.x / (int)frame;
            arm->angle.y -= arm->angle.y / (int)frame;
            --frame;
        } else if (frame > PISTOL_DRAW_START)
            --frame;
        if (frame == PISTOL_UNHOLSTER - 1) {
            meshSwapPaired(lara, i ? JOINT_MASK_LEG_L1 : JOINT_MASK_LEG_R1,
                           i ? JOINT_MASK_ARM_L3 : JOINT_MASK_ARM_R3);
            soundPlay(SND_HOLSTER, &lara->pos);
        }
        setPistolPose(arm, frame);
    }
    /* tr2-lara-slide-height-and-gaze, Paired-pistol holster and gaze. */
    if (!looking)
        setPairedLook(e, (e->armR.angle.x + e->armL.angle.x) / 4,
                         (e->armR.angle.y + e->armL.angle.y) / 4);
    if (e->armR.frameIndex == PISTOL_DRAW_START && e->armL.frameIndex == PISTOL_DRAW_START) {
        e->armR.frameIndex = e->armL.frameIndex = 0;
        clearPistolAim(e);
        e->weaponState = WEAPON_STATE_FREE;
    }
}

static Location pistolOrigin(const Lara *lara)
{
    Location from;
    GAME_WEAPON_ORIGIN(from,lara,650);
    return from;
}

static bool pistolAimAt(Lara *lara,const Location *from,ItemObj *target,vec3s *angle,bool retained)
{
    /* tr2-lara-arms, Pistol lock and creature strikes, Q-T2K-05. */
    const AABBs *box=ItemObj_getBoundingBox(target,false);
    int32 x=(box->minX+box->maxX)/2, z=(box->minZ+box->maxZ)/2;
    int32 s=tr2_sin(target->angle.y), c=tr2_sin(target->angle.y+ANGLE_90);
    Location to={.room=target->room,.pos=vec3i_create(
        i32_add(target->pos.x,(x*c+z*s)>>14),
        i32_add(target->pos.y,box->minY+(box->maxY-box->minY)/3),
        i32_add(target->pos.z,(z*c-x*s)>>14))};
    vec3i dir=vec3i_sub(to.pos,from->pos);
    bool visible=game_tr2_sight(from,&to,true);
    if (!visible && !retained) return false;
    angle->y=wrap_i16(game_tr2_bearing(dir.z,dir.x)-lara->angle.y);
    while (dir.x<INT16_MIN || dir.x>INT16_MAX || dir.y<INT16_MIN || dir.y>INT16_MAX ||
           dir.z<INT16_MIN || dir.z>INT16_MAX) {
        dir.x>>=2; dir.y>>=2; dir.z>>=2;
    }
    angle->x=wrap_i16(-game_tr2_bearing(game_tr2_horizontal_length(dir.x,dir.z),dir.y)-lara->angle.x);
    /* Combat uses this bearing while the item stays selected, even
     * when sight drops both locks (tr2-camera, Q-T2C-13). */
    return visible;
}

static ItemObj *selectTarget(Lara *lara,const Location *from,int pitchCone)
{
    int32 least = 8192 * 8192, bestYaw = 10920;
    ItemObj *target = NULL;
    for (unsigned slot=0;slot<TR2_CREATURE_SLOTS;++slot) {
        ItemObj *candidate=gs->tr2->creatures[slot].item;
        if (!candidate || candidate==lara || candidate->health<=0)
            continue;
        int64_t x = (int64_t)candidate->pos.x - from->pos.x;
        int64_t y = (int64_t)candidate->pos.y - from->pos.y;
        int64_t z = (int64_t)candidate->pos.z - from->pos.z;
        if (x < -8192 || x > 8192 || y < -8192 || y > 8192 || z < -8192 || z > 8192)
            continue;
        int32 dist = x*x + y*y + z*z;
        if (dist >= least)
            continue;
        vec3s angle = {0};
        if (!pistolAimAt(lara,from,candidate,&angle,false))
            continue;
        int32 pitch = abs(wrap_i16((int32)angle.x - lara->extraL->torso.angle.x));
        int32 yaw = abs(wrap_i16((int32)angle.y - lara->extraL->torso.angle.y));
        if (pitch > pitchCone || yaw > 10920 || yaw >= bestYaw + 2730)
            continue;
        target = candidate;
        least = dist;
        bestYaw = yaw;
    }
    return target;
}

static void updatePistolAim(Lara *lara, const Location *from, bool action)
{
    ExtraInfoLara *e = lara->extraL;
    ExtraInfoLara_Arm *r = &e->armR, *l = &e->armL;
    ItemObj *target = r->target;
    if (!action || !target || !target->room)
        target = selectTarget(lara,from,10920);
    r->target = l->target = target;
    vec3s aim = {0};
    if (!target || !pistolAimAt(lara,from,target,&aim,true)) {
        r->aim = l->aim = false;
    } else if (abs(aim.x) <= 10920 && abs(aim.y) <= 10920) {
        r->aim = l->aim = true;
    } else {
        bool pitch = abs(aim.x) <= 14560;
        r->aim = r->aim && pitch && aim.y >= -10920 && aim.y <= 30940;
        l->aim = l->aim && pitch && aim.y >= -30940 && aim.y <= 10920;
    }
    r->angleAim = l->angleAim = aim;
    game_weapon_aim(r);
    game_weapon_aim(l);
    if (r->aim || l->aim) {
        const ExtraInfoLara_Arm *locked = r->aim ? r : l;
        if (r->aim && l->aim)
            setPairedLook(e, (r->angle.x + l->angle.x) / 4, (r->angle.y + l->angle.y) / 4);
        else
            setPairedLook(e, locked->angle.x / 2, locked->angle.y / 2);
    }
}

/* Static order: tr2-lara-arms, Pistol attempts and random draws. */
vec3s game_tr2_pistol_ray_angles(int16 armPitch, int16 armYaw, int16 bodyYaw)
{
    int32 pitch = armPitch + game_weapon_spread(1456,true);
    int32 yaw = bodyYaw + armYaw + game_weapon_spread(1456,true);
    return vec3s_create(wrap_i16(pitch),wrap_i16(yaw),0);
}

/* Shared sphere hit, damage, blood and terrain for pistol bullets and pellets. */
static void resolveRay(ItemObj *target,const Location *from,vec3s ray,int damage)
{
    GameTR2 *tr2=gs->tr2;
    ++tr2->arms.ammoUsed;
    if (target) {
        vec3i point;
        if (game_tr2_ray_hit(target,&from->pos,ray.x,ray.y,&point)) {
            if (target->health>0 && target->health<=damage)
                ++gs->gSaveGame.kills;
            target->health=X_MAX(INT16_MIN,target->health-damage);
            target->flags |= ITEM_FLAG_INJURED;
            ++tr2->ammoHits;
            if (TR64_RULE(ROOM_EFFECTS))
                game_tr2_shot_effect(TR2_FX_BLOOD,target->room,point,target->hSpeed,target->angle.y);
            if (tr2->urbanUnit) {
                Location hitEnd = {.room = target->room, .pos = point};
                const FloorData *floorData = gs->gLastFloorData;
                uint16 slant = gs->gLastFloorSlant;
                TR2UrbanCall ucall = {.op = TR2_URBAN_WINDOW_RAY, .from = from, .to = &hitEnd};
                (void)code_call("tr2_urban", (uint32_t)(uintptr_t)&ucall);
                gs->gLastFloorData = floorData;
                gs->gLastFloorSlant = slant;
            }
            return;
        }
    }
    int32 sx=tr2_sin(ray.x), cx=tr2_sin(ray.x+ANGLE_90);
    int32 sy=tr2_sin(ray.y), cy=tr2_sin(ray.y+ANGLE_90);
    /* Q-T2K-10: normalize to the signed 14-bit direction before
     * projecting the spread-adjusted ray over its 8192-unit range. */
    Location end = {.room=from->room, .pos=vec3i_add(from->pos,
                    vec3i_create(((cx*sy)>>14)>>1,(-sx)>>1,((cx*cy)>>14)>>1))};
    const FloorData *floorData = gs->gLastFloorData;
    uint16 slant = gs->gLastFloorSlant;
    bool clear = game_tr2_sight(from,&end,true);
    bool hitWindow = false;
    if (tr2->urbanUnit) {
        TR2UrbanCall ucall = {.op = TR2_URBAN_WINDOW_RAY, .from = from, .to = &end};
        hitWindow = code_call("tr2_urban", (uint32_t)(uintptr_t)&ucall) != 0;
    }
    if (!clear && !hitWindow)
        game_tr2_shot_effect(TR2_FX_RICOCHET,end.room,end.pos,0,0);
    gs->gLastFloorData = floorData;
    gs->gLastFloorSlant = slant;
}

static void firePistol(Lara *lara,ExtraInfoLara_Arm *arm,const Location *from)
{
    gs->tr2->ammo[0]=999; /* Reset unlimited pistols to 1000 before the spend. */
    const vec3s ray=game_tr2_pistol_ray_angles(arm->angle.x,arm->angle.y,lara->angle.y);
    arm->flash.timer=3;
    resolveRay(arm->target,from,ray,1);
}

static void fireShotgun(Lara *lara,const ExtraInfoLara_Arm *arm)
{
    Location from;
    GAME_WEAPON_ORIGIN(from,lara,500);
    /* The shell attempts all six rays; a spent pellet takes the
     * ordinary zero-amplitude pitch then yaw draws after its outer pair. */
    for (unsigned i=0;i<WEAPON_SHOTGUN_PELLETS;++i) {
        int32 yaw=lara->angle.y+arm->angle.y+game_weapon_spread(3640,true);
        int32 pitch=arm->angle.x+game_weapon_spread(3640,true);
        if (!gs->tr2->ammo[3]) continue;
        --gs->tr2->ammo[3];
        (void)rand_logic();
        (void)rand_logic();
        resolveRay(arm->target,&from,vec3s_create(wrap_i16(pitch),wrap_i16(yaw),0),3);
    }
    lara->extraL->armR.flash.timer=3;
}

static void aimShotgun(Lara *lara,bool action)
{
    ExtraInfoLara *e=lara->extraL;
    ExtraInfoLara_Arm *arm=&e->armL;
    Location from=pistolOrigin(lara);
    ItemObj *target=arm->target;
    if (!action || !target || !target->room) target=selectTarget(lara,&from,10010);
    arm->target=e->armR.target=target;
    vec3s aim={0};
    if (!target || !pistolAimAt(lara,&from,target,&aim,true)) arm->aim=false;
    else if (abs(aim.x)<=10010 && abs(aim.y)<=10920) arm->aim=true;
    else arm->aim=arm->aim && abs(aim.x)<=11830 && abs(aim.y)<=14560;
    e->armR.aim=arm->aim;
    e->armR.angleAim=arm->angleAim=aim;
    game_weapon_aim(arm);
    if (arm->aim) {
        e->torso.angle.x=arm->angle.x;
        e->torso.angle.y=arm->angle.y;
        e->head.angle.x=e->head.angle.y=0;
    }
}

__attribute__((weak)) void tr64_shotgun_test_arm(ExtraInfoLara *e, bool action)
{
    (void)e;
    (void)action;
}

static void shotgunRequest(ItemObj *pose, unsigned from, unsigned to, unsigned kind, unsigned frameIndex)
{
    if (game_tr2_shotgun_pick(from, to, kind) >= SHOTGUN_CLIPS)
        sys_error("Tomb Raider II: shotgun request is not in the shared table");
    uint8 goal = 0;
    if (!game_tr2_shotgun_goal(from, to, kind, frameIndex, &goal))
        sys_error("Tomb Raider II: shotgun request is not in the shared table");
    pose->goalState = goal;
}

static void shotgunTick(Lara *lara,bool start,bool action)
{
    GameTR2 *tr2=gs->tr2;
    ExtraInfoLara *e=lara->extraL;
    const unsigned base=gs->level.models[TR2_MODEL_SHOTGUN].animIndex;
    ItemObj *pose=tr2->shotgunPose;
    if (start) {
        pose=gs->firstFree;
        if (!pose) sys_error("Tomb Raider II: shotgun pose has no free item slot");
        gs->firstFree=pose->nextItem;
        ItemObj_resetSlot(pose);
        pose->type=TR2_MODEL_SHOTGUN;
        pose->pos=lara->pos;
        ItemObj_animSet(pose,base+SHOTGUN_DRAW,true,0);
        tr2->shotgunPose=pose;
        tr2->lastWeapon=4; /* Public selection number. */
    }
    unsigned clip=pose->animIndex-base;
    unsigned frame=pose->frameIndex-tr64_u16(gs->level.anims[pose->animIndex].frame_start);
    unsigned to=SHOTGUN_CLIPS;
    bool locked=false;
    if (e->weaponState==WEAPON_STATE_READY) {
        if (action && !tr2->ammo[3]) {
            e->goalWeapon=carriedWeapon(TR2_RING_PISTOLS) ? WEAPON_PISTOLS : WEAPON_NONE;
            soundPlay(SND_EMPTY,&lara->pos);
            return; /* Keep the selected target and its old sight for Combat. */
        }
        aimShotgun(lara,action);
        tr64_shotgun_test_arm(e, action);
        bool aimed=game_weapon_aim_eligible(&e->armL,action);
        locked=e->armL.aim!=0;
        bool hasTarget=e->armL.target!=NULL;
        unsigned from=clip, kind=TR2_SG_FRAME0;
        if (clip==SHOTGUN_AIM) {
            to=aimed?SHOTGUN_RECOIL:SHOTGUN_LOWER;
            kind=TR2_SG_GOAL;
        } else if (clip==SHOTGUN_RECOIL) {
            /* tr2-lara-arms.md:85. Not the first frame, Action released
             * and no lock: set goal 4, and repeat that write. A re-press
             * does not clear it, and neither does a lock gained then.
             * The first frame fires, sets goal 4 into the lower clip, or
             * sets goal 0 so this clip loops. A latched goal 4 is left. */
            if (!frame) {
                if (action && (locked || !hasTarget))
                    to=SHOTGUN_RECOIL;
                else if (action && hasTarget && !locked) {
                    to=SHOTGUN_LOWER;
                    kind=TR2_SG_CITED4;
                } else if (!action && locked) {
                    to=SHOTGUN_RECOIL;
                    kind=TR2_SG_LOOP0;
                }
            } else if (!action && !locked) {
                to=SHOTGUN_AIM;
                kind=TR2_SG_CITED4;
            }
        }
        if (to<SHOTGUN_CLIPS)
            shotgunRequest(pose, from, to, kind, pose->frameIndex);
    } else if (e->weaponState==WEAPON_STATE_HOLSTER) {
        to=SHOTGUN_STOW;
        shotgunRequest(pose, TR2_SHOTGUN_ANY, to, TR2_SG_GOAL, pose->frameIndex);
    }
    if (to<SHOTGUN_CLIPS) {
        if (to==SHOTGUN_RECOIL && clip==SHOTGUN_RECOIL && !frame && action)
            fireShotgun(lara,&e->armL);
    } else if (e->weaponState==WEAPON_STATE_HOLSTER ||
               (e->weaponState==WEAPON_STATE_READY && clip==SHOTGUN_AIM) ||
               (e->weaponState==WEAPON_STATE_READY && clip==SHOTGUN_RECOIL &&
                !frame && (action || locked)))
        sys_error("Tomb Raider II: shotgun request is not in the shared table");
    pose->pos=lara->pos;
    ItemObj_animProcess(pose,false);
    clip=pose->animIndex-base;
    frame=pose->frameIndex-tr64_u16(gs->level.anims[pose->animIndex].frame_start);
    if (e->weaponState==WEAPON_STATE_DRAW) {
        if (clip==SHOTGUN_DRAW && frame==10) {
            Lara_meshSwapShotgun(lara,true);
        } else if (clip==SHOTGUN_AIM) {
            e->weaponState=WEAPON_STATE_READY;
            clearPistolAim(e);
            e->armL.angle=e->armR.angle=vec3s_create(0,0,0);
        }
    } else if (e->weaponState==WEAPON_STATE_HOLSTER && clip==SHOTGUN_STOW && frame==21) {
        Lara_meshSwapShotgun(lara,false);
    }
    game_weapon_pose_pair(e,pose->animIndex,frame);
    if ((pose->flags&ITEM_FLAG_STATUS)==ITEM_FLAG_STATUS_INACTIVE) {
        clearPistolAim(e);
        e->weaponState=WEAPON_STATE_FREE;
        tr2->shotgunPose=NULL;
        pose->nextItem=gs->firstFree;
        gs->firstFree=pose;
    }
}

/* tr2-opera-house.md §10: Grenade Launcher (model 7, weapon 6, projectile 248). */
enum {
    GRENADE_DRAW = 0, GRENADE_AIM = 1, GRENADE_RELOAD = 2,
    GRENADE_FIRE = 3, GRENADE_LOWER = 4, GRENADE_STOW = 5
};

static void __attribute__((optimize("Os"))) fireGrenade(Lara *lara, const ExtraInfoLara_Arm *arm, const ItemObj *pose)
{
    GameTR2 *tr2 = gs->tr2;
    if (!tr2->ammo[5])
        return;
    ItemObj *proj = gs->firstFree;
    if (!proj)
        return;
    gs->firstFree = proj->nextItem;
    ItemObj_resetSlot(proj);
    proj->type = TR2_ITEM_GRENADE_PROJ;
    proj->pos = game_tr2_lara_right_hand(lara, pose, vec3i_create(-2, 373, 77));
    proj->angle.x = wrap_i16((int32)lara->angle.x + arm->angle.x);
    proj->angle.y = wrap_i16((int32)lara->angle.y + arm->angle.y);
    proj->angle.z = 0;
    proj->hSpeed = 200;
    proj->vSpeed = 0;
    proj->health = -16384;
    proj->visibleMask = UINT32_MAX;
    proj->flags = ITEM_FLAG_STATUS_ACTIVE;
    if (gs->level.models[TR2_ITEM_GRENADE_PROJ].animIndex < tr2->animCount)
        ItemObj_animSet(proj, gs->level.models[TR2_ITEM_GRENADE_PROJ].animIndex, true, 0);
    Room_add(lara->room, proj);
    ItemObj_activateBase(proj);
    --tr2->ammo[5];
    ++tr2->arms.ammoUsed;
}

static void __attribute__((optimize("Os"))) grenadeTick(Lara *lara, bool start, bool action)
{
    GameTR2 *tr2 = gs->tr2;
    ExtraInfoLara *e = lara->extraL;
    const unsigned base = gs->level.models[TR2_MODEL_GRENADE].animIndex;
    ItemObj *pose = tr2->shotgunPose;
    if (start) {
        pose = gs->firstFree;
        if (!pose)
            sys_error("Tomb Raider II: grenade pose has no free item slot");
        gs->firstFree = pose->nextItem;
        ItemObj_resetSlot(pose);
        pose->type = TR2_MODEL_GRENADE;
        pose->pos = lara->pos;
        ItemObj_animSet(pose, base + GRENADE_DRAW, true, 0);
        tr2->shotgunPose = pose;
        tr2->lastWeapon = 6;
    }
    unsigned clip = pose->animIndex - base;
    unsigned frame = pose->frameIndex - tr64_u16(gs->level.anims[pose->animIndex].frame_start);
    if (e->weaponState == WEAPON_STATE_READY) {
        if (action && !tr2->ammo[5]) {
            e->goalWeapon = carriedWeapon(TR2_RING_PISTOLS) ? WEAPON_PISTOLS : WEAPON_NONE;
            soundPlay(SND_EMPTY, &lara->pos);
            return;
        }
        aimShotgun(lara, action);
        bool locked = e->armL.aim != 0;
        bool hasTarget = e->armL.target != NULL;
        bool aimed = game_weapon_aim_eligible(&e->armL, action);
        if (clip == GRENADE_AIM) {
            pose->goalState = aimed ? 2 : 4;
        } else if (clip == GRENADE_FIRE && frame == 0) {
            if (action && (locked || !hasTarget) && tr2->ammo[5] > 0 &&
                lara->waterState != WATER_STATE_UNDER) {
                fireGrenade(lara, &e->armL, pose);
                pose->goalState = 2;
            } else if (!action && locked) {
                pose->goalState = 0;
            } else {
                pose->goalState = 4;
            }
        }
    } else if (e->weaponState == WEAPON_STATE_HOLSTER) {
        pose->goalState = 3;
    }
    pose->pos = lara->pos;
    ItemObj_animProcess(pose, false);
    clip = pose->animIndex - base;
    frame = pose->frameIndex - tr64_u16(gs->level.anims[pose->animIndex].frame_start);
    if (e->weaponState == WEAPON_STATE_DRAW) {
        if (clip == GRENADE_DRAW && frame == 10u) {
            tr2->backWeapon = 0;
            ItemObj_meshSwap(lara, TR2_MODEL_GRENADE, JOINT_MASK_ARM_R3);
        } else if (clip == GRENADE_AIM) {
            e->weaponState = WEAPON_STATE_READY;
            clearPistolAim(e);
            e->armL.angle = e->armR.angle = vec3s_create(0, 0, 0);
        }
    } else if (e->weaponState == WEAPON_STATE_HOLSTER && clip == GRENADE_STOW && frame == 21u) {
        tr2->backWeapon = TR2_MODEL_GRENADE;
        ItemObj_meshSwap(lara, ITEM_LARA, JOINT_MASK_ARM_R3);
    }
    game_weapon_pose_pair(e, pose->animIndex, frame);
    if ((pose->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        clearPistolAim(e);
        e->weaponState = WEAPON_STATE_FREE;
        tr2->shotgunPose = NULL;
        pose->nextItem = gs->firstFree;
        gs->firstFree = pose;
    }
}

static void readyPistols(Lara *lara, bool action)
{
    ExtraInfoLara *e = lara->extraL;
    TR2Arms *tr2Arms = &gs->tr2->arms;
    uint32_t *reserve = pairedReserve(e->weapon);
    if (action && e->weapon != WEAPON_PISTOLS && *reserve == 0) {
        soundPlay(SND_EMPTY, &lara->pos);
        e->goalWeapon = carriedWeapon(TR2_RING_PISTOLS) ? WEAPON_PISTOLS : WEAPON_NONE;
        return;
    }
    if (action && (e->weapon == WEAPON_PISTOLS || *reserve > 0)) {
        if (gs->level.models[TR2_MODEL_UZI_ARMS].count == JOINT_MAX)
            ItemObj_meshSwap(lara, TR2_MODEL_UZI_ARMS, JOINT_MASK_HEAD);
    } else {
        ItemObj_meshSwap(lara, ITEM_LARA, JOINT_MASK_HEAD);
    }
    const Location from = pistolOrigin(lara);
    updatePistolAim(lara, &from, action);
    ExtraInfoLara_Arm *arms[] = {&e->armR,&e->armL};
    const bool uzi = e->weapon == WEAPON_UZIS;
    const unsigned recoilTicks = uzi ? 3u : PISTOL_RECOIL_TICKS;
    const int damage = e->weapon == WEAPON_MAGNUMS ? 2 : 1;
    const int shotSound = e->weapon == WEAPON_MAGNUMS ? 21 : SND_PISTOLS_SHOT;
    bool fired = false, uziShotPlayed = false;
    for (unsigned i = 0; i < 2; ++i) {
        ExtraInfoLara_Arm *arm = arms[i];
        unsigned frame = arm->frameIndex;
        bool shooting = action && (arm->aim || !arm->target);
        bool raised = arm->aim || shooting;
        if (frame >= PISTOL_RECOIL) {
            /* OpenLara fixed/lara.h 3684–3720: leaving the aim branch
             * drops recoil straight back to the last aim pose. */
            if (!raised) {
                frame = PISTOL_AIM_END;
                if (uzi && tr2Arms->uziActive[i]) {
                    soundPlay(44, &lara->pos);
                    tr2Arms->uziActive[i] = 0;
                }
            } else {
                if (uzi) {
                    soundPlay(SND_UZIS_SHOT, &lara->pos);
                    tr2Arms->uziActive[i] = 1;
                }
                if (++frame == PISTOL_RECOIL + recoilTicks)
                    frame = PISTOL_AIM_END;
            }
        } else if (raised) {
            if (frame < PISTOL_AIM_END)
                ++frame;
            else if (shooting) {
                if (e->weapon == WEAPON_PISTOLS) {
                    firePistol(lara, arm, &from);
                    fired = true;
                } else if (*reserve > 0) {
                    --(*reserve);
                    const vec3s ray = game_tr2_pistol_ray_angles(arm->angle.x, arm->angle.y, lara->angle.y);
                    arm->flash.timer = 3;
                    resolveRay(arm->target, &from, ray, damage);
                    if (uzi) {
                        if (!uziShotPlayed) {
                            soundPlay(SND_UZIS_SHOT, &lara->pos);
                            uziShotPlayed = true;
                        }
                        tr2Arms->uziActive[i] = 1;
                    } else {
                        fired = true;
                    }
                }
                frame = PISTOL_RECOIL;
            } else if (uzi && tr2Arms->uziActive[i]) {
                soundPlay(44, &lara->pos);
                tr2Arms->uziActive[i] = 0;
            }
        } else {
            if (uzi && tr2Arms->uziActive[i]) {
                soundPlay(44, &lara->pos);
                tr2Arms->uziActive[i] = 0;
            }
            if (frame)
                --frame;
        }
        setPistolPose(arm, frame);
    }
    if (fired)
        soundPlay(shotSound, &lara->pos);
}

static TR2RingEntry *flareStock(void)
{
    for (unsigned i = 0; i < gs->tr2->carriedCount; ++i)
        if (gs->tr2->carried[i].type == TR2_RING_FLARES)
            return gs->tr2->carried + i;
    sys_error("Tomb Raider II: no carried flare slot");
    return NULL;
}

static void setFlarePose(ExtraInfoLara_Arm *arm, unsigned frame)
{
    arm->animIndex = flareClip(frame);
    arm->frameIndex = frame;
}

/* Eligible held overlays, from tr2-water-and-flares, Movement or occupied hands. */
static bool flareOverlay(unsigned state)
{
    switch (state) {
    case 0: case 2: case 4: case 6: case 7: case 16: case 20: case 21: case 22:
    case 39: case 40: case 41: case 65:
        return true;
    default:
        return false;
    }
}

static void advanceHeldFlare(Lara *lara)
{
    TR2Arms *arms = &gs->tr2->arms;
    if (!arms->flareLit)
        return;
    lara->extraL->armL.flash.timer = flareLight(arms->flareAge);
    if (arms->flareAge < FLARE_BURN_END)
        ++arms->flareAge;
}

static void drawFlare(Lara *lara, bool start)
{
    TR2Arms *arms = &gs->tr2->arms;
    ExtraInfoLara *e = lara->extraL;
    if (lara->state == Lara_STATE_PICKUP || lara->state == Lara_STATE_PICKUP_FLARE) {
        setFlarePose(&e->armL, FLARE_DRAW_END - 1);
        arms->flareLeft = false;
        advanceHeldFlare(lara);
        return;
    }
    unsigned frame = start ? FLARE_DRAW_START : e->armL.frameIndex + 1u;
    arms->flareLeft = true;
    if (frame == FLARE_COST) {
        --flareStock()->quantity;
        ItemObj_meshSwap(lara, TR2_MODEL_FLARE_ARMS, JOINT_MASK_ARM_L3);
    }
    if (frame == FLARE_IGNITION) {
        arms->flareAge = 0;
        arms->flareLit = true;
        soundPlay(11, &lara->pos);
    }
    if (frame == FLARE_DRAW_END) {
        frame = FLARE_REST;
        clearPistolAim(e);
        e->weaponState = WEAPON_STATE_FREE;
    }
    setFlarePose(&e->armL, frame);
    advanceHeldFlare(lara);
}

/* tr2-lara-arms, Spawn, travel and retirement. The controller stores
 * age in headOffset and flame visibility in aggression, independent
 * of the generic item timer (which is still a trigger countdown). */
static void releaseFlare(Lara *lara, bool thrown)
{
    TR2Arms *arms = &gs->tr2->arms;
    ItemObj *item = gs->firstFree;
    if (item) {
        gs->firstFree = item->nextItem;
        ItemObj_resetSlot(item);
        item->type = TR2_ITEM_LOOSE_FLARE;
        ItemObj_animSet(item, gs->level.models[item->type].animIndex, true, 0);
        item->health = -16384;
        item->visibleMask = UINT32_MAX;
        item->flags = ITEM_FLAG_COLLISION | ITEM_FLAG_STATUS_ACTIVE;
        item->pos = game_tr2_flare_hand(lara, arms->flareLeft);
        item->angle.y = wrap_i16((int32)lara->angle.y - (thrown ? 0 : ANGLE_45));
        item->hSpeed = wrap_i16(lara->hSpeed + (thrown ? 50 : 10));
        item->vSpeed = wrap_i16(lara->vSpeed + (thrown ? -50 : 50));
        item->headOffset = arms->flareAge;
        Room *room = Room_getRoom(lara->room, item->pos.x, item->pos.y, item->pos.z);
        const Sector *cell = Room_getSector(room, item->pos.x, item->pos.z);
        int32 floor = Sector_getFloor(cell, item->pos.x, item->pos.y, item->pos.z);
        if (item->pos.y > floor) {
            item->pos.x = lara->pos.x;
            item->pos.z = lara->pos.z;
            item->angle.y = wrap_i16(-(int32)lara->angle.y);
            room = lara->room;
            cell = Room_getSector(room, item->pos.x, item->pos.z);
        }
        item->roomFloor = cell->floor * 256;
        Room_add(room, item);
        ItemObj_activateBase(item);
    }
    ItemObj_meshSwap(lara, ITEM_LARA, JOINT_MASK_ARM_L3);
    arms->flareLit = false;
    lara->extraL->armL.flash.timer = 0;
}

static void finishFlareStow(Lara *lara, bool body)
{
    ExtraInfoLara *e = lara->extraL;
    TR2Arms *arms = &gs->tr2->arms;
    if (body) {
        ItemObj_animSet(lara, Lara_ANIM_STAND, false, 0);
        arms->flareFrame = lara->frameIndex;
    } else
        arms->flareFrame = 0;
    if (arms->flareLit) {
        ItemObj_meshSwap(lara, ITEM_LARA, JOINT_MASK_ARM_L3);
        arms->flareLit = false;
        e->armL.flash.timer = 0;
    }
    e->armL.frameIndex = 0;
    clearPistolAim(e);
    e->weapon = rememberedGun();
    /* Q-ARM-02 (assumed): a ring choice made during the stow survives it. */
    if (!arms->gunRequested)
        e->goalWeapon = e->weapon;
    e->weaponState = WEAPON_STATE_FREE;
    arms->flareLeft = false;
}

static void stowFlare(Lara *lara)
{
    ExtraInfoLara_Arm *arm = &lara->extraL->armL;
    TR2Arms *arms = &gs->tr2->arms;
    unsigned frame = arm->frameIndex;
    unsigned begin = tr64_u16(gs->level.anims[FLARE_BODY_THROW].frame_start);
    if (lara->goalState == Lara_STATE_STOP && lara->animIndex == Lara_ANIM_STAND_NORMAL) {
        ItemObj_animSet(lara, FLARE_BODY_THROW, false, frame);
        arms->flareFrame = lara->frameIndex;
    }
    if (lara->animIndex == FLARE_BODY_THROW) {
        if (arms->flareFrame >= begin + FLARE_BODY_THROW_FINISH) {
            finishFlareStow(lara, true);
            return;
        }
        arms->flareLeft = false;
        ++arms->flareFrame;
    }
    if (frame == FLARE_DRAW_END - 1 || frame == FLARE_RAISE_END)
        frame = 1;
    else
        ++frame;
    if (frame == FLARE_DRAW_START) {
        finishFlareStow(lara, false);
        return;
    }
    if (frame == FLARE_RELEASE)
        releaseFlare(lara, true);
    else
        advanceHeldFlare(lara);
    setFlarePose(arm, frame);
}

void game_tr2_update_arms(Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    TR2Arms *arms = &tr2->arms;
    ExtraInfoLara *e = lara->extraL;
    unsigned raw = tr2->input;
    bool draw = (raw & IN_WEAPON) != 0;
    bool flare = (raw & TR2_IN_FLARE) != 0 || tr2->ringFlare;
    tr2->ringFlare=false;
    arms->firearmsView = false;
    if (e->armR.flash.timer)
        --e->armR.flash.timer;
    if (e->weapon != TR2_WEAPON_FLARE && e->armL.flash.timer)
        --e->armL.flash.timer;
    if (lara->health <= 0)
        return;

    if (e->weapon == TR2_WEAPON_FLARE) {
        if (e->weaponState == WEAPON_STATE_DRAW) {
            drawFlare(lara, false);
            return;
        }
        if (e->weaponState == WEAPON_STATE_FREE && (draw || arms->gunRequested) && gunCanDraw(lara)) {
            releaseFlare(lara, false);
            arms->flareLeft = false;
            e->weapon = e->goalWeapon = arms->gunRequested ? e->goalWeapon : rememberedGun();
            arms->gunRequested = false;
            e->weaponState = WEAPON_STATE_DRAW;
            arms->firearmsView = true;
            if (e->weapon==WEAPON_SHOTGUN) shotgunTick(lara,true,false);
            else if (e->weapon==TR2_WEAPON_GRENADE) grenadeTick(lara,true,false);
            else {
                tr2->lastWeapon=pairedPublicWeapon(e->weapon);
                drawPistols(lara,true);
            }
            return;
        }
        if (e->weaponState == WEAPON_STATE_FREE &&
            ((!draw && flare) || arms->flareAge == FLARE_BURN_END))
            e->weaponState = WEAPON_STATE_HOLSTER;
        if (e->weaponState == WEAPON_STATE_HOLSTER) {
            stowFlare(lara);
            return;
        }
        bool overlay = flareOverlay(lara->state);
        if (overlay && e->weaponState == WEAPON_STATE_FREE) {
            unsigned frame = e->armL.frameIndex;
            if (!arms->flareLeft)
                frame = FLARE_RAISE;
            else if (frame >= FLARE_RAISE)
                frame = frame == FLARE_RAISE_END ? FLARE_REST : frame + 1;
            setFlarePose(&e->armL, frame);
        }
        arms->flareLeft = overlay;
        advanceHeldFlare(lara);
        return;
    }

    if (!arms->gunRequested) {
        if (draw && e->weaponState == WEAPON_STATE_FREE)
            e->goalWeapon = rememberedGun();
        else if (flare && (e->weaponState == WEAPON_STATE_FREE || e->weaponState == WEAPON_STATE_READY) &&
                 flareStock()->quantity)
            e->goalWeapon = TR2_WEAPON_FLARE;
    } else if (flare && e->weaponState == WEAPON_STATE_FREE && !gunCanDraw(lara) &&
               flareStock()->quantity) {
        /* Q-ARM-02 (assumed): a flare request replaces a gun choice that
         * is still waiting for gun access (tr2-water-and-flares: a flare
         * can be drawn in water). */
        arms->gunRequested = false;
        e->goalWeapon = TR2_WEAPON_FLARE;
    }
    if (e->weaponState == WEAPON_STATE_FREE) {
        if (e->goalWeapon == TR2_WEAPON_FLARE) {
            e->weapon = TR2_WEAPON_FLARE;
            e->weaponState = WEAPON_STATE_DRAW;
            drawFlare(lara, true);
        } else if ((draw || arms->gunRequested || e->goalWeapon!=e->weapon) &&
                   e->goalWeapon!=WEAPON_NONE && gunCanDraw(lara)) {
            e->weapon=e->goalWeapon;
            arms->gunRequested = false;
            e->weaponState = WEAPON_STATE_DRAW;
            arms->firearmsView = true;
            if (e->weapon==WEAPON_SHOTGUN) shotgunTick(lara,true,false);
            else if (e->weapon==TR2_WEAPON_GRENADE) grenadeTick(lara,true,false);
            else {
                tr2->lastWeapon=pairedPublicWeapon(e->weapon);
                drawPistols(lara,true);
            }
        }
        return;
    }
    if (e->weaponState == WEAPON_STATE_READY && (draw || e->goalWeapon != e->weapon ||
        lara->waterState == WATER_STATE_SURFACE || lara->waterState == WATER_STATE_UNDER ||
        (lara->waterState == WATER_STATE_WADE && tr2->waterDistance < -gunDepth(e->weapon))))
        e->weaponState = WEAPON_STATE_HOLSTER;
    if ((e->weapon==WEAPON_SHOTGUN || e->weapon==TR2_WEAPON_GRENADE) &&
        (e->weaponState==WEAPON_STATE_DRAW ||
         e->weaponState==WEAPON_STATE_READY || e->weaponState==WEAPON_STATE_HOLSTER)) {
        arms->firearmsView=e->weaponState!=WEAPON_STATE_HOLSTER;
        if (e->weapon==WEAPON_SHOTGUN)
            shotgunTick(lara,false,(raw&IN_ACTION)!=0);
        else
            grenadeTick(lara,false,(raw&IN_ACTION)!=0);
        return;
    }
    switch (e->weaponState) {
    case WEAPON_STATE_DRAW:
        arms->firearmsView = true;
        drawPistols(lara, false);
        break;
    case WEAPON_STATE_READY:
        arms->firearmsView = true;
        readyPistols(lara, (raw & IN_ACTION) != 0);
        break;
    case WEAPON_STATE_HOLSTER:
        holsterPistols(lara, (raw & IN_LOOK) != 0);
        break;
    }
}
