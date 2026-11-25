/* SPDX-License-Identifier: MIT */
#include "engine/render/texture.h"
void tr64_render_texture_apply(tr64_render_t *r,tr64_render_counts_t *counts)
{
 uint32_t delta=r->texture_requested_step-r->texture_step;
 if(!delta)return;
 for(uint32_t at=0;at<r->texture_cycle_words;) {
  uint16_t *cycle=r->texture_cycles+at;unsigned n=cycle[0];
  unsigned phase=(cycle[1]+delta%n)%n;cycle[1]=(uint16_t)phase;
  for(unsigned i=0,j=phase;i<n;++i) {
   r->texture_map[cycle[2+i]]=cycle[2+j];if(++j==n)j=0;
  }
  at+=n+2;++counts->anim_ranges;
 }
 counts->anim_steps=delta;
 r->texture_step=r->texture_requested_step;
}
