/* SPDX-License-Identifier: MIT */
/* Directed host presentation & rendering probe for TR2 room effects, muzzle flashes,
 * flare flame & dynamic lighting, creature head tracking, and Lara's 6-segment braid. */
#include "game/internal.h"
#include "tr64/game.h"
#include "tr64/code.h"
#include "tr64/level.h"
#include "engine/core/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void tr64_gpu_init(void);
extern void tr64_render_scene_set_live(int live);
extern void tr64_render_scene_load(const tr64_level_t *level, const char *name, uint32_t pack_bytes);
extern void tr64_render_game_scene_prepare(void);
extern void tr64_render_game_scene_frame(const tr64_game_view_t *view,
                                         tr64_game_drawable_t *buffer, uint32_t capacity);

void __real_game_tr2_tick(uint32 mask);
void __real_tr64_level_load(tr64_level_t *out, uint32_t pack_bytes, const char *name);

static int render_ready;
static uint32_t saved_pack_bytes;
static char saved_level_name[32];
static tr64_game_drawable_t frame_buf[TR64_GAME_DRAW_CHUNK];

void __wrap_tr64_level_load(tr64_level_t *out, uint32_t pack_bytes, const char *name)
{
    saved_pack_bytes = pack_bytes;
    strncpy(saved_level_name, name, sizeof(saved_level_name) - 1);
    saved_level_name[sizeof(saved_level_name) - 1] = 0;
    __real_tr64_level_load(out, pack_bytes, name);
    if (getenv("TR64_FX_SHOT_OUT") || getenv("TR64_FX_RENDER_ALL")) {
        tr64_gpu_init();
        code_loading_begin();
        tr64_load_scope_begin();
        tr64_render_scene_set_live(1);
        tr64_render_scene_load(out, saved_level_name, saved_pack_bytes);
        tr64_load_scope_end();
        code_loading_end();
        render_ready = 1;
    }
}

static void ensure_render(void)
{
    if (render_ready == 1) {
        tr64_render_game_scene_prepare();
        render_ready = 2;
    }
}

static uint32 pull_room(uint32 room, uint32 chunk, tr64_game_drawable_t *out, uint32 cap)
{
    uint32 cursor = 0, total = 0;
    while (cursor != TR64_GAME_VIEW_END) {
        uint32 avail = cap - total;
        uint32 ask = chunk < avail ? chunk : avail;
        uint32 n = game_view_room(room, &cursor, out + total, ask);
        if (n > ask) {
            /* Single item larger than ask: retry with exact required capacity. */
            if (n > cap - total) sys_error("fx probe: room exceeds %lu drawables", (unsigned long)cap);
            n = game_view_room(room, &cursor, out + total, n);
        }
        total += n;
        if (cursor == TR64_GAME_VIEW_END) break;
    }
    return total;
}

static void verify_chunk_cursor(uint32 room, const tr64_game_drawable_t *ref, uint32 refCount)
{
    tr64_game_drawable_t chunked[256];
    uint32 got = pull_room(room, 1u, chunked, 256u);
    if (got != refCount || (got && memcmp(chunked, ref, got * sizeof(*ref)) != 0))
        sys_error("fx probe: multi-chunk cursor mismatch in room %lu (got=%lu ref=%lu)",
                  (unsigned long)room, (unsigned long)got, (unsigned long)refCount);
}

static void set_custom_camera(tr64_game_view_t *view, const char *spec)
{
    long r, ex, ey, ez, tx, ty, tz;
    if (sscanf(spec, "%ld,%ld,%ld,%ld,%ld,%ld,%ld", &r, &ex, &ey, &ez, &tx, &ty, &tz) != 7)
        sys_error("fx probe: bad TR64_FX_CAM format");
    int16 ax = 0, ay = 0;
    anglesFromVector((int32)(tx - ex), (int32)(ty - ey), (int32)(tz - ez), &ax, &ay);
    uint32 txsc = gSinCosTable[(uint16)ax >> 4], tysc = gSinCosTable[(uint16)ay >> 4];
    int32 sx = wrap_i32(txsc) >> 16, cx = (int16)txsc;
    int32 sy = wrap_i32(tysc) >> 16, cy = (int16)tysc;
    view->camera_basis[0] = cy; view->camera_basis[1] = 0; view->camera_basis[2] = -sy;
    view->camera_basis[3] = (sx * sy) >> 14; view->camera_basis[4] = cx; view->camera_basis[5] = (sx * cy) >> 14;
    view->camera_basis[6] = (cx * sy) >> 14; view->camera_basis[7] = -sx; view->camera_basis[8] = (cx * cy) >> 14;
    view->camera_eye[0] = (int32)ex; view->camera_eye[1] = (int32)ey; view->camera_eye[2] = (int32)ez;
    view->camera_target[0] = (int32)tx; view->camera_target[1] = (int32)ty; view->camera_target[2] = (int32)tz;
    view->camera_room = (uint32)r;
}

static void dump_room(uint32 room, bool checkCreatureZero)
{
    tr64_game_drawable_t buf[256];
    uint32 n = pull_room(room, 256u, buf, 256u);
    verify_chunk_cursor(room, buf, n);
    const Lara *lara = gs->players[0];
    uint32 laraId = lara ? 0x80000000u + (uint32)(lara - gs->items) : UINT32_MAX;
    for (uint32 i = 0; i < n; ++i) {
        const tr64_game_drawable_t *d = buf + i;
        if (d->instance_id >= 0x80000000u + MAX_ITEMS) {
            printf("FX %u %u %u %u %u %u %d %d %d %d %d %d %d\n",
                   gs->tick, room, d->instance_id - (0x80000000u + MAX_ITEMS),
                   d->kind, d->mesh_slot, d->bone, d->light_offset,
                   d->translation[0], d->translation[1], d->translation[2],
                   d->basis[0], d->basis[1], d->basis[2]);
        } else if (d->instance_id == laraId) {
            if (d->bone == JOINT_HEAD || d->bone >= JOINT_MAX) {
                printf("LARA %u %u %u %u %u %d %d %d %d\n",
                       gs->tick, room, d->bone, d->kind, d->mesh_slot, d->light_offset,
                       d->translation[0], d->translation[1], d->translation[2]);
            }
        } else if (d->instance_id >= 0x80000000u) {
            uint32 idx = d->instance_id - 0x80000000u;
            if (idx < MAX_ITEMS && gs->items[idx].type == TR2_ITEM_LOOSE_FLARE) {
                printf("FLARE %u %u %u %u %u %u %d %d %d %d\n",
                       gs->tick, room, idx, d->bone, d->kind, d->mesh_slot, d->light_offset,
                       d->translation[0], d->translation[1], d->translation[2]);
            }
        }
    }
    if (checkCreatureZero) {
        for (unsigned c = 0; c < TR2_CREATURE_SLOTS; ++c) {
            TR2Creature *ai = gs->tr2->creatures + c;
            if (!ai->item || ai->item->room != gs->rooms + room) continue;
            if (!ai->jointYaw[0] && !ai->jointYaw[1]) continue;
            uint32 cid = 0x80000000u + (uint32)(ai->item - gs->items);
            uint32 idx = (uint32)(ai->item - gs->items);
            for (uint32 i = 0; i < n; ++i) {
                const tr64_game_drawable_t *d = buf + i;
                if (d->instance_id != cid) continue;
                bool tracked = (ai->item->type == TR2_ITEM_TIGER && d->bone == TR2_TIGER_HEAD_JOINT) ||
                               (ai->item->type == TR2_ITEM_REX && (d->bone == 11 || d->bone == 12)) ||
                               (ai->item->type == TR2_ITEM_DOBERMAN && d->bone == 20) ||
                               ((ai->item->type >= TR2_ITEM_GUNMAN_1 && ai->item->type <= TR2_ITEM_GUNMAN_3) &&
                                (d->bone == 1 || d->bone == 2)) ||
                               (ai->item->type == TR2_ITEM_RAT && d->bone == 2) ||
                               (ai->item->type == TR2_ITEM_THUG && d->bone == 5);
                if (!tracked) continue;
                printf("CREATURE %u %u %u %u %d %d %u %d %d %d %d %d %d\n",
                       gs->tick, room, idx, ai->item->type, ai->jointYaw[0], ai->jointYaw[1],
                       d->bone, d->basis[0], d->basis[1], d->basis[2], d->basis[6], d->basis[7], d->basis[8]);
            }
            /* Negative control: compare with jointYaw temporarily zeroed, then restore immediately. */
            int16 jy0 = ai->jointYaw[0], jy1 = ai->jointYaw[1];
            ai->jointYaw[0] = ai->jointYaw[1] = 0;
            tr64_game_drawable_t zbuf[256];
            uint32 zn = pull_room(room, 256u, zbuf, 256u);
            ai->jointYaw[0] = jy0; ai->jointYaw[1] = jy1;
            for (uint32 i = 0; i < zn; ++i) {
                const tr64_game_drawable_t *d = zbuf + i;
                if (d->instance_id != cid) continue;
                bool tracked = (ai->item->type == TR2_ITEM_TIGER && d->bone == TR2_TIGER_HEAD_JOINT) ||
                               (ai->item->type == TR2_ITEM_REX && (d->bone == 11 || d->bone == 12)) ||
                               (ai->item->type == TR2_ITEM_DOBERMAN && d->bone == 20) ||
                               ((ai->item->type >= TR2_ITEM_GUNMAN_1 && ai->item->type <= TR2_ITEM_GUNMAN_3) &&
                                (d->bone == 1 || d->bone == 2)) ||
                               (ai->item->type == TR2_ITEM_RAT && d->bone == 2) ||
                               (ai->item->type == TR2_ITEM_THUG && d->bone == 5);
                if (!tracked) continue;
                printf("CREATURE_ZERO %u %u %u %u %u %d %d %d %d %d %d\n",
                       gs->tick, room, idx, ai->item->type, d->bone,
                       d->basis[0], d->basis[1], d->basis[2], d->basis[6], d->basis[7], d->basis[8]);
            }
        }
    }
}

void __wrap_game_tr2_tick(uint32 mask)
{
    __real_game_tr2_tick(mask);
    uint32 logicBefore = gs->gRandSeedLogic, drawBefore = gs->gRandSeedDraw;
    vec3i laraPosBefore = gs->players[0]->pos;
    int16 laraHealthBefore = gs->players[0]->health;

    if (gs->tick == 1) {
        /* Report cooked sprite sequence starts/counts for exact ID verification. */
        static const uint16 types[] = {
            TR2_MODEL_HAIR, TR2_MODEL_GUN_FLASH, TR2_SPRITE_WAKE, TR2_SPRITE_EXPLOSION,
            TR2_SPRITE_SPLASH, TR2_SPRITE_BUBBLE, TR2_SPRITE_BLOOD, TR2_SPRITE_GLOW,
            TR2_SPRITE_RICOCHET, TR2_SPRITE_FLAME
        };
        for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            const Model *m = gs->level.models + types[i];
            printf("ASSET %u %d %u\n", types[i], m->count, m->start);
        }
    }

    uint32 seenRooms[16];
    unsigned seenCount = 0;
    const char *roomEnv = getenv("TR64_FX_ROOM");
    if (roomEnv && *roomEnv) {
        seenRooms[seenCount++] = (uint32)strtoul(roomEnv, NULL, 10);
    } else {
        uint32 lr = (uint32)(gs->players[0]->room - gs->rooms);
        seenRooms[seenCount++] = lr;
        for (uint8 fx = gs->tr2->effectActive; fx && seenCount < 16u; fx = gs->tr2->effects[fx - 1u].next) {
            const Room *r = gs->tr2->effects[fx - 1u].room;
            if (!r) continue;
            uint32 ri = (uint32)(r - gs->rooms);
            bool dup = false;
            for (unsigned j = 0; j < seenCount; ++j) if (seenRooms[j] == ri) dup = true;
            if (!dup) seenRooms[seenCount++] = ri;
        }
        for (unsigned c = 0; c < TR2_CREATURE_SLOTS && seenCount < 16u; ++c) {
            const ItemObj *it = gs->tr2->creatures[c].item;
            if (!it || !it->room) continue;
            uint32 ri = (uint32)(it->room - gs->rooms);
            bool dup = false;
            for (unsigned j = 0; j < seenCount; ++j) if (seenRooms[j] == ri) dup = true;
            if (!dup) seenRooms[seenCount++] = ri;
        }
    }
    bool checkCreature = getenv("TR64_FX_CREATURE") != NULL;
    for (unsigned i = 0; i < seenCount; ++i)
        dump_room(seenRooms[i], checkCreature);

    const char *shotOut = getenv("TR64_FX_SHOT_OUT");
    const char *shotTickEnv = getenv("TR64_FX_SHOT_TICK");
    bool renderAll = getenv("TR64_FX_RENDER_ALL") != NULL;
    bool takeShot = shotOut && shotTickEnv && gs->tick == (uint32)strtoul(shotTickEnv, NULL, 10);
    if (renderAll || takeShot) {
        ensure_render();
        tr64_game_view_t view;
        game_view_camera(&view);
        const char *cam = getenv("TR64_FX_CAM");
        if (cam && *cam) set_custom_camera(&view, cam);
        if (getenv("TR64_FX_SUPPRESS_EFFECTS")) {
            /* Negative control for rendered pixel comparison: hide room effects only for this shot. */
            uint8 saved = gs->tr2->effectActive;
            gs->tr2->effectActive = 0;
            tr64_render_game_scene_frame(&view, frame_buf, TR64_GAME_DRAW_CHUNK);
            gs->tr2->effectActive = saved;
        } else {
            tr64_render_game_scene_frame(&view, frame_buf, TR64_GAME_DRAW_CHUNK);
        }
        if (takeShot && sys_shot(shotOut) < 0)
            sys_error("fx probe: sys_shot(%s) failed", shotOut);
    }

    if (gs->gRandSeedLogic != logicBefore || gs->gRandSeedDraw != drawBefore ||
        gs->players[0]->pos.x != laraPosBefore.x || gs->players[0]->pos.y != laraPosBefore.y ||
        gs->players[0]->pos.z != laraPosBefore.z || gs->players[0]->health != laraHealthBefore)
        sys_error("fx probe: presentation mutated simulation state or RNG at tick %u", gs->tick);
    fflush(stdout);
}
