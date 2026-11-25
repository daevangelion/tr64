/* SPDX-License-Identifier: MIT */
#ifndef TR64_RENDER_LOAD_H
#define TR64_RENDER_LOAD_H
#include "tr64/render.h"
enum { TR64_RENDER_LOAD_GEO=0x100,TR64_RENDER_LOAD_BIND,TR64_RENDER_LOAD_WORKSPACE };
typedef struct {
 uint32_t op;
 union {
  struct { tr64_geo_t *out;tr64_gpu_span_t span;int meshes;tr64_render_error_t *error; } geo;
  struct {
   tr64_render_t *out;const tr64_level_t *core;tr64_gpu_span_t textures,rooms,meshes;
   void *workspace;size_t bytes;tr64_render_error_t *error;
  } bind;
  struct { const tr64_level_t *core;const tr64_geo_t *rooms,*meshes; } workspace;
 } data;
} tr64_render_load_request_t;
/* Only the load unit calls these. Never call a public dispatch facade from
 * inside the unit, and never retain a request/stack address after returning. */
int tr64_geo_open_load(tr64_geo_t *,tr64_gpu_span_t,int,tr64_render_error_t *);
size_t tr64_render_workspace_bytes_load(const tr64_level_t *,const tr64_geo_t *,const tr64_geo_t *);
int tr64_render_bind_load(tr64_render_t *,const tr64_level_t *,tr64_gpu_span_t,
 tr64_gpu_span_t,tr64_gpu_span_t,void *,size_t,tr64_render_error_t *);
uint32_t tr64_render_load_dispatch(uint32_t request);
#endif
