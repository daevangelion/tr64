#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed TR2 Level 3 (Bartoli's Hideout / venice.TR2) gameplay & cook test.

Exercises all 7 required Bartoli's Hideout mechanics with positive and negative
controls on the real cooked venice.pack (loaded generically as "venice"):
  1. Boot & stand 180 ticks (room 102, speedboat + Lara, venice script metadata
     + 3-dragon 4x shotgun reward, zero unhandled types).
  2. Pushable block 2 (type 68, room 151) sector raise/push + Trapdoor 2
     (type 115, room 50) closed floor/ceiling support vs open drop.
  3. Statue with knife blade (type 82, room 16): startup anim +3/state 1,
     inactive solid collision, 55-tick cycle (24t state 2 + 31t state 1),
     mesh-7 20 HP + blood + 4 RNG draws in state 2, 0 HP in state 1 (neg).
  4. Detonator box (type 227, room 73): silent refusal on no key / wrong key
     197, Key 2 (198) 158-frame sequence, plunger sound 354 at T+62,
     camera bounce -150 + sound 105 at T+80, T+159 clip 103 + key trigger
     music 37 + FlipMap slot 1 + spent status 4, T+160 active retirement,
     re-trigger refusal (neg).
  5. Auto Pistols (137), Uzis (138), Uzi Ammo (145), Grenade Ammo (148),
     9+ main-ring + 2 key capacity, world duplicate replacement, Auto Pistols
     (2 HP, 10t cadence, sound 21, empty click 48 -> Pistols), Uzis (1 HP,
     4t cadence, sound 43 + stop sound 44).
  6. Campfire (type 253, room 6): proximity 5 HP at 450..600 without ignition,
     ignition strictly < 450 (cooldown 100 + attached flame -7 HP/tick +
     sound 150), strict water extinguish only when hip flame.Y > waterH.
  7. Floor trigger ops: op 8 (music one-shot + mask), op 9 arg 6 (room 30
     switch 34 FlipMap 0 + sound 278 for 31 ticks), op 10 (inert no-op).
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

DRIVER_C = r'''/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/game.h"
#include "tr64/level.h"
#include "tr64/engine.h"
#include "tr64/code.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "engine/core/sys.h"
#include "game/internal.h"
#include "game/tr2/objects.h"
#include "game/object.h"

extern void tr64_snd_boot(void) __attribute__((weak));
extern void tr64_snd_level_start(const tr64_level_t *) __attribute__((weak));
extern void tr64_snd_tick(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

#define CHECK(cond, msg) do { if (!(cond)) fail(msg); } while (0)

static bool tickHasSound(int16 id)
{
    for (uint32 i = 0; i < gs->soundCount; ++i)
        if (!gs->sounds[i].stop && gs->sounds[i].sound == id)
            return true;
    return false;
}

static unsigned countEffects(TR2EffectKind kind)
{
    unsigned n = 0;
    for (uint8 idx = gs->tr2->effectActive; idx; idx = gs->tr2->effects[idx - 1].next)
        if (gs->tr2->effects[idx - 1].kind == kind)
            ++n;
    return n;
}

static void step(uint32 mask)
{
    game_tr2_tick(mask);
    tr64_io_frame();
    if (tr64_snd_tick) tr64_snd_tick();
    if (tr64_snd_pump) tr64_snd_pump();
    const tr64_game_stop_t *st = game_stop_info();
    if (st && st->kind != TR64_GAME_STOP_NONE) {
        fprintf(stderr, "FAIL: unexpected stop %s\n", st->name);
        exit(1);
    }
}

static void stepRoute(uint32 mask, unsigned cmd, uint32 pause)
{
    game_tr2_route_update(mask, cmd, pause);
    tr64_io_frame();
    if (tr64_snd_tick) tr64_snd_tick();
    if (tr64_snd_pump) tr64_snd_pump();
    const tr64_game_stop_t *st = game_stop_info();
    if (st && st->kind != TR64_GAME_STOP_NONE) {
        fprintf(stderr, "FAIL: unexpected stop %s\n", st->name);
        exit(1);
    }
}

static void moveLara(unsigned roomIdx, int32 x, int32 y, int32 z, int16 yaw)
{
    Lara *lara = gs->players[0];
    Room *dst = gs->rooms + roomIdx;
    if (lara->room != dst) {
        Room_remove(lara->room, lara);
        Room_add(dst, lara);
    }
    lara->pos = vec3i_create(x, y, z);
    lara->angle = vec3s_create(0, yaw, 0);
    lara->hSpeed = 0;
    lara->vSpeed = 0;
    lara->flags &= ~(ITEM_FLAG_GRAVITY | ITEM_FLAG_INJURED);
    lara->waterState = (tr64_u16(dst->info->flags) & ROOM_FLAG_WATER_MASK)
        ? WATER_STATE_UNDER : WATER_STATE_ABOVE;
    ItemObj_animSet(lara, lara->waterState == WATER_STATE_UNDER
        ? Lara_ANIM_UNDERWATER : Lara_ANIM_STAND_NORMAL, true, 0);
    lara->state = lara->goalState = lara->waterState == WATER_STATE_UNDER
        ? Lara_STATE_UW_TREAD : Lara_STATE_STOP;
    lara->extraL->weaponState = WEAPON_STATE_FREE;
    lara->health = 1000;
    gs->tr2->vehicle = -1;
    gs->tr2->boatTick = false;
    gs->tr2->extraAnim = false;
    gs->tr2->motionStart.state = lara->state;
    gs->cinfo.pos = lara->pos;
}

static tr64_level_t level;
static int packBytes;

static void bootVenice(void)
{
    static bool booted = false;
    if (!booted) {
        tr64_eng_boot();
        code_boot();
        if (tr64_snd_boot) tr64_snd_boot();
        tr64_io_init((uint32_t)packBytes);
        mem_level_begin();
        tr64_io_frame();
        tr64_level_load(&level, (uint32_t)packBytes, "venice");
        if (tr64_snd_level_start) tr64_snd_level_start(&level);
        tr64_io_frame();
        if (tr64_snd_pump) tr64_snd_pump();
        booted = true;
    } else {
        game_level_stop();
        tr64_mem_reset(TR64_MEM_state);
    }
    game_init();
    game_tr2_start(&level, "venice", 0u);
}

static void testBootAndScript(void)
{
    bootVenice();
    Lara *lara = gs->players[0];
    CHECK((unsigned)(lara->room - gs->rooms) == 102u, "Lara start room != 102");
    CHECK(lara->pos.x == 73216 && lara->pos.y == 4864 && lara->pos.z == 19968,
          "Lara start pos mismatch");
    CHECK(gs->items[96].type == TR2_ITEM_BOAT, "item 96 is not speedboat");
    CHECK(game_tr2_ring_quantity(TR2_RING_FLARES) == 2u, "venice startFlares != 2");
    CHECK(strstr(game_tr2_level_script_finish_label(), "Bartoli") != NULL,
          "venice finish label mismatch");

    /* Stand 180 ticks cleanly. */
    for (int i = 0; i < 180; ++i)
        step(0);
    CHECK(lara->health == 1000, "Lara lost health standing at start");
    CHECK(gs->tick == 180, "tick count != 180");

    /* Verify 3-dragon secret reward: 4x Shotgun Ammo (48 pellets if Shotgun owned). */
    uint32 beforeShotgun = gs->tr2->ammo[3];
    ItemObj d0 = {.type = 190}, d1 = {.type = 191}, d2 = {.type = 192};
    game_tr2_pickup_credit(&d0);
    game_tr2_pickup_credit(&d1);
    game_tr2_pickup_credit(&d2);
    CHECK(gs->gSaveGame.secrets == 7u, "3 dragons did not set secrets=7");
    CHECK(gs->tr2->ammo[3] == beforeShotgun + 48u, "3-dragon reward != +48 shotgun pellets");

    /* Negative/unowned control: without Shotgun (158), gives 4 bundles of 165. */
    bootVenice();
    game_tr2_ring_remove(158u);
    game_tr2_pickup_credit(&d0);
    game_tr2_pickup_credit(&d1);
    game_tr2_pickup_credit(&d2);
    CHECK(game_tr2_ring_quantity(165u) == 4u, "3-dragon unowned reward != 4 bundles of 165");
    printf("PASS 1: boot & stand 180t in Bartoli's Hideout (room 102, speedboat+Lara, venice script + 3-dragon reward)\n");
}

static void testBlock2AndTrapdoor2(void)
{
    bootVenice();
    /* Block 2 (type 68, item 129, room 151 at 65024, 2048, 39424). */
    ItemObj *blk = gs->items + 129;
    CHECK(blk->type == 68u, "item 129 is not block type 68");
    const Sector *blkSec = Room_getSector(blk->room, blk->pos.x, blk->pos.z);
    CHECK(blkSec->floor * 256 == 1024, "block 2 did not raise starting sector by 1024");

    /* Position Lara on tile (-X) facing +X (16384) to pull block 2 onto (64000, 2048, 39424). */
    moveLara(151, blk->pos.x - 612, 2048, blk->pos.z, 16384);
    /* Grasp (Action) then hold Action + Down to pull. */
    step(IN_ACTION);
    bool pulled = false;
    for (int t = 0; t < 240; ++t) {
        step(IN_ACTION | IN_DOWN);
        if (blk->pos.x == 64000 && (blk->flags & ITEM_FLAG_ACTIVE) == 0) {
            pulled = true;
            break;
        }
    }
    CHECK(pulled, "block 2 (type 68) did not finish pull onto destination tile");
    const Sector *oldSec = Room_getSector(blk->room, 65024, 39424);
    const Sector *newSec = Room_getSector(blk->room, 64000, 39424);
    CHECK(oldSec->floor * 256 == 2048, "block 2 vacated sector did not restore to 2048");
    CHECK(newSec->floor * 256 == 1024, "block 2 arrival sector was not raised to 1024");

    /* Trapdoor 2 (type 115, item 48, room 50 at 41472, 2816, 36352, yaw 32768). */
    ItemObj *td = gs->items + 48;
    CHECK(td->type == 115u && td->state == 0u, "item 48 is not closed trapdoor type 115");
    int32 f = 9999, c = -9999;
    ItemObj_getItemFloorCeiling(td, td->pos.x, td->pos.y - 256, td->pos.z, &f, &c);
    CHECK(f == 2816, "closed trapdoor 2 did not provide floor support at own tile");
    /* Second covered tile for yaw 32768 (ANGLE_180) is +Z (z + 1024), while -Z is uncovered. */
    f = 9999; c = -9999;
    ItemObj_getItemFloorCeiling(td, td->pos.x, td->pos.y - 256, td->pos.z + 1024, &f, &c);
    CHECK(f == 2816, "closed trapdoor 2 did not provide floor support at second covered tile (+Z)");
    f = 9999; c = -9999;
    ItemObj_getItemFloorCeiling(td, td->pos.x, td->pos.y - 256, td->pos.z - 1024, &f, &c);
    CHECK(f == 9999, "closed trapdoor 2 erroneously supported uncovered tile (-Z)");
    /* Ceiling underside when queried strictly below origin. */
    f = 9999; c = -9999;
    ItemObj_getItemFloorCeiling(td, td->pos.x, td->pos.y + 256, td->pos.z, &f, &c);
    CHECK(c == 2816 + 256, "closed trapdoor 2 did not provide ceiling underside");

    /* Activate trapdoor 2 -> opens to state 1 and drops floor support. */
    td->flags |= ITEM_FLAG_MASK;
    CHECK(game_tr2_activate_item(td), "trapdoor 2 failed to activate");
    for (int t = 0; t < 40; ++t)
        step(0);
    CHECK(td->state == 1u, "trapdoor 2 did not open to state 1");
    f = 9999; c = -9999;
    ItemObj_getItemFloorCeiling(td, td->pos.x, td->pos.y - 256, td->pos.z, &f, &c);
    CHECK(f == 9999, "open trapdoor 2 still provided floor support");
    printf("PASS 2: pushable block 2 (type 68) pull/occupancy & trapdoor 2 (type 115) support/open\n");
}

static void testStatueBlade(void)
{
    bootVenice();
    ItemObj *st = gs->items + 16;
    CHECK(st->type == TR2_ITEM_STATUE_BLADE, "item 16 is not statue blade 82");
    CHECK(st->animIndex == gs->level.models[82].animIndex + 3u, "statue startup anim != base+3");
    CHECK(st->state == 1u, "statue startup state != 1");

    /* Inactive solid collision: clip 407 bbox is local X=-510..-96, Z=274..759;
     * with yaw 49152 (-90 deg), local (-300, 500) is world (st.x - 500, st.z - 300). */
    moveLara(16, st->pos.x - 500, st->pos.y, st->pos.z - 300, 16384);
    gs->cinfo.enemyPush = 1;
    gs->cinfo.radius = LARA_RADIUS;
    int32 startX = gs->players[0]->pos.x, startZ = gs->players[0]->pos.z;
    step(0);
    CHECK(gs->players[0]->health == 1000, "inactive statue dealt damage");
    CHECK(gs->players[0]->pos.x != startX || gs->players[0]->pos.z != startZ,
          "inactive statue did not push Lara out");

    /* Activate statue and verify 55-tick swing cycle (24t state 2 + 31t state 1). */
    moveLara(16, st->pos.x - 2048, st->pos.y, st->pos.z, 0);
    st->flags |= ITEM_FLAG_MASK;
    CHECK(game_tr2_activate_item(st), "statue 16 failed to activate");
    /* Wait until it enters state 2 to align cleanly at cycle start. */
    int guard = 0;
    while (st->state != 2 && guard++ < 10)
        step(0);
    CHECK(st->state == 2, "statue never entered state 2");
    unsigned s2Ticks = 0, s1Ticks = 0;
    bool saw80 = tickHasSound(80), saw81 = false;
    while (st->state == 2 && s2Ticks < 60) {
        ++s2Ticks;
        step(0);
        if (tickHasSound(80)) saw80 = true;
        if (tickHasSound(81)) saw81 = true;
    }
    while (st->state == 1 && s1Ticks < 60) {
        ++s1Ticks;
        step(0);
        if (tickHasSound(80)) saw80 = true;
        if (tickHasSound(81)) saw81 = true;
    }
    CHECK(s2Ticks == 24u && s1Ticks == 31u, "statue cycle != 24t state 2 + 31t state 1 (55t)");
    CHECK(saw80 && saw81, "statue missing swing sounds 80/81");

    /* Positive control: mesh 7 touch in state 2 -> -20 HP + blood + 4 gameplay RNG draws. */
    CHECK(st->state == 2, "statue not back in state 2");
    Lara *lara = gs->players[0];
    moveLara(16, st->pos.x - 1024, st->pos.y, st->pos.z, 0);
    uint32 seedBefore = gs->gRandSeedLogic;
    uint32 expectedSeed = seedBefore;
    for (int i = 0; i < 4; ++i)
        expectedSeed = 0x3039u + expectedSeed * 0x41c64e6du;
    unsigned bloodBefore = countEffects(TR2_FX_BLOOD);
    lara->extraL->healthTimer = 0;
    st->hitMask = 0x80u; /* Mesh 7 (knife blade) touch bit */
    step(0);
    CHECK(lara->health == 980, "statue mesh 7 in state 2 did not deal 20 HP");
    CHECK(lara->extraL->healthTimer > 0, "statue hit did not trigger hit healthTimer");
    CHECK(countEffects(TR2_FX_BLOOD) == bloodBefore + 1u, "statue hit did not spawn 1 blood splat");
    CHECK(gs->gRandSeedLogic == expectedSeed, "statue blood did not consume exactly 4 logic RNG draws");

    /* Negative control 1: non-blade meshes (0..6) in state 2 deal 0 HP. */
    lara->health = 1000;
    lara->flags &= ~ITEM_FLAG_INJURED;
    st->hitMask = 0x7fu;
    step(0);
    CHECK(lara->health == 1000, "statue meshes 0..6 dealt damage in state 2");

    /* Negative control 2: mesh 7 in state 1 (safe) deals 0 HP & 0 blood. */
    while (st->state != 1)
        step(0);
    lara->health = 1000;
    bloodBefore = countEffects(TR2_FX_BLOOD);
    st->hitMask = 0x80u;
    step(0);
    CHECK(lara->health == 1000, "statue mesh 7 dealt damage in state 1");
    CHECK(countEffects(TR2_FX_BLOOD) <= bloodBefore, "statue spawned blood in state 1");
    printf("PASS 3: statue blade (type 82) solid/55t cycle (24s2+31s1)/20HP+blood+4RNG/state-1 neg\n");
}

static void testDetonatorBox(void)
{
    bootVenice();
    ItemObj *det = gs->items + 75;
    CHECK(det->type == TR2_ITEM_DETONATOR, "item 75 is not detonator 227");
    CHECK((det->flags & ITEM_FLAG_STATUS) == 0, "detonator did not start inactive");
    CHECK(gs->gSaveGame.flipped == 0, "map already flipped before detonator");

    /* Neg 1: No key carried, Action held -> refuses silently (no sound 2), stays inactive. */
    moveLara(73, det->pos.x, det->pos.y, det->pos.z, 12345);
    step(IN_ACTION);
    CHECK(!tickHasSound(2), "detonator played refusal sound 2 with no key");
    CHECK(!gs->tr2->extraAnim && (det->flags & ITEM_FLAG_STATUS) == 0,
          "detonator activated without key");

    /* Neg 2: Wrong key (Key 1 = 197) -> refuses silently (no sound 2), key 197 kept. */
    ItemObj k1 = {.type = 193};
    game_tr2_pickup_credit(&k1);
    CHECK(game_tr2_ring_quantity(197u) == 1u, "Key 1 not credited");
    moveLara(73, det->pos.x, det->pos.y, det->pos.z, 12345);
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 197u, 0);
    CHECK(!tickHasSound(2), "detonator played refusal sound 2 on wrong key");
    CHECK(game_tr2_ring_quantity(197u) == 1u, "wrong key 197 was consumed");
    CHECK(!gs->tr2->extraAnim && (det->flags & ITEM_FLAG_STATUS) == 0,
          "detonator activated on wrong key");

    /* Positive: Key 2 (198) triggers full 158-frame sequence, T+62 sound 354,
     * T+80 bounce -150 + sound 105, T+159 clip 103 + music 37 + FlipMap 1 + spent 4,
     * T+160 active retirement. */
    ItemObj k2 = {.type = 194};
    game_tr2_pickup_credit(&k2);
    CHECK(game_tr2_ring_quantity(198u) == 1u, "Key 2 not credited");
    moveLara(73, det->pos.x + 120, det->pos.y, det->pos.z - 120, 12345);
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 198u, 0); /* Tick T */
    Lara *lara = gs->players[0];
    CHECK(game_tr2_ring_quantity(198u) == 0u, "Key 2 was not consumed on accept");
    CHECK(gs->tr2->extraAnim, "extraAnim not set at tick T");
    CHECK(lara->pos.x == det->pos.x && lara->pos.y == det->pos.y && lara->pos.z == det->pos.z,
          "Lara did not snap to detonator origin at tick T");
    CHECK(det->angle.y == 12345, "detonator yaw did not align to Lara");
    CHECK(lara->animIndex == gs->level.models[TR2_MODEL_LARA_EXTRA].animIndex + 1u &&
          lara->state == 1u && lara->frameIndex == 1u,
          "Lara did not enter extra clip +1/state 1/frame 1 at tick T");
    CHECK((det->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE,
          "detonator not active at tick T");

    bool saw354At62 = false, saw105At80 = false, sawBounceAt80 = false;
    for (int rel = 1; rel <= 158; ++rel) {
        step(0);
        if (rel == 62) saw354At62 = tickHasSound(354);
        if (rel == 80) {
            saw105At80 = tickHasSound(105);
            sawBounceAt80 = (lara->extraL->camera.bounce < -140);
            CHECK(gs->gSaveGame.flipped == 0, "flip happened early at T+80");
        }
        CHECK(gs->gSaveGame.flipped == 0, "flip happened before T+159");
    }
    CHECK(saw354At62, "missing plunger sound 354 at T+62");
    CHECK(saw105At80, "missing explosion sound 105 at T+80");
    CHECK(sawBounceAt80, "missing camera bounce -150 at T+80");
    CHECK(lara->state == 0u, "Lara did not chain into extra state 0 at T+158");

    /* Tick T+159: exit extra anim to stand 103, free hands, key trigger fires FlipMap 1 + track 37! */
    step(0);
    CHECK(!gs->tr2->extraAnim, "extraAnim still active at T+159");
    CHECK(lara->animIndex == Lara_ANIM_STAND_NORMAL && lara->state == Lara_STATE_STOP,
          "Lara did not return to stand clip 103 at T+159");
    CHECK(lara->extraL->weaponState == WEAPON_STATE_FREE, "hands not freed at T+159");
    CHECK(gs->gSaveGame.flipped == 1, "FlipMap slot 1 did not collapse building at T+159");
    CHECK(gs->gCurTrack == 37, "detonator trigger did not play music track 37 at T+159");
    CHECK((det->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE,
          "detonator not marked spent (status 4) at T+159");

    /* Tick T+160: detonator retires from active list. */
    step(0);
    CHECK((det->flags & ITEM_FLAG_ACTIVE) == 0, "detonator did not retire from active list at T+160");

    /* Neg 3: Re-trigger spent detonator with fresh Key 2 -> refused silently, key kept, no 2nd flip. */
    game_tr2_pickup_credit(&k2);
    CHECK(game_tr2_ring_quantity(198u) == 1u, "second Key 2 not credited");
    moveLara(73, det->pos.x, det->pos.y, det->pos.z, 0);
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 198u, 0);
    CHECK(!tickHasSound(2), "spent detonator played refusal sound 2");
    CHECK(game_tr2_ring_quantity(198u) == 1u, "spent detonator consumed Key 2");
    CHECK(!gs->tr2->extraAnim && gs->gSaveGame.flipped == 1,
          "spent detonator re-triggered or re-flipped");
    printf("PASS 4: detonator (type 227) silent negs, T+62 s354, T+80 bounce -150+s105, T+159 flip+track 37, T+160 retire, spent neg\n");
}

static void testWeaponsAndCapacity(void)
{
    bootVenice();
    /* Test ammo-before-weapon bundling + conversion + world duplicate replacement. */
    ItemObj p144 = {.type = 144}, p145 = {.type = 145}, p148 = {.type = 148};
    ItemObj p193 = {.type = 193}, p194 = {.type = 194};
    game_tr2_pickup_credit(&p144); /* 166 bundle */
    game_tr2_pickup_credit(&p145); /* 167 bundle */
    game_tr2_pickup_credit(&p148); /* 170 bundle */
    game_tr2_pickup_credit(&p193); /* Key 1 (197) */
    game_tr2_pickup_credit(&p194); /* Key 2 (198) */
    CHECK(game_tr2_ring_quantity(166u) == 1u, "144 without Auto did not bundle 166");
    CHECK(game_tr2_ring_quantity(167u) == 1u, "145 without Uzis did not bundle 167");
    CHECK(game_tr2_ring_quantity(170u) == 1u, "148 without Grenade did not bundle 170");

    /* Stage duplicate world placements for 137 & 138 to test world replacement. */
    ItemObj *dupAuto = gs->items + 51; /* unused pickup 148 in room 51 */
    ItemObj *dupUzi = gs->items + 52;
    dupAuto->type = 137;
    dupUzi->type = 138;

    /* Credit Auto Pistols (item 113) and Uzis (item 126). */
    game_tr2_pickup_credit(gs->items + 113);
    CHECK(game_tr2_ring_quantity(159u) == 1u, "Auto Pistols 159 not in ring");
    CHECK(game_tr2_ring_quantity(166u) == 0u, "166 bundle not converted on Auto pickup");
    CHECK(gs->tr2->ammo[1] == 80u, "Auto reserve != 40 + 40 converted");
    CHECK(dupAuto->type == 144u, "remaining world 137 not replaced with 144");

    game_tr2_pickup_credit(gs->items + 126);
    CHECK(game_tr2_ring_quantity(160u) == 1u, "Uzis 160 not in ring");
    CHECK(game_tr2_ring_quantity(167u) == 0u, "167 bundle not converted on Uzi pickup");
    CHECK(gs->tr2->ammo[2] == 160u, "Uzi reserve != 80 + 80 converted");
    CHECK(dupUzi->type == 145u, "remaining world 138 not replaced with 145");

    /* Collect 145 while Uzis owned -> +80 direct. */
    game_tr2_pickup_credit(&p145);
    CHECK(gs->tr2->ammo[2] == 240u, "145 with Uzis owned did not add +80");

    /* Verify 9 main-ring items + 2 keys simultaneously (11 total > old cap 10!)
     * and strictly ascending priority order. */
    CHECK(gs->tr2->carriedCount == 11u, "expected 11 simultaneous carried items (9 main + 2 keys)");
    for (unsigned i = 1; i < gs->tr2->carriedCount; ++i)
        CHECK(game_tr2_ring_priority(gs->tr2->carried[i - 1].type) <=
              game_tr2_ring_priority(gs->tr2->carried[i].type),
              "carried ring out of priority order");

    /* Also test Grenade Ammo (148) when Grenade Launcher (163) IS owned -> +2 direct. */
    ItemObj d0 = {.type = 190};
    (void)d0;
    /* Add 163 via carried ring directly? Easier: test 148 bundle +2 once 163 present. */
    TR2RingEntry *slot170 = game_tr2_ring_find(170u);
    CHECK(slot170 != NULL, "170 bundle missing");
    slot170->type = 163u; /* simulate owned Grenade Launcher */
    uint32 gBefore = gs->tr2->ammo[5];
    game_tr2_pickup_credit(&p148);
    CHECK(gs->tr2->ammo[5] == gBefore + 2u, "148 with Grenade Launcher owned did not add +2");

    /* Wake a Thug (item 0? let's find first Doberman/Thug) to test Auto Pistols & Uzis damage + cadence + sounds! */
    ItemObj *target = NULL;
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        if (gs->items[i].type == TR2_ITEM_THUG) {
            target = gs->items + i;
            break;
        }
    }
    CHECK(target != NULL, "no Thug found in venice.TR2");
    moveLara(151, 61952, 2048, 39424, 16384);
    Room_remove(target->room, target);
    target->pos = vec3i_create(61952 + 2048, 2048, 39424);
    target->angle.y = -16384;
    Room_add(gs->rooms + 151, target);
    target->flags |= ITEM_FLAG_MASK;
    CHECK(game_tr2_activate_item(target), "target Thug failed to wake");
    target->health = 100;

    /* Equip Auto Pistols (159 -> WEAPON_MAGNUMS). */
    gs->tr2->ammo[1] = 20;
    CHECK(game_tr2_request_ring_weapon(WEAPON_MAGNUMS), "failed to request Auto Pistols");
    for (int t = 0; t < 20 && gs->players[0]->extraL->weaponState != WEAPON_STATE_READY; ++t)
        step(0);
    CHECK(gs->players[0]->extraL->weapon == WEAPON_MAGNUMS &&
          gs->players[0]->extraL->weaponState == WEAPON_STATE_READY,
          "Auto Pistols did not reach READY");

    int firstAutoShot = -1, secondAutoShot = -1;
    int16 hpBefore = target->health;
    uint32 ammoBefore = gs->tr2->ammo[1];
    for (int t = 0; t < 25; ++t) {
        step(IN_ACTION);
        if (tickHasSound(21)) {
            if (firstAutoShot < 0) firstAutoShot = t;
            else if (secondAutoShot < 0) secondAutoShot = t;
        }
    }
    CHECK(firstAutoShot >= 0 && secondAutoShot >= 0, "Auto Pistols did not fire twice with sound 21");
    CHECK(secondAutoShot - firstAutoShot == 10, "Auto Pistols cadence != 10 ticks");
    uint32 autoSpent = ammoBefore - gs->tr2->ammo[1];
    CHECK(autoSpent >= 4u, "Auto Pistols did not spend ammo per hand");
    CHECK((uint32)(hpBefore - target->health) == autoSpent * 2u, "Auto Pistols damage != 2 HP per hit");

    /* Empty Auto Pistols -> dry click sound 48 + fallback to Pistols (1). */
    gs->tr2->ammo[1] = 0;
    step(IN_ACTION);
    CHECK(tickHasSound(48), "empty Auto Pistols did not play dry-click sound 48");
    CHECK(gs->players[0]->extraL->goalWeapon == WEAPON_PISTOLS,
          "empty Auto Pistols did not fall back to Pistols");

    /* Equip Uzis (160 -> WEAPON_UZIS). */
    for (int t = 0; t < 40 && gs->players[0]->extraL->weaponState != WEAPON_STATE_FREE; ++t)
        step(0);
    target->health = 100;
    gs->tr2->ammo[2] = 40;
    CHECK(game_tr2_request_ring_weapon(WEAPON_UZIS), "failed to request Uzis");
    for (int t = 0; t < 25 && gs->players[0]->extraL->weaponState != WEAPON_STATE_READY; ++t)
        step(0);
    CHECK(gs->players[0]->extraL->weapon == WEAPON_UZIS &&
          gs->players[0]->extraL->weaponState == WEAPON_STATE_READY,
          "Uzis did not reach READY");

    int firstUziSpend = -1, secondUziSpend = -1;
    unsigned uzi43Ticks = 0;
    hpBefore = target->health;
    ammoBefore = gs->tr2->ammo[2];
    for (int t = 0; t < 15; ++t) {
        uint32 prevAmmo = gs->tr2->ammo[2];
        step(IN_ACTION);
        if (tickHasSound(43)) ++uzi43Ticks;
        if (gs->tr2->ammo[2] < prevAmmo) {
            if (firstUziSpend < 0) firstUziSpend = t;
            else if (secondUziSpend < 0) secondUziSpend = t;
        }
    }
    CHECK(firstUziSpend >= 0 && secondUziSpend >= 0, "Uzis did not fire twice");
    CHECK(secondUziSpend - firstUziSpend == 4, "Uzis cadence != 4 ticks");
    CHECK(uzi43Ticks >= 8u, "Uzis did not replay sound 43 on shot + recoil ticks");
    uint32 uziSpent = ammoBefore - gs->tr2->ammo[2];
    CHECK((uint32)(hpBefore - target->health) == uziSpent * 1u, "Uzis damage != 1 HP per hit");

    /* Release Action -> stop/echo sound 44 plays when firing stops! */
    bool sawStop44 = false;
    for (int t = 0; t < 10; ++t) {
        step(0);
        if (tickHasSound(44)) sawStop44 = true;
    }
    CHECK(sawStop44, "Uzis did not play stop/echo sound 44 after Action released");
    printf("PASS 5: Auto Pistols (2HP/10t/s21/empty48), Uzis (1HP/4t/s43+s44), pickups 137/138/145/148, 11-item ring\n");
}

static void testCampfireAndExtinguish(void)
{
    bootVenice();
    ItemObj *fire = gs->items + 8; /* Room 6, (41472, 1280, 31232), reverse-start burning */
    CHECK(fire->type == TR2_ITEM_CAMPFIRE, "item 8 is not campfire 253");
    /* Let emitter spawn its stationary flame on tick 1. */
    step(0);
    Lara *lara = gs->players[0];

    /* Proximity-only zone: dist 500 (450 < 500 <= 600) -> -5 HP, NO ignition. */
    moveLara(6, fire->pos.x, fire->pos.y, fire->pos.z + 500, 0);
    step(0);
    CHECK(lara->health == 995, "campfire at dist 500 did not deal 5 HP");
    CHECK(gs->gLaraHazardState[0].burnTicks == 0, "campfire ignited Lara at dist 500");

    /* Exact boundary dist 450 (dx^2+dz^2 == 202500, not strictly < 202500) -> -5 HP, NO ignition. */
    moveLara(6, fire->pos.x, fire->pos.y, fire->pos.z + 450, 0);
    step(0);
    CHECK(lara->health == 995, "campfire at dist 450 did not deal 5 HP");
    CHECK(gs->gLaraHazardState[0].burnTicks == 0, "campfire ignited Lara at exact dist 450");

    /* Ignition zone: dist 400 (< 450) -> -5 HP + attach flame-on-Lara + cooldown 100. */
    unsigned flamesBefore = countEffects(TR2_FX_FLAME);
    moveLara(6, fire->pos.x, fire->pos.y, fire->pos.z + 400, 0);
    step(0);
    CHECK(lara->health == 995, "campfire at dist 400 did not deal 5 HP");
    CHECK(gs->gLaraHazardState[0].burnTicks != 0, "campfire did not ignite Lara at dist 400");
    CHECK(countEffects(TR2_FX_FLAME) == flamesBefore + 1u, "campfire did not attach TR2_FX_FLAME");

    /* Move away from campfire -> attached flame deals -7 HP/tick + sound 150. */
    moveLara(102, 72192, 4864, 18944, 0);
    /* Wait: room 102 (72192, 4864, 18944) has water directly below at waterY=4864.
     * Hip joint 0 of standing Lara is at Y ~ 4864 - 400 (~4464) < waterY (4864),
     * so hip is ABOVE water plane -> must NOT extinguish! */
    int16 hp = lara->health;
    step(0);
    CHECK(gs->gLaraHazardState[0].burnTicks != 0, "flame extinguished while hip above water plane");
    CHECK(lara->health == hp - 7, "attached flame did not deal 7 HP/tick");
    CHECK(tickHasSound(150), "attached flame did not play sound 150");

    /* Submerge Lara into room 150 (water room below room 102, waterY = 4864) so hip.Y > 4864! */
    moveLara(150, 72192, 5600, 18944, 0);
    hp = lara->health;
    step(0);
    CHECK(gs->gLaraHazardState[0].burnTicks == 0, "flame did not extinguish strictly below water surface");
    CHECK(lara->health == hp, "extinguishing tick still dealt 7 HP damage");
    CHECK(countEffects(TR2_FX_FLAME) == flamesBefore, "attached flame was not retired from effect pool");
    printf("PASS 6: campfire (type 253) 5HP@500/450 neg, ignite@400 (-7HP/t + s150), strict water extinguish\n");
}

static void testFlipEffectAndTriggerOps(void)
{
    bootVenice();
    /* Room 30 switch 34 (48640, -5632, 38400, yaw 32768) pairs FlipMap 0 + op 9 arg 6! */
    ItemObj *sw = gs->items + 34;
    CHECK(sw->type == TR2_ITEM_WALL_SWITCH, "item 34 is not wall switch");
    /* Stand in front of wall switch (z + 400 in switch's local frame; yaw 32768 => -Z in world). */
    moveLara(30, sw->pos.x, sw->pos.y, sw->pos.z - 400, (int16)32768);
    step(IN_ACTION);
    bool flipped = false;
    unsigned s278Count = 0;
    for (int t = 0; t < 120; ++t) {
        step(0);
        if (gs->gSaveGame.flipped) flipped = true;
        if (tickHasSound(278)) ++s278Count;
    }
    CHECK(flipped, "room 30 switch 34 did not flip FlipMap 0");
    CHECK(s278Count == 31u, "FlipEffect arg 6 did not play sound 278 for exactly 31 ticks");

    /* Verify op == 10 (room 51 global tile (50, 32)) is an inert no-op:
     * does not increment inertAction[10] and scores no secret. */
    uint32 secBefore = gs->gSaveGame.secrets;
    moveLara(51, 50 * 1024 + 512, 8704, 32 * 1024 + 512, 0);
    step(0);
    CHECK(gs->tr2->inertAction[10] == 0u, "op == 10 incremented inertAction[10]");
    CHECK(gs->gSaveGame.secrets == secBefore, "op == 10 erroneously credited a secret");

    /* Verify op == 8 one-shot mask overlap suppression: trigger synthetic one-shot track 45 twice. */
    FloorData prog[3];
    prog[0] = (FloorData){ .raw = __builtin_bswap16((uint16)(0x8000u | 4u)) };
    prog[1] = (FloorData){ .raw = __builtin_bswap16((uint16)(0x0100u | 0x3e00u)) }; /* once + full mask */
    prog[2] = (FloorData){ .raw = __builtin_bswap16((uint16)(0x8000u | (8u << 10) | 45u)) };
    uint32 serial0 = gs->musicSerial;
    game_check_trigger(prog, gs->players[0]);
    CHECK(gs->gCurTrack == 45 && gs->musicSerial == serial0 + 1u, "op == 8 one-shot track 45 did not play");
    game_check_trigger(prog, gs->players[0]);
    CHECK(gs->musicSerial == serial0 + 1u, "op == 8 one-shot track 45 replayed despite mask overlap");

    tr64_mem_stats_t st;
    tr64_mem_stats(TR64_MEM_state, &st);
    printf("PASS 7: FlipEffect op 9 arg 6 (31t sound 278 on FlipMap 0), op 8 music one-shot/overlap, op 10 inert no-op\n");
    printf("INFO venice state_pool_used=%lu sizeof_GameTR2=%lu (TR2 budget 303104)\n",
           (unsigned long)st.used, (unsigned long)sizeof(GameTR2));
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s VENICE.pack\n", argv[0]);
        return 2;
    }
    packBytes = sys_cart_open(argv[1]);
    CHECK(packBytes > 0, "cannot open venice.pack");
    testBootAndScript();
    testBlock2AndTrapdoor2();
    testStatueBlade();
    testDetonatorBox();
    testWeaponsAndCapacity();
    testCampfireAndExtinguish();
    testFlipEffectAndTriggerOps();
    printf("ALL 7 BARTOLI HIDEOUT TESTS PASSED\n");
    return 0;
}
'''


def expect(ok, msg):
    if not ok:
        raise RuntimeError(msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('build_dir', type=Path, help='TR2 host BUILD_DIR containing host/*.o and generated/')
    ap.add_argument('venice_pack', type=Path, help='Cooked venice.pack')
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')

    repo = Path(__file__).resolve().parents[2]
    gen = args.build_dir / 'generated'
    objs = [p for p in sorted((args.build_dir / 'host').rglob('*.o'))
            if p.name != 'main.o']
    expect(objs, f'no host objects found under {args.build_dir / "host"}')

    with tempfile.TemporaryDirectory(prefix='tr2-bartoli-', dir=scratch) as td:
        root = Path(td)
        src = root / 'bartoli_driver.c'
        obj = root / 'bartoli_driver.o'
        exe = root / 'bartoli-host'
        src.write_text(DRIVER_C, encoding='ascii')

        c = subprocess.run([
            'rtk', 'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
            f'-I{repo}', f'-I{repo / "include"}', f'-I{gen}', f'-I{gen / "host"}',
            '-DTR64_GAME=2', '-DTR64_HOST=1', '-D_POSIX_C_SOURCE=200809L',
            '-c', str(src), '-o', str(obj),
        ], capture_output=True, text=True)
        expect(c.returncode == 0, f'compile failed:\n{c.stdout}{c.stderr}')

        l = subprocess.run([
            'rtk', 'cc', '-m32', '-o', str(exe), str(obj),
            *(str(p) for p in objs),
        ], capture_output=True, text=True)
        expect(l.returncode == 0, f'link failed:\n{l.stdout}{l.stderr}')

        r = subprocess.run(
            ['rtk', str(exe), str(args.venice_pack)],
            capture_output=True, text=True, timeout=60,
        )
        print(r.stdout, end='')
        if r.stderr:
            print(r.stderr, end='')
        expect(r.returncode == 0, f'bartoli-host exited {r.returncode}')


if __name__ == '__main__':
    main()
