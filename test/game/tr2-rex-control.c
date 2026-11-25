/* SPDX-License-Identifier: MIT */
/* Directed test-only links, not part of any sealed comparison. */
#include "game/tr2/creature.h"
#include <stdio.h>

void __real_game_tr2_tick(uint32);

static bool partialShell;

static void fatalLunge(void)
{
    ItemObj *rex=gs->items+104;
    if (!game_tr2_activate_item(rex)) sys_error("fatal lunge control: wake refused");
    rex->state=7;
    rex->hitMask=1u<<12;
    game_tr2_update_creature(rex,gs->players[0]);
    sys_error("fatal lunge control returned without a refusal");
}

static void checkPartialShell(uint32 mask)
{
    GameTR2 *tr2=gs->tr2;
    uint32 before=tr2->arms.ammoUsed;
    uint32 expected=gs->gRandSeedLogic;
    if (before || !(mask&IN_ACTION) || !tr2->shotgunPose || tr2->ammo[3]!=12)
        sys_error("partial shell: expected the first charged and ready shell");
    tr2->ammo[3]=2;
    /* Six outer pairs and the two issued pellets' inner pairs. */
    for (unsigned i=0;i<16;++i) expected=expected*UINT32_C(0x41c64e6d)+UINT32_C(0x3039);
    __real_game_tr2_tick(mask);
    if (tr2->ammo[3] || tr2->arms.ammoUsed!=before+2 || gs->gRandSeedLogic!=expected)
        sys_error("partial shell: wrong reserve, issued rounds or gameplay draws");
    puts("PASS partial shell issues two pellets and advances gameplay RNG 16 times; unsealed");
    exit(0);
}

static void bitePose(void)
{
    ItemObj *tiger=gs->items+18;
    if (!game_tr2_activate_item(tiger)) sys_error("tiger bite pose control: wake refused");
    TR2Creature *ai=game_tr2_creature_slot(tiger);
    if (!ai) sys_error("tiger bite pose control: wake did not acquire a slot");
    int16 prior=6000;
    ai->jointYaw[0]=prior;
    tiger->hitMask=1u<<26;
    tiger->state=6;
    const vec3i offset=vec3i_create(19,-13,3);
    vec3i expected=game_tr2_combat_joint(tiger,TR2_TIGER_BITE_JOINT,offset,prior);
    uint8 free=gs->tr2->effectFree;
    Interest p={.ahead=true,.yaw=-6000};
    game_tr2_tiger_control(ai,gs->players[0],&p);
    vec3i turned=game_tr2_combat_joint(tiger,TR2_TIGER_BITE_JOINT,offset,ai->jointYaw[0]);
    if (!free || gs->tr2->effectActive!=free || ai->jointYaw[0]!=prior-910 ||
        (turned.x==expected.x && turned.y==expected.y && turned.z==expected.z))
        sys_error("tiger bite pose control: eye turn, effect, or its discriminator absent");
    const vec3i point=gs->tr2->effects[free-1].pos;
    if (point.x!=expected.x || point.y!=expected.y || point.z!=expected.z)
        sys_error("tiger bite blood did not use its saved pre-turn yaw");
    puts("PASS tiger bite blood precedes a distinct head turn; unsealed");
    exit(0);
}

static void selectWeapon(void)
{
    Lara *lara=gs->players[0];
    ExtraInfoLara *e=lara->extraL;
    uint32 logic=gs->gRandSeedLogic,draw=gs->gRandSeedDraw,clock=gs->gSaveGame.time;
    /* Q-ARM-02 (assumed): a choice made without gun access is kept, not
     * drawn, and a later choice replaces it. */
    lara->waterState=WATER_STATE_UNDER;
    if (!game_tr2_request_ring_weapon(WEAPON_SHOTGUN) || e->goalWeapon!=WEAPON_SHOTGUN ||
        !gs->tr2->arms.gunRequested)
        sys_error("gun selection did not keep a submerged request");
    game_tr2_update_arms(lara);
    if (e->weaponState!=WEAPON_STATE_FREE || gs->tr2->shotgunPose || !gs->tr2->arms.gunRequested)
        sys_error("gun selection drew or dropped a submerged request");
    lara->waterState=WATER_STATE_ABOVE;
    if (game_tr2_request_ring_weapon(WEAPON_NONE) || !game_tr2_request_ring_weapon(WEAPON_PISTOLS) ||
        e->goalWeapon!=WEAPON_PISTOLS ||
        e->weaponState!=WEAPON_STATE_FREE || gs->gRandSeedLogic!=logic ||
        gs->gRandSeedDraw!=draw || gs->gSaveGame.time!=clock)
        sys_error("gun selection rejected a carried pistol or advanced gameplay");
    game_tr2_update_arms(lara);
    if (e->weaponState!=WEAPON_STATE_DRAW || e->armL.frameIndex!=5 || e->weapon!=WEAPON_PISTOLS ||
        !game_tr2_request_ring_weapon(WEAPON_SHOTGUN) || e->goalWeapon!=WEAPON_SHOTGUN)
        sys_error("gun selection did not start the selected free pistol or keep a choice made while drawing");
    for (unsigned i=0;i<18;++i) game_tr2_update_arms(lara);
    if (e->weaponState!=WEAPON_STATE_READY || e->goalWeapon!=WEAPON_SHOTGUN || !gs->tr2->arms.gunRequested)
        sys_error("gun selection lost a choice made while drawing");
    if (!game_tr2_request_ring_weapon(WEAPON_SHOTGUN))
        sys_error("gun selection did not queue a switch from ready pistols");
    game_tr2_update_arms(lara);
    if (e->weaponState!=WEAPON_STATE_HOLSTER || e->weapon!=WEAPON_PISTOLS)
        sys_error("gun selection changed a ready weapon without holstering it first");
    for (unsigned i=0;i<32 && e->weaponState!=WEAPON_STATE_DRAW;++i) game_tr2_update_arms(lara);
    if (e->weapon!=WEAPON_SHOTGUN || e->weaponState!=WEAPON_STATE_DRAW || !gs->tr2->shotgunPose)
        sys_error("gun selection did not draw its queued shotgun");
    puts("PASS live weapon selection admits free pistols and switches a ready gun via stow; unsealed");
    exit(0);
}

static void poseSlot(void)
{
    Lara *lara=gs->players[0];
    ExtraInfoLara *e=lara->extraL;
    uint8 goal=e->goalWeapon;
    bool requested=gs->tr2->arms.gunRequested;
    lara->extraL=NULL;
    if (game_tr2_request_ring_weapon(WEAPON_SHOTGUN))
        sys_error("gun selection used a missing extra");
    lara->extraL=e;
    if (e->goalWeapon!=goal || gs->tr2->arms.gunRequested!=requested)
        sys_error("gun selection changed state without an extra");
    ItemObj *saved=gs->firstFree;
    gs->firstFree=NULL;
    if (game_tr2_request_ring_weapon(WEAPON_SHOTGUN) || e->goalWeapon!=goal)
        sys_error("gun selection admitted a shotgun with no pose slot");
    gs->firstFree=saved;
    Room *room=saved->room;
    saved->room=lara->room;
    if (game_tr2_request_ring_weapon(WEAPON_SHOTGUN) || e->goalWeapon!=goal)
        sys_error("gun selection admitted a shotgun pose slot that is in use");
    saved->room=room;
    uint16 flags=saved->flags;
    saved->flags|=ITEM_FLAG_ACTIVE;
    if (game_tr2_request_ring_weapon(WEAPON_SHOTGUN) || e->goalWeapon!=goal)
        sys_error("gun selection admitted an active pose slot");
    saved->flags=flags;
    if (!game_tr2_request_ring_weapon(WEAPON_PISTOLS) || e->goalWeapon!=WEAPON_PISTOLS)
        sys_error("gun selection refused pistols because the shotgun slot was busy");
    puts("PASS ring weapon refuses a missing extra and an absent or live shotgun pose slot; unsealed");
    exit(0);
}

static void unknownState(void)
{
    ItemObj *rex=gs->items+104;
    if (!game_tr2_activate_item(rex)) sys_error("unknown state control: wake refused");
    rex->state=9;
    game_tr2_update_creature(rex,gs->players[0]);
    sys_error("unknown state control returned without a refusal");
}

/* The authored room 62 group must work before room 83 as well.
 * Use actual floor words; duplicate visits may not complete the mask. */
static void reverseMask(void)
{
    Lara *lara=gs->players[0];
    ItemObj *rex=gs->items+104;
    const Sector *a=Room_getSector(gs->rooms+62,67*1024+512,73*1024+512);
    Sector_getFloor(a,67*1024+512,lara->pos.y,73*1024+512);
    const FloorData *first=gs->gLastFloorData;
    if (!first) sys_error("reverse mask control: missing room 62 action");
    game_check_trigger(first,lara);
    game_check_trigger(first,lara);
    if ((rex->flags&ITEM_FLAG_MASK)!=512 || (rex->flags&ITEM_FLAG_ACTIVE))
        sys_error("reverse mask control: room 62 alone completed the mask");
    const Sector *b=Room_getSector(gs->rooms+83,60*1024+512,65*1024+512);
    Sector_getFloor(b,60*1024+512,lara->pos.y,65*1024+512);
    if (!gs->gLastFloorData) sys_error("reverse mask control: missing room 83 action");
    game_check_trigger(gs->gLastFloorData,lara);
    if ((rex->flags&ITEM_FLAG_MASK)!=ITEM_FLAG_MASK || !game_tr2_creature_slot(rex) ||
        (gs->items[99].flags&ITEM_FLAG_ACTIVE))
        sys_error("reverse mask control: completion did not wake only slot 104");
    puts("PASS authored masks in reverse order; duplicate bit does not wake; unsealed");
    exit(0);
}

/* Ring gun latch (Q-ARM-02, game_tr2_request_ring_weapon and
 * game_tr2_update_arms) on two ordinary fixtures, with the choice made
 * through the real ring commit. Both modes check every tick of the window
 * after the real tick and stop by name on the first difference.
 *   ring-busy  fixture pickup: pistols are chosen at tick 40 while the hands
 *              are busy with the pickup (weaponState BUSY until tick 65);
 *              the choice is kept and the pistols are drawn at tick 66.
 *   ring-flare fixture schema21/pool-swim: pistols are chosen at tick 620
 *              under water, where gun access is closed; a flare chosen at
 *              tick 640 is drawn and the pistol choice is dropped.
 *   ring-land  fixture stand: pistols are chosen at tick 30 on dry land with
 *              free hands and the flare control is held in the same tick;
 *              gun access is open, so the flare does not replace the choice
 *              and the pistols are drawn. */
enum { RING_NONE, RING_BUSY, RING_FLARE, RING_LAND };
static unsigned ringMode;

static void ringCheck(bool ok,unsigned tick,const char *what)
{
    if (!ok) sys_error("ring latch control: tick %u: %s",tick,what);
}

static uint32 ringBefore(unsigned tick)
{
    ExtraInfoLara *e=gs->players[0]->extraL;
    GameTR2 *tr2=gs->tr2;
    if (ringMode==RING_BUSY && tick==40) {
        ringCheck(e->weaponState==WEAPON_STATE_BUSY && e->weapon==WEAPON_PISTOLS &&
                  !tr2->arms.gunRequested,tick,"pickup fixture is not in busy hands with pistols current");
        game_tr2_ring_commit(TR2_RING_PISTOLS,0);
        ringCheck(e->goalWeapon==WEAPON_PISTOLS && tr2->arms.gunRequested &&
                  e->weaponState==WEAPON_STATE_BUSY,tick,"pistol choice with busy hands was not kept");
    }
    if (ringMode==RING_FLARE && tick==620) {
        ringCheck(gs->players[0]->waterState==WATER_STATE_UNDER && e->weaponState==WEAPON_STATE_FREE &&
                  !tr2->arms.gunRequested,tick,"pool-swim is not free-handed under water");
        game_tr2_ring_commit(TR2_RING_PISTOLS,0);
        ringCheck(e->goalWeapon==WEAPON_PISTOLS && tr2->arms.gunRequested,tick,
                  "pistol choice with gun access closed was not kept");
    }
    if (ringMode==RING_FLARE && tick==640) {
        ringCheck(gs->players[0]->waterState==WATER_STATE_UNDER && e->weaponState==WEAPON_STATE_FREE &&
                  e->goalWeapon==WEAPON_PISTOLS && tr2->arms.gunRequested,tick,
                  "pending pistol choice was lost before the flare was chosen");
        game_tr2_ring_commit(TR2_RING_FLARES,0);
        ringCheck(tr2->ringFlare && tr2->arms.gunRequested,tick,"ring flare request did not wait for the arm update");
    }
    if (ringMode==RING_LAND && tick==30) {
        ringCheck(gs->players[0]->waterState==WATER_STATE_ABOVE && e->weaponState==WEAPON_STATE_FREE &&
                  e->weapon==WEAPON_PISTOLS && !tr2->arms.gunRequested &&
                  game_tr2_ring_quantity(TR2_RING_FLARES),tick,
                  "stand fixture is not free-handed on dry land with a flare in stock");
        game_tr2_ring_commit(TR2_RING_PISTOLS,0);
        ringCheck(e->goalWeapon==WEAPON_PISTOLS && tr2->arms.gunRequested,tick,"pistol choice on dry land was not kept");
        return TR2_IN_FLARE; /* The flare control is held through the ring close. */
    }
    return 0;
}

static void ringAfter(unsigned tick)
{
    ExtraInfoLara *e=gs->players[0]->extraL;
    GameTR2 *tr2=gs->tr2;
    if (ringMode==RING_BUSY) {
        if (tick>=40 && tick<66)
            ringCheck(e->weaponState==WEAPON_STATE_BUSY && e->goalWeapon==WEAPON_PISTOLS &&
                      tr2->arms.gunRequested,tick,"busy-hands choice was drawn early or dropped");
        if (tick==66)
            ringCheck(e->weaponState==WEAPON_STATE_DRAW && e->weapon==WEAPON_PISTOLS &&
                      !tr2->arms.gunRequested,tick,"pistols were not drawn the tick the hands became free");
        if (tick==84) {
            ringCheck(e->weaponState==WEAPON_STATE_READY && e->weapon==WEAPON_PISTOLS,tick,
                      "drawn pistols did not become ready");
            puts("PASS pistols chosen with busy hands are kept and drawn at tick 66 once free; unsealed");
            exit(0);
        }
    }
    if (ringMode==RING_FLARE) {
        if (tick>=620 && tick<640)
            ringCheck(e->weaponState==WEAPON_STATE_FREE && e->goalWeapon==WEAPON_PISTOLS &&
                      tr2->arms.gunRequested,tick,"pistol choice was drawn or dropped while gun access was closed");
        if (tick>=640 && tick<=650)
            ringCheck(e->weapon==TR2_WEAPON_FLARE && e->goalWeapon==TR2_WEAPON_FLARE &&
                      !tr2->arms.gunRequested && !tr2->ringFlare &&
                      (tick>640 || e->weaponState==WEAPON_STATE_DRAW),tick,
                      "flare was not drawn in place of the pending pistol choice");
        if (tick==650) {
            puts("PASS a flare chosen with gun access closed is drawn and drops the pending pistol choice; unsealed");
            exit(0);
        }
    }
    if (ringMode==RING_LAND) {
        if (tick>=30 && tick<=40)
            ringCheck(e->weapon==WEAPON_PISTOLS && e->goalWeapon==WEAPON_PISTOLS &&
                      !tr2->arms.gunRequested && !tr2->ringFlare &&
                      (tick>30 || e->weaponState==WEAPON_STATE_DRAW),tick,
                      "pistols were not drawn: a flare replaced a gun choice with gun access open");
        if (tick==40) {
            puts("PASS a flare requested on dry land with pistols chosen in the same tick leaves the pistols drawn; unsealed");
            exit(0);
        }
    }
}

void __wrap_game_tr2_tick(uint32 mask)
{
    if (!gs->tick) {
        const char *mode=getenv("TR64_REX_CONTROL");
        if (mode && !strcmp(mode,"reverse-mask")) reverseMask();
        if (mode && !strcmp(mode,"fatal")) fatalLunge();
        partialShell=mode && !strcmp(mode,"partial-shell");
        if (mode && !strcmp(mode,"bite-pose")) bitePose();
        if (mode && !strcmp(mode,"select")) selectWeapon();
        if (mode && !strcmp(mode,"pose-slot")) poseSlot();
        if (mode && !strcmp(mode,"unknown-state")) unknownState();
        ringMode=mode && !strcmp(mode,"ring-busy") ? RING_BUSY :
                 mode && !strcmp(mode,"ring-flare") ? RING_FLARE :
                 mode && !strcmp(mode,"ring-land") ? RING_LAND : RING_NONE;
    }
    if (partialShell && gs->tick==63) checkPartialShell(mask);
    const unsigned tick=gs->tick;
    if (ringMode) mask|=ringBefore(tick);
    __real_game_tr2_tick(mask);
    if (ringMode) ringAfter(tick);
}
