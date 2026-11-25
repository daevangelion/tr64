/* SPDX-License-Identifier: MIT */
/* Directed presentation check. Not part of a sealed comparison. */
#include "game/internal.h"
#include "tr64/game.h"
#include <stdio.h>

void __real_game_tr2_tick(uint32 mask);

void __wrap_game_tr2_tick(uint32 mask)
{
    __real_game_tr2_tick(mask);
    Lara *lara = gs->players[0];
    ExtraInfoLara *e = lara->extraL;
    uint32 room = (uint32)(lara->room - gs->rooms);
    uint32 id = 0x80000000u + (uint32)(lara - gs->items);
    uint32 cursor = 0;
    tr64_game_drawable_t buf[128];
    printf("BODY %u %u %u %u %u %u %d\n", gs->tick, lara->animIndex, lara->frameIndex,
           e->weaponState, e->weapon, e->armL.animIndex, lara->angle.y);
    while (cursor != TR64_GAME_VIEW_END) {
        uint32 n = game_view_room(room, &cursor, buf, 128);
        if (n > 128) sys_error("arm release control: room item exceeds the dump buffer");
        for (uint32 i = 0; i < n; ++i) {
            if (buf[i].instance_id != id) continue;
            if (buf[i].bone < JOINT_ARM_R1 || buf[i].bone > JOINT_ARM_L3) continue;
            printf("ARM %u %u", gs->tick, buf[i].bone);
            for (unsigned k = 0; k < 9; ++k) printf(" %d", buf[i].basis[k]);
            printf("\n");
        }
        if (cursor == TR64_GAME_VIEW_END) break;
    }
    fflush(stdout);
}
