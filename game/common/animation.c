/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/item.h:121-223,302-431 and common.cpp:1365-1426
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

#if TR64_HAS_SEQUEL
/* Authored PC banks end at the next model's start. */
unsigned game_tr2_model_bank_end(unsigned base)
{
    unsigned end = gs->tr2->animCount;
    if (base >= end)
        return end;
    for (unsigned type = 0; type < TR2_PUBLIC_TYPES; ++type) {
        const Model *next = gs->level.models + type;
        if (next->count > 0 && next->animIndex > base && next->animIndex < end)
            end = next->animIndex;
    }
    return end;
}

uint8 game_tr2_shotgun_clip_state(unsigned clip)
{
    unsigned state = tr64_u16(gs->level.anims[clip].state);
    if (state > UINT8_MAX)
        sys_error("Tomb Raider II: shotgun clip %u state %u does not fit the item state", clip, state);
    return (uint8)state;
}

bool game_tr2_shotgun_checking;

/* cite non-null: the specification names that destination clip.
 * cite null: the destination is whatever the level's range names. */
static const TR2ShotgunReq tr2ShotgunReq[] = {
    { SHOTGUN_AIM, SHOTGUN_RECOIL, TR2_SG_GOAL, "tr2-lara-arms.md:79" },
    { SHOTGUN_AIM, SHOTGUN_LOWER, TR2_SG_GOAL, 0 },
    { SHOTGUN_RECOIL, SHOTGUN_LOWER, TR2_SG_FRAME0, 0 },
    { SHOTGUN_RECOIL, SHOTGUN_LOWER, TR2_SG_CITED4, "tr2-lara-arms.md:85" },
    { SHOTGUN_RECOIL, SHOTGUN_AIM, TR2_SG_FRAME0, 0 },
    { SHOTGUN_RECOIL, SHOTGUN_AIM, TR2_SG_CITED4, "tr2-lara-arms.md:85" },
    { SHOTGUN_RECOIL, SHOTGUN_RECOIL, TR2_SG_FRAME0, "tr2-lara-arms.md:80" },
    { SHOTGUN_RECOIL, SHOTGUN_RECOIL, TR2_SG_LOOP0, "tr2-lara-arms.md:85" },
    { TR2_SHOTGUN_ANY, SHOTGUN_STOW, TR2_SG_GOAL, 0 },
    { SHOTGUN_DRAW, SHOTGUN_AIM, TR2_SG_EXIT, "tr2-lara-arms.md:78" },
};

const TR2ShotgunReq *game_tr2_shotgun_reqs(unsigned *count)
{
    *count = (unsigned)(sizeof(tr2ShotgunReq) / sizeof(*tr2ShotgunReq));
    return tr2ShotgunReq;
}

unsigned game_tr2_shotgun_pick(unsigned from, unsigned to, unsigned kind)
{
    for (unsigned i = 0; i < sizeof(tr2ShotgunReq) / sizeof(*tr2ShotgunReq); ++i)
        if (tr2ShotgunReq[i].from == from && tr2ShotgunReq[i].to == to &&
            tr2ShotgunReq[i].kind == kind)
            return tr2ShotgunReq[i].to;
    return SHOTGUN_CLIPS;
}

/* tr2-lara-arms.md:85 names goal 4 and the two ranges that choose the clip. */
enum { TR2_SHOTGUN_EXIT_GOAL = 4 };

bool game_tr2_shotgun_goal(unsigned from, unsigned to, unsigned kind, unsigned frameIndex, uint8 *goal)
{
    (void)frameIndex;
    (void)from;
    unsigned base = gs->level.models[TR2_MODEL_SHOTGUN].animIndex;
    if (kind == TR2_SG_CITED4) {
        *goal = TR2_SHOTGUN_EXIT_GOAL;
        return true;
    }
    if (kind == TR2_SG_LOOP0) {
        *goal = 0;
        return true;
    }
    if (kind == TR2_SG_EXIT || to >= SHOTGUN_CLIPS)
        return false;
    *goal = game_tr2_shotgun_clip_state(base + to);
    return true;
}

/* tr2-camera, Footfalls: test the prior raw eye before this item moves. */
static void tr2Footfall(const ItemObj *item)
{
    Camera *camera=&gs->playersExtra[0].camera;
    int64_t x=(int64_t)item->pos.x-camera->view.pos.x;
    int64_t y=(int64_t)item->pos.y-camera->view.pos.y;
    int64_t z=(int64_t)item->pos.z-camera->view.pos.z;
    if (llabs(x)>=16384 || llabs(y)>=16384 || llabs(z)>=16384) return;
    camera->bounce=(1048576-(int32)((x*x+y*y+z*z)>>8))*100/1048576;
}

static void tr2LaraEffect(Lara *lara, unsigned word)
{
    unsigned effect = word;
    if (TR64_RULE(WATER)) {
        /* Water and flares, Numbered animation effects: the high
         * bits filter the Lara cue with the pre-movement waterline. */
        int32 distance = gs->tr2->waterDistance;
        unsigned flags = word >> 14;
        if (flags == 3 || (flags == 1 && distance < 0 && distance != WALL) ||
            (flags == 2 && distance >= 0))
            return;
        effect &= 0x3fff;
    }
    switch (effect) {
    case TR2_ANIM_TURN:
        /* Water and flares, Defined effect numbers: turn reverses
         * pitch as well as yaw, independently of the current clip. */
        lara->angle.y = wrap_i16((uint16)lara->angle.y + 32768u);
        if (TR64_RULE(WATER) || (TR64_RULE(AIR_TWIST) &&
            (lara->animIndex == Lara_ANIM_UW_ROLL || lara->animIndex == 207 ||
             lara->animIndex == 210 || lara->animIndex == 212)))
            lara->angle.x = wrap_i16(-(int32)lara->angle.x);
        break;
    case TR2_ANIM_HANDS_FREE:
        /* docs/specs/tr2-tr3-differences.md §4 and the II ledge rules. */
        lara->extraL->weaponState = WEAPON_STATE_FREE;
        break;
    case TR2_ANIM_BUBBLES:
        /* Presentation effect deferred: count without drawing RNG. */
        ++gs->tr2->arms.inertBubbleCues;
        break;
    case TR2_ANIM_RESERVED_16:
    case TR2_ANIM_RESERVED_17:
        break; /* Water and flares, Defined effect numbers: valid inert slots. */
    default:
        sys_error("Tomb Raider II: unvalidated Lara animation effect %u", effect);
    }
}

static void refusedKey(unsigned index,unsigned frame)
{
    ++gs->tr2->badFrameQueries;
    sys_error("Tomb Raider II: packed key outside available frame data: animation=%u frame=%u count=%u",
              index,frame,(unsigned)gs->tr2->badFrameQueries);
}

static const Anim *packedClip(unsigned index,unsigned frame)
{
    if (index >= gs->tr2->animCount)
        refusedKey(index,frame);
    return gs->level.anims + index;
}

PackedFrame game_tr2_packed_key(const Anim *anim, unsigned index, unsigned frame)
{
    unsigned base = tr64_u32(anim->frame_offset) / 2, extent = anim->frame_size;
    if (!anim->frame_rate || extent < 9)
        refusedKey(index,frame);
    int32 elapsed = (int32)frame - tr64_u16(anim->frame_start);
    int32 key = elapsed / anim->frame_rate;
    int64_t offset = (int64_t)base + (int64_t)key * extent;
    unsigned n = gs->level.framesCount;
    if (offset < 0 || offset > n || extent > n - (unsigned)offset)
        refusedKey(index,frame);
    return (PackedFrame){gs->level.animFrames + (unsigned)offset,extent};
}

/* OpenLara src/animation.h:99–105,120–145 supplies the packed stride.
 * tr2-lara-start.md, Authored animation links: a retained destination
 * may be beyond its clip's declared range; use the stored frame, with
 * each selected key still bounded by the global packed frame storage. */
PackedFramePair game_tr2_packed_frames(const ItemObj *self)
{
    unsigned index = self->animIndex, frame = self->frameIndex;
    const Anim *anim = packedClip(index,frame);
    unsigned base = tr64_u32(anim->frame_offset) / 2, extent = anim->frame_size;
    int32 rate = anim->frame_rate;
    if (!rate || extent < 9)
        refusedKey(index,frame);
    int32 begin = tr64_u16(anim->frame_start), end = tr64_u16(anim->frame_end);
    int32 elapsed = (int32)frame - begin;
    int32 key = rate == 1 ? elapsed : elapsed / rate;
    int32 delta = rate == 1 ? 0 : elapsed - key * rate;
    unsigned n = gs->level.framesCount;
    int64_t offA = (int64_t)base + (int64_t)key * extent;
    if (offA < 0 || offA > n || extent > n - (unsigned)offA)
        refusedKey(index,frame);
    PackedFrame a = {gs->level.animFrames + (unsigned)offA, extent};
    PackedFrame b = a;
    if (delta) {
        int32 nextFrame = (int32)frame - delta + rate;
        if (TR64_RULE(POSE_TAIL)) {
            /* Authored absolute end versus the next key's relative frame. */
            if (nextFrame - begin > end)
                rate = end - (nextFrame - begin - rate);
            if (rate <= 0)
                refusedKey(index,frame);
            int64_t offB = offA + extent;
            if (offB < 0 || offB > n || extent > n - (unsigned)offB)
                refusedKey(index,frame);
            b = (PackedFrame){gs->level.animFrames + (unsigned)offB, extent};
        } else if (frame < (unsigned)begin || frame > (unsigned)end || nextFrame <= end) {
            int64_t offB = offA + extent;
            if (offB < 0 || offB > n || extent > n - (unsigned)offB)
                refusedKey(index,frame);
            b = (PackedFrame){gs->level.animFrames + (unsigned)offB, extent};
        } else {
            unsigned next = tr64_u16(anim->next_animation);
            unsigned target = tr64_u16(anim->next_frame);
            b = game_tr2_packed_key(packedClip(next,target),next,target);
            rate = end - ((int32)frame - delta) + 1;
        }
    }
    return (PackedFramePair){a,b,delta,rate};
}
#endif

/* Frame pointers are encoded words, not cast native C structs. The tenth
 * header word is the source joint count; packed rotations begin at word 10. */
static void getFrames(const ItemObj *self, const tr64_u16_be_t **a, const tr64_u16_be_t **b,
                      int32 *delta, int32 *rate)
{
#if TR64_HAS_SEQUEL
    if (TR64_RULE(PACKED_POSE)) {
        PackedFramePair pair = game_tr2_packed_frames(self);
        *a = pair.a.words;
        *b = pair.b.words;
        *delta = pair.delta;
        *rate = pair.rate;
        return;
    }
#endif
    const Anim *anim = gs->level.anims + self->animIndex;
    uint32 base = tr64_u32(anim->frame_offset) / 2;
    uint32 begin = tr64_u16(anim->frame_start), end = tr64_u16(anim->frame_end);
    int32 size = 10 + gs->level.models[self->type].count * 2;
    int32 frame = self->frameIndex - begin, indexA, indexB;
    *rate = anim->frame_rate;
    if (begin == end)
    {
        *a = *b = gs->level.animFrames + base;
        *rate = 1;
        *delta = 0;
        return;
    }
    indexA = frame / *rate;
    *delta = frame - indexA * *rate;
    indexB = indexA + 1;
    if (indexB * *rate >= (int32)end)
        indexB = indexA;
    *a = gs->level.animFrames + base + indexA * size;
    *b = gs->level.animFrames + base + indexB * size;
    if (!*delta || *a == *b)
    {
        *delta = 0;
        return;
    }
    indexB *= *rate;
    if (indexB > (int32)end)
        *rate -= indexB - end;
}
static int16 frameSigned(const tr64_u16_be_t *p)
{
    uint32 n = tr64_u16(*p);
    return (int16)((int32)(n & 32767) - (int32)(n & 32768));
}
static AABBs frameBox(const tr64_u16_be_t *p)
{
    AABBs b = {frameSigned(p),     frameSigned(p + 1), frameSigned(p + 2),
               frameSigned(p + 3), frameSigned(p + 4), frameSigned(p + 5)};
    return b;
}
const AABBs *ItemObj_getBoundingBox(const ItemObj *self, bool lerp)
{
    const tr64_u16_be_t *a, *b;
    int32 delta, rate, t;
    AABBs ba, bb;
    getFrames(self, &a, &b, &delta, &rate);
    if (!lerp)
        a = delta <= (rate >> 1) ? a : b;
    ba = frameBox(a);
    if (!lerp || !delta)
    {
        gs->tmpBox = ba;
        return &gs->tmpBox;
    }
    bb = frameBox(b);
#if TR64_HAS_SEQUEL
    if (TR64_RULE(BOUNDING_LERP)) {
        /* TR2 bounds interpolate in integers, truncating the change (not
         * the result) toward zero: tr2-lara-slide-height-and-gaze.md. */
#define BOUND(field) gs->tmpBox.field = ba.field + ((bb.field - ba.field) * delta) / rate
        BOUND(minX); BOUND(maxX); BOUND(minY); BOUND(maxY); BOUND(minZ); BOUND(maxZ);
#undef BOUND
        return &gs->tmpBox;
    }
#endif
    t = divTable[rate] * delta;
#define LERP_FIELD(f)                                                                              \
    gs->tmpBox.f =                                                                                 \
        wrap_i16((uint32)ba.f + (uint32)(wrap_i32((uint32)(bb.f - ba.f) * (uint32)t) >> 16))
    LERP_FIELD(minX);
    LERP_FIELD(maxX);
    LERP_FIELD(minY);
    LERP_FIELD(maxY);
    LERP_FIELD(minZ);
    LERP_FIELD(maxZ);
#undef LERP_FIELD
    return &gs->tmpBox;
}
void game_frame_pair(const ItemObj *self, const tr64_u16_be_t **a, const tr64_u16_be_t **b,
                     int32 *delta, int32 *rate)
{
    getFrames(self, a, b, delta, rate);
}

/* Predict the authored event without playing motion, sound or effects on
 * the dead Lara. These committed animations don't depend on further input. */
uint16 ItemObj_ticksToInteractionEvent(const InteractionAnim *key,bool levelEnd)
{
    ItemObj cursor={.animIndex=key->anim,.frameIndex=key->frame,
                    .state=key->state,.goalState=key->goal};
    const Anim *anim=gs->level.anims+cursor.animIndex;
    for (uint16 tick=1;tick<4096;++tick) {
        ++cursor.frameIndex;
        anim=ItemObj_animChange(&cursor,anim);
        bool end=cursor.frameIndex>tr64_u16(anim->frame_end);
        for (unsigned pass=0;pass<1u+(unsigned)end;++pass) {
            if (pass) {
                cursor.frameIndex=tr64_u16(anim->next_frame);
                cursor.animIndex=tr64_u16(anim->next_animation);
                anim=gs->level.anims+cursor.animIndex;
                cursor.state=tr64_u16(anim->state);
            }
            const tr64_i16_be_t *p=gs->level.animCommands+tr64_u16(anim->command_offset);
            for (unsigned i=0;i<tr64_u16(anim->command_count);++i) {
                switch (tr64_i16(*p++)) {
                case ANIM_CMD_OFFSET: p+=3; break;
                case ANIM_CMD_JUMP: case ANIM_CMD_SOUND: p+=2; break;
                case ANIM_CMD_EMPTY: if (!levelEnd && end && !pass)return tick; break;
                case ANIM_CMD_EFFECT:
                    if ((!end || pass) && cursor.frameIndex==tr64_i16(p[0]) &&
                        tr64_i16(p[1])==(levelEnd ? FX_FINISH_LEVEL : FX_LARA_HANDSFREE))return tick;
                    p+=2;
                    break;
                }
            }
        }
    }
    sys_error("game committed interaction animation never reaches its event");
    return 1;
}


void ItemObj_animCmd(ItemObj *self, bool fx, const Anim *anim)
{
#if TR64_HAS_SEQUEL
    if (game_tr2_shotgun_checking)
        return;
#endif
    const tr64_i16_be_t *ptr = gs->level.animCommands + tr64_u16(anim->command_offset);
    uint32 count = tr64_u16(anim->command_count), i;
    for (i = 0; i < count; ++i)
    {
        int32 cmd = tr64_i16(*ptr++);
        switch (cmd)
        {
        case ANIM_CMD_NONE:
            break;
        case ANIM_CMD_OFFSET:
            if (!fx)
            {
                uint32 sc = gSinCosTable[(uint16)self->angle.y >> 4];
                int32 s = wrap_i32(sc) >> 16, c = (int16)(sc & 65535);
                int32 x = tr64_i16(ptr[0]), z = tr64_i16(ptr[2]);
                self->pos.x = i32_add(self->pos.x, i32_add((uint32)x * c, (uint32)z * s) >> 14);
                self->pos.y = i32_add(self->pos.y, tr64_i16(ptr[1]));
                self->pos.z = i32_add(self->pos.z, i32_sub(i32_mul(z, c), i32_mul(x, s)) >> 14);
            }
            ptr += 3;
            break;
        case ANIM_CMD_JUMP:
            if (!fx)
            {
                if (self->type == ITEM_LARA && self->extraL->vSpeedHack)
                {
                    self->vSpeed = -self->extraL->vSpeedHack;
                    self->extraL->vSpeedHack = 0;
                }
                else
                    self->vSpeed = tr64_i16(ptr[0]);
                self->hSpeed = tr64_i16(ptr[1]);
                self->flags |= ITEM_FLAG_GRAVITY;
            }
            ptr += 2;
            break;
        case ANIM_CMD_EMPTY:
            if (!fx)
            {
                if (self->type != ITEM_LARA || !self->extraL)
                    sys_error("game Lara-only empty animation command");
                self->extraL->weaponState = WEAPON_STATE_FREE;
            }
            break;
        case ANIM_CMD_KILL:
            if (!fx)
                self->flags = (self->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INACTIVE;
            break;
        case ANIM_CMD_SOUND:
            if (fx && self->frameIndex == tr64_i16(ptr[0]))
            {
#if TR64_HAS_TR3
                uint16 w = (uint16)tr64_i16(ptr[1]);
                unsigned sfl = w >> 14;
                bool wet = (self->waterState == WATER_STATE_UNDER ||
                            self->waterState == WATER_STATE_SURFACE ||
                            self->waterState == WATER_STATE_WADE);
                if (!((sfl == 1 && wet) || (sfl == 2 && !wet) ||
                      (sfl != 0 && game_tr3_in_swamp(self))))
                    soundPlay(w & 0x3fff, &self->pos);
#else
                soundPlay((uint16)tr64_i16(ptr[1]) & 0x3fff, &self->pos);
#endif
            }
            ptr += 2;
            break;
        case ANIM_CMD_EFFECT:
            if (fx && self->frameIndex == tr64_i16(ptr[0]))
            {
#if TR64_HAS_SEQUEL
                if (TR64_RULE(CREATURES) && self->type==TR2_ITEM_REX && tr64_i16(ptr[1])==FX_FLOOR_SHAKE) {
                    tr2Footfall(self);
                    ptr+=2;
                    break;
                }
                if (TR64_RULE(ARMS)) {
                    if (self->type != ITEM_LARA || !self->extraL)
                        sys_error("Tomb Raider II: unvalidated world animation effect");
                    tr2LaraEffect(self, (uint16)tr64_i16(ptr[1]));
                    ptr += 2;
                    break;
                }
#endif
                switch (tr64_i16(ptr[1])
#if TR64_HAS_TR3
                        & 0x3fff
#endif
                )
                {
                case FX_ROTATE_180:
                    self->angle.y = (int16)((uint16)self->angle.y + 32768u);
#if !TR64_HAS_TR3
                    if (self->type == ITEM_LARA && game_lara_has_tr2() &&
                        (self->state == Lara_STATE_ROLL_UW || self->waterState == WATER_STATE_UNDER))
                        self->angle.x = wrap_i16(-(int32)self->angle.x);
#endif
                    break;
                case FX_LARA_NORMAL:
                    if (self->type != ITEM_LARA)
                        sys_error("game Lara-only normal animation effect");
                    ItemObj_animSet(self, 11, true, 0);
                    break;
                case FX_LARA_BUBBLES:
                {
                    const vec3i offset = {0, 0, 50};
                    ItemObj_fxBubbles(self, self->room, JOINT_HEAD, &offset);
                    break;
                }
                case FX_LARA_HANDSFREE:
                    if (self->type != ITEM_LARA || !self->extraL)
                        sys_error("game Lara-only hands-free animation effect");
                    self->extraL->weaponState = WEAPON_STATE_FREE;
                    break;
                case FX_FINISH_LEVEL:
                    /* Repaired fork defect (oracle host.patch hunk 13): item.h ignored it, so
                     * LEVEL3B, LEVEL8C and LEVEL10B could not end; non-fixed
                     * controller.h:1173 loads the next level. The END trigger's path. */
                    game_complete_level(self->type == ITEM_LARA ? self : NULL);
                    break;
                default:
                    break; /* Other effect opcodes are also ignored in item.h. */
                }
            }
            ptr += 2;
            break;
        default:
            sys_error("game animation opcode %ld", (long)cmd);
        }
    }
}
