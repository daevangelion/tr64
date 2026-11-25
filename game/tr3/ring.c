/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory.h and draw.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III 3D inventory ring, Start Menu, Passport + SRAM save/load,
 * Graphics/Sound/Controls options, Death Passport, Level-End Stats, HUD ammo,
 * and 3D spinning pickup notifications. */
#include <string.h>
#include "game/tr3/tr3.h"
#include "tr64/eng.h"
#include "ui_string_ids.h"

/* Ring animation & framing constants matching OpenLara / TR1 / TR2. */
enum {
    RING_RADIUS_OPEN  = 688,
    RING_HEIGHT_OPEN  = -256,
    RING_HEIGHT_CLOSE = 512,
    RING_OPEN_TICKS   = 10,
    RING_SPIN_TICKS   = 8,
    RING_SEL_TICKS    = 8,
    RING_SEL_DIST     = 256,
    RING_SHOW_DIST    = 256
};

/* Logical ring kinds when browsing in-game. */
enum {
    TR3_RING_KIND_MAIN    = 0,
    TR3_RING_KIND_KEYS    = 1,
    TR3_RING_KIND_OPTIONS = 2
};

/* Special transition targets matching tr64/game.h transition rules. */
enum {
    TR3_LVL_NONE     = -1,
    TR3_LVL_LOAD     = -2,
    TR3_LVL_TITLE    = -3,
    TR3_LVL_NEW_GAME = -4
};

static bool has_model(const tr3_state_t *st, uint32_t type)
{
    return st && st->level && tr3_find_model(st->level, type) != NULL;
}

static uint32_t ring_model_frames(const tr3_state_t *st, uint32_t type)
{
    const tr64_core_models_t *m = tr3_find_model(st->level, type);
    if (!m) return 1u;
    uint16_t aidx = tr64_u16(m->animation);
    uint32_t acount = tr64_level_table(st->level, TR64_CORE_animations)->count;
    if (aidx == 65535u || aidx >= acount) return 1u;
    const tr64_core_animations_t *a = tr64_level_animations(st->level, aidx);
    uint32_t s = tr64_u16(a->frame_start);
    uint32_t e = tr64_u16(a->frame_end);
    return (e >= s) ? (e - s + 1u) : 1u;
}

static int32_t ring_item_label(uint32_t type)
{
    switch (type) {
    case TR3_RING_PASSPORT:  return TR64_STR_GAME;
    case TR3_RING_STOPWATCH: return TR64_STR_STOPWATCH;
    case TR3_RING_DETAIL:    return TR64_STR_DETAIL;
    case TR3_RING_SOUND:     return TR64_STR_SOUND;
    case TR3_RING_CONTROLS:  return TR64_STR_CONTROLS;
    case TR3_RING_PISTOLS:   return TR64_STR_PISTOLS;
    case TR3_RING_SHOTGUN:   return TR64_STR_SHOTGUN;
    case TR3_RING_DEAGLE:    return TR64_STR_TR3_DEAGLE;
    case TR3_RING_UZIS:      return TR64_STR_UZIS;
    case TR3_RING_HARPOON:   return TR64_STR_TR3_HARPOON;
    case TR3_RING_MP5:       return TR64_STR_TR3_MP5;
    case TR3_RING_ROCKET:    return TR64_STR_TR3_ROCKET;
    case TR3_RING_GRENADE:   return TR64_STR_TR3_GRENADE;
    case TR3_RING_SMALL_MEDI:return TR64_STR_MEDI_SMALL;
    case TR3_RING_LARGE_MEDI:return TR64_STR_MEDI_BIG;
    case TR3_RING_FLARES:    return TR64_STR_TR3_FLARES;
    case TR3_RING_CRYSTAL:   return TR64_STR_TR3_CRYSTAL;
    case TR3_RING_KEY4:      return TR64_STR_TR3_INDRA_KEY;
    default:
        if (type >= TR3_RING_KEY_FIRST && type <= TR3_RING_KEY4)
            return TR64_STR_KEY;
        return TR64_STR_TR3_GENERIC;
    }
}

static int32_t ring_item_count(const tr3_state_t *st, uint32_t type)
{
    const tr3_lara_t *l = &st->lara;
    switch (type) {
    case TR3_RING_SHOTGUN:    return l->ammo[TR3_WP_SHOTGUN] / 6;
    case TR3_RING_DEAGLE:     return l->ammo[TR3_WP_DEAGLE];
    case TR3_RING_UZIS:       return l->ammo[TR3_WP_UZIS];
    case TR3_RING_HARPOON:    return l->ammo[TR3_WP_HARPOON];
    case TR3_RING_MP5:        return l->ammo[TR3_WP_MP5];
    case TR3_RING_ROCKET:     return l->ammo[TR3_WP_ROCKET];
    case TR3_RING_GRENADE:    return l->ammo[TR3_WP_GRENADE];
    case TR3_RING_SMALL_MEDI: return l->small_medi;
    case TR3_RING_LARGE_MEDI: return l->large_medi;
    case TR3_RING_FLARES:     return l->flares;
    case TR3_RING_CRYSTAL:    return l->crystals;
    default:                  return -1;
    }
}

static void ring_push(tr3_ring_state_t *r, const tr3_state_t *st, uint32_t type)
{
    if (r->item_count >= TR64_GAME_MENU_ITEMS) return;
    if (!has_model(st, type)) return;
    r->item_types[r->item_count++] = (uint16_t)type;
}

static bool has_any_keys(const tr3_state_t *st)
{
    for (int k = 0; k < 4; ++k) {
        if (st->lara.keys[k] && has_model(st, (uint32_t)(TR3_RING_KEY_FIRST + k)))
            return true;
    }
    return false;
}

static void ring_populate(tr3_state_t *st)
{
    tr3_ring_state_t *r = &st->ring;
    r->item_count = 0;
    if (r->page == TR64_GAME_MENU_PAGE_TITLE) {
        ring_push(r, st, TR3_RING_PASSPORT);
        ring_push(r, st, TR3_RING_DETAIL);
        ring_push(r, st, TR3_RING_SOUND);
        ring_push(r, st, TR3_RING_CONTROLS);
        if (r->item_count == 0) ring_push(r, st, TR3_RING_PISTOLS);
    } else if (r->page == TR64_GAME_MENU_PAGE_DEATH) {
        ring_push(r, st, TR3_RING_PASSPORT);
    } else if (r->page == TR64_GAME_MENU_PAGE_STATS) {
        r->item_count = 0;
    } else if (r->ring_kind == TR3_RING_KIND_OPTIONS) {
        ring_push(r, st, TR3_RING_PASSPORT);
        ring_push(r, st, TR3_RING_STOPWATCH);
        ring_push(r, st, TR3_RING_DETAIL);
        ring_push(r, st, TR3_RING_SOUND);
        ring_push(r, st, TR3_RING_CONTROLS);
    } else if (r->ring_kind == TR3_RING_KIND_KEYS) {
        for (int k = 0; k < 4; ++k) {
            if (st->lara.keys[k])
                ring_push(r, st, (uint32_t)(TR3_RING_KEY_FIRST + k));
        }
        if (r->item_count == 0)
            r->ring_kind = TR3_RING_KIND_MAIN;
    }
    if (r->page != TR64_GAME_MENU_PAGE_TITLE &&
        r->page != TR64_GAME_MENU_PAGE_DEATH &&
        r->page != TR64_GAME_MENU_PAGE_STATS &&
        r->ring_kind == TR3_RING_KIND_MAIN) {
        static const uint16_t wp_models[TR3_WP_COUNT] = {
            0, TR3_RING_PISTOLS, TR3_RING_SHOTGUN, TR3_RING_DEAGLE,
            TR3_RING_UZIS, TR3_RING_MP5, TR3_RING_HARPOON,
            TR3_RING_GRENADE, TR3_RING_ROCKET
        };
        for (int w = 1; w < TR3_WP_COUNT; ++w) {
            if (st->lara.owned_weapons & (1u << w))
                ring_push(r, st, wp_models[w]);
        }
        if (st->lara.small_medi > 0) ring_push(r, st, TR3_RING_SMALL_MEDI);
        if (st->lara.large_medi > 0) ring_push(r, st, TR3_RING_LARGE_MEDI);
        if (st->lara.flares > 0)     ring_push(r, st, TR3_RING_FLARES);
        if (st->lara.crystals > 0)   ring_push(r, st, TR3_RING_CRYSTAL);
        if (r->item_count == 0)      ring_push(r, st, TR3_RING_PASSPORT);
    }

    if (r->item_index >= r->item_count) r->item_index = 0;
    if (r->item_count > 0)
        r->rot = r->rot_target = (r->item_index * 65536) / r->item_count;
    else
        r->rot = r->rot_target = 0;
}

static bool has_usable_save(void)
{
    return eng_game_has_save ? (eng_game_has_save() != 0) : false;
}

static void build_options_list(const tr3_state_t *st, uint32_t item_type,
                               tr64_game_menu_option_t *opts, int32_t *out_count,
                               int32_t *out_w, int32_t *out_h)
{
    int32_t n = 0;
    const tr64_game_settings_t *s = &st->settings;
    if (item_type == TR3_RING_DETAIL) {
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_BAR,    TR64_STR_OPT_DETAIL_GAMMA,     (s->video_gamma * 256) / 31 };
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_BAR,    TR64_STR_OPT_DETAIL_FOG,       (s->video_fog * 256) / 31 };
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_DETAIL_AUTO_DIST, s->video_auto_dist };
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_DETAIL_FILTER,    s->video_filter };
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_DETAIL_FPS,       s->video_fps };
        *out_w = 240;
        *out_h = 4 + n * 18;
    } else if (item_type == TR3_RING_SOUND) {
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_SOUND_SFX,   s->audio_sfx };
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_SOUND_MUSIC, s->audio_music };
        *out_w = 200;
        *out_h = 4 + n * 18;
    } else if (item_type == TR3_RING_CONTROLS) {
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_CONTROLS_VIBRATION, s->controls_vibration };
        opts[n++] = (tr64_game_menu_option_t){ TR64_GAME_MENU_OPTION_SWITCH, TR64_STR_OPT_CONTROLS_SWAP,      s->controls_swap };
        *out_w = 220;
        *out_h = 4 + n * 18;
    } else {
        *out_w = 0;
        *out_h = 0;
    }
    *out_count = n;
}

static int32_t passport_min_page(const tr3_ring_state_t *r)
{
    (void)r;
    if (has_usable_save()) return 0;
    return 1;
}

static int32_t passport_max_page(const tr3_ring_state_t *r)
{
    if (r->page == TR64_GAME_MENU_PAGE_TITLE) return 1;
    return 2;
}

static int32_t passport_page_label(const tr3_ring_state_t *r)
{
    if (r->passport_page == 0) return TR64_STR_LOAD_GAME;
    if (r->passport_page == 1) {
        if (r->page == TR64_GAME_MENU_PAGE_TITLE) return TR64_STR_START_GAME;
        if (r->page == TR64_GAME_MENU_PAGE_DEATH) return TR64_STR_RESTART_LEVEL;
        return TR64_STR_SAVE_GAME;
    }
    return TR64_STR_EXIT_TO_TITLE;
}

static int32_t passport_page_target_frame(const tr3_state_t *st, int32_t page)
{
    uint32_t total = ring_model_frames(st, TR3_RING_PASSPORT);
    if (total <= 1u) return 0;
    int32_t max_f = (int32_t)(total - 1u);
    int32_t f = 14 + page * 5;
    if (f > max_f) f = max_f;
    return f;
}

static int32_t passport_action_hint(const tr3_ring_state_t *r)
{
    if (r->state != TR64_GAME_MENU_STATE_SHOW || r->frame_index != r->frame_target)
        return TR64_GAME_MENU_ACTION_NONE;
    if (r->item_count <= 0 || r->item_types[r->item_index] != TR3_RING_PASSPORT)
        return TR64_GAME_MENU_ACTION_NONE;
    if (r->passport_page == 0 && has_usable_save())
        return TR64_GAME_MENU_ACTION_LOAD_SLOT;
    if (r->passport_page == 1 &&
        r->page != TR64_GAME_MENU_PAGE_TITLE &&
        r->page != TR64_GAME_MENU_PAGE_DEATH)
        return TR64_GAME_MENU_ACTION_SAVE_SLOT;
    return TR64_GAME_MENU_ACTION_NONE;
}

static void ring_start_close(tr3_ring_state_t *r)
{
    r->state = TR64_GAME_MENU_STATE_CLOSING;
    r->phase_ticks = 0;
    r->sel_dist = 0;
    r->frame_index = 0;
    r->frame_target = 0;
}

static void ring_open_selected(tr3_state_t *st)
{
    tr3_ring_state_t *r = &st->ring;
    if (r->item_count <= 0) return;
    uint32_t type = r->item_types[r->item_index];

    /* Immediate-use inventory items (weapons, medipacks, flares). */
    switch (type) {
    case TR3_RING_PISTOLS:  st->lara.weapon = TR3_WP_PISTOLS; st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_SHOTGUN:  st->lara.weapon = TR3_WP_SHOTGUN; st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_DEAGLE:   st->lara.weapon = TR3_WP_DEAGLE;  st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_UZIS:     st->lara.weapon = TR3_WP_UZIS;    st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_HARPOON:  st->lara.weapon = TR3_WP_HARPOON; st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_MP5:      st->lara.weapon = TR3_WP_MP5;     st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_ROCKET:   st->lara.weapon = TR3_WP_ROCKET;  st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_GRENADE:  st->lara.weapon = TR3_WP_GRENADE; st->lara.guns_drawn = 1; ring_start_close(r); return;
    case TR3_RING_SMALL_MEDI:
        tr3_lara_use_medipack(st, false);
        ring_start_close(r);
        return;
    case TR3_RING_LARGE_MEDI:
        tr3_lara_use_medipack(st, true);
        ring_start_close(r);
        return;
    case TR3_RING_FLARES:
        if (st->lara.flares > 0 && !st->lara.flare_lit) {
            --st->lara.flares;
            st->lara.flare_lit = 1;
            st->lara.flare_age = 0;
        }
        ring_start_close(r);
        return;
    default:
        break;
    }

    /* Sub-menu items: Passport, Stopwatch, Graphics, Sound, Controls. */
    if (type == TR3_RING_PASSPORT || type == TR3_RING_STOPWATCH ||
        type == TR3_RING_DETAIL || type == TR3_RING_SOUND || type == TR3_RING_CONTROLS) {
        r->state = TR64_GAME_MENU_STATE_SELECT;
        r->phase_ticks = 0;
        r->option_index = 0;
        r->save_msg_ticks = 0;
        r->save_msg_str = 0;
        if (type == TR3_RING_PASSPORT) {
            int32_t min_p = passport_min_page(r);
            r->passport_page = (r->page == TR64_GAME_MENU_PAGE_TITLE) ? 1 : min_p;
            if (r->passport_page < min_p) r->passport_page = min_p;
            r->frame_index = 0;
            r->frame_target = passport_page_target_frame(st, r->passport_page);
        } else {
            r->frame_index = 0;
            r->frame_target = 0;
        }
    }
}

bool game_tr3_ring_open(int page)
{
    if (!tr3_gs) return false;
    tr3_state_t *st = tr3_gs;
    tr3_ring_state_t *r = &st->ring;
    memset(r, 0, sizeof(*r));
    r->page = (int32_t)page;
    r->pending_level = TR3_LVL_NONE;
    r->pending_transition = TR64_GAME_TRANSITION_CAMPAIGN;
    r->ring_kind = (page == TR64_GAME_MENU_PAGE_OPTIONS) ? TR3_RING_KIND_OPTIONS : TR3_RING_KIND_MAIN;

    if (page == TR64_GAME_MENU_PAGE_STATS) {
        r->state = TR64_GAME_MENU_STATE_SHOW;
        r->height = RING_HEIGHT_OPEN;
        r->radius = RING_RADIUS_OPEN;
        return true;
    }

    ring_populate(st);
    r->state = TR64_GAME_MENU_STATE_OPENING;
    r->height = RING_HEIGHT_CLOSE;
    r->radius = 0;
    r->phase_ticks = 0;
    return true;
}

bool game_tr3_ring_active(void)
{
    return tr3_gs && tr3_gs->ring.state != TR64_GAME_MENU_STATE_NONE;
}

static void save_settings_hook(void)
{
    if (eng_game_save_settings)
        eng_game_save_settings();
}

static void tick_option_adjust(tr3_state_t *st, uint32_t item_type, uint16_t edge, uint16_t held)
{
    tr3_ring_state_t *r = &st->ring;
    tr64_game_menu_option_t opts[TR64_GAME_MENU_OPTIONS];
    int32_t n = 0, w = 0, h = 0;
    build_options_list(st, item_type, opts, &n, &w, &h);
    if (n <= 0) return;

    if (edge & TR64_GAME_FORWARD)
        r->option_index = (r->option_index > 0) ? (r->option_index - 1) : (n - 1);
    else if (edge & TR64_GAME_BACK)
        r->option_index = (r->option_index + 1 < n) ? (r->option_index + 1) : 0;

    int32_t delta = 0;
    if (held & TR64_GAME_LEFT) delta = -1;
    else if (held & TR64_GAME_RIGHT) delta = 1;
    bool toggle = ((edge & (TR64_GAME_LEFT | TR64_GAME_RIGHT | TR64_GAME_ACTION)) != 0);

    bool changed = false;
    tr64_game_settings_t *s = &st->settings;
    if (item_type == TR3_RING_DETAIL) {
        switch (r->option_index) {
        case 0: /* Gamma 0..31 */
            if (delta) {
                int32_t v = s->video_gamma + delta;
                if (v < 0) v = 0;
                if (v > 31) v = 31;
                if (v != s->video_gamma) { s->video_gamma = v; changed = true; }
            }
            break;
        case 1: /* Draw Distance 0..31 */
            if (delta) {
                int32_t v = s->video_fog + delta;
                if (v < 0) v = 0;
                if (v > 31) v = 31;
                if (v != s->video_fog) { s->video_fog = v; changed = true; }
            }
            break;
        case 2: /* Auto Distance */
            if (toggle) { s->video_auto_dist ^= 1; changed = true; }
            break;
        case 3: /* Bilinear Filtering */
            if (toggle) { s->video_filter ^= 1; changed = true; }
            break;
        case 4: /* Show FPS */
            if (toggle) { s->video_fps ^= 1; changed = true; }
            break;
        default: break;
        }
    } else if (item_type == TR3_RING_SOUND) {
        if (r->option_index == 0 && toggle) { s->audio_sfx ^= 1; changed = true; }
        else if (r->option_index == 1 && toggle) { s->audio_music ^= 1; changed = true; }
    } else if (item_type == TR3_RING_CONTROLS) {
        if (r->option_index == 0 && toggle) {
            s->controls_vibration ^= 1;
            changed = true;
            if (eng_game_vibrate) eng_game_vibrate();
        } else if (r->option_index == 1 && toggle) {
            s->controls_swap ^= 1;
            changed = true;
        }
    }

    if (changed) {
        tr3_settings_remember(s);
        save_settings_hook();
    }
}

bool game_tr3_ring_update(uint16_t input)
{
    if (!tr3_gs || tr3_gs->ring.state == TR64_GAME_MENU_STATE_NONE)
        return false;
    tr3_state_t *st = tr3_gs;
    tr3_ring_state_t *r = &st->ring;
    uint16_t edge = (uint16_t)(input & ~r->last_input);
    r->last_input = input;

    if (r->save_msg_ticks > 0) --r->save_msg_ticks;

    /* Level-End Statistics screen: dismiss on Action/Jump/Select. */
    if (r->page == TR64_GAME_MENU_PAGE_STATS) {
        if (edge & (TR64_GAME_ACTION | TR64_GAME_JUMP | TR64_GAME_SELECT)) {
            int32_t next_id = st->script ? (st->script->id + 1) : 1;
            r->state = TR64_GAME_MENU_STATE_NONE;
            memset(&st->stop, 0, sizeof(st->stop));
            st->stop.kind = TR64_GAME_STOP_LEVEL_END;
            st->stop.next_level_id = next_id;
            st->stop.transition = TR64_GAME_TRANSITION_CAMPAIGN;
            return false;
        }
        return true;
    }

    r->rot_item = (r->rot_item + 512) & 65535;

    switch (r->state) {
    case TR64_GAME_MENU_STATE_OPENING:
        ++r->phase_ticks;
        r->height = RING_HEIGHT_CLOSE +
                    ((RING_HEIGHT_OPEN - RING_HEIGHT_CLOSE) * r->phase_ticks) / RING_OPEN_TICKS;
        r->radius = (RING_RADIUS_OPEN * r->phase_ticks) / RING_OPEN_TICKS;
        if (r->phase_ticks >= RING_OPEN_TICKS) {
            r->height = RING_HEIGHT_OPEN;
            r->radius = RING_RADIUS_OPEN;
            r->state = TR64_GAME_MENU_STATE_READY;
            if (r->page == TR64_GAME_MENU_PAGE_DEATH)
                ring_open_selected(st);
        }
        return true;

    case TR64_GAME_MENU_STATE_CLOSING:
        ++r->phase_ticks;
        r->height = RING_HEIGHT_OPEN +
                    ((RING_HEIGHT_CLOSE - RING_HEIGHT_OPEN) * r->phase_ticks) / RING_OPEN_TICKS;
        r->radius = RING_RADIUS_OPEN - (RING_RADIUS_OPEN * r->phase_ticks) / RING_OPEN_TICKS;
        if (r->phase_ticks >= RING_OPEN_TICKS) {
            r->state = TR64_GAME_MENU_STATE_NONE;
            if (r->pending_level != TR3_LVL_NONE) {
                memset(&st->stop, 0, sizeof(st->stop));
                st->stop.kind = TR64_GAME_STOP_LEVEL_END;
                st->stop.next_level_id = r->pending_level;
                st->stop.transition = r->pending_transition;
            }
            return false;
        }
        return true;

    case TR64_GAME_MENU_STATE_SPIN: {
        int32_t diff = (int16_t)(r->rot_target - r->rot);
        int32_t step = (diff >= 0) ? 2048 : -2048;
        if (diff > -2048 && diff < 2048) {
            r->rot = r->rot_target;
            r->state = TR64_GAME_MENU_STATE_READY;
        } else {
            r->rot = (r->rot + step) & 65535;
        }
        return true;
    }

    case TR64_GAME_MENU_STATE_READY: {
        if ((edge & (TR64_GAME_JUMP | TR64_GAME_SELECT)) &&
            r->page != TR64_GAME_MENU_PAGE_TITLE &&
            r->page != TR64_GAME_MENU_PAGE_DEATH) {
            ring_start_close(r);
            return true;
        }
        if ((edge & TR64_GAME_LEFT) && r->item_count > 1) {
            r->item_index = (r->item_index > 0) ? (r->item_index - 1) : (r->item_count - 1);
            r->rot_target = (r->item_index * 65536) / r->item_count;
            r->state = TR64_GAME_MENU_STATE_SPIN;
            return true;
        }
        if ((edge & TR64_GAME_RIGHT) && r->item_count > 1) {
            r->item_index = (r->item_index + 1 < r->item_count) ? (r->item_index + 1) : 0;
            r->rot_target = (r->item_index * 65536) / r->item_count;
            r->state = TR64_GAME_MENU_STATE_SPIN;
            return true;
        }
        if (r->page != TR64_GAME_MENU_PAGE_TITLE && r->page != TR64_GAME_MENU_PAGE_DEATH) {
            if (edge & TR64_GAME_FORWARD) {
                if (r->ring_kind == TR3_RING_KIND_OPTIONS) {
                    r->ring_kind = TR3_RING_KIND_MAIN;
                    r->item_index = 0;
                    ring_populate(st);
                } else if (r->ring_kind == TR3_RING_KIND_MAIN && has_any_keys(st)) {
                    r->ring_kind = TR3_RING_KIND_KEYS;
                    r->item_index = 0;
                    ring_populate(st);
                }
                return true;
            }
            if (edge & TR64_GAME_BACK) {
                if (r->ring_kind == TR3_RING_KIND_KEYS) {
                    r->ring_kind = TR3_RING_KIND_MAIN;
                    r->item_index = 0;
                    ring_populate(st);
                } else if (r->ring_kind == TR3_RING_KIND_MAIN) {
                    r->ring_kind = TR3_RING_KIND_OPTIONS;
                    r->item_index = 0;
                    ring_populate(st);
                }
                return true;
            }
        }
        if (edge & TR64_GAME_ACTION) {
            ring_open_selected(st);
            return true;
        }
        return true;
    }

    case TR64_GAME_MENU_STATE_SELECT:
        ++r->phase_ticks;
        r->sel_dist = (RING_SEL_DIST * r->phase_ticks) / RING_SEL_TICKS;
        if (r->frame_index < r->frame_target)
            r->frame_index += (r->frame_target - r->frame_index + 1) / 2;
        if (r->phase_ticks >= RING_SEL_TICKS) {
            r->sel_dist = RING_SHOW_DIST;
            r->frame_index = r->frame_target;
            r->state = TR64_GAME_MENU_STATE_SHOW;
        }
        return true;

    case TR64_GAME_MENU_STATE_DESELECT:
        ++r->phase_ticks;
        r->sel_dist = RING_SHOW_DIST - (RING_SHOW_DIST * r->phase_ticks) / RING_SEL_TICKS;
        if (r->frame_index > 0) r->frame_index /= 2;
        if (r->phase_ticks >= RING_SEL_TICKS) {
            r->sel_dist = 0;
            r->frame_index = 0;
            r->frame_target = 0;
            r->state = TR64_GAME_MENU_STATE_READY;
        }
        return true;

    case TR64_GAME_MENU_STATE_SHOW: {
        uint32_t type = (r->item_count > 0) ? r->item_types[r->item_index] : 0u;
        if (r->frame_index < r->frame_target) {
            ++r->frame_index;
            return true;
        }
        if (r->frame_index > r->frame_target) {
            --r->frame_index;
            return true;
        }

        if ((edge & (TR64_GAME_JUMP | TR64_GAME_SELECT)) && r->page != TR64_GAME_MENU_PAGE_DEATH) {
            r->state = TR64_GAME_MENU_STATE_DESELECT;
            r->phase_ticks = 0;
            return true;
        }

        if (type == TR3_RING_PASSPORT) {
            int32_t min_p = passport_min_page(r);
            int32_t max_p = passport_max_page(r);
            if ((edge & TR64_GAME_LEFT) && r->passport_page > min_p) {
                --r->passport_page;
                r->frame_target = passport_page_target_frame(st, r->passport_page);
                return true;
            }
            if ((edge & TR64_GAME_RIGHT) && r->passport_page < max_p) {
                ++r->passport_page;
                r->frame_target = passport_page_target_frame(st, r->passport_page);
                return true;
            }
            if (edge & TR64_GAME_ACTION) {
                if (r->passport_page == 0 && has_usable_save()) {
                    r->pending_level = TR3_LVL_LOAD;
                    r->pending_transition = TR64_GAME_TRANSITION_LOAD;
                    ring_start_close(r);
                    return true;
                }
                if (r->passport_page == 1) {
                    if (r->page == TR64_GAME_MENU_PAGE_TITLE) {
                        r->pending_level = TR3_LVL_NEW_GAME;
                        r->pending_transition = TR64_GAME_TRANSITION_NEW_GAME;
                        ring_start_close(r);
                        return true;
                    }
                    if (r->page == TR64_GAME_MENU_PAGE_DEATH) {
                        r->pending_level = game_level_id();
                        r->pending_transition = TR64_GAME_TRANSITION_RESTART;
                        ring_start_close(r);
                        return true;
                    }
                    /* Save Game via engine SRAM medium. */
                    if (eng_game_save) {
                        int res = eng_game_save();
                        if (res == TR64_ENG_SAVE_OK) {
                            r->save_msg_str = TR64_STR_HINT_SAVING_DONE;
                            r->save_msg_ticks = 45;
                            ring_start_close(r);
                        } else {
                            r->save_msg_str = TR64_STR_HINT_SAVING_ERROR;
                            r->save_msg_ticks = 60;
                        }
                    } else {
                        ring_start_close(r);
                    }
                    return true;
                }
                if (r->passport_page == 2) {
                    r->pending_level = TR3_LVL_TITLE;
                    r->pending_transition = TR64_GAME_TRANSITION_TITLE;
                    ring_start_close(r);
                    return true;
                }
            }
        } else if (type == TR3_RING_STOPWATCH) {
            if (edge & TR64_GAME_ACTION) {
                r->state = TR64_GAME_MENU_STATE_DESELECT;
                r->phase_ticks = 0;
            }
        } else {
            tick_option_adjust(st, type, edge, input);
        }
        return true;
    }

    default:
        r->state = TR64_GAME_MENU_STATE_NONE;
        return false;
    }
}

static void menu_camera(int32_t height, int32_t radius, int32_t basis[9], int32_t eye[3])
{
    int32_t dy = -(height + 96);
    int16_t ax = (int16_t)(-tr3_atan2(dy, 768));
    int32_t sx = tr3_sin(ax), cx = tr3_cos(ax);
    /* Looking along -Z (yaw = 32768 -> cy = -16384, sy = 0). */
    basis[0] = -16384; basis[1] = 0;   basis[2] = 0;
    basis[3] = 0;      basis[4] = cx;  basis[5] = -sx;
    basis[6] = 0;      basis[7] = -sx; basis[8] = -cx;
    eye[0] = 0;
    eye[1] = height;
    eye[2] = radius + 768;
}

int game_menu_view(tr64_game_menu_view_t *out)
{
    if (!tr3_gs || !out || tr3_gs->ring.state == TR64_GAME_MENU_STATE_NONE)
        return 0;
    const tr3_state_t *st = tr3_gs;
    const tr3_ring_state_t *r = &st->ring;
    memset(out, 0, sizeof(*out));

    out->state = r->state;
    out->page = r->page;
    out->actionHint = passport_action_hint(r);
    out->height = r->height;
    out->radius = r->radius;
    out->rot = r->rot;
    out->rotItem = r->rot_item;
    out->selDist = r->sel_dist;
    out->passportPage = r->passport_page;
    out->itemsCount = r->item_count;
    out->itemIndex = r->item_index;
    out->frameIndex = r->frame_index;
    out->frameTarget = r->frame_target;
    out->itemVisMask = 0xFFFFFFFFu;
    game_stats_view(&out->stats);

    for (int32_t i = 0; i < r->item_count && i < TR64_GAME_MENU_ITEMS; ++i) {
        out->items[i] = (tr64_game_menu_item_t){
            .slot = i,
            .type = r->item_types[i],
            .animIndex = 0,
            .frameIndex = (i == r->item_index) ? r->frame_index : 0,
            .visibleMask = 0xFFFFFFFFu
        };
    }

    uint32_t cur_type = (r->item_count > 0) ? r->item_types[r->item_index] : 0u;
    if (r->page == TR64_GAME_MENU_PAGE_TITLE) {
        out->titleID = TR64_STR_TR3_GENERIC;
        out->showTitle = 0;
    } else if (r->page == TR64_GAME_MENU_PAGE_DEATH) {
        out->titleID = TR64_STR_GAME;
        out->showTitle = 1;
    } else if (r->page != TR64_GAME_MENU_PAGE_STATS) {
        out->titleID = (r->ring_kind == TR3_RING_KIND_OPTIONS) ? TR64_STR_INV_TITLE_OPTIONS :
                       (r->ring_kind == TR3_RING_KIND_KEYS)    ? TR64_STR_INV_TITLE_KEYS :
                                                                 TR64_STR_INV_TITLE_MAIN;
        out->showTitle = 1;
        out->showUp = (r->state == TR64_GAME_MENU_STATE_READY &&
                       ((r->ring_kind == TR3_RING_KIND_OPTIONS) ||
                        (r->ring_kind == TR3_RING_KIND_MAIN && has_any_keys(st)))) ? 1u : 0u;
        out->showDown = (r->state == TR64_GAME_MENU_STATE_READY &&
                         (r->ring_kind == TR3_RING_KIND_KEYS || r->ring_kind == TR3_RING_KIND_MAIN)) ? 1u : 0u;
    }

    if (r->state == TR64_GAME_MENU_STATE_SHOW && r->frame_index == r->frame_target) {
        if (cur_type == TR3_RING_PASSPORT) {
            out->itemID = passport_page_label(r);
            out->showItem = 1;
            out->showLeft = (r->passport_page > passport_min_page(r)) ? 1u : 0u;
            out->showRight = (r->passport_page < passport_max_page(r)) ? 1u : 0u;
        } else if (cur_type == TR3_RING_DETAIL || cur_type == TR3_RING_SOUND || cur_type == TR3_RING_CONTROLS) {
            out->itemID = ring_item_label(cur_type);
            out->showItem = 1;
            out->optionIndex = r->option_index;
            build_options_list(st, cur_type, out->options, &out->optionsCount,
                               &out->optionsWidth, &out->optionsHeight);
        } else if (cur_type == TR3_RING_STOPWATCH) {
            out->itemID = TR64_STR_STOPWATCH;
            out->showItem = 0;
        }
    } else if (r->page != TR64_GAME_MENU_PAGE_STATS && r->item_count > 0) {
        out->itemID = ring_item_label(cur_type);
        out->showItem = 1;
        int32_t cnt = ring_item_count(st, cur_type);
        if (cnt >= 0 && r->state == TR64_GAME_MENU_STATE_READY) {
            out->showCount = 1;
            out->countValue = (int16_t)(cnt > 9999 ? 9999 : cnt);
        }
    }

    if (r->page != TR64_GAME_MENU_PAGE_STATS && r->page != TR64_GAME_MENU_PAGE_TITLE &&
        (cur_type == TR3_RING_SMALL_MEDI || cur_type == TR3_RING_LARGE_MEDI)) {
        out->showHealth = 1;
        out->healthQ8 = (st->lara.hp * 256) / 1000;
    }

    if (out->page != TR64_GAME_MENU_PAGE_END && out->page != TR64_GAME_MENU_PAGE_STATS)
        menu_camera(out->height, out->radius, out->camera_basis, out->camera_eye);
    return 1;
}

unsigned game_menu_player(void) { return 0; }

static void emit_text(tr64_game_menu_text_t **out, int kind, int align,
                      int x, int y, int32_t val, int suffix, int lh)
{
    *(*out)++ = (tr64_game_menu_text_t){
        val, (int16_t)x, (int16_t)y,
        (uint8_t)kind, (uint8_t)align, (uint8_t)suffix, (uint8_t)lh
    };
}

uint32_t game_menu_text_view(const tr64_game_menu_view_t *v,
                             const uint16_t *widths, uint32_t width_count,
                             tr64_game_menu_text_t *buffer, uint32_t capacity)
{
    if (!v || v->state == TR64_GAME_MENU_STATE_NONE) return 0;
    bool opts_vis = (v->state == TR64_GAME_MENU_STATE_SHOW && v->frameIndex == v->frameTarget);
    bool stats = (v->page == TR64_GAME_MENU_PAGE_STATS) ||
                 (opts_vis && v->itemIndex >= 0 && v->itemIndex < v->itemsCount &&
                  v->items[v->itemIndex].type == TR3_RING_STOPWATCH);
    if (stats) {
        if (!buffer || capacity < 1u) return 1u;
        tr64_game_menu_text_t *out = buffer;
        emit_text(&out, TR64_GAME_TEXT_STATS_BLOCK, TR64_GAME_TEXT_CENTER, 0, 48, 0, 0, 16);
        return 1u;
    }

    bool have_w = widths && v->itemID >= 0 && (uint32_t)v->itemID < width_count;
    bool show_l = v->showLeft && have_w;
    bool show_r = v->showRight && have_w;

    uint32_t required = (uint32_t)!!v->showTitle + (uint32_t)!!v->showItem +
                        (uint32_t)!!v->showCount + 2u * (uint32_t)!!v->showUp +
                        2u * (uint32_t)!!v->showDown + (uint32_t)show_l + (uint32_t)show_r;
    if (opts_vis) {
        for (int32_t i = 0; i < v->optionsCount; ++i) {
            int32_t t = v->options[i].type;
            required += (t == TR64_GAME_MENU_OPTION_SWITCH) ? 2u :
                        (t == TR64_GAME_MENU_OPTION_BUTTON || t == TR64_GAME_MENU_OPTION_BAR ||
                         t == TR64_GAME_MENU_OPTION_TEXT) ? 1u : 0u;
        }
    }
    if (!buffer || capacity < required) return required;

    tr64_game_menu_text_t *out = buffer;
    if (v->showCount)
        emit_text(&out, TR64_GAME_TEXT_NUMBER, TR64_GAME_TEXT_LEFT, 192, 208, v->countValue, 0, 0);
    if (opts_vis) {
        int32_t y = (240 - v->optionsHeight) / 2 - 12 + 2;
        for (int32_t i = 0; i < v->optionsCount; ++i) {
            const tr64_game_menu_option_t *o = v->options + i;
            if (o->type == TR64_GAME_MENU_OPTION_SPACE) {
                y -= 10;
            } else if (o->type == TR64_GAME_MENU_OPTION_BUTTON || o->type == TR64_GAME_MENU_OPTION_TEXT) {
                emit_text(&out, TR64_GAME_TEXT_STRING, TR64_GAME_TEXT_CENTER, 0, y + 16, o->str, 0, 0);
            } else if (o->type == TR64_GAME_MENU_OPTION_BAR || o->type == TR64_GAME_MENU_OPTION_SWITCH) {
                emit_text(&out, TR64_GAME_TEXT_STRING, TR64_GAME_TEXT_RIGHT, -168, y + 16, o->str, 0, 0);
                if (o->type == TR64_GAME_MENU_OPTION_SWITCH)
                    emit_text(&out, TR64_GAME_TEXT_STRING, TR64_GAME_TEXT_CENTER, 44, y + 16,
                              o->value ? TR64_STR_ON : TR64_STR_OFF, 0, 0);
            }
            y += 18;
        }
    }
    if (v->showTitle)
        emit_text(&out, TR64_GAME_TEXT_STRING, TR64_GAME_TEXT_CENTER, 0, 20, v->titleID, 0, 0);
    if (v->showUp) {
        emit_text(&out, TR64_GAME_TEXT_GLYPH, TR64_GAME_TEXT_LEFT, 4, 20, 80, 0, 0);
        emit_text(&out, TR64_GAME_TEXT_GLYPH, TR64_GAME_TEXT_RIGHT, -6, 20, 80, 0, 0);
    }
    if (v->showDown) {
        emit_text(&out, TR64_GAME_TEXT_GLYPH, TR64_GAME_TEXT_LEFT, 4, 235, 81, 0, 0);
        emit_text(&out, TR64_GAME_TEXT_GLYPH, TR64_GAME_TEXT_RIGHT, -6, 235, 81, 0, 0);
    }
    if (v->showItem)
        emit_text(&out, TR64_GAME_TEXT_STRING, TR64_GAME_TEXT_CENTER, 0, 232, v->itemID, 0, 0);
    if (show_r)
        emit_text(&out, TR64_GAME_TEXT_GLYPH, TR64_GAME_TEXT_LEFT, (320 + widths[v->itemID]) / 2 + 4, 232, 109, 0, 0);
    if (show_l)
        emit_text(&out, TR64_GAME_TEXT_GLYPH, TR64_GAME_TEXT_LEFT, (320 - widths[v->itemID]) / 2 - 18, 232, 108, 0, 0);
    return required;
}

uint32_t game_menu_item_view(const tr64_game_menu_view_t *v, uint32_t list_index,
                             tr64_game_drawable_t *buffer, uint32_t capacity)
{
    if (!tr3_gs || !v || v->state == TR64_GAME_MENU_STATE_NONE ||
        v->page == TR64_GAME_MENU_PAGE_END || v->page == TR64_GAME_MENU_PAGE_STATS)
        return 0;
    if (v->itemsCount <= 0 || list_index >= (uint32_t)v->itemsCount)
        return 0;

    const tr64_game_menu_item_t *it = &v->items[list_index];
    int32_t ang = (int32_t)(list_index * 65536u / (uint32_t)v->itemsCount) - v->rot - 16384;
    int32_t sa = tr3_sin(ang), ca = tr3_cos(ang);
    int32_t pos[3] = {
        (v->radius * ca) >> 14,
        0,
        (-v->radius * sa) >> 14
    };

    int32_t yaw = ang + 16384;
    if ((int32_t)list_index == v->itemIndex) {
        pos[2] += v->selDist;
        if (v->state != TR64_GAME_MENU_STATE_SHOW)
            yaw += v->rotItem;
    }

    int32_t sy = tr3_sin(yaw), cy = tr3_cos(yaw);
    int32_t sx = tr3_sin(-3640), cx = tr3_cos(-3640); /* ~ -20 deg tilt toward camera */
    int32_t basis[9] = {
        cy,               0,   sy,
        (sx * sy) >> 14,  cx, -(sx * cy) >> 14,
        -(cx * sy) >> 14, sx,  (cx * cy) >> 14
    };

    return tr3_emit_ring_model(tr3_gs, (uint32_t)it->type, (uint16_t)it->frameIndex,
                               basis, pos, 0x40000000u + list_index, -64, buffer, capacity);
}

static uint32_t world_to_ring_model(int32_t type)
{
    if (type >= TR3_OBJ_PISTOLS_ITEM && type <= TR3_OBJ_FLARE_BOX)
        return (uint32_t)(type + 25);
    if (type == TR3_OBJ_SAVE_CRYSTAL) return TR3_RING_CRYSTAL;
    if (type >= TR3_OBJ_KEY_FIRST && type <= TR3_OBJ_KEY_LAST)
        return (uint32_t)(type + 4);
    return 0u;
}

uint32_t game_pickup_item_view(int32_t type, int16_t angle_y,
                               tr64_game_drawable_t *buffer, uint32_t capacity)
{
    if (!tr3_gs) return 0;
    uint32_t mtype = world_to_ring_model(type);
    if (!mtype || !has_model(tr3_gs, mtype)) return 0;
    int32_t sy = tr3_sin(angle_y), cy = tr3_cos(angle_y);
    int32_t sx = tr3_sin(-2730), cx = tr3_cos(-2730); /* -15 deg tilt */
    int32_t basis[9] = {
        cy,               0,   sy,
        (sx * sy) >> 14,  cx, -(sx * cy) >> 14,
        -(cx * sy) >> 14, sx,  (cx * cy) >> 14
    };
    int32_t pos[3] = { 0, 0, 0 };
    return tr3_emit_ring_model(tr3_gs, mtype, 0u, basis, pos, 0x40000100u, -32, buffer, capacity);
}

uint32_t game_hud_text_view(tr64_game_menu_text_t *buffer, uint32_t capacity)
{
    if (!tr3_gs || tr3_gs->ring.state != TR64_GAME_MENU_STATE_NONE) return 0;
    const tr3_lara_t *l = &tr3_gs->lara;
    if (!l->guns_drawn || l->weapon <= TR3_WP_PISTOLS || l->weapon >= TR3_WP_COUNT)
        return 0;
    if (!buffer || capacity < 1u) return 1u;
    int32_t val = (l->weapon == TR3_WP_SHOTGUN) ? (l->ammo[l->weapon] / 6) : l->ammo[l->weapon];
    if (val < 0) val = 0;
    int32_t y = 4 + 16;
    if (l->water_status == TR3_WATER_UNDER || l->air < 1800) y += 10;
    if (l->stamina < 120 || l->state == TR3_ST_SPRINT) y += 10;
    *buffer = (tr64_game_menu_text_t){
        val, 212, (int16_t)y,
        TR64_GAME_TEXT_NUMBER, TR64_GAME_TEXT_LEFT, 0, 0
    };
    return 1u;
}

uint32_t game_hud_text_view_player(unsigned slot, tr64_game_menu_text_t *buffer, uint32_t capacity)
{
    return slot == 0 ? game_hud_text_view(buffer, capacity) : 0u;
}
