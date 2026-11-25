/* SPDX-License-Identifier: MIT */
/* Link game/animation.c with section GC: stored tails, shortened divisor,
 * retained out-of-clip destinations and packed storage bounds. */
#include <stdarg.h>
#include <stdio.h>
#include "game/internal.h"

GameState *gs;

void sys_error(const char *fmt, ...)
{
    va_list args;
    va_start(args,fmt);
    vfprintf(stderr,fmt,args);
    va_end(args);
    fputc('\n',stderr);
    exit(23);
}

static tr64_u16_be_t be16(unsigned v)
{
    return (tr64_u16_be_t){.raw=tr64_core_be16((uint16)v)};
}

static void check(void)
{
    ItemObj lara = {.animIndex=0,.frameIndex=104};
    PackedFramePair p = game_tr2_packed_frames(&lara);
    if (p.delta != 1 || p.rate != 3 || p.a.extent != 10 || p.b.extent != 10 ||
        p.a.words != gs->level.animFrames + 10 || p.b.words != gs->level.animFrames + 20)
        sys_error("tail must use the physically stored key and nominal spacing");
    lara.frameIndex = 103;
    p = game_tr2_packed_frames(&lara);
    if (p.delta || p.a.words != p.b.words || p.a.extent != p.b.extent)
        sys_error("an exact packed key must not sample the successor");
}

int main(int argc,char **argv)
{
    static tr64_u16_be_t words[210];
    static Anim anims[2];
    anims[0].frame_start = be16(100);
    anims[0].frame_end = be16(104);
    anims[0].next_animation = be16(1);
    anims[0].frame_size = 10;
    anims[0].frame_rate = 3;
    anims[1].frame_offset.raw = tr64_core_be32(60);
    anims[1].frame_size = 9;
    anims[1].frame_rate = 1;
    static GameTR2 probe = {.animCount=2};
    static GameState world = {.level={.anims=anims,.animFrames=words,.framesCount=210},
                              .tr2=&probe,.tr2Rules=TR2_RULES_ALL};
    gs = &world;
#ifndef FROZEN_V2
    world.tr2Rules|=TR2_RULES_CAMERA;
#endif
    if (argc == 2 && !strcmp(argv[1],"far")) {
        world.level.framesCount=20; /* The preceding key fits; the far key doesn't. */
        ItemObj item={.animIndex=0,.frameIndex=104};
        (void)game_tr2_packed_frames(&item);
        return 1;
    }
    if (argc == 2 && !strcmp(argv[1],"current")) {
        ItemObj item = {.animIndex=1,.frameIndex=20};
        (void)game_tr2_packed_frames(&item);
        return 1;
    }
    if (argc != 1)
        return 2;
    check();
    anims[0].frame_start=be16(0);
    anims[0].frame_end=be16(4);
    ItemObj tail = {.animIndex=0,.frameIndex=4};
    PackedFramePair p = game_tr2_packed_frames(&tail);
    if (p.rate!=1 || p.delta!=1 || p.b.words!=words+20)
        sys_error("relative next frame beyond the absolute end must shorten the divisor");
    ItemObj retained = {.animIndex=1,.frameIndex=19};
    p = game_tr2_packed_frames(&retained);
    if (p.delta || p.a.words != words+201 || p.b.words != words+201)
        sys_error("retained out-of-clip exact key was changed or clamped");
    puts("PASS: packed-key extent, stored tail and signed divisor selection");
    return 0;
}
