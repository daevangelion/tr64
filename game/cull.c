/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/draw.h:644-790 and common.cpp matrix routines at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "cull.h"
#include <limits.h>

static uint32 magnitude(int32 value)
{
    return (uint32)(value < 0 ? -value : value);
}

static uint32 root_length(const tr64_u16_be_t *frame)
{
    return magnitude(wrap_i16(tr64_u16(frame[6]))) +
           magnitude(wrap_i16(tr64_u16(frame[7]))) +
           magnitude(wrap_i16(tr64_u16(frame[8])));
}

bool game_item_cull_sphere(const ItemObj *item, GameCullMeshRadius query, void *context,
                           vec3i *center, int32 *radius)
{
    const Model *model;
    const tr64_u16_be_t *a, *b;
    int32 delta, rate, visual;
    uint32 reach, leaf = 0, norm, extent;
    bool lara;
    if (!gs || !item || !query || !center || !radius)
        return false;
    model = gs->level.models + item->type;
    /* Model.count is int8: rejecting nonpositive values also proves n<=127. */
    if (model->count <= 0 || item->type == ITEM_MUZZLE_FLASH)
        return false;
    lara = item->type == ITEM_LARA;
    if (lara && model->count != JOINT_MAX)
        return false;
    if (lara && item->extraL->hitQuadrant != -1)
    {
        ItemObj pose = *item;
        static const uint16 hit[] = {Lara_ANIM_HIT_FRONT, Lara_ANIM_HIT_LEFT,
                                    Lara_ANIM_HIT_BACK, Lara_ANIM_HIT_RIGHT};
        int32 quadrant = item->extraL->hitQuadrant;
        if (quadrant < 0 || quadrant > 3)
            return false;
        pose.animIndex = hit[quadrant];
        pose.frameIndex = tr64_u16(gs->level.anims[pose.animIndex].frame_start) +
                          item->extraL->hitFrame;
        game_frame_pair(&pose, &a, &b, &delta, &rate);
    }
    else
        game_frame_pair(item, &a, &b, &delta, &rate);
    reach = root_length(a);
    if (delta)
    {
        uint32 next = root_length(b);
        if (next > reach)
            reach = next;
        reach += 3; /* Componentwise arithmetic-shift rounding of the root lerp. */
    }
    for (int32 joint = 0; joint < model->count; ++joint)
    {
        if (joint)
        {
            const tr64_i32_be_t *node = gs->level.nodes + (model->nodeIndex + joint - 1) * 4;
            /* The production pose truncates these source i32 words to i16. All
             * branches are included, so POP/PUSH cannot increase this sum. */
            reach += magnitude(wrap_i16((uint32)tr64_i32(node[1]))) +
                     magnitude(wrap_i16((uint32)tr64_i32(node[2]))) +
                     magnitude(wrap_i16((uint32)tr64_i32(node[3])));
        }
        if (reach > 32763)
            return false;
        if (lara || (joint < 32 && (item->visibleMask & (1u << joint))))
        {
            uint32 slot = lara ? item->extraL->meshes[joint] : model->start + joint;
            if (!query(context, slot, &visual) || visual < 0 || visual > 32763)
                return false;
            if ((uint32)visual > leaf)
                leaf = (uint32)visual;
        }
    }
    if (lara)
    {
        for (uint32 arm = 0; arm < 2; ++arm)
        {
            const ExtraInfoLara_Arm *current = arm ? &item->extraL->armL : &item->extraL->armR;
            if (!current->flash.timer)
                continue;
            if (!query(context, gs->level.models[ITEM_MUZZLE_FLASH].start, &visual) ||
                visual < 0 || visual > 32763)
                return false;
            visual += (int32)magnitude(current->flash.offset) + 55;
            if ((uint32)visual > leaf)
                leaf = (uint32)visual;
        }
    }
    reach += leaf;
    if (reach > 32763)
        return false;
    /* gSinCosTable has s*s+c*c < 16385^2. An axis rotation therefore has
     * norm <=16385/16384; rounding its six entries adds Frobenius norm <3.
     * While the Q14 matrix norm is <=32768, each axis adds at most 5.
     * A frame lerp is convex and its nine rounded entries add at most 3.
     * There are at most 6*n+15 axes and n lerps on any emitted path: this
     * covers ordinary/extra enemy rotations, Lara head/torso/aim bases and
     * hand flashes. With n<=127, 16384+5*(6*n+15)+3*n stays below 32768,
     * proving the invariant rather than assuming perfectly rigid matrices.
     * Reach is a sum of L1 translation lengths plus a visual-origin radius.
     * Reject oversized arithmetic: keeping its scaled extent <32764 also
     * keeps Q14 translation/interpolation sums in the source's i32 range.
     * Four world units cover separate translation and vertex rounding. */
    norm = 16459u + 33u * (uint32)model->count;
    extent = (reach * norm + 16383u) >> 14;
    if (extent > 32763)
        return false;
    extent += 4;
    if (item->pos.x < INT32_MIN + (int32)extent || item->pos.x > INT32_MAX - (int32)extent ||
        item->pos.y < INT32_MIN + (int32)extent || item->pos.y > INT32_MAX - (int32)extent ||
        item->pos.z < INT32_MIN + (int32)extent || item->pos.z > INT32_MAX - (int32)extent)
        return false;
    *center = item->pos;
    *radius = (int32)extent;
    return true;
}
