/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_TR2_FLARE_H
#define TR64_GAME_TR2_FLARE_H

#include "../internal.h"

enum { FLARE_BURN_END = 1800 };
enum { PISTOL_AIM_END = 4, PISTOL_DRAW_START = 5, PISTOL_UNHOLSTER = 13,
       PISTOL_READY = 23, PISTOL_RECOIL = 24, PISTOL_RECOIL_TICKS = 9,
       FLARE_REST = 0, FLARE_RELEASE = 21, FLARE_DRAW_START = 33,
       FLARE_COST = 46, FLARE_IGNITION = 72, FLARE_DRAW_END = 94,
       FLARE_RAISE = 95, FLARE_RAISE_END = 109,
       FLARE_BODY_THROW = 189, FLARE_BODY_THROW_FINISH = 31 };

static inline bool flareLight(unsigned age)
{
    if (age >= FLARE_BURN_END)
        return false;
    uint32 draw = rand_draw(); /* Also supplies the X jitter of the visual field. */
    return age < 1740 || draw > 8192;
}

#endif
