/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/item.h:895-928 and common.cpp:1359-1426 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#if TR64_HAS_SEQUEL
#include "weapon.h"
#endif
static int16 signedWord(const tr64_u16_be_t *p)
{
    uint32 n = tr64_u16(*p);
    return (int16)((int32)(n & 32767) - (int32)(n & 32768));
}
static void frameRotate(const tr64_u16_be_t *angles)
{
    uint32 lo = tr64_u16(angles[0]), hi = tr64_u16(angles[1]);
    int16 ax = (hi & 0x3ff0) << 2;
    int16 ay = ((hi & 15) << 12) | ((lo & 0xfc00) >> 4);
    int16 az = (lo & 1023) << 6;
    matrixRotateYXZ_c(ax, ay, az);
}
#if TR64_HAS_SEQUEL
static int packedRotationWidth(const tr64_u16_be_t *word)
{
    return tr64_u16(*word) >> 14 ? 1 : 2;
}

/* OpenLara src/format.h:2666–2710: the sequel starts rotations after the
 * nine-word header. Consume the stream once, not once per requested bone. */
static const tr64_u16_be_t *packedAngles(const tr64_u16_be_t *angle,
                                        const tr64_u16_be_t *limit, vec3s *out)
{
    if (angle >= limit)
        return NULL;
    int width = packedRotationWidth(angle);
    if (limit-angle < width)
        return NULL;
    if (!out)
        return angle + width;
    uint32 hi = tr64_u16(*angle++);
    int16 ax = 0, ay = 0, az = 0;
    int16 single = wrap_i16((hi & 1023u) << 6);
    switch (hi >> 14) {
    case 1: ax = single; break;
    case 2: ay = single; break;
    case 3: az = single; break;
    default: {
        uint32 lo = tr64_u16(*angle++);
        ax = wrap_i16((hi & 0x3ff0u) << 2);
        ay = wrap_i16(((hi & 15u) << 12) | ((lo & 0xfc00u) >> 4));
        az = wrap_i16((lo & 1023u) << 6);
        break;
    }
    }
    *out=vec3s_create(ax,ay,az);
    return angle;
}

const tr64_u16_be_t *game_tr2_step_packed(const tr64_u16_be_t *angle,
                                         const tr64_u16_be_t *limit, bool apply)
{
    vec3s rotation;
    const tr64_u16_be_t *next = packedAngles(angle,limit,apply ? &rotation : NULL);
    if (next && apply)
        matrixRotateYXZ_c(rotation.x,rotation.y,rotation.z);
    return next;
}

const tr64_u16_be_t *tr2FrameRotate(const tr64_u16_be_t *angle,
                                  const tr64_u16_be_t *limit)
{
    if (angle >= limit)
        sys_error("Tomb Raider II: packed pose rotation truncated");
    uint32 hi = tr64_u16(*angle++);
    unsigned mode = hi >> 14;
    if (mode) {
        int16 single = wrap_i16((hi & 1023u) << 6);
        if (single) {
            if (mode == 1) matrixRotateX_c(single);
            else if (mode == 2) matrixRotateY_c(single);
            else matrixRotateZ_c(single);
        }
        return angle;
    }
    if (angle >= limit)
        sys_error("Tomb Raider II: packed pose rotation truncated");
    uint32 lo = tr64_u16(*angle++);
    int16 ay = wrap_i16(((hi & 15u) << 12) | ((lo & 0xfc00u) >> 4));
    int16 ax = wrap_i16((hi & 0x3ff0u) << 2);
    int16 az = wrap_i16((lo & 1023u) << 6);
    if (ay) matrixRotateY_c(ay);
    if (ax) matrixRotateX_c(ax);
    if (az) matrixRotateZ_c(az);
    return angle;
}

/* tr2-lara-arms, Release-hand transform. Build only the root/torso
 * chain; the shoulder choice depends on the body fraction, not on
 * whether the flare was thrown or dropped. */
static void flareBoneTranslate(const Model *model, unsigned joint)
{
    const tr64_i32_be_t *node = gs->level.nodes + (model->nodeIndex + joint - 1) * 4;
    matrixTranslateRel_c((int16)tr64_i32(node[1]), (int16)tr64_i32(node[2]),
                         (int16)tr64_i32(node[3]));
}

const tr64_u16_be_t *skipPackedRotations(PackedFrame key, unsigned count)
{
    const tr64_u16_be_t *word = key.words + 9, *end = key.words + key.extent;
    while (count--) {
        ASSERT(word < end);
        int width = packedRotationWidth(word);
        ASSERT(end - word >= width);
        word += width;
    }
    return word;
}

static void flareTorso(const Lara *lara, const Model *model, PackedFrame key)
{
    const vec3s a = lara->angle, t = lara->extraL->torso.angle;
    matrixSetIdentity_c();
    matrixRotateYXZ_c(a.x, a.y, a.z);
    matrixTranslateRel_c(signedWord(key.words + 6), signedWord(key.words + 7),
                         signedWord(key.words + 8));
    tr2FrameRotate(key.words + 9, key.words + key.extent);
    flareBoneTranslate(model, JOINT_TORSO);
    tr2FrameRotate(skipPackedRotations(key, JOINT_TORSO), key.words + key.extent);
    matrixRotateYXZ_c(t.x, t.y, t.z);
}

static void blendFlareTorso(Matrix *a, const Matrix *b, int32 delta, int32 rate)
{
#define BLEND(member) do { \
    int64_t left = a->member, right = b->member; \
    a->member = rate == 2 ? (left + right) >> 1 : left + (right - left) * delta / rate; \
} while (0)
    BLEND(e00); BLEND(e01); BLEND(e02); BLEND(e03);
    BLEND(e10); BLEND(e11); BLEND(e12); BLEND(e13);
    BLEND(e20); BLEND(e21); BLEND(e22); BLEND(e23);
#undef BLEND
}

vec3i game_tr2_flare_hand(const Lara *lara, bool overlay)
{
    const Model *model = gs->level.models + ITEM_LARA;
    PackedFramePair body = game_tr2_packed_frames(lara);
    PackedFrame leftArm = body.a;
    if (overlay) {
        const ExtraInfoLara_Arm *arm = &lara->extraL->armL;
        ItemObj pose = {.animIndex=arm->animIndex, .frameIndex=arm->frameIndex};
        leftArm = game_tr2_packed_frames(&pose).a;
    }
    Matrix *old = gs->gMatrixPtr;
    matrixPush_c();
    Matrix *hand = gs->gMatrixPtr;
    flareTorso(lara, model, body.a);
    if (body.delta) {
        matrixPush_c();
        flareTorso(lara, model, body.b);
        blendFlareTorso(hand, gs->gMatrixPtr, body.delta, body.rate);
        gs->gMatrixPtr = hand;
    }
    flareBoneTranslate(model, body.delta ? JOINT_ARM_R1 : JOINT_ARM_L1);
    const tr64_u16_be_t *limit = leftArm.words + leftArm.extent;
    const tr64_u16_be_t *p = skipPackedRotations(leftArm, JOINT_ARM_L1);
    p = tr2FrameRotate(p, limit);
    flareBoneTranslate(model, JOINT_ARM_L2);
    p = tr2FrameRotate(p, limit);
    flareBoneTranslate(model, JOINT_ARM_L3);
    tr2FrameRotate(p, limit);
    matrixTranslateRel_c(-16, 32, 42);
    vec3i point = vec3i_add(lara->pos, vec3i_create(hand->e03 >> 14, hand->e13 >> 14, hand->e23 >> 14));
    gs->gMatrixPtr = old;
    return point;
}
#endif
static void frameRoot(const tr64_u16_be_t *frame)
{
    matrixTranslateRel_c(signedWord(frame + 6), signedWord(frame + 7), signedWord(frame + 8));
    frameRotate(frame + 10);
}
static void boneTransform(const Model *model, uint32 bone)
{
    const tr64_i32_be_t *node = gs->level.nodes + (model->nodeIndex + bone) * 4;
    uint32 flags = (uint32)tr64_i32(node[0]);
    if (flags & NODE_FLAG_POP)
    {
        if (gs->gMatrixPtr <= gs->gMatrixStack)
            sys_error("game bone stack underflow");
        --gs->gMatrixPtr;
    }
    if (flags & NODE_FLAG_PUSH)
        matrixPush_c();
    matrixTranslateRel_c((int16)tr64_i32(node[1]), (int16)tr64_i32(node[2]),
                         (int16)tr64_i32(node[3]));
}
static void boneFrame(const Model *model, uint32 bone, const tr64_u16_be_t *angles)
{
    boneTransform(model,bone);
    frameRotate(angles);
}
vec3i ItemObj_getJoint(const ItemObj *self, int32 jointIndex, const vec3i *offset)
{
    const Model *model = gs->level.models + self->type;
    const tr64_u16_be_t *a, *b, *frame;
    int32 delta, rate, i;
    Matrix *old = gs->gMatrixPtr, *m;
    vec3i result;
    if (jointIndex < 0 || jointIndex >= model->count)
        sys_error("game joint index");
#if TR64_HAS_SEQUEL
    const bool packed = TR64_RULE(PACKED_POSE);
    unsigned extent = 0;
    if (packed) {
        PackedFramePair pair = game_tr2_packed_frames(self);
        PackedFrame key = pair.delta <= (pair.rate >> 1) ? pair.a : pair.b;
        frame = key.words;
        extent = key.extent;
    } else
#endif
    {
        game_frame_pair(self, &a, &b, &delta, &rate);
        frame = delta <= (rate >> 1) ? a : b;
    }
    matrixPush_c();
    matrixSetIdentity_c();
    matrixRotateYXZ_c(self->angle.x, self->angle.y, self->angle.z);
#if TR64_HAS_SEQUEL
    if (packed) {
        const tr64_u16_be_t *limit = frame + extent;
        matrixTranslateRel_c(signedWord(frame + 6), signedWord(frame + 7), signedWord(frame + 8));
        const tr64_u16_be_t *angle = tr2FrameRotate(frame + 9,limit);
        for (i = 0; i < jointIndex; ++i) {
            boneTransform(model,(uint32)i);
            angle = tr2FrameRotate(angle,limit);
        }
    } else
#endif
    {
        frameRoot(frame);
        for (i = 0; i < jointIndex; ++i)
            boneFrame(model, (uint32)i, frame + 10 + (i + 1) * 2);
    }
    matrixTranslateRel_c(offset->x, offset->y, offset->z);
    m = gs->gMatrixPtr;
    result = vec3i_create(m->e03 >> 14, m->e13 >> 14, m->e23 >> 14);
    gs->gMatrixPtr = old;
    return result;
}

#if TR64_HAS_SEQUEL
/* Q-T2K-09: each local axis uses II's quarter-wave table and one
 * signed rounding after the pair of products. */
static void combatRotatePair(int32 *a,int32 *b,int32 s,int32 c)
{
    int32 next=(*a*c-*b*s)>>14;
    *b=(*a*s+*b*c)>>14;
    *a=next;
}

#define COMBAT_ROTATE(a,b,angle) do { \
    int32 s=tr2_sin(angle),c=tr2_sin((angle)+ANGLE_90); \
    combatRotatePair(&m->e0##a,&m->e0##b,s,c); \
    combatRotatePair(&m->e1##a,&m->e1##b,s,c); \
    combatRotatePair(&m->e2##a,&m->e2##b,s,c); \
} while (0)

static void combatRotate(vec3s a)
{
    Matrix *m=gs->gMatrixPtr;
    if (a.y) COMBAT_ROTATE(0,2,a.y);
    if (a.x) COMBAT_ROTATE(2,1,a.x);
    if (a.z) COMBAT_ROTATE(1,0,a.z);
}
#undef COMBAT_ROTATE

typedef struct {
    const Model *model;
    const tr64_u16_be_t *p, *end;
    Matrix *base;
    int16 jointYaw;
    int16 rexYaw[2];
} PackedSphereCursor;

static PackedSphereCursor beginSpheres(const ItemObj *item, int16 jointYaw)
{
    const Model *model=gs->level.models+item->type;
    if (model->count<=0 || model->count>32)
        sys_error("Tomb Raider II: invalid sphere count for model %u",item->type);
    PackedFramePair pair=game_tr2_packed_frames(item);
    PackedFrame key=pair.delta>pair.rate/2 ? pair.b : pair.a;
    combatRotate(item->angle);
    matrixTranslateRel_c(signedWord(key.words+6),signedWord(key.words+7),signedWord(key.words+8));
    const TR2Creature *rex=item->type==TR2_ITEM_REX ? game_tr2_creature_slot(item) : NULL;
    return (PackedSphereCursor){model,key.words+9,key.words+key.extent,gs->gMatrixPtr,jointYaw,
                               {rex ? rex->jointYaw[0] : 0, rex ? rex->jointYaw[1] : 0}};
}

static void nextCombatJoint(PackedSphereCursor *c,unsigned i)
{
    if (i) {
        const tr64_i32_be_t *node=gs->level.nodes+(c->model->nodeIndex+i-1)*4;
        if ((tr64_i32(node[0])&NODE_FLAG_POP) && gs->gMatrixPtr==c->base)
            sys_error("Tomb Raider II: sphere skeleton underflow for model %u",c->model->type);
        boneTransform(c->model,i-1);
    }
    vec3s authored;
    c->p=packedAngles(c->p,c->end,&authored);
    combatRotate(authored);
    /* Q-T2K-02: both kinds of spheres use the tiger's live extra yaw. */
    if (i==TR2_TIGER_HEAD_JOINT && c->jointYaw) combatRotate(vec3s_create(0,c->jointYaw,0));
    if (i>=11 && i<=12 && c->rexYaw[i-11]) combatRotate(vec3s_create(0,c->rexYaw[i-11],0));
}

unsigned game_tr2_joint_origins(const ItemObj *item,vec3i out[32])
{
    Matrix *old=gs->gMatrixPtr;
    matrixPush_c();
    matrixSetIdentity_c();
    PackedSphereCursor pose=beginSpheres(item,0);
    unsigned count=pose.model->count;
    for (unsigned i=0;i<count;++i) {
        nextCombatJoint(&pose,i);
        const Matrix *m=gs->gMatrixPtr;
        out[i]=vec3i_create(i32_add(item->pos.x,m->e03>>14),
                            i32_add(item->pos.y,m->e13>>14),
                            i32_add(item->pos.z,m->e23>>14));
    }
    gs->gMatrixPtr=old;
    return count;
}

/* Q-T2K-13: injury blood uses the nearest pre-advance contact pose,
 * including enabled live rotations and the attacker's room. */
vec3i game_tr2_combat_joint(const ItemObj *item,unsigned joint,vec3i offset,int16 extraYaw)
{
    Matrix *old=gs->gMatrixPtr;
    matrixPush_c();
    matrixSetIdentity_c();
    PackedSphereCursor pose=beginSpheres(item,extraYaw);
    if (joint>=(unsigned)pose.model->count)
        sys_error("Tomb Raider II: injury joint %u outside creature model %u",joint,item->type);
    for (unsigned i=0;i<=joint;++i) nextCombatJoint(&pose,i);
    const Matrix *m=gs->gMatrixPtr;
    vec3i pos=vec3i_create(
        i32_add(item->pos.x,(m->e03+(int64_t)m->e00*offset.x+(int64_t)m->e01*offset.y+(int64_t)m->e02*offset.z)>>14),
        i32_add(item->pos.y,(m->e13+(int64_t)m->e10*offset.x+(int64_t)m->e11*offset.y+(int64_t)m->e12*offset.z)>>14),
        i32_add(item->pos.z,(m->e23+(int64_t)m->e20*offset.x+(int64_t)m->e21*offset.y+(int64_t)m->e22*offset.z)>>14));
    gs->gMatrixPtr=old;
    return pos;
}

static int32 nextSphere(PackedSphereCursor *c,unsigned i,int64_t result[3])
{
    nextCombatJoint(c,i);
    const tr64_core_mesh_spheres_t *s=tr64_level_mesh_spheres(gs->core,c->model->start+i);
    int32 radius=wrap_i16(tr64_u16(s->radius));
    if (radius>0) {
        int32 x=tr64_i16(s->x),y=tr64_i16(s->y),z=tr64_i16(s->z);
        const Matrix *m=gs->gMatrixPtr;
        result[0]=m->e03+(int64_t)m->e00*x+(int64_t)m->e01*y+(int64_t)m->e02*z;
        result[1]=m->e13+(int64_t)m->e10*x+(int64_t)m->e11*y+(int64_t)m->e12*z;
        result[2]=m->e23+(int64_t)m->e20*x+(int64_t)m->e21*y+(int64_t)m->e22*z;
    }
    return radius;
}

static unsigned worldSpheres(const ItemObj *item,int16 head,Sphere out[32])
{
    Matrix *old=gs->gMatrixPtr;
    matrixPush_c();
    matrixSetIdentity_c();
    PackedSphereCursor pose=beginSpheres(item,head);
    unsigned count=pose.model->count;
    for (unsigned i=0;i<count;++i) {
        int64_t center[3];
        out[i].radius=nextSphere(&pose,i,center);
        if (out[i].radius>0)
            out[i].center=vec3i_create(i32_add(item->pos.x,center[0]>>14),
                                       i32_add(item->pos.y,center[1]>>14),
                                       i32_add(item->pos.z,center[2]>>14));
    }
    gs->gMatrixPtr=old;
    return count;
}

uint32 game_tr2_creature_touch(const ItemObj *item,const Lara *lara)
{
    const TR2Creature *ai=game_tr2_creature_slot(item);
    Sphere body[32], other[32];
    unsigned n=worldSpheres(item,item->type==TR2_ITEM_TIGER && ai ? ai->jointYaw[0] : 0,body);
    unsigned m=worldSpheres(lara,0,other);
    uint32 touch=0;
    for (unsigned i=0;i<n;++i) {
        if (body[i].radius<=0) continue;
        for (unsigned j=0;j<m;++j) {
            if (other[j].radius<=0) continue;
            int32 radius=body[i].radius+other[j].radius;
            int64_t x=(int64_t)body[i].center.x-other[j].center.x;
            int64_t y=(int64_t)body[i].center.y-other[j].center.y;
            int64_t z=(int64_t)body[i].center.z-other[j].center.z;
            if (llabs(x)<radius && llabs(y)<radius && llabs(z)<radius &&
                x*x+y*y+z*z<(int64_t)radius*radius) { touch|=1u<<i; break; }
        }
    }
    return touch;
}

/* INT64_MAX names a miss so a strict minimum retains the earlier mesh. */
int64_t game_tr2_sphere_entry(int64_t x, int64_t y, int64_t z, int32 radius)
{
    if (radius <= 0 || x <= -radius || x >= radius || y <= -radius || y >= radius ||
        z <= radius || x*x+y*y > (int64_t)radius*radius)
        return INT64_MAX;
    return z-radius;
}

/* Pistol lock and creature strikes, tr2-lara-arms.md. Keep the ray
 * basis through the pose; round only once at each sphere centre. */
bool game_tr2_ray_hit(const ItemObj *item, const vec3i *origin, int16 pitch, int16 yaw, vec3i *point)
{
    int32 sy=tr2_sin(yaw), cy=tr2_sin(yaw+ANGLE_90);
    int32 sx=tr2_sin(pitch), cx=tr2_sin(pitch+ANGLE_90);
    Matrix *old = gs->gMatrixPtr;
    matrixPush_c();
    Matrix *base = gs->gMatrixPtr;
    *base = (Matrix){.e00=cy,.e02=-sy,
        .e10=(sx*sy)>>14,.e11=cx,.e12=(sx*cy)>>14,
        .e20=(cx*sy)>>14,.e21=-sx,.e22=(cx*cy)>>14};
    const vec3i forward = vec3i_create(base->e20,base->e21,base->e22);
    const int64_t x = (int64_t)item->pos.x-origin->x;
    const int64_t y = (int64_t)item->pos.y-origin->y;
    const int64_t z = (int64_t)item->pos.z-origin->z;
    const int64_t rx = base->e00*x + base->e02*z;
    const int64_t ry = base->e10*x + base->e11*y + base->e12*z;
    const int64_t rz = base->e20*x + base->e21*y + base->e22*z;
    const TR2Creature *ai=game_tr2_creature_slot(item);
    PackedSphereCursor pose=beginSpheres(item,item->type==TR2_ITEM_TIGER && ai ? ai->jointYaw[0] : 0);
    int64_t best = INT64_MAX;
    for (unsigned i=0;i<(unsigned)pose.model->count;++i) {
        int64_t p[3];
        const int32 radius=nextSphere(&pose,i,p);
        if (radius<=0) continue;
        int64_t a=(rx+p[0])>>14, b=(ry+p[1])>>14, d=(rz+p[2])>>14;
        int64_t entry = game_tr2_sphere_entry(a,b,d,radius);
        if (entry < best)
            best = entry;
    }
    gs->gMatrixPtr = old;
    if (best == INT64_MAX)
        return false;
    *point = vec3i_create(i32_add(origin->x,wrap_i32((uint32_t)((forward.x*best)>>14))),
                          i32_add(origin->y,wrap_i32((uint32_t)((forward.y*best)>>14))),
                          i32_add(origin->z,wrap_i32((uint32_t)((forward.z*best)>>14))));
    return true;
}

vec3i __attribute__((optimize("Os"))) game_tr2_lara_right_hand(const Lara *lara, const ItemObj *pose, vec3i offset)
{
    const Model *model = gs->level.models + ITEM_LARA;
    PackedFramePair body = game_tr2_packed_frames(lara);
    PackedFrame rightArm = pose ? game_tr2_packed_frames(pose).a : body.a;
    Matrix *old = gs->gMatrixPtr;
    matrixPush_c();
    Matrix *hand = gs->gMatrixPtr;
    flareTorso(lara, model, body.a);
    if (body.delta) {
        matrixPush_c();
        flareTorso(lara, model, body.b);
        blendFlareTorso(hand, gs->gMatrixPtr, body.delta, body.rate);
        gs->gMatrixPtr = hand;
    }
    flareBoneTranslate(model, JOINT_ARM_R1);
    const tr64_u16_be_t *limit = rightArm.words + rightArm.extent;
    const tr64_u16_be_t *p = skipPackedRotations(rightArm, JOINT_ARM_R1);
    p = tr2FrameRotate(p, limit);
    flareBoneTranslate(model, JOINT_ARM_R2);
    p = tr2FrameRotate(p, limit);
    flareBoneTranslate(model, JOINT_ARM_R3);
    tr2FrameRotate(p, limit);
    matrixTranslateRel_c(offset.x, offset.y, offset.z);
    vec3i point = vec3i_add(lara->pos, vec3i_create(hand->e03 >> 14, hand->e13 >> 14, hand->e23 >> 14));
    gs->gMatrixPtr = old;
    return point;
}
#endif
