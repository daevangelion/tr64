/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/load.h"

static int fail(tr64_render_error_t *e,int meshes,uint32_t code,uint32_t offset)
{
 if(e)*e=(tr64_render_error_t){code,meshes?TR64_RENDER_SECTION_MESHES:TR64_RENDER_SECTION_ROOMS,offset};
 return -1;
}
int tr64_geo_open_load(tr64_geo_t *g,tr64_gpu_span_t span,int meshes,tr64_render_error_t *e)
{
 const uint8_t *p=span.data;uint32_t counts[5],offsets[5],stride[5]={32,32,24,8,32};
 uint32_t cursor=64,batches=0,pairs=0,faces=0,triangles=0;
 if(!p || span.bytes<64)return fail(e,meshes,TR64_RENDER_TRUNCATED,0);
 if(memcmp(p,meshes?"MG64":"RG64",4) || u32(p+4)!=1 || u32(p+56)!=1)
  return fail(e,meshes,TR64_RENDER_VERSION,0);
 if(((uintptr_t)p&7) || u32(p+8)!=span.bytes || u32(p+60))return fail(e,meshes,TR64_RENDER_LAYOUT,8);
 for(unsigned i=0;i<5;++i) {
  counts[i]=u32(p+12+4*i);offsets[i]=u32(p+32+4*i);
  if((i<3 && counts[i]>65535) || offsets[i]!=cursor || counts[i]>(span.bytes-cursor)/stride[i])
   return fail(e,meshes,TR64_RENDER_LAYOUT,12+4*i);
  cursor+=counts[i]*stride[i];
 }
 if(cursor!=span.bytes)return fail(e,meshes,TR64_RENDER_LAYOUT,8);
 for(uint32_t i=0;i<counts[0];++i) {
  const uint8_t *o=p+offsets[0]+32*i;uint32_t first=u32(o+4),n=u16(o+8);
  if(first!=batches || n>counts[1]-batches || u16(o+10) || u32(o+24) || u32(o+28))
   return fail(e,meshes,TR64_RENDER_LAYOUT,offsets[0]+32*i);
  if(!meshes && u32(o)!=i)return fail(e,meshes,TR64_RENDER_REFERENCE,offsets[0]+32*i);
  if(meshes && i && u32(o)<=u32(o-32))return fail(e,meshes,TR64_RENDER_REFERENCE,offsets[0]+32*i);
  for(unsigned a=0;a<3;++a)if(s16(o+12+2*a)>s16(o+18+2*a))
   return fail(e,meshes,TR64_RENDER_BOUNDS,offsets[0]+32*i+12);
  for(uint32_t b=first;b<first+n;++b) {
   const uint8_t *v=p+offsets[1]+32*b;
   if(u16(v+14)!=i)return fail(e,meshes,TR64_RENDER_REFERENCE,offsets[1]+32*b+14);
   for(unsigned a=0;a<3;++a)
    if(s16(o+12+2*a)>s16(v+2*a) || s16(o+18+2*a)<s16(v+6+2*a))
     return fail(e,meshes,TR64_RENDER_BOUNDS,offsets[0]+32*i+12);
  }
  batches+=n;
 }
 if(batches!=counts[1])return fail(e,meshes,TR64_RENDER_LAYOUT,16);
 for(uint32_t b=0;b<counts[1];++b) {
  const uint8_t *v=p+offsets[1]+32*b;unsigned n=u16(v+12);
  uint32_t vo=u32(v+16);
  if(!n || n>70 || vo!=offsets[4]+pairs*32 || u16(v+14)>=counts[0] || u16(v+22) || u32(v+24) || u32(v+28))
   return fail(e,meshes,TR64_RENDER_LAYOUT,offsets[1]+32*b);
  uint32_t need=(n+1)/2;
  if(need>counts[4]-pairs)return fail(e,meshes,TR64_RENDER_TRUNCATED,vo);
  for(unsigned a=0;a<3;++a)if(s16(v+2*a)>s16(v+6+2*a))return fail(e,meshes,TR64_RENDER_BOUNDS,offsets[1]+32*b);
  for(unsigned i=0;i<n;++i) {
   const uint8_t *xyz=p+vo+(i/2)*32+(i%2)*8;
   for(unsigned a=0;a<3;++a)if(s16(xyz+2*a)<s16(v+2*a) || s16(xyz+2*a)>s16(v+6+2*a))
    return fail(e,meshes,TR64_RENDER_BOUNDS,vo+(i/2)*32+(i%2)*8);
   if(u16(xyz+6))return fail(e,meshes,TR64_RENDER_FLAGS,vo+(i/2)*32+(i%2)*8+6);
  }
  for(unsigned i=0;i<need;++i)for(unsigned z=24;z<32;++z)
   if(p[vo+32*i+z])return fail(e,meshes,TR64_RENDER_FLAGS,vo+32*i+z);
  pairs+=need;
 }
 if(pairs!=counts[4])return fail(e,meshes,TR64_RENDER_LAYOUT,28);
 for(uint32_t r=0;r<counts[2];++r) {
  const uint8_t *run=p+offsets[2]+24*r;unsigned b=u16(run+4),n=u16(run+6),flags=run[3];
  if(b>=counts[1] || !n || n>counts[3]-faces || (flags&224) || (flags&3)==3)
   return fail(e,meshes,TR64_RENDER_REFERENCE,offsets[2]+24*r);
  unsigned vertices=u16(p+offsets[1]+32*b+12);
  for(unsigned i=0;i<n;++i) {
   const uint8_t *f=p+offsets[3]+8*(faces+i);
   for(unsigned j=0;j<4;++j)if((j!=3 || f[j]!=255) && f[j]>=vertices)
    return fail(e,meshes,TR64_RENDER_REFERENCE,offsets[3]+8*(faces+i)+j);
   triangles+=f[3]==255?1:2;
  }
  faces+=n;
 }
 if(faces!=counts[3] || triangles!=u32(p+52))return fail(e,meshes,TR64_RENDER_LAYOUT,24);
 *g=(tr64_geo_t){span,counts[0],counts[1],counts[2],counts[3],counts[4],triangles,
                 offsets[0],offsets[1],offsets[2],offsets[3],offsets[4]};
 if(e)*e=(tr64_render_error_t){0,0,0};
 return 0;
}
