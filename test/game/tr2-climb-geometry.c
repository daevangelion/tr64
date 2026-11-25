/* SPDX-License-Identifier: MIT */
/* Include the private hand probe; link game/{room,item,math,tables}.c
 * with host generated headers, -m32, -ffunction-sections, -fdata-sections
 * and -Wl,--gc-sections. Authored side exits use the movement replay. */
#include <stdarg.h>
#include <stdio.h>
#include "game/lara.c"

static GameState state;
GameState *gs = &state;

void sys_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

static bool clearance(Lara *lara, bool near)
{
    for (int distance = 69; distance <= 71; ++distance) {
        ClimbHand hand = tr2ClimbHand(lara, ANGLE_90, 0, near ? -distance : distance, 512, false);
        int want = near ? (distance <= 70 ? HAND_WALL : HAND_BLOCKED) :
                          (distance < 70 ? HAND_EDGE : HAND_BLOCKED);
        printf("%s clearance %d: kind=%d expected=%d\n", near ? "near" : "far", distance, hand.kind, want);
        if (hand.kind != want)
            return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    RoomInfo info = {0};
    memcpy(&info.x_sectors, "\0\3", 2);
    memcpy(&info.z_sectors, "\0\3", 2);
    Sector sectors[9];
    for (int i = 0; i < 9; ++i)
        sectors[i] = (Sector){ .boxIndex = 65535, .roomBelow = 255, .roomAbove = 255, .floor = 6, .ceiling = -1 };
    sectors[4].floor = 5;
    sectors[4].ceiling = 0;
    Room room = { .info = &info, .sectors = sectors };
    ExtraInfoLara extra = {0};
    Lara lara = { .room = &room, .pos = {923, 0, 1500}, .extraL = &extra };
    GameTR2 tr2 = { .climbEnabled = true };
    gs->tr2 = &tr2;
    gs->tr2Rules = TR2_RULES_ALL | TR2_RULE_CLIMB_WALL;

    if (argc != 2 || strcmp(argv[1], "clearance") != 0)
        return 2;
    bool far = clearance(&lara, false);
    sectors[1].ceiling = 0;   /* Near floor 1536; upper intrusion is exactly distance. */
    sectors[4].ceiling = 3;   /* Keep the far vertical span open. */
    bool near = clearance(&lara, true);
    return !(far && near);
}
