/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/draw.h and src/ui.h, format.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/draw.h:81-116,532-790,793-889,1075-1095,
 * lara.h:4238-4259 and common.cpp:1389-1456 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "tr64/code.h"
enum { NODE_FLAG_ROTX = 1u << 2, NODE_FLAG_ROTY = 1u << 3, NODE_FLAG_ROTZ = 1u << 4 };
static int16 frameWord(const tr64_u16_be_t *p)
{
    uint32 n = tr64_u16(*p);
    return (int16)((int32)(n & 32767) - (int32)(n & 32768));
}
static int16 rootWordLerp(const tr64_u16_be_t *a, const tr64_u16_be_t *b, int32 t)
{
    int32 x = frameWord(a), y = frameWord(b);
    return wrap_i16((uint32)x + (uint32)(i32_mul(y - x, t) >> 16));
}
static void frameRotate(const tr64_u16_be_t *p)
{
    uint32 lo = tr64_u16(p[0]), hi = tr64_u16(p[1]);
    matrixRotateYXZ_c((int16)((hi & 0x3ff0) << 2),
                      (int16)(((hi & 15) << 12) | ((lo & 0xfc00) >> 4)), (int16)((lo & 1023) << 6));
}
static void frameLerp(vec3s pos, const tr64_u16_be_t *a, const tr64_u16_be_t *b, int32 delta,
                      int32 rate)
{
    matrixTranslateRel_c(pos.x, pos.y, pos.z);
    if (!delta || (a[0].raw == b[0].raw && a[1].raw == b[1].raw))
    {
        frameRotate(a);
        return;
    }
    matrixPush_c();
    frameRotate(b);
    --gs->gMatrixPtr;
    frameRotate(a);
    matrixLerp_c(gs->gMatrixPtr + 1, delta, rate);
}
static vec3s nodePosition(const tr64_i32_be_t *node)
{
    return vec3s_create((int16)tr64_i32(node[1]), (int16)tr64_i32(node[2]),
                        (int16)tr64_i32(node[3]));
}
#define VIEW_COLD __attribute__((noinline, optimize("Os")))
#if TR64_HAS_TR2
#define TR2_PRES_COLD VIEW_COLD
/* tr2-water-and-flares.md:184-190 dynamic flare falloff + muzzle flash illumination.
 * Pure integer arithmetic, zero RNG, zero simulation state mutation. */
static TR2_PRES_COLD int32 tr2Falloff(const vec3i *point, const vec3i *src, int32 radius, unsigned age)
{
    int64_t dx64 = (int64_t)point->x - (int64_t)src->x;
    int64_t dy64 = (int64_t)point->y - (int64_t)src->y;
    int64_t dz64 = (int64_t)point->z - (int64_t)src->z;
    if (dx64 < -radius || dx64 > radius || dy64 < -radius || dy64 > radius || dz64 < -radius || dz64 > radius)
        return 0;
    int32 dx = (int32)dx64, dy = (int32)dy64, dz = (int32)dz64;
    int32 dist2 = dx * dx + dy * dy + dz * dz;
    int32 rad2 = radius * radius;
    if (radius == 32) return dist2 <= rad2 ? 4096 : 0;
    if (dist2 >= rad2) return 0;
    unsigned exp = age < 30u ? 12u - (30u - age) / 5u : 12u;
    return (1 << exp) - (dist2 >> (22u - exp));
}
static TR2_PRES_COLD int32 tr2ApplyDynamicLight(int32 offset, const vec3i *point)
{
    int32 best = 0;
    const Lara *lara = gs->players[0];
    if (lara && lara->extraL) {
        const ExtraInfoLara *e = lara->extraL;
        int32 rad = 0;
        unsigned age = 30u;
        if (gs->tr2->arms.flareLit && gs->tr2->arms.flareAge <= 1800u) {
            if (e->armL.flash.timer) {
                rad = 2048;
                age = gs->tr2->arms.flareAge ? gs->tr2->arms.flareAge - 1u : 0u;
            } else if (gs->tr2->arms.flareAge < 1800u) {
                rad = 32;
            }
        } else if (e->weapon != TR2_WEAPON_FLARE && (e->armR.flash.timer || e->armL.flash.timer)) {
            rad = 2048;
        }
        if (rad) {
            vec3i lp = {lara->pos.x, i32_sub(lara->pos.y, 512), lara->pos.z};
            best = tr2Falloff(point, &lp, rad, age);
        }
    }
    for (const ItemObj *it = gs->firstActive; it; it = it->nextActive) {
        if (it->type != TR2_ITEM_LOOSE_FLARE || it->headOffset >= 1800u) continue;
        unsigned age = it->headOffset;
        int32 v = tr2Falloff(point, &it->pos, (it->aggression || age < 1740u) ? 2048 : 32, age);
        if (v > best) best = v;
    }
    for (unsigned i = 0; i < gs->tr2->dynLightCount; ++i) {
        const TR2DynLight *dl = gs->tr2->dynLights + i;
        int32 rad = 1 << (dl->falloff > 13u ? 13u : dl->falloff);
        int32 v = tr2Falloff(point, &dl->pos, rad, 30u);
        if (v > best) best = v;
    }
    if (best > 0) {
        offset -= best;
        if (offset < -4096) offset = -4096;
    }
    return offset;
}
static TR2_PRES_COLD void tr2FillLight(tr64_gpu_light_t *dst, int32 x, int32 y, int32 z,
                                       int32 radius, unsigned age)
{
    dst->pos[0] = x;
    dst->pos[1] = y;
    dst->pos[2] = z;
    dst->radius = radius;
    if (radius == 32) {
        dst->peak = 4096u;
        dst->shift = 0u;
    } else {
        unsigned exp = age < 30u ? 12u - (30u - age) / 5u : 12u;
        dst->peak = (uint16_t)(1u << exp);
        dst->shift = (uint8_t)(22u - exp);
    }
    dst->active = 1u;
}
static TR2_PRES_COLD uint64_t tr2EyeDist2(const int32_t eye[3], const vec3i *p)
{
    if (!eye) return 0u;
    int64_t dx = (int64_t)p->x - (int64_t)eye[0];
    int64_t dy = (int64_t)p->y - (int64_t)eye[1];
    int64_t dz = (int64_t)p->z - (int64_t)eye[2];
    uint64_t ax = (uint64_t)(dx < 0 ? -dx : dx);
    uint64_t ay = (uint64_t)(dy < 0 ? -dy : dy);
    uint64_t az = (uint64_t)(dz < 0 ? -dz : dz);
    if (ax > (UINT64_C(1) << 30)) ax = UINT64_C(1) << 30;
    if (ay > (UINT64_C(1) << 30)) ay = UINT64_C(1) << 30;
    if (az > (UINT64_C(1) << 30)) az = UINT64_C(1) << 30;
    return ax * ax + ay * ay + az * az;
}
#endif
VIEW_COLD uint32_t game_view_lights(const int32_t eye[3], tr64_gpu_light_t *out, uint32_t max_lights)
{
    if (!gs || !out || !max_lights)
        return 0u;
    uint32_t count = 0u;
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE)) {
        if (!gs->tr2) return 0u;
        const Lara *lara = gs->players[0];
        if (lara && lara->extraL) {
            const ExtraInfoLara *e = lara->extraL;
            int32 rad = 0;
            unsigned age = 30u;
            if (gs->tr2->arms.flareLit && gs->tr2->arms.flareAge <= 1800u) {
                if (e->armL.flash.timer) {
                    rad = 2048;
                    age = gs->tr2->arms.flareAge ? gs->tr2->arms.flareAge - 1u : 0u;
                } else if (gs->tr2->arms.flareAge < 1800u) {
                    rad = 32;
                }
            } else if (e->weapon != TR2_WEAPON_FLARE && (e->armR.flash.timer || e->armL.flash.timer)) {
                rad = 2048;
            }
            if (rad) {
                tr2FillLight(out + count++, lara->pos.x, i32_sub(lara->pos.y, 512), lara->pos.z, rad, age);
            }
        }
        if (count < max_lights) {
            uint32_t cap = max_lights - count;
            if (cap > TR64_GPU_MAX_LIGHTS) cap = TR64_GPU_MAX_LIGHTS;
            const ItemObj *bestIt[TR64_GPU_MAX_LIGHTS] = {0};
            uint64_t bestD[TR64_GPU_MAX_LIGHTS];
            uint32_t found = 0u;
            for (const ItemObj *it = gs->firstActive; it; it = it->nextActive) {
                if (it->type != TR2_ITEM_LOOSE_FLARE || it->headOffset >= 1800u) continue;
                uint64_t d = tr2EyeDist2(eye, &it->pos);
                uint32_t slot = found;
                if (found < cap) {
                    ++found;
                } else {
                    uint32_t worst = 0u;
                    for (uint32_t k = 1u; k < cap; ++k)
                        if (bestD[k] > bestD[worst]) worst = k;
                    if (d >= bestD[worst]) continue;
                    slot = worst;
                }
                bestIt[slot] = it;
                bestD[slot] = d;
            }
            for (uint32_t k = 0u; k < found; ++k) {
                const ItemObj *it = bestIt[k];
                unsigned age = it->headOffset;
                int32 rad = (it->aggression || age < 1740u) ? 2048 : 32;
                tr2FillLight(out + count++, it->pos.x, it->pos.y, it->pos.z, rad, age);
            }
        }
        for (unsigned i = 0; i < gs->tr2->dynLightCount && count < max_lights; ++i) {
            const TR2DynLight *dl = gs->tr2->dynLights + i;
            unsigned f = dl->falloff > 13u ? 13u : dl->falloff;
            unsigned exp = dl->intensity > 12u ? 12u : dl->intensity;
            out[count++] = (tr64_gpu_light_t){
                .pos = {dl->pos.x, dl->pos.y, dl->pos.z},
                .radius = 1 << f,
                .peak = (uint16_t)(1u << exp),
                .shift = (uint8_t)(2u * f > exp ? 2u * f - exp : 0u),
                .active = 1u
            };
        }
        return count;
    }
#endif
    (void)eye;
    for (unsigned slot = 0; slot < TR64_GAME_PLAYERS && count < max_lights; ++slot) {
        const Lara *lara = gs->players[slot];
        if (lara && lara->extraL && (lara->extraL->armR.flash.timer || lara->extraL->armL.flash.timer)) {
            out[count++] = (tr64_gpu_light_t){
                .pos = {lara->pos.x, i32_sub(lara->pos.y, 512), lara->pos.z},
                .radius = 2048, .peak = 4096u, .shift = 10u, .active = 1u
            };
        }
    }
    return count;
}
static int32 lightingOffset(const ItemObj *lara, const tr64_u16_be_t *frame, bool rest)
{
    const RoomInfo *r = lara->room->info;
    uint32 i, count = tr64_u32(r->light_count), start = tr64_u32(r->light_start);
    int32 ambient = (tr64_u16(r->ambient) >> 5) << 5, maxLum = 0;
    vec3i center, point;
#if TR64_HAS_TR2
    int32 result;
    if (lara->intensity) {
        result = ((int32)lara->intensity << 5) - 4096;
        point = vec3i_create(lara->pos.x, i32_sub(lara->pos.y, 256), lara->pos.z);
    } else if (!count) {
        result = ambient - 4096;
        point = vec3i_create(lara->pos.x, i32_sub(lara->pos.y, 256), lara->pos.z);
    } else {
#else
    if (lara->intensity)
        return ((int32)lara->intensity << 5) - 4096;
    if (!count)
        return ambient - 4096;
#endif
    center = vec3i_create((frameWord(frame) + frameWord(frame + 1)) >> 1,
                          (frameWord(frame + 2) + frameWord(frame + 3)) >> 1,
                          (frameWord(frame + 4) + frameWord(frame + 5)) >> 1);
    point = rest ? vec3i_add(lara->pos, center) : ItemObj_getRelative(lara, &center);
    ambient = 8191 - ambient;
    const tr64_level_table_t *lt = gs->core->tables + (TR64_CORE_lights - 1);
    const tr64_core_lights_t *lights = (const tr64_core_lights_t *)lt->data;
    int32 ox = tr64_i32(r->x), oz = tr64_i32(r->z);
    for (i = 0; i < count; ++i)
    {
        uint32 li = start + i;
        const tr64_core_lights_t *light = li < lt->count ? lights + li : tr64_level_lights(gs->core, li);
        vec3i pos = vec3i_create((int16)(tr64_i32(light->x) - ox) + ox, (int16)tr64_i32(light->y),
                                 (int16)(tr64_i32(light->z) - oz) + oz);
        vec3i d = vec3i_sub(point, pos);
        int32 radius = (uint8)(tr64_u32(light->radius) >> 8) << 8;
        int32 intensity = (uint8)(tr64_u16(light->intensity) >> 5) << 5;
        int32 dist = i32_add((uint32)d.x * d.x + (uint32)d.y * d.y, (uint32)d.z * d.z) >> 12;
        int32 att = i32_mul(radius, radius) >> 12;
        int32 divisor = wrap_i32((uint32)dist + (uint32)att), lum;
        if (!divisor)
            sys_error("game light attenuation divisor");
        lum = i32_add(i32_mul(intensity, att) / divisor, ambient);
        if (lum > maxLum)
            maxLum = lum;
    }
#if TR64_HAS_TR2
    result = 8191 - (i32_add(maxLum, ambient) >> 1) - 4096;
    }
    return TR64_RULE(PACKED_POSE) ? tr2ApplyDynamicLight(result, &point) : result;
#else
    return 8191 - (i32_add(maxLum, ambient) >> 1) - 4096;
#endif
}
static void cameraBasis(int32 *basis,int32 angleX,int32 angleY)
{
    uint32 tx=gSinCosTable[(uint16)angleX>>4],ty=gSinCosTable[(uint16)angleY>>4];
    int32 sx=wrap_i32(tx)>>16,cx=(int16)tx,sy=wrap_i32(ty)>>16,cy=(int16)ty;
    basis[0]=cy;basis[1]=0;basis[2]=-sy;
    basis[3]=(sx*sy)>>14;basis[4]=cx;basis[5]=(sx*cy)>>14;
    basis[6]=(cx*sy)>>14;basis[7]=-sx;basis[8]=(cx*cy)>>14;
}
static void menuCamera(int32 height,int32 radius,int32 pitch,int32 *basis,int32 *eye)
{
    int16 ax,ay;
    anglesFromVector(0,-(height+96),-768,&ax,&ay);
    cameraBasis(basis,ax+pitch,ay);
    eye[0]=0;eye[1]=height;eye[2]=radius+768;
}
static void cameraView(tr64_game_view_t *out,const Camera *c)
{
    cameraBasis(out->camera_basis,c->angle.x,c->angle.y);
    out->camera_eye[0] = c->view.pos.x;
#if TR64_HAS_TR2
    if (TR64_RULE(CAMERA_VIEW) && c->mode != CAMERA_MODE_CUTSCENE)
        out->camera_eye[1] = c->displayY;
    else
#endif
    out->camera_eye[1] = c->view.pos.y;
    out->camera_eye[2] = c->view.pos.z;
    out->camera_target[0] = c->target.pos.x;
    out->camera_target[1] = c->target.pos.y;
    out->camera_target[2] = c->target.pos.z;
    out->camera_room = (uint32)(c->view.room - gs->rooms);
    out->flipped = gs->gSaveGame.flipped;
}
static bool undrawn(uint32 type)
{
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE)) return type == TR2_ITEM_CAMERA_TARGET;
#endif
    return type == ITEM_VIEW_TARGET || type == ITEM_WATERFALL || type == ITEM_LAVA_EMITTER;
}
static void drawable(tr64_game_drawable_t *out, const ItemObj *item, uint32 identity,
                     uint32 joint, uint32 mesh, int32 light)
{
    const Matrix *m = gs->gMatrixPtr;
    out->mesh_slot = mesh;
    out->instance_id = 0x80000000u + identity;
    out->room = item->room ? (uint32)(item->room-gs->rooms) : UINT32_MAX;
    out->basis[0] = m->e00; out->basis[1] = m->e01; out->basis[2] = m->e02;
    out->basis[3] = m->e10; out->basis[4] = m->e11; out->basis[5] = m->e12;
    out->basis[6] = m->e20; out->basis[7] = m->e21; out->basis[8] = m->e22;
    out->translation[0] = i32_add(item->pos.x, m->e03 >> 14);
    out->translation[1] = i32_add(item->pos.y, m->e13 >> 14);
    out->translation[2] = i32_add(item->pos.z, m->e23 >> 14);
    out->light_offset = light;
    out->water = item->room ? tr64_u16(item->room->info->flags)&ROOM_FLAG_WATER_MASK : 0;
    out->kind = TR64_GAME_DRAW_MESH;
    out->bone = (uint8)joint;
    out->blend = TR64_GAME_DRAW_BLEND_NONE;
}
static uint32 flashView(const ItemObj *item, uint32 identity, tr64_game_drawable_t *out)
{
    const ItemObj *owner = (const ItemObj *)item->extraL;
    const Model *model = gs->level.models + owner->type;
    const tr64_u16_be_t *frame, *next;
    Matrix *old = gs->gMatrixPtr;
    int32 delta, rate, joint;
    game_frame_pair(owner, &frame, &next, &delta, &rate);
    matrixPush_c();
    matrixSetIdentity_c();
    matrixRotateYXZ_c(owner->angle.x, owner->angle.y, owner->angle.z);
    frameLerp(vec3s_create(frameWord(frame + 6), frameWord(frame + 7), frameWord(frame + 8)),
              frame + 10, frame + 10, 0, 1);
    for (joint = 0; joint < item->hSpeed; ++joint)
    {
        const tr64_i32_be_t *node = gs->level.nodes + (model->nodeIndex + joint) * 4;
        uint32 flags = tr64_i32(node[0]);
        if (flags & NODE_FLAG_POP)
        {
            if (gs->gMatrixPtr <= old + 1) sys_error("game flash bone stack underflow");
            --gs->gMatrixPtr;
        }
        if (flags & NODE_FLAG_PUSH) matrixPush_c();
        frameLerp(nodePosition(node), frame + 12 + joint * 2, frame + 12 + joint * 2, 0, 1);
    }
    matrixTranslateRel_c(item->angle.x, item->angle.y, item->angle.z + 150);
    matrixRotateYXZ_c(-16384, 0, 0);
    drawable(out, owner, identity, 0, gs->level.models[ITEM_MUZZLE_FLASH].start,
             ((int32)item->intensity << 5) - 4096);
    out->blend = TR64_GAME_DRAW_ADDITIVE;
    gs->gMatrixPtr = old;
    return 1;
}
#if TR64_HAS_TR2
static bool tr2GunFlash(const ExtraInfoLara *e, unsigned side)
{
    if (!e || e->weapon == TR2_WEAPON_FLARE ||
        gs->level.models[TR2_MODEL_GUN_FLASH].count <= 0)
        return false;
    return (side ? e->armL.flash.timer : e->armR.flash.timer) != 0;
}
static bool tr2HeldFlareFlame(const ExtraInfoLara *e)
{
    return e && gs->tr2->arms.flareLit && e->armL.flash.timer != 0 &&
           gs->level.models[TR2_SPRITE_GLOW].count < 0;
}
static bool tr2LooseFlareFlame(const ItemObj *item)
{
    return item->room && item->type == TR2_ITEM_LOOSE_FLARE &&
           (item->flags & ITEM_FLAG_ACTIVE) && item->headOffset < 1800u &&
           (item->aggression || item->headOffset < 1740u) &&
           gs->level.models[TR2_SPRITE_GLOW].count < 0;
}
static bool tr2DrawBraid(const ItemObj *item, bool lara)
{
    return lara && item->room && item->visibleMask &&
           gs->level.models[TR2_MODEL_HAIR].count == TR2_BRAID_SEGMENTS;
}
#endif
static uint32 modelCount(const ItemObj *item, bool lara)
{
    const Model *model = gs->level.models + item->type;
    uint32 count = 0, joint;
    if (undrawn(item->type) || !model->count) return 0;
    if (!TR64_RULE(PACKED_POSE) && item->type == ITEM_MUZZLE_FLASH)
        return MuzzleFlash_ownerIsAlive(item) ? 1 : 0;
    if (model->count < 0) return 1;
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE) && lara) {
        const ExtraInfoLara *e = item->extraL;
        return JOINT_MAX +
               (uint32)tr2GunFlash(e, 0) + (uint32)tr2GunFlash(e, 1) +
               (uint32)tr2HeldFlareFlame(e) +
               (tr2DrawBraid(item, true) ? TR2_BRAID_SEGMENTS : 0u);
    }
#endif
    if (lara)
        return JOINT_MAX + !!item->extraL->armR.flash.timer + !!item->extraL->armL.flash.timer;
    for (joint = 0; joint < (uint32)model->count; ++joint)
        if (joint < 32 && (item->visibleMask & (1u << joint))) ++count;
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE) && tr2LooseFlareFlame(item)) ++count;
#endif
    return count;
}
static const tr64_u16_be_t *armFrame(const ItemObj *item, const ExtraInfoLara_Arm *arm)
{
    const Anim *anim = gs->level.anims + arm->animIndex;
    const Model *model = gs->level.models + item->type;
    uint32 offset = tr64_u32(anim->frame_offset) / 2 +
                    arm->frameIndex * (10 + model->count * 2);
    if (anim->frame_rate != 1 || offset + 10 + model->count * 2 > gs->level.framesCount)
        sys_error("game armed frame bounds");
    return gs->level.animFrames + offset;
}
static vec3s decodedAngles(const tr64_u16_be_t *p)
{
    uint32 lo = tr64_u16(p[0]), hi = tr64_u16(p[1]);
    return vec3s_create((int16)((hi & 0x3ff0) << 2),
                       (int16)(((hi & 15) << 12) | ((lo & 0xfc00) >> 4)),
                       (int16)((lo & 1023) << 6));
}
static void restoreBasis(Matrix *m, const Matrix *basis)
{
    m->e00 = basis->e00; m->e01 = basis->e01; m->e02 = basis->e02;
    m->e10 = basis->e10; m->e11 = basis->e11; m->e12 = basis->e12;
    m->e20 = basis->e20; m->e21 = basis->e21; m->e22 = basis->e22;
}
#if TR64_HAS_TR2
/* Q-ARM-01, presentation. tr2-lara-arms.md:36 keeps the terminal arm
 * selection after hands are free and does not say the published joints
 * return to the body. The first game's holster completion clears
 * animIndex, which is this file's publication gate. The sequel keeps the
 * index and uses that completion state, except the held flare, whose
 * overlay the specification keeps while free (tr2-lara-arms.md:104). */
static bool weaponOverlay(const ExtraInfoLara *e, const ExtraInfoLara_Arm *arm, unsigned side)
{
    if (e->weapon == TR2_WEAPON_FLARE)
        return side == 1 && gs->tr2->arms.flareLeft && arm->animIndex;
    /* Draw, holster and ready are the gun pose. Free and busy hands
     * (a pickup, a switch) publish the body, matching the first game
     * once its holster completion has cleared the arm index. */
    if (e->weaponState != WEAPON_STATE_DRAW &&
        e->weaponState != WEAPON_STATE_HOLSTER &&
        e->weaponState != WEAPON_STATE_READY)
        return false;
    if ((e->weapon == WEAPON_SHOTGUN || e->weapon == TR2_WEAPON_GRENADE) && !gs->tr2->shotgunPose)
        return false;
    return arm->animIndex != 0;
}
#endif

#if TR64_HAS_TR2
typedef struct { const tr64_u16_be_t *at,*end; } Rotation;
static Rotation rotationStream(PackedFrame frame,unsigned joint)
{
    return (Rotation){skipPackedRotations(frame,joint),frame.words+frame.extent};
}
static void skipRotation(Rotation *r)
{
    if(r->at>=r->end)sys_error("Tomb Raider II: packed drawable rotation bounds");
    unsigned n=tr64_u16(*r->at)>>14 ? 1 : 2;
    if(r->end-r->at<(int)n)sys_error("Tomb Raider II: truncated drawable rotation");
    r->at+=n;
}
static void packedLaraArms(const ExtraInfoLara *e, Rotation arms[2])
{
    const ExtraInfoLara_Arm *pose[]={&e->armR,&e->armL};
    for(unsigned side=0;side<2;++side) {
        bool active = weaponOverlay(e, pose[side], side);
        if(active && pose[side]->animIndex) {
            unsigned armAnim=pose[side]->animIndex,armFrame=pose[side]->frameIndex;
            if(armAnim>=gs->tr2->animCount)
                sys_error("Tomb Raider II: packed arm animation bounds");
            const Anim *anim=gs->level.anims+armAnim;
            if (e->weapon==WEAPON_SHOTGUN || e->weapon==TR2_WEAPON_GRENADE)
                armFrame=(uint16)(armFrame+tr64_u16(anim->frame_start));
            arms[side]=rotationStream(game_tr2_packed_key(anim,armAnim,armFrame),
                                      side==0 ? JOINT_ARM_R1 : JOINT_ARM_L1);
        }
    }
}
static TR2_PRES_COLD void tr2FillDrawable(tr64_game_drawable_t *d, uint32 meshSlot,
                          uint32 id, uint32 room, const vec3i *pos, int32 light,
                          uint8 water, uint8 kind, uint8 bone, uint8 blend)
{
    if (kind == TR64_GAME_DRAW_SPRITE) {
        memset(d->basis, 0, sizeof(d->basis));
        d->basis[0] = d->basis[4] = d->basis[8] = 16384;
    } else {
        const Matrix *m = gs->gMatrixPtr;
        d->basis[0] = m->e00; d->basis[1] = m->e01; d->basis[2] = m->e02;
        d->basis[3] = m->e10; d->basis[4] = m->e11; d->basis[5] = m->e12;
        d->basis[6] = m->e20; d->basis[7] = m->e21; d->basis[8] = m->e22;
    }
    d->mesh_slot = meshSlot;
    d->instance_id = id;
    d->room = room;
    d->translation[0] = pos->x;
    d->translation[1] = pos->y;
    d->translation[2] = pos->z;
    d->light_offset = light;
    d->water = water;
    d->kind = kind;
    d->bone = bone;
    d->blend = blend;
}
static TR2_PRES_COLD const TR2Creature *tr2CreatureFor(const ItemObj *item)
{
    for (unsigned i = 0; i < TR2_CREATURE_SLOTS; ++i)
        if (gs->tr2->creatures[i].item == item)
            return gs->tr2->creatures + i;
    return NULL;
}
static TR2_PRES_COLD void tr2ApplyCreatureJoint(uint32 type, uint32 joint, uint32 flags,
                                                const TR2Creature *ai, uint32 *extraIndex)
{
    static const struct { uint8 type, j0, j1; } map[] = {
        {TR2_ITEM_TIGER, TR2_TIGER_HEAD_JOINT, 255}, {TR2_ITEM_REX, 11, 12},
        {TR2_ITEM_DOBERMAN, 20, 255}, {TR2_ITEM_GUNMAN_1, 1, 2},
        {TR2_ITEM_GUNMAN_2, 1, 2}, {TR2_ITEM_GUNMAN_3, 1, 2},
        {TR2_ITEM_OPERA_BOSS, 1, 11},
        {TR2_ITEM_RAT, 2, 255}, {TR2_ITEM_THUG, 5, 255}
    };
    if (flags & (NODE_FLAG_ROTY | NODE_FLAG_ROTX | NODE_FLAG_ROTZ)) {
        static const uint8 bits[3] = {NODE_FLAG_ROTY, NODE_FLAG_ROTX, NODE_FLAG_ROTZ};
        for (unsigned a = 0; a < 3u; ++a)
            if (flags & bits[a]) {
                int32 ang = *extraIndex < 2u ? ai->jointYaw[(*extraIndex)++] : 0;
                if (a == 0u) matrixRotateY_c(ang);
                else if (a == 1u) matrixRotateX_c(ang);
                else matrixRotateZ_c(ang);
            }
        return;
    }
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (map[i].type != type) continue;
        int16 yaw = joint == map[i].j0 ? ai->jointYaw[0] : joint == map[i].j1 ? ai->jointYaw[1] : 0;
        if (yaw) matrixRotateY_c(yaw);
        return;
    }
}
/* Presentation-only 6-segment braid spring chain (model 2). Never mutates
 * gameplay state or RNG; resets on level start or >512-unit teleport. */
static TR2_PRES_COLD void tr2CaptureBraid(const ItemObj *item, vec3i ref[2])
{
    matrixPush_c();
    matrixTranslateRel_c(0, -23, -55);
    const Matrix *hm = gs->gMatrixPtr;
    ref[0] = vec3i_create(i32_add(item->pos.x, hm->e03 >> 14),
                          i32_add(item->pos.y, hm->e13 >> 14),
                          i32_add(item->pos.z, hm->e23 >> 14));
    ref[1] = vec3i_create(-hm->e02, -hm->e12, -hm->e22);
    --gs->gMatrixPtr;
}
static inline void tr2BraidAxis(int32 *cur, int32 *prv, bool uw, int32 grav)
{
    int32 v = *cur - *prv;
    if (v < -64) v = -64; else if (v > 64) v = 64;
    *prv = *cur;
    *cur += (uw ? (v >> 1) : ((v * 3) >> 2)) + grav;
}
static TR2_PRES_COLD void tr2EmitBraid(const ItemObj *item, uint32 identity,
                         const vec3i ref[2], int32 light, tr64_game_drawable_t *out)
{
    const Model *hair = gs->level.models + TR2_MODEL_HAIR;
    TR2Braid *b = &gs->tr2->braid;
    const vec3i *anchor = ref, *back = ref + 1;
    bool reset = !b->valid || gs->tick < b->lastTick;
    if (!reset) {
        int64_t adx = (int64_t)anchor->x - (int64_t)b->pos[0].x;
        int64_t ady = (int64_t)anchor->y - (int64_t)b->pos[0].y;
        int64_t adz = (int64_t)anchor->z - (int64_t)b->pos[0].z;
        if (adx < -512 || adx > 512 || ady < -512 || ady > 512 || adz < -512 || adz > 512)
            reset = true;
    }
    if (reset || gs->tick != b->lastTick) {
        bool uw = item->waterState == WATER_STATE_UNDER ||
                  (tr64_u16(item->room->info->flags) & ROOM_FLAG_WATER_MASK) != 0;
        b->valid = 1;
        b->lastTick = gs->tick;
        b->prev[0] = reset ? *anchor : b->pos[0];
        b->pos[0] = *anchor;
        for (unsigned i = 0; i < TR2_BRAID_SEGMENTS; ++i) {
            int32 len = i < TR2_BRAID_SEGMENTS - 1u
                ? tr64_i32(gs->level.nodes[(hair->nodeIndex + i) * 4u + 3u]) : 44;
            if (len <= 0 || len > 256) len = 44;
            vec3i *cur = b->pos + i + 1u, *prv = b->prev + i + 1u, *par = b->pos + i;
            if (reset) {
                cur->x = par->x + ((back->x * 8) >> 14);
                cur->y = par->y + len;
                cur->z = par->z + ((back->z * 8) >> 14);
                *prv = *cur;
            } else {
                tr2BraidAxis(&cur->x, &prv->x, uw, 0);
                tr2BraidAxis(&cur->y, &prv->y, uw, uw ? -3 : 14);
                tr2BraidAxis(&cur->z, &prv->z, uw, 0);
                int32 dx = cur->x - par->x, dy = cur->y - par->y, dz = cur->z - par->z;
                int32 d = (int32)phd_sqrt((uint32)(dx * dx + dy * dy + dz * dz));
                cur->x = par->x + (d ? (dx * len) / d : 0);
                cur->y = par->y + (d ? (dy * len) / d : len);
                cur->z = par->z + (d ? (dz * len) / d : 0);
            }
        }
    }
    uint32 room = (uint32)(item->room - gs->rooms);
    uint8 water = tr64_u16(item->room->info->flags) & ROOM_FLAG_WATER_MASK;
    matrixPush_c();
    for (unsigned i = 0; i < TR2_BRAID_SEGMENTS; ++i) {
        int32 dx = b->pos[i + 1u].x - b->pos[i].x;
        int32 dy = b->pos[i + 1u].y - b->pos[i].y;
        int32 dz = b->pos[i + 1u].z - b->pos[i].z;
        int16 ax = 0, ay = 0;
        anglesFromVector(dx, dy, dz, &ax, &ay);
        if (!dx && !dz) ay = (int16)phd_atan(back->z, back->x);
        matrixSetIdentity_c();
        matrixRotateYXZ_c(ax, ay, 0);
        tr2FillDrawable(out + i, hair->start + i, 0x80000000u + identity, room,
                        b->pos + i, light, water, TR64_GAME_DRAW_MESH,
                        (uint8)(JOINT_MAX + 3u + i), TR64_GAME_DRAW_BLEND_NONE);
    }
    --gs->gMatrixPtr;
}
static TR2_PRES_COLD uint32 tr2LaraHandExtras(const ItemObj *item, const ExtraInfoLara *e,
                                              uint32 identity, uint32 joint, tr64_game_drawable_t *out)
{
    uint32 count = 0;
    uint32 room = item->room ? (uint32)(item->room - gs->rooms) : UINT32_MAX;
    uint8 water = item->room ? (tr64_u16(item->room->info->flags) & ROOM_FLAG_WATER_MASK) : 0;
    unsigned fside = joint == JOINT_ARM_L3;
    if (tr2GunFlash(e, fside)) {
        bool sg = e->weapon == WEAPON_SHOTGUN;
        matrixPush_c();
        matrixTranslateRel_c(0, sg ? 240 : 155, sg ? 45 : 55);
        matrixRotateYXZ_c(-16384, 0, (int16)(gs->tick * 13171u + fside * 21845u));
        const Matrix *fm = gs->gMatrixPtr;
        vec3i fp = {i32_add(item->pos.x, fm->e03 >> 14),
                    i32_add(item->pos.y, fm->e13 >> 14),
                    i32_add(item->pos.z, fm->e23 >> 14)};
        tr2FillDrawable(out + count++, gs->level.models[TR2_MODEL_GUN_FLASH].start,
                        0x80000000u + identity, room, &fp, -4096, water,
                        TR64_GAME_DRAW_MESH, (uint8)(JOINT_MAX + fside), TR64_GAME_DRAW_ADDITIVE);
        --gs->gMatrixPtr;
    }
    if (fside && tr2HeldFlareFlame(e)) {
        matrixPush_c();
        matrixTranslateRel_c(11, 32, 41);
        const Matrix *fm = gs->gMatrixPtr;
        vec3i fp = {i32_add(item->pos.x, fm->e03 >> 14),
                    i32_add(item->pos.y, fm->e13 >> 14),
                    i32_add(item->pos.z, fm->e23 >> 14)};
        tr2FillDrawable(out + count++, gs->level.models[TR2_SPRITE_GLOW].start,
                        0x80000000u + identity, room, &fp, -4096, water,
                        TR64_GAME_DRAW_SPRITE, JOINT_MAX + 2u, TR64_GAME_DRAW_ADDITIVE);
        --gs->gMatrixPtr;
    }
    return count;
}
/* TR2 packed frames use the same bounded cursor as ItemObj_getJoint.
 * This is presentation only: no pose output feeds the simulation. */
static uint32 packedModelView(const ItemObj *item,uint32 identity,tr64_game_drawable_t *out,bool lara)
{
    const Model *model=gs->level.models+item->type;
    const tr64_u32_be_t *nodeFlags=(const tr64_u32_be_t *)
        gs->core->tables[TR64_CORE_node_flags-1].data;
    if(lara && model->count!=JOINT_MAX)sys_error("Tomb Raider II: Lara drawable bone count");
    PackedFramePair pair=game_tr2_packed_frames(item);
    Rotation a={pair.a.words+9,pair.a.words+pair.a.extent};
    Rotation b={pair.b.words+9,pair.b.words+pair.b.extent};
    const ExtraInfoLara *e=lara ? item->extraL : NULL;
    const TR2Creature *ai=(!lara && item->room) ? tr2CreatureFor(item) : NULL;
    Rotation arms[2]={{0}};
    if(e)packedLaraArms(e,arms);
    int32 light=item->room ? lightingOffset(item,pair.a.words,false) : -32;
    int32 t=pair.delta*divTable[pair.rate];
    Matrix *old=gs->gMatrixPtr;
    matrixPush_c();
    if(item->room) {
        matrixSetIdentity_c();
        matrixRotateYXZ_c(item->angle.x,item->angle.y,item->angle.z);
    }
    Matrix *basis=gs->gMatrixPtr;
    if(lara)matrixPush_c();
    Matrix *root=gs->gMatrixPtr;
    uint32 count=0, extraIndex=0;
    vec3i braidRef[2]={{0}};
    const tr64_i32_be_t *nodes=gs->level.nodes;
    for(uint32 joint=0;joint<(uint32)model->count;++joint) {
        vec3s offset;
        uint32 flags=0;
        if(!joint) {
            const tr64_u16_be_t *x=pair.a.words+6,*y=pair.b.words+6;
            offset=pair.delta ? vec3s_create(rootWordLerp(x,y,t),rootWordLerp(x+1,y+1,t),rootWordLerp(x+2,y+2,t))
                              : vec3s_create(frameWord(x),frameWord(x+1),frameWord(x+2));
        } else {
            uint32 bone=model->nodeIndex+joint-1;
            flags=tr64_u32(nodeFlags[bone]);
            if(flags&NODE_FLAG_POP) {
                if(gs->gMatrixPtr<=root)sys_error("Tomb Raider II: drawable bone stack underflow");
                --gs->gMatrixPtr;
            }
            if(flags&NODE_FLAG_PUSH)matrixPush_c();
            offset=nodePosition(nodes+4*bone);
        }
        matrixTranslateRel_c(offset.x,offset.y,offset.z);
        unsigned side=joint>=JOINT_ARM_R1 && joint<=JOINT_ARM_R3 ? 0 :
                      joint>=JOINT_ARM_L1 && joint<=JOINT_ARM_L3 ? 1 : 2;
        if(side<2 && arms[side].at) {
            const ExtraInfoLara_Arm *aim=side==0 ? &e->armR : &e->armL;
            if((e->weapon==WEAPON_PISTOLS || e->weapon==WEAPON_MAGNUMS || e->weapon==WEAPON_UZIS) &&
               joint==(side==0 ? JOINT_ARM_R1 : JOINT_ARM_L1)) {
                restoreBasis(gs->gMatrixPtr,basis);
                matrixRotateYXZ_c(aim->angle.x,aim->angle.y,aim->angle.z);
            }
            arms[side].at=tr2FrameRotate(arms[side].at,arms[side].end);
            skipRotation(&a);
            if(pair.delta)skipRotation(&b);
        } else if(!pair.delta) {
            a.at=tr2FrameRotate(a.at,a.end);
        } else {
            bool same=false;
            if(a.at<a.end && b.at<b.end && a.at->raw==b.at->raw) {
                uint32 ha=tr64_u16(*a.at);
                if(ha>>14)same=true;
                else if(a.end-a.at>=2 && b.end-b.at>=2 && a.at[1].raw==b.at[1].raw)same=true;
            }
            if(same) {
                const tr64_u16_be_t *nextA=tr2FrameRotate(a.at,a.end);
                b.at+=nextA-a.at;
                a.at=nextA;
            } else {
                matrixPush_c();
                b.at=tr2FrameRotate(b.at,b.end);
                --gs->gMatrixPtr;
                a.at=tr2FrameRotate(a.at,a.end);
                matrixLerp_c(gs->gMatrixPtr+1,pair.delta,pair.rate);
            }
        }
        if(e && joint==JOINT_TORSO)
            matrixRotateYXZ_c(e->torso.angle.x,e->torso.angle.y,e->torso.angle.z);
        if(e && joint==JOINT_HEAD)
            matrixRotateYXZ_c(e->head.angle.x,e->head.angle.y,e->head.angle.z);
        if(ai && joint)
            tr2ApplyCreatureJoint(item->type,joint,flags,ai,&extraIndex);
        if(lara || (joint<32 && (item->visibleMask&(1u<<joint))))
            drawable(out+count++,item,identity,joint,lara ? e->meshes[joint] : model->start+joint,light);
        if(e && (joint==JOINT_ARM_R3 || joint==JOINT_ARM_L3))
            count+=tr2LaraHandExtras(item,e,identity,joint,out+count);
        if(joint==JOINT_HEAD && tr2DrawBraid(item,lara))
            tr2CaptureBraid(item,braidRef);
    }
    gs->gMatrixPtr=old;
    if(tr2LooseFlareFlame(item)) {
        vec3i fp={item->pos.x,i32_sub(item->pos.y,16),item->pos.z};
        tr2FillDrawable(out+count++,gs->level.models[TR2_SPRITE_GLOW].start,
                        0x80000000u+identity,(uint32)(item->room-gs->rooms),&fp,
                        -4096,tr64_u16(item->room->info->flags)&ROOM_FLAG_WATER_MASK,
                        TR64_GAME_DRAW_SPRITE,(uint8)model->count,TR64_GAME_DRAW_ADDITIVE);
    }
    if(tr2DrawBraid(item,lara)) {
        tr2EmitBraid(item,identity,braidRef,light,out+count);
        count+=TR2_BRAID_SEGMENTS;
    }
    return count;
}
static TR2_PRES_COLD bool tr2EffectDrawable(uint32 fxIndex, uint32 room, tr64_game_drawable_t *out)
{
    const TR2Effect *fx = gs->tr2->effects + fxIndex - 1u;
    if (fx->room != gs->rooms + room) return false;
    int32 ambient = ((tr64_u16(fx->room->info->ambient) >> 5) << 5) - 4096;
    int32 lit = tr2ApplyDynamicLight(ambient, &fx->pos);
    uint8 water = tr64_u16(fx->room->info->flags) & ROOM_FLAG_WATER_MASK;
    uint32 id = 0x80000000u + MAX_ITEMS + fxIndex;
    if (fx->kind == TR2_FX_SHARD || fx->kind == TR2_FX_GUN_FLASH) {
        if (fx->asset.mesh >= gs->core->tables[TR64_CORE_mesh_pointers - 1].count)
            return false;
        Matrix *old = gs->gMatrixPtr;
        matrixPush_c();
        matrixSetIdentity_c();
        matrixRotateYXZ_c(fx->angle.x, fx->angle.y, fx->angle.z);
        bool flash = fx->kind == TR2_FX_GUN_FLASH;
        tr2FillDrawable(out, fx->asset.mesh, id, room, &fx->pos, flash ? -4096 : lit, water,
                        TR64_GAME_DRAW_MESH, 0,
                        flash ? TR64_GAME_DRAW_ADDITIVE : TR64_GAME_DRAW_BLEND_NONE);
        gs->gMatrixPtr = old;
        return true;
    }
    static const struct { uint8 type, flags, blend; } fxSpec[TR2_FX_BUBBLE + 1] = {
        {0, 0, 0},
        {TR2_SPRITE_FLAME, 3, TR64_GAME_DRAW_ADDITIVE},
        {0, 0, 0},
        {TR2_SPRITE_SPLASH, 0, TR64_GAME_DRAW_TRANSLUCENT},
        {TR2_SPRITE_BLOOD, 4, TR64_GAME_DRAW_BLEND_NONE},
        {TR2_SPRITE_RICOCHET, 11, TR64_GAME_DRAW_BLEND_NONE},
        {TR2_SPRITE_WAKE, 16, TR64_GAME_DRAW_TRANSLUCENT},
        {TR2_SPRITE_EXPLOSION, 34, TR64_GAME_DRAW_ADDITIVE},
        {TR2_SPRITE_BUBBLE, 9, TR64_GAME_DRAW_TRANSLUCENT}
    };
    if (fx->kind > TR2_FX_BUBBLE) return false;
    uint32 type = fxSpec[fx->kind].type, fl = fxSpec[fx->kind].flags;
    if (!type || gs->level.models[type].count >= 0) return false;
    uint32 seqLen = (uint32)(-gs->level.models[type].count);
    uint32 frame = (fl & 32u) ? (uint32)(fx->age >> 1)
                 : (fl & 16u) ? ((fx->age > 20u ? 0u : 20u - fx->age) >> 2)
                 : (fl & 8u)  ? ((uint32)fx->asset.spriteRoll + ((fl & 2u) ? fx->age : 0u))
                 : (fl & 4u)  ? (uint32)(fx->age >> 2)
                 : (fl & 1u)  ? ((gs->tick >> 1) + fxIndex)
                 : (uint32)fx->age;
    uint32 idx = (fl & 1u) ? (frame % seqLen) : (frame < seqLen ? frame : seqLen - 1u);
    tr2FillDrawable(out, gs->level.models[type].start + idx, id, room, &fx->pos,
                    (fl & 2u) ? -4096 : lit, water, TR64_GAME_DRAW_SPRITE, 0, fxSpec[fx->kind].blend);
    return true;
}
#endif
/* Pure read-only floor lookup for ground shadows: zero mutation of gs,
 * gLastFloorData, gLastFloorSlant, dynamicFloorCalls, tmpBox, or RNG. */
static VIEW_COLD int32 shadowFloor(const ItemObj *item, uint16_t *outRoom)
{
    Room *r = item->room;
    const Sector *s;
#if TR64_HAS_TR2
    if (TR64_RULE(ROOM_LINKS))
        s = game_tr2_room_sector(&r, item->pos.x, item->pos.y, item->pos.z);
    else
#endif
    {
        r = Room_getRoom(r, item->pos.x, item->pos.y, item->pos.z);
        s = Room_getSector(r, item->pos.x, item->pos.z);
    }
    unsigned hops = gs->level.roomsCount;
    while (s->roomBelow != NO_ROOM && hops--) {
        r = gs->rooms + s->roomBelow;
        s = Room_getSector(r, item->pos.x, item->pos.z);
        unsigned phops = gs->level.roomsCount;
        const Room *next;
        while ((next = Sector_getNextRoom(s)) != NULL && phops--) {
            r = (Room *)next;
            s = Room_getSector(r, item->pos.x, item->pos.z);
        }
    }
    if (s->floor == NO_FLOOR) return WALL;
    int32 floor = s->floor * 256;
    if (s->floorIndex) {
        const FloorData *fd = gs->level.floors + s->floorIndex;
        uint16 cmd = tr64_u16(*fd++);
        if ((cmd & 0x1Fu) == FLOOR_TYPE_FLOOR) {
            uint16 slant = tr64_u16(*fd);
            int32 sx = FD_SLANT_X(slant), sz = FD_SLANT_Z(slant);
            int32 dx = item->pos.x & 1023, dz = item->pos.z & 1023;
            floor += sx < 0 ? -((sx * dx) >> 2) : (sx * (1023 - dx)) >> 2;
            floor += sz < 0 ? -((sz * dz) >> 2) : (sz * (1023 - dz)) >> 2;
        }
    }
    *outRoom = (uint16_t)(r - gs->rooms);
    return floor;
}
static VIEW_COLD bool emitShadow(const ItemObj *item, tr64_game_shadow_t *out)
{
    if (!item || !item->room || (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
        return false;
    if (item->room < gs->rooms || (uint32)(item->room - gs->rooms) >= gs->level.roomsCount)
        return false;
    const Model *model = gs->level.models + item->type;
    if (model->count <= 0) return false;
    uint16_t floorRoom = (uint16_t)(item->room - gs->rooms);
    int32 floor = shadowFloor(item, &floorRoom);
    if (item->roomFloor != WALL && (int64_t)item->roomFloor >= (int64_t)item->pos.y - 64 &&
        (floor == WALL || item->roomFloor < floor)) {
        floor = item->roomFloor;
        floorRoom = (uint16_t)(item->room - gs->rooms);
    }
    if (floor == WALL || (int64_t)floor < (int64_t)item->pos.y - 64 ||
        (int64_t)floor - (int64_t)item->pos.y > 4096)
        return false;
    const tr64_u16_be_t *frameWords;
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE)) {
        PackedFramePair pair = game_tr2_packed_frames(item);
        if (!pair.a.words) return false;
        frameWords = pair.a.words;
    } else
#endif
    {
        uint32 animCount = gs->core ? gs->core->tables[TR64_CORE_animations - 1].count : 0u;
        if (!gs->level.anims || !gs->level.animFrames || (uint32)item->animIndex >= animCount)
            return false;
        const Anim *anim = gs->level.anims + item->animIndex;
        uint32 begin = tr64_u16(anim->frame_start), end = tr64_u16(anim->frame_end);
        if (begin != end && !anim->frame_rate) return false;
        int32 idx = (begin == end)
            ? 0
            : ((int32)item->frameIndex - (int32)begin) / (int32)anim->frame_rate;
        int64_t off = (int64_t)(tr64_u32(anim->frame_offset) / 2u) +
                      (int64_t)idx * (10 + (int64_t)model->count * 2);
        if (off < 0 || (uint64_t)off + 6u > gs->level.framesCount) return false;
        frameWords = gs->level.animFrames + (uint32)off;
    }
    int32 minX = frameWord(frameWords + 0), maxX = frameWord(frameWords + 1);
    int32 minZ = frameWord(frameWords + 4), maxZ = frameWord(frameWords + 5);
    int32 cx = (minX + maxX) >> 1, cz = (minZ + maxZ) >> 1;
    int32 rx = ((maxX - minX) * 3) >> 3;
    int32 rz = ((maxZ - minZ) * 3) >> 3;
    if (rx < 96) rx = 96; else if (rx > 1024) rx = 1024;
    if (rz < 96) rz = 96; else if (rz > 1024) rz = 1024;
    int32 s, c;
    sincos(item->angle.y, s, c);
    out->x = i32_add(item->pos.x, (cx * c + cz * s) >> FIXED_SHIFT);
    out->y = i32_sub(floor, 8);
    out->z = i32_add(item->pos.z, (cz * c - cx * s) >> FIXED_SHIFT);
    out->rx = (uint16_t)rx;
    out->rz = (uint16_t)rz;
    out->yaw = item->angle.y;
    out->room = floorRoom;
    out->alpha = 128;
    out->pad = 0;
    return true;
}
VIEW_COLD uint32_t game_view_shadows(tr64_game_shadow_t *out, uint32_t capacity)
{
    if (!gs || !out || !capacity)
        return 0u;
    uint32_t count = 0u;
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE)) {
        if (!gs->tr2) return 0u;
        if (gs->players[0] && count < capacity && emitShadow(gs->players[0], out + count))
            ++count;
        for (unsigned i = 0; i < TR2_CREATURE_SLOTS && count < capacity; ++i) {
            const ItemObj *it = gs->tr2->creatures[i].item;
            if (it && emitShadow(it, out + count)) ++count;
        }
        for (unsigned i = 0; i < TR2_BOAT_SLOTS && count < capacity; ++i) {
            const TR2BoatHull *h = gs->tr2->boatHulls + i;
            if (!h->present || h->itemIndex < 0 || (uint32)h->itemIndex >= gs->level.itemsCount)
                continue;
            if (emitShadow(gs->items + h->itemIndex, out + count)) ++count;
        }
        return count;
    }
#endif
    if (gs->gLevelID == LVL_TR1_TITLE)
        return 0u;
    for (unsigned slot = 0; slot < TR64_GAME_PLAYERS && count < capacity; ++slot) {
        if (gs->players[slot] && emitShadow(gs->players[slot], out + count))
            ++count;
    }
    for (const ItemObj *it = gs->firstActive; it && count < capacity; it = it->nextActive) {
        if (it->type < ITEM_DOPPELGANGER || it->type > ITEM_ADAM ||
            it->type == ITEM_UNUSED_1 || it->type == ITEM_UNUSED_2 ||
            it->type == ITEM_SKATEBOARD || it->health <= 0)
            continue;
        if (emitShadow(it, out + count)) ++count;
    }
    return count;
}
static uint32 modelView(const ItemObj *item, uint32 identity, tr64_game_drawable_t *out,
                        bool lara,bool rest,bool menu)
{
#if TR64_HAS_TR2
    if (TR64_RULE(PACKED_POSE)) {
        if (rest) sys_error("Tomb Raider II: rest draw mode unavailable");
        return packedModelView(item,identity,out,lara);
    }
#endif
    const Model *model = gs->level.models + item->type;
    const tr64_u16_be_t *a, *b;
    const tr64_u32_be_t *nodeFlags = (const tr64_u32_be_t *)
        tr64_level_table(gs->core, TR64_CORE_node_flags)->data;
    Matrix *old = gs->gMatrixPtr, *rootMatrix;
    const Matrix *basis;
    int32 delta = 0, rate = 1, light, t;
    uint32 joint, count = 0, extraIndex = 0;
    vec3s root, torso = {0, 0, 0};
    if (lara && model->count != JOINT_MAX) sys_error("game Lara bone count");
    if (rest)
    {
        uint32 offset = tr64_u32(gs->level.anims[model->animIndex].frame_offset) / 2;
        if (offset + 10 + model->count * 2 > gs->level.framesCount)
            sys_error("game rest frame bounds");
        a = b = gs->level.animFrames + offset;
    }
    else
    {
        if (lara && item->extraL->hitQuadrant != -1)
        {
            ItemObj pose = *item;
            static const uint16 hit[] = {Lara_ANIM_HIT_FRONT, Lara_ANIM_HIT_LEFT,
                                        Lara_ANIM_HIT_BACK, Lara_ANIM_HIT_RIGHT};
            int32 quadrant = item->extraL->hitQuadrant;
            if (quadrant < 0 || quadrant > 3) sys_error("game hit quadrant");
            pose.animIndex = hit[quadrant];
            pose.frameIndex = tr64_u16(gs->level.anims[pose.animIndex].frame_start) +
                              item->extraL->hitFrame;
            game_frame_pair(&pose, &a, &b, &delta, &rate);
        }
        else
            game_frame_pair(item, &a, &b, &delta, &rate);
    }
    /* Fixed normal-only inventory meshes start at zero; MG64 encodes a 4096
     * neutral darkness baseline. Convert source ambient 255*32-4096 to the
     * native delta, rather than adding that baseline a second time. */
    light = menu ? ((255 << 5) - 4096 - 4096) : lightingOffset(item,a,rest);
    t = delta * divTable[rate];
    root = vec3s_create(rootWordLerp(a + 6, b + 6, t), rootWordLerp(a + 7, b + 7, t),
                        rootWordLerp(a + 8, b + 8, t));
    if (lara)
    {
        torso = item->extraL->torso.angle;
        if (item->extraL->weapon == WEAPON_SHOTGUN && item->extraL->armR.animIndex)
        {
            vec3s angle = decodedAngles(armFrame(item, &item->extraL->armR) + 10 + JOINT_TORSO * 2);
            torso.x = wrap_i16((uint32)torso.x + angle.x - 1216);
            torso.y = wrap_i16((uint32)torso.y + angle.y + 832);
            torso.z = wrap_i16((uint32)torso.z + angle.z + 192);
        }
    }
    matrixPush_c();
    if(!menu) {
        matrixSetIdentity_c();
        matrixRotateYXZ_c(item->angle.x,item->angle.y,item->angle.z);
    }
    basis = gs->gMatrixPtr;
    if (lara) matrixPush_c();
    rootMatrix = gs->gMatrixPtr;
    frameLerp(root, a + 10, b + 10, delta, rate);
    for (joint = 0; joint < (uint32)model->count; ++joint)
    {
        if (joint)
        {
            const tr64_i32_be_t *node = gs->level.nodes + (model->nodeIndex + joint - 1) * 4;
            uint32 flags = tr64_u32(nodeFlags[model->nodeIndex + joint - 1]);
            const ExtraInfoLara_Arm *arm = NULL;
            if (flags & NODE_FLAG_POP)
            {
                if (gs->gMatrixPtr <= rootMatrix) sys_error("game draw bone stack underflow");
                --gs->gMatrixPtr;
            }
            if (flags & NODE_FLAG_PUSH) matrixPush_c();
            if (lara && joint >= JOINT_ARM_R1 && joint <= JOINT_ARM_R3)
                arm = &item->extraL->armR;
            else if (lara && joint >= JOINT_ARM_L1 && joint <= JOINT_ARM_L3)
                arm = &item->extraL->armL;
            if (arm && (joint == JOINT_ARM_R1 || joint == JOINT_ARM_L1))
            {
                vec3s pos = nodePosition(node);
                matrixTranslateRel_c(pos.x, pos.y, pos.z);
                if (arm->useBasis)
                {
                    restoreBasis(gs->gMatrixPtr, basis);
                    matrixRotateYXZ_c(arm->angle.x, arm->angle.y, arm->angle.z);
                }
                if (arm->animIndex)
                    frameRotate(armFrame(item, arm) + 10 + joint * 2);
                else
                    frameLerp(vec3s_create(0, 0, 0), a + 10 + joint * 2,
                              b + 10 + joint * 2, delta, rate);
            }
            else if (arm && arm->animIndex)
                frameLerp(nodePosition(node), armFrame(item, arm) + 10 + joint * 2,
                          armFrame(item, arm) + 10 + joint * 2, 0, 1);
            else
                frameLerp(nodePosition(node), a + 10 + joint * 2, b + 10 + joint * 2, delta, rate);
            if (lara && joint == JOINT_TORSO)
                matrixRotateYXZ_c(torso.x, torso.y, torso.z);
            if (lara && joint == JOINT_HEAD)
                matrixRotateYXZ_c(item->extraL->head.angle.x, item->extraL->head.angle.y,
                                  item->extraL->head.angle.z);
            if (!lara && !rest && item->extraE)
            {
                uint32 axis;
                for (axis = 0; axis < 3; ++axis)
                    if (flags & (axis == 0 ? NODE_FLAG_ROTY : axis == 1 ? NODE_FLAG_ROTX : NODE_FLAG_ROTZ))
                    {
                        int32 angle;
                        switch (extraIndex++)
                        {
                        case 0: angle = item->extraE->rotHead; break;
                        case 1: angle = item->extraE->rotNeck; break;
                        case 2: angle = item->extraE->maxTurn; break;
                        case 3: angle = item->extraE->_reserved; break;
                        default: sys_error("game extra bone angle bounds");
                        }
                        if (axis == 0) matrixRotateY_c(angle);
                        else if (axis == 1) matrixRotateX_c(angle);
                        else matrixRotateZ_c(angle);
                    }
            }
        }
        if (lara || (joint < 32 && (item->visibleMask & (1u << joint))))
            drawable(out + count++, item, identity, joint,
                     lara ? item->extraL->meshes[joint] : model->start + joint, light);
        if (lara && (joint == JOINT_ARM_R3 || joint == JOINT_ARM_L3))
        {
            const ExtraInfoLara_Arm_Flash *flash = joint == JOINT_ARM_R3 ?
                &item->extraL->armR.flash : &item->extraL->armL.flash;
            if (flash->timer)
            {
                matrixPush_c();
                matrixTranslateRel_c(0, flash->offset, 55);
                matrixRotateYXZ_c(-16384, 0, flash->angle);
                drawable(out + count, item, identity, JOINT_MAX + (joint == JOINT_ARM_L3),
                         gs->level.models[ITEM_MUZZLE_FLASH].start, flash->intensity - 4096);
                out[count++].blend = TR64_GAME_DRAW_ADDITIVE;
                --gs->gMatrixPtr;
            }
        }
    }
    gs->gMatrixPtr = old;
    return count;
}
int game_hud_view_player(unsigned slot,tr64_game_hud_view_t *out)
{
    const Lara *lara;
    const ExtraInfoLara *extra;
    bool world;
    if (!gs || !out || slot >= TR64_GAME_PLAYERS || !(lara = gs->players[slot]))
        return 0;
    extra = lara->extraL;
    world = gs->inventory.state == INV_STATE_NONE;
    out->health = lara->health;
    out->health_max = LARA_MAX_HEALTH;
    out->air = lara->timer;
    out->air_max = LARA_MAX_OXYGEN;
    out->show_health = world && extra &&
                       (extra->healthTimer || extra->weaponState == WEAPON_STATE_READY);
    out->show_air = world && (lara->waterState == WATER_STATE_SURFACE ||
                              lara->waterState == WATER_STATE_UNDER);
    return 1;
}
int game_hud_view(tr64_game_hud_view_t *out)
{
    return game_hud_view_player(0,out);
}
int game_menu_view(tr64_game_menu_view_t *out)
{
    GameInventoryCall call={0};
    if(!gs || !out || gs->inventory.state==INV_STATE_NONE)return 0;
    call.op=GAME_INV_VIEW;
    call.view=out;
    code_call("inv",(uint32)(uintptr_t)&call);
    if(out->page!=INV_PAGE_END && out->page!=INV_PAGE_STATS)
        menuCamera(out->height,out->radius,out->pitch,out->camera_basis,out->camera_eye);
    return 1;
}
/* OpenLara src/level.h:25 is 10/30 s; lines 2292-2294 halve it for the first game only. */
void game_view_camera_player(unsigned slot,tr64_game_view_t *out)
{
    if (!gs || !out)
        sys_error("game camera view before level");
    if (slot >= TR64_GAME_PLAYERS || (slot && !gs->players[slot]))
        return;
    if(!TR64_CART_TR2 && gs->gLevelID==LVL_TR1_TITLE) {
        memset(out,0,sizeof(*out));
        menuCamera(gs->inventory.height,gs->inventory.radius,gs->inventory.pitch,
                   out->camera_basis,out->camera_eye);
        out->camera_room=UINT32_MAX;
        return;
    }
    const Camera *c=&gs->playersExtra[slot].camera;
    cameraView(out,c);
    if(!TR64_CART_TR2 && (gs->players[slot]==NULL || c->mode==CAMERA_MODE_CUTSCENE)) {
        /* camera.h:66,416: the eye and the yaw plus the cutscene's rotation
         * are what matrixSetView received, not view.pos/angle.y alone. */
        const vec3i *eye=gs->players[slot] ? &c->view.pos : &gs->gCameraViewPos;
        cameraBasis(out->camera_basis,c->angle.x,c->angle.y+c->targetAngle.y);
        out->camera_eye[0]=eye->x;out->camera_eye[1]=eye->y;out->camera_eye[2]=eye->z;
        out->camera_target[0]=eye->x;out->camera_target[1]=eye->y;out->camera_target[2]=eye->z;
    }
#if TR64_HAS_TR2
    out->texture_animation_step = gs->textureTicks ? (gs->textureTicks - 1) / (TR64_SECOND_GAME ? 10u : 5u) : 0;
#else
    out->texture_animation_step = gs->textureTicks ? (gs->textureTicks - 1) / 5 : 0;
#endif
}
void game_view_camera(tr64_game_view_t *out)
{
    game_view_camera_player(0,out);
}
static uint8 tr1SpriteBlend(uint32 type)
{
    switch (type)
    {
    case ITEM_FLAME:
    case ITEM_EXPLOSION:
    case ITEM_SPARKLES:
    case ITEM_LAVA_PARTICLE:
        return TR64_GAME_DRAW_ADDITIVE;
    case ITEM_SPLASH:
    case ITEM_BUBBLE:
    case ITEM_SMOKE:
        return TR64_GAME_DRAW_TRANSLUCENT;
    default:
        return TR64_GAME_DRAW_BLEND_NONE;
    }
}
uint32 game_view_room(uint32 room, uint32 *cursor, tr64_game_drawable_t *buffer, uint32 capacity)
{
    const ItemObj *item;
    uint32 written = 0, visited = 0, original;
    if (!gs || !cursor || room >= gs->level.roomsCount || (capacity && !buffer))
        sys_error("game room view arguments");
    original = *cursor;
    if (original == TR64_GAME_VIEW_END)
        return 0;
#if TR64_HAS_TR2
    if (original > MAX_ITEMS + (TR64_RULE(ROOM_EFFECTS) ? TR2_EFFECT_SLOTS : 0u))
        sys_error("game room view cursor");
    item = original > MAX_ITEMS ? NULL : (original ? gs->items + original - 1 : gs->rooms[room].firstItem);
#else
    if (original > MAX_ITEMS)
        sys_error("game room view cursor");
    item = original ? gs->items + original - 1 : gs->rooms[room].firstItem;
#endif
    while (item)
    {
        const Model *model;
        uint32 required, index, count = 0;
        if (++visited > MAX_ITEMS || item->room != gs->rooms + room)
            sys_error("game room view item link");
        if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE ||
            undrawn(item->type))
        {
            item = item->nextItem;
            continue;
        }
        required = modelCount(item, item->type == ITEM_LARA);
        if (!required)
        {
            item = item->nextItem;
            continue;
        }
        if (required > capacity - written)
        {
            if (!written)
                return required;
            *cursor = (uint32)(item - gs->items) + 1;
            return written;
        }
        index = (uint32)(item - gs->items);
        model = gs->level.models + item->type;
        if (!TR64_RULE(PACKED_POSE) && item->type == ITEM_MUZZLE_FLASH)
            count = flashView(item, index, buffer + written);
        else if (model->count > 0)
            count=modelView(item,index,buffer+written,item->type==ITEM_LARA,false,false);
        else
        {
            tr64_game_drawable_t *out = buffer + written;
            memset(out, 0, sizeof(*out));
            out->mesh_slot = model->start + item->frameIndex;
            out->instance_id = 0x80000000u + index;
            out->room = room;
            out->basis[0] = out->basis[4] = out->basis[8] = 16384;
            out->translation[0] = item->pos.x;
            out->translation[1] = item->pos.y;
            out->translation[2] = item->pos.z;
            out->light_offset = ((int32)item->intensity << 5) - 4096;
#if TR64_HAS_TR2
            if (TR64_RULE(PACKED_POSE))
                out->light_offset = tr2ApplyDynamicLight(out->light_offset, &item->pos);
#endif
            out->water = tr64_u16(item->room->info->flags) & ROOM_FLAG_WATER_MASK;
            out->kind = TR64_GAME_DRAW_SPRITE;
            if (!TR64_RULE(PACKED_POSE))
                out->blend = tr1SpriteBlend(item->type);
            count = 1;
        }
        if (count != required)
            sys_error("game room drawable count mismatch");
        written += count;
        item = item->nextItem;
    }
#if TR64_HAS_TR2
    if (TR64_RULE(ROOM_EFFECTS))
    {
        uint32 fxIndex = original > MAX_ITEMS ? original - MAX_ITEMS : gs->tr2->effectActive;
        uint32 fxVisited = 0;
        while (fxIndex)
        {
            if (++fxVisited > TR2_EFFECT_SLOTS || fxIndex > TR2_EFFECT_SLOTS)
                sys_error("game room view effect link");
            tr64_game_drawable_t fxDraw;
            if (tr2EffectDrawable(fxIndex, room, &fxDraw))
            {
                if (1u > capacity - written)
                {
                    if (!written)
                        return 1u;
                    *cursor = MAX_ITEMS + fxIndex;
                    return written;
                }
                buffer[written++] = fxDraw;
            }
            fxIndex = gs->tr2->effects[fxIndex - 1u].next;
        }
    }
#endif
    *cursor = TR64_GAME_VIEW_END;
    return written;
}
uint32 game_rest_item_view(uint32 index, tr64_game_drawable_t *buffer, uint32 capacity)
{
    ItemObj item;
    const tr64_core_items_t *source;
    uint32 required;
    if (!gs || index >= gs->level.itemsCount) sys_error("game rest item index");
    source = tr64_level_items(gs->core, index);
    item = gs->items[index];
    item.type = tr64_u16(source->type);
    item.intensity = tr64_i16(source->intensity) < 0 ? 0 : tr64_i16(source->intensity) >> 5;
    if (gs->level.models[item.type].count <= 0)
        sys_error("game rest item is not a model: item=%lu type=%u", (unsigned long)index, item.type);
    if (undrawn(item.type)) return 0;
    item.room = gs->rooms + tr64_u16(source->room);
    item.pos = vec3i_create(tr64_i32(source->x), tr64_i32(source->y), tr64_i32(source->z));
    item.angle = vec3s_create(0, ((int32)tr64_level_quadrant(source->angle) - 2) * 16384, 0);
    item.visibleMask = UINT32_MAX;
    required = modelCount(&item, false);
    if (required > capacity || !buffer) return required;
    return modelView(&item,index,buffer,false,true,false);
}
uint32 game_menu_item_view(const tr64_game_menu_view_t *view,uint32 index,
                          tr64_game_drawable_t *buffer,uint32 capacity)
{
    ItemObj item={0};
    Matrix *old;
    uint32 required,count;
    if(!gs || !view || view->state==INV_STATE_NONE ||
       view->page==INV_PAGE_END || view->page==INV_PAGE_STATS)return 0;
    if(view->itemsCount<0 || view->itemsCount>SLOT_MAX || index>=(uint32)view->itemsCount)
        sys_error("game inventory view item index");
    const tr64_game_menu_item_t *source=view->items+index;
    if((uint32)source->type>=
#if TR64_HAS_TR2
       (TR64_RULE(PACKED_POSE) ? TR2_PUBLIC_TYPES : ITEM_MAX) ||
#else
       ITEM_MAX ||
#endif
       gs->level.models[source->type].count<=0)
        sys_error("game inventory model missing: type=%ld",(long)source->type);
    item.type=source->type;
    item.animIndex=source->animIndex;
    item.frameIndex=source->frameIndex;
    item.visibleMask=source->visibleMask;
    required=modelCount(&item,false);
    if(required>capacity || !buffer)return required;
    old=gs->gMatrixPtr;
    matrixPush_c();matrixSetIdentity_c();
    matrixRotateY_c((int32)index*65536/view->itemsCount-view->rot-16384);
    matrixTranslateRel_c(view->radius,0,0);
    matrixRotateY_c(16384);
    if(view->itemIndex==(int32)index) {
        matrixRotateX_c(view->selRotPre);
        matrixTranslateRel_c(0,0,view->selDist);
        matrixRotateYXZ_c(-view->selRotX,-view->selRotY,0);
        matrixRotateY_c(view->rotItem);
    }
    count=modelView(&item,0x40000000u+index,buffer,false,false,true);
    gs->gMatrixPtr=old;
    return count;
}

static int32 pickupModelType(int32 type)
{
#if TR64_HAS_TR2
    if(TR64_RULE(PACKED_POSE)) {
        /* tr2-triggers-and-interaction, Pickups: world-to-ring destinations. */
        if((type>=135 && type<=150 && type!=142) || (type>=193 && type<=196))
            return type+(type>=193?4:22);
        if(type==151) return TR2_RING_FLARES;
        if(type>=174 && type<=177) return type+4;
        if(type==205 || type==206) return type+2;
        return type;
    }
#endif
    switch(type) {
#define PICKUP_MODEL(T) case ITEM_##T:return ITEM_INV_##T
    PICKUP_MODEL(PISTOLS);PICKUP_MODEL(SHOTGUN);
    PICKUP_MODEL(MAGNUMS);PICKUP_MODEL(UZIS);
    PICKUP_MODEL(AMMO_PISTOLS);PICKUP_MODEL(AMMO_SHOTGUN);
    PICKUP_MODEL(AMMO_MAGNUMS);PICKUP_MODEL(AMMO_UZIS);
    PICKUP_MODEL(MEDIKIT_SMALL);PICKUP_MODEL(MEDIKIT_BIG);
    PICKUP_MODEL(KEY_ITEM_1);PICKUP_MODEL(KEY_ITEM_2);
    PICKUP_MODEL(KEY_ITEM_3);PICKUP_MODEL(KEY_ITEM_4);
    PICKUP_MODEL(PUZZLE_1);PICKUP_MODEL(PUZZLE_2);
    PICKUP_MODEL(PUZZLE_3);PICKUP_MODEL(PUZZLE_4);
    PICKUP_MODEL(LEADBAR);
#undef PICKUP_MODEL
    case ITEM_SCION_PICKUP_DROP:case ITEM_SCION_PICKUP_QUALOPEC:return ITEM_INV_SCION;
    default:return type;
    }
}
uint32_t game_pickup_item_view(int32_t type,int16_t angle_y,
                             tr64_game_drawable_t *buffer,uint32_t capacity)
{
    ItemObj item={0};
    if(!gs || (!TR64_RULE(PACKED_POSE) && type==ITEM_SCION_PICKUP_HOLDER))return 0; /* src/lara.h:3206 */
    type=pickupModelType(type);
    if((uint32)type>=
#if TR64_HAS_TR2
       (TR64_RULE(PACKED_POSE) ? TR2_PUBLIC_TYPES : ITEM_MAX) ||
#else
       ITEM_MAX ||
#endif
       gs->level.models[type].count<=0)return 0;
    item.type=type;
    item.animIndex=gs->level.models[type].animIndex;
#if TR64_HAS_TR2
    if(TR64_RULE(PACKED_POSE) && item.animIndex>=gs->tr2->animCount)
        sys_error("Tomb Raider II: missing pickup pose for type %ld",(long)type);
#endif
    item.frameIndex=tr64_u16(gs->level.anims[item.animIndex].frame_start);
    item.visibleMask=UINT32_MAX;
    uint32 required=modelCount(&item,false);
    if(!buffer || capacity<required)return required;
    Matrix *old=gs->gMatrixPtr;
    matrixPush_c();matrixSetIdentity_c();
    matrixRotateX_c(-ANGLE(15));matrixRotateY_c(angle_y);
    uint32 count=modelView(&item,0,buffer,false,false,true);
    gs->gMatrixPtr=old;
    return count;
}
