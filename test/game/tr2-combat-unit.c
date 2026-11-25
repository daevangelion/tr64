/* SPDX-License-Identifier: MIT */
/* Link the real arm, RNG and pose functions with section GC. */
#include "game/internal.h"
#include <stdio.h>

static void expect(bool pass, const char *why)
{
    if (!pass) {
        fprintf(stderr,"FAIL: %s\n",why);
        exit(1);
    }
}

int main(void)
{
    static GameState state;
    gs=&state;
    state.gRandSeedLogic=state.gRandSeedDraw=3547461959u;
    vec3s right=game_tr2_pistol_ray_angles(-500,-1200,-15000);
    vec3s left=game_tr2_pistol_ray_angles(-600,900,-15000);
    expect(right.x==-357 && right.y==-16384 && left.x==-480 && left.y==-13994,
           "first random value is pitch, second yaw, right draws before left");
    expect(state.gRandSeedLogic==1162535651u && state.gRandSeedDraw==3547461959u,
           "only the four gameplay draws advance");

    expect(game_tr2_sphere_entry(3,4,21,5)==16,"non-axis tangent qualifies");
    expect(game_tr2_sphere_entry(5,0,21,5)==INT64_MAX,"strict perpendicular bound");
    expect(game_tr2_sphere_entry(4,4,21,5)==INT64_MAX,"circle, not square");
    expect(game_tr2_sphere_entry(0,0,5,5)==INT64_MAX,"strict forward depth");
    expect(game_tr2_sphere_entry(0,0,-20,5)==INT64_MAX,"sphere behind ray");
    expect(game_tr2_sphere_entry(INT64_MAX,0,21,5)==INT64_MAX,"bounds precede squares");
    expect(game_tr2_sphere_entry(0,0,21,0)==INT64_MAX,"empty mesh sphere");
    puts("PASS: exact pitch/yaw draws, gameplay stream and sphere boundaries");
    return 0;
}
