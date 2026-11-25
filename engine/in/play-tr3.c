/* SPDX-License-Identifier: MIT */
/* Interactive 4 MiB N64 play loop for Tomb Raider III (TR64_GAME=3)
 * with shared HUD, 3D Inventory & Options Ring, Graphics Options,
 * Passport + SRAM Save/Load, Death Passport, Level-End Stats, and L+R level select. */
#include <string.h>
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/game.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/pack.h"
#include "tr64/render.h"
#include "tr64/res.h"
#include "tr64/route.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/in/input.h"
#include "engine/in/pace.h"
#include "engine/in/play.h"
#include "engine/save/game.h"
#include "engine/snd/snd.h"
#include "engine/ui/frontend.h"
#include "engine/ui/perf.h"
#include "engine/ui/picker.h"
#include "engine/ui/ui.h"
#include "game/tr3/tr3.h"
#include "build_id.h"

enum { PACK_MAX_LEVELS = 24 };

static char level_name[32];
static char pack_levels[PACK_MAX_LEVELS][32];
static unsigned pack_level_count;
static bool pack_has_title;
static char first_level[32];

static const char *const india_order[] = {
    "jungle", "temple", "quadchas", "tonyboss", "house"
};

static void copy_name(char dst[32], const char *src)
{
    size_t n = strlen(src);
    if (n > 31u) n = 31u;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool pack_has(const char *name)
{
    if (!name || !name[0]) return false;
    if (!strcmp(name, "title")) return pack_has_title;
    for (unsigned i = 0; i < pack_level_count; ++i)
        if (!strcmp(pack_levels[i], name)) return true;
    return false;
}

static void pack_scan_tr3(void)
{
    tr64_pack_t pack;
    uint32_t bytes = tr64_level_pack_bind_streams();
    tr64_load_scope_begin();
    tr64_pack_mount_catalog(&pack, bytes);
    while (!tr64_pack_poll(&pack)) {
        if (tr64_io_frame_bytes() >= TR64_IO_FRAME) tr64_io_frame();
        tr64_io_pump();
    }
    pack_level_count = 0;
    pack_has_title = false;
    first_level[0] = 0;
    char raw_levels[PACK_MAX_LEVELS][32];
    unsigned raw_count = 0;
    for (uint32_t i = 0; i < pack.levels; ++i) {
        const uint8_t *e = pack.directory + i * 48u;
        if (tr64_be32(e + 44u) != 3u) continue;
        char name[32];
        memcpy(name, e, 32u);
        name[31] = 0;
        if (!strcmp(name, "title")) {
            pack_has_title = true;
        } else if (raw_count < PACK_MAX_LEVELS) {
            memcpy(raw_levels[raw_count], name, 32u);
            ++raw_count;
        }
    }
    tr64_pack_unmount(&pack);
    tr64_load_scope_end();

    /* Order gameplay levels in india_order so Next/Previous chords follow the campaign. */
    const char *id_names[PACK_MAX_LEVELS + 1];
    unsigned id_count = 0;
    bool placed[PACK_MAX_LEVELS] = {0};
    for (unsigned c = 0; c < sizeof(india_order) / sizeof(india_order[0]); ++c) {
        for (unsigned i = 0; i < raw_count; ++i) {
            if (!placed[i] && !strcmp(raw_levels[i], india_order[c])) {
                memcpy(pack_levels[pack_level_count], raw_levels[i], 32u);
                id_names[id_count++] = pack_levels[pack_level_count];
                ++pack_level_count;
                placed[i] = true;
                break;
            }
        }
    }
    for (unsigned i = 0; i < raw_count; ++i) {
        if (!placed[i] && pack_level_count < PACK_MAX_LEVELS) {
            memcpy(pack_levels[pack_level_count], raw_levels[i], 32u);
            id_names[id_count++] = pack_levels[pack_level_count];
            ++pack_level_count;
        }
    }
    if (pack_has_title) id_names[id_count++] = "title";
    if (pack_level_count > 0)
        copy_name(first_level, pack_levels[0]);
    if (!first_level[0] && !pack_has_title)
        sys_error("Tomb Raider III: no TR3 level in cart pack");
    tr64_save_identity_bind(id_names, id_count);
    tr64_save_game_require_confirmation();
}

static void set_title_target(char dst[32], bool *start_menu)
{
    if (pack_has_title) {
        copy_name(dst, "title");
        *start_menu = false;
    } else {
        copy_name(dst, first_level);
        *start_menu = true;
    }
}

static bool select_pack_level(unsigned cmd, char dst[32])
{
    if (!pack_level_count) return false;
    unsigned cur = 0;
    for (unsigned i = 0; i < pack_level_count; ++i) {
        if (!strcmp(pack_levels[i], dst)) { cur = i; break; }
    }
    switch (cmd) {
    case TR64_DEMO_RESTART:
        return true;
    case TR64_DEMO_PREVIOUS:
        copy_name(dst, pack_levels[cur ? cur - 1u : pack_level_count - 1u]);
        return true;
    case TR64_DEMO_NEXT:
        copy_name(dst, pack_levels[(cur + 1u) % pack_level_count]);
        return true;
    case TR64_DEMO_FIRST:
        copy_name(dst, first_level);
        return true;
    case TR64_DEMO_GYM:
        if (!pack_has("house")) return false;
        copy_name(dst, "house");
        return true;
    default:
        return false;
    }
}

static void show_notice(const char *title, const char *body, uint16_t *previous)
{
    uint32_t start = sys_time_us();
    tr64_pace_t pace;
    tr64_pace_reset(&pace, start, 1);
    if (!tr64_ui_message(title, body)) return;
    while ((uint32_t)(sys_time_us() - start) < 2000000u) {
        tr64_io_pump();
        tr64_snd_pump();
        uint32_t due = tr64_pace_due(&pace, sys_time_us());
        if (due) tr64_io_frame();
        for (; due; --due) {
            tr64_input_t pad = tr64_input_read();
            *previous = pad.buttons;
        }
    }
}

static uint32_t tr3_actions(tr64_input_t pad)
{
    uint16_t buttons = (uint16_t)pad.buttons;
    tr64_input_t base = pad;
    base.buttons &= (uint16_t)~(TR64_PAD_START | TR64_PAD_L |
                                TR64_PAD_CDOWN | TR64_PAD_CLEFT | TR64_PAD_CRIGHT);
    uint32_t mask = tr64_input_game(base);
    if (buttons & TR64_PAD_L)      mask |= TR3_IN_SPRINT;
    if (buttons & TR64_PAD_CLEFT)  mask |= TR3_IN_CROUCH;
    if (buttons & TR64_PAD_CRIGHT) mask |= TR3_IN_FLARE;
    if (buttons & TR64_PAD_CDOWN)  mask |= TR3_IN_ROLL;
    if ((buttons & TR64_PAD_L) && (buttons & TR64_PAD_Z))
        mask |= TR3_IN_SWAP_GUN;
    return mask;
}

static void draw_frame(tr64_game_drawable_t *drawables, uint32_t ticks, uint32_t dropped)
{
    tr64_game_view_t view;
    if (!strcmp(level_name, "title")) {
        memset(&view, 0, sizeof(view));
        view.camera_room = UINT32_MAX;
    } else {
        game_view_camera(&view);
    }
    tr64_play_message_ready(0);
    tr64_ui_game_frame(&view, drawables, TR64_GAME_DRAW_CHUNK);
    tr64_play_message_ready(1);
    if (ticks && TR64_PERF_ON())
        tr64_perf_end(&(tr64_perf_frame_t){ ticks, dropped, view.camera_room, level_name });
}

static bool run_ring(tr64_game_drawable_t *drawables, uint16_t *previous, int page)
{
    if (!game_tr3_ring_open(page)) return false;
    tr64_perf_restart_window();
    tr64_pace_t menu_pace;
    tr64_pace_reset(&menu_pace, sys_time_us(), 0);
    uint16_t held = (uint16_t)(*previous & (TR64_PAD_START | TR64_PAD_B | TR64_PAD_A));
    draw_frame(drawables, 0, 0);
    bool open = true;
    while (open) {
        tr64_io_pump();
        tr64_snd_pump();
        tr64_ui_game_poll();
        uint32_t due = tr64_pace_due(&menu_pace, sys_time_us());
        if (!due) continue;
        tr64_input_t pad = tr64_input_read();
        *previous = pad.buttons;
        held &= pad.buttons;
        pad.buttons &= (uint16_t)~held;
        do {
            uint16_t menu_actions = tr64_ui_picker_filter(tr64_input_game(pad));
            open = game_tr3_ring_update(menu_actions);
            tr64_snd_tick();
            tr64_snd_pump();
        } while (open && --due);
        if (open) draw_frame(drawables, 0, 0);
    }
    const tr64_game_stop_t *stop = game_stop_info();
    if (stop && stop->kind != TR64_GAME_STOP_NONE) {
        log_printf("play-tr3: ring transition kind=%lu next=%ld tr=%lu\n",
                   (unsigned long)stop->kind, (long)stop->next_level_id,
                   (unsigned long)stop->transition);
        return true;
    }
    return false;
}

static void apply_transition(const tr64_game_stop_t *stop, tr64_game_inventory_carry_t *carry,
                             int *carry_next, int *restore_next, bool *start_menu, uint16_t *previous)
{
    *carry_next = 0;
    *restore_next = 0;
    if (stop->transition == TR64_GAME_TRANSITION_LOAD) {
        tr64_save_info_t info;
        const int confirmed = tr64_save_game_take_load_confirmation();
        const char *target = NULL;
        if (confirmed && tr64_save_slot_status(tr64_save_game_selected(), &info) == TR64_SAVE_OK)
            target = game_level_name(info.level_id);
        if (!target || !pack_has(target)) {
            log_printf("play-tr3: selected SRAM slot refused confirmed=%d\n", confirmed);
            log_flush();
            show_notice("SAVE NOT USABLE", "RETURNING TO TITLE", previous);
            set_title_target(level_name, start_menu);
            return;
        }
        *restore_next = 1;
        *start_menu = false;
        copy_name(level_name, target);
        return;
    }
    if (stop->transition == TR64_GAME_TRANSITION_NEW_GAME) {
        copy_name(level_name, first_level);
        *start_menu = false;
        return;
    }
    if (stop->transition == TR64_GAME_TRANSITION_RESTART) {
        *start_menu = false;
        return;
    }
    if (stop->transition == TR64_GAME_TRANSITION_CAMPAIGN) {
        const char *next = game_level_name(stop->next_level_id);
        if (next && pack_has(next)) {
            if (game_inventory_capture(carry)) {
                carry->transition = TR64_GAME_TRANSITION_CAMPAIGN;
                *carry_next = 1;
            }
            copy_name(level_name, next);
            *start_menu = false;
            return;
        }
    }
    set_title_target(level_name, start_menu);
}

void tr64_play_run(void)
{
    log_printf("play-tr3: build_id=%s mode=live\n", TR64_BUILD_ID);
    log_flush();
    pack_scan_tr3();

    tr64_game_inventory_carry_t carry = {0};
    int carry_next = 0, restore_next = 0;
    bool start_menu = false;
    set_title_target(level_name, &start_menu);
    uint16_t previous = 0;

    for (;;) {
        const int restore = restore_next;
        restore_next = 0;
        const bool is_title = !strcmp(level_name, "title");

        mem_level_begin();
        code_loading_begin();
        code_set_frame_handler(NULL, NULL);
        log_printf("play-tr3: loading=%s start_menu=%u\n", level_name, (unsigned)start_menu);
        log_flush();
        tr64_render_scene_set_live(1);
        tr64_scene_load(level_name);
        game_init();
        if (carry_next) {
            game_level_start_transition(tr64_level_current(), level_name, &carry);
            carry_next = 0;
        } else {
            game_level_start(tr64_level_current(), level_name);
        }
        tr64_game_drawable_t *drawables =
            tr64_mem_alloc(TR64_MEM_state, TR64_GAME_DRAW_CHUNK * sizeof(*drawables));
        tr64_render_game_scene_prepare();
        code_loading_end();
        tr64_snd_level_start(tr64_level_current());
        tr64_snd_pump();
        tr64_ui_game_start(tr64_level_current(), level_name);
        tr64_play_message_ready(1);

        int load_refused = 0;
        if (restore && !tr64_save_game_load()) {
            log_printf("play-tr3: selected SRAM load refused status=%d\n", tr64_save_game_status());
            log_flush();
            show_notice("SAVE NOT USABLE", "RETURNING TO TITLE", &previous);
            set_title_target(level_name, &start_menu);
            load_refused = 1;
        }

        if (!load_refused && (is_title || start_menu)) {
            for (;;) {
                tr64_io_pump();
                tr64_snd_pump();
                if (run_ring(drawables, &previous, TR64_GAME_MENU_PAGE_TITLE)) {
                    const tr64_game_stop_t *stop = game_stop_info();
                    if (stop && stop->kind == TR64_GAME_STOP_LEVEL_END) {
                        apply_transition(stop, &carry, &carry_next, &restore_next, &start_menu, &previous);
                        break;
                    }
                }
            }
        } else if (!load_refused) {
            draw_frame(drawables, 0, 0);
            tr64_mem_log();
            log_printf("play-tr3: ready=%s hz=%u\n", level_name, TR64_TICK_HZ);
            log_flush();

            tr64_pace_t pace;
            tr64_pace_reset(&pace, sys_time_us(), 0);
            uint32_t frame = 0;
            uint32_t death_hold = 0;

            for (;;) {
                tr64_io_pump();
                tr64_snd_pump();
                tr64_ui_game_poll();
                uint32_t due = tr64_pace_due(&pace, sys_time_us());
                if (!due) continue;

                tr64_input_t pad = tr64_input_read();
                const unsigned demo_cmd = (pad.buttons & TR64_PAD_R)
                                            ? tr64_input_demo(pad, previous)
                                            : TR64_DEMO_NONE;
                bool start_edge = (pad.buttons & TR64_PAD_START) && !(previous & TR64_PAD_START);
                bool perf_chord = start_edge && (pad.buttons & TR64_PAD_R) && !(pad.buttons & TR64_PAD_L);
                bool lr_held = (pad.buttons & (TR64_PAD_L | TR64_PAD_R)) == (TR64_PAD_L | TR64_PAD_R);
                previous = (uint16_t)pad.buttons;

                if (demo_cmd) {
                    char from[32];
                    copy_name(from, level_name);
                    if (select_pack_level(demo_cmd, level_name)) {
                        log_printf("play-tr3: select from=%s to=%s command=%u\n", from, level_name, demo_cmd);
                        log_flush();
                        carry_next = 0;
                        start_menu = false;
                        break;
                    }
                }
                if (start_edge && !perf_chord && !lr_held) {
                    if (run_ring(drawables, &previous, TR64_GAME_MENU_PAGE_MAIN)) {
                        const tr64_game_stop_t *stop = game_stop_info();
                        if (stop && stop->kind == TR64_GAME_STOP_LEVEL_END) {
                            apply_transition(stop, &carry, &carry_next, &restore_next, &start_menu, &previous);
                            break;
                        }
                    }
                    draw_frame(drawables, 0, 0);
                    tr64_pace_restart(&pace, sys_time_us());
                    continue;
                }
                if (perf_chord) tr64_perf_toggle();
                if (tr64_perf_state) tr64_perf_begin(due);

                code_frame(frame++);
                tr64_io_frame();
                uint32_t mask = lr_held ? 0u : tr3_actions(pad);
                uint32_t ticks = due;
                bool leave_level = false;

                do {
                    game_tr3_tick(mask);
                    tr64_snd_tick();
                    tr64_snd_pump();
                    const tr64_game_stop_t *stop = game_stop_info();
                    if (stop && stop->kind == TR64_GAME_STOP_DEATH) {
                        if (++death_hold >= 60u) {
                            log_printf("play-tr3: death -> opening Death Passport\n");
                            log_flush();
                            for (;;) {
                                tr64_io_pump();
                                tr64_snd_pump();
                                if (run_ring(drawables, &previous, TR64_GAME_MENU_PAGE_DEATH)) {
                                    stop = game_stop_info();
                                    if (stop && stop->kind == TR64_GAME_STOP_LEVEL_END) {
                                        apply_transition(stop, &carry, &carry_next, &restore_next,
                                                         &start_menu, &previous);
                                        leave_level = true;
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    } else if (stop && stop->kind == TR64_GAME_STOP_LEVEL_END) {
                        log_printf("play-tr3: level end -> showing Level Statistics\n");
                        log_flush();
                        (void)run_ring(drawables, &previous, TR64_GAME_MENU_PAGE_STATS);
                        stop = game_stop_info();
                        if (stop && stop->kind == TR64_GAME_STOP_LEVEL_END)
                            apply_transition(stop, &carry, &carry_next, &restore_next, &start_menu, &previous);
                        else
                            start_menu = true;
                        leave_level = true;
                        break;
                    }
                    tr64_game_view_t v;
                    game_view_camera(&v);
                    tr64_ui_game_tick(&v, drawables, TR64_GAME_DRAW_CHUNK);
                } while (--due);

                if (leave_level) break;
                draw_frame(drawables, ticks, pace.dropped);
            }
        }

        tr64_snd_level_stop();
        tr64_snd_report();
        tr64_ui_game_stop();
        game_level_stop();
        tr64_render_scene_release();
        eng_level_release();
    }
}
