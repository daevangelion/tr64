#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed TR2 Level 4 (Opera House / OPERA.TR2) gameplay & cook test.

Exercises all 11 Opera House specification sections (docs/specs/wall/tr2-opera-house.md)
with positive and negative controls on the real cooked opera.pack:
  1. Boot, opera script metadata (track 31, secretsMax 3, next rig), unit residency
     (tr2_opera loaded, tr2_boat & tr2_creatures unloaded), 3-Dragon Uzi reward
     (+400 unowned / +320 owned).
  2. Puzzles (174..189): reverse ring priority (179 before 178), reach & yaw bounds,
     empty-ring refusal sound 2 + debounce, wrong-item refusal Y-1, cancel silence,
     alignment (0,0,327), clip 134 / state 43, rel frame 80 swap 182->186, completed
     non-interactive, subtype 3 OR-mask (Relay Box + lever -> Elevator; Circuit Board
     + lever -> FlipMap 0 + FlipEffect 10 + Gunman 189).
  3. Clear Bodies (trigger op 11): startup bit 15 -> cleanupEligible, corpse list
     registration on creature retirement, cargo preservation across clear, save/load
     persistence before and after op 11.
  4. Drawbridge (type 63, item 90): state 0 solid push-out & no floor, dynamic light
     (14/11 at joint 0 (3000,720,0)), state 1 non-solid + 256-unit slab floor/ceiling
     on 2 behind-yaw tiles (68,65)/(68,66) and not on pivot (68,64), raise back solid.
  5. Elevator (type 65, item 63): dynamic pool (yStart 2304, waitCounter), 90t open
     pause + 351t descent (+16/t to 7920) + 90t pause + 350t ascent (to 2320), room
     transfer, exact sec 4 2x2 footprint floor/ceiling & closed-door wall table,
     mid-trip save/load round-trip.
  6. Swinging Pendulums (58/96), Falling Sandbag (79), Spinning Fans (76/95):
     pendulum solid + non-stopping + 50 HP/1 blood/4 RNG; falling drop gravity +
     300 HP/t no blood + land status 4 solid; fans 213/217 + no wind force on 95 +
     0x06 blade 200 HP/3 blood + status 4 clears collidable (passable).
  7. Multi-Boulder / Rolling Crates (type 83, items 106 & 165): roll + ground 222 +
     camera bounce, 1024-unit wall stop (223 + goal 2 -> status 4), untriggered reset
     to authored (pos, room), airborne 100 HP/1 blood vs grounded instant kill
     (clip 139, state 46, 15 blood).
  8. Opera House Dual-Gun Boss (type 20, item 142): 150 HP, startup base+3/state 2 +
     +-45 deg yaw jitter, maxTurn 0 -> 546, run tilt, state 10 simultaneous dual fire
     (2x 3-tick muzzle flash 240, up to 100 HP), death clip +32 state 11 releasing
     cargo (141 + 2x148 unowned, 3x148 when 141 carried).
  9. Grenade Launcher (141/7/163/148/170/248/229): pickup + bundle conversion +
     back-weapon priority 3>6>7>8; draw +0 joint 10 swap @10 (s7), holster +5 @21,
     53t steady cadence, dry-click 48 -> Pistols, no muzzle light; projectile 248
     11t flat + arc @12, window 72 shatter pass-through, creature 30 HP + explosive
     death + cargo drop, 0 self-damage, explosion 229 28t + s105 + alternating
     12/10 & 13/11 dynamic lights.
 10. Full Opera House save/load round-trip (promoted socket 186, elevator, cleanup,
     grenade ammo) + peak paged code frames <= 24 with inv + save open.
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
#include "engine/save/save.h"
#include "game/common/internal.h"
#include "game/tr2/objects.h"
#include "game/tr2/enemy_urban.h"
#include "game/common/object.h"

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
#define ITEM_STATUS(it) ((it)->flags & ITEM_FLAG_STATUS)
#define ITEM_FLAG_STATUS_NONE 0u

static uint32 codeFramesUsed(void)
{
    tr64_mem_stats_t st;
    tr64_mem_stats(TR64_MEM_code, &st);
    return st.used / 4096u;
}

static uint32 codeFramesPeak(void)
{
    tr64_mem_stats_t st;
    tr64_mem_stats(TR64_MEM_code, &st);
    return st.peak / 4096u;
}

static bool bitGet(const uint32_t *bits, unsigned idx)
{
    return (bits[idx >> 5] & (1u << (idx & 31u))) != 0u;
}

static unsigned cleanupCount(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < gs->level.itemsCount && i < 256u; ++i)
        if (bitGet(gs->tr2->cleanupList, i))
            ++n;
    return n;
}

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
    lara->extraL->camera.target.pos = lara->pos;
    lara->extraL->camera.target.room = dst;
    lara->extraL->camera.view.pos = lara->pos;
    lara->extraL->camera.view.room = dst;
}

static tr64_level_t level;
static int packBytes;
static uint8 saveBuf[TR64_SAVE_PAYLOAD_BYTES];

static void bootOpera(void)
{
    static bool booted = false;
    if (!booted) {
        tr64_eng_boot();
        code_boot();
        if (tr64_snd_boot) tr64_snd_boot();
        tr64_io_init((uint32_t)packBytes);
        mem_level_begin();
        tr64_io_frame();
        tr64_level_load(&level, (uint32_t)packBytes, "opera");
        if (tr64_snd_level_start) tr64_snd_level_start(&level);
        tr64_io_frame();
        if (tr64_snd_pump) tr64_snd_pump();
        booted = true;
    } else {
        game_level_stop();
        tr64_mem_reset(TR64_MEM_state);
    }
    game_init();
    game_tr2_start(&level, "opera", 0u);
}

static void pickWorldItem(unsigned itemIdx)
{
    ItemObj *it = gs->items + itemIdx;
    bool wet = (tr64_u16(it->room->info->flags) & ROOM_FLAG_WATER_MASK) != 0;
    moveLara((unsigned)(it->room - gs->rooms), it->pos.x, it->pos.y - (wet ? 200 : 0), it->pos.z, 0);
    if (wet) gs->players[0]->angle.x = -4550;
    for (int i = 0; i < 90 && ITEM_STATUS(it) != ITEM_FLAG_STATUS_INVISIBLE; ++i)
        step(IN_ACTION);
    for (int i = 0; i < 40 && gs->players[0]->state != (wet ? Lara_STATE_UW_TREAD : Lara_STATE_STOP); ++i)
        step(0);
}

static void pullSwitch(unsigned itemIdx, int32 dx, int32 dz, int16 yaw)
{
    ItemObj *sw = gs->items + itemIdx;
    moveLara((unsigned)(sw->room - gs->rooms), sw->pos.x + dx, sw->pos.y, sw->pos.z + dz, yaw);
    step(IN_ACTION);
    for (int i = 0; i < 90 && (sw->state == 1 || gs->players[0]->state != Lara_STATE_STOP); ++i)
        step(0);
}

static void grantPickup(uint16 type)
{
    ItemObj dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.type = type;
    game_tr2_pickup_credit(&dummy);
}

static void test1BootScriptAndDragons(void)
{
    bootOpera();
    CHECK(gs->tr2 != NULL, "tr2 state missing");
    CHECK(gs->tr2->operaUnit, "tr2_opera unit should load on Opera House");
    CHECK(!gs->tr2->boatUnit, "tr2_boat unit must NOT load on Opera House");
    CHECK(!gs->tr2->creatureUnit, "tr2_creatures unit must NOT load on Opera House");
    CHECK(game_tr2_level_script_id() == TR2_LVL_OPERA, "opera script id != TR2_LVL_OPERA");
    CHECK(game_tr2_level_script_ambient_track(TR2_LVL_OPERA) == 31, "opera ambient track != 31");
    CHECK(game_tr2_level_script_secrets_max() == 3u, "opera secretsMax != 3");
    CHECK(game_tr2_level_script_next_id() == TR2_LVL_RIG, "opera nextLevelId != rig");
    CHECK(gs->tr2->elevatorCount == 1u, "expected 1 elevator slot in Opera House");
    CHECK(codeFramesUsed() <= 24u, "code frames > 24 after Opera boot");

    for (int i = 0; i < 180; ++i)
        step(0);
    CHECK(gs->players[0]->health == 1000, "Lara lost HP while standing at start");

    /* Case A: 3 Dragons with Uzis unowned -> grants Uzis + 400 rounds. */
    CHECK(game_tr2_ring_quantity(TR2_RING_UZIS) == 0u, "Uzis should start unowned");
    pickWorldItem(44);   /* Gold Dragon */
    pickWorldItem(214);  /* Jade Dragon */
    pickWorldItem(215);  /* Silver Dragon */
    CHECK(gs->gSaveGame.secrets == 7u, "all 3 dragon bits should be set");
    CHECK(game_tr2_ring_quantity(TR2_RING_UZIS) == 1u, "3-Dragon reward must grant Uzis");
    CHECK(gs->tr2->ammo[2] == 400u, "3-Dragon unowned Uzi reward must grant 400 rounds");

    /* Case B: 3 Dragons with Uzis already owned -> +320 rounds. */
    bootOpera();
    grantPickup(138u); /* Uzis pickup */
    gs->tr2->ammo[2] = 100u;
    pickWorldItem(44);
    pickWorldItem(214);
    pickWorldItem(215);
    CHECK(gs->tr2->ammo[2] == 420u, "3-Dragon owned Uzi reward must add +320 rounds");
    printf("PASS 1: Opera boot 180t, script (track 31/rig/3 dragons), unit residency (frames=%u/24), 3-Dragon Uzi reward (400 unowned / +320 owned)\n",
        (unsigned)codeFramesUsed());
}

static void test2PuzzlesAndOrMask(void)
{
    bootOpera();
    Lara *lara = gs->players[0];
    ItemObj *relaySock = gs->items + 195;   /* type 182, room 183, (78336, 2304, 43520), yaw 0 */
    ItemObj *boardSock = gs->items + 6;     /* type 183, room 0, (83456, 512, 54784), yaw 0 */
    ItemObj *elev = gs->items + 63;         /* type 65 */
    ItemObj *gunman = gs->items + 189;      /* type 18 */

    CHECK(relaySock->type == 182u, "item 195 should be puzzle hole 1 (182)");
    CHECK(boardSock->type == 183u, "item 6 should be puzzle hole 2 (183)");

    /* Neg 1: Out of reach (Z = 600 -> dz = 600 > 512) -> no sound 2, no ring. */
    moveLara(183, relaySock->pos.x, relaySock->pos.y, relaySock->pos.z + 600, 0);
    step(IN_ACTION);
    CHECK(!tickHasSound(2), "out-of-reach socket must not emit refusal sound 2");
    CHECK(gs->inventory.state == INV_STATE_NONE, "out-of-reach socket must not open ring");

    /* Neg 2: In reach with empty key ring -> refusal sound 2 + debounce. */
    moveLara(183, relaySock->pos.x, relaySock->pos.y, relaySock->pos.z + 380, 0);
    step(IN_ACTION);
    CHECK(tickHasSound(2), "empty-ring puzzle socket must play refusal sound 2");
    CHECK(gs->tr2->refusalValid, "refusal debounce must latch");
    step(IN_ACTION);
    CHECK(!tickHasSound(2), "refusal debounce must suppress repeat sound 2 at same pos");
    lara->pos.z += 10;
    step(IN_ACTION);
    CHECK(tickHasSound(2), "moving away must clear refusal debounce");

    /* Pick up Relay Box (100 -> 178) and Circuit Board (82 -> 179). */
    pickWorldItem(100);
    pickWorldItem(82);
    CHECK(game_tr2_ring_quantity(178u) == 1u, "Relay Box should be in ring as 178");
    CHECK(game_tr2_ring_quantity(179u) == 1u, "Circuit Board should be in ring as 179");
    /* Reverse priority: 179 (prio 107) must precede 178 (prio 108) on carried ring. */
    CHECK(game_tr2_ring_priority(179u) == 107u && game_tr2_ring_priority(178u) == 108u,
        "Puzzle ring priorities must be reverse (179=107, 178=108)");
    {
        unsigned idx178 = 99, idx179 = 99;
        for (unsigned i = 0; i < gs->tr2->carriedCount; ++i) {
            if (gs->tr2->carried[i].type == 178u) idx178 = i;
            if (gs->tr2->carried[i].type == 179u) idx179 = i;
        }
        CHECK(idx179 < idx178, "Puzzle 2 (179) must precede Puzzle 1 (178) on key ring");
    }

    /* Neg 3: Wrong puzzle item (use 179 on socket 182) -> refusal sound 2 even at latched refusalPos, not consumed. */
    moveLara(183, relaySock->pos.x, relaySock->pos.y, relaySock->pos.z + 380, 0);
    gs->tr2->refusalPos = lara->pos;
    gs->tr2->refusalValid = true;
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 179u, 0);
    CHECK(tickHasSound(2), "wrong puzzle item must play refusal sound 2 even at latched refusalPos");
    CHECK(game_tr2_ring_quantity(179u) == 1u, "wrong puzzle item must not be consumed");
    CHECK(relaySock->type == 182u, "socket must remain uncompleted");

    /* Neg 4: Cancel key ring -> silent. */
    moveLara(183, relaySock->pos.x, relaySock->pos.y, relaySock->pos.z + 380, 0);
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY, 0);
    CHECK(!tickHasSound(2), "cancelling key ring must be silent");

    /* Positive: Insert Relay Box (178) into socket 195 (182). */
    moveLara(183, relaySock->pos.x, relaySock->pos.y, relaySock->pos.z + 380, 0);
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 178u, 0);
    CHECK(game_tr2_ring_quantity(178u) == 0u, "Relay Box must be consumed");
    CHECK(lara->state == 43, "Lara must enter state 43 (use puzzle)");
    CHECK(lara->animIndex == 134, "Lara must play clip 134");
    CHECK(lara->pos.x == relaySock->pos.x && lara->pos.z == relaySock->pos.z + 327,
        "Lara must align to (0,0,+327) along socket yaw");
    CHECK(ITEM_STATUS(relaySock) == ITEM_FLAG_STATUS_ACTIVE, "socket must enter status 2 during animation");

    /* Wait for frame 80 swap (182 -> 186) and completion. */
    bool swappedAt80 = false;
    for (int t = 0; t < 160; ++t) {
        step(0);
        if (!swappedAt80 && relaySock->type == 186u) {
            int32 relFrame = (int32)lara->frameIndex - (int32)tr64_u16(gs->level.anims[lara->animIndex].frame_start);
            CHECK(relFrame == 80, "puzzle model swap must happen at relative frame 80");
            swappedAt80 = true;
        }
        if (swappedAt80 && lara->state == Lara_STATE_STOP)
            break;
    }
    CHECK(swappedAt80, "Relay Box socket must swap 182 -> 186");
    CHECK(ITEM_STATUS(relaySock) == ITEM_FLAG_STATUS_INACTIVE, "subtype 3 trigger must promote socket to status 4");
    CHECK(ITEM_STATUS(elev) == ITEM_FLAG_STATUS_NONE, "Elevator must NOT wake before lever 64 adds bit 0x0200");
    CHECK((elev->flags & ITEM_FLAG_MASK) == 0x3c00u, "Relay Box must OR 0x3c00 onto Elevator");

    /* Neg 5: Completed socket 186 is non-interactive. */
    moveLara(183, relaySock->pos.x, relaySock->pos.y, relaySock->pos.z + 380, 0);
    step(IN_ACTION);
    CHECK(!tickHasSound(2) && gs->inventory.state == INV_STATE_NONE, "completed socket 186 must be non-interactive");

    /* Pull Elevator switch 64 (yaw +16384, mask 0x0200) -> completes 0x3e00 and wakes Elevator! */
    pullSwitch(64, 380, 0, 16384);
    CHECK(ITEM_STATUS(elev) == ITEM_FLAG_STATUS_ACTIVE,
        "Elevator must wake once Relay Box (0x3c00) + lever 64 (0x0200) combine");

    /* Positive: Circuit Board (179) in room 0 socket 6 (0x0200) + lever 2 (0x3c00) -> FlipMap 0 + sound 330 + Gunman 189. */
    CHECK(!gs->gSaveGame.flipped, "FlipMap should start unflipped");
    moveLara(0, boardSock->pos.x, boardSock->pos.y, boardSock->pos.z + 380, 0);
    stepRoute(IN_ACTION, TR2_RING_EVENT | TR2_RING_EVENT_KEY | 179u, 0);
    for (int t = 0; t < 160 && lara->state != Lara_STATE_STOP; ++t)
        step(0);
    CHECK(boardSock->type == 187u, "Circuit Board socket must swap 183 -> 187");
    CHECK(ITEM_STATUS(boardSock) == ITEM_FLAG_STATUS_INACTIVE, "Circuit Board socket must reach status 4");
    CHECK(!gs->gSaveGame.flipped, "FlipMap must NOT flip before lever 2 adds 0x3c00");

    bool saw330 = false;
    ItemObj *sw2 = gs->items + 2;
    moveLara(0, sw2->pos.x - 380, sw2->pos.y, sw2->pos.z, -16384);
    step(IN_ACTION);
    for (int t = 0; t < 90; ++t) {
        step(0);
        if (tickHasSound(330)) saw330 = true;
        if (gs->gSaveGame.flipped && ITEM_STATUS(gunman) == ITEM_FLAG_STATUS_ACTIVE) break;
    }
    CHECK(gs->gSaveGame.flipped, "Circuit Board + lever 2 must activate FlipMap 0");
    CHECK(saw330, "Circuit Board + lever 2 FlipEffect 10 must play sound 330");
    CHECK(ITEM_STATUS(gunman) == ITEM_FLAG_STATUS_ACTIVE, "Circuit Board + lever 2 must wake Gunman 189");
    printf("PASS 2: Puzzles 174..189 (reverse prio, reach/refusal/debounce/wrong-item/cancel, frame 80 swap, OR-mask Elevator + FlipMap/Gunman)\n");
}

static void test3ClearBodiesAndSave(void)
{
    bootOpera();
    ItemObj *enemy = gs->items + 20;  /* type 18, room 12, flags 0x8100, cargo 19 (151) & 21 (143) */
    ItemObj *cargo1 = gs->items + 19;
    ItemObj *cargo2 = gs->items + 21;

    CHECK(!(enemy->flags & TR2_PLACED_CLEANUP), "startup must clear bit 15 from runtime flags");
    CHECK(bitGet(gs->tr2->cleanupEligible, 20u), "startup must mark item 20 cleanupEligible");

    /* Wake enemy 20 and kill it so it retires and drops cargo. */
    game_tr2_activate_item(enemy);
    enemy->health = 1;
    step(0);
    enemy->health = -10;
    for (int t = 0; t < 120 && ITEM_STATUS(enemy) != ITEM_FLAG_STATUS_INACTIVE; ++t)
        step(0);
    CHECK(ITEM_STATUS(enemy) == ITEM_FLAG_STATUS_INACTIVE, "dead enemy 20 must retire to status 4");
    CHECK(cargo1->room != NULL && ITEM_STATUS(cargo1) == ITEM_FLAG_STATUS_NONE, "cargo 19 must be released into world");
    CHECK(cargo2->room != NULL && ITEM_STATUS(cargo2) == ITEM_FLAG_STATUS_NONE, "cargo 21 must be released into world");
    CHECK(bitGet(gs->tr2->cleanupList, 20u), "corpse 20 must enter cleanupList");

    /* Save BEFORE Clear Bodies and reload -> corpse must still be on cleanupList! */
    uint32 n = game_save(saveBuf, sizeof(saveBuf));
    CHECK(n > 0u, "save before Clear Bodies failed");
    CHECK(game_load(saveBuf, n), "load before Clear Bodies failed");
    CHECK(bitGet(gs->tr2->cleanupList, 20u),
        "reload must restore pending cleanup-eligible corpse onto cleanupList");

    /* Step onto Clear Bodies trigger (room 183, (81408, 2304, 43520)). */
    moveLara(183, 81408, 2304, 43520, 0);
    step(0);
    CHECK(cleanupCount() == 0u, "Clear Bodies op 11 must empty cleanupList");
    CHECK(enemy->room == NULL && (enemy->flags & TR2_FLAG_RETIRED) != 0u,
        "Clear Bodies must permanently retire corpse 20");
    CHECK((enemy->flags & TR2_PLACED_CLEANUP) != 0u,
        "Clear Bodies must set bit 15 on cleared corpse for persistence");
    CHECK(cargo1->room != NULL && ITEM_STATUS(cargo1) == ITEM_FLAG_STATUS_NONE,
        "Clear Bodies must NOT remove released cargo 19");
    CHECK(cargo2->room != NULL && ITEM_STATUS(cargo2) == ITEM_FLAG_STATUS_NONE,
        "Clear Bodies must NOT remove released cargo 21");

    /* Save AFTER Clear Bodies and reload -> corpse stays absent & not on cleanupList. */
    n = game_save(saveBuf, sizeof(saveBuf));
    CHECK(n > 0u, "save after Clear Bodies failed");
    CHECK(game_load(saveBuf, n), "load after Clear Bodies failed");
    CHECK(cleanupCount() == 0u, "cleared corpse must not re-enter cleanupList after reload");
    CHECK(enemy->room == NULL && (enemy->flags & TR2_PLACED_CLEANUP) != 0u,
        "cleared corpse must stay permanently retired after reload");
    printf("PASS 3: Clear Bodies op 11 (bit 15 -> cleanupEligible, corpse clear, cargo preserved, pre/post save/load)\n");
}

static void test4Drawbridge(void)
{
    bootOpera();
    Lara *lara = gs->players[0];
    ItemObj *db = gs->items + 90; /* type 63, room 87, (70144, 5120, 66048), yaw -32768 */

    CHECK(db->type == TR2_ITEM_DRAWBRIDGE && db->state == 0, "drawbridge starts raised (state 0)");

    /* State 0: floor on (68,65) = (70144, 67072) is NOT substituted (still 7168). */
    {
        Room *r86 = gs->rooms + 86;
        const Sector *sec = Room_getSector(r86, 70144, 67072);
        int32 fl = Sector_getFloor(sec, 70144, 5000, 67072);
        CHECK(fl == 7168, "raised drawbridge must not substitute floor");
    }
    /* State 0: solid push-out when Lara steps into raised drawbridge leaf at tile edge. */
    moveLara(87, db->pos.x, db->pos.y, db->pos.z + 480, 0);
    int32 zStart = lara->pos.z;
    step(0);
    CHECK(lara->pos.z != zStart, "raised drawbridge (state 0) must push Lara out");

    /* Pull lever 99 (room 91) to lower drawbridge. */
    bool sawLight = false, saw161 = false;
    ItemObj *sw99 = gs->items + 99;
    moveLara(91, sw99->pos.x - 380, sw99->pos.y, sw99->pos.z, -16384);
    step(IN_ACTION);
    for (int t = 0; t < 160; ++t) {
        step(0);
        if (tickHasSound(161)) saw161 = true;
        for (uint8 i = 0; i < gs->tr2->dynLightCount; ++i) {
            if (gs->tr2->dynLights[i].intensity == 14 && gs->tr2->dynLights[i].falloff == 11)
                sawLight = true;
        }
        if (db->state == 1 && saw161 && sawLight) break;
    }
    CHECK(db->state == 1, "drawbridge must lower to state 1");
    CHECK(sawLight, "active drawbridge must emit dynamic point light (intensity 14, falloff 11)");
    CHECK(saw161, "lowering drawbridge must play sound 161");

    /* State 1: walkable slab floor (5120) above and ceiling (5376) below on (68,65) & (68,66),
     * and NOT on pivot tile (68,64). */
    {
        Room *r86 = gs->rooms + 86;
        const Sector *s1 = Room_getSector(r86, 70144, 67072); /* (68,65) */
        const Sector *s2 = Room_getSector(r86, 70144, 68096); /* (68,66) */
        const Sector *sp = Room_getSector(gs->rooms + 87, 70144, 66048); /* (68,64) */
        CHECK(Sector_getFloor(s1, 70144, 5120, 67072) == 5120, "drawbridge (68,65) floor != 5120");
        CHECK(Sector_getFloor(s2, 70144, 5120, 68096) == 5120, "drawbridge (68,66) floor != 5120");
        CHECK(Sector_getCeiling(s1, 70144, 5500, 67072) == 5376, "drawbridge (68,65) ceiling != 5376");
        CHECK(Sector_getCeiling(s2, 70144, 5500, 68096) == 5376, "drawbridge (68,66) ceiling != 5376");
        moveLara(87, 70144, 5120, 67072, 0);
        step(0);
        CHECK(lara->pos.y == 5120, "Lara must stand on lowered drawbridge at Y=5120");
        CHECK(Sector_getCeiling(sp, 70144, 5500, 66048) != 5376, "pivot tile (68,64) must not be slab-substituted");
    }

    /* Pull lever 99 again to raise drawbridge back to state 0 (sound 162). */
    bool saw162 = false;
    moveLara(91, sw99->pos.x - 380, sw99->pos.y, sw99->pos.z, -16384);
    step(IN_ACTION);
    for (int t = 0; t < 160; ++t) {
        step(0);
        if (tickHasSound(162)) saw162 = true;
        if (db->state == 0 && saw162) break;
    }
    CHECK(db->state == 0 && saw162, "drawbridge must raise back to state 0 with sound 162");
    {
        Room *r86 = gs->rooms + 86;
        const Sector *s1 = Room_getSector(r86, 70144, 67072);
        CHECK(Sector_getFloor(s1, 70144, 5000, 67072) == 7168, "re-raised drawbridge must clear floor");
    }
    printf("PASS 4: Drawbridge type 63 (state 0 solid, dynlight 14/11, state 1 slab 5120/5376 on 2 tiles, s161/s162)\n");
}

static void test5ElevatorAndSave(void)
{
    bootOpera();
    ItemObj *elev = gs->items + 63; /* type 65, room 57, (77312, 2304, 45568), tile (75,44) */
    CHECK(elev->type == TR2_ITEM_ELEVATOR, "item 63 should be elevator");
    CHECK(gs->tr2->elevatorCount == 1u && gs->tr2->elevators[0].itemIndex == 63, "elevator pool slot missing");
    CHECK(gs->tr2->elevators[0].yStart == 2304, "elevator yStart != 2304");

    /* Verify exact sec 4 floor/ceiling table on 2x2 footprint {74,75}x{44,45} while doors open vs closed. */
    {
        Room *r48 = gs->rooms + 48;
        const Sector *inCab = Room_getSector(r48, 76288, 45568); /* (74,44) */
        const Sector *inCab2 = Room_getSector(r48, 77312, 46592); /* (75,45) */
        int32 ey = elev->pos.y; /* 2304 */

        elev->state = 1;
        CHECK(Sector_getFloor(inCab, 76288, ey - 1280, 45568) == ey - 1280, "elev open roof floor != Y-1280");
        CHECK(Sector_getFloor(inCab, 76288, ey, 45568) == ey, "elev open cab floor != Y");
        CHECK(Sector_getCeiling(inCab2, 77312, ey, 46592) == ey - 1024, "elev open cab ceiling != Y-1024");
        CHECK(Sector_getCeiling(inCab, 76288, ey + 1000, 45568) == ey + 256, "elev open underside ceiling != Y+256");

        elev->state = 0;
        moveLara(57, 77312, ey, 44544, 0); /* tile (75,43), outside shaft */
        CHECK(Sector_getFloor(inCab, 76288, ey, 45568) == WALL, "closed elev must block outside entry (floor=WALL)");
        CHECK(Sector_getCeiling(inCab, 76288, ey, 45568) == 32767, "closed elev must block outside entry (ceil=32767)");
        moveLara(57, 76288, ey, 45568, 0);
        CHECK(Sector_getFloor(inCab, 76288, ey, 45568) == ey, "closed elev inside floor != Y");
        CHECK(Sector_getCeiling(inCab, 76288, ey, 45568) == ey - 1024, "closed elev inside ceiling != Y-1024");
    }

    /* Activate Elevator cleanly (full trigger mask 0x3e00) and verify 90t wait + 351t descent. */
    moveLara(57, 77312, 2304, 44544, 0);
    elev->flags = (uint16)((elev->flags & ~ITEM_FLAG_MASK) | ITEM_FLAG_MASK);
    game_tr2_activate_item(elev);
    bool saw286 = false, saw287 = false;
    for (int t = 0; t < 90; ++t) {
        step(0);
        if (tickHasSound(286)) saw286 = true;
        CHECK(elev->pos.y == 2304, "elevator must pause during first 90 ticks");
    }
    CHECK(saw286, "elevator open door must play sound 286");
    CHECK(gs->tr2->elevators[0].waitCounter == 90, "waitCounter should be 90 after 90 ticks");

    /* Descend 100 ticks -> Y = 2304 + 1600 = 3904, then save/load mid-motion! */
    for (int t = 0; t < 100; ++t) {
        step(0);
        if (tickHasSound(287)) saw287 = true;
    }
    CHECK(saw287, "elevator close door must play sound 287");
    CHECK(elev->pos.y == 2304 + 16 * 100, "elevator must descend at +16/tick");

    uint32 n = game_save(saveBuf, sizeof(saveBuf));
    CHECK(n > 0u, "elevator mid-trip save failed");
    for (int t = 0; t < 20; ++t) step(0);
    CHECK(elev->pos.y == 2304 + 16 * 120, "elevator should advance before reload");
    CHECK(game_load(saveBuf, n), "elevator mid-trip load failed");
    CHECK(elev->pos.y == 2304 + 16 * 100 && gs->tr2->elevators[0].waitCounter == 90 &&
          gs->tr2->elevators[0].yStart == 2304, "elevator save/load must restore exact Y/yStart/waitCounter");

    /* Finish remaining 251 ticks of descent -> Y = 2304 + 5616 = 7920 (bottom). */
    for (int t = 0; t < 251; ++t) step(0);
    CHECK(elev->pos.y == 2304 + 5616, "elevator bottom must equal Y0 + 5616 (7920)");
    step(0);
    CHECK(elev->pos.y == 7920 && elev->goalState == 1 && gs->tr2->elevators[0].waitCounter == 0,
        "reaching bottom must open doors and reset waitCounter to 0");
    CHECK(elev->room != gs->rooms + 57, "elevator must transfer room along shaft");

    /* Untrigger elevator -> pause 90 ticks at bottom, then ascend 350 ticks back to Y0 + 16 = 2320. */
    elev->flags &= ~ITEM_FLAG_MASK;
    for (int t = 0; t < 90; ++t) {
        step(0);
        CHECK(elev->pos.y == 7920, "elevator must pause 90 ticks at bottom");
    }
    for (int t = 0; t < 350; ++t) step(0);
    CHECK(elev->pos.y == 2304 + 16, "elevator top return must equal Y0 + 16 (2320)");
    step(0);
    CHECK(elev->pos.y == 2320 && elev->goalState == 1 && gs->tr2->elevators[0].waitCounter == 0,
        "reaching top must open doors and reset waitCounter to 0");
    printf("PASS 5: Elevator type 65 (2x2 footprint/wall table, 90t wait, 351t descent/350t ascent, mid-trip save/load)\n");
}

static void test6PendulumsFallingAndFans(void)
{
    /* --- 6A: Swinging Sandbag (58, item 94) & Crate (96, item 36) --- */
    bootOpera();
    Lara *lara = gs->players[0];
    ItemObj *bag = gs->items + 94;   /* type 58, room 89, (70144, 1920, 68096) */
    ItemObj *crate = gs->items + 36; /* type 96, room 35, (79360, 2944, 32256) */

    const AABBs *bb = ItemObj_getBoundingBox(bag, false);
    int32 bagMidY = bag->pos.y + ((bb->minY + bb->maxY) >> 1) + 380;
    moveLara(89, bag->pos.x + ((bb->minX + bb->maxX) >> 1), bagMidY,
             bag->pos.z + ((bb->minZ + bb->maxZ) >> 1) + 10, 0);
    int32 bx = lara->pos.x, bz = lara->pos.z;
    step(0);
    CHECK(lara->pos.x != bx || lara->pos.z != bz, "inactive swinging sandbag 58 must push Lara out");

    bag->flags = (uint16)((bag->flags & ~ITEM_FLAG_MASK) | ITEM_FLAG_MASK);
    game_tr2_activate_item(bag);
    step(0);
    bag->flags &= ~ITEM_FLAG_MASK;
    for (int t = 0; t < 15; ++t) step(0);
    CHECK(ITEM_STATUS(bag) == ITEM_FLAG_STATUS_ACTIVE, "swinging pendulum 58 must keep looping without stopping");

    bool hitBag = false;
    for (int t = 0; t < 60 && !hitBag; ++t) {
        moveLara(140, 79360, -640, 26112, 0);
        bag->hitMask = 1u;
        int32 hp0 = lara->health;
        unsigned b0 = countEffects(TR2_FX_BLOOD);
        uint32 rng0 = gs->gRandSeedLogic;
        step(0);
        if (lara->health < hp0) {
            CHECK(hp0 - lara->health == 50, "swinging sandbag 58 must deal 50 HP");
            CHECK(countEffects(TR2_FX_BLOOD) == b0 + 1, "swinging sandbag 58 must spawn 1 blood splat");
            uint32 expectRng = rng0;
            for (int r = 0; r < 4; ++r) expectRng = expectRng * 1103515245u + 12345u;
            CHECK(gs->gRandSeedLogic == expectRng, "swinging sandbag 58 must consume 4 logic RNG draws");
            hitBag = true;
        }
    }
    CHECK(hitBag, "swinging sandbag 58 never hit Lara");

    bool hitCrate = false;
    for (int t = 0; t < 60 && !hitCrate; ++t) {
        const AABBs *cb = ItemObj_getBoundingBox(crate, false);
        int32 cy = crate->pos.y + ((cb->minY + cb->maxY) >> 1) + 380;
        Room *cr = Room_getRoom(crate->room, crate->pos.x, cy, crate->pos.z);
        moveLara((unsigned)(cr - gs->rooms), crate->pos.x, cy, crate->pos.z, 0);
        int32 hp0 = lara->health;
        unsigned b0 = countEffects(TR2_FX_BLOOD);
        step(0);
        if (lara->health < hp0) {
            CHECK(hp0 - lara->health == 50, "swinging crate 96 must deal 50 HP");
            CHECK(countEffects(TR2_FX_BLOOD) == b0 + 1, "swinging crate 96 must spawn 1 blood splat");
            hitCrate = true;
        }
    }
    CHECK(hitCrate, "swinging crate 96 never hit Lara");

    /* --- 6B: Falling Sandbag (79, item 65, room 58, (72192, 3200, 54784)) --- */
    bootOpera();
    lara = gs->players[0];
    ItemObj *fall = gs->items + 65;
    CHECK(fall->type == TR2_ITEM_FALLING_CEILING && fall->state == 0, "falling sandbag starts state 0");

    fall->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(fall);
    bool sawFallHit = false, sawLand = false;
    for (int t = 0; t < 90; ++t) {
        if (!sawFallHit && fall->state == 1) {
            moveLara((unsigned)(fall->room - gs->rooms), fall->pos.x, fall->pos.y + 50, fall->pos.z, 0);
            int32 hp0 = lara->health;
            unsigned b0 = countEffects(TR2_FX_BLOOD);
            step(0);
            if (lara->health < hp0) {
                CHECK(hp0 - lara->health == 300, "falling sandbag 79 must deal 300 HP/tick");
                CHECK(countEffects(TR2_FX_BLOOD) == b0, "falling sandbag 79 must NOT spawn blood");
                sawFallHit = true;
            }
        } else {
            moveLara(140, 79360, -640, 26112, 0);
            step(0);
        }
        if (ITEM_STATUS(fall) == ITEM_FLAG_STATUS_INACTIVE) {
            sawLand = true;
            break;
        }
    }
    CHECK(sawFallHit, "falling sandbag 79 never dealt 300 HP");
    CHECK(sawLand && fall->state == 2 && !(fall->flags & ITEM_FLAG_GRAVITY),
        "falling sandbag 79 must land in state 2, clear gravity, and deactivate to status 4");
    const AABBs *fb = ItemObj_getBoundingBox(fall, false);
    int32 fx0 = fall->pos.x + ((fb->minX + fb->maxX) >> 1);
    int32 fz0 = fall->pos.z + ((fb->minZ + fb->maxZ) >> 1) + 10;
    moveLara((unsigned)(fall->room - gs->rooms), fx0, fall->pos.y, fz0, 0);
    step(0);
    if (lara->pos.x == fx0 && lara->pos.z == fz0) {
        fprintf(stderr, "DEBUG fall bbox=(%d..%d, %d..%d, %d..%d) pos=(%d,%d,%d) flags=%#x\n",
            fb->minX, fb->maxX, fb->minY, fb->maxY, fb->minZ, fb->maxZ,
            fall->pos.x, fall->pos.y, fall->pos.z, fall->flags);
    }
    CHECK(lara->pos.x != fx0 || lara->pos.z != fz0, "landed falling sandbag 79 (status 4) must be solid");

    /* --- 6C: Spinning Fans (76 item 154 & 95 item 213) --- */
    bootOpera();
    lara = gs->players[0];
    ItemObj *fan76 = gs->items + 154; /* room 129, (49664, 6656, 51712) */
    ItemObj *fan95 = gs->items + 213; /* room 187, (60928, 6400, 36352) */

    fan76->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(fan76);
    fan95->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(fan95);

    moveLara(187, 61952, 6400, 36352, 0);
    int32 wx0 = lara->pos.x, wz0 = lara->pos.z;
    bool saw213 = false, saw217 = false;
    for (int t = 0; t < 20; ++t) {
        step(0);
        if (tickHasSound(217)) saw217 = true;
    }
    CHECK(lara->pos.x == wx0 && lara->pos.z == wz0, "fan 95 must NOT apply wind push force");
    moveLara(129, fan76->pos.x, fan76->pos.y, fan76->pos.z, 0);
    for (int t = 0; t < 5 && !saw213; ++t) {
        step(0);
        if (tickHasSound(213)) saw213 = true;
    }
    CHECK(saw213 && saw217, "fans 76 & 95 must play sounds 213 and 217");

    bool hitFan = false;
    for (int t = 0; t < 20 && !hitFan; ++t) {
        moveLara(129, fan76->pos.x, fan76->pos.y, fan76->pos.z, 0);
        int32 hp0 = lara->health;
        unsigned b0 = countEffects(TR2_FX_BLOOD);
        step(0);
        if (lara->health < hp0) {
            CHECK(hp0 - lara->health == 200, "fan blade must deal 200 HP/tick");
            CHECK(countEffects(TR2_FX_BLOOD) == b0 + 3, "fan blade must spawn 3 blood splats");
            hitFan = true;
        }
    }
    CHECK(hitFan, "spinning fan 76 blade never hit Lara");

    fan76->flags &= ~ITEM_FLAG_MASK;
    moveLara(140, 79360, -640, 26112, 0);
    for (int t = 0; t < 120 && ITEM_STATUS(fan76) != ITEM_FLAG_STATUS_INACTIVE; ++t)
        step(0);
    CHECK(ITEM_STATUS(fan76) == ITEM_FLAG_STATUS_INACTIVE, "untriggered fan 76 must stop to status 4");
    CHECK(!(fan76->flags & ITEM_FLAG_COLLISION), "stopped fan 76 must clear ITEM_FLAG_COLLISION (passable)");
    printf("PASS 6: Swinging 58/96 (50HP/1blood/4RNG), Falling 79 (300HP/0blood/solid land), Fans 76/95 (no wind, s213/217, 200HP/3blood, passable stopped)\n");
}

static void test7MultiBoulder(void)
{
    bootOpera();
    Lara *lara = gs->players[0];
    ItemObj *crate1 = gs->items + 106; /* type 83, room 101, (84480, 5248, 52736), yaw -16384 */
    vec3i initPos = crate1->pos;
    Room *initRoom = crate1->room;

    CHECK(crate1->type == TR2_ITEM_ROLLING_CRATE, "item 106 should be multi-boulder type 83");

    moveLara(55, 77312, 7168, 52736 + 1024, 0);
    crate1->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(crate1);
    bool saw222 = false, saw223 = false, sawBounce = false;
    for (int t = 0; t < 300 && ITEM_STATUS(crate1) != ITEM_FLAG_STATUS_INACTIVE; ++t) {
        step(0);
        if (tickHasSound(222)) saw222 = true;
        if (tickHasSound(223)) saw223 = true;
        if (gs->players[0]->extraL->camera.bounce < 0) sawBounce = true;
    }
    CHECK(saw222 && sawBounce, "rolling crate 83 must emit sound 222 & camera bounce");
    CHECK(saw223 && ITEM_STATUS(crate1) == ITEM_FLAG_STATUS_INACTIVE,
        "rolling crate 83 must stop on wall probe with sound 223 -> status 4");
    CHECK(crate1->pos.x != initPos.x, "rolling crate 83 must have moved from initial X");

    crate1->flags &= ~ITEM_FLAG_MASK;
    step(0);
    CHECK(crate1->pos.x == initPos.x && crate1->pos.y == initPos.y && crate1->pos.z == initPos.z &&
          crate1->room == initRoom && crate1->state == 1 && ITEM_STATUS(crate1) == ITEM_FLAG_STATUS_NONE,
          "untriggered parked rolling crate 83 must reset to initial (pos, room)");

    ItemObj *crate2 = gs->items + 165; /* room 161, (75264, 5504, 62976), yaw 16384 */
    crate2->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(crate2);
    step(0);
    step(0);
    moveLara((unsigned)(crate2->room - gs->rooms), crate2->pos.x + 120, crate2->pos.y, crate2->pos.z, 0);
    lara->flags |= ITEM_FLAG_GRAVITY;
    lara->vSpeed = -30;
    int32 hp0 = lara->health;
    unsigned b0 = countEffects(TR2_FX_BLOOD);
    step(0);
    CHECK(hp0 - lara->health == 100, "airborne rolling crate hit must deal 100 HP");
    CHECK(countEffects(TR2_FX_BLOOD) == b0 + 1, "airborne rolling crate hit must spawn 1 blood splat");

    moveLara((unsigned)(crate2->room - gs->rooms), crate2->pos.x + 100, crate2->pos.y, crate2->pos.z, 0);
    b0 = countEffects(TR2_FX_BLOOD);
    step(0);
    CHECK(lara->health == -1 && lara->animIndex == 139 && lara->state == 46,
        "grounded rolling crate hit must instant-kill Lara (HP -1, clip 139, state 46)");
    CHECK(countEffects(TR2_FX_BLOOD) == b0 + 15, "grounded rolling crate kill must spawn 15 blood splats");
    printf("PASS 7: Multi-Boulder type 83 (roll/s222/bounce/wall-stop s223/reset, airborne 100HP+1blood, grounded clip 139+15blood)\n");
}

static void test8OperaBoss(void)
{
    bootOpera();
    Lara *lara = gs->players[0];
    ItemObj *boss = gs->items + 142; /* type 20, room 122, (65024, 5888, 43520), yaw 16384 */
    ItemObj *c143 = gs->items + 143; /* 148 */
    ItemObj *c144 = gs->items + 144; /* 141 Grenade Launcher */
    ItemObj *c145 = gs->items + 145; /* 148 */

    CHECK(boss->type == TR2_ITEM_OPERA_BOSS && boss->health == 150, "Opera Boss must start with 150 HP");
    CHECK(boss->state == 2, "Opera Boss must start in state 2 (base clip +3)");
    {
        int16 dy = (int16)(boss->angle.y - 16384);
        CHECK(dy >= -8192 && dy <= 8192, "Opera Boss startup yaw jitter must stay within +-45 deg");
    }

    moveLara(122, boss->pos.x + 1200, boss->pos.y, boss->pos.z, -16384);
    boss->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(boss);
    bool sawDualFlash = false, sawShotDmg = false;
    for (int t = 0; t < 240; ++t) {
        int32 hp0 = lara->health;
        step(0);
        if (boss->state == 10 && countEffects(TR2_FX_GUN_FLASH) >= 2) sawDualFlash = true;
        if (lara->health < hp0) {
            int32 dmg = hp0 - lara->health;
            CHECK(dmg == 50 || dmg == 100, "Opera Boss shot damage must be 50 or 100 HP");
            sawShotDmg = true;
            lara->health = 1000;
        }
        if (sawDualFlash && sawShotDmg) break;
    }
    CHECK(sawDualFlash, "Opera Boss state 10 must spawn 2 simultaneous muzzle flashes (type 240)");
    CHECK(sawShotDmg, "Opera Boss hitscan must damage Lara (50/100 HP)");

    boss->health = 0;
    for (int t = 0; t < 120 && ITEM_STATUS(boss) != ITEM_FLAG_STATUS_INACTIVE; ++t)
        step(0);
    CHECK(boss->state == 11 && ITEM_STATUS(boss) == ITEM_FLAG_STATUS_INACTIVE,
        "dead Opera Boss must reach state 11 & status 4");
    CHECK(c143->room != NULL && c144->room != NULL && c145->room != NULL,
        "dead Opera Boss must release all 3 cargo items");
    CHECK(c144->type == 141u, "unowned Grenade Launcher cargo must stay type 141");

    /* Campaign carry check: when Grenade Launcher is ALREADY owned at level start, item 144 becomes type 148! */
    {
        grantPickup(141u);
        tr64_game_inventory_carry_t carry;
        CHECK(game_tr2_capture_carry(&carry) != 0, "capture carry with Grenade Launcher failed");
        game_level_stop();
        tr64_mem_reset(TR64_MEM_state);
        game_init();
        game_tr2_start(&level, "opera", 0u);
        game_tr2_apply_carry(&carry);
        CHECK(gs->items[144].type == 148u,
            "when Grenade Launcher is already owned, world pickup 141 must convert to 148");
    }
    printf("PASS 8: Opera Boss type 20 (150 HP, +-45 jitter, state 10 dual flash/50-100 HP, death state 11 + cargo drop & 141->148 carry)\n");
}

static void test9GrenadeLauncher(void)
{
    bootOpera();
    Lara *lara = gs->players[0];

    /* Grant 2 Grenade Ammo bundles (170) BEFORE picking up Grenade Launcher (141) -> 2*2 + 2 = 6! */
    grantPickup(148u);
    grantPickup(148u);
    CHECK(game_tr2_ring_quantity(170u) == 2u, "unowned grenade bundles must sit on ring as 170");
    grantPickup(141u);
    CHECK(game_tr2_ring_quantity(163u) == 1u, "Grenade Launcher must enter ring as 163");
    CHECK(game_tr2_ring_quantity(170u) == 0u, "170 bundles must convert on Grenade pickup");
    CHECK(gs->tr2->ammo[5] == 6u, "2 bundles*2 + 2 base must yield 6 grenades");
    CHECK(gs->items[144].type == 148u, "pickup 141 must convert remaining world 141 -> 148");

    /* Back-weapon priority check: Shotgun (3) > M16 (6) > Grenade (7) > Harpoon (8). */
    {
        tr64_game_inventory_carry_t carry;
        bootOpera();
        TR2RingEntry *sg = game_tr2_ring_find(TR2_RING_SHOTGUN);
        if (sg) game_tr2_ring_debit(sg);
        grantPickup(141u); /* Grenade */
        CHECK(game_tr2_capture_carry(&carry) != 0, "capture grenade carry");
        bootOpera();
        game_tr2_apply_carry(&carry);
        CHECK(gs->tr2->backWeapon == 7u, "back weapon must select Grenade (7) when only long gun");

        bootOpera(); /* starts with Shotgun (158) owned */
        grantPickup(141u); /* + Grenade (163) */
        CHECK(game_tr2_capture_carry(&carry) != 0, "capture shotgun+grenade carry");
        bootOpera();
        game_tr2_apply_carry(&carry);
        CHECK(gs->tr2->backWeapon == TR2_MODEL_SHOTGUN, "back weapon must prefer Shotgun (3) over Grenade (7)");
    }

    /* Fresh boot for draw / fire / projectile 248 / window 72 / creature gib / explosion 229 / empty dry-click. */
    bootOpera();
    lara = gs->players[0];
    grantPickup(141u);
    gs->tr2->ammo[5] = 3u;
    CHECK(game_tr2_request_ring_weapon(TR2_WEAPON_GRENADE), "request Grenade Launcher");

    ItemObj *win = gs->items + 17; /* room 10, (87552, 5888, 34304), open behind to x=89600 */
    moveLara(10, 85504, 6144, 34304, 16384);

    bool sawDrawSound = false;
    for (int t = 0; t < 40 && lara->extraL->weaponState != WEAPON_STATE_READY; ++t) {
        step(0);
        if (tickHasSound(7)) sawDrawSound = true;
    }
    CHECK(lara->extraL->weaponState == WEAPON_STATE_READY, "Grenade Launcher must finish drawing");
    CHECK(lara->extraL->weapon == TR2_WEAPON_GRENADE, "active weapon must be Grenade Launcher");
    CHECK(sawDrawSound, "drawing Grenade Launcher must emit sound 7");

    int32 firedTick = -1;
    ItemObj *proj = NULL;
    bool shatteredWin = false, sawArc = false, saw105 = false;
    int32 projAge = 0;
    for (int t = 0; t < 90; ++t) {
        step(t < 10 ? IN_ACTION : 0);
        if (firedTick < 0 && gs->tr2->ammo[5] == 2u) {
            firedTick = t;
            CHECK(gs->tr2->dynLightCount == 0u, "Grenade Launcher fire must NOT emit muzzle flash dynamic light");
            for (int i = gs->level.itemsCount; i < MAX_ITEMS; ++i) {
                ItemObj *it = gs->items + i;
                if (it->type == 248u && ITEM_STATUS(it) == ITEM_FLAG_STATUS_ACTIVE) {
                    proj = it;
                    break;
                }
            }
            CHECK(proj != NULL, "firing Grenade Launcher must spawn projectile 248");
        }
        if (proj && ITEM_STATUS(proj) == ITEM_FLAG_STATUS_ACTIVE) {
            ++projAge; /* age 1 = spawn tick (hSpeed=200), ages 2..12 = 11 flat update ticks, age 13 = 12th update (arc) */
            if (projAge <= 12)
                CHECK(proj->vSpeed == 0, "grenade projectile 248 must fly flat for first 11 update ticks");
            else if (projAge == 13) {
                CHECK(proj->vSpeed == 1, "grenade projectile 248 must start arcing (vSpeed=1) on 12th update tick");
                sawArc = true;
            }
            if (!shatteredWin && (win->flags & ITEM_FLAG_ONCE) != 0u) {
                shatteredWin = true;
                CHECK(ITEM_STATUS(proj) == ITEM_FLAG_STATUS_ACTIVE,
                    "shattering window 72 must NOT detonate grenade 248");
            }
        }
        if (tickHasSound(105)) saw105 = true;
        if (saw105 && countEffects(TR2_FX_EXPLOSION) > 0u) break;
    }
    CHECK(firedTick >= 0, "grenade never fired");
    CHECK(shatteredWin, "grenade 248 must shatter breakable window 72");
    CHECK(sawArc, "grenade 248 must fly flat 11 ticks and arc on tick 12");
    CHECK(saw105 && countEffects(TR2_FX_EXPLOSION) == 1u,
        "grenade detonation must spawn explosion 229 + sound 105");

    bool sawOddLight = false, sawEvenLight = false;
    int expTicks = 1;
    for (int t = 0; t < 40 && countEffects(TR2_FX_EXPLOSION) > 0u; ++t) {
        step(0);
        ++expTicks;
        for (uint8 i = 0; i < gs->tr2->dynLightCount; ++i) {
            if (gs->tr2->dynLights[i].intensity == 12 && gs->tr2->dynLights[i].falloff == 10) sawOddLight = true;
            if (gs->tr2->dynLights[i].intensity == 13 && gs->tr2->dynLights[i].falloff == 11) sawEvenLight = true;
        }
    }
    CHECK(expTicks == 28, "explosion 229 must live exactly 28 ticks");
    CHECK(sawOddLight && sawEvenLight, "explosion 229 must alternate dynamic light 12/10 and 13/11");
    CHECK(gs->tr2->dynLightCount == 0u, "explosion 229 must emit no dynamic light on retirement tick");

    /* Direct hit on active creature (enemy 189, 25 HP -> 30 HP explosive death + cargo 190 drop)
     * AND point-blank 0 self-damage to Lara! */
    ItemObj *target = gs->items + 189; /* room 174, (72192, 6656, 48640), cargo 190 */
    ItemObj *cargo190 = gs->items + 190;
    target->flags |= ITEM_FLAG_MASK;
    game_tr2_activate_item(target);
    target->health = 25;
    uint8 savedWState = lara->extraL->weaponState;
    moveLara(174, target->pos.x - 300, target->pos.y, target->pos.z, 16384);
    lara->extraL->weaponState = savedWState;
    uint32 hits0 = gs->tr2->ammoHits;
    uint32 kills0 = gs->gSaveGame.kills;
    for (int t = 0; t < 80 && gs->tr2->ammo[5] == 2u; ++t)
        step(IN_ACTION);
    CHECK(gs->tr2->ammo[5] == 1u, "second grenade must fire after reload");
    for (int t = 0; t < 20 && target->room != NULL; ++t)
        step(0);
    CHECK(target->room == NULL && (target->flags & TR2_FLAG_RETIRED) != 0u,
        "lethal grenade hit must gib/retire creature immediately");
    CHECK(cargo190->room != NULL && ITEM_STATUS(cargo190) == ITEM_FLAG_STATUS_NONE,
        "explosive death must still release creature cargo");
    CHECK(gs->tr2->ammoHits > hits0 && gs->gSaveGame.kills > kills0,
        "grenade kill must increment hits & kills");
    CHECK(lara->health == 1000, "point-blank grenade explosion must deal ZERO self-damage to Lara");

    /* Fire last grenade (1 -> 0) and then attempt empty fire -> dry-click sound 48 -> Pistols! */
    for (int t = 0; t < 80 && gs->tr2->ammo[5] == 1u; ++t)
        step(IN_ACTION);
    CHECK(gs->tr2->ammo[5] == 0u, "third grenade must empty reserve");
    bool saw48 = false;
    for (int t = 0; t < 120; ++t) {
        step(IN_ACTION);
        if (tickHasSound(48)) saw48 = true;
        if (lara->extraL->weapon == WEAPON_PISTOLS) break;
    }
    CHECK(saw48 && lara->extraL->weapon == WEAPON_PISTOLS,
        "empty Grenade Launcher must emit dry-click 48 and switch to Pistols");
    printf("PASS 9: Grenade Launcher (141/7/163/248/229: bundles, back-prio, draw/reload/s48, 11t flat+arc, glass passthrough, gib+cargo, 0 self-dmg, 28t lights)\n");
}

static void test10FullSaveAndCodeBudget(void)
{
    bootOpera();
    CHECK(game_tr2_ring_open(false), "open main ring on Opera House");
    uint32 framesWithInv = codeFramesUsed();
    CHECK(framesWithInv <= 24u, "code frames with inv > 24");

    uint32 n = game_save(saveBuf, sizeof(saveBuf));
    CHECK(n > 0u && n <= TR64_SAVE_PAYLOAD_BYTES, "Opera House game_save failed");
    CHECK(codeFramesPeak() <= 24u, "peak code frames during inv+save > 24");

    gs->inventory.state = INV_STATE_NONE;
    code_loading_begin();
    if (code_is_loaded("inv")) code_unload("inv");
    code_loading_end();
    CHECK(game_load(saveBuf, n), "Opera House game_load failed");
    for (int t = 0; t < 30; ++t) step(0);
    printf("PASS 10: Opera House save/load round-trip (%u/%u B) + peak code frames %u/24 (inv=%u/24)\n",
        (unsigned)n, (unsigned)TR64_SAVE_PAYLOAD_BYTES,
        (unsigned)codeFramesPeak(), (unsigned)framesWithInv);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) {
        fprintf(stderr, "usage: %s <opera.pack>\n", argv[0]);
        return 2;
    }
    packBytes = sys_cart_open(argv[1]);
    CHECK(packBytes > 0, "cannot open opera.pack");

    test1BootScriptAndDragons();
    test2PuzzlesAndOrMask();
    test3ClearBodiesAndSave();
    test4Drawbridge();
    test5ElevatorAndSave();
    test6PendulumsFallingAndFans();
    test7MultiBoulder();
    test8OperaBoss();
    test9GrenadeLauncher();
    test10FullSaveAndCodeBudget();
    tr64_mem_stats_t st;
    tr64_mem_stats(TR64_MEM_state, &st);
    printf("INFO opera state_pool_used=%u sizeof_GameTR2=%u\n",
        (unsigned)st.used, (unsigned)sizeof(GameTR2));
    printf("ALL 10 OPERA HOUSE TESTS PASSED\n");
    return 0;
}
'''


def expect(ok, msg):
    if not ok:
        raise RuntimeError(msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('build_dir', type=Path, help='TR2 host BUILD_DIR containing host/*.o and generated/')
    ap.add_argument('opera_pack', type=Path, help='Cooked opera.pack')
    args = ap.parse_args()
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        ap.error('set TMPDIR to the assigned tr64 scratch directory')

    repo = Path(__file__).resolve().parents[2]
    gen = args.build_dir / 'generated'
    objs = [p for p in sorted((args.build_dir / 'host').rglob('*.o'))
            if p.name != 'main.o']
    expect(objs, f'no host objects found under {args.build_dir / "host"}')

    with tempfile.TemporaryDirectory(prefix='tr2-opera-', dir=scratch) as td:
        root = Path(td)
        src = root / 'opera_driver.c'
        obj = root / 'opera_driver.o'
        exe = root / 'opera-host'
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
            ['rtk', str(exe), str(args.opera_pack)],
            capture_output=True, text=True, timeout=120,
        )
        print(r.stdout, end='')
        if r.stderr:
            print(r.stderr, end='')
        expect(r.returncode == 0, f'opera-host exited {r.returncode}')


if __name__ == '__main__':
    main()
