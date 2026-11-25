/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Host test & live render harness for Tomb Raider III (TR64_GAME=3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/tr3/tr3.h"
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/engine.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/render.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/ui/format.h"
#include "engine/ui/ui.h"

extern void tr64_gpu_init(void) __attribute__((weak));
extern void tr64_snd_boot(void) __attribute__((weak));
extern void tr64_snd_level_start(const tr64_level_t *) __attribute__((weak));
extern void tr64_snd_level_stop(void) __attribute__((weak));
extern void tr64_snd_tick(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));
extern void tr64_snd_report(void) __attribute__((weak));
extern void tr64_scene_load(const char *level) __attribute__((weak));

#define TR3_CHECK(cond, msg) do { \
    if (!(cond)) sys_error("tr3-test FAIL: %s", (msg)); \
} while (0)

/* Build synthetic split-triangle word0: op(0..4), split1(5..9), split2(10..14), end(15). */
static uint16_t make_split_w0(uint32_t op, int32_t s1, int32_t s2)
{
    return (uint16_t)(0x8000u | (op & 0x1Fu) | (((uint32_t)s1 & 0x1Fu) << 5) | (((uint32_t)s2 & 0x1Fu) << 10));
}

static uint16_t make_split_w1(uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3)
{
    return (uint16_t)((t0 & 0xFu) | ((t1 & 0xFu) << 4) | ((t2 & 0xFu) << 8) | ((t3 & 0xFu) << 12));
}

static void run_synthetic_floor_tests(void)
{
    /* 1. Box word unpacking (§1.1). */
    tr3_box_word_t bw = tr3_unpack_box_word((uint16_t)(0x8000u | (0x345u << 4) | 0xAu));
    TR3_CHECK(bw.footstep == 0xAu && bw.box_index == 0x345u && bw.stopper == 1u, "box word unpack");
    tr3_box_word_t bw_none = tr3_unpack_box_word((uint16_t)(0x7FFu << 4));
    TR3_CHECK(bw_none.box_index == 0x7FFu && bw_none.stopper == 0u, "box word none");

    /* 2. Split-triangle floor 0x07 (NW-SE both solid): test NE vs SW halves, tilt, class, chunky cam (§1.2). */
    int32_t y = 0; int8_t tx = 0, tz = 0; uint8_t cls = 0; bool popen = false;
    uint16_t w0 = make_split_w0(TR3_FD_SPLIT_F_NWSE, -2, 1); /* h1=-512, h2=+256 */
    uint16_t w1 = make_split_w1(0, 1, 2, 5); /* t0=0, t1=1, t2=2, t3=5 -> NE: tx=3, tz=5 (steep diagonal=3); SW: tx=-1, tz=1 (gentle=4) */
    tr3_eval_split_tri(w0, w1, 2048, 800, 800, false, &y, &tx, &tz, &cls, &popen); /* NE: dx=800 > 1024-800=224 */
    TR3_CHECK(tx == 3 && tz == 5 && cls == TR3_HEIGHT_DIAGONAL && !popen, "0x07 NE steep diagonal");
    tr3_eval_split_tri(w0, w1, 2048, 100, 100, false, &y, &tx, &tz, &cls, &popen); /* SW: dx=100 <= 924 */
    TR3_CHECK(tx == -1 && tz == 1 && cls == TR3_HEIGHT_SPLIT_TRI && !popen, "0x07 SW gentle split");
    /* Chunky camera floor evaluates to min(base+h1, base+h2) = 2048 - 512 = 1536. */
    tr3_eval_split_tri(w0, w1, 2048, 100, 100, true, &y, NULL, NULL, NULL, NULL);
    TR3_CHECK(y == 1536, "0x07 chunky camera floor min");

    /* 3. Split-triangle ceiling 0x09 (NW-SE): negated nibbles & chunky camera max (§1.2). */
    uint16_t cw0 = make_split_w0(TR3_FD_SPLIT_C_NWSE, -2, 1);
    tr3_eval_split_tri(cw0, w1, 0, 800, 800, false, &y, &tx, &tz, NULL, NULL);
    /* Negated: t0=0, t1=-1, t2=-2, t3=-5 -> NE ceil: tx=t0-t1=+1, tz=t3-t0=-5. */
    TR3_CHECK(tx == 1 && tz == -5, "0x09 NE ceiling negated nibbles");
    tr3_eval_split_tri(cw0, w1, 0, 100, 100, true, &y, NULL, NULL, NULL, NULL);
    TR3_CHECK(y == 256, "0x09 chunky camera ceiling max");

    /* 4. Half-portal predicates 0x0B..0x12 (§1.2). */
    const uint16_t p_ops[8] = {
        TR3_FD_SPLIT_FP_SW, TR3_FD_SPLIT_FP_NE, TR3_FD_SPLIT_FP_NW, TR3_FD_SPLIT_FP_SE,
        TR3_FD_SPLIT_CP_SW, TR3_FD_SPLIT_CP_NE, TR3_FD_SPLIT_CP_NW, TR3_FD_SPLIT_CP_SE
    };
    for (int k = 0; k < 8; ++k) {
        bool a = false, b = false;
        uint16_t pw = make_split_w0(p_ops[k], 0, 0);
        if (k == 0 || k == 1 || k == 4 || k == 5) {
            /* NW-SE: (100,100) is SW, (900,900) is NE */
            tr3_eval_split_tri(pw, 0, 0, 100, 100, false, NULL, NULL, NULL, NULL, &a);
            tr3_eval_split_tri(pw, 0, 0, 900, 900, false, NULL, NULL, NULL, NULL, &b);
            bool expect_sw = (k == 0 || k == 4);
            TR3_CHECK(a == expect_sw && b == !expect_sw, "NW-SE portal half predicate");
        } else {
            /* NE-SW: (100,900) is NW (dx<=dz), (900,100) is SE (dx>dz) */
            tr3_eval_split_tri(pw, 0, 0, 100, 900, false, NULL, NULL, NULL, NULL, &a);
            tr3_eval_split_tri(pw, 0, 0, 900, 100, false, NULL, NULL, NULL, NULL, &b);
            bool expect_nw = (k == 2 || k == 6);
            TR3_CHECK(a == expect_nw && b == !expect_nw, "NE-SW portal half predicate");
        }
    }
    printf("PASS synthetic floor tests (box unpack, 0x07..0x12 split-tri, chunky cam, portal halves)\n");
}

static void run_live_pack_tests(const char *level_name)
{
    tr3_state_t *st = tr3_gs;
    TR3_CHECK(st != NULL && st->level != NULL, "state initialized");

    /* Verify India campaign script metadata (§4.2). */
    const tr3_script_level_t *sj = tr3_script_by_name("jungle");
    const tr3_script_level_t *stmp = tr3_script_by_name("temple");
    TR3_CHECK(sj && sj->ambient_track == 34 && sj->rain == 1 && sj->secrets_total == 6 && sj->cold == 0,
              "India script metadata: Jungle");
    TR3_CHECK(stmp && stmp->ambient_track == 34 && stmp->rain == 0 && stmp->secrets_total == 4,
              "India script metadata: Temple");

    /* Verify starting stock (§4.3). */
    TR3_CHECK(st->lara.hp == 1000 && st->lara.stamina == 120 && st->lara.air == 1800 &&
              st->lara.small_medi == 1 && st->lara.large_medi == 1 && st->lara.flares == 2 &&
              (st->lara.owned_weapons & (1u << TR3_WP_PISTOLS)) &&
              !(st->lara.owned_weapons & (1u << TR3_WP_SHOTGUN)),
              "TR3 India starting stock");

    /* 1. Sprint stamina full-bar gate & authentic AnimState transitions (§2.2). */
    int32_t sx = st->lara.x, sy = st->lara.y, sz = st->lara.z;
    uint16_t sroom = st->lara.room;
    int16_t syaw = st->lara.rot_y;
    tr3_lara_set_anim(st, 0, TR3_ST_RUN);
    game_tr3_tick(TR3_IN_FORWARD | TR3_IN_SPRINT);
    TR3_CHECK(st->lara.state == TR3_ST_SPRINT && st->lara.stamina == 119,
              "sprint starts at full 120 stamina via authored transition");
    /* From Sprint loop (223), releasing sprint transitions smoothly via authored decel (243) back to Run (1). */
    game_place(sroom, sx, sy, sz, syaw);
    tr3_lara_set_anim(st, 223, TR3_ST_SPRINT);
    game_tr3_tick(TR3_IN_FORWARD);
    TR3_CHECK(st->lara.state == TR3_ST_RUN && st->lara.stamina == 120,
              "sprint decelerates smoothly back to Run and recovers +1/tick");
    /* Negative control: partial stamina (<120) must NOT allow re-entering Sprint! */
    st->lara.stamina = 115;
    tr3_lara_set_anim(st, 0, TR3_ST_RUN);
    game_tr3_tick(TR3_IN_FORWARD | TR3_IN_SPRINT);
    TR3_CHECK(st->lara.state == TR3_ST_RUN, "full-bar 120 stamina gate blocks re-entry");
    /* Sprint Roll (74) from Sprint loop (223) on Jump. */
    game_place(sroom, sx, sy, sz, syaw);
    st->lara.stamina = 120;
    tr3_lara_set_anim(st, 223, TR3_ST_SPRINT);
    game_tr3_tick(TR3_IN_FORWARD | TR3_IN_SPRINT | TR3_IN_JUMP);
    TR3_CHECK(st->lara.state == TR3_ST_SPRINT_ROLL, "jump during sprint triggers sprint roll (74)");

    /* Restore start position for remaining behavioral tests. */
    game_place(sroom, sx, sy, sz, syaw);
    tr3_lara_set_anim(st, 103, TR3_ST_STOP);

    /* 1b. Shared Lara controller checks: Run-Start -> Run, Ground Roll, Jump, & 30 Hz interpolation. */
    game_tr3_tick(TR3_IN_FORWARD);
    TR3_CHECK(st->lara.state == TR3_ST_RUN && st->lara.anim == 6,
              "Stand + Forward enters authored Run-Start clip (6)");
    for (int t = 0; t < 20; ++t) game_tr3_tick(TR3_IN_FORWARD);
    TR3_CHECK(st->lara.state == TR3_ST_RUN && st->lara.anim == 0 &&
              (st->lara.x != sx || st->lara.z != sz),
              "Run-Start blends into Run loop (0) and displaces Lara");
    /* Smooth 30 Hz keyframe interpolation check on rate=4 Stand clip (103):
     * intermediate frames (delta=1,2) must differ from keyframe (delta=0), while delta=0 repeats identically. */
    game_place(sroom, sx, sy, sz, syaw);
    tr3_lara_set_anim(st, 103, TR3_ST_STOP);
    uint16_t f0 = st->lara.frame;
    tr64_game_drawable_t d0[32], d0_copy[32], d1[32], d2[32];
    uint32_t cur = 0;
    uint32_t n0 = game_view_room(st->lara.room, &cur, d0, 32);
    cur = 0;
    uint32_t n0b = game_view_room(st->lara.room, &cur, d0_copy, 32);
    TR3_CHECK(n0 >= 15 && n0 == n0b && memcmp(d0, d0_copy, n0 * sizeof(d0[0])) == 0,
              "negative control: delta=0 pose is deterministic");
    st->lara.frame = (uint16_t)(f0 + 1u);
    cur = 0; uint32_t n1 = game_view_room(st->lara.room, &cur, d1, 32);
    st->lara.frame = (uint16_t)(f0 + 2u);
    cur = 0; uint32_t n2 = game_view_room(st->lara.room, &cur, d2, 32);
    TR3_CHECK(n1 == n0 && n2 == n0 &&
              (memcmp(d0, d1, 15 * sizeof(d0[0])) != 0 || memcmp(d0, d2, 15 * sizeof(d0[0])) != 0),
              "30 Hz keyframe interpolation smoothly updates intermediate bone matrices");
    st->lara.frame = f0;
    /* Ground Roll (146 -> 147) rotates 180 deg at authored FX_ROTATE_180, not on tick 1. */
    int16_t roll_yaw0 = st->lara.rot_y;
    game_tr3_tick(TR3_IN_ROLL);
    TR3_CHECK(st->lara.state == TR3_ST_ROLL && st->lara.rot_y == roll_yaw0,
              "Ground Roll enters clip 146 without snapping yaw on tick 1");
    for (int t = 0; t < 35; ++t) game_tr3_tick(0);
    TR3_CHECK((int16_t)(st->lara.rot_y - roll_yaw0) == (int16_t)32768,
              "Ground Roll completes authored 180-deg turn via FX_ROTATE_180");

    /* Restore start position for Crouch & Crawl test. */
    game_place(sroom, sx, sy, sz, syaw);
    tr3_lara_set_anim(st, 103, TR3_ST_STOP);

    /* 2. Crouch & Crawl + rifle block (§2.3) via real authored transitions (217->245->222->258->263->259->221). */
    game_tr3_tick(TR3_IN_CROUCH);
    TR3_CHECK(st->lara.state == TR3_ST_CROUCH_IDLE, "enter crouch idle (71)");
    for (int t = 0; t < 25 && st->lara.anim != 222; ++t) game_tr3_tick(TR3_IN_CROUCH);
    TR3_CHECK(st->lara.anim == 222 && st->lara.state == TR3_ST_CROUCH_IDLE, "settled into crouch idle clip (222)");
    game_tr3_tick(TR3_IN_CROUCH | TR3_IN_FORWARD);
    TR3_CHECK(st->lara.state == TR3_ST_CRAWL_IDLE, "crouch + forward enters crawl idle (80)");
    for (int t = 0; t < 25 && st->lara.anim != 263; ++t) game_tr3_tick(TR3_IN_CROUCH);
    TR3_CHECK(st->lara.anim == 263 && st->lara.state == TR3_ST_CRAWL_IDLE, "settled into crawl idle clip (263)");
    game_tr3_tick(0);
    TR3_CHECK(st->lara.state == TR3_ST_CROUCH_IDLE, "release crouch exits crawl to crouch");
    for (int t = 0; t < 25 && st->lara.state != TR3_ST_STOP; ++t) game_tr3_tick(0);
    TR3_CHECK(st->lara.state == TR3_ST_STOP, "release crouch stands up smoothly when headroom clear");
    /* Rifle weapon blocks crouch. */
    game_place(sroom, sx, sy, sz, syaw);
    tr3_lara_set_anim(st, 103, TR3_ST_STOP);
    st->lara.owned_weapons |= (uint16_t)(1u << TR3_WP_SHOTGUN);
    st->lara.weapon = TR3_WP_SHOTGUN;
    st->lara.guns_drawn = 1;
    game_tr3_tick(TR3_IN_CROUCH);
    TR3_CHECK(st->lara.state != TR3_ST_CROUCH_IDLE, "rifle blocks entering crouch");
    st->lara.guns_drawn = 0;
    st->lara.weapon = TR3_WP_PISTOLS;

    /* 3. Poison pulse every 64 ticks + Medipack cure (§2.6). */
    st->lara.hp = 1000;
    st->lara.poison = 160;
    st->ticks = 63; /* next tick will be 64 -> (64*4)&255 == 0 -> deals 160>>4 = 10 HP */
    game_tr3_tick(0);
    TR3_CHECK(st->lara.hp == 990 && st->lara.poison == 160, "poison pulse deals poison>>4 (10 HP) every 64 ticks");
    tr3_lara_use_medipack(st, false);
    TR3_CHECK(st->lara.hp == 1000 && st->lara.poison == 0, "medipack cures poison");

    /* 4. Save Crystal (180) proximity heal + poison cure (§3.6). */
    int crystal_idx = -1;
    for (uint16_t i = 0; i < st->item_count; ++i)
        if (st->items[i].type == TR3_OBJ_SAVE_CRYSTAL && !st->items[i].invisible) { crystal_idx = i; break; }
    if (crystal_idx >= 0) {
        tr3_item_t *cr = &st->items[crystal_idx];
        int16_t rot0 = cr->rot_y;
        game_tr3_tick(0);
        TR3_CHECK((int16_t)(cr->rot_y - rot0) == 1024, "Save Crystal spins +1024/tick");
        st->lara.hp = 400;
        st->lara.poison = 160;
        game_place(cr->room, cr->x, cr->init_y, cr->z, 0);
        game_tr3_tick(0);
        TR3_CHECK(st->lara.hp == 900 && st->lara.poison == 0 && cr->invisible == 1,
                  "Save Crystal auto-collects (+500 HP, cures poison, destroys crystal)");
    }

    /* 5. Small Flame (331) zero damage & zero ignition (§3.7). */
    for (uint16_t i = 0; i < st->item_count; ++i) {
        if (st->items[i].type == TR3_OBJ_SMALL_FLAME) {
            st->lara.hp = 800;
            st->lara.burning = 0;
            game_place(st->items[i].room, st->items[i].x, st->items[i].y, st->items[i].z, 0);
            for (int t = 0; t < 10; ++t) game_tr3_tick(0);
            TR3_CHECK(st->lara.hp == 800 && st->lara.burning == 0, "Small Flame (331) never damages or ignites");
            break;
        }
    }

    /* 6. Quicksand / Swamp (0x80) sinking & deep suffocation (§2.5) on a real Jungle swamp room! */
    const uint32_t rcount = tr64_level_table(st->level, TR64_CORE_rooms)->count;
    for (uint32_t r = 0; r < rcount; ++r) {
        const tr64_core_rooms_t *rm = tr64_level_rooms(st->level, r);
        if (tr64_u16(rm->flags) & TR3_ROOM_SWAMP) {
            int32_t rx = tr64_i32(rm->x) + (int32_t)(tr64_u16(rm->x_sectors) / 2u) * TR3_TILE + 512;
            int32_t rz = tr64_i32(rm->z) + (int32_t)(tr64_u16(rm->z_sectors) / 2u) * TR3_TILE + 512;
            tr3_floor_sample_t sfs;
            tr3_floor_eval(st, rx, tr64_i32(rm->y_top) + 256, rz, (uint16_t)r, false, &sfs);
            if (sfs.in_swamp && sfs.floor_y != TR3_NO_HEIGHT && sfs.water_y != TR3_NO_HEIGHT) {
                int32_t start_y = sfs.water_y + 300;
                if (start_y < sfs.floor_y - 20) {
                    game_place((uint16_t)r, rx, start_y, rz, 0);
                    game_tr3_tick(0);
                    TR3_CHECK(st->lara.water_status == TR3_WATER_WADE && st->lara.y == start_y + 2,
                              "swamp locks wade and sinks +2/tick");
                }
                /* Test deep suffocation (> 775 below surface): -6 air/tick, then -10 HP/tick. */
                st->lara.hp = 500;
                st->lara.air = 4;
                game_place((uint16_t)r, rx, sfs.water_y + 800, rz, 0);
                game_tr3_tick(0);
                TR3_CHECK(st->lara.air == -1 && st->lara.hp == 490,
                          "swamp deep suffocation (>775) drains -6 air and -10 HP/tick");
                break;
            }
        }
    }

    /* 7. Monkey ally-hostility flip, AI waypoint binding, and Tiger 24 HP (§3.2, §3.3, §3.4). */
    int monkey_idx = -1, tiger_idx = -1;
    bool found_ai_modify_monkey = false;
    for (uint16_t i = 0; i < st->item_count; ++i) {
        if (st->items[i].type == TR3_OBJ_MONKEY) {
            if (monkey_idx < 0) monkey_idx = i;
            if (st->items[i].ai_bits & TR3_AI_MODIFY) found_ai_modify_monkey = true;
        }
        if (st->items[i].type == TR3_OBJ_TIGER && tiger_idx < 0) tiger_idx = i;
    }
    if (monkey_idx >= 0) {
        TR3_CHECK(st->items[monkey_idx].hp == 8 && st->ally_hostile == 0, "Monkey starts 8 HP & friendly");
        if (!strcmp(level_name, "jungle"))
            TR3_CHECK(found_ai_modify_monkey, "Jungle AI Modify waypoint bound to Monkey in Room 132");
        tr3_entities_call(st, TR3_ENT_HIT, 0, monkey_idx, 2);
        TR3_CHECK(st->ally_hostile == 1 && st->items[monkey_idx].hp == 6,
                  "hurting one Monkey flips global ally_hostile=1");
    }
    if (tiger_idx >= 0) {
        TR3_CHECK(st->items[tiger_idx].hp == 24, "Tiger starts with TR3 24 HP");
        tr3_entities_call(st, TR3_ENT_HIT, 0, tiger_idx, 24);
        TR3_CHECK(st->items[tiger_idx].killed == 1 && st->stats.kills >= 1, "Tiger dies cleanly at 24 damage");
    }

    /* 8. Piranhas (338) swarm attack in anchor room (§3.5). */
    if (st->shoal_count > 0) {
        tr3_shoal_t *sh = &st->shoals[0];
        st->items[sh->item_index].active = 1;
        st->lara.hp = 500;
        game_place(sh->room, sh->anchor_x, sh->anchor_y + 256, sh->anchor_z, 0);
        sh->fish_x[0] = st->lara.x;
        sh->fish_y[0] = st->lara.y - 256;
        sh->fish_z[0] = st->lara.z;
        tr3_entities_call(st, TR3_ENT_TICK, 0, -1, 0);
        TR3_CHECK(st->lara.hp <= 496, "Piranha swarm deals 4 HP/tick per fish within 256");
    }

    /* 9. Save/Load roundtrip. */
    uint8_t save_buf[2048];
    uint32_t sbytes = game_save(save_buf, sizeof(save_buf));
    TR3_CHECK(sbytes > 0 && sbytes <= sizeof(save_buf), "TR3 save serialization fits SRAM slot");
    st->lara.hp = 123;
    TR3_CHECK(game_load(save_buf, sbytes) == 1 && st->lara.hp != 123, "TR3 save/load restores state");

    /* 10. Shared UI font (356), Sprint HUD, 3D Ring, Graphics Options, Stats & Pickup 3D view. */
    TR3_CHECK(tr64_ui_bind(st->level) == 1, "TR3 110-glyph font (sprite_sequence 356) binds cleanly");
    st->lara.stamina = 60;
    tr64_game_hud_view_t hud = {0};
    TR3_CHECK(game_hud_view(&hud) == 1 && hud.show_sprint == 1 && hud.sprint == 60 && hud.sprint_max == 120,
              "TR3 HUD exposes Sprint/Dash bar");
    TR3_CHECK(game_tr3_ring_open(TR64_GAME_MENU_PAGE_TITLE), "TR3 Start Menu ring opens");
    for (int i = 0; i < 12; ++i) game_tr3_ring_update(0);
    tr64_game_menu_view_t mv = {0};
    TR3_CHECK(game_menu_view(&mv) == 1 && mv.state == TR64_GAME_MENU_STATE_READY && mv.itemsCount >= 2,
              "TR3 Start Menu settles with 3D items");
    tr64_game_drawable_t ring_buf[32];
    uint32_t parts = game_menu_item_view(&mv, 0, ring_buf, 32);
    TR3_CHECK(parts > 0 && ring_buf[0].room == UINT32_MAX && ring_buf[0].water == 0,
              "TR3 3D ring model emits valid menu drawables");
    uint32_t wc = 0;
    const uint16_t *widths = tr64_ui_string_widths(&wc);
    tr64_game_menu_text_t texts[32];
    uint32_t tcnt = game_menu_text_view(&mv, widths, wc, texts, 32);
    TR3_CHECK(tcnt > 0 && tcnt <= 32, "TR3 menu text view populates labels");
    /* Navigate right to Graphics (181), open it, toggle Auto Distance & Filtering. */
    game_tr3_ring_update(TR64_GAME_RIGHT);
    for (int i = 0; i < 16; ++i) game_tr3_ring_update(0);
    game_tr3_ring_update(TR64_GAME_ACTION);
    for (int i = 0; i < 12; ++i) game_tr3_ring_update(0);
    TR3_CHECK(game_menu_view(&mv) == 1 && mv.state == TR64_GAME_MENU_STATE_SHOW && mv.optionsCount == 5,
              "TR3 Graphics Options exposes 5 rows (Gamma, Draw Distance, Auto Distance, Filtering, Show FPS)");
    /* Move down to Auto Distance (row 2) and toggle, then down to Filtering (row 3) and toggle. */
    game_tr3_ring_update(TR64_GAME_BACK); game_tr3_ring_update(0);
    game_tr3_ring_update(TR64_GAME_BACK); game_tr3_ring_update(0);
    game_tr3_ring_update(TR64_GAME_ACTION); game_tr3_ring_update(0);
    game_tr3_ring_update(TR64_GAME_BACK); game_tr3_ring_update(0);
    game_tr3_ring_update(TR64_GAME_ACTION); game_tr3_ring_update(0);
    TR3_CHECK(game_settings_view()->video_auto_dist == 1 && game_settings_view()->video_filter == 1,
              "TR3 Graphics Options toggles Auto Distance and Bilinear Filtering live");
    /* Reset settings back to defaults and test Level-End Stats formatting & 3D Pickup view. */
    tr64_game_settings_t reset_s = *game_settings_view();
    reset_s.video_auto_dist = 0; reset_s.video_filter = 0;
    game_settings_apply(&reset_s);
    tr64_game_level_stats_t stv = {0};
    char stats_buf[192] = {0};
    TR3_CHECK(game_stats_view(&stv) == 1 && tr64_ui_stats_text(stats_buf, &stv) == 1 && stats_buf[0] != 0,
              "TR3 Level-End Statistics formats cleanly via shared UI formatter");
    TR3_CHECK(game_pickup_item_view(TR3_OBJ_SMALL_MEDI, 0, ring_buf, 32) > 0,
              "TR3 3D pickup notification emits valid Small Medipack model");
    tr64_ui_unbind();

    printf("PASS live TR3 behavioral suite on %s (sprint/crouch/poison/crystal/flame/swamp/monkey/tiger/piranha/save/ui-ring)\n",
           level_name);
}

int tr64_game_host_scene(int argc, char **argv)
{
    if (argc < 2)
        return 0;

    if (!strcmp(argv[1], "--tr3-test")) {
        const char *level_name = (argc >= 3) ? argv[2] : "jungle";
        run_synthetic_floor_tests();
        tr64_eng_boot();
        tr64_io_init(0);
        code_boot();
        if (tr64_gpu_init) tr64_gpu_init();
        if (tr64_snd_boot) tr64_snd_boot();
        mem_level_begin();
        code_loading_begin();
        tr64_render_scene_set_live(1);
        tr64_scene_load(level_name);
        code_loading_end();
        game_init();
        game_level_start(tr64_level_current(), level_name);
        if (tr64_snd_level_start) tr64_snd_level_start(tr64_level_current());
        run_live_pack_tests(level_name);
        if (tr64_snd_level_stop) tr64_snd_level_stop();
        game_level_stop();
        tr64_render_scene_release();
        eng_level_release();
        sys_memory_report();
        log_flush();
        sys_quit(0);
        return 1;
    }

    if (!strcmp(argv[1], "--tr3-play")) {
        const char *level_name = (argc >= 3) ? argv[2] : "jungle";
        uint32_t ticks = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10) : 10u;
        const char *shot = NULL;
        uint32_t input_mask = 0;
        for (int i = 4; i < argc; ++i) {
            if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
            else if (!strcmp(argv[i], "--input") && i + 1 < argc) input_mask = (uint32_t)strtoul(argv[++i], NULL, 0);
        }

        tr64_eng_boot();
        tr64_io_init(0);
        code_boot();
        if (tr64_gpu_init) tr64_gpu_init();
        if (tr64_snd_boot) tr64_snd_boot();
        mem_level_begin();
        code_loading_begin();
        tr64_render_scene_set_live(1);
        tr64_scene_load(level_name);
        code_loading_end();
        game_init();
        game_level_start(tr64_level_current(), level_name);
        tr64_game_drawable_t *drawables =
            tr64_mem_alloc(TR64_MEM_state, TR64_GAME_DRAW_CHUNK * sizeof(*drawables));
        tr64_render_game_scene_prepare();
        if (tr64_snd_level_start) tr64_snd_level_start(tr64_level_current());

        tr64_game_view_t view;
        for (uint32_t t = 0; t < ticks; ++t) {
            game_tr3_tick(input_mask);
            if (tr64_snd_tick) tr64_snd_tick();
            if (tr64_snd_pump) tr64_snd_pump();
        }
        game_view_camera(&view);
        tr64_render_game_scene_frame(&view, drawables, TR64_GAME_DRAW_CHUNK);
        if (shot && sys_shot(shot) < 0)
            sys_error("tr3-play: shot write failed");

        printf("tr3-play: level=%s ticks=%lu lara=(%ld,%ld,%ld) room=%u hp=%d stamina=%d state=%u hash=%08lx\n",
               level_name, (unsigned long)ticks,
               (long)tr3_gs->lara.x, (long)tr3_gs->lara.y, (long)tr3_gs->lara.z,
               (unsigned)tr3_gs->lara.room, (int)tr3_gs->lara.hp, (int)tr3_gs->lara.stamina,
               (unsigned)tr3_gs->lara.state, (unsigned long)game_state_hash());

        if (tr64_snd_level_stop) tr64_snd_level_stop();
        if (tr64_snd_report) tr64_snd_report();
        game_level_stop();
        tr64_render_scene_release();
        eng_level_release();
        sys_memory_report();
        log_flush();
        sys_quit(0);
        return 1;
    }

    return 0;
}
