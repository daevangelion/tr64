/* SPDX-License-Identifier: MIT */
#ifndef TR64_SND_SPATIAL_H
#define TR64_SND_SPATIAL_H
#include "tr64/game.h"
#define TR64_SND_GAIN_ONE 4096u
/* Main-game listener pan/facing; distance is optional because game events
 * already contain fixed-game camera-target attenuation. Outputs are Q12. */
void tr64_snd_spatial_gain(const tr64_game_view_t *view,const int32_t position[3],
                           uint32_t gain,int attenuate,uint16_t *left,uint16_t *right);
#endif
