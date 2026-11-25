#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""End-to-end verification of cross-game TR1/TR2/TR3 Lara abilities in TR1 and TR2 builds.

Verifies:
  1. Cook-time Lara animation grafting (--lara-tr2-donor / --lara-tr3-donor) on TR1
     (level1, gym) and TR2 (wall) with strict n64_validate + 4 MiB pool layout checks.
  2. All 25 TR1 fixtures (level1 12 + gym 13) remain 100% bit-exact on grafted TR1 packs
     when controls_abilities == TR1 (0).
  3. Negative control A: un-grafted TR1 pack safely ignores TR2/TR3 ability requests without
     crashing or entering missing states.
  4. TR1 grafted pack:
     - Controls menu cycles TR I -> TR II -> TR III -> TR I and renders STR_ABILITIES_TR1/2/3.
     - TR2 mode: Mid-Air 180 roll (ROLL_AIR) and Underwater 180 roll (ROLL_UW) execute and
       rotate angle.y by ~180 deg; Negative control B: TR1 mode suppresses both on the same pack.
     - TR3 mode: Sprint (state 73, stamina drains < 120), Sprint Roll (state 74),
       Crouch (state 71) -> Crawl Idle (80) -> Crawl Forward (81, displaces Lara) -> Stand (2),
       and live downgrade from TR3 -> TR1 cleanly resets Lara to Stand without getting stuck.
  5. TR2 grafted pack (WALL.TR2):
     - TR1 mode suppresses climbEnabled and mid-air 180 roll (negative control C).
     - TR2 mode enables climbEnabled and mid-air 180 roll.
     - TR3 mode enables Sprint (73), Sprint Roll (74), Crouch (71), and Crawl (80/81).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PROBE_C = r'''/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/game.h"
#include "game/internal.h"
#include "ui_string_ids.h"

extern void __real_game_tick_players(const uint16_t input_mask[TR64_GAME_PLAYERS]);
extern void __real_game_tr2_tick(uint32_t mask);

static int g_ran = 0;
static int g_settings_saved = 0;

void __wrap_eng_game_save_settings(void)
{
    ++g_settings_saved;
}

static void set_abilities(int32_t mode)
{
    tr64_game_settings_t s = *game_settings_view();
    s.controls_abilities = mode;
    assert(game_settings_apply(&s) == 1);
    assert(game_settings_view()->controls_abilities == mode);
}

static int16_t yaw_diff(int16_t a, int16_t b)
{
    int32_t d = (int32_t)(uint16_t)b - (int32_t)(uint16_t)a;
    while (d > 32767) d -= 65536;
    while (d < -32768) d += 65536;
    return (int16_t)d;
}

static void tick_tr1(uint16_t mask, uint32_t extra)
{
    uint16_t p[TR64_GAME_PLAYERS] = {mask, 0};
    game_set_extra_input_player(0, extra);
    __real_game_tick_players(p);
}

static void tick_tr2(uint32_t mask, uint32_t extra)
{
    game_set_extra_input(extra);
    __real_game_tr2_tick(mask);
}

static void test_tr1_menu_abilities(void)
{
    /* Open options ring -> navigate to SLOT_CONTROLS -> select -> verify OPT_ID_ABILITIES cycles 0->1->2->0 */
    Lara *lara = gs->players[0];
    assert(lara && lara->extraL);
    /* Open ring with IN_SELECT (2048) */
    tick_tr1(TR64_GAME_SELECT, 0);
    for (int i = 0; i < 30; ++i) tick_tr1(0, 0);
    /* Step down from Main ring to Options ring */
    tick_tr1(TR64_GAME_BACK, 0);
    for (int i = 0; i < 30; ++i) tick_tr1(0, 0);
    /* Spin to SLOT_CONTROLS (type == ITEM_INV_CONTROLS = 97) */
    tr64_game_menu_view_t mv;
    int found = 0;
    for (int tries = 0; tries < 8; ++tries) {
        assert(game_menu_view(&mv));
        if (mv.itemsCount > 0 && mv.itemIndex >= 0 && mv.itemIndex < mv.itemsCount &&
            mv.items[mv.itemIndex].type == ITEM_INV_CONTROLS) {
            found = 1;
            break;
        }
        tick_tr1(TR64_GAME_RIGHT, 0);
        for (int i = 0; i < 20; ++i) tick_tr1(0, 0);
    }
    assert(found);
    /* Open Controls options */
    tick_tr1(TR64_GAME_ACTION, 0);
    for (int i = 0; i < 25; ++i) tick_tr1(0, 0);
    assert(game_menu_view(&mv));
    assert(mv.optionsCount >= 3);
    assert(mv.options[2].str == TR64_STR_OPT_CONTROLS_ABILITIES);
    assert(mv.options[2].type == TR64_GAME_MENU_OPTION_VALUE);
    assert(mv.options[2].value == TR64_LARA_ABILITIES_TR1);
    /* Verify menu text emits TR64_STR_ABILITIES_TR1 */
    uint16_t widths[256];
    for (int i = 0; i < 256; ++i) widths[i] = 40;
    tr64_game_menu_text_t texts[64];
    uint32_t tc = game_menu_text_view(&mv, widths, 256, texts, 64);
    int saw_tr1 = 0;
    for (uint32_t i = 0; i < tc; ++i)
        if (texts[i].kind == TR64_GAME_TEXT_STRING && texts[i].value == TR64_STR_ABILITIES_TR1)
            saw_tr1 = 1;
    assert(saw_tr1);
    /* Move cursor down twice to OPT_ID_ABILITIES (idx 2) */
    tick_tr1(TR64_GAME_BACK, 0); tick_tr1(0, 0);
    tick_tr1(TR64_GAME_BACK, 0); tick_tr1(0, 0);
    /* Press Right: TR1 -> TR2 */
    tick_tr1(TR64_GAME_RIGHT, 0); tick_tr1(0, 0);
    assert(game_settings_view()->controls_abilities == TR64_LARA_ABILITIES_TR2);
    assert(game_menu_view(&mv));
    tc = game_menu_text_view(&mv, widths, 256, texts, 64);
    int saw_tr2 = 0;
    for (uint32_t i = 0; i < tc; ++i)
        if (texts[i].kind == TR64_GAME_TEXT_STRING && texts[i].value == TR64_STR_ABILITIES_TR2)
            saw_tr2 = 1;
    assert(saw_tr2);
    /* Press Right: TR2 -> TR3 */
    tick_tr1(TR64_GAME_RIGHT, 0); tick_tr1(0, 0);
    assert(game_settings_view()->controls_abilities == TR64_LARA_ABILITIES_TR3);
    assert(game_menu_view(&mv));
    tc = game_menu_text_view(&mv, widths, 256, texts, 64);
    int saw_tr3 = 0;
    for (uint32_t i = 0; i < tc; ++i)
        if (texts[i].kind == TR64_GAME_TEXT_STRING && texts[i].value == TR64_STR_ABILITIES_TR3)
            saw_tr3 = 1;
    assert(saw_tr3);
    /* Press Right: wrap TR3 -> TR1 */
    tick_tr1(TR64_GAME_RIGHT, 0); tick_tr1(0, 0);
    assert(game_settings_view()->controls_abilities == TR64_LARA_ABILITIES_TR1);
    assert(g_settings_saved == 3);
    /* Close Controls + close Ring */
    tick_tr1(TR64_GAME_JUMP, 0);
    for (int i = 0; i < 20; ++i) tick_tr1(0, 0);
    tick_tr1(TR64_GAME_JUMP, 0);
    for (int i = 0; i < 30; ++i) tick_tr1(0, 0);
    assert(!game_menu_view(&mv));
    printf("PASS TR1_MENU_ABILITIES\n");
}

static void run_tr1_suite(const char *mode)
{
    Lara *lara = gs->players[0];
    assert(lara && lara->extraL);

    if (!strcmp(mode, "ungrafted")) {
        assert(!gs->extraAnims.hasTR2 && !gs->extraAnims.hasTR3);
        set_abilities(TR64_LARA_ABILITIES_TR3);
        for (int i = 0; i < 25; ++i) tick_tr1(TR64_GAME_FORWARD, TR64_GAME_EXTRA_SPRINT);
        assert(lara->state != Lara_STATE_SPRINT);
        for (int i = 0; i < 15; ++i) tick_tr1(0, TR64_GAME_EXTRA_CROUCH);
        assert(lara->state != Lara_STATE_CROUCH_IDLE);
        printf("PASS NEG_CONTROL_UNGRAFTED\n");
        return;
    }

    assert(gs->extraAnims.hasTR2 && gs->extraAnims.hasTR3);

    if (!strcmp(mode, "level1")) {
        test_tr1_menu_abilities();

        /* Save clean start position/room for repeatable sub-checks */
        vec3i start_pos = lara->pos;
        Room *start_room = lara->room;
        int16_t start_yaw = lara->angle.y;

        /* 1. Negative control B: TR1 mode suppresses mid-air roll during forward jump */
        set_abilities(TR64_LARA_ABILITIES_TR1);
        for (int i = 0; i < 16; ++i) tick_tr1(TR64_GAME_FORWARD, 0);
        for (int i = 0; i < 25 && !(lara->flags & ITEM_FLAG_GRAVITY); ++i)
            tick_tr1(TR64_GAME_FORWARD | TR64_GAME_JUMP, 0);
        assert((lara->flags & ITEM_FLAG_GRAVITY) != 0);
        int saw_flip_tr1 = 0;
        for (int i = 0; i < 25 && (lara->flags & ITEM_FLAG_GRAVITY); ++i) {
            tick_tr1(TR64_GAME_BACK, 0);
            if ((lara->flags & ITEM_FLAG_GRAVITY) && abs(yaw_diff(start_yaw, lara->angle.y)) > 16000)
                saw_flip_tr1 = 1;
        }
        assert(!saw_flip_tr1);
        printf("PASS NEG_CONTROL_TR1_NO_AIR_ROLL\n");

        /* Reset Lara to start */
        lara->pos = start_pos; lara->room = start_room;
        lara->angle = vec3s_create(0, start_yaw, 0);
        lara->hSpeed = lara->vSpeed = 0; lara->flags &= ~ITEM_FLAG_GRAVITY;
        ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
        for (int i = 0; i < 4; ++i) tick_tr1(0, 0);

        /* 2. TR2 mode: Mid-Air 180 roll executes while airborne and flips yaw ~180 deg */
        set_abilities(TR64_LARA_ABILITIES_TR2);
        for (int i = 0; i < 16; ++i) tick_tr1(TR64_GAME_FORWARD, 0);
        for (int i = 0; i < 25 && !(lara->flags & ITEM_FLAG_GRAVITY); ++i)
            tick_tr1(TR64_GAME_FORWARD | TR64_GAME_JUMP, 0);
        assert((lara->flags & ITEM_FLAG_GRAVITY) != 0);
        int saw_flip_tr2 = 0;
        for (int i = 0; i < 25 && (lara->flags & ITEM_FLAG_GRAVITY); ++i) {
            tick_tr1(TR64_GAME_BACK, 0);
            if ((lara->flags & ITEM_FLAG_GRAVITY) && abs(yaw_diff(start_yaw, lara->angle.y)) > 24000) {
                saw_flip_tr2 = 1;
                break;
            }
        }
        assert(saw_flip_tr2);
        printf("PASS TR1_WITH_TR2_AIR_ROLL yaw_delta=%d\n", (int)yaw_diff(start_yaw, lara->angle.y));

        /* Reset Lara to start */
        lara->pos = start_pos; lara->room = start_room;
        lara->angle = vec3s_create(0, start_yaw, 0);
        lara->hSpeed = lara->vSpeed = 0; lara->flags &= ~ITEM_FLAG_GRAVITY;
        ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
        gs->stamina[0] = 120;
        for (int i = 0; i < 4; ++i) tick_tr1(0, 0);

        /* 3. TR3 mode: Sprint (73), stamina drain, Sprint Roll (74) */
        set_abilities(TR64_LARA_ABILITIES_TR3);
        int saw_sprint = 0, saw_sprint_roll = 0;
        for (int i = 0; i < 20; ++i) tick_tr1(TR64_GAME_FORWARD, 0);
        for (int i = 0; i < 25; ++i) {
            tick_tr1(TR64_GAME_FORWARD, TR64_GAME_EXTRA_SPRINT);
            if (lara->state == Lara_STATE_SPRINT) saw_sprint = 1;
        }
        assert(saw_sprint && gs->stamina[0] < 120);
        for (int i = 0; i < 12; ++i) {
            tick_tr1(TR64_GAME_FORWARD | TR64_GAME_JUMP, TR64_GAME_EXTRA_SPRINT);
            if (lara->state == Lara_STATE_SPRINT_ROLL) { saw_sprint_roll = 1; break; }
        }
        assert(saw_sprint_roll);
        printf("PASS TR1_WITH_TR3_SPRINT_AND_ROLL stamina=%d\n", (int)gs->stamina[0]);

        /* Reset Lara to start for Crouch & Crawl */
        lara->pos = start_pos; lara->room = start_room;
        lara->angle = vec3s_create(0, start_yaw, 0);
        lara->hSpeed = lara->vSpeed = 0; lara->flags &= ~ITEM_FLAG_GRAVITY;
        ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
        for (int i = 0; i < 4; ++i) tick_tr1(0, 0);

        /* 4. TR3 mode: Crouch (71) -> Crawl Idle (80) -> Crawl Forward (81) + displacement -> Stand (2) */
        int saw_crouch = 0, saw_crawl_idle = 0, saw_crawl_fwd = 0;
        for (int i = 0; i < 25; ++i) {
            tick_tr1(0, TR64_GAME_EXTRA_CROUCH);
            if (lara->state == Lara_STATE_CROUCH_IDLE) saw_crouch = 1;
        }
        assert(saw_crouch);
        vec3i crawl_start = lara->pos;
        for (int i = 0; i < 55; ++i) {
            tick_tr1(TR64_GAME_FORWARD, TR64_GAME_EXTRA_CROUCH);
            if (lara->state == Lara_STATE_CRAWL_IDLE) saw_crawl_idle = 1;
            if (lara->state == Lara_STATE_CRAWL_FORWARD) saw_crawl_fwd = 1;
        }
        assert(saw_crawl_idle && saw_crawl_fwd);
        int32_t moved = abs(lara->pos.x - crawl_start.x) + abs(lara->pos.z - crawl_start.z);
        assert(moved > 100);
        /* Live downgrade from TR3 -> TR1 while crawling must cleanly return to Stand */
        set_abilities(TR64_LARA_ABILITIES_TR1);
        assert(lara->state == Lara_STATE_STOP);
        for (int i = 0; i < 5; ++i) tick_tr1(0, 0);
        assert(lara->state == Lara_STATE_STOP);
        printf("PASS TR1_WITH_TR3_CROUCH_CRAWL_DOWNGRADE moved=%ld\n", (long)moved);
    } else if (!strcmp(mode, "gym_uw")) {
        /* Place Lara underwater in Gym pool (same coordinates as GYM/underwater-start fixture) */
        Room *water_room = NULL;
        for (uint32_t r = 0; r < gs->level.roomsCount; ++r) {
            if (tr64_u16(gs->rooms[r].info->flags) & ROOM_FLAG_WATER_MASK) {
                water_room = gs->rooms + r;
                break;
            }
        }
        assert(water_room != NULL);
        int32_t cx = tr64_i32(water_room->info->x) + (tr64_u16(water_room->info->x_sectors) * 1024) / 2;
        int32_t cz = tr64_i32(water_room->info->z) + (tr64_u16(water_room->info->z_sectors) * 1024) / 2;
        int32_t cy = (tr64_i32(water_room->info->y_top) + tr64_i32(water_room->info->y_bottom)) / 2;
        Room_remove(lara->room, lara);
        Room_add(water_room, lara);
        lara->pos = vec3i_create(cx, cy, cz);
        lara->waterState = WATER_STATE_UNDER;
        lara->angle = vec3s_create(0, 0, 0);
        ItemObj_animSet(lara, Lara_ANIM_UNDERWATER, true, 0);
        for (int i = 0; i < 5; ++i) tick_tr1(0, 0);

        /* Negative control: TR1 mode suppresses underwater 180 roll */
        set_abilities(TR64_LARA_ABILITIES_TR1);
        int16_t yaw0 = lara->angle.y;
        int saw_uw_roll_tr1 = 0;
        for (int i = 0; i < 20; ++i) {
            tick_tr1(TR64_GAME_FORWARD | TR64_GAME_BACK, 0);
            if (lara->state == Lara_STATE_ROLL_UW) saw_uw_roll_tr1 = 1;
        }
        assert(!saw_uw_roll_tr1 && abs(yaw_diff(yaw0, lara->angle.y)) < 4000);
        printf("PASS NEG_CONTROL_TR1_NO_UW_ROLL\n");

        /* TR2 mode: Underwater 180 roll executes and flips yaw ~180 deg */
        set_abilities(TR64_LARA_ABILITIES_TR2);
        int saw_uw_roll_tr2 = 0, flipped = 0;
        for (int i = 0; i < 30; ++i) {
            tick_tr1(i < 3 ? (TR64_GAME_FORWARD | TR64_GAME_BACK) : 0, 0);
            if (lara->state == Lara_STATE_ROLL_UW) saw_uw_roll_tr2 = 1;
            if (abs(yaw_diff(yaw0, lara->angle.y)) > 24000) flipped = 1;
        }
        assert(saw_uw_roll_tr2 && flipped);
        printf("PASS TR1_WITH_TR2_UW_ROLL yaw_delta=%d\n", (int)yaw_diff(yaw0, lara->angle.y));
    }
}

static void run_tr2_suite(void)
{
    Lara *lara = gs->players[0];
    assert(lara && lara->extraL && gs->tr2);
    assert(gs->extraAnims.hasTR2 && gs->extraAnims.hasTR3);

    /* Place Lara near the back of a flat dry 3x3 courtyard in Room 0 (spawn starts airborne on a slope). */
    Room_remove(lara->room, lara);
    Room_add(gs->rooms + 0, lara);
    lara->pos = vec3i_create(33280, 0, 32384);
    lara->angle = vec3s_create(0, 0, 0);
    lara->hSpeed = lara->vSpeed = 0;
    lara->flags &= ~ITEM_FLAG_GRAVITY;
    lara->waterState = WATER_STATE_ABOVE;
    ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
    for (int i = 0; i < 4; ++i) tick_tr2(0, 0);
    vec3i start_pos = lara->pos;
    Room *start_room = lara->room;
    int16_t start_yaw = lara->angle.y;

    /* 1. Negative control C: TR1 mode in TR2 disables climbEnabled and mid-air 180 roll */
    set_abilities(TR64_LARA_ABILITIES_TR1);
    assert(!gs->tr2->climbEnabled);
    for (int i = 0; i < 25 && !(lara->flags & ITEM_FLAG_GRAVITY); ++i)
        tick_tr2(TR64_GAME_FORWARD | TR64_GAME_JUMP, 0);
    assert((lara->flags & ITEM_FLAG_GRAVITY) != 0);
    int saw_flip_tr1 = 0;
    for (int i = 0; i < 25 && (lara->flags & ITEM_FLAG_GRAVITY); ++i) {
        tick_tr2(TR2_IN_ROLL, 0);
        if ((lara->flags & ITEM_FLAG_GRAVITY) && abs(yaw_diff(start_yaw, lara->angle.y)) > 16000)
            saw_flip_tr1 = 1;
    }
    assert(!saw_flip_tr1);
    printf("PASS NEG_CONTROL_TR2_DOWNGRADE_TR1\n");

    /* Reset Lara */
    lara->pos = start_pos; lara->room = start_room;
    lara->angle = vec3s_create(0, start_yaw, 0);
    lara->hSpeed = lara->vSpeed = 0; lara->flags &= ~ITEM_FLAG_GRAVITY;
    ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
    for (int i = 0; i < 4; ++i) tick_tr2(0, 0);

    /* 2. TR2 mode re-enables climbEnabled and mid-air 180 roll */
    set_abilities(TR64_LARA_ABILITIES_TR2);
    assert(gs->tr2->climbEnabled);
    for (int i = 0; i < 25 && !(lara->flags & ITEM_FLAG_GRAVITY); ++i)
        tick_tr2(TR64_GAME_FORWARD | TR64_GAME_JUMP, 0);
    assert((lara->flags & ITEM_FLAG_GRAVITY) != 0);
    int saw_flip_tr2 = 0;
    for (int i = 0; i < 25 && (lara->flags & ITEM_FLAG_GRAVITY); ++i) {
        tick_tr2(TR2_IN_ROLL, 0);
        if ((lara->flags & ITEM_FLAG_GRAVITY) && abs(yaw_diff(start_yaw, lara->angle.y)) > 24000) {
            saw_flip_tr2 = 1;
            break;
        }
    }
    assert(saw_flip_tr2);
    printf("PASS TR2_NATIVE_AIR_ROLL_AND_CLIMB\n");

    /* Reset Lara */
    lara->pos = start_pos; lara->room = start_room;
    lara->angle = vec3s_create(0, start_yaw, 0);
    lara->hSpeed = lara->vSpeed = 0; lara->flags &= ~ITEM_FLAG_GRAVITY;
    ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
    gs->stamina[0] = 120;
    for (int i = 0; i < 4; ++i) tick_tr2(0, 0);

    /* 3. TR3 mode in TR2: Sprint (73), Sprint Roll (74), Crouch (71), Crawl (80/81) */
    set_abilities(TR64_LARA_ABILITIES_TR3);
    int saw_sprint = 0, saw_sroll = 0;
    for (int i = 0; i < 16; ++i) tick_tr2(TR64_GAME_FORWARD, 0);
    for (int i = 0; i < 30; ++i) {
        tick_tr2(TR64_GAME_FORWARD, TR64_GAME_EXTRA_SPRINT);
        if (lara->state == Lara_STATE_SPRINT) saw_sprint = 1;
        if (saw_sprint && gs->stamina[0] <= 105) break;
    }
    assert(saw_sprint && gs->stamina[0] < 120);
    for (int i = 0; i < 20; ++i) {
        tick_tr2(TR64_GAME_FORWARD | TR64_GAME_JUMP, TR64_GAME_EXTRA_SPRINT);
        if (lara->state == Lara_STATE_SPRINT_ROLL) { saw_sroll = 1; break; }
    }
    assert(saw_sroll);

    /* Reset Lara for Crouch + Crawl */
    lara->pos = start_pos; lara->room = start_room;
    lara->angle = vec3s_create(0, start_yaw, 0);
    lara->hSpeed = lara->vSpeed = 0; lara->flags &= ~ITEM_FLAG_GRAVITY;
    ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
    for (int i = 0; i < 4; ++i) tick_tr2(0, 0);

    int saw_crouch = 0, saw_crawl = 0;
    for (int i = 0; i < 25; ++i) {
        tick_tr2(0, TR64_GAME_EXTRA_CROUCH);
        if (lara->state == Lara_STATE_CROUCH_IDLE) saw_crouch = 1;
    }
    assert(saw_crouch);
    vec3i cstart = lara->pos;
    for (int i = 0; i < 55; ++i) {
        tick_tr2(TR64_GAME_FORWARD, TR64_GAME_EXTRA_CROUCH);
        if (lara->state == Lara_STATE_CRAWL_FORWARD) saw_crawl = 1;
    }
    int32_t moved = abs(lara->pos.x - cstart.x) + abs(lara->pos.z - cstart.z);
    assert(saw_crawl && moved > 100);
    printf("PASS TR2_WITH_TR3_SPRINT_CROUCH_CRAWL moved=%ld\n", (long)moved);
}

void __wrap_game_tick_players(const uint16_t input_mask[TR64_GAME_PLAYERS])
{
    __real_game_tick_players(input_mask);
    const char *mode = getenv("TR64_LA_MODE");
    if (!g_ran && mode && gs && gs->core && gs->core->game < 2) {
        g_ran = 1;
        run_tr1_suite(mode);
    }
}

void __wrap_game_tr2_tick(uint32_t mask)
{
    __real_game_tr2_tick(mask);
    const char *mode = getenv("TR64_LA_MODE");
    if (!g_ran && mode && !strcmp(mode, "tr2_wall") && gs && gs->core && gs->core->game == 2) {
        g_ran = 1;
        run_tr2_suite();
    }
}
'''


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise SystemExit(f'FAIL: {msg}')


def run(cmd: list[str], env: dict[str, str] | None = None, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(['rtk', *cmd], capture_output=True, text=True, env=env, timeout=timeout)
    if p.returncode != 0:
        raise SystemExit(f'FAIL ({p.returncode}): {" ".join(cmd)}\n{p.stdout[-1200:]}\n{p.stderr[-1200:]}')
    return p


def build_host(build_dir: Path, pack_path: Path) -> Path:
    exe = build_dir / 'host/tr64-host'
    run(['make', '-j4', f'BUILD_DIR={build_dir}', f'TR64_LEVEL_PACK={pack_path}', str(exe)])
    expect(exe.exists(), f'missing {exe}')
    return exe


def build_probe(build_dir: Path, work: Path, tag: str) -> Path:
    src = work / f'la_probe_{tag}.c'
    obj = work / f'la_probe_{tag}.o'
    exe = work / f'la-probe-{tag}'
    src.write_text(PROBE_C, encoding='utf-8')
    gen = build_dir / 'generated'
    objs = sorted((build_dir / 'host').rglob('*.o'))
    expect(bool(objs), f'no host objects under {build_dir / "host"}')
    run([
        'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        f'-I{ROOT}', f'-I{ROOT / "include"}', f'-I{gen}', f'-I{gen / "host"}',
        '-DTR64_GAME=1', '-DTR64_HOST=1', '-D_POSIX_C_SOURCE=200809L',
        '-c', str(src), '-o', str(obj),
    ])
    run([
        'cc', '-m32',
        '-Wl,--wrap=game_tick_players', '-Wl,--wrap=game_tr2_tick', '-Wl,--wrap=eng_game_save_settings',
        '-o', str(exe), str(obj), *(str(p) for p in objs),
    ])
    return exe


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--work-dir', type=Path, required=True)
    args = ap.parse_args()

    work = args.work_dir
    work.mkdir(parents=True, exist_ok=True)
    cache = work / 'compressor'
    tr2_donor = Path('/tmp/tr64/data/pc-tr2/WALL.TR2')
    tr3_donor = Path('/tmp/tr64/data/pc-tr3/JUNGLE.TR2')
    tr2_sfx = Path('/tmp/tr64/data/pc-tr2/MAIN.SFX')

    # 1. Cook un-grafted level1, grafted level1, grafted gym, grafted wall
    p_l1_plain = work / 'level1_plain.pack'
    p_l1_graft = work / 'level1_graft.pack'
    p_gym_graft = work / 'gym_graft.pack'
    p_tr1_merged = work / 'tr1_graft_merged.pack'
    p_wall_graft = work / 'wall_graft.pack'

    run(['python3', str(ROOT / 'tools/cook/pack_level.py'),
         '--source', 'pc', '--input', '/tmp/tr64/data/pc/level1.phd',
         '--cache', str(cache), '--out', str(p_l1_plain)])
    run(['python3', str(ROOT / 'tools/cook/pack_level.py'),
         '--source', 'pc', '--input', '/tmp/tr64/data/pc/level1.phd',
         '--lara-tr2-donor', str(tr2_donor), '--lara-tr3-donor', str(tr3_donor),
         '--cache', str(cache), '--out', str(p_l1_graft)])
    run(['python3', str(ROOT / 'tools/cook/pack_level.py'),
         '--source', 'pc', '--input', '/tmp/tr64/data/pc/gym.phd',
         '--lara-tr2-donor', str(tr2_donor), '--lara-tr3-donor', str(tr3_donor),
         '--cache', str(cache), '--out', str(p_gym_graft)])
    run(['python3', str(ROOT / 'tools/cook/pack_merge.py'),
         '--inputs', str(p_l1_graft), str(p_gym_graft), '--out', str(p_tr1_merged)])
    run(['python3', str(ROOT / 'tools/cook/pack_level.py'),
         '--source', 'pc', '--input', str(tr2_donor), '--main-sfx', str(tr2_sfx),
         '--lara-tr3-donor', str(tr3_donor), '--cache', str(cache), '--out', str(p_wall_graft)])
    print('PASS cook: grafted TR1 (level1, gym) and TR2 (wall) packs validated')

    # 2. Build host binaries bound to grafted and ungrafted packs
    b_graft = args.build_dir / 'host-graft'
    b_plain = args.build_dir / 'host-plain'
    graft_host = build_host(b_graft, p_tr1_merged)
    build_host(b_plain, p_l1_plain)

    # 3. Verify ALL 25 TR1 fixtures pass bit-exact on the GRAFTED TR1 pack!
    run(['python3', str(ROOT / 'test/game/fixtures.py'),
         '--executable', str(graft_host), '--level', 'level1',
         '--out', str(work / 'fix-level1')])
    run(['python3', str(ROOT / 'test/game/fixtures.py'),
         '--executable', str(graft_host), '--level', 'gym',
         '--out', str(work / 'fix-gym')])
    print('PASS 25 TR1 fixtures (level1 12 + gym 13) bit-exact on GRAFTED TR1 pack')

    # 4. Run directed C probe checks (menu + TR1/TR2/TR3 abilities + 3 negative controls)
    probe_plain = build_probe(b_plain, work, 'plain')
    probe_graft = build_probe(b_graft, work, 'graft')
    inp = work / 'in.txt'
    inp.write_text('0\n0\n', encoding='ascii')

    # 4a. Negative control A: un-grafted pack
    env = dict(os.environ, TR64_LA_MODE='ungrafted')
    p = run([str(probe_plain), '--game-input', str(inp), '--game-level', 'level1',
             '--game-ticks', '2', '--game-trace-only'], env=env)
    expect('PASS NEG_CONTROL_UNGRAFTED' in p.stdout, p.stdout)
    print('PASS NEG_CONTROL_UNGRAFTED')

    # 4b. TR1 grafted level1: Menu + TR2 Air-Roll + TR3 Sprint/Roll/Crouch/Crawl + Negative control B
    env = dict(os.environ, TR64_LA_MODE='level1')
    p = run([str(probe_graft), '--game-input', str(inp), '--game-level', 'level1',
             '--game-ticks', '2', '--game-trace-only'], env=env)
    for tag in ('PASS TR1_MENU_ABILITIES', 'PASS NEG_CONTROL_TR1_NO_AIR_ROLL',
                'PASS TR1_WITH_TR2_AIR_ROLL', 'PASS TR1_WITH_TR3_SPRINT_AND_ROLL',
                'PASS TR1_WITH_TR3_CROUCH_CRAWL_DOWNGRADE'):
        expect(tag in p.stdout, f'missing {tag} in:\n{p.stdout}')
        print([ln for ln in p.stdout.splitlines() if ln.startswith(tag)][0])

    # 4c. TR1 grafted gym: Underwater 180 roll + negative control
    env = dict(os.environ, TR64_LA_MODE='gym_uw')
    p = run([str(probe_graft), '--game-input', str(inp), '--game-level', 'gym',
             '--game-ticks', '2', '--game-trace-only'], env=env)
    for tag in ('PASS NEG_CONTROL_TR1_NO_UW_ROLL', 'PASS TR1_WITH_TR2_UW_ROLL'):
        expect(tag in p.stdout, f'missing {tag} in:\n{p.stdout}')
        print([ln for ln in p.stdout.splitlines() if ln.startswith(tag)][0])

    # 4d. TR2 grafted wall: TR1 downgrade (negative control C), TR2 native, TR3 Sprint/Crouch/Crawl
    tr2_out = work / 'wall_trace.jsonl'
    env = dict(os.environ, TR64_LA_MODE='tr2_wall')
    p = run([str(probe_graft), '--game-tr2-schema21', str(p_wall_graft), str(inp), str(tr2_out)], env=env)
    for tag in ('PASS NEG_CONTROL_TR2_DOWNGRADE_TR1', 'PASS TR2_NATIVE_AIR_ROLL_AND_CLIMB',
                'PASS TR2_WITH_TR3_SPRINT_CROUCH_CRAWL'):
        expect(tag in p.stdout, f'missing {tag} in:\n{p.stdout}')
        print([ln for ln in p.stdout.splitlines() if ln.startswith(tag)][0])

    print('ALL CROSS-GAME LARA ABILITIES VERIFICATION CHECKS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
