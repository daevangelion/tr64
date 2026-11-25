/* SPDX-License-Identifier: MIT */
#ifndef TR64_RENDER_TEXTURE_H
#define TR64_RENDER_TEXTURE_H
#include "tr64/render.h"
/* Cold, called only within the shared load unit. Storage is caller state. */
size_t tr64_render_texture_bytes_load(const tr64_level_t *core);
int tr64_render_texture_bind_load(tr64_render_t *render,uint16_t *storage,size_t bytes,tr64_render_error_t *error);
/* Hot: apply the requested cumulative simulation rotation count. */
void tr64_render_texture_apply(tr64_render_t *render,tr64_render_counts_t *counts);
#endif
