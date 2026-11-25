/* SPDX-License-Identifier: MIT */
/* Link game/lara.c with section GC to exercise the actual host water helpers. */
#include "game/internal.h"
#include <stdio.h>

static void expect(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void)
{
    ExtraInfoLara extra = {0};
    Lara lara = {.health=1000, .timer=1, .waterState=WATER_STATE_UNDER, .extraL=&extra};
    game_tr2_water_air(&lara);
    expect(lara.timer == 0 && lara.health == 1000, "zero air is not damage");
    game_tr2_water_air(&lara);
    expect(lara.timer == -1 && lara.health == 995, "first drowning tick");
    game_tr2_water_air(&lara);
    expect(lara.timer == -1 && lara.health == 990, "continued drowning clamps air");
    lara.waterState = WATER_STATE_SURFACE;
    game_tr2_water_air(&lara);
    expect(lara.timer == 9 && lara.health == 990, "surface restores ten air");
    lara.timer = 1798;
    game_tr2_water_air(&lara);
    expect(lara.timer == 1800, "surface caps air");
    lara.waterState = WATER_STATE_WADE;
    lara.timer = 900;
    game_tr2_water_air(&lara);
    expect(lara.timer == 1800, "wade restores full air in one tick");
    lara.waterState = WATER_STATE_ABOVE;
    lara.timer = 900;
    game_tr2_water_air(&lara);
    expect(lara.timer == 1800, "dry restores full air in one tick");

    lara.waterState = WATER_STATE_SURFACE;
    lara.vSpeed = 7;
    lara.pos = vec3i_create(41, -13, 65);
    extra.moveAngle = ANGLE_180;
    game_tr2_water_move(&lara);
    expect(lara.pos.x == 41 && lara.pos.y == -13 && lara.pos.z == 63,
           "surface uses retained heading and rounds negative displacement down");
    lara.waterState = WATER_STATE_UNDER;
    lara.angle.x = -ANGLE_90;
    game_tr2_water_move(&lara);
    expect(lara.pos.x == 41 && lara.pos.y == -11 && lara.pos.z == 63,
           "submerged displacement uses the updated pitch");
    lara.angle.x = -ANGLE_45;
    lara.angle.y = -ANGLE_90;
    game_tr2_water_move(&lara);
    expect(lara.pos.x == 39 && lara.pos.y == -9 && lara.pos.z == 63,
           "submerged X/Z round independently before multiplication by pitch cosine");
    puts("PASS: underwater air/health edges and wet signed projection");
    return 0;
}
