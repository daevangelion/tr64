/* SPDX-License-Identifier: MIT */
#ifndef TR64_RENDER_SPRITE_H
#define TR64_RENDER_SPRITE_H
#include "tr64/render.h"
/* Returns1 submitted,0 culled,-1 explicit error. Scratch bytes are borrowed
 * only until gpu_batch returns; TRANSIENT fences the cart's DMA copy. */
int tr64_render_sprite(const tr64_render_t *render,const tr64_render_pose_t *pose,
 tr64_gpu_rect_t clip,const int64_t world[3],uint16_t descriptor,uint16_t gain,
 uint8_t water,uint32_t room,uint32_t matrix,tr64_render_counts_t *counts,
 tr64_render_error_t *error);
#endif
