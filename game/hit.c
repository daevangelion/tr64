/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Source item.h:939-1012,1443-1479; common.cpp:1237-1246,1430-1456. */
#include "internal.h"

void matrixSetView(const vec3i *pos, int32 angleX, int32 angleY)
{
    int32 sx, cx, sy, cy;
    sincos(angleX, sx, cx);
    sincos(angleY, sy, cy);
    Matrix *m = gs->gMatrixPtr;
    m->e00 = cy; m->e01 = 0; m->e02 = -sy; m->e03 = 0;
    m->e10 = (sx * sy) >> 14; m->e11 = cx; m->e12 = (sx * cy) >> 14; m->e13 = 0;
    m->e20 = (cx * sy) >> 14; m->e21 = -sx; m->e22 = (cx * cy) >> 14; m->e23 = 0;
    gs->gCameraViewPos = *pos;
}

static void rotateFrame(const tr64_u16_be_t *p)
{
    uint32 lo = tr64_u16(p[0]), hi = tr64_u16(p[1]);
    matrixRotateYXZ_c(wrap_i16((hi & 0x3ff0) << 2),
        wrap_i16(((hi & 15) << 12) | ((lo & 0xfc00) >> 4)), wrap_i16((lo & 1023) << 6));
}

static void meshSphere(const Model *model, uint32 joint, vec3i origin, Sphere *out)
{
    const tr64_core_mesh_spheres_t *sphere = tr64_level_mesh_spheres(gs->core, model->start + joint);
    matrixPush_c();
    matrixTranslateRel_c(tr64_i16(sphere->x), tr64_i16(sphere->y), tr64_i16(sphere->z));
    const Matrix *m = gs->gMatrixPtr;
    out->center = vec3i_add(origin, vec3i_create(m->e03 >> 14, m->e13 >> 14, m->e23 >> 14));
    out->radius = wrap_i16(tr64_u16(sphere->radius));
    --gs->gMatrixPtr;
}

#if TR64_HAS_TR2
/* Q-INT-03: the same packed walk serves admission and the contact query.
 * Use a private stack of the engine's fixed capacity so callers' current
 * matrix depth cannot change which chains fit. Slot 0 is the inherited
 * base, slot 1 the root, and each mesh needs one temporary slot. */
const char *game_tr2_walk_packed_spheres(unsigned type, PackedFrame key,
                                         const ItemObj *item, Sphere *spheres,
                                         Matrix *stack, bool world, unsigned *joint)
{
    const Model *model = gs->level.models + type;
    unsigned slots = sizeof(gs->gMatrixStack)/sizeof(gs->gMatrixStack[0]);
    *joint = 0;
    if (model->count < 1 || model->count > 32)
        return "sphere mesh count outside 1..32";
    if (key.extent < 9)
        return "packed rotations truncated";
    unsigned count = model->count;
    if (!spheres) {
        unsigned meshes = tr64_level_table(gs->core,TR64_CORE_mesh_pointers)->count;
        unsigned spheresCount = tr64_level_table(gs->core,TR64_CORE_mesh_spheres)->count;
        unsigned nodes = tr64_level_table(gs->core,TR64_CORE_bones)->count / 4;
        if (model->start > meshes || count > meshes-model->start ||
            model->start > spheresCount || count > spheresCount-model->start)
            return "mesh/sphere reference outside table";
        if (model->nodeIndex > nodes || count-1 > nodes-model->nodeIndex)
            return "bone reference outside table";
    }
    unsigned depth = 1;
    const tr64_u16_be_t *word = key.words + 9, *end = key.words + key.extent;
    const tr64_i32_be_t *node = count > 1 ? gs->level.nodes+4*model->nodeIndex : NULL;
    Matrix *saved = gs->gMatrixPtr;
    vec3i origin = {0};
    const char *failure = NULL;
    if (spheres) {
        gs->gMatrixPtr = stack + depth;
        if (world) {
            matrixSetIdentity_c();
            origin = item->pos;
        } else {
            stack[depth] = *saved;
            stack[depth].e03 = stack[depth].e13 = stack[depth].e23 = 0;
            vec3i d = vec3i_sub(item->pos,gs->gCameraViewPos);
            matrixTranslateRel_c(d.x,d.y,d.z);
        }
        matrixRotateYXZ_c(item->angle.x,item->angle.y,item->angle.z);
        matrixTranslateRel_c(wrap_i16(tr64_u16(key.words[6])),
                             wrap_i16(tr64_u16(key.words[7])),
                             wrap_i16(tr64_u16(key.words[8])));
    }
    for (; *joint < count; ++*joint) {
        if (*joint) {
            unsigned flags = (uint32)tr64_i32(node[0]);
            if (flags & NODE_FLAG_POP) {
                if (depth == 1) {
                    failure = "sphere bone stack underflow";
                    break;
                }
                --depth;
            }
            if (flags & NODE_FLAG_PUSH) {
                /* Reserve one further slot for the sphere transform. */
                if (depth + 2 >= slots) {
                    failure = "sphere bone stack overflow";
                    break;
                }
                if (spheres)
                    stack[depth+1] = stack[depth];
                ++depth;
            }
            if (spheres) {
                gs->gMatrixPtr = stack + depth;
                matrixTranslateRel_c(wrap_i16(tr64_i32(node[1])),wrap_i16(tr64_i32(node[2])),
                                     wrap_i16(tr64_i32(node[3])));
            }
            node += 4;
        }
        word = game_tr2_step_packed(word,end,spheres != NULL);
        if (!word) {
            failure = "packed rotations truncated";
            break;
        }
        if (spheres) {
            const tr64_core_mesh_spheres_t *m = tr64_level_mesh_spheres(gs->core,model->start + *joint);
            Matrix *point = stack + depth + 1;
            *point = stack[depth];
            gs->gMatrixPtr = point;
            matrixTranslateRel_c(tr64_i16(m->x),tr64_i16(m->y),tr64_i16(m->z));
            spheres[*joint] = (Sphere){
                .center = vec3i_add(origin,vec3i_create(point->e03>>14,point->e13>>14,point->e23>>14)),
                .radius = wrap_i16(tr64_u16(m->radius))
            };
            gs->gMatrixPtr = stack + depth;
        }
    }
    if (spheres)
        gs->gMatrixPtr = saved;
    return failure;
}
#endif

int32 ItemObj_getSpheres(const ItemObj *self, Sphere *spheres, bool flag)
{
#if TR64_HAS_TR2
    if (TR64_RULE(INTERACTIONS)) {
        PackedFramePair pair = game_tr2_packed_frames(self);
        PackedFrame key = pair.delta <= (pair.rate >> 1) ? pair.a : pair.b;
        Matrix stack[sizeof(gs->gMatrixStack)/sizeof(gs->gMatrixStack[0])];
        unsigned joint;
        const char *why = game_tr2_walk_packed_spheres(self->type,key,self,spheres,stack,flag,&joint);
        if (why)
            sys_error("Tomb Raider II: admitted sphere pose changed: type=%u animation=%u frame=%u joint=%u: %s",
                      self->type,self->animIndex,self->frameIndex,joint,why);
        return gs->level.models[self->type].count;
    }
#endif
    const Model *model = gs->level.models + self->type;
    const tr64_u16_be_t *a, *b, *frame;
    int32 delta, rate;
    Matrix *old = gs->gMatrixPtr;
    vec3i origin = flag ? self->pos : vec3i_create(0, 0, 0);
    if (model->count <= 0 || model->count > 32)
        sys_error("game sphere count type=%u count=%d", self->type, model->count);
    game_frame_pair(self, &a, &b, &delta, &rate);
    frame = delta <= (rate >> 1) ? a : b;
    matrixPush_c();
    if (flag)
        matrixSetIdentity_c();
    else
    {
        vec3i d = vec3i_sub(self->pos, gs->gCameraViewPos);
        /* Source TranslateAbs replaces translation, retaining the current view basis. */
        gs->gMatrixPtr->e03 = gs->gMatrixPtr->e13 = gs->gMatrixPtr->e23 = 0;
        matrixTranslateRel_c(d.x, d.y, d.z);
    }
    matrixRotateYXZ_c(self->angle.x, self->angle.y, self->angle.z);
    matrixTranslateRel_c(wrap_i16(tr64_u16(frame[6])), wrap_i16(tr64_u16(frame[7])),
                         wrap_i16(tr64_u16(frame[8])));
    rotateFrame(frame + 10);
    meshSphere(model, 0, origin, spheres);
    for (int32 i = 1; i < model->count; ++i)
    {
        const tr64_i32_be_t *node = gs->level.nodes + (model->nodeIndex + i - 1) * 4;
        uint32 flags = (uint32)tr64_i32(node[0]);
        if (flags & NODE_FLAG_POP)
        {
            if (gs->gMatrixPtr <= old + 1)
                sys_error("game sphere bone stack underflow");
            --gs->gMatrixPtr;
        }
        if (flags & NODE_FLAG_PUSH)
            matrixPush_c();
        matrixTranslateRel_c((int16)tr64_i32(node[1]), (int16)tr64_i32(node[2]),
                             (int16)tr64_i32(node[3]));
        rotateFrame(frame + 10 + i * 2);
        meshSphere(model, (uint32)i, origin, spheres + i);
    }
    gs->gMatrixPtr = old;
    return model->count;
}

uint32 ItemObj_collideSpheres(const ItemObj *self, Lara *lara)
{
    Sphere a[32], b[32];
    int32 aCount = ItemObj_getSpheres(self, a, true);
    int32 bCount = ItemObj_getSpheres(lara, b, true);
    uint32 mask = 0;
    for (int32 i = 0; i < aCount; ++i)
    {
        if (a[i].radius <= 0)
            continue;
        for (int32 j = 0; j < bCount; ++j)
        {
            if (b[j].radius <= 0)
                continue;
            vec3i d = vec3i_sub(b[j].center, a[i].center);
            int32 r = b[j].radius + a[i].radius;
#if TR64_HAS_TR2
            if (TR64_RULE(INTERACTIONS)) {
                /* Positive radii, exact (not wrapped) strict sphere overlap. */
                if (d.x <= -r || d.x >= r || d.y <= -r || d.y >= r || d.z <= -r || d.z >= r)
                    continue;
                if ((int64_t)d.x*d.x + (int64_t)d.y*d.y + (int64_t)d.z*d.z >= (int64_t)r*r)
                    continue;
                mask |= (uint32)1 << i;
                break;
            }
#endif
            int32 dist = i32_add(i32_add(i32_mul(d.x, d.x), i32_mul(d.y, d.y)), i32_mul(d.z, d.z));
            if (dist < i32_mul(r, r))
                mask |= (uint32)1 << i;
        }
    }
    return mask;
}
