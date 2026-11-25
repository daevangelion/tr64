/* SPDX-License-Identifier: MIT */
/* Directed test link. The no-target and repress routes have no creature,
 * so they cannot gain or lose a lock. Not a game rule. */
#include "game/internal.h"
#include <stdlib.h>
void tr64_shotgun_test_arm(ExtraInfoLara *e, bool action)
{
    const char *mode = getenv("TR64_SHOTGUN_CONTROL");
    ItemObj *pose = gs->tr2 ? gs->tr2->shotgunPose : NULL;
    static int mid;
    if (!mode || !pose)
        return;
    unsigned base = gs->level.models[TR2_MODEL_SHOTGUN].animIndex;
    if (pose->animIndex != base + SHOTGUN_RECOIL) {
        mid = 0;
        e->armL.target = e->armR.target = NULL;
        e->armL.aim = e->armR.aim = false;
        return;
    }
    unsigned frame = pose->frameIndex - tr64_u16(gs->level.anims[pose->animIndex].frame_start);
    if (frame >= 1)
        mid = 1;
    if (mode[0] == 'k') {
        e->armL.target = e->armR.target = gs->items;
        e->armL.aim = e->armR.aim = true;
        return;
    }
    if (!action) {
        e->armL.target = e->armR.target = NULL;
        e->armL.aim = e->armR.aim = false;
        return;
    }
    if (mode[0] == 'l' && !frame && !mid)
        return;
    e->armL.target = e->armR.target = gs->items;
    e->armL.aim = e->armR.aim = mode[0] == 'g';
}
