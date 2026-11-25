/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/room.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III floor data, split-triangle geometry (0x07..0x12), monkey-swing (0x13),
 * and trigger evaluator from docs/specs/wall/tr3-jungle-and-movement.md §1. */
#include <string.h>
#include "game/tr3/tr3.h"

tr3_box_word_t tr3_unpack_box_word(uint16_t raw)
{
    tr3_box_word_t out;
    out.footstep  = (uint8_t)(raw & 0x0Fu);
    out.box_index = (uint16_t)((raw >> 4) & 0x07FFu);
    out.stopper   = (uint8_t)((raw >> 15) & 1u);
    return out;
}

static inline int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

static inline int32_t sign_ext5(uint32_t v)
{
    v &= 0x1Fu;
    return (v & 0x10u) ? (int32_t)v - 32 : (int32_t)v;
}

static inline bool is_ceil_split(uint32_t op)
{
    return op == TR3_FD_SPLIT_C_NWSE || op == TR3_FD_SPLIT_C_NESW ||
           (op >= TR3_FD_SPLIT_CP_SW && op <= TR3_FD_SPLIT_CP_SE);
}

static inline bool is_nwse_split(uint32_t op)
{
    return op == TR3_FD_SPLIT_F_NWSE || op == TR3_FD_SPLIT_C_NWSE ||
           op == TR3_FD_SPLIT_FP_SW  || op == TR3_FD_SPLIT_FP_NE  ||
           op == TR3_FD_SPLIT_CP_SW  || op == TR3_FD_SPLIT_CP_NE;
}

void tr3_eval_split_tri(uint16_t w0, uint16_t w1, int32_t base_y, int32_t dx, int32_t dz,
                        bool chunky_cam, int32_t *out_y, int8_t *out_tx, int8_t *out_tz,
                        uint8_t *out_class, bool *out_portal_open)
{
    const uint32_t op = w0 & 0x1Fu;
    const int32_t h1 = sign_ext5(w0 >> 5) * TR3_CLICK;
    const int32_t h2 = sign_ext5(w0 >> 10) * TR3_CLICK;
    const bool ceil = is_ceil_split(op);
    const bool nwse = is_nwse_split(op);

    int32_t t0 = (int32_t)(w1 & 0x0Fu);
    int32_t t1 = (int32_t)((w1 >> 4) & 0x0Fu);
    int32_t t2 = (int32_t)((w1 >> 8) & 0x0Fu);
    int32_t t3 = (int32_t)((w1 >> 12) & 0x0Fu);
    if (ceil) {
        t0 = -t0;
        t1 = -t1;
        t2 = -t2;
        t3 = -t3;
    }

    dx &= 1023;
    dz &= 1023;

    bool portal_open = false;
    switch (op) {
    case TR3_FD_SPLIT_FP_SW:
    case TR3_FD_SPLIT_CP_SW:
        portal_open = (dx <= 1024 - dz);
        break;
    case TR3_FD_SPLIT_FP_NE:
    case TR3_FD_SPLIT_CP_NE:
        portal_open = (dx > 1024 - dz);
        break;
    case TR3_FD_SPLIT_FP_NW:
    case TR3_FD_SPLIT_CP_NW:
        portal_open = (dx <= dz);
        break;
    case TR3_FD_SPLIT_FP_SE:
    case TR3_FD_SPLIT_CP_SE:
        portal_open = (dx > dz);
        break;
    default:
        break;
    }
    if (out_portal_open)
        *out_portal_open = portal_open;

    int32_t shift;
    int32_t tx, tz;
    if (nwse) {
        if (dx > 1024 - dz) {
            shift = h1;
            tx = ceil ? (t0 - t1) : (t3 - t2);
            tz = t3 - t0;
        } else {
            shift = h2;
            tx = ceil ? (t3 - t2) : (t0 - t1);
            tz = t2 - t1;
        }
    } else {
        if (dx > dz) {
            shift = h1;
            tx = ceil ? (t3 - t2) : (t0 - t1);
            tz = t3 - t0;
        } else {
            shift = h2;
            tx = ceil ? (t0 - t1) : (t3 - t2);
            tz = t2 - t1;
        }
    }

    int32_t y;
    if (chunky_cam) {
        const int32_t a = base_y + h1;
        const int32_t b = base_y + h2;
        y = ceil ? (a > b ? a : b) : (a < b ? a : b);
    } else {
        y = base_y + shift;
        if (!ceil) {
            y += (tz < 0) ? -((tz * dz) >> 2) : ((tz * (1023 - dz)) >> 2);
            y += (tx < 0) ? -((tx * dx) >> 2) : ((tx * (1023 - dx)) >> 2);
        } else {
            y += (tz < 0) ? ((tz * dz) >> 2) : -((tz * (1023 - dz)) >> 2);
            y += (tx < 0) ? ((tx * (1023 - dx)) >> 2) : -((tx * dx) >> 2);
        }
    }

    if (out_y)
        *out_y = y;
    if (out_tx)
        *out_tx = (int8_t)tx;
    if (out_tz)
        *out_tz = (int8_t)tz;
    if (out_class && !ceil) {
        *out_class = (abs32(tx) > 2 || abs32(tz) > 2) ? TR3_HEIGHT_DIAGONAL : TR3_HEIGHT_SPLIT_TRI;
    }
}

uint16_t tr3_physical_room(const tr3_state_t *st, uint16_t room)
{
    if (!st || !st->level)
        return room;
    const uint32_t rcount = tr64_level_table(st->level, TR64_CORE_rooms)->count;
    if (room >= rcount)
        return room;
    if (st->flipped) {
        int16_t alt = tr64_i16(tr64_level_rooms(st->level, room)->alternate_room);
        if (alt >= 0 && (uint32_t)alt < rcount)
            return (uint16_t)alt;
    }
    return room;
}

uint16_t tr3_room_flags(const tr3_state_t *st, uint16_t room)
{
    if (!st || !st->level)
        return 0;
    const uint32_t rcount = tr64_level_table(st->level, TR64_CORE_rooms)->count;
    uint16_t phys = tr3_physical_room(st, room);
    if (phys >= rcount)
        return 0;
    return tr64_u16(tr64_level_rooms(st->level, phys)->flags);
}

static uint16_t fd_word(const tr64_level_t *l, uint32_t idx)
{
    const tr64_level_table_t *t = tr64_level_table(l, TR64_CORE_floor_data);
    if (idx >= t->count)
        return 0x8000u;
    return tr64_u16(*tr64_level_floor_data(l, idx));
}

static uint32_t skip_trigger_stream(const tr64_level_t *l, uint32_t p, uint32_t sub, uint32_t n)
{
    if (p < n)
        ++p; /* timer/flags word */
    if (sub == TR3_TRIG_SWITCH || sub == TR3_TRIG_KEY || sub == TR3_TRIG_PICKUP) {
        if (p >= n)
            return n;
        uint16_t ctrl = fd_word(l, p++);
        if (ctrl & 0x8000u)
            return p;
    }
    while (p < n) {
        uint16_t a = fd_word(l, p++);
        uint32_t ak = (a >> 10) & 0x1Fu;
        if (ak == TR3_ACT_CAMERA || ak == 12u) {
            if (p >= n)
                break;
            uint16_t ex = fd_word(l, p++);
            if (ex & 0x8000u)
                break;
            continue;
        }
        if (a & 0x8000u)
            break;
    }
    return p;
}

static const tr64_core_sectors_t *room_sector_clamped(const tr3_state_t *st, uint16_t room,
                                                      int32_t x, int32_t z)
{
    const tr64_level_t *l = st->level;
    const tr64_core_rooms_t *r = tr64_level_rooms(l, tr3_physical_room(st, room));
    const int32_t nx = (int32_t)tr64_u16(r->x_sectors);
    const int32_t nz = (int32_t)tr64_u16(r->z_sectors);
    int64_t tx64 = ((int64_t)x - (int64_t)tr64_i32(r->x)) >> 10;
    int64_t tz64 = ((int64_t)z - (int64_t)tr64_i32(r->z)) >> 10;
    int32_t tx = (tx64 < -1) ? -1 : (tx64 > nx ? nx : (int32_t)tx64);
    int32_t tz = (tz64 < -1) ? -1 : (tz64 > nz ? nz : (int32_t)tz64);
    if (tz <= 0) {
        tz = 0;
        if (tx < 1) tx = 1;
        else if (tx > nx - 2) tx = nx - 2;
    } else if (tz >= nz - 1) {
        tz = nz - 1;
        if (tx < 1) tx = 1;
        else if (tx > nx - 2) tx = nx - 2;
    } else {
        if (tx < 0) tx = 0;
        else if (tx > nx - 1) tx = nx - 1;
    }
    if (tx < 0) tx = 0;
    if (tx >= nx) tx = nx - 1;
    if (tz < 0) tz = 0;
    if (tz >= nz) tz = nz - 1;
    return tr64_level_sectors(l, tr64_u32(r->sector_start) + (uint32_t)tx * (uint32_t)nz + (uint32_t)tz);
}

static uint16_t sector_portal_room(const tr64_level_t *l, const tr64_core_sectors_t *s)
{
    uint32_t p = tr64_u16(s->floor_index);
    if (!p)
        return TR3_NO_ROOM;
    const uint32_t n = tr64_level_table(l, TR64_CORE_floor_data)->count;
    while (p < n) {
        uint16_t w = fd_word(l, p++);
        uint32_t k = w & 0x1Fu;
        if (k == TR3_FD_PORTAL) {
            return (p < n) ? fd_word(l, p) : TR3_NO_ROOM;
        }
        if (k == TR3_FD_FLOOR_SLOPE || k == TR3_FD_CEIL_SLOPE ||
            (k >= TR3_FD_SPLIT_F_NWSE && k <= TR3_FD_SPLIT_CP_SE)) {
            if (p < n) ++p;
        } else if (k == TR3_FD_TRIGGER) {
            p = skip_trigger_stream(l, p, (w >> 8) & 0x7Fu, n);
        }
        if (w & 0x8000u)
            break;
    }
    return TR3_NO_ROOM;
}

static bool sector_allows_pit(const tr64_level_t *l, const tr64_core_sectors_t *s,
                              int32_t dx, int32_t dz)
{
    uint32_t p = tr64_u16(s->floor_index);
    if (!p)
        return true;
    const uint32_t n = tr64_level_table(l, TR64_CORE_floor_data)->count;
    while (p < n) {
        uint16_t w = fd_word(l, p++);
        uint32_t k = w & 0x1Fu;
        if (k >= TR3_FD_SPLIT_FP_SW && k <= TR3_FD_SPLIT_FP_SE) {
            uint16_t w1 = (p < n) ? fd_word(l, p++) : 0;
            bool open = false;
            tr3_eval_split_tri(w, w1, 0, dx, dz, false, NULL, NULL, NULL, NULL, &open);
            return open;
        }
        if (k == TR3_FD_PORTAL || k == TR3_FD_FLOOR_SLOPE || k == TR3_FD_CEIL_SLOPE ||
            (k >= TR3_FD_SPLIT_F_NWSE && k <= TR3_FD_SPLIT_CP_SE)) {
            if (p < n) ++p;
        } else if (k == TR3_FD_TRIGGER) {
            p = skip_trigger_stream(l, p, (w >> 8) & 0x7Fu, n);
        }
        if (w & 0x8000u)
            break;
    }
    return true;
}

static bool sector_allows_sky(const tr64_level_t *l, const tr64_core_sectors_t *s,
                              int32_t dx, int32_t dz)
{
    uint32_t p = tr64_u16(s->floor_index);
    if (!p)
        return true;
    const uint32_t n = tr64_level_table(l, TR64_CORE_floor_data)->count;
    while (p < n) {
        uint16_t w = fd_word(l, p++);
        uint32_t k = w & 0x1Fu;
        if (k >= TR3_FD_SPLIT_CP_SW && k <= TR3_FD_SPLIT_CP_SE) {
            uint16_t w1 = (p < n) ? fd_word(l, p++) : 0;
            bool open = false;
            tr3_eval_split_tri(w, w1, 0, dx, dz, false, NULL, NULL, NULL, NULL, &open);
            return open;
        }
        if (k == TR3_FD_PORTAL || k == TR3_FD_FLOOR_SLOPE || k == TR3_FD_CEIL_SLOPE ||
            (k >= TR3_FD_SPLIT_F_NWSE && k <= TR3_FD_SPLIT_CP_SE)) {
            if (p < n) ++p;
        } else if (k == TR3_FD_TRIGGER) {
            p = skip_trigger_stream(l, p, (w >> 8) & 0x7Fu, n);
        }
        if (w & 0x8000u)
            break;
    }
    return true;
}

const tr64_core_sectors_t *tr3_get_sector(const tr3_state_t *st, int32_t x, int32_t y, int32_t z,
                                          uint16_t *io_room)
{
    const tr64_level_t *l = st->level;
    const uint32_t rcount = tr64_level_table(l, TR64_CORE_rooms)->count;
    uint16_t room = *io_room;
    if (room >= rcount)
        room = 0;

    const tr64_core_sectors_t *s = NULL;
    for (unsigned step = 0; step < 16u; ++step) {
        s = room_sector_clamped(st, room, x, z);
        uint16_t portal = sector_portal_room(l, s);
        if (portal != TR3_NO_ROOM && portal < rcount && portal != room) {
            room = portal;
            continue;
        }
        break;
    }

    const int32_t dx = x & 1023;
    const int32_t dz = z & 1023;
    for (unsigned step = 0; step < 16u; ++step) {
        if (s->room_above != TR3_NO_ROOM && s->room_above < rcount &&
            y < (int32_t)s->ceiling * TR3_CLICK && sector_allows_sky(l, s, dx, dz)) {
            room = s->room_above;
            s = room_sector_clamped(st, room, x, z);
            continue;
        }
        if (s->room_below != TR3_NO_ROOM && s->room_below < rcount &&
            y >= (int32_t)s->floor * TR3_CLICK && sector_allows_pit(l, s, dx, dz)) {
            room = s->room_below;
            s = room_sector_clamped(st, room, x, z);
            continue;
        }
        break;
    }

    *io_room = room;
    return s;
}

void tr3_floor_eval(const tr3_state_t *st, int32_t x, int32_t y, int32_t z,
                    uint16_t room, bool chunky_cam, tr3_floor_sample_t *out)
{
    memset(out, 0, sizeof(*out));
    out->floor_y = TR3_NO_HEIGHT;
    out->ceiling_y = TR3_NO_HEIGHT;
    out->water_y = TR3_NO_HEIGHT;
    out->room = room;

    if (!st || !st->level)
        return;

    const tr64_level_t *l = st->level;
    const uint32_t rcount = tr64_level_table(l, TR64_CORE_rooms)->count;
    const uint32_t fcount = tr64_level_table(l, TR64_CORE_floor_data)->count;
    const int32_t dx = x & 1023;
    const int32_t dz = z & 1023;

    uint16_t cur_room = room;
    const tr64_core_sectors_t *s = tr3_get_sector(st, x, y, z, &cur_room);
    out->room = cur_room;

    uint16_t rflags = tr3_room_flags(st, cur_room);
    out->in_water = (rflags & TR3_ROOM_WATER) ? 1u : 0u;
    out->in_swamp = (rflags & TR3_ROOM_SWAMP) ? 1u : 0u;

    /* Find liquid surface (water or swamp) by walking vertically (§1.4 & §2.5). */
    if (out->in_water || out->in_swamp) {
        uint16_t wr = cur_room;
        const tr64_core_sectors_t *ws = s;
        for (unsigned step = 0; step < 16u; ++step) {
            if (ws->room_above == TR3_NO_ROOM || ws->room_above >= rcount ||
                !sector_allows_sky(l, ws, dx, dz))
                break;
            uint16_t af = tr3_room_flags(st, ws->room_above);
            if (!(af & (TR3_ROOM_WATER | TR3_ROOM_SWAMP))) {
                out->water_y = (int32_t)ws->ceiling * TR3_CLICK;
                break;
            }
            wr = ws->room_above;
            ws = room_sector_clamped(st, wr, x, z);
        }
    } else {
        uint16_t wr = cur_room;
        const tr64_core_sectors_t *ws = s;
        for (unsigned step = 0; step < 16u; ++step) {
            if (ws->room_below == TR3_NO_ROOM || ws->room_below >= rcount ||
                !sector_allows_pit(l, ws, dx, dz))
                break;
            uint16_t bf = tr3_room_flags(st, ws->room_below);
            if (bf & (TR3_ROOM_WATER | TR3_ROOM_SWAMP)) {
                out->water_y = (int32_t)ws->floor * TR3_CLICK;
                break;
            }
            wr = ws->room_below;
            ws = room_sector_clamped(st, wr, x, z);
        }
    }

    /* Descend to bottom pit sector for floor height, triggers, death, climb, monkey (§1.2, §1.3). */
    uint16_t froom = cur_room;
    const tr64_core_sectors_t *fs = s;
    for (unsigned step = 0; step < 16u; ++step) {
        if (fs->room_below != TR3_NO_ROOM && fs->room_below < rcount &&
            sector_allows_pit(l, fs, dx, dz)) {
            froom = fs->room_below;
            fs = room_sector_clamped(st, froom, x, z);
            continue;
        }
        break;
    }

    int32_t floor_y = (fs->floor == -127) ? TR3_NO_HEIGHT : (int32_t)fs->floor * TR3_CLICK;
    uint8_t htype = TR3_HEIGHT_WALL;
    int8_t tx = 0, tz = 0;

    uint32_t p = tr64_u16(fs->floor_index);
    if (p && p < fcount) {
        while (p < fcount) {
            uint16_t w = fd_word(l, p++);
            uint32_t k = w & 0x1Fu;
            switch (k) {
            case TR3_FD_PORTAL:
                if (p < fcount) ++p;
                break;
            case TR3_FD_FLOOR_SLOPE: {
                uint16_t tilt = (p < fcount) ? fd_word(l, p++) : 0;
                tx = (int8_t)(tilt & 0xFFu);
                tz = (int8_t)((tilt >> 8) & 0xFFu);
                if (floor_y != TR3_NO_HEIGHT) {
                    htype = (abs32(tx) > 2 || abs32(tz) > 2) ? TR3_HEIGHT_BIG_SLOPE : TR3_HEIGHT_SMALL_SLOPE;
                    floor_y += (tz < 0) ? -(((int32_t)tz * dz) >> 2) : (((int32_t)tz * (1023 - dz)) >> 2);
                    floor_y += (tx < 0) ? -(((int32_t)tx * dx) >> 2) : (((int32_t)tx * (1023 - dx)) >> 2);
                }
                break;
            }
            case TR3_FD_CEIL_SLOPE:
                if (p < fcount) ++p;
                break;
            case TR3_FD_TRIGGER:
                out->trigger_index = (uint16_t)(p - 1u);
                p = skip_trigger_stream(l, p, (w >> 8) & 0x7Fu, fcount);
                break;
            case TR3_FD_DEATH:
                out->death_floor = 1u;
                break;
            case TR3_FD_CLIMB:
                out->climb_flags |= (uint8_t)((w >> 8) & 0x0Fu);
                break;
            case TR3_FD_SPLIT_F_NWSE:
            case TR3_FD_SPLIT_F_NESW:
            case TR3_FD_SPLIT_FP_SW:
            case TR3_FD_SPLIT_FP_NE:
            case TR3_FD_SPLIT_FP_NW:
            case TR3_FD_SPLIT_FP_SE: {
                uint16_t w1 = (p < fcount) ? fd_word(l, p++) : 0;
                if (floor_y != TR3_NO_HEIGHT) {
                    tr3_eval_split_tri(w, w1, floor_y, dx, dz, chunky_cam,
                                       &floor_y, &tx, &tz, &htype, NULL);
                }
                break;
            }
            case TR3_FD_SPLIT_C_NWSE:
            case TR3_FD_SPLIT_C_NESW:
            case TR3_FD_SPLIT_CP_SW:
            case TR3_FD_SPLIT_CP_NE:
            case TR3_FD_SPLIT_CP_NW:
            case TR3_FD_SPLIT_CP_SE:
                if (p < fcount) ++p;
                break;
            case TR3_FD_MONKEY:
                out->climb_flags |= TR3_CLIMB_MONKEY;
                break;
            case TR3_FD_MINECART_LEFT:
                out->minecart_flags |= 1u;
                break;
            case TR3_FD_MINECART_RIGHT:
                out->minecart_flags |= 2u;
                break;
            default:
                break;
            }
            if (w & 0x8000u)
                break;
        }
    }

    /* Ascend to top sky sector for ceiling height (§1.2). */
    uint16_t croom = cur_room;
    const tr64_core_sectors_t *cs = s;
    for (unsigned step = 0; step < 16u; ++step) {
        if (cs->room_above != TR3_NO_ROOM && cs->room_above < rcount &&
            sector_allows_sky(l, cs, dx, dz)) {
            croom = cs->room_above;
            cs = room_sector_clamped(st, croom, x, z);
            continue;
        }
        break;
    }

    int32_t ceil_y = (cs->ceiling == -127) ? TR3_NO_HEIGHT : (int32_t)cs->ceiling * TR3_CLICK;
    p = tr64_u16(cs->floor_index);
    if (p && p < fcount && ceil_y != TR3_NO_HEIGHT) {
        while (p < fcount) {
            uint16_t w = fd_word(l, p++);
            uint32_t k = w & 0x1Fu;
            if (k == TR3_FD_CEIL_SLOPE) {
                uint16_t tilt = (p < fcount) ? fd_word(l, p++) : 0;
                int8_t ctx = (int8_t)(tilt & 0xFFu);
                int8_t ctz = (int8_t)((tilt >> 8) & 0xFFu);
                ceil_y += (ctz < 0) ? (((int32_t)ctz * dz) >> 2) : -(((int32_t)ctz * (1023 - dz)) >> 2);
                ceil_y += (ctx < 0) ? (((int32_t)ctx * (1023 - dx)) >> 2) : -(((int32_t)ctx * dx) >> 2);
            } else if (k == TR3_FD_SPLIT_C_NWSE || k == TR3_FD_SPLIT_C_NESW ||
                       (k >= TR3_FD_SPLIT_CP_SW && k <= TR3_FD_SPLIT_CP_SE)) {
                uint16_t w1 = (p < fcount) ? fd_word(l, p++) : 0;
                tr3_eval_split_tri(w, w1, ceil_y, dx, dz, chunky_cam,
                                   &ceil_y, NULL, NULL, NULL, NULL);
            } else if (k == TR3_FD_PORTAL || k == TR3_FD_FLOOR_SLOPE ||
                       (k >= TR3_FD_SPLIT_F_NWSE && k <= TR3_FD_SPLIT_FP_SE)) {
                if (p < fcount) ++p;
            } else if (k == TR3_FD_TRIGGER) {
                p = skip_trigger_stream(l, p, (w >> 8) & 0x7Fu, fcount);
            }
            if (w & 0x8000u)
                break;
        }
    }

    /* Walkable objects stacked on floor override floor height & reset surface class to 0 (§1.2). */
    for (uint16_t i = 0; i < st->item_count; ++i) {
        const tr3_item_t *it = &st->items[i];
        if (it->invisible)
            continue;
        if (it->type >= TR3_OBJ_DOOR_FIRST && it->type <= TR3_OBJ_DOOR_LAST && it->state == 0) {
            if (abs32(x - it->x) < 420 && abs32(z - it->z) < 420 &&
                y <= it->y + 128 && y >= it->y - 1280) {
                floor_y = TR3_NO_HEIGHT;
                htype = TR3_HEIGHT_WALL;
                tx = tz = 0;
            }
        } else if (it->type >= TR3_OBJ_PUSHBLOCK1 && it->type <= TR3_OBJ_PUSHBLOCK4) {
            if (abs32(x - it->x) < 512 && abs32(z - it->z) < 512 &&
                y <= it->y - 512 && it->y - 1024 < floor_y) {
                floor_y = it->y - 1024;
                htype = TR3_HEIGHT_WALL;
                tx = tz = 0;
            }
        } else if (it->type >= TR3_OBJ_TRAPDOOR_FIRST && it->type <= TR3_OBJ_TRAPDOOR_LAST) {
            if (it->state == 0 && abs32(x - it->x) < 512 && abs32(z - it->z) < 512) {
                if (y <= it->y && it->y < floor_y) {
                    floor_y = it->y;
                    htype = TR3_HEIGHT_WALL;
                    tx = tz = 0;
                }
            }
        }
    }

    out->floor_y = floor_y;
    out->ceiling_y = ceil_y;
    out->tilt_x = tx;
    out->tilt_z = tz;
    out->height_type = htype;
}

static bool is_monkey_state(uint8_t s)
{
    return s == TR3_ST_MONKEY_IDLE || s == TR3_ST_MONKEY_FORWARD ||
           s == TR3_ST_MONKEY_LEFT || s == TR3_ST_MONKEY_RIGHT ||
           s == TR3_ST_MONKEY_ROLL || s == TR3_ST_MONKEY_TURN_L ||
           s == TR3_ST_MONKEY_TURN_R;
}

static void activate_object(tr3_state_t *st, uint32_t idx, uint16_t mask, int16_t timer,
                            bool anti, bool toggle)
{
    if (idx >= st->item_count)
        return;
    tr3_item_t *it = &st->items[idx];
    if (anti) {
        it->flags &= (uint16_t)~(mask << 9);
        if (it->type >= TR3_OBJ_DOOR_FIRST && it->type <= TR3_OBJ_DOOR_LAST) {
            it->goal_state = 0;
        } else if (it->type == TR3_OBJ_PIRANHAS) {
            it->active = 0;
        }
        return;
    }
    if (toggle)
        it->flags ^= (uint16_t)(mask << 9);
    else
        it->flags |= (uint16_t)(mask << 9);
    if (timer > 0)
        it->timer = (int16_t)(timer * TR64_TICK_HZ);
    uint16_t code = (uint16_t)((it->flags >> 9) & 0x1Fu);
    if (code == 0x1Fu) {
        it->active = 1;
        if (it->type >= TR3_OBJ_DOOR_FIRST && it->type <= TR3_OBJ_DOOR_LAST) {
            if (it->goal_state != 1) {
                it->goal_state = 1;
                tr3_emit_sound(st, 35, &it->x);
            }
        } else if (it->type >= TR3_OBJ_TRAPDOOR_FIRST && it->type <= TR3_OBJ_TRAPDOOR_LAST) {
            it->goal_state = 1;
        }
    } else if (toggle) {
        it->active = 0;
        if (it->type >= TR3_OBJ_DOOR_FIRST && it->type <= TR3_OBJ_DOOR_LAST)
            it->goal_state = 0;
        else if (it->type >= TR3_OBJ_TRAPDOOR_FIRST && it->type <= TR3_OBJ_TRAPDOOR_LAST)
            it->goal_state = 0;
    }
}

void tr3_test_triggers(tr3_state_t *st, uint16_t room, int32_t x, int32_t y, int32_t z,
                       bool heavy, int16_t heavy_item_idx)
{
    if (!st || !st->level)
        return;

    tr3_floor_sample_t sample;
    tr3_floor_eval(st, x, y, z, room, false, &sample);
    if (!sample.trigger_index)
        return;

    const tr64_level_t *l = st->level;
    const uint32_t fcount = tr64_level_table(l, TR64_CORE_floor_data)->count;
    uint32_t p = sample.trigger_index;
    if (p >= fcount)
        return;

    uint16_t w = fd_word(l, p++);
    if ((w & 0x1Fu) != TR3_FD_TRIGGER || p >= fcount)
        return;

    const uint32_t sub = (w >> 8) & 0x7Fu;
    const uint16_t tflags = fd_word(l, p++);
    const int16_t timer = (int16_t)(tflags & 0xFFu);
    const bool one_shot = (tflags & 0x0100u) != 0;
    const uint16_t mask = (uint16_t)((tflags >> 9) & 0x1Fu);

    bool trigger_active = false;
    bool anti = false;
    bool toggle_obj = false;
    uint16_t ctrl_idx = UINT16_MAX;

    if (sub == TR3_TRIG_SWITCH || sub == TR3_TRIG_KEY || sub == TR3_TRIG_PICKUP) {
        if (p >= fcount)
            return;
        uint16_t cw = fd_word(l, p++);
        ctrl_idx = cw & 0x3FFu;
        if (cw & 0x8000u)
            return;
    }

    if (heavy) {
        if (sub == TR3_TRIG_HEAVY) {
            trigger_active = true;
        } else if (sub == TR3_TRIG_HEAVY_SWITCH) {
            if (heavy_item_idx >= 0 && (uint32_t)heavy_item_idx < st->item_count) {
                uint16_t imask = (uint16_t)((st->items[heavy_item_idx].flags >> 9) & 0x1Fu);
                if (imask != 0 && imask == mask) {
                    trigger_active = true;
                    toggle_obj = true;
                }
            }
        } else if (sub == TR3_TRIG_HEAVY_ANTITRIG) {
            trigger_active = true;
            anti = true;
        }
    } else {
        switch (sub) {
        case TR3_TRIG_TRIGGER:
            trigger_active = true;
            break;
        case TR3_TRIG_PAD:
            trigger_active = (y >= sample.floor_y - 16);
            break;
        case TR3_TRIG_ANTIPAD:
            trigger_active = (y >= sample.floor_y - 16);
            anti = true;
            break;
        case TR3_TRIG_ANTITRIGGER:
            trigger_active = true;
            anti = true;
            break;
        case TR3_TRIG_COMBAT:
            trigger_active = (st->lara.guns_drawn != 0);
            break;
        case TR3_TRIG_MONKEY:
            trigger_active = is_monkey_state(st->lara.state);
            break;
        case TR3_TRIG_SWITCH:
            if (ctrl_idx < st->item_count) {
                tr3_item_t *sw = &st->items[ctrl_idx];
                if (sw->active) {
                    trigger_active = (sw->state == 0);
                    if (one_shot && sw->state == 0)
                        sw->one_shot_locked = 1;
                }
            }
            break;
        case TR3_TRIG_KEY:
            if (ctrl_idx < st->item_count && st->items[ctrl_idx].state == 1)
                trigger_active = true;
            break;
        case TR3_TRIG_PICKUP:
            if (ctrl_idx < st->item_count && st->items[ctrl_idx].invisible)
                trigger_active = true;
            break;
        case TR3_TRIG_DUMMY:
        default:
            break;
        }
    }

    if (!trigger_active)
        return;

    while (p < fcount) {
        uint16_t a = fd_word(l, p++);
        uint32_t ak = (a >> 10) & 0x1Fu;
        uint32_t arg = a & 0x3FFu;
        uint16_t cam_extra = 0;
        if (ak == TR3_ACT_CAMERA || ak == 12u) {
            if (p < fcount)
                cam_extra = fd_word(l, p++);
        }

        switch (ak) {
        case TR3_ACT_OBJECT:
            activate_object(st, arg, mask, timer, anti, toggle_obj);
            break;
        case TR3_ACT_CAMERA:
            if (!anti && arg < tr64_level_table(l, TR64_CORE_cameras)->count) {
                st->camera.fixed_cam = (int16_t)arg;
                st->camera.fixed_timer = (int16_t)((cam_extra & 0xFFu) * TR64_TICK_HZ);
                if (st->camera.fixed_timer <= 0)
                    st->camera.fixed_timer = 30;
            }
            break;
        case TR3_ACT_SINK:
            /* TR3 smooth water current (§1.3 & §2.6): record 1 + sink_index directly. */
            if (!heavy && arg < tr64_level_table(l, TR64_CORE_cameras)->count)
                st->lara.current_sink = (int16_t)(1u + arg);
            break;
        case TR3_ACT_FLIPMAP:
            if (arg < 10u) {
                uint8_t prev = st->flip_flags[arg] & 0x1Fu;
                st->flip_flags[arg] |= (uint8_t)mask;
                if (prev != 0x1Fu && (st->flip_flags[arg] & 0x1Fu) == 0x1Fu)
                    st->flipped ^= 1u;
            }
            break;
        case TR3_ACT_FLIP_ON:
            if (arg < 10u && (st->flip_flags[arg] & 0x1Fu) == 0x1Fu)
                st->flipped = 1u;
            break;
        case TR3_ACT_FLIP_OFF:
            if (arg < 10u && (st->flip_flags[arg] & 0x1Fu) == 0x1Fu)
                st->flipped = 0u;
            break;
        case TR3_ACT_TARGET:
            if (arg < st->item_count)
                st->camera.look_item = (int16_t)arg;
            break;
        case TR3_ACT_FINISH:
            if (!heavy) {
                st->stop.kind = TR64_GAME_STOP_LEVEL_END;
                st->stop.transition = TR64_GAME_TRANSITION_CAMPAIGN;
                st->stop.next_level_id = st->script ? st->script->id + 1 : 0;
                memcpy(st->stop.name, "level finish", 13);
            }
            break;
        case TR3_ACT_CD:
            if (arg < 128u && !st->cd_played[arg]) {
                st->cd_played[arg] = 1u;
                tr3_request_music(st, (int32_t)arg);
            }
            break;
        case TR3_ACT_SECRET:
            /* TR3 secret trigger (§1.3): bitmask 1 << arg, engine-side track 122 once. */
            if (arg < 32u && !(st->secrets_mask & (1u << arg))) {
                st->secrets_mask |= (1u << arg);
                ++st->secrets_found;
                st->stats.secrets = st->secrets_found;
                tr3_request_music(st, 122);
            }
            break;
        case TR3_ACT_CLEAR_BODIES:
            for (uint16_t i = 0; i < st->item_count; ++i) {
                tr3_item_t *it = &st->items[i];
                if (it->killed && (it->flags & 0x8000u)) {
                    it->invisible = 1;
                    it->active = 0;
                }
            }
            break;
        default:
            break;
        }

        if (ak == TR3_ACT_CAMERA || ak == 12u) {
            if (cam_extra & 0x8000u)
                break;
        } else if (a & 0x8000u) {
            break;
        }
    }
}

/* Shared TR1-TR3 room/sector adapter backed by TR3 split-triangle floor engine. */
#include "game/common/internal.h"

static Sector tr3_sec_scratch[4];
static uint16_t tr3_sec_room[4];
static uint32_t tr3_sec_slot;

static const Sector *tr3_decode_sector(const tr64_core_sectors_t *cs, uint16_t room)
{
    uint32_t slot = tr3_sec_slot++ & 3u;
    Sector *out = &tr3_sec_scratch[slot];
    tr3_sec_room[slot] = room;
    out->floorIndex = tr64_u16(cs->floor_index);
    tr3_box_word_t bw = tr3_unpack_box_word(tr64_u16(cs->box_index));
    out->boxIndex = (bw.box_index == 0x7FFu || bw.box_index >= gs->level.boxesCount)
                    ? UINT16_MAX : bw.box_index;
    out->roomBelow = cs->room_below;
    out->floor = cs->floor;
    out->roomAbove = cs->room_above;
    out->ceiling = cs->ceiling;
    return out;
}

static uint16_t tr3_sector_room(const Sector *s)
{
    if (s >= tr3_sec_scratch && s < tr3_sec_scratch + 4)
        return tr3_sec_room[s - tr3_sec_scratch];
    return tr3_gs ? tr3_gs->lara.room : 0u;
}

void Room_add(Room *self, ItemObj *item)
{
    if (!self || !item) return;
    item->room = self;
    item->nextItem = self->firstItem;
    self->firstItem = item;
}

void Room_remove(Room *self, ItemObj *item)
{
    if (!self || !item) return;
    ItemObj *prev = NULL, *curr = self->firstItem;
    while (curr) {
        ItemObj *next = curr->nextItem;
        if (curr == item) {
            item->nextItem = NULL;
            if (prev) prev->nextItem = next;
            else self->firstItem = next;
            break;
        }
        prev = curr;
        curr = next;
    }
}

Room **Room_addNearRoom(Room *self, Room **list, int32 x, int32 y, int32 z)
{
    Room *nearRoom = Room_getRoom(self, x, y, z);
    int32 count = (int32)(list - gs->roomsList);
    for (int32 i = 0; i < count; ++i)
        if (gs->roomsList[i] == nearRoom)
            return list;
    if (list - gs->roomsList < 31)
        *list++ = nearRoom;
    return list;
}

Room **Room_getNearRooms(Room *self, const vec3i *pos, int32 radius, int32 height)
{
    Room **list = gs->roomsList;
    *list++ = self;
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), pos->y, i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), pos->y, i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), pos->y, i32_add(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), pos->y, i32_add(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), i32_sub(pos->y, height), i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), i32_sub(pos->y, height), i32_sub(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_add(pos->x, radius), i32_sub(pos->y, height), i32_add(pos->z, radius));
    list = Room_addNearRoom(self, list, i32_sub(pos->x, radius), i32_sub(pos->y, height), i32_add(pos->z, radius));
    if (list - gs->roomsList < 32) *list++ = NULL;
    else gs->roomsList[31] = NULL;
    return gs->roomsList;
}

const Sector *Room_getSector(const Room *self, int32 x, int32 z)
{
    uint16_t r = (uint16_t)(self - gs->rooms);
    const tr64_core_sectors_t *cs = room_sector_clamped(tr3_gs, r, x, z);
    return tr3_decode_sector(cs, r);
}

const Sector *game_tr2_room_sector(Room **room, int32 x, int32 y, int32 z)
{
    uint16_t r = (uint16_t)(*room - gs->rooms);
    const tr64_core_sectors_t *cs = tr3_get_sector(tr3_gs, x, y, z, &r);
    *room = gs->rooms + r;
    return tr3_decode_sector(cs, r);
}

Room *Room_getRoom(Room *self, int32 x, int32 y, int32 z)
{
    game_tr2_room_sector(&self, x, y, z);
    return self;
}

int32 game_tr2_water_height(Room *room, int32 x, int32 z)
{
    uint16_t r = (uint16_t)(room - gs->rooms);
    const tr64_core_rooms_t *ri = tr64_level_rooms(tr3_gs->level, tr3_physical_room(tr3_gs, r));
    int32_t mid_y = tr64_i32(ri->y_top) + 256;
    if (mid_y > tr64_i32(ri->y_bottom))
        mid_y = tr64_i32(ri->y_bottom);
    tr3_floor_sample_t fs;
    tr3_floor_eval(tr3_gs, x, mid_y, z, r, false, &fs);
    return fs.water_y;
}

const Sector *Room_getWaterSector(const Room *self, int32 x, int32 z)
{
    int32 wy = game_tr2_water_height((Room *)self, x, z);
    if (wy == TR3_NO_HEIGHT)
        return NULL;
    uint32_t slot = tr3_sec_slot++ & 3u;
    Sector *out = &tr3_sec_scratch[slot];
    memset(out, 0, sizeof(*out));
    tr3_sec_room[slot] = (uint16_t)(self - gs->rooms);
    out->roomAbove = 0; /* non-0xFF so ItemObj_getWaterLevel uses ceiling*256 */
    out->ceiling = (int8)(wy >> 8);
    return out;
}

static int32 tr3_sector_floor_impl(const Sector *s, int32 x, int32 y, int32 z, bool chunky)
{
    uint16_t r = tr3_sector_room(s);
    tr3_floor_sample_t fs;
    tr3_floor_eval(tr3_gs, x, y, z, r, chunky, &fs);
    gs->gLastFloorSlant = (uint16)((uint8)fs.tilt_x | ((uint16)(uint8)fs.tilt_z << 8));
    gs->gLastFloorData = fs.trigger_index ? (gs->level.floors + fs.trigger_index) : NULL;
    tr3_gs->last_height_type = fs.height_type;
    tr3_gs->last_climb_flags = fs.climb_flags;
    return fs.floor_y;
}

static int32 tr3_sector_ceiling_impl(const Sector *s, int32 x, int32 y, int32 z, bool chunky)
{
    uint16_t r = tr3_sector_room(s);
    tr3_floor_sample_t fs;
    tr3_floor_eval(tr3_gs, x, y, z, r, chunky, &fs);
    return fs.ceiling_y;
}

int32 Sector_getFloor(const Sector *self, int32 x, int32 y, int32 z)
{
    return tr3_sector_floor_impl(self, x, y, z, false);
}

int32 Sector_getCeiling(const Sector *self, int32 x, int32 y, int32 z)
{
    return tr3_sector_ceiling_impl(self, x, y, z, false);
}

void game_tr2_camera_heights(const Sector *s, const vec3i *pos, bool full, int32 *floor, int32 *ceiling)
{
    uint16_t r = tr3_sector_room(s);
    tr3_floor_sample_t fs;
    tr3_floor_eval(tr3_gs, pos->x, pos->y, pos->z, r, !full, &fs);
    *floor = fs.floor_y;
    *ceiling = fs.ceiling_y;
}

Room *Sector_getNextRoom(const Sector *self)
{
    uint16_t p = sector_portal_room(tr3_gs->level, &(tr64_core_sectors_t){
        .floor_index = { .raw = tr64_u16((tr64_u16_be_t){ .raw = self->floorIndex }) }
    });
    (void)p;
    if (!self->floorIndex)
        return NULL;
    const uint32_t n = tr64_level_table(tr3_gs->level, TR64_CORE_floor_data)->count;
    uint32_t idx = self->floorIndex;
    while (idx < n) {
        uint16_t w = fd_word(tr3_gs->level, idx++);
        uint32_t k = w & 0x1Fu;
        if (k == TR3_FD_PORTAL) {
            uint16_t pr = (idx < n) ? fd_word(tr3_gs->level, idx) : TR3_NO_ROOM;
            return (pr != TR3_NO_ROOM && pr < gs->level.roomsCount) ? (gs->rooms + pr) : NULL;
        }
        if (k == TR3_FD_FLOOR_SLOPE || k == TR3_FD_CEIL_SLOPE ||
            (k >= TR3_FD_SPLIT_F_NWSE && k <= TR3_FD_SPLIT_CP_SE)) {
            if (idx < n) ++idx;
        } else if (k == TR3_FD_TRIGGER) {
            idx = skip_trigger_stream(tr3_gs->level, idx, (w >> 8) & 0x7Fu, n);
        }
        if (w & 0x8000u) break;
    }
    return NULL;
}

const Sector *Sector_getSectorAbove(const Sector *self, int32 posX, int32 posZ)
{
    if (self->roomAbove == 0xFF || self->roomAbove >= gs->level.roomsCount)
        return self;
    return Sector_getSectorAbove(Room_getSector(gs->rooms + self->roomAbove, posX, posZ), posX, posZ);
}

const Sector *Sector_getSectorBelow(const Sector *self, int32 posX, int32 posZ)
{
    if (self->roomBelow == 0xFF || self->roomBelow >= gs->level.roomsCount)
        return self;
    return Sector_getSectorBelow(Room_getSector(gs->rooms + self->roomBelow, posX, posZ), posX, posZ);
}

void Sector_getTriggerFloorCeiling(const Sector *self, int32 x, int32 y, int32 z, int32 *floor, int32 *ceiling)
{
    (void)self; (void)x; (void)y; (void)z; (void)floor; (void)ceiling;
}

bool trace(const Location *from, Location *to, bool accurate)
{
    return game_tr2_sight(from, to, accurate);
}

bool traceX(const Location *from, Location *to, bool accurate)
{
    return game_tr2_sight(from, to, accurate);
}

bool traceZ(const Location *from, Location *to, bool accurate)
{
    return game_tr2_sight(from, to, accurate);
}
