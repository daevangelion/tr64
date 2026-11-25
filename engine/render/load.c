/* SPDX-License-Identifier: MIT */
#include "engine/core/sys.h"
#include "engine/render/load.h"
uint32_t tr64_render_load_dispatch(uint32_t argument)
{
 const tr64_render_load_request_t *q=(const tr64_render_load_request_t *)(uintptr_t)argument;
 if(!q)sys_error("render load request is null");
 switch(q->op) {
 case TR64_RENDER_LOAD_GEO:
  return (uint32_t)tr64_geo_open_load(q->data.geo.out,q->data.geo.span,q->data.geo.meshes,q->data.geo.error);
 case TR64_RENDER_LOAD_BIND:
  return (uint32_t)tr64_render_bind_load(q->data.bind.out,q->data.bind.core,q->data.bind.textures,
   q->data.bind.rooms,q->data.bind.meshes,q->data.bind.workspace,q->data.bind.bytes,q->data.bind.error);
 case TR64_RENDER_LOAD_WORKSPACE:
  return (uint32_t)tr64_render_workspace_bytes_load(q->data.workspace.core,q->data.workspace.rooms,q->data.workspace.meshes);
 default:sys_error("unknown renderer load operation %lu",(unsigned long)q->op);
 }
}
