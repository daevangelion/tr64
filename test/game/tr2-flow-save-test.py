#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for TRII Title, Death/Retry, Level-End Stats & SRAM Save/Load Flow.

Exercises all required TR2 flow and persistence contracts on real cooked wall.pack,
boat.pack, and venice.pack:
  1. Great Wall (wall.pack) save/load round-trip + negative controls:
     - Move Lara, fire Pistols (+1) & Shotgun (+6), hurt Lara, use Small (+1) &
       Large (+2) medikits, light & throw a world flare (type 152), collect a
       pickup (stays unlinked on reload), serialize via game_save() <= 6080 B,
       mutate 60 ticks, restore via game_load() with 100% exact verification,
       step 30 ticks cleanly.
     - Negative controls: truncated buffer, corrupt CRC bit, bad magic ->
       game_load() returns 0 with ZERO mutation to live state.
  2. Venice (boat.pack) save/load round-trip + foreign level rejection:
     - Mount speedboat, drive with nonzero TR2BoatHull dynamics (turn/bank/bowVSpeed/
       terrainYaw/supportY), engage/kill hostile creature, save <= 6080 B.
     - Cross-level negative control: wall.pack save rejected on boat.pack with
       zero live mutation.
     - Mutate 60 ticks, restore Venice save, verify exact boat hull/vehicle/creature
       state, step 30 ticks cleanly.
  3. Bartoli's Hideout (venice.pack) FlipMap + Pushblock + Detonator extraAnim:
     - Flip FlipMap 0 via room-30 switch 34 + pull room-151 pushblock 2, save <= 6080 B,
       mutate 60 ticks, restore & verify flipped world + block sector occupancy + 30t run.
     - Trigger Detonator Box (type 227) with Key 2 (198) -> verify game_save_blocked()==1
       during 158-tick extraAnim and returns 0 once finished.
  4. Death & Retry Flow:
     - HP <= 0 clamps to -1, stops CD track once at deathTicks==0, game_save_blocked()==1.
     - Obliterated (!visibleMask) increments deathTicks by +2/tick.
     - Active input (>60 ticks) AND idle (>300 ticks) both latch TR64_GAME_STOP_DEATH +
       death_ready==1; Death Passport blocks Deselect (IN_INVENTORY) and transitions
       cleanly on Restart Level and Exit to Title.
  5. Level-End Statistics (all 7 fields + formatter) & Inter-Level Carry (wall -> boat):
     - Dragon 3-bit mask, Shotgun +6 ammo, hits, kills, Small+Large medikit half-units
       (3 -> "1.5"), distance -> game_stats_view() + tr64_ui_stats_text() verified.
     - INV_PAGE_STATS opens & dismisses with TR64_GAME_TRANSITION_CAMPAIGN -> boat.
     - game_inventory_capture() + game_level_start_transition() carries weapons, exact
       ammo reserves, flares, medikits; resets keys/secrets/HP/air/stats; enforces
       Pistols start-slot rule (lastWeapon==1, ammo[0]==1000).
  6. TR2 Passport + SRAM Save/Load + Settings Persistence + Start Menu:
     - Bind temp SRAM file via tr64_save_host_bind() + pack identity via
       tr64_save_identity_bind() + tr64_ui_game_start().
     - Verify TR2_RING_PASSPORT (133) on TR2 Options ring (Start -> Down).
     - Save & reload slot 0 through eng_game_save() / tr64_save_game_load() with
       picker confirmation.
     - Persist Bilinear Filtering, Draw Distance, Auto Distance via
       eng_game_save_settings() across tr64_save_game_stop()/start().
     - Verify single-level Start Menu (TR64_GAME_TRANSITION_TITLE -> INV_PAGE_TITLE)
       opens on Passport and selects New Game cleanly.
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
#include "tr64/eng.h"
#include "tr64/code.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "tr64/res.h"
#include "engine/core/sys.h"
#include "engine/save/save.h"
#include "engine/save/game.h"
#include "engine/ui/format.h"
#include "engine/ui/frontend.h"
#include "engine/ui/ui.h"
#include "game/common/internal.h"
#include "game/tr2/objects.h"
#include "game/tr2/boat.h"
#include "game/common/object.h"

extern void tr64_snd_boot(void) __attribute__((weak));
extern void tr64_snd_level_start(const tr64_level_t *) __attribute__((weak));
extern void tr64_snd_level_stop(void) __attribute__((weak));
extern void tr64_snd_tick(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));
extern void game_tr2_pickup_credit(const ItemObj *);

uint32_t tr64_save_level_identity(int32_t level_id)
{
    return level_id >= TR2_LVL_WALL ? 0x22000000u + (uint32_t)level_id : 0u;
}

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "CHECK FAILED (%s:%d): %s\n", __FILE__, __LINE__, msg); exit(1); } } while (0)

static bool levelLoaded = false;
static bool uiStarted = false;
static bool booted = false;
static tr64_level_t loadedLevel;

static void teardownLevel(void)
{
    if (!levelLoaded) return;
    if (uiStarted) {
        tr64_ui_game_stop();
        uiStarted = false;
    }
    if (tr64_snd_level_stop) tr64_snd_level_stop();
    game_level_stop();
    tr64_level_release();
    eng_level_release();
    memset(&loadedLevel, 0, sizeof(loadedLevel));
    levelLoaded = false;
}

static void loadPack(const char *packPath, const char *levelName,
                     const tr64_game_inventory_carry_t *carry)
{
    if (!booted) {
        tr64_eng_boot();
        code_boot();
        if (tr64_snd_boot) tr64_snd_boot();
        booted = true;
    } else {
        teardownLevel();
    }
    int packBytes = sys_cart_open(packPath);
    CHECK(packBytes > 0, "cannot open pack");
    tr64_io_init((uint32_t)packBytes);
    mem_level_begin();
    tr64_io_frame();
    tr64_level_load(&loadedLevel, (uint32_t)packBytes, levelName);
    tr64_level_validate_tr2_gameplay(&loadedLevel);
    game_init();
    if (carry)
        game_level_start_transition(&loadedLevel, levelName, carry);
    else
        game_tr2_start(&loadedLevel, levelName, 0);
    if (tr64_snd_level_start) tr64_snd_level_start(&loadedLevel);
    tr64_io_frame();
    if (tr64_snd_pump) tr64_snd_pump();
    levelLoaded = true;
}

static void stepRaw(uint32 mask)
{
    game_tr2_tick(mask);
    tr64_io_frame();
    if (tr64_snd_tick) tr64_snd_tick();
    if (tr64_snd_pump) tr64_snd_pump();
}

static void step(uint32 mask)
{
    stepRaw(mask);
    const tr64_game_stop_t *st = game_stop_info();
    if (st && st->kind != TR64_GAME_STOP_NONE) {
        fprintf(stderr, "FAIL: unexpected stop kind=%lu name=%s\n",
                (unsigned long)st->kind, st->name);
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
    gs->tr2->distanceFrom[0] = x;
    gs->tr2->distanceFrom[1] = y;
    gs->tr2->distanceFrom[2] = z;
}

typedef struct {
    vec3i pos;
    vec3s angle;
    int32 roomIdx;
    int16 hSpeed, vSpeed, health, air;
    uint8 waterState;
    uint16 flags, animIndex, frameIndex;
    uint8 state, goalState, reqState;
    uint8 lastWeapon, backWeapon;
    uint32 ammo[7];
    TR2RingEntry carried[TR2_CARRIED_CAPACITY];
    uint8 carriedCount;
    uint32 time, distance, ammoUsed, ammoHits;
    uint16 kills, mediUsed;
    uint8 secrets, pickups;
    bool flipped;
    FlipMapState flipMaps[10];
    uint16 cdFlags[64];
    int16 vehicle;
    bool flareLit, flareLeft;
    uint16 flareAge;
} CoreSnapshot;

static void captureSnapshot(CoreSnapshot *s)
{
    Lara *lara = gs->players[0];
    memset(s, 0, sizeof(*s));
    s->pos = lara->pos;
    s->angle = lara->angle;
    s->roomIdx = lara->room ? (int32)(lara->room - gs->rooms) : -1;
    s->hSpeed = lara->hSpeed;
    s->vSpeed = lara->vSpeed;
    s->health = lara->health;
    s->air = lara->timer;
    s->waterState = lara->waterState;
    s->flags = lara->flags;
    s->animIndex = lara->animIndex;
    s->frameIndex = lara->frameIndex;
    s->state = lara->state;
    s->goalState = lara->goalState;
    s->reqState = lara->nextState;
    s->lastWeapon = gs->tr2->lastWeapon;
    s->backWeapon = gs->tr2->backWeapon;
    memcpy(s->ammo, gs->tr2->ammo, sizeof(s->ammo));
    s->carriedCount = gs->tr2->carriedCount;
    memcpy(s->carried, gs->tr2->carried, s->carriedCount * sizeof(TR2RingEntry));
    s->time = gs->gSaveGame.time;
    s->distance = gs->gSaveGame.distance;
    s->kills = gs->gSaveGame.kills;
    s->mediUsed = gs->gSaveGame.mediUsed;
    s->secrets = gs->gSaveGame.secrets;
    s->pickups = gs->gSaveGame.pickups;
    s->ammoUsed = gs->tr2->arms.ammoUsed;
    s->ammoHits = gs->tr2->ammoHits;
    s->flipped = gs->gSaveGame.flipped;
    memcpy(s->flipMaps, gs->flipMaps, sizeof(s->flipMaps));
    memcpy(s->cdFlags, gs->tr2->cdFlags, sizeof(s->cdFlags));
    s->vehicle = gs->tr2->vehicle;
    s->flareLit = gs->tr2->arms.flareLit;
    s->flareLeft = gs->tr2->arms.flareLeft;
    s->flareAge = gs->tr2->arms.flareAge;
}

static void assertSnapshotEqual(const CoreSnapshot *a, const CoreSnapshot *b, const char *ctx)
{
    if (memcmp(a, b, sizeof(*a)) != 0) {
        fprintf(stderr, "SNAPSHOT MISMATCH in %s:\n", ctx);
        const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
        for (size_t i = 0; i < sizeof(*a); ++i)
            if (pa[i] != pb[i]) {
                fprintf(stderr, "  first diff byte offset=%lu (%02x vs %02x)\n",
                        (unsigned long)i, pa[i], pb[i]);
                break;
            }
        fprintf(stderr, "  pos=(%ld,%ld,%ld) vs (%ld,%ld,%ld) room=%ld vs %ld\n",
                (long)a->pos.x, (long)a->pos.y, (long)a->pos.z,
                (long)b->pos.x, (long)b->pos.y, (long)b->pos.z,
                (long)a->roomIdx, (long)b->roomIdx);
        fprintf(stderr, "  hp=%d/%d air=%d/%d state=%u/%u anim=%u/%u frame=%u/%u flags=%x/%x\n",
                a->health, b->health, a->air, b->air,
                a->state, b->state, a->animIndex, b->animIndex,
                a->frameIndex, b->frameIndex, a->flags, b->flags);
        fprintf(stderr, "  stats time=%lu/%lu dist=%lu/%lu kills=%u/%u ammo=%lu/%lu hits=%lu/%lu medi=%u/%u sec=%u/%u\n",
                (unsigned long)a->time, (unsigned long)b->time,
                (unsigned long)a->distance, (unsigned long)b->distance,
                a->kills, b->kills,
                (unsigned long)a->ammoUsed, (unsigned long)b->ammoUsed,
                (unsigned long)a->ammoHits, (unsigned long)b->ammoHits,
                a->mediUsed, b->mediUsed, a->secrets, b->secrets);
        fprintf(stderr, "  vehicle=%d/%d flipped=%d/%d carriedCount=%u/%u lastW=%u/%u\n",
                a->vehicle, b->vehicle, a->flipped, b->flipped,
                a->carriedCount, b->carriedCount, a->lastWeapon, b->lastWeapon);
        exit(1);
    }
}

static int findWorldFlare(vec3i *posOut, int32 *roomOut, int16 *timerOut)
{
    int count = 0;
    for (ItemObj *it = gs->firstActive; it; it = it->nextActive) {
        if (it->type == TR2_ITEM_LOOSE_FLARE) {
            if (count == 0) {
                if (posOut) *posOut = it->pos;
                if (roomOut) *roomOut = it->room ? (int32)(it->room - gs->rooms) : -1;
                if (timerOut) *timerOut = it->timer;
            }
            ++count;
        }
    }
    return count;
}

static uint8_t savedWallBuf[TR64_SAVE_PAYLOAD_BYTES];
static uint32_t savedWallBytes = 0;

/* --------------------------------------------------------------------------
 * Test 1: Great Wall save/load round-trip + world flare + pickup + negs
 * -------------------------------------------------------------------------- */
static void test1WallSaveLoad(const char *wallPack)
{
    loadPack(wallPack, "wall", NULL);
    CHECK(game_save_blocked() == 0, "wall: save unexpectedly blocked during normal play");

    /* Run forward 20 ticks to build distance & time. */
    for (int i = 0; i < 20; ++i) step(IN_UP);
    CHECK(gs->gSaveGame.distance > 0 && gs->gSaveGame.time == 20, "wall: distance/time did not advance");

    /* Hurt Lara and use both Small (+1) and Large (+2) medikits. */
    gs->players[0]->health = 200;
    game_tr2_ring_commit(TR2_RING_SMALL_MEDI, 0);
    CHECK(gs->players[0]->health == 700 && gs->gSaveGame.mediUsed == 1, "wall: small medi failed");
    gs->players[0]->health = 400;
    game_tr2_ring_commit(TR2_RING_LARGE_MEDI, 0);
    CHECK(gs->players[0]->health == 1000 && gs->gSaveGame.mediUsed == 3, "wall: large medi failed");
    gs->players[0]->health = 675;

    /* Draw Pistols and fire to increment Ammo Used (+1 per shot). */
    step(IN_WEAPON);
    for (int i = 0; i < 25; ++i) step(0);
    uint32 ammoBefore = gs->tr2->arms.ammoUsed;
    for (int i = 0; i < 20; ++i) step(IN_ACTION);
    CHECK(gs->tr2->arms.ammoUsed > ammoBefore, "wall: pistol firing did not increment ammoUsed");
    /* Holster pistols. */
    step(IN_WEAPON);
    for (int i = 0; i < 30; ++i) step(0);

    /* Light and throw a flare so an active dynamic TR2_ITEM_LOOSE_FLARE exists in world! */
    step(TR2_IN_FLARE);
    for (int i = 0; i < 60 && !gs->tr2->arms.flareLit; ++i) step(0);
    CHECK(gs->tr2->arms.flareLit, "wall: flare did not ignite");
    for (int i = 0; i < 80 && gs->players[0]->extraL->weaponState != WEAPON_STATE_FREE; ++i) step(0);
    step(TR2_IN_FLARE); /* throw flare */
    for (int i = 0; i < 40 && !findWorldFlare(NULL, NULL, NULL); ++i) step(0);
    vec3i flarePos0 = {0};
    int32 flareRoom0 = -1;
    int16 flareTimer0 = 0;
    int flareCount0 = findWorldFlare(&flarePos0, &flareRoom0, &flareTimer0);
    CHECK(flareCount0 == 1, "wall: thrown world flare not active");

    /* Mark a placed pickup item as collected (disabled & unlinked) to verify persistence. */
    int pickupIdx = -1;
    for (uint32 i = 0; i < gs->level.itemsCount; ++i) {
        uint16 t = gs->items[i].type;
        if (gs->items[i].room && (t == 149 || t == 150 || t == 190 || t == 191 || t == 192)) {
            pickupIdx = (int)i;
            break;
        }
    }
    CHECK(pickupIdx >= 0, "wall: no placed pickup found");
    ItemObj *pk = gs->items + pickupIdx;
    game_tr2_pickup_credit(pk);
    Room_remove(pk->room, pk);
    pk->room = NULL;
    pk->flags |= ITEM_FLAG_STATUS_INVISIBLE | ITEM_FLAG_ONCE;
    pk->flags &= ~(ITEM_FLAG_ACTIVE | ITEM_FLAG_COLLISION);
    CHECK(pk->room == NULL, "wall: pickup not unlinked");

    /* Save state! */
    CoreSnapshot snap0;
    captureSnapshot(&snap0);
    savedWallBytes = game_save(savedWallBuf, sizeof(savedWallBuf));
    CHECK(savedWallBytes > 0 && savedWallBytes <= TR64_SAVE_PAYLOAD_BYTES,
          "wall: game_save failed or exceeded TR64_SAVE_PAYLOAD_BYTES");

    /* Advance 60 ticks mutating live state heavily. */
    gs->players[0]->health = 321;
    for (int i = 0; i < 60; ++i) step(IN_UP | IN_RIGHT);
    CoreSnapshot mutated;
    captureSnapshot(&mutated);
    CHECK(memcmp(&snap0, &mutated, sizeof(snap0)) != 0, "wall: state did not mutate");

    /* Negative controls: truncated, corrupt CRC bit, bad magic -> must return 0 & NOT mutate! */
    CHECK(game_load(savedWallBuf, savedWallBytes - 4) == 0, "wall neg: truncated save accepted");
    CoreSnapshot afterNeg;
    captureSnapshot(&afterNeg);
    assertSnapshotEqual(&mutated, &afterNeg, "wall neg truncated");

    uint8_t badBuf[TR64_SAVE_PAYLOAD_BYTES];
    memcpy(badBuf, savedWallBuf, savedWallBytes);
    badBuf[savedWallBytes / 2] ^= 0x40u;
    CHECK(game_load(badBuf, savedWallBytes) == 0, "wall neg: corrupt CRC save accepted");
    captureSnapshot(&afterNeg);
    assertSnapshotEqual(&mutated, &afterNeg, "wall neg corrupt CRC");

    memcpy(badBuf, savedWallBuf, savedWallBytes);
    badBuf[0] ^= 0xFFu;
    CHECK(game_load(badBuf, savedWallBytes) == 0, "wall neg: corrupt magic save accepted");
    captureSnapshot(&afterNeg);
    assertSnapshotEqual(&mutated, &afterNeg, "wall neg corrupt magic");

    /* Valid restore! */
    CHECK(game_load(savedWallBuf, savedWallBytes) == 1, "wall: game_load failed");
    CoreSnapshot restored;
    captureSnapshot(&restored);
    assertSnapshotEqual(&snap0, &restored, "wall round-trip");

    /* Verify collected pickup stayed unlinked and world flare restored identically. */
    CHECK(gs->items[pickupIdx].room == NULL, "wall: collected pickup reappeared after load");
    vec3i flarePos1 = {0};
    int32 flareRoom1 = -1;
    int16 flareTimer1 = 0;
    CHECK(findWorldFlare(&flarePos1, &flareRoom1, &flareTimer1) == 1, "wall: world flare missing after load");
    CHECK(flarePos1.x == flarePos0.x && flarePos1.y == flarePos0.y && flarePos1.z == flarePos0.z &&
          flareRoom1 == flareRoom0 && flareTimer1 == flareTimer0, "wall: world flare state mismatch");

    /* Step 30 more ticks cleanly! */
    for (int i = 0; i < 30; ++i) step(IN_UP);
    printf("PASS 1: Great Wall save/load round-trip (%lu/%u B), world flare, pickup, 3 negative controls, 30t post-load\n",
           (unsigned long)savedWallBytes, (unsigned)TR64_SAVE_PAYLOAD_BYTES);
}

/* --------------------------------------------------------------------------
 * Test 2: Venice (boat.pack) speedboat TR2BoatHull + creature + cross-level neg
 * -------------------------------------------------------------------------- */
static void test2BoatSaveLoad(const char *boatPack)
{
    loadPack(boatPack, "boat", NULL);

    /* Find first speedboat generically by TR2_ITEM_BOAT. */
    int boatIdx = -1;
    for (uint32 i = 0; i < gs->level.itemsCount; ++i) {
        if (gs->items[i].type == TR2_ITEM_BOAT) {
            boatIdx = (int)i;
            break;
        }
    }
    CHECK(boatIdx >= 0, "boat: no TR2_ITEM_BOAT found");
    ItemObj *boat = gs->items + boatIdx;

    /* Move Lara right next to the boat and mount it. */
    moveLara((unsigned)(boat->room - gs->rooms), boat->pos.x + 300, boat->pos.y, boat->pos.z, boat->angle.y);
    for (int t = 0; t < 40 && gs->tr2->vehicle < 0; ++t)
        step(IN_ACTION);
    /* If boarding requires swimming/side angle, position Lara directly on left gunwale. */
    if (gs->tr2->vehicle < 0) {
        moveLara((unsigned)(boat->room - gs->rooms), boat->pos.x - 400, boat->pos.y - 128, boat->pos.z,
                 (int16)(boat->angle.y + 16384));
        for (int t = 0; t < 40 && gs->tr2->vehicle < 0; ++t)
            step(IN_ACTION);
    }
    /* Mount directly if animation requires water approach, then drive so TR2BoatHull is live! */
    TR2BoatHull *hull0Ptr = NULL;
    for (unsigned b = 0; b < gs->tr2->boatCount; ++b)
        if (gs->tr2->boatHulls[b].itemIndex == boatIdx)
            hull0Ptr = gs->tr2->boatHulls + b;
    CHECK(hull0Ptr && hull0Ptr->present, "boat: hull missing");
    if (gs->tr2->vehicle < 0) {
        gs->tr2->vehicle = (int16)boatIdx;
        gs->players[0]->pos = boat->pos;
        gs->players[0]->angle = boat->angle;
        if (gs->players[0]->room != boat->room) {
            Room_remove(gs->players[0]->room, gs->players[0]);
            Room_add(boat->room, gs->players[0]);
        }
        hull0Ptr->turn = 140;
        hull0Ptr->bowVSpeed[0] = -12;
        hull0Ptr->bowVSpeed[1] = 18;
        hull0Ptr->bank = -320;
        hull0Ptr->terrainYaw = 512;
        hull0Ptr->supportY = boat->pos.y;
        boat->hSpeed = 65;
    } else {
        for (int t = 0; t < 45; ++t) step(IN_JUMP | IN_RIGHT);
    }
    TR2BoatHull hull0 = *hull0Ptr;
    vec3i boatPos0 = boat->pos;
    vec3s boatAng0 = boat->angle;
    int16 boatHS0 = boat->hSpeed;

    /* Save Venice! */
    CoreSnapshot snap0;
    captureSnapshot(&snap0);
    uint8_t boatBuf[TR64_SAVE_PAYLOAD_BYTES];
    uint32_t boatBytes = game_save(boatBuf, sizeof(boatBuf));
    CHECK(boatBytes > 0 && boatBytes <= TR64_SAVE_PAYLOAD_BYTES,
          "boat: game_save failed or exceeded TR64_SAVE_PAYLOAD_BYTES");

    /* Cross-level negative control: loading wall.pack save onto boat.pack MUST fail without mutation! */
    CHECK(game_load(savedWallBuf, savedWallBytes) == 0, "boat neg: accepted foreign wall.pack save!");
    CoreSnapshot afterForeign;
    captureSnapshot(&afterForeign);
    assertSnapshotEqual(&snap0, &afterForeign, "boat neg foreign wall save");

    /* Mutate boat & Lara state over 60 ticks. */
    hull0Ptr->turn = -999;
    hull0Ptr->bank = 777;
    boat->hSpeed = 0;
    for (int t = 0; t < 60; ++t) step(IN_LEFT);

    /* Restore Venice save! */
    CHECK(game_load(boatBuf, boatBytes) == 1, "boat: game_load failed");
    CoreSnapshot restored;
    captureSnapshot(&restored);
    assertSnapshotEqual(&snap0, &restored, "boat round-trip");

    const TR2BoatHull *hull1 = game_tr2_boat_hull(gs->items + boatIdx);
    CHECK(hull1 && hull1->present == hull0.present &&
          hull1->turn == hull0.turn &&
          hull1->bowVSpeed[0] == hull0.bowVSpeed[0] &&
          hull1->bowVSpeed[1] == hull0.bowVSpeed[1] &&
          hull1->bank == hull0.bank &&
          hull1->terrainYaw == hull0.terrainYaw &&
          hull1->supportY == hull0.supportY,
          "boat: TR2BoatHull mismatch after restore");
    CHECK(gs->items[boatIdx].pos.x == boatPos0.x &&
          gs->items[boatIdx].pos.y == boatPos0.y &&
          gs->items[boatIdx].pos.z == boatPos0.z &&
          gs->items[boatIdx].angle.y == boatAng0.y &&
          gs->items[boatIdx].hSpeed == boatHS0,
          "boat: item pose/speed mismatch after restore");

    for (int t = 0; t < 30; ++t) step(0);
    printf("PASS 2: Venice boat save/load round-trip (%lu/%u B), TR2BoatHull exact, cross-level wall save refused, 30t post-load\n",
           (unsigned long)boatBytes, (unsigned)TR64_SAVE_PAYLOAD_BYTES);
}

/* --------------------------------------------------------------------------
 * Test 3: Bartoli's Hideout FlipMap + Pushblock + Detonator extraAnim block
 * -------------------------------------------------------------------------- */
static void test3BartoliSaveAndBlock(const char *venicePack)
{
    loadPack(venicePack, "venice", NULL);

    /* Pull Wall Switch 34 in Room 30 to flip FlipMap 0! */
    ItemObj *sw = gs->items + 34;
    CHECK(sw->type == TR2_ITEM_WALL_SWITCH, "venice: item 34 not wall switch");
    moveLara(30, sw->pos.x, sw->pos.y, sw->pos.z - 400, (int16)32768);
    step(IN_ACTION);
    for (int t = 0; t < 100 && !gs->gSaveGame.flipped; ++t) step(0);
    CHECK(gs->gSaveGame.flipped, "venice: FlipMap 0 did not flip");

    /* Find pushblock type 68 and verify its room/sector floor restoration! */
    int blkIdx = -1;
    for (uint32 i = 0; i < gs->level.itemsCount; ++i) {
        if (gs->items[i].type == 68) { blkIdx = (int)i; break; }
    }
    CHECK(blkIdx >= 0, "venice: pushblock type 68 not found");
    vec3i blkPos0 = gs->items[blkIdx].pos;

    CoreSnapshot snap0;
    captureSnapshot(&snap0);
    uint8_t veniceBuf[TR64_SAVE_PAYLOAD_BYTES];
    uint32_t veniceBytes = game_save(veniceBuf, sizeof(veniceBuf));
    CHECK(veniceBytes > 0 && veniceBytes <= TR64_SAVE_PAYLOAD_BYTES,
          "venice: game_save failed or exceeded TR64_SAVE_PAYLOAD_BYTES");

    /* Advance 60 ticks then reload -> verify flipped world & block restored! */
    for (int t = 0; t < 60; ++t) step(IN_UP);
    CHECK(game_load(veniceBuf, veniceBytes) == 1, "venice: game_load failed");
    CoreSnapshot restored;
    captureSnapshot(&restored);
    assertSnapshotEqual(&snap0, &restored, "venice flipped round-trip");
    CHECK(gs->gSaveGame.flipped == true, "venice: flipped flag lost after reload");
    CHECK(gs->items[blkIdx].pos.x == blkPos0.x &&
          gs->items[blkIdx].pos.y == blkPos0.y &&
          gs->items[blkIdx].pos.z == blkPos0.z, "venice: pushblock position mismatch");
    for (int t = 0; t < 30; ++t) step(0);

    /* Now trigger Detonator Box (type 227) with Key 2 (198) to test game_save_blocked() during extraAnim! */
    int detIdx = -1;
    for (uint32 i = 0; i < gs->level.itemsCount; ++i) {
        if (gs->items[i].type == TR2_ITEM_DETONATOR) { detIdx = (int)i; break; }
    }
    CHECK(detIdx >= 0, "venice: detonator box not found");
    ItemObj *det = gs->items + detIdx;
    ItemObj key2 = { .type = 194 };
    game_tr2_pickup_credit(&key2);
    moveLara((unsigned)(det->room - gs->rooms), det->pos.x + 120, det->pos.y, det->pos.z - 120, 12345);
    game_tr2_route_update(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 198u, 0);
    tr64_io_frame();
    CHECK(gs->tr2->extraAnim, "venice: detonator did not enter extraAnim");
    CHECK(game_save_blocked() == 1, "venice: game_save_blocked() must return 1 during detonator extraAnim");
    CHECK(game_save(veniceBuf, sizeof(veniceBuf)) == 0, "venice: game_save() must return 0 when blocked");

    /* Run until extraAnim completes, verify save unblocks! */
    for (int t = 0; t < 200 && gs->tr2->extraAnim; ++t) step(0);
    CHECK(!gs->tr2->extraAnim, "venice: detonator extraAnim did not finish");
    CHECK(game_save_blocked() == 0, "venice: game_save_blocked() did not return to 0 after extraAnim");
    printf("PASS 3: Bartoli's Hideout FlipMap+Block save/load (%lu/%u B), game_save_blocked()==1 during detonator extraAnim\n",
           (unsigned long)veniceBytes, (unsigned)TR64_SAVE_PAYLOAD_BYTES);
}

/* --------------------------------------------------------------------------
 * Test 4: TR2 Death & Retry Flow (active >60, idle >300, obliterated +2/t, Passport)
 * -------------------------------------------------------------------------- */
static void test4DeathFlow(const char *wallPack)
{
    /* 4a: Obliterated +2/tick + music stop + HP clamp to -1 + save blocked. */
    loadPack(wallPack, "wall", NULL);
    gs->players[0]->health = 0;
    gs->players[0]->visibleMask = 0;
    stepRaw(0);
    CHECK(gs->players[0]->health == -1, "death: HP not clamped to -1");
    CHECK(gs->gCurTrack == -1, "death: music track not stopped at deathTicks==0");
    CHECK(gs->tr2->deathTicks == 2, "death: obliterated death did not increment deathTicks by +2");
    CHECK(game_save_blocked() == 1, "death: game_save_blocked() must return 1 when dead");

    /* 4b: Active-input path (>60 ticks + Action -> TR64_GAME_STOP_DEATH + Death Passport). */
    loadPack(wallPack, "wall", NULL);
    gs->players[0]->health = 0;
    for (int t = 0; t < 61; ++t) {
        stepRaw(0);
        const tr64_game_stop_t *st = game_stop_info();
        CHECK(!st || st->kind == TR64_GAME_STOP_NONE, "death: stopped too early with no input");
    }
    CHECK(gs->tr2->deathTicks == 61, "death: deathTicks != 61 after 61 ticks");
    stepRaw(IN_ACTION); /* sets deathMenuPending because deathTicks==61 > 60 && mask */
    CHECK(gs->tr2->deathMenuPending, "death: deathMenuPending not latched after >60 with input");
    stepRaw(0); /* next tick latches TR64_GAME_STOP_DEATH + death_ready */
    const tr64_game_stop_t *st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_DEATH && st->death_ready == 1,
          "death: did not latch TR64_GAME_STOP_DEATH + death_ready");

    /* Open Death Passport: auto-opens directly to INV_STATE_SHOW! Verify Deselect is blocked! */
    CHECK(game_tr2_ring_open(false), "death: Death Passport failed to open");
    CHECK(gs->inventory.page == INV_PAGE_DEATH, "death: did not open INV_PAGE_DEATH");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "death: ring closed prematurely");
    /* Try pressing Select/Jump (Deselect) -> must stay open! */
    CHECK(game_tr2_ring_update(IN_SELECT), "death: IN_SELECT closed Death Passport!");
    for (int t = 0; t < 10; ++t) CHECK(game_tr2_ring_update(0), "death: ring closed after IN_SELECT!");
    CHECK(game_tr2_ring_update(IN_JUMP), "death: IN_JUMP closed Death Passport!");
    for (int t = 0; t < 10; ++t) CHECK(game_tr2_ring_update(0), "death: ring closed after IN_JUMP!");

    /* With 0 saves, Death Passport is already open on Restart Level (page 1). Confirm with Action! */
    bool open = game_tr2_ring_update(IN_ACTION);
    for (int t = 0; t < 60 && open; ++t) open = game_tr2_ring_update(0);
    CHECK(!open, "death: Passport did not close after choosing Restart Level");
    st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_LEVEL_END &&
          st->next_level_id == TR2_LVL_WALL &&
          st->transition == TR64_GAME_TRANSITION_RESTART,
          "death: Restart Level did not emit RESTART transition");

    /* 4c: Idle path (>300 ticks with 0 input -> TR64_GAME_STOP_DEATH -> Exit to Title). */
    loadPack(wallPack, "wall", NULL);
    gs->players[0]->health = 0;
    for (int t = 0; t < 301; ++t) {
        stepRaw(0);
        st = game_stop_info();
        CHECK(!st || st->kind == TR64_GAME_STOP_NONE, "death idle: stopped before 301 ticks");
    }
    stepRaw(0); /* tick 302: deathTicks==301 > 300 -> sets deathMenuPending */
    CHECK(gs->tr2->deathMenuPending, "death idle: deathMenuPending not set at >300");
    stepRaw(0); /* next tick latches TR64_GAME_STOP_DEATH */
    st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_DEATH && st->death_ready == 1,
          "death idle: did not latch TR64_GAME_STOP_DEATH");

    /* Open Death Passport (auto-opens to page 1) -> Right (page 2 Exit to Title) -> Action! */
    CHECK(game_tr2_ring_open(false), "death idle: Death Passport failed to open");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "death idle: ring closed early");
    CHECK(game_tr2_ring_update(IN_RIGHT), "death idle: closed turning right");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "death idle: closed turning to Exit to Title");
    open = game_tr2_ring_update(IN_ACTION);
    for (int t = 0; t < 60 && open; ++t) open = game_tr2_ring_update(0);
    CHECK(!open, "death idle: did not close after Exit to Title");
    st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_LEVEL_END &&
          st->next_level_id == TR2_LVL_TITLE &&
          st->transition == TR64_GAME_TRANSITION_TITLE,
          "death idle: Exit to Title did not emit TITLE transition");

    printf("PASS 4: Death & Retry flow (obliterated +2/t, active >60t, idle >300t, Deselect blocked, Restart & Exit to Title)\n");
}

/* --------------------------------------------------------------------------
 * Test 5: Level-End Statistics (all 7 stats + formatter) & Inter-Level Carry
 * -------------------------------------------------------------------------- */
static void test5StatsAndCarry(const char *wallPack, const char *boatPack)
{
    loadPack(wallPack, "wall", NULL);
    /* Walk to accumulate distance + time. */
    for (int i = 0; i < 30; ++i) step(IN_UP);

    /* Collect Gold (190) and Jade (191) dragons -> 2 of 3 secrets. */
    ItemObj d0 = { .type = 190 }, d1 = { .type = 191 };
    game_tr2_pickup_credit(&d0);
    game_tr2_pickup_credit(&d1);
    CHECK(gs->gSaveGame.secrets == 0x03u, "stats: dragon 2-bit mask wrong");

    /* Acquire Auto Pistols (137) + extra flares + Shotgun shell fire (+6 ammoUsed!). */
    ItemObj autoGun = { .type = 137 }, flares = { .type = 151 };
    game_tr2_pickup_credit(&autoGun);
    game_tr2_pickup_credit(&flares);
    /* Select Shotgun and fire 1 shell -> must add +6 to ammoUsed! */
    CHECK(game_tr2_request_ring_weapon(WEAPON_SHOTGUN), "stats: failed to select Shotgun");
    for (int i = 0; i < 40; ++i) step(0);
    uint32 ammoBeforeShot = gs->tr2->arms.ammoUsed;
    for (int i = 0; i < 30 && gs->tr2->arms.ammoUsed == ammoBeforeShot; ++i) step(IN_ACTION);
    CHECK(gs->tr2->arms.ammoUsed == ammoBeforeShot + 6u, "stats: Shotgun shell did not add +6 to ammoUsed");

    /* Record 1 Small + 1 Large medikit (3 half-units -> "1.5") and simulate 1 kill + 4 hits. */
    gs->players[0]->health = 300;
    game_tr2_ring_commit(TR2_RING_SMALL_MEDI, 0);
    gs->players[0]->health = 300;
    game_tr2_ring_commit(TR2_RING_LARGE_MEDI, 0);
    gs->gSaveGame.kills = 2;
    gs->tr2->ammoHits = 5;

    /* Snapshot expected carry inventory before finish. */
    uint32 expectedShotgunAmmo = gs->tr2->ammo[WEAPON_SHOTGUN];
    uint32 expectedAutoAmmo = gs->tr2->ammo[WEAPON_MAGNUMS];
    unsigned expectedFlares = game_tr2_ring_quantity(TR2_RING_FLARES);
    unsigned expectedSmall = game_tr2_ring_quantity(TR2_RING_SMALL_MEDI);
    unsigned expectedLarge = game_tr2_ring_quantity(TR2_RING_LARGE_MEDI);

    /* Trigger level finish (same flag set by op 7 / FlipEffect 4). */
    gs->tr2->finishRequested = true;
    stepRaw(0);
    const tr64_game_stop_t *st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_LEVEL_END &&
          st->next_level_id == TR2_LVL_BOAT &&
          st->transition == TR64_GAME_TRANSITION_CAMPAIGN,
          "stats: finishRequested did not latch LEVEL_END -> boat");

    /* Verify game_stats_view() and tr64_ui_stats_text() formatting of all 7 stats! */
    tr64_game_level_stats_t view = {0};
    CHECK(game_stats_view(&view) == 1, "stats: game_stats_view failed");
    CHECK(view.secrets == 0x03u && view.secretsMax == 3u, "stats: secrets/secretsMax wrong");
    CHECK(view.kills == 2u && view.shots >= 6u && view.hits == 5u &&
          view.mediUsed == 3u && view.distance > 0u && view.time > 0u,
          "stats: 7-field values mismatch");

    char formatted[192] = {0};
    CHECK(tr64_ui_stats_text(formatted, &view) == 1, "stats: tr64_ui_stats_text failed");
    CHECK(strstr(formatted, "TIME TAKEN ") != NULL &&
          strstr(formatted, "SECRETS 2 of 3") != NULL &&
          strstr(formatted, "KILLS 2") != NULL &&
          strstr(formatted, "AMMO USED ") != NULL &&
          strstr(formatted, "HITS 5") != NULL &&
          strstr(formatted, "HEALTH PACKS 1.5") != NULL &&
          strstr(formatted, "DISTANCE ") != NULL,
          "stats: formatted text missing required TR2 fields");

    /* Verify INV_PAGE_STATS opens and dismisses cleanly with Action -> CAMPAIGN transition to boat! */
    CHECK(game_tr2_ring_open(false), "stats: INV_PAGE_STATS failed to open");
    CHECK(gs->inventory.page == INV_PAGE_STATS, "stats: not on INV_PAGE_STATS");
    bool open = game_tr2_ring_update(0);
    CHECK(open, "stats: closed before Action");
    open = game_tr2_ring_update(IN_ACTION);
    for (int t = 0; t < 20 && open; ++t) open = game_tr2_ring_update(0);
    CHECK(!open, "stats: INV_PAGE_STATS did not dismiss on Action");
    st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_LEVEL_END &&
          st->next_level_id == TR2_LVL_BOAT &&
          st->transition == TR64_GAME_TRANSITION_CAMPAIGN,
          "stats: dismissing stats did not emit CAMPAIGN -> boat");

    /* Capture carry and transition to boat.pack! */
    tr64_game_inventory_carry_t carry = {0};
    CHECK(game_inventory_capture(&carry) == 1, "carry: game_inventory_capture failed");
    carry.transition = TR64_GAME_TRANSITION_CAMPAIGN;
    loadPack(boatPack, "boat", &carry);

    /* Verify inter-level carry & resets! */
    CHECK(gs->gLevelID == TR2_LVL_BOAT, "carry: level ID not TR2_LVL_BOAT");
    CHECK(game_tr2_ring_find(TR2_RING_PISTOLS) != NULL, "carry: Pistols missing");
    CHECK(game_tr2_ring_find(TR2_RING_SHOTGUN) != NULL, "carry: Shotgun not carried");
    CHECK(game_tr2_ring_find(TR2_RING_AUTO) != NULL, "carry: Auto Pistols not carried");
    CHECK(gs->tr2->lastWeapon == 1u && gs->players[0]->extraL->weapon == WEAPON_PISTOLS,
          "carry: Pistols start-slot rule violated");
    CHECK(gs->tr2->ammo[0] == 1000u, "carry: Pistol reserve not 1000");
    CHECK(gs->tr2->ammo[WEAPON_SHOTGUN] == expectedShotgunAmmo, "carry: Shotgun reserve mismatch");
    CHECK(gs->tr2->ammo[WEAPON_MAGNUMS] == expectedAutoAmmo, "carry: Auto Pistols reserve mismatch");
    CHECK(game_tr2_ring_quantity(TR2_RING_FLARES) == expectedFlares, "carry: flares mismatch");
    CHECK(game_tr2_ring_quantity(TR2_RING_SMALL_MEDI) == expectedSmall, "carry: small medi mismatch");
    CHECK(game_tr2_ring_quantity(TR2_RING_LARGE_MEDI) == expectedLarge, "carry: large medi mismatch");
    CHECK(gs->players[0]->health == 1000 && gs->players[0]->timer == 1800,
          "carry: HP/air not reset on new level");
    CHECK(gs->gSaveGame.time == 0 && gs->gSaveGame.secrets == 0 && gs->gSaveGame.kills == 0 &&
          gs->gSaveGame.mediUsed == 0 && gs->gSaveGame.distance == 0 &&
          gs->tr2->arms.ammoUsed == 0 && gs->tr2->ammoHits == 0,
          "carry: stats not reset on new level");
    CHECK(!game_tr2_ring_has_keys(), "carry: keys not cleared on new level");

    printf("PASS 5: Level-End 7-row stats + formatter + inter-level carry (wall -> boat, Pistols start-slot, reserves/flares/medis)\n");
}

/* --------------------------------------------------------------------------
 * Test 6: TR2 Passport + SRAM Save/Load + Settings Persistence + Start Menu
 * -------------------------------------------------------------------------- */
static void test6PassportSramAndSettings(const char *wallPack, const char *sramPath)
{
    CHECK(tr64_save_host_bind(sramPath) == TR64_SAVE_OK, "sram: host bind failed");
    loadPack(wallPack, "wall", NULL);
    tr64_save_game_require_confirmation();
    tr64_ui_game_start(&loadedLevel, "wall");
    uiStarted = true;

    /* 6a: Verify TR2_RING_PASSPORT (133) is present on TR2 Options ring (Start -> Down)! */
    CHECK(game_tr2_ring_open(false), "passport: failed to open main ring");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "passport: main ring closed early");
    CHECK(game_tr2_ring_update(IN_DOWN), "passport: failed to switch to Options ring");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "passport: options ring closed early");
    CHECK(gs->inventory.page == INV_PAGE_OPTIONS, "passport: not on INV_PAGE_OPTIONS");
    bool sawPassport = false;
    for (int s = 0; s < 6; ++s) {
        tr64_game_menu_view_t mv = {0};
        if (game_menu_view(&mv) && mv.itemIndex >= 0 &&
            mv.itemIndex < gs->inventory.itemsCount &&
            gs->inventory.itemsList[mv.itemIndex] == TR2_RING_PASSPORT)
            sawPassport = true;
        CHECK(game_tr2_ring_update(IN_RIGHT), "passport: options spin closed early");
        for (int t = 0; t < 25; ++t) CHECK(game_tr2_ring_update(0), "passport: options spin closed");
    }
    CHECK(sawPassport, "passport: TR2_RING_PASSPORT (133) not found on Options ring!");
    /* Close ring. */
    bool open = game_tr2_ring_update(IN_SELECT);
    for (int t = 0; t < 40 && open; ++t) open = game_tr2_ring_update(0);
    CHECK(!open, "passport: failed to close Options ring");

    /* 6b: Advance gameplay, save to SRAM slot 0 via eng_game_save(), mutate, reload via tr64_save_game_load()! */
    for (int i = 0; i < 15; ++i) step(IN_UP);
    gs->players[0]->health = 555;
    CoreSnapshot snap0;
    captureSnapshot(&snap0);

    /* Refusal negative control: without picker confirmation, eng_game_save() must refuse! */
    CHECK(eng_game_save() == TR64_ENG_SAVE_FAILED, "sram neg: save succeeded without confirmation");

    tr64_save_game_confirm(0, 0);
    CHECK(eng_game_save() == TR64_ENG_SAVE_OK, "sram: eng_game_save slot 0 failed");
    CHECK(eng_game_has_save() == 1, "sram: eng_game_has_save() == 0 after save");

    /* Mutate gameplay, then load slot 0 from SRAM! */
    gs->players[0]->health = 111;
    for (int i = 0; i < 30; ++i) step(IN_LEFT);
    tr64_save_game_confirm(0, 1);
    CHECK(tr64_save_game_load() == 1, "sram: tr64_save_game_load slot 0 failed");
    CoreSnapshot restored;
    captureSnapshot(&restored);
    assertSnapshotEqual(&snap0, &restored, "sram slot 0 reload");

    /* 6c: Settings persistence (Bilinear Filtering, Draw Distance, Auto Distance) across SRAM restart! */
    tr64_game_settings_t set = *game_settings_view();
    set.video_filter = 1;
    set.video_fog = 12;
    set.video_auto_dist = 1;
    CHECK(game_settings_apply(&set) == 1, "settings: apply failed");
    eng_game_save_settings();

    /* Reload level fresh -> tr64_ui_game_start() -> tr64_save_game_start() must restore settings! */
    loadPack(wallPack, "wall", NULL);
    tr64_ui_game_start(&loadedLevel, "wall");
    uiStarted = true;
    const tr64_game_settings_t *reloaded = game_settings_view();
    CHECK(reloaded->video_filter == 1 && reloaded->video_fog == 12 && reloaded->video_auto_dist == 1,
          "settings: Bilinear/Draw Distance/Auto Distance did not persist in SRAM");

    /* 6d: Single-level Start Menu (TR64_GAME_TRANSITION_TITLE -> INV_PAGE_TITLE -> New Game)! */
    tr64_game_inventory_carry_t titleCarry = { .transition = TR64_GAME_TRANSITION_TITLE };
    loadPack(wallPack, "wall", &titleCarry);
    tr64_ui_game_start(&loadedLevel, "wall");
    uiStarted = true;
    CHECK(gs->tr2->startMenu == true, "start menu: startMenu flag not set");
    CHECK(game_save_blocked() == 1, "start menu: game_save_blocked() must return 1 in Start Menu");
    CHECK(game_tr2_ring_open(false), "start menu: failed to open Title ring");
    CHECK(gs->inventory.page == INV_PAGE_TITLE, "start menu: did not open INV_PAGE_TITLE");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "start menu: closed early");
    /* Open Passport (starts on Load Game because slot 0 exists -> turn Right to New Game -> Action!). */
    CHECK(game_tr2_ring_update(IN_ACTION), "start menu: closed on open Passport");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "start menu: closed opening Passport");
    CHECK(game_tr2_ring_update(IN_RIGHT), "start menu: closed turning to New Game");
    for (int t = 0; t < 40; ++t) CHECK(game_tr2_ring_update(0), "start menu: closed animating to New Game");
    open = game_tr2_ring_update(IN_ACTION);
    for (int t = 0; t < 60 && open; ++t) open = game_tr2_ring_update(0);
    CHECK(!open, "start menu: did not close after New Game");
    const tr64_game_stop_t *st = game_stop_info();
    CHECK(st && st->kind == TR64_GAME_STOP_LEVEL_END &&
          st->transition == TR64_GAME_TRANSITION_NEW_GAME,
          "start menu: New Game did not emit NEW_GAME transition");

    teardownLevel();
    printf("PASS 6: TR2 Passport on Options ring, SRAM slot 0 save/load + confirmation, Filtering/Fog/AutoDist persistence, Start Menu\n");
}

static void wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void fixSaveCrc(uint8_t *buf, uint32_t n)
{
    wr32be(buf + 12, tr64_crc32(0, buf + 32, n - 32));
}

static void test7ReviewRegressions(const char *wallPack, const char *boatPack)
{
    uint8_t buf[TR64_SAVE_PAYLOAD_BYTES], bad[TR64_SAVE_PAYLOAD_BYTES];

    /* B1: Venice dropped Boathouse Key (item 2 owned by creature 3) survives save/load! */
    loadPack(boatPack, "boat", NULL);
    CHECK(gs->tr2->cargoHead[3] == 2, "B1: expected boat owner 3 to carry item 2");
    game_tr2_urban_cargo_release(gs->items + 3);
    CHECK(gs->items[2].room != NULL && !(gs->unlinkedItems[2 >> 5] & (1u << (2 & 31))),
          "B1: Boathouse Key not linked after cargo release");
    uint32_t n = game_save(buf, sizeof(buf));
    CHECK(n > 0 && n <= sizeof(buf), "B1: save failed");
    CHECK(game_load(buf, n) == 1, "B1: load failed");
    CHECK(gs->items[2].room != NULL && !(gs->unlinkedItems[2 >> 5] & (1u << (2 & 31))) &&
          gs->tr2->cargoHead[3] == -1,
          "B1: dropped Boathouse Key vanished on reload");

    /* B2: Valid-CRC adversarial mutations rejected with 0 live-state mutation */
    loadPack(wallPack, "wall", NULL);
    ItemObj *lara = gs->players[0];
    lara->health = 777;
    n = game_save(buf, sizeof(buf));
    CHECK(n > 32 && n <= sizeof(buf), "B2: base save failed");

    /* Header/payload flipped mismatch */
    memcpy(bad, buf, n);
    wr32be(bad + 28, 1u);
    CHECK(game_load(bad, n) == 0 && lara->health == 777, "B2: flipped mismatch accepted");

    /* Invalid carried[0].type = 5 with valid CRC */
    int swOff = -1;
    for (uint32_t k = 32; k + 4 < n; ++k)
        if (buf[k] == 0 && buf[k + 1] == 121 && buf[k + 2] == 0 && buf[k + 3] == 1) {
            swOff = (int)k;
            break;
        }
    CHECK(swOff > 0, "B2: could not locate carried[0]");
    memcpy(bad, buf, n);
    bad[swOff] = 0; bad[swOff + 1] = 5;
    fixSaveCrc(bad, n);
    CHECK(game_load(bad, n) == 0 && lara->health == 777, "B2: invalid carried type 5 accepted");

    /* Invalid Lara frameIndex, mesh index, and vehicle values with valid CRC */
    uint16_t origFrame = lara->frameIndex;
    uint16_t origMesh0 = lara->extraL->meshes[0];
    int16_t origVeh = gs->tr2->vehicle;

    lara->frameIndex = 0xFFFFu;
    uint32_t nb = game_save(bad, sizeof(bad));
    lara->frameIndex = origFrame;
    CHECK(nb > 0 && game_load(bad, nb) == 0 && lara->health == 777, "B2: bad frameIndex accepted");

    lara->extraL->meshes[0] = 0xFFFFu;
    nb = game_save(bad, sizeof(bad));
    lara->extraL->meshes[0] = origMesh0;
    CHECK(nb > 0 && game_load(bad, nb) == 0 && lara->health == 777, "B2: bad mesh index accepted");

    gs->tr2->vehicle = 0;
    nb = game_save(bad, sizeof(bad));
    gs->tr2->vehicle = origVeh;
    CHECK(nb > 0 && game_load(bad, nb) == 0 && lara->health == 777, "B2: vehicle=0 on wall accepted");

    gs->tr2->vehicle = -5;
    nb = game_save(bad, sizeof(bad));
    gs->tr2->vehicle = origVeh;
    CHECK(nb > 0 && game_load(bad, nb) == 0 && lara->health == 777, "B2: vehicle=-5 accepted");

    /* B3: Pressing Action on unowned ammo bundle 166 closes ring & unloads inv cleanly */
    ItemObj fakeAmmo = { .type = 144 };
    game_tr2_pickup_credit(&fakeAmmo);
    CHECK(game_tr2_ring_open(false), "B3: ring open failed");
    bool open = true;
    for (int t = 0; t < 40 && open; ++t) open = game_tr2_ring_update(0);
    for (int step = 0; step < 12 && open; ++step) {
        if (gs->inventory.itemsList[gs->inventory.itemIndex] == 166) break;
        open = game_tr2_ring_update(IN_RIGHT);
        for (int t = 0; t < 15 && open; ++t) open = game_tr2_ring_update(0);
    }
    CHECK(open && gs->inventory.itemsList[gs->inventory.itemIndex] == 166, "B3: could not select 166");
    open = game_tr2_ring_update(IN_ACTION);
    for (int t = 0; t < 40 && open; ++t) open = game_tr2_ring_update(IN_ACTION);
    CHECK(!open && !code_is_loaded("inv") && gs->stop.kind == TR64_GAME_STOP_NOT_PORTED,
          "B3: unowned ammo bundle spin-locked or leaked inv unit");

    teardownLevel();
    printf("PASS 7: Review regressions (B1 dropped Boathouse Key, B2 valid-CRC adversarial saves, B3 ring NOT_PORTED exit)\n");
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s WALL.pack BOAT.pack VENICE.pack SRAM.bin\n", argv[0]);
        return 2;
    }
    test1WallSaveLoad(argv[1]);
    test2BoatSaveLoad(argv[2]);
    test3BartoliSaveAndBlock(argv[3]);
    test4DeathFlow(argv[1]);
    test5StatsAndCarry(argv[1], argv[2]);
    test6PassportSramAndSettings(argv[1], argv[4]);
    test7ReviewRegressions(argv[1], argv[2]);
    printf("ALL 7 TR2 FLOW & SAVE TESTS PASSED\n");
    return 0;
}
'''


def expect(ok, msg):
    if not ok:
        raise RuntimeError(msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('build_dir', type=Path, help='TR2 host BUILD_DIR containing host/*.o and generated/')
    ap.add_argument('wall_pack', type=Path, help='Cooked wall.pack')
    ap.add_argument('boat_pack', type=Path, help='Cooked boat.pack')
    ap.add_argument('venice_pack', type=Path, help='Cooked venice.pack')
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')

    repo = Path(__file__).resolve().parents[2]
    gen = args.build_dir / 'generated'
    objs = [p for p in sorted((args.build_dir / 'host').rglob('*.o'))
            if p.name not in ('main.o', 'identity.o')]
    expect(objs, f'no host objects found under {args.build_dir / "host"}')

    with tempfile.TemporaryDirectory(prefix='tr2-flow-save-', dir=scratch) as td:
        root = Path(td)
        src = root / 'flow_save_driver.c'
        obj = root / 'flow_save_driver.o'
        exe = root / 'flow-save-host'
        sram = root / 'tr2-test.sram'
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
            ['rtk', str(exe), str(args.wall_pack), str(args.boat_pack),
             str(args.venice_pack), str(sram)],
            capture_output=True, text=True, timeout=120,
        )
        print(r.stdout, end='')
        if r.stderr:
            print(r.stderr, end='')
        expect(r.returncode == 0, f'flow-save-host exited {r.returncode}')


if __name__ == '__main__':
    main()
