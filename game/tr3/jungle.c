/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III interactive entities, AI waypoints, creatures (Monkey, Tiger, Piranhas),
 * Save Crystal (180), traps, movers, and pickups from docs/specs/wall/tr3-jungle-and-movement.md §3.
 * Linked into paged TLB code unit `tr3_entities` (game/units-tr3.def). */
#include <string.h>
#include "game/tr3/tr3.h"

static inline int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

static inline int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool is_ai_marker(uint16_t type)
{
    return type >= TR3_OBJ_AI_GUARD && type <= TR3_OBJ_AI_X3;
}

static uint8_t ai_marker_bit(uint16_t type)
{
    switch (type) {
    case TR3_OBJ_AI_GUARD:   return TR3_AI_GUARD;
    case TR3_OBJ_AI_AMBUSH:  return TR3_AI_AMBUSH;
    case TR3_OBJ_AI_PATROL1: return TR3_AI_PATROL1;
    case TR3_OBJ_AI_MODIFY:  return TR3_AI_MODIFY;
    case TR3_OBJ_AI_FOLLOW:  return TR3_AI_FOLLOW;
    default:                 return 0;
    }
}

static void item_set_clip(const tr3_state_t *st, tr3_item_t *it, uint16_t clip_rel, uint8_t state)
{
    const tr64_core_models_t *m = tr3_find_model(st->level, it->type);
    uint32_t acount = tr64_level_table(st->level, TR64_CORE_animations)->count;
    if (m && tr64_u16(m->animation) != 65535u) {
        uint32_t idx = (uint32_t)tr64_u16(m->animation) + clip_rel;
        if (idx < acount) {
            it->anim = (uint16_t)idx;
            it->frame = tr64_u16(tr64_level_animations(st->level, idx)->frame_start);
        }
    }
    it->state = state;
    it->goal_state = state;
}

static void item_step_anim(const tr3_state_t *st, tr3_item_t *it)
{
    uint32_t acount = tr64_level_table(st->level, TR64_CORE_animations)->count;
    if (!acount || it->anim >= acount)
        return;
    const tr64_core_animations_t *a = tr64_level_animations(st->level, it->anim);
    uint16_t end = tr64_u16(a->frame_end);
    ++it->frame;
    if (it->frame > end) {
        uint16_t na = tr64_u16(a->next_animation);
        uint16_t nf = tr64_u16(a->next_frame);
        if (na < acount) {
            const tr64_core_animations_t *next = tr64_level_animations(st->level, na);
            uint16_t ns = tr64_u16(next->frame_start);
            uint16_t ne = tr64_u16(next->frame_end);
            it->anim = na;
            it->frame = (nf >= ns && nf <= ne) ? nf : ns;
        } else {
            it->frame = tr64_u16(a->frame_start);
        }
    }
}

/* Move creature horizontally toward (tx, tz) with simple floor validation. */
static bool creature_step_toward(tr3_state_t *st, tr3_item_t *it, int32_t tx, int32_t tz,
                                 int32_t speed, int16_t max_turn, int32_t max_step)
{
    int32_t dx = tx - it->x;
    int32_t dz = tz - it->z;
    int16_t want = tr3_atan2(dx, dz);
    int16_t diff = (int16_t)(want - it->rot_y);
    if (diff > max_turn) diff = max_turn;
    else if (diff < -max_turn) diff = -max_turn;
    it->rot_y = (int16_t)(it->rot_y + diff);

    int32_t nx = it->x + ((tr3_sin(it->rot_y) * speed) >> 14);
    int32_t nz = it->z + ((tr3_cos(it->rot_y) * speed) >> 14);
    tr3_floor_sample_t fs;
    tr3_floor_eval(st, nx, it->y - 128, nz, it->room, false, &fs);
    if (fs.floor_y == TR3_NO_HEIGHT)
        return false;
    if (abs32(fs.floor_y - it->y) > max_step)
        return false;
    it->x = nx;
    it->z = nz;
    it->y = fs.floor_y;
    it->room = fs.room;
    return true;
}

static void init_shoal(tr3_state_t *st, uint16_t item_idx)
{
    if (st->shoal_count >= TR3_MAX_SHOALS)
        return;
    const tr3_item_t *it = &st->items[item_idx];
    tr3_shoal_t *sh = &st->shoals[st->shoal_count++];
    memset(sh, 0, sizeof(*sh));
    sh->item_index = item_idx;
    sh->room = it->room;
    sh->anchor_x = it->x;
    sh->anchor_y = it->y;
    sh->anchor_z = it->z;
    /* Default swim box in quarter-tiles: {10, 3, 22} -> +-2560, 0..768, +-5632 (§3.5). */
    sh->range_x = 10 * TR3_CLICK;
    sh->range_y = 3 * TR3_CLICK;
    sh->range_z = 22 * TR3_CLICK;
    sh->active = it->active;
    for (int f = 0; f < TR3_SHOAL_FISH; ++f) {
        sh->fish_x[f] = sh->anchor_x + (((f * 173) & 511) - 256);
        sh->fish_y[f] = sh->anchor_y + ((f * 29) % (sh->range_y > 0 ? sh->range_y : 256));
        sh->fish_z[f] = sh->anchor_z + (((f * 311) & 511) - 256);
        sh->fish_yaw[f] = (int16_t)(f * 2730);
    }
}

static void ent_init(tr3_state_t *st)
{
    const tr64_level_t *l = st->level;
    const uint32_t icount = tr64_level_table(l, TR64_CORE_items)->count;
    st->item_count = (uint16_t)(icount > TR3_MAX_ITEMS ? TR3_MAX_ITEMS : icount);
    st->shoal_count = 0;
    st->ally_hostile = 0;

    for (uint16_t i = 0; i < st->item_count; ++i) {
        const tr64_core_items_t *ci = tr64_level_items(l, i);
        tr3_item_t *it = &st->items[i];
        memset(it, 0, sizeof(*it));
        it->type = tr64_u16(ci->type);
        it->room = tr64_u16(ci->room);
        it->init_room = it->room;
        it->x = it->init_x = tr64_i32(ci->x);
        it->y = it->init_y = tr64_i32(ci->y);
        it->z = it->init_z = tr64_i32(ci->z);
        it->rot_y = it->init_rot_y = tr64_i16(ci->angle);
        it->shade = tr64_i16(ci->intensity);
        it->flags = tr64_u16(ci->flags);
        it->carried_item = -1;
        it->target_item = -1;

        const tr64_core_models_t *m = tr3_find_model(l, it->type);
        if (m && tr64_u16(m->animation) != 65535u) {
            uint16_t an = tr64_u16(m->animation);
            if (an < tr64_level_table(l, TR64_CORE_animations)->count) {
                it->anim = an;
                it->frame = tr64_u16(tr64_level_animations(l, an)->frame_start);
            }
        }

        if (it->type == TR3_OBJ_LARA || it->type == TR3_OBJ_CAMERA_TARGET ||
            is_ai_marker(it->type) || it->type == TR3_OBJ_DART_EMITTER) {
            it->invisible = 1;
        }
        if ((it->flags & 0x0100u) != 0)
            it->invisible = 1;
        if (((it->flags >> 9) & 0x1Fu) == 0x1Fu)
            it->active = 1;

        switch (it->type) {
        case TR3_OBJ_TIGER:
            it->hp = 24; /* §3.4: 24 HP in TR3 */
            it->state = 1;
            break;
        case TR3_OBJ_MONKEY:
            it->hp = 8;  /* §3.2: 8 HP, starts sitting (state 6, clip 2) */
            item_set_clip(st, it, 2, 6);
            break;
        case TR3_OBJ_SAVE_CRYSTAL:
            it->active = 1;
            it->invisible = 0;
            break;
        case TR3_OBJ_SMALL_FLAME:
        case TR3_OBJ_BIG_FLAME:
        case TR3_OBJ_JET_FLAME:
        case TR3_OBJ_SIDE_FLAME:
            it->invisible = 1;
            break;
        case TR3_OBJ_PIRANHAS:
        case TR3_OBJ_TROPICAL_FISH:
            init_shoal(st, i);
            break;
        case TR3_OBJ_SWITCH_NORMAL:
        case TR3_OBJ_SWITCH_BUTTON:
        case TR3_OBJ_SWITCH_UW:
            it->state = 1; /* switches start up (1); first pull transitions to 0 */
            break;
        default:
            break;
        }
    }

    /* Level-load AI waypoint binding rule (§3.3):
     * Any AI marker (74..78) in the same room at exact same (X, Z) binds its bit
     * and yaw tag to the co-located creature and is removed from active waypoints. */
    for (uint16_t i = 0; i < st->item_count; ++i) {
        tr3_item_t *cr = &st->items[i];
        if (cr->type != TR3_OBJ_MONKEY && cr->type != TR3_OBJ_TIGER)
            continue;
        for (uint16_t j = 0; j < st->item_count; ++j) {
            tr3_item_t *mk = &st->items[j];
            uint8_t bit = ai_marker_bit(mk->type);
            if (!bit)
                continue;
            if (mk->room == cr->room && mk->x == cr->x && mk->z == cr->z) {
                cr->ai_bits |= bit;
                cr->ai_yaw = mk->rot_y;
                mk->room = TR3_NO_ROOM; /* removed from room so only distant markers remain */
                mk->invisible = 1;
            }
        }
        if (cr->type == TR3_OBJ_MONKEY) {
            cr->mesh_swap = (cr->ai_bits == TR3_AI_MODIFY) ? 2u : 1u;
        }
    }
}

static bool is_pickup_type(uint16_t t)
{
    return (t >= TR3_OBJ_PISTOLS_ITEM && t <= TR3_OBJ_FLARE_ITEM) ||
           (t >= TR3_OBJ_KEY_FIRST && t <= TR3_OBJ_KEY_LAST) ||
           (t >= TR3_OBJ_ARTIFACT_FIRST && t <= TR3_OBJ_ARTIFACT_LAST);
}

static void grant_pickup(tr3_state_t *st, tr3_item_t *it)
{
    tr3_lara_t *lara = &st->lara;
    switch (it->type) {
    case TR3_OBJ_SHOTGUN_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_SHOTGUN);
        lara->ammo[TR3_WP_SHOTGUN] += 12;
        break;
    case TR3_OBJ_DEAGLE_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_DEAGLE);
        lara->ammo[TR3_WP_DEAGLE] += 10;
        break;
    case TR3_OBJ_UZI_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_UZIS);
        lara->ammo[TR3_WP_UZIS] += 40;
        break;
    case TR3_OBJ_HARPOON_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_HARPOON);
        lara->ammo[TR3_WP_HARPOON] += 3;
        break;
    case TR3_OBJ_MP5_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_MP5);
        lara->ammo[TR3_WP_MP5] += 60;
        break;
    case TR3_OBJ_ROCKET_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_ROCKET);
        lara->ammo[TR3_WP_ROCKET] += 1;
        break;
    case TR3_OBJ_GRENADE_ITEM:
        lara->owned_weapons |= (uint16_t)(1u << TR3_WP_GRENADE);
        lara->ammo[TR3_WP_GRENADE] += 2;
        break;
    case TR3_OBJ_SHOTGUN_AMMO:  lara->ammo[TR3_WP_SHOTGUN] += 12; break; /* 2 shells = 12 pellets */
    case TR3_OBJ_DEAGLE_AMMO:   lara->ammo[TR3_WP_DEAGLE]  += 10; break;
    case TR3_OBJ_UZI_AMMO:      lara->ammo[TR3_WP_UZIS]    += 40; break; /* TR3 halved to 40 */
    case TR3_OBJ_HARPOON_AMMO:  lara->ammo[TR3_WP_HARPOON] += 3;  break;
    case TR3_OBJ_MP5_AMMO:      lara->ammo[TR3_WP_MP5]     += 60; break; /* TR3 60 rounds */
    case TR3_OBJ_ROCKET_AMMO:   lara->ammo[TR3_WP_ROCKET]  += 1;  break;
    case TR3_OBJ_GRENADE_AMMO:  lara->ammo[TR3_WP_GRENADE] += 2;  break;
    case TR3_OBJ_SMALL_MEDI:    ++lara->small_medi; break;
    case TR3_OBJ_LARGE_MEDI:    ++lara->large_medi; break;
    case TR3_OBJ_FLARE_BOX:     lara->flares += 6; break;
    case TR3_OBJ_FLARE_ITEM:    ++lara->flares; break;
    default:
        if (it->type >= TR3_OBJ_KEY_FIRST && it->type <= TR3_OBJ_KEY_LAST) {
            uint32_t k = it->type - TR3_OBJ_KEY_FIRST;
            if (k < TR3_MAX_KEYS)
                ++lara->keys[k];
        } else if (it->type >= TR3_OBJ_ARTIFACT_FIRST && it->type <= TR3_OBJ_ARTIFACT_LAST) {
            lara->artifacts |= (uint8_t)(1u << (it->type - TR3_OBJ_ARTIFACT_FIRST));
        }
        break;
    }

    it->invisible = 1;
    it->active = 0;
    ++st->stats.pickups;
    tr3_record_pickup(st, it->type, it->x, it->y, it->z);
    tr3_emit_sound(st, 115, &lara->x);

    /* Declarative finish-on-pickup check (e.g., Infada Stone 240 in Caves of Kaliya, §4.2). */
    if (st->script && st->script->finish_pickup_type && it->type == st->script->finish_pickup_type) {
        st->stop.kind = TR64_GAME_STOP_LEVEL_END;
        st->stop.transition = TR64_GAME_TRANSITION_CAMPAIGN;
        st->stop.next_level_id = st->script->id + 1;
        memcpy(st->stop.name, "artifact finish", 16);
    }
}

static void monkey_release_carried(tr3_state_t *st, tr3_item_t *mk)
{
    if (mk->carried_item < 0 || (uint32_t)mk->carried_item >= st->item_count)
        return;
    tr3_item_t *pk = &st->items[mk->carried_item];
    pk->x = mk->x;
    pk->y = mk->y;
    pk->z = mk->z;
    pk->room = mk->room;
    pk->invisible = 0;
    pk->ai_bits |= TR3_AI_GUARD; /* prevents re-theft (§3.2) */
    mk->carried_item = -1;
}

static void tick_monkey(tr3_state_t *st, tr3_item_t *it)
{
    if (it->killed || it->hp <= 0)
        return;

    tr3_lara_t *lara = &st->lara;

    /* 1. Pickup theft & Ambush fleeing (§3.2). */
    if (it->carried_item >= 0) {
        /* Flee to an AI Ambush (75) marker ignoring yaw tag. */
        int16_t amb = -1;
        int32_t best = INT32_MAX;
        for (uint16_t j = 0; j < st->item_count; ++j) {
            const tr3_item_t *m = &st->items[j];
            if (m->type != TR3_OBJ_AI_AMBUSH || m->room == TR3_NO_ROOM)
                continue;
            int32_t d = abs32(m->x - it->x) + abs32(m->z - it->z);
            if (d < best) { best = d; amb = (int16_t)j; }
        }
        if (amb >= 0) {
            const tr3_item_t *m = &st->items[amb];
            if (abs32(m->x - it->x) < 682 && abs32(m->z - it->z) < 682 && abs32(m->y - it->y) < 256) {
                if (it->state != 5) {
                    item_set_clip(st, it, 6, 5);
                    it->timer = 0;
                }
                ++it->timer;
                if (it->timer >= 12) {
                    monkey_release_carried(st, it);
                    it->ai_bits = 0;
                    item_set_clip(st, it, 2, 6);
                }
            } else {
                if (it->state != 4) item_set_clip(st, it, 4, 4);
                creature_step_toward(st, it, m->x, m->z, 64, 2002, 512);
            }
            item_step_anim(st, it);
            return;
        }
    } else if (!(it->ai_bits & (TR3_AI_GUARD | TR3_AI_PATROL1 | TR3_AI_AMBUSH | TR3_AI_FOLLOW))) {
        /* Retail quirk: Monkey prioritizes eligible pickup even after being shot (§3.2). */
        uint16_t want_type = (it->ai_bits == TR3_AI_MODIFY) ? TR3_OBJ_KEY4 : TR3_OBJ_SMALL_MEDI;
        int16_t best_pk = -1;
        int32_t best_d = 8192;
        for (uint16_t j = 0; j < st->item_count; ++j) {
            const tr3_item_t *pk = &st->items[j];
            if (pk->type != want_type || pk->invisible || (pk->ai_bits & TR3_AI_GUARD))
                continue;
            int32_t d = abs32(pk->x - it->x) + abs32(pk->z - it->z);
            if (d < best_d && abs32(pk->y - it->y) < 1024) {
                best_d = d;
                best_pk = (int16_t)j;
            }
        }
        if (best_pk >= 0) {
            tr3_item_t *pk = &st->items[best_pk];
            if (abs32(pk->x - it->x) < 682 && abs32(pk->z - it->z) < 682 && abs32(pk->y - it->y) < 256) {
                if (it->state != 5) {
                    item_set_clip(st, it, 6, 5);
                    it->timer = 0;
                }
                ++it->timer;
                if (it->timer >= 12) {
                    it->carried_item = best_pk;
                    pk->invisible = 1;
                    pk->room = TR3_NO_ROOM;
                    if (!(it->ai_bits & TR3_AI_MODIFY))
                        it->ai_bits |= (TR3_AI_AMBUSH | TR3_AI_MODIFY);
                    else
                        it->ai_bits |= TR3_AI_AMBUSH;
                }
            } else {
                if (it->state != 4) item_set_clip(st, it, 4, 4);
                creature_step_toward(st, it, pk->x, pk->z, 56, 2002, 512);
            }
            item_step_anim(st, it);
            return;
        }
    }

    /* 2. Friendly until ally_hostile flips (§3.2). */
    if (!st->ally_hostile) {
        /* Patrol between AI Patrol 1 (76) and AI Patrol 2 (79) if marked, else sit/idle. */
        if (it->ai_bits & TR3_AI_PATROL1) {
            uint16_t mtype = (it->mood & 1u) ? TR3_OBJ_AI_PATROL2 : TR3_OBJ_AI_PATROL1;
            for (uint16_t j = 0; j < st->item_count; ++j) {
                const tr3_item_t *m = &st->items[j];
                if (m->type == mtype && m->room != TR3_NO_ROOM && m->rot_y == it->ai_yaw) {
                    if (abs32(m->x - it->x) < 768 && abs32(m->z - it->z) < 768) {
                        tr3_test_triggers(st, m->room, m->x, m->y, m->z, true, (int16_t)(it - st->items));
                        it->mood ^= 1u;
                    } else {
                        if (it->state != 2) item_set_clip(st, it, 1, 2);
                        creature_step_toward(st, it, m->x, m->z, 28, 1274, 512);
                    }
                    break;
                }
            }
        }
        item_step_anim(st, it);
        return;
    }

    /* 3. Hostile combat against Lara (§3.2). */
    int32_t dx = lara->x - it->x;
    int32_t dy = lara->y - it->y;
    int32_t dz = lara->z - it->z;
    int64_t d2 = (int64_t)dx * (int64_t)dx + (int64_t)dz * (int64_t)dz;
    int32_t dist = (d2 > (int64_t)UINT32_MAX) ? 65535 : (int32_t)tr3_isqrt((uint32_t)d2);

    if (dist <= 341 && abs32(dy) < 512) {
        uint8_t atk = (lara->y < it->y) ? 13u : 12u; /* High (13) vs Low (12) */
        if (it->state != atk) {
            item_set_clip(st, it, atk == 13u ? 12u : 11u, atk);
            it->hit_latch = 0;
        }
        if (!it->hit_latch) {
            lara->hp = (int16_t)clamp32(lara->hp - 40, 0, 1000);
            it->hit_latch = 1;
            tr3_emit_sound(st, 30, &it->x);
        }
    } else if (dist <= 682 && abs32(dy) < 512) {
        if (it->state != 14) {
            item_set_clip(st, it, 13, 14);
            it->hit_latch = 0;
        }
        if (!it->hit_latch && dist <= 450) {
            lara->hp = (int16_t)clamp32(lara->hp - 50, 0, 1000);
            it->hit_latch = 1;
            tr3_emit_sound(st, 30, &it->x);
        }
        creature_step_toward(st, it, lara->x, lara->z, 48, 2002, 512);
    } else {
        it->hit_latch = 0;
        if (it->state != 4) item_set_clip(st, it, 4, 4);
        creature_step_toward(st, it, lara->x, lara->z, 56, 2002, 512);
    }
    item_step_anim(st, it);
}

static void tick_tiger(tr3_state_t *st, tr3_item_t *it)
{
    if (it->killed || it->hp <= 0)
        return;

    tr3_lara_t *lara = &st->lara;
    int32_t dx = lara->x - it->x;
    int32_t dy = lara->y - it->y;
    int32_t dz = lara->z - it->z;
    int64_t d2 = (int64_t)dx * (int64_t)dx + (int64_t)dz * (int64_t)dz;
    int32_t dist = (d2 > (int64_t)UINT32_MAX) ? 65535 : (int32_t)tr3_isqrt((uint32_t)d2);

    /* Tight ahead bite cone + TR3 widened vertical tolerance 512 (§3.4). */
    int16_t aim = tr3_atan2(dx, dz);
    int16_t diff = (int16_t)(aim - it->rot_y);
    bool in_bite_cone = (abs32(diff) < 5461 && abs32(dy) <= 512);

    if (dist <= 384 && in_bite_cone) {
        if (it->state != 5) {
            item_set_clip(st, it, 6, 5);
            it->hit_latch = 0;
        }
        if (!it->hit_latch) {
            lara->hp = (int16_t)clamp32(lara->hp - 90, 0, 1000); /* TR3: 90 HP bite (§3.4) */
            it->hit_latch = 1;
            tr3_emit_sound(st, 316, &it->x);
        }
    } else if (dist <= 1536 && lara->speed > 0 && in_bite_cone) {
        if (it->state != 6) {
            item_set_clip(st, it, 7, 6);
            it->hit_latch = 0;
        }
        if (!it->hit_latch && dist <= 450) {
            lara->hp = (int16_t)clamp32(lara->hp - 90, 0, 1000);
            it->hit_latch = 1;
        }
        creature_step_toward(st, it, lara->x, lara->z, 72, 1092, 512);
    } else {
        it->hit_latch = 0;
        /* TR3 delta: immediately transition Walk -> Run in attack mood (§3.4). */
        if (it->state != 3) item_set_clip(st, it, 2, 3);
        creature_step_toward(st, it, lara->x, lara->z, 64, 1092, 512);
    }
    item_step_anim(st, it);
}

static void tick_shoals(tr3_state_t *st)
{
    tr3_lara_t *lara = &st->lara;
    for (uint8_t s = 0; s < st->shoal_count; ++s) {
        tr3_shoal_t *sh = &st->shoals[s];
        const tr3_item_t *owner = &st->items[sh->item_index];
        sh->active = owner->active;
        if (!sh->active)
            continue;

        bool is_piranha = (owner->type == TR3_OBJ_PIRANHAS);
        bool attack_lara = false;
        int32_t tx = sh->anchor_x, ty = sh->anchor_y + 256, tz = sh->anchor_z;

        if (is_piranha) {
            /* Carcass (38) distraction in anchor room (§3.5). */
            bool has_carcass = false;
            for (uint16_t j = 0; j < st->item_count; ++j) {
                const tr3_item_t *c = &st->items[j];
                if (c->type == TR3_OBJ_CARCASS && !c->invisible && c->room == sh->room) {
                    tx = c->x; ty = c->y; tz = c->z;
                    has_carcass = true;
                    break;
                }
            }
            if (!has_carcass && lara->room == sh->room && lara->hp > 0) {
                attack_lara = true;
                tx = lara->x;
                ty = lara->y - 256;
                tz = lara->z;
            }
        }

        if (sh->blood_cd > 0)
            --sh->blood_cd;

        for (int f = 0; f < TR3_SHOAL_FISH; ++f) {
            int32_t fx = sh->fish_x[f], fy = sh->fish_y[f], fz = sh->fish_z[f];
            if (attack_lara) {
                int32_t dx = tx - fx, dy = ty - fy, dz = tz - fz;
                int32_t spd = 36 + (f & 7) * 2;
                int16_t ang = tr3_atan2(dx, dz);
                sh->fish_yaw[f] = ang;
                fx += (tr3_sin(ang) * spd) >> 14;
                fz += (tr3_cos(ang) * spd) >> 14;
                fy += clamp32(dy, -24, 24);

                /* Damage rule (§3.5): each fish within horiz circle 256 & vert <= 256 deals 4 HP/tick. */
                int32_t ldx = fx - lara->x;
                int32_t ldy = fy - (lara->y - 256);
                int32_t ldz = fz - lara->z;
                if (abs32(ldx) <= 256 && abs32(ldz) <= 256 &&
                    (ldx * ldx + ldz * ldz) <= 256 * 256 && abs32(ldy) <= 256) {
                    lara->hp = (int16_t)clamp32(lara->hp - 4, 0, 1000);
                    if (sh->blood_cd == 0)
                        sh->blood_cd = 8;
                }
            } else {
                int16_t ang = (int16_t)(sh->fish_yaw[f] + ((f & 1) ? 256 : -256));
                sh->fish_yaw[f] = ang;
                fx += (tr3_sin(ang) * 20) >> 14;
                fz += (tr3_cos(ang) * 20) >> 14;
            }
            sh->fish_x[f] = clamp32(fx, sh->anchor_x - sh->range_x, sh->anchor_x + sh->range_x);
            sh->fish_y[f] = clamp32(fy, sh->anchor_y, sh->anchor_y + sh->range_y);
            sh->fish_z[f] = clamp32(fz, sh->anchor_z - sh->range_z, sh->anchor_z + sh->range_z);
        }
    }
}

static void tick_darts(tr3_state_t *st)
{
    tr3_lara_t *lara = &st->lara;
    for (int i = 0; i < TR3_MAX_DARTS; ++i) {
        tr3_dart_t *d = &st->darts[i];
        if (!d->active)
            continue;
        /* Poison Dart speed = 256 u/tick (§3.7). */
        d->x += (tr3_sin(d->yaw) * 256) >> 14;
        d->z += (tr3_cos(d->yaw) * 256) >> 14;
        --d->life;

        if (abs32(d->x - lara->x) < 180 && abs32(d->z - lara->z) < 180 &&
            d->y >= lara->y - 762 && d->y <= lara->y) {
            /* Hitting Lara deals 25 HP and adds +160 poison (§3.7). */
            lara->hp = (int16_t)clamp32(lara->hp - 25, 0, 1000);
            lara->poison = (int16_t)clamp32(lara->poison + 160, 0, 256);
            d->active = 0;
            continue;
        }

        tr3_floor_sample_t fs;
        tr3_floor_eval(st, d->x, d->y, d->z, d->room, false, &fs);
        d->room = fs.room;
        if (d->life <= 0 || fs.floor_y == TR3_NO_HEIGHT || d->y >= fs.floor_y ||
            (fs.ceiling_y != TR3_NO_HEIGHT && d->y <= fs.ceiling_y)) {
            d->active = 0;
        }
    }
}

static void ent_tick(tr3_state_t *st, uint32_t input)
{
    tr3_lara_t *lara = &st->lara;

    for (uint16_t i = 0; i < st->item_count; ++i) {
        tr3_item_t *it = &st->items[i];
        if (it->invisible && it->type != TR3_OBJ_DART_EMITTER &&
            it->type != TR3_OBJ_SMALL_FLAME && it->type != TR3_OBJ_BIG_FLAME &&
            it->type != TR3_OBJ_JET_FLAME && it->type != TR3_OBJ_SIDE_FLAME)
            continue;

        switch (it->type) {
        case TR3_OBJ_SAVE_CRYSTAL: {
            /* Hover, bob, spin +1024/tick, auto-collect within |dx|<256, |dy|<1024, |dz|<256 (§3.6). */
            it->rot_y = (int16_t)(it->rot_y + 1024);
            int32_t bob = abs32(tr3_cos((int32_t)((st->ticks & 63u) << 10)) >> 6);
            it->y = it->init_y - bob - 64;
            if (lara->hp > 0 &&
                abs32(lara->x - it->x) < 256 &&
                abs32(lara->y - it->y) < 1024 &&
                abs32(lara->z - it->z) < 256) {
                /* Retail PC behaviour (§3.6): +500 HP (cap 1000), cure Poison, destroy crystal. */
                lara->hp = (int16_t)clamp32(lara->hp + 500, 0, 1000);
                lara->poison = 0;
                ++lara->crystals;
                ++st->stats.pickups;
                tr3_record_pickup(st, TR3_OBJ_SAVE_CRYSTAL, it->x, it->y, it->z);
                tr3_emit_sound(st, 116, &it->x);
                it->active = 0;
                it->invisible = 1;
            }
            break;
        }

        case TR3_OBJ_MONKEY:
            if (it->active)
                tick_monkey(st, it);
            break;

        case TR3_OBJ_TIGER:
            if (it->active)
                tick_tiger(st, it);
            break;

        case TR3_OBJ_SPIKES:
            /* Stationary Spikes (§3.7): 15 HP/tick at speed >= 30; impale if falling > 6. */
            if (lara->hp > 0 && abs32(lara->x - it->x) < 350 &&
                abs32(lara->z - it->z) < 350 && abs32(lara->y - it->y) < 384) {
                if (lara->gravity && lara->fall_speed > 6) {
                    lara->hp = 0;
                } else if (abs32(lara->speed) >= 30) {
                    lara->hp = (int16_t)clamp32(lara->hp - 15, 0, 1000);
                }
            }
            break;

        case TR3_OBJ_BOULDER1:
        case TR3_OBJ_BOULDER2:
        case TR3_OBJ_BOULDER3:
        case TR3_OBJ_BOULDER4: {
            if (!it->active)
                break;
            /* TR3 boulder stop probe (§3.7): 320 for type 88, 1088 for 89, 1024 for 112/126. */
            int32_t probe = (it->type == TR3_OBJ_BOULDER1) ? 320 :
                            (it->type == TR3_OBJ_BOULDER4) ? 1088 : 1024;
            int32_t px = it->x + ((tr3_sin(it->rot_y) * probe) >> 14);
            int32_t pz = it->z + ((tr3_cos(it->rot_y) * probe) >> 14);
            tr3_floor_sample_t fs;
            tr3_floor_eval(st, px, it->y - 256, pz, it->room, false, &fs);
            if (fs.floor_y == TR3_NO_HEIGHT || fs.floor_y < it->y - 256) {
                it->speed = 0;
                it->active = 0;
            } else {
                if (it->speed < 128) it->speed += 8;
                it->x += (tr3_sin(it->rot_y) * it->speed) >> 14;
                it->z += (tr3_cos(it->rot_y) * it->speed) >> 14;
                tr3_floor_eval(st, it->x, it->y - 256, it->z, it->room, false, &fs);
                if (fs.floor_y != TR3_NO_HEIGHT) {
                    it->y = fs.floor_y;
                    it->room = fs.room;
                }
                it->rot_x = (int16_t)(it->rot_x + (it->speed << 4));
                tr3_test_triggers(st, it->room, it->x, it->y, it->z, true, (int16_t)i);
                if (lara->hp > 0 && abs32(lara->x - it->x) < 420 &&
                    abs32(lara->z - it->z) < 420 && abs32(lara->y - it->y) < 512) {
                    if (!lara->gravity)
                        lara->hp = 0; /* instant crush when grounded (§3.7) */
                    else
                        lara->hp = (int16_t)clamp32(lara->hp - 100, 0, 1000);
                }
            }
            break;
        }

        case TR3_OBJ_SPIKE_WALL: {
            if (!it->active) {
                it->x = it->init_x; it->y = it->init_y; it->z = it->init_z;
                it->room = it->init_room;
                break;
            }
            int32_t nx = it->x + ((tr3_sin(it->rot_y) * 16) >> 14);
            int32_t nz = it->z + ((tr3_cos(it->rot_y) * 16) >> 14);
            tr3_floor_sample_t fs;
            tr3_floor_eval(st, nx, it->y - 256, nz, it->room, false, &fs);
            if (fs.floor_y != it->y) {
                it->active = 0;
            } else {
                it->x = nx; it->z = nz; it->room = fs.room;
            }
            if (lara->hp > 0 && abs32(lara->x - it->x) < 400 &&
                abs32(lara->z - it->z) < 400 && abs32(lara->y - it->y) < 768) {
                lara->hp = (int16_t)clamp32(lara->hp - 20, 0, 1000);
            }
            break;
        }

        case TR3_OBJ_ZIPLINE: {
            if (!lara->on_zipline || lara->zipline_item != (int16_t)i) {
                if (!it->active && (it->x != it->init_x || it->z != it->init_z)) {
                    it->x = it->init_x; it->y = it->init_y; it->z = it->init_z;
                    it->room = it->init_room;
                    it->speed = 0;
                }
                break;
            }
            /* Zip Line ride (§3.7): +5/tick to 100, descend speed >> 2, probe 1024 ahead. */
            if (it->speed < 100) it->speed += 5;
            if (it->speed > 100) it->speed = 100;
            int32_t vx = (tr3_sin(it->rot_y) * it->speed) >> 14;
            int32_t vz = (tr3_cos(it->rot_y) * it->speed) >> 14;
            int32_t vy = it->speed >> 2;
            it->x += vx;
            it->z += vz;
            it->y += vy;
            int32_t px = it->x + ((tr3_sin(it->rot_y) * 1024) >> 14);
            int32_t pz = it->z + ((tr3_cos(it->rot_y) * 1024) >> 14);
            tr3_floor_sample_t cur_fs, fs;
            tr3_floor_eval(st, it->x, it->y, it->z, it->room, false, &cur_fs);
            it->room = cur_fs.room;
            tr3_floor_eval(st, px, it->y, pz, it->room, false, &fs);
            lara->x = it->x;
            lara->y = it->y;
            lara->z = it->z;
            lara->room = it->room;
            bool stop = !(input & TR3_IN_ACTION) || fs.floor_y == TR3_NO_HEIGHT ||
                        abs32(fs.floor_y - it->y) <= 256 ||
                        (fs.ceiling_y != TR3_NO_HEIGHT && abs32(fs.ceiling_y - it->y) <= 256);
            if (stop) {
                lara->on_zipline = 0;
                lara->zipline_item = -1;
                lara->hands_busy = 0;
                lara->gravity = 1;
                lara->speed = it->speed;
                lara->fall_speed = (int16_t)(it->speed >> 2);
                lara->move_angle = it->rot_y;
                tr3_lara_set_anim(st, 16, TR3_ST_FORWARD_JUMP);
            }
            break;
        }

        case TR3_OBJ_DART_EMITTER:
            if (it->active) {
                if (it->timer <= 0) {
                    it->timer = 24; /* fires every 24 ticks (§3.7) */
                    for (int k = 0; k < TR3_MAX_DARTS; ++k) {
                        if (!st->darts[k].active) {
                            st->darts[k].active = 1;
                            st->darts[k].room = it->room;
                            st->darts[k].yaw = (int16_t)(it->rot_y + 32768);
                            st->darts[k].x = it->x + ((tr3_sin(it->rot_y) * 512) >> 14);
                            st->darts[k].y = it->y - 512;
                            st->darts[k].z = it->z + ((tr3_cos(it->rot_y) * 512) >> 14);
                            st->darts[k].life = 120;
                            break;
                        }
                    }
                } else {
                    --it->timer;
                }
            }
            break;

        case TR3_OBJ_SMALL_FLAME:
            /* Crucial TR3 rule (§3.7): Small Flame (331) is purely decorative and
             * NEVER damages or ignites Lara! */
            break;

        case TR3_OBJ_BIG_FLAME:
        case TR3_OBJ_JET_FLAME:
        case TR3_OBJ_SIDE_FLAME:
            if (it->active && lara->hp > 0) {
                int32_t d = abs32(lara->x - it->x) + abs32(lara->z - it->z);
                if (d < 600 && abs32(lara->y - it->y) < 512) {
                    lara->hp = (int16_t)clamp32(lara->hp - 5, 0, 1000);
                    if (d < 450)
                        lara->burning = 1;
                }
            }
            break;

        default:
            if (it->type >= TR3_OBJ_DOOR_FIRST && it->type <= TR3_OBJ_DOOR_LAST) {
                if (it->timer > 0) {
                    --it->timer;
                    if (it->timer == 0)
                        it->goal_state = 0;
                }
                if (it->state != it->goal_state) {
                    it->state = it->goal_state;
                    item_set_clip(st, it, it->state ? 1u : 0u, it->state);
                }
                item_step_anim(st, it);
            } else if (it->type >= TR3_OBJ_TRAPDOOR_FIRST && it->type <= TR3_OBJ_TRAPDOOR_LAST) {
                if (it->state != it->goal_state) {
                    it->state = it->goal_state;
                    item_set_clip(st, it, it->state ? 1u : 0u, it->state);
                }
                item_step_anim(st, it);
            } else if (it->type == TR3_OBJ_SWITCH_NORMAL || it->type == TR3_OBJ_SWITCH_BUTTON) {
                if (it->timer > 0) {
                    --it->timer;
                    if (it->timer == 0 && !it->one_shot_locked) {
                        it->state = 1;
                        it->active = 0;
                    }
                }
            }
            break;
        }
    }

    tick_shoals(st);
    tick_darts(st);
}

static void ent_interact(tr3_state_t *st)
{
    tr3_lara_t *lara = &st->lara;
    for (uint16_t i = 0; i < st->item_count; ++i) {
        tr3_item_t *it = &st->items[i];
        if (it->invisible)
            continue;
        int32_t dx = lara->x - it->x;
        int32_t dy = lara->y - it->y;
        int32_t dz = lara->z - it->z;

        /* Pickups. */
        if (is_pickup_type(it->type) && abs32(dx) < 384 && abs32(dz) < 384 && abs32(dy) < 384) {
            grant_pickup(st, it);
            return;
        }

        /* Wall Switches (129/128/130) with TR3 one-shot lockout (§1.3 & §3.7). */
        if ((it->type == TR3_OBJ_SWITCH_NORMAL || it->type == TR3_OBJ_SWITCH_BUTTON ||
             it->type == TR3_OBJ_SWITCH_UW) &&
            abs32(dx) < 512 && abs32(dz) < 512 && abs32(dy) < 512) {
            if (it->one_shot_locked)
                return;
            it->state = (it->state == 0) ? 1u : 0u;
            it->active = 1;
            item_set_clip(st, it, it->state == 0 ? 1u : 0u, it->state);
            tr3_emit_sound(st, 55, &it->x);
            tr3_test_triggers(st, it->room, it->x, it->y, it->z, false, -1);
            return;
        }

        /* Keyholes (232..235) matching Key (224..227) (§3.7). */
        if (it->type >= TR3_OBJ_KEYHOLE_FIRST && it->type <= TR3_OBJ_KEYHOLE_LAST &&
            it->state == 0 && abs32(dx) < 512 && abs32(dz) < 512 && abs32(dy) < 512) {
            uint32_t k = it->type - TR3_OBJ_KEYHOLE_FIRST;
            if (k < TR3_MAX_KEYS && lara->keys[k] > 0) {
                --lara->keys[k];
                it->state = 1;
                it->active = 1;
                tr3_emit_sound(st, 55, &it->x);
                tr3_test_triggers(st, it->room, it->x, it->y, it->z, false, -1);
            }
            return;
        }

        /* Pushable Blocks (97..100) (§3.7). */
        if (it->type >= TR3_OBJ_PUSHBLOCK1 && it->type <= TR3_OBJ_PUSHBLOCK4 &&
            abs32(dx) < 768 && abs32(dz) < 768 && abs32(dy) < 256) {
            int16_t quad = (int16_t)(((uint16_t)(lara->rot_y + 8192)) & 0xC000u);
            int32_t nx = it->x + ((tr3_sin(quad) * TR3_TILE) >> 14);
            int32_t nz = it->z + ((tr3_cos(quad) * TR3_TILE) >> 14);
            tr3_floor_sample_t fs;
            tr3_floor_eval(st, nx, it->y - 256, nz, it->room, false, &fs);
            if (fs.floor_y == it->y && (fs.ceiling_y == TR3_NO_HEIGHT || fs.floor_y - fs.ceiling_y >= TR3_TILE)) {
                it->x = nx;
                it->z = nz;
                it->room = fs.room;
                tr3_emit_sound(st, 63, &it->x);
                tr3_test_triggers(st, it->room, it->x, it->y, it->z, true, (int16_t)i);
            }
            return;
        }

        /* Zip Line Handle (127) (§3.7). */
        if (it->type == TR3_OBJ_ZIPLINE && lara->state == TR3_ST_STOP &&
            abs32(dx) < 512 && abs32(dz) < 512 && abs32(dy) < 256) {
            lara->on_zipline = 1;
            lara->zipline_item = (int16_t)i;
            lara->hands_busy = 1;
            lara->rot_y = it->rot_y;
            it->active = 1;
            it->speed = 0;
            tr3_lara_set_anim(st, 214, TR3_ST_ZIPLINE);
            return;
        }
    }
}

static void ent_hit(tr3_state_t *st, int32_t idx, int32_t dmg)
{
    if (idx < 0 || (uint32_t)idx >= st->item_count || dmg <= 0)
        return;
    tr3_item_t *it = &st->items[idx];
    if (it->killed || it->hp <= 0)
        return;

    it->hp = (int16_t)(it->hp - dmg);
    it->hit_status = 1;

    /* Hurting ANY Monkey flips level-wide ally-hostility permanently (§3.2). */
    if (it->type == TR3_OBJ_MONKEY) {
        st->ally_hostile = 1;
        for (uint16_t j = 0; j < st->item_count; ++j) {
            if (st->items[j].type == TR3_OBJ_MONKEY && !st->items[j].killed)
                st->items[j].active = 1;
        }
    }

    if (it->hp <= 0) {
        it->hp = 0;
        it->killed = 1;
        it->active = 0;
        ++st->stats.kills;
        if (it->type == TR3_OBJ_MONKEY) {
            monkey_release_carried(st, it);
            item_set_clip(st, it, 14, 11); /* Monkey Death clip 14, state 11 */
        } else if (it->type == TR3_OBJ_TIGER) {
            item_set_clip(st, it, 11, 7);
        }
    }
}

/* Sole entry point into paged code unit `tr3_entities`. */
uint32_t tr64_unit_tr3_entities(uint32_t arg)
{
    tr3_entity_call_t *c = (tr3_entity_call_t *)(uintptr_t)arg;
    if (!c || !c->st)
        return 0;
    switch (c->op) {
    case TR3_ENT_INIT:     ent_init(c->st); return 1;
    case TR3_ENT_TICK:     ent_tick(c->st, c->input); return 1;
    case TR3_ENT_INTERACT: ent_interact(c->st); return 1;
    case TR3_ENT_HIT:      ent_hit(c->st, c->target_idx, c->damage); return 1;
    default:               return 0;
    }
}
