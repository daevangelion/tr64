/* SPDX-License-Identifier: MIT */
#include "tr64/code.h"
#include "engine/render/load.h"
/* The caller holds Core's load scope. Dispatch resolves the unit on each
 * call; no callable pointer or unit-owned memory survives its outer lease. */
int tr64_geo_open(tr64_geo_t *out,tr64_gpu_span_t span,int meshes,tr64_render_error_t *error)
{
 tr64_render_load_request_t q={.op=TR64_RENDER_LOAD_GEO,.data.geo={out,span,meshes,error}};
 return code_call("load",(uint32_t)(uintptr_t)&q)?-1:0;
}
size_t tr64_render_workspace_bytes(const tr64_level_t *core,const tr64_geo_t *rooms,const tr64_geo_t *meshes)
{
 tr64_render_load_request_t q={.op=TR64_RENDER_LOAD_WORKSPACE,.data.workspace={core,rooms,meshes}};
 return (size_t)code_call("load",(uint32_t)(uintptr_t)&q);
}
int tr64_render_bind(tr64_render_t *out,const tr64_level_t *core,tr64_gpu_span_t tx,
 tr64_gpu_span_t rooms,tr64_gpu_span_t meshes,void *workspace,size_t bytes,tr64_render_error_t *error)
{
 tr64_render_load_request_t q={.op=TR64_RENDER_LOAD_BIND,.data.bind={out,core,tx,rooms,meshes,workspace,bytes,error}};
 uint32_t status=code_call("load",(uint32_t)(uintptr_t)&q);
 if(status)out->bound=0;
 return status?-1:0;
}
