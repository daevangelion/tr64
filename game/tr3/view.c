/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/draw.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Tomb Raider III skeletal pose emitter with smooth 30 Hz keyframe interpolation
 * (packed TR3 frames, articulated skin joints 315, 6-segment wind braid 2,
 * monkey mesh swaps 123/124, dynamic lights, and ground shadows). */
#include <string.h>
#include "game/tr3/tr3.h"

typedef struct {
    int32_t m[9];
    int32_t t[3]; /* Q14 relative translation */
} tr3_mat_t;

typedef struct {
    const tr64_u16_be_t *a;
    const tr64_u16_be_t *b;
    uint32_t a_words;
    uint32_t b_words;
    int32_t delta;
    int32_t rate;
} tr3_frame_pair_t;

static void mat_ident(tr3_mat_t *M)
{
    memset(M, 0, sizeof(*M));
    M->m[0] = M->m[4] = M->m[8] = 16384;
}

static void mat_rot_x(tr3_mat_t *M, int32_t a)
{
    if (!a) return;
    int32_t s = tr3_sin(a), c = tr3_cos(a);
    for (int r = 0; r < 3; ++r) {
        int32_t y = M->m[r * 3 + 1], z = M->m[r * 3 + 2];
        M->m[r * 3 + 2] = (z * c - y * s) >> 14;
        M->m[r * 3 + 1] = (y * c + z * s) >> 14;
    }
}

static void mat_rot_y(tr3_mat_t *M, int32_t a)
{
    if (!a) return;
    int32_t s = tr3_sin(a), c = tr3_cos(a);
    for (int r = 0; r < 3; ++r) {
        int32_t x = M->m[r * 3 + 0], z = M->m[r * 3 + 2];
        M->m[r * 3 + 0] = (x * c - z * s) >> 14;
        M->m[r * 3 + 2] = (z * c + x * s) >> 14;
    }
}

static void mat_rot_z(tr3_mat_t *M, int32_t a)
{
    if (!a) return;
    int32_t s = tr3_sin(a), c = tr3_cos(a);
    for (int r = 0; r < 3; ++r) {
        int32_t x = M->m[r * 3 + 0], y = M->m[r * 3 + 1];
        M->m[r * 3 + 1] = (y * c - x * s) >> 14;
        M->m[r * 3 + 0] = (x * c + y * s) >> 14;
    }
}

static void mat_rot_yxz(tr3_mat_t *M, int32_t ax, int32_t ay, int32_t az)
{
    mat_rot_y(M, ay);
    mat_rot_x(M, ax);
    mat_rot_z(M, az);
}

static void mat_trans_rel(tr3_mat_t *M, int32_t x, int32_t y, int32_t z)
{
    for (int r = 0; r < 3; ++r) {
        M->t[r] += M->m[r * 3 + 0] * x + M->m[r * 3 + 1] * y + M->m[r * 3 + 2] * z;
    }
}

/* Smooth 30 Hz basis interpolation matching shared game/matrix.c matrixLerp_c. */
static void mat_lerp_basis(tr3_mat_t *a, const tr3_mat_t *b, int32_t delta, int32_t rate)
{
    if (rate <= 1 || delta <= 0)
        return;
    if (rate == 2) {
        for (int i = 0; i < 9; ++i)
            a->m[i] += (b->m[i] - a->m[i]) >> 1;
        return;
    }
    for (int i = 0; i < 9; ++i)
        a->m[i] += ((b->m[i] - a->m[i]) * delta) / rate;
}

static inline int16_t signed_u16(tr64_u16_be_t w)
{
    uint32_t n = tr64_u16(w);
    return (int16_t)((int32_t)(n & 32767u) - (int32_t)(n & 32768u));
}

static inline int32_t root_lerp(tr64_u16_be_t a, tr64_u16_be_t b, int32_t delta, int32_t rate)
{
    int32_t va = signed_u16(a);
    if (delta <= 0 || rate <= 1)
        return va;
    int32_t vb = signed_u16(b);
    return va + ((vb - va) * delta) / rate;
}

/* Decode one packed TR3 bone rotation (1 or 2 words) and apply to M. */
static const tr64_u16_be_t *apply_packed_rot(tr3_mat_t *M, const tr64_u16_be_t *p,
                                             const tr64_u16_be_t *end)
{
    if (!p || p >= end)
        return end;
    uint32_t hi = tr64_u16(*p++);
    uint32_t mode = hi >> 14;
    if (mode) {
        int16_t ang = (int16_t)((hi & 1023u) << 6);
        if (mode == 1u) mat_rot_x(M, ang);
        else if (mode == 2u) mat_rot_y(M, ang);
        else mat_rot_z(M, ang);
        return p;
    }
    if (p >= end)
        return end;
    uint32_t lo = tr64_u16(*p++);
    int16_t ax = (int16_t)((hi & 0x3FF0u) << 2);
    int16_t ay = (int16_t)(((hi & 15u) << 12) | ((lo & 0xFC00u) >> 4));
    int16_t az = (int16_t)((lo & 1023u) << 6);
    mat_rot_yxz(M, ax, ay, az);
    return p;
}

/* Interpolate one bone rotation between keyframes A and B (matches game/view.c:734-750). */
static void apply_lerp_rot(tr3_mat_t *M, const tr64_u16_be_t **pa, const tr64_u16_be_t *enda,
                           const tr64_u16_be_t **pb, const tr64_u16_be_t *endb,
                           int32_t delta, int32_t rate)
{
    if (delta <= 0 || !*pb || *pb >= endb) {
        *pa = apply_packed_rot(M, *pa, enda);
        return;
    }
    /* Fast path: identical packed rotation words need no matrix lerp. */
    bool same = false;
    if (*pa < enda && *pb < endb && (*pa)->raw == (*pb)->raw) {
        uint32_t ha = tr64_u16(**pa);
        if (ha >> 14)
            same = true;
        else if (enda - *pa >= 2 && endb - *pb >= 2 && (*pa)[1].raw == (*pb)[1].raw)
            same = true;
    }
    if (same) {
        const tr64_u16_be_t *next_a = apply_packed_rot(M, *pa, enda);
        *pb += (next_a - *pa);
        *pa = next_a;
        return;
    }
    tr3_mat_t mb = *M;
    *pb = apply_packed_rot(&mb, *pb, endb);
    *pa = apply_packed_rot(M, *pa, enda);
    mat_lerp_basis(M, &mb, delta, rate);
}

static const tr64_u16_be_t *resolve_key_at(const tr64_level_t *l, uint16_t anim_idx,
                                           uint32_t key_idx, uint32_t *out_words)
{
    const uint32_t acount = tr64_level_table(l, TR64_CORE_animations)->count;
    const uint32_t fcount = tr64_level_table(l, TR64_CORE_frames)->count;
    if (!acount || !fcount || anim_idx >= acount)
        return NULL;
    const tr64_core_animations_t *a = tr64_level_animations(l, anim_idx);
    uint32_t fsize = a->frame_size ? a->frame_size : 10u;
    uint32_t woff = tr64_u32(a->frame_offset) / 2u + key_idx * fsize;
    if (woff >= fcount || fsize > fcount - woff)
        return NULL;
    *out_words = fsize;
    return tr64_level_frames(l, woff);
}

/* Resolve keyframe pair (A, B, delta, rate) for smooth 30 Hz interpolation
 * (mirrors shared game_tr2_packed_frames including clip tail wrap). */
static tr3_frame_pair_t resolve_frame_pair(const tr64_level_t *l, uint16_t anim_idx, uint16_t frame)
{
    tr3_frame_pair_t pair;
    memset(&pair, 0, sizeof(pair));
    pair.rate = 1;
    const uint32_t acount = tr64_level_table(l, TR64_CORE_animations)->count;
    if (!acount || anim_idx >= acount)
        return pair;

    const tr64_core_animations_t *a = tr64_level_animations(l, anim_idx);
    uint32_t rate = a->frame_rate ? a->frame_rate : 1u;
    uint32_t start = tr64_u16(a->frame_start);
    uint32_t end = tr64_u16(a->frame_end);
    uint32_t rel = (frame >= start) ? (uint32_t)(frame - start) : 0u;
    uint32_t key_a = rel / rate;
    uint32_t delta = rel % rate;

    pair.a = resolve_key_at(l, anim_idx, key_a, &pair.a_words);
    if (!pair.a || delta == 0u || rate <= 1u) {
        pair.b = pair.a;
        pair.b_words = pair.a_words;
        pair.delta = 0;
        pair.rate = (int32_t)rate;
        return pair;
    }

    uint32_t key_b = key_a + 1u;
    uint32_t next_frame_pos = start + key_b * rate;
    if (next_frame_pos > end) {
        /* Tail of clip: adjust effective rate across remaining frames and blend
         * toward next_animation / next_frame when available. */
        uint32_t rem = (end >= start + key_a * rate) ? (end - (start + key_a * rate) + 1u) : 1u;
        uint16_t next_a = tr64_u16(a->next_animation);
        uint16_t next_f = tr64_u16(a->next_frame);
        if (next_a < acount) {
            const tr64_core_animations_t *na = tr64_level_animations(l, next_a);
            uint32_t nrate = na->frame_rate ? na->frame_rate : 1u;
            uint32_t nstart = tr64_u16(na->frame_start);
            uint32_t nkey = (next_f >= nstart) ? (uint32_t)(next_f - nstart) / nrate : 0u;
            pair.b = resolve_key_at(l, next_a, nkey, &pair.b_words);
        }
        if (!pair.b)
            pair.b = resolve_key_at(l, anim_idx, key_b, &pair.b_words);
        if (pair.b && rem > 1u) {
            pair.delta = (int32_t)delta;
            pair.rate = (int32_t)rem;
            return pair;
        }
        pair.b = pair.a;
        pair.b_words = pair.a_words;
        pair.delta = 0;
        pair.rate = 1;
        return pair;
    }

    pair.b = resolve_key_at(l, anim_idx, key_b, &pair.b_words);
    if (!pair.b) {
        pair.b = pair.a;
        pair.b_words = pair.a_words;
        pair.delta = 0;
        pair.rate = 1;
        return pair;
    }
    pair.delta = (int32_t)delta;
    pair.rate = (int32_t)rate;
    return pair;
}

static void fill_drawable(tr64_game_drawable_t *d, uint32_t mesh_slot, uint32_t inst_id,
                          uint32_t room, const tr3_mat_t *M, int32_t wx, int32_t wy, int32_t wz,
                          int32_t light_offset, uint8_t water, uint8_t bone)
{
    d->mesh_slot = mesh_slot;
    d->instance_id = inst_id;
    d->room = room;
    memcpy(d->basis, M->m, sizeof(d->basis));
    d->translation[0] = wx + (M->t[0] >> 14);
    d->translation[1] = wy + (M->t[1] >> 14);
    d->translation[2] = wz + (M->t[2] >> 14);
    d->light_offset = light_offset;
    d->water = water;
    d->kind = TR64_GAME_DRAW_MESH;
    d->bone = bone;
    d->blend = TR64_GAME_DRAW_BLEND_NONE;
}

static int32_t room_light_offset(const tr3_state_t *st, uint32_t room, int16_t item_shade)
{
    if (item_shade >= 0)
        return (int32_t)item_shade - 4096;
    const uint32_t rcount = tr64_level_table(st->level, TR64_CORE_rooms)->count;
    if (room < rcount) {
        uint16_t amb = tr64_u16(tr64_level_rooms(st->level, tr3_physical_room(st, (uint16_t)room))->ambient);
        return (int32_t)amb - 4096;
    }
    return 0;
}

/* Emit a smoothly 30 Hz interpolated skeletal model (Lara, Monkey, Tiger, Door, Switch, etc.). */
static uint32_t emit_model(const tr3_state_t *st, const tr64_core_models_t *m,
                           const tr64_core_models_t *swap_m, uint32_t swap_mask,
                           uint16_t anim_idx, uint16_t frame, uint32_t inst_id,
                           uint32_t room, int32_t wx, int32_t wy, int32_t wz,
                           int16_t rx, int16_t ry, int16_t rz, int32_t light_offset,
                           uint8_t water, bool is_lara,
                           tr64_game_drawable_t *buf, uint32_t cap)
{
    if (!m) return 0;
    const tr64_level_t *l = st->level;
    const uint32_t mcount = tr64_u16(m->mesh_count);
    const uint32_t mstart = tr64_u16(m->mesh_start);
    const uint32_t bone_base = tr64_u32(m->bone_offset) / 4u;
    const uint32_t nflags_cnt = tr64_level_table(l, TR64_CORE_node_flags)->count;
    const uint32_t bones_cnt = tr64_level_table(l, TR64_CORE_bones)->count;
    const uint32_t mp_cnt = tr64_level_table(l, TR64_CORE_mesh_pointers)->count;

    /* TR3 Lara uses Model 315 (15 body meshes) instead of Model 0's dummy slots,
     * plus 6-segment wind-swayed braid (Model 2) (§2.6). */
    const tr64_core_models_t *skin_m  = is_lara ? tr3_find_model(l, TR3_OBJ_SKIN_JOINTS) : NULL;
    const tr64_core_models_t *braid_m = is_lara ? tr3_find_model(l, TR3_OBJ_BRAID) : NULL;
    const uint32_t body_start = (skin_m && tr64_u16(skin_m->mesh_count) == mcount)
                                ? tr64_u16(skin_m->mesh_start) : mstart;
    uint32_t need = mcount;
    if (braid_m && tr64_u16(braid_m->mesh_count) >= TR3_BRAID_SEGS)
        need += TR3_BRAID_SEGS;
    if (need > cap)
        return need;

    tr3_frame_pair_t pair = resolve_frame_pair(l, anim_idx, frame);
    const tr64_u16_be_t *pa = (pair.a && pair.a_words > 9u) ? pair.a + 9 : NULL;
    const tr64_u16_be_t *enda = pair.a ? pair.a + pair.a_words : NULL;
    const tr64_u16_be_t *pb = (pair.b && pair.b_words > 9u) ? pair.b + 9 : NULL;
    const tr64_u16_be_t *endb = pair.b ? pair.b + pair.b_words : NULL;

    tr3_mat_t stack[12];
    int sp = 0;
    mat_ident(&stack[0]);
    mat_rot_yxz(&stack[0], rx, ry, rz);
    if (pair.a && pair.a_words >= 9u) {
        if (pair.delta > 0 && pair.b && pair.b_words >= 9u) {
            mat_trans_rel(&stack[0],
                          root_lerp(pair.a[6], pair.b[6], pair.delta, pair.rate),
                          root_lerp(pair.a[7], pair.b[7], pair.delta, pair.rate),
                          root_lerp(pair.a[8], pair.b[8], pair.delta, pair.rate));
        } else {
            mat_trans_rel(&stack[0], signed_u16(pair.a[6]), signed_u16(pair.a[7]), signed_u16(pair.a[8]));
        }
    }

    uint32_t written = 0;
    tr3_mat_t head_mat;
    bool have_head = false;
    for (uint32_t b = 0; b < mcount; ++b) {
        if (b > 0) {
            uint32_t nidx = bone_base + b - 1u;
            uint32_t fl = (nidx < nflags_cnt) ? tr64_u32(*tr64_level_node_flags(l, nidx)) : 0u;
            if ((fl & 1u) && sp > 0) --sp; /* POP */
            if ((fl & 2u) && sp < 11) { stack[sp + 1] = stack[sp]; ++sp; } /* PUSH */
            if (nidx * 4u + 3u < bones_cnt) {
                int32_t bx = tr64_i32(*tr64_level_bones(l, nidx * 4u + 1u));
                int32_t by = tr64_i32(*tr64_level_bones(l, nidx * 4u + 2u));
                int32_t bz = tr64_i32(*tr64_level_bones(l, nidx * 4u + 3u));
                mat_trans_rel(&stack[sp], bx, by, bz);
            }
        }
        apply_lerp_rot(&stack[sp], &pa, enda, &pb, endb, pair.delta, pair.rate);
        if (is_lara && b == 7u && (st->lara.torso_x || st->lara.torso_y))
            mat_rot_yxz(&stack[sp], st->lara.torso_x, st->lara.torso_y, 0);
        if (is_lara && b == 14u) {
            if (st->lara.head_x || st->lara.head_y)
                mat_rot_yxz(&stack[sp], st->lara.head_x, st->lara.head_y, 0);
            head_mat = stack[sp];
            have_head = true;
        }

        uint32_t slot = body_start + b;
        if (swap_m && b < 32u && b < tr64_u16(swap_m->mesh_count) && !((swap_mask >> b) & 1u))
            slot = tr64_u16(swap_m->mesh_start) + b;

        if (slot < mp_cnt) {
            fill_drawable(&buf[written++], slot, inst_id, room, &stack[sp],
                          wx, wy, wz, light_offset, water, (uint8_t)b);
        }
    }

    /* 6-segment wind-swayed physics braid (2) anchored cleanly to back of Lara's head (§2.6). */
    if (braid_m && tr64_u16(braid_m->mesh_count) >= TR3_BRAID_SEGS && have_head) {
        uint32_t bstart = tr64_u16(braid_m->mesh_start);
        tr3_mat_t bm = head_mat;
        mat_trans_rel(&bm, 0, -24, -48);
        bool windy = (tr3_room_flags(st, (uint16_t)room) & TR3_ROOM_WIND) != 0;
        int16_t sway = windy ? (int16_t)((tr3_sin((int32_t)(st->ticks * 640u)) * 1024) >> 14) : 0;
        mat_rot_x(&bm, -14500); /* hang downward behind neck */
        for (uint32_t s = 0; s < TR3_BRAID_SEGS; ++s) {
            uint32_t bslot = bstart + s;
            if (bslot >= mp_cnt) break;
            if (s > 0) {
                mat_trans_rel(&bm, 0, 0, 38);
                mat_rot_yxz(&bm, (int16_t)(sway >> 1), sway, 0);
            }
            fill_drawable(&buf[written++], bslot, inst_id + 1u, room, &bm,
                          wx, wy, wz, light_offset, water, (uint8_t)s);
        }
    }

    return written;
}

void game_view_camera(tr64_game_view_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!tr3_gs) return;
    memcpy(out->camera_basis, tr3_gs->camera.basis, sizeof(out->camera_basis));
    memcpy(out->camera_eye, tr3_gs->camera.eye, sizeof(out->camera_eye));
    memcpy(out->camera_target, tr3_gs->camera.target, sizeof(out->camera_target));
    out->camera_room = tr3_gs->camera.room;
    out->flipped = tr3_gs->flipped;
    out->texture_animation_step = tr3_gs->ticks / 6u;
}

void game_view_camera_player(unsigned slot, tr64_game_view_t *out)
{
    if (slot == 0)
        game_view_camera(out);
}

uint32_t game_view_room(uint32_t room, uint32_t *cursor, tr64_game_drawable_t *buffer,
                        uint32_t capacity)
{
    if (!tr3_gs || !cursor)
        return 0;
    const tr3_state_t *st = tr3_gs;
    uint32_t c = *cursor;
    if (c == TR64_GAME_VIEW_END)
        return 0;

    const uint8_t water = (tr3_room_flags(st, (uint16_t)room) & TR3_ROOM_WATER) ? 1u : 0u;
    uint32_t written = 0;

    /* Cursor 0: Lara if she is in this room. */
    if (c == 0) {
        if (st->lara.room == room) {
            const tr64_core_models_t *lm = tr3_find_model(st->level, TR3_OBJ_LARA);
            int32_t loff = room_light_offset(st, room, -1);
            uint32_t n = emit_model(st, lm, NULL, 0, st->lara.anim, st->lara.frame,
                                    0x80000000u, room, st->lara.x, st->lara.y, st->lara.z,
                                    st->lara.rot_x, st->lara.rot_y, st->lara.rot_z,
                                    loff, water, true, buffer, capacity);
            if (n > capacity)
                return n;
            written += n;
        }
        c = 1;
    }

    /* Cursor 1..item_count: level items in this room. */
    while (c >= 1u && c <= st->item_count) {
        uint16_t idx = (uint16_t)(c - 1u);
        const tr3_item_t *it = &st->items[idx];
        if (!it->invisible && it->room == room) {
            const tr64_core_models_t *m = tr3_find_model(st->level, it->type);
            if (m && tr64_u16(m->mesh_count) > 0) {
                const tr64_core_models_t *swap_m = NULL;
                uint32_t swap_mask = 0;
                if (it->type == TR3_OBJ_MONKEY) {
                    if (it->mesh_swap == 2u) {
                        swap_m = tr3_find_model(st->level, TR3_OBJ_MESH_SWAP3);
                        swap_mask = (it->carried_item >= 0) ? 0xFFFF6E6Fu : 0xFFFF6F6Fu;
                    } else if (it->mesh_swap == 1u) {
                        swap_m = tr3_find_model(st->level, TR3_OBJ_MESH_SWAP2);
                        swap_mask = (it->carried_item >= 0) ? 0xFFFFFEFFu : 0xFFFFFFFFu;
                    }
                }
                int32_t loff = room_light_offset(st, room, it->shade);
                if (it->type == TR3_OBJ_SAVE_CRYSTAL)
                    loff = -3072; /* bright glowing crystal */
                uint32_t left = capacity - written;
                uint32_t n = emit_model(st, m, swap_m, swap_mask, it->anim, it->frame,
                                        0x80000000u + idx + 4u, room, it->x, it->y, it->z,
                                        it->rot_x, it->rot_y, it->rot_z,
                                        loff, water, false, buffer + written, left);
                if (n > left) {
                    if (written > 0) {
                        *cursor = c;
                        return written;
                    }
                    return n;
                }
                written += n;
            }
        }
        ++c;
    }

    *cursor = TR64_GAME_VIEW_END;
    return written;
}

uint32_t game_rest_item_view(uint32_t coreItemIndex, tr64_game_drawable_t *buffer,
                             uint32_t capacity)
{
    if (!tr3_gs || coreItemIndex >= tr3_gs->item_count)
        return 0;
    const tr3_item_t *it = &tr3_gs->items[coreItemIndex];
    const tr64_core_models_t *m = tr3_find_model(tr3_gs->level, it->type);
    if (!m) return 0;
    if (!buffer || !capacity) return tr64_u16(m->mesh_count);
    return emit_model(tr3_gs, m, NULL, 0, it->anim, it->frame,
                      0x80000000u + coreItemIndex, it->room, it->x, it->y, it->z,
                      0, it->rot_y, 0, room_light_offset(tr3_gs, it->room, it->shade),
                      0, false, buffer, capacity);
}

static uint64_t dist2_eye(const int32_t eye[3], int32_t x, int32_t y, int32_t z)
{
    int64_t dx = (int64_t)x - eye[0];
    int64_t dy = (int64_t)y - eye[1];
    int64_t dz = (int64_t)z - eye[2];
    return (uint64_t)(dx * dx + dy * dy + dz * dz);
}

uint32_t game_view_lights(const int32_t eye[3], tr64_gpu_light_t *out, uint32_t max_lights)
{
    if (!tr3_gs || !out || !max_lights)
        return 0;
    const tr3_state_t *st = tr3_gs;
    uint32_t count = 0;

    /* Lara lit flare or weapon muzzle flash. */
    if (st->lara.flare_lit && st->lara.flare_age < 900u) {
        bool flicker = (st->lara.flare_age >= 810u) && ((st->ticks & 1u) != 0);
        if (!flicker) {
            out[count++] = (tr64_gpu_light_t){
                .pos = { st->lara.x, st->lara.y - 512, st->lara.z },
                .radius = 2048, .peak = 4096u, .shift = 10u, .active = 1u
            };
        }
    } else if (st->lara.muzzle_timer > 0) {
        out[count++] = (tr64_gpu_light_t){
            .pos = { st->lara.x, st->lara.y - 512, st->lara.z },
            .radius = 2048, .peak = 4096u, .shift = 10u, .active = 1u
        };
    }

    /* Save Crystals (180) and Flame Emitters (330..333), prioritized closest to eye. */
    uint64_t best_d[8];
    for (uint32_t k = 0; k < count && k < 8u; ++k) best_d[k] = 0;

    for (uint16_t i = 0; i < st->item_count; ++i) {
        const tr3_item_t *it = &st->items[i];
        int32_t radius = 0;
        uint16_t peak = 0;
        if (it->type == TR3_OBJ_SAVE_CRYSTAL && !it->invisible) {
            int32_t pulse = tr3_cos((int32_t)((st->ticks & 63u) << 10));
            if (pulse < 0) pulse = -pulse;
            radius = 2048;
            peak = (uint16_t)(1536 + (pulse >> 3));
        } else if (it->type == TR3_OBJ_SMALL_FLAME || it->type == TR3_OBJ_BIG_FLAME ||
                   it->type == TR3_OBJ_JET_FLAME || it->type == TR3_OBJ_SIDE_FLAME) {
            radius = (it->type == TR3_OBJ_SMALL_FLAME) ? 2048 : 3072;
            peak = (uint16_t)(2560 + (((st->ticks * 37u + i * 19u) & 31u) << 5));
        } else {
            continue;
        }
        uint64_t d2 = eye ? dist2_eye(eye, it->x, it->y - 256, it->z) : 0;
        if (d2 > UINT64_C(10240) * UINT64_C(10240))
            continue;
        tr64_gpu_light_t cand = {
            .pos = { it->x, it->y - 256, it->z },
            .radius = radius, .peak = peak, .shift = 10u, .active = 1u
        };
        if (count < max_lights && count < 8u) {
            best_d[count] = d2;
            out[count++] = cand;
        } else if (count > 0) {
            uint32_t worst = 0;
            for (uint32_t k = 1; k < count; ++k)
                if (best_d[k] > best_d[worst]) worst = k;
            if (d2 < best_d[worst]) {
                best_d[worst] = d2;
                out[worst] = cand;
            }
        }
    }
    return count;
}

uint32_t game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity)
{
    if (!tr3_gs || !out || !capacity)
        return 0;
    const tr3_state_t *st = tr3_gs;
    uint32_t n = 0;

    /* Lara's ground shadow. */
    tr3_floor_sample_t fs;
    tr3_floor_eval(st, st->lara.x, st->lara.y - 128, st->lara.z, st->lara.room, false, &fs);
    if (fs.floor_y != TR3_NO_HEIGHT) {
        out[n++] = (tr64_game_shadow_t){
            .x = st->lara.x, .y = fs.floor_y - 2, .z = st->lara.z,
            .rx = 160, .rz = 160, .yaw = st->lara.rot_y,
            .room = fs.room, .alpha = 128, .pad = 0
        };
    }

    /* Active Monkeys and Tigers. */
    for (uint16_t i = 0; i < st->item_count && n < capacity; ++i) {
        const tr3_item_t *it = &st->items[i];
        if (it->invisible || (it->type != TR3_OBJ_MONKEY && it->type != TR3_OBJ_TIGER))
            continue;
        uint16_t r = (it->type == TR3_OBJ_TIGER) ? 220u : 128u;
        out[n++] = (tr64_game_shadow_t){
            .x = it->x, .y = it->y - 2, .z = it->z,
            .rx = r, .rz = r, .yaw = it->rot_y,
            .room = it->room, .alpha = 128, .pad = 0
        };
    }
    return n;
}

uint32_t tr3_emit_ring_model(const tr3_state_t *st, uint32_t model_type, uint16_t frame_rel,
                             const int32_t basis[9], const int32_t trans[3],
                             uint32_t inst_id, int32_t light_offset,
                             tr64_game_drawable_t *buf, uint32_t cap)
{
    if (!st || !st->level) return 0;
    const tr64_level_t *l = st->level;
    const tr64_core_models_t *m = tr3_find_model(l, model_type);
    if (!m) return 0;
    const uint32_t mcount = tr64_u16(m->mesh_count);
    const uint32_t mstart = tr64_u16(m->mesh_start);
    const uint32_t bone_base = tr64_u32(m->bone_offset) / 4u;
    const uint32_t nflags_cnt = tr64_level_table(l, TR64_CORE_node_flags)->count;
    const uint32_t bones_cnt = tr64_level_table(l, TR64_CORE_bones)->count;
    const uint32_t mp_cnt = tr64_level_table(l, TR64_CORE_mesh_pointers)->count;
    if (!mcount) return 0;
    if (!buf || mcount > cap) return mcount;

    uint16_t aidx = tr64_u16(m->animation);
    uint32_t acount = tr64_level_table(l, TR64_CORE_animations)->count;
    tr3_frame_pair_t pair;
    memset(&pair, 0, sizeof(pair));
    pair.rate = 1;
    if (aidx != 65535u && aidx < acount) {
        const tr64_core_animations_t *a = tr64_level_animations(l, aidx);
        uint16_t start = tr64_u16(a->frame_start);
        uint16_t end = tr64_u16(a->frame_end);
        uint32_t f = (uint32_t)start + (uint32_t)frame_rel;
        if (f > end) f = end;
        pair = resolve_frame_pair(l, aidx, (uint16_t)f);
    }
    const tr64_u16_be_t *pa = (pair.a && pair.a_words > 9u) ? pair.a + 9 : NULL;
    const tr64_u16_be_t *enda = pair.a ? pair.a + pair.a_words : NULL;
    const tr64_u16_be_t *pb = (pair.b && pair.b_words > 9u) ? pair.b + 9 : NULL;
    const tr64_u16_be_t *endb = pair.b ? pair.b + pair.b_words : NULL;

    tr3_mat_t stack[12];
    int sp = 0;
    memcpy(stack[0].m, basis, sizeof(stack[0].m));
    stack[0].t[0] = stack[0].t[1] = stack[0].t[2] = 0;
    if (pair.a && pair.a_words >= 9u) {
        if (pair.delta > 0 && pair.b && pair.b_words >= 9u) {
            mat_trans_rel(&stack[0],
                          root_lerp(pair.a[6], pair.b[6], pair.delta, pair.rate),
                          root_lerp(pair.a[7], pair.b[7], pair.delta, pair.rate),
                          root_lerp(pair.a[8], pair.b[8], pair.delta, pair.rate));
        } else {
            mat_trans_rel(&stack[0], signed_u16(pair.a[6]), signed_u16(pair.a[7]), signed_u16(pair.a[8]));
        }
    }

    uint32_t written = 0;
    for (uint32_t b = 0; b < mcount; ++b) {
        if (b > 0) {
            uint32_t nidx = bone_base + b - 1u;
            uint32_t fl = (nidx < nflags_cnt) ? tr64_u32(*tr64_level_node_flags(l, nidx)) : 0u;
            if ((fl & 1u) && sp > 0) --sp;
            if ((fl & 2u) && sp < 11) { stack[sp + 1] = stack[sp]; ++sp; }
            if (nidx * 4u + 3u < bones_cnt) {
                int32_t bx = tr64_i32(*tr64_level_bones(l, nidx * 4u + 1u));
                int32_t by = tr64_i32(*tr64_level_bones(l, nidx * 4u + 2u));
                int32_t bz = tr64_i32(*tr64_level_bones(l, nidx * 4u + 3u));
                mat_trans_rel(&stack[sp], bx, by, bz);
            }
        }
        apply_lerp_rot(&stack[sp], &pa, enda, &pb, endb, pair.delta, pair.rate);
        uint32_t slot = mstart + b;
        if (slot < mp_cnt) {
            fill_drawable(&buf[written++], slot, inst_id, UINT32_MAX, &stack[sp],
                          trans[0], trans[1], trans[2], light_offset, 0u, (uint8_t)b);
        }
    }
    return written;
}
