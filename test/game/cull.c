/* SPDX-License-Identifier: MIT */
/* Host-only proof over production Core, game poses and actual resident MG64 vertices. */
#ifdef TR64_HOST
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/common/internal.h"
#include "game/common/cull.h"
#include "engine/render/internal.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/engine.h"
#include "tr64/route.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"

typedef struct {
    tr64_geo_t meshes;
    uint32 radii[512], slots;
    uint16 objects[512];
} Visuals;
static Visuals visual;
static tr64_game_drawable_t parts[128];
static uint32 samples, bounded, bypassed, vertices, flashes, aimed, hits, extra, lerped;
static uint8 seen_types[191], seen_slots[512];

static uint32 ceil_root(uint64_t square)
{
    uint32 lo = 0, hi = 65536;
    while (lo < hi)
    {
        uint32 mid = lo + (hi - lo) / 2;
        if ((uint64_t)mid * mid < square) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static const uint8_t *vertex(const tr64_geo_t *g, const uint8_t *batch, uint32 index)
{
    return g->bytes.data + u32(batch + 16) + (index / 2) * 32 + (index & 1) * 8;
}

static int radius_query(void *context, uint32 slot, int32 *radius)
{
    const Visuals *v = context;
    if (slot >= v->slots || v->objects[slot] == UINT16_MAX) return 0;
    *radius = (int32)v->radii[slot];
    return 1;
}

static int unknown_query(void *context, uint32 slot, int32 *radius)
{
    (void)context; (void)slot; (void)radius;
    return 0;
}

static int negative_query(void *context, uint32 slot, int32 *radius)
{
    (void)context; (void)slot; *radius = -1; return 1;
}

static void prepare_visuals(void)
{
    tr64_mem_stats_t memory;
    tr64_render_error_t error;
    mem_level_stats(TR64_LEVEL_meshes, &memory);
    tr64_load_scope_begin();
    if (tr64_geo_open(&visual.meshes, (tr64_gpu_span_t){memory.base, memory.used}, 1, &error))
        sys_error("cull proof MG64 open code=%u offset=%u", error.code, error.offset);
    tr64_load_scope_end();
    visual.slots = tr64_level_table(gs->core, TR64_CORE_mesh_pointers)->count;
    if (visual.slots > 512) sys_error("cull proof mesh slot bound");
    for (uint32 slot = 0; slot < visual.slots; ++slot)
    {
        uint32 id = mesh_index(&visual.meshes, tr64_u32(*tr64_level_mesh_pointers(gs->core, slot)));
        uint64_t maximum = 0;
        visual.objects[slot] = id == UINT_MAX ? UINT16_MAX : (uint16)id;
        if (id == UINT_MAX) continue;
        const uint8_t *o = object(&visual.meshes, id);
        for (uint32 bi = u32(o + 4); bi < u32(o + 4) + u16(o + 8); ++bi)
        {
            const uint8_t *batch = visual.meshes.bytes.data + visual.meshes.batch_offset + 32 * bi;
            for (uint32 vi = 0; vi < u16(batch + 12); ++vi)
            {
                const uint8_t *p = vertex(&visual.meshes, batch, vi);
                int64_t x = s16(p), y = s16(p + 2), z = s16(p + 4);
                uint64_t square = (uint64_t)(x*x + y*y + z*z);
                if (square > maximum) maximum = square;
            }
        }
        visual.radii[slot] = ceil_root(maximum);
    }
    for (uint32 i = 0; i < 4096; ++i)
    {
        int32 s = wrap_i32(gSinCosTable[i]) >> 16, c = wrap_i16(gSinCosTable[i]);
        if ((int64_t)s*s + (int64_t)c*c >= (int64_t)16385*16385)
            sys_error("cull proof rotation-table norm index=%u", i);
    }
}

static void check_part(const tr64_game_drawable_t *part, vec3i center, int32 radius, uint32 tick)
{
    uint32 slot = part->mesh_slot;
    if (slot >= visual.slots || visual.objects[slot] == UINT16_MAX)
        sys_error("cull proof emitted mesh not in MG64 slot=%u", slot);
    seen_slots[slot] = 1;
    if (part->bone >= JOINT_MAX &&
        gs->items[part->instance_id - 0x80000000u].type == ITEM_LARA) ++flashes;
    const uint8_t *o = object(&visual.meshes, visual.objects[slot]);
    for (uint32 bi = u32(o + 4); bi < u32(o + 4) + u16(o + 8); ++bi)
    {
        const uint8_t *batch = visual.meshes.bytes.data + visual.meshes.batch_offset + 32 * bi;
        for (uint32 vi = 0; vi < u16(batch + 12); ++vi)
        {
            const uint8_t *p = vertex(&visual.meshes, batch, vi);
            int32 local[3] = {s16(p), s16(p + 2), s16(p + 4)};
            int64_t d[3];
            for (uint32 axis = 0; axis < 3; ++axis)
            {
                const int32 *b = part->basis + axis * 3;
                int32 origin = axis == 0 ? center.x : axis == 1 ? center.y : center.z;
                d[axis] = (int64_t)part->translation[axis] - origin +
                          (((int64_t)b[0]*local[0] + (int64_t)b[1]*local[1] +
                            (int64_t)b[2]*local[2]) >> 14);
            }
            if ((uint64_t)(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]) > (uint64_t)radius*radius)
                sys_error("cull containment FAIL tick=%u item=%u type=%u bone=%u slot=%u radius=%d vertex=%d,%d,%d delta=%lld,%lld,%lld",
                          tick, part->instance_id - 0x80000000u,
                          gs->items[part->instance_id - 0x80000000u].type, part->bone,
                          slot, radius, local[0], local[1], local[2],
                          (long long)d[0], (long long)d[1], (long long)d[2]);
            ++vertices;
        }
    }
}

static void sample(uint32 tick, int negative)
{
    uint32 hash = game_state_hash();
    Matrix *matrix = gs->gMatrixPtr;
    for (uint32 room = 0; room < gs->level.roomsCount; ++room)
    {
        uint32 cursor = 0;
        while (cursor != TR64_GAME_VIEW_END)
        {
            uint32 count = game_view_room(room, &cursor, parts, 128);
            if (count > 128) sys_error("cull proof group exceeds caller buffer");
            for (uint32 begin = 0; begin < count;)
            {
                uint32 end = begin + 1, index = parts[begin].instance_id - 0x80000000u;
                while (end < count && parts[end].instance_id == parts[begin].instance_id) ++end;
                if (index >= MAX_ITEMS) sys_error("cull proof item identity");
                const ItemObj *item = gs->items + index;
                vec3i center = {123, 456, 789}; int32 radius = -1;
                ++samples;
                if (!game_item_cull_sphere(item, radius_query, &visual, &center, &radius))
                {
                    if (center.x != 123 || center.y != 456 || center.z != 789 || radius != -1)
                        sys_error("cull proof bypass wrote outputs");
                    ++bypassed; begin = end; continue;
                }
                ++bounded; seen_types[item->type] = 1;
                const tr64_u16_be_t *a, *b; int32 delta, rate;
                game_frame_pair(item, &a, &b, &delta, &rate); lerped += delta != 0;
                if (item->type == ITEM_LARA)
                {
                    aimed += item->extraL->armR.useBasis || item->extraL->armL.useBasis;
                    hits += item->extraL->hitQuadrant != -1;
                }
                else if (item->type >= ITEM_DOPPELGANGER && item->type <= ITEM_ADAM && item->extraE)
                    extra += item->extraE->rotHead != 0 || item->extraE->rotNeck != 0;
                if (negative && item->type == ITEM_LARA) radius = 0;
                for (uint32 part = begin; part < end; ++part)
                    if (parts[part].kind == TR64_GAME_DRAW_MESH)
                        check_part(parts + part, center, radius, tick);
                begin = end;
            }
        }
    }
    if (gs->gMatrixPtr != matrix || game_state_hash() != hash)
        sys_error("cull proof view/query changed simulation or RNG");
}

static void reject_tests(void)
{
    ItemObj item = *gs->players[0];
    vec3i center = {123,456,789}; int32 radius = -1;
    if (game_item_cull_sphere(&item, unknown_query, NULL, &center, &radius) ||
        center.x != 123 || center.y != 456 || center.z != 789 || radius != -1)
        sys_error("cull proof unknown visual radius did not preserve item");
    if (game_item_cull_sphere(&item, negative_query, NULL, &center, &radius) ||
        center.x != 123 || center.y != 456 || center.z != 789 || radius != -1)
        sys_error("cull proof negative visual radius did not preserve item");
    item.pos.x = INT32_MAX;
    if (game_item_cull_sphere(&item, radius_query, &visual, &center, &radius) || radius != -1)
        sys_error("cull proof world-wrap risk did not preserve item");
}

static int unit_radius(void *context, uint32 slot, int32 *radius)
{
    (void)context; (void)slot; *radius = 100; return 1;
}

static void root_node_test(void)
{
    GameLevel saved = gs->level;
    Model models[191] = {{0}};
    Anim anim = {0};
    tr64_u16_be_t frame[14] = {{0}};
    tr64_i32_be_t nodes[4] = {{0}};
    ItemObj item = {0};
    vec3i center = {123,456,789}; int32 radius = -1;
    models[ITEM_TRAP_FLOOR].count = 2;
    anim.frame_rate = 1;
    frame[6].raw = tr64_core_be16(1000);
    /* Effective i16 node translation is 500, not the full source i32 word. */
    nodes[1].raw = tr64_core_be32(65536u + 500u);
    gs->level.models = models; gs->level.anims = &anim;
    gs->level.animFrames = frame; gs->level.nodes = nodes;
    item.type = ITEM_TRAP_FLOOR; item.visibleMask = 3;
    if (!game_item_cull_sphere(&item, unit_radius, NULL, &center, &radius) ||
        center.x || center.y || center.z || radius < 1500 || radius > 2000)
        sys_error("cull proof root/node reach or effective-i16 translation");
    radius = -1; center = vec3i_create(123,456,789);
    frame[6].raw = tr64_core_be16(32768);
    if (game_item_cull_sphere(&item, unit_radius, NULL, &center, &radius) ||
        radius != -1 || center.x != 123 || center.y != 456 || center.z != 789)
        sys_error("cull proof oversized Q14 arithmetic was not bypassed");
    /* Core count128 would occupy byte0x80, i.e. INT8_MIN in Model.count.
     * Poison pose spans to prove rejection occurs before any frame/bone read. */
    models[ITEM_TRAP_FLOOR].count = INT8_MIN;
    gs->level.anims = NULL; gs->level.animFrames = NULL; gs->level.nodes = NULL;
    radius = -1; center = vec3i_create(123,456,789);
    if (game_item_cull_sphere(&item, unit_radius, NULL, &center, &radius) ||
        radius != -1 || center.x != 123 || center.y != 456 || center.z != 789)
        sys_error("cull proof encoded128 count was not bypassed without writes");
    gs->level = saved;
}

static void swapped_mesh_test(void)
{
    Lara *lara = gs->players[0];
    uint16 saved = lara->extraL->meshes[0], chosen = saved;
    uint32 maximum = visual.radii[saved], hash = game_state_hash();
    vec3i center; int32 radius;
    for (uint32 slot = 0; slot < visual.slots; ++slot)
    {
        if (visual.radii[slot] <= maximum) continue;
        lara->extraL->meshes[0] = (uint16)slot;
        if (game_item_cull_sphere(lara, radius_query, &visual, &center, &radius))
        {
            chosen = (uint16)slot;
            maximum = visual.radii[slot];
        }
    }
    lara->extraL->meshes[0] = chosen;
    if (chosen == saved) sys_error("cull proof has no adversarial visual mesh swap");
    sample(0, 0); /* Actual MG64 vertices through the production pose, not a Core radius. */
    lara->extraL->meshes[0] = saved;
    if (game_state_hash() != hash) sys_error("cull proof mesh-swap unit changed simulation");
}

void tr64_level_reload_probe(const char *level)
{
    const char *path = getenv("TR64_CULL_INPUT");
    uint16 masks[600]; uint32 count = 0;
    int negative = getenv("TR64_CULL_NEGATIVE") != NULL, placed = 0;
    int32 room = 0, x = 0, y = 0, z = 0, angle = 0;
    if (!path) sys_error("cull proof requires TR64_CULL_INPUT");
    FILE *input = fopen(path, "r"); char word[128];
    if (!input) sys_error("cull proof input open");
    while (fscanf(input, "%127s", word) == 1)
    {
        if (!strcmp(word, "start"))
        {
            if (count || placed || fscanf(input, "%d%d%d%d%d", &room, &x, &y, &z, &angle) != 5)
                sys_error("cull proof start directive");
            placed = 1;
        }
        else
        {
            char *end; unsigned long mask; errno = 0;
            if (word[0] == '-' || word[0] == '+') sys_error("cull proof signed mask");
            mask = strtoul(word, &end, word[0] == '0' && (word[1] == 'x' || word[1] == 'X') ? 16 : 10);
            if (errno || !*word || *end || mask > 4095 || (mask & 1025) || count == 600)
                sys_error("cull proof input mask");
            masks[count++] = (uint16)mask;
        }
    }
    if (ferror(input) || fclose(input) || count != 600) sys_error("cull proof needs exactly600 masks");
    mem_level_begin(); code_loading_begin(); tr64_render_scene_set_live(1);
    tr64_scene_load(level); code_loading_end();
    game_init(); game_level_start(tr64_level_current(), level);
    if (placed && (room < 0 || !game_place((uint32)room, x, y, z, wrap_i16((uint32)angle))))
        sys_error("cull proof invalid placement");
    prepare_visuals(); root_node_test(); reject_tests();
    if (!negative) swapped_mesh_test();
    sample(0, negative);
    for (uint32 tick = 1; tick <= 600; ++tick)
    {
        game_tick(masks[tick-1]);
        if (game_stop_info()->kind != TR64_GAME_STOP_NONE)
            sys_error("cull proof source boundary tick=%u kind=%u name=%s", tick,
                      game_stop_info()->kind, game_stop_info()->name);
        sample(tick, negative);
    }
    uint32 types = 0, slots = 0;
    for (uint32 i = 0; i < 191; ++i) types += seen_types[i];
    for (uint32 i = 0; i < 512; ++i) slots += seen_slots[i];
    if (!bounded || !vertices) sys_error("cull proof no actual posed vertices checked");
    log_printf("cull proof PASS level=%s ticks=600 samples=%u bounded=%u bypassed=%u vertices=%u types=%u slots=%u aimed=%u flashes=%u hits=%u enemy_extra=%u lerped=%u mesh_swap=1\n",
               level, samples, bounded, bypassed, vertices, types, slots, aimed, flashes, hits, extra, lerped);
}
#endif
