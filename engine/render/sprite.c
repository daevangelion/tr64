/* SPDX-License-Identifier: MIT */
/* Projection facts: OpenLara2333adef src/platform/32x/render.cpp:1113-1158.
 * Native scalar lighting/fog and Z-buffer policy are the engine's model. */
#include <limits.h>
#include <string.h>
#include "engine/render/sprite.h"
extern uint8_t tr64_render_cam_water;
int tr64_render_sprite_flags(const tr64_render_t *,const tr64_render_pose_t *,
 tr64_gpu_rect_t,const int64_t [3],uint16_t,uint16_t,uint8_t,uint32_t,uint32_t,uint8_t,
 tr64_render_counts_t *,tr64_render_error_t *);
static uint16_t u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int32_t s16(const uint8_t *p) { unsigned n=u16(p);return (int32_t)(n&32767)-(int32_t)(n&32768); }
static void put16(uint8_t *p,unsigned v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static void put32(uint8_t *p,uint32_t v) { put16(p,v>>16);put16(p+2,v); }
static int64_t down(int64_t n) { return n<0?-((-n+16383)/16384):n/16384; }
static int fail(tr64_render_error_t *e,unsigned code,unsigned offset)
{ if(e)*e=(tr64_render_error_t){code,TR64_RENDER_SECTION_TEXTURES,offset};return -1; }
int tr64_render_sprite_flags(const tr64_render_t *r,const tr64_render_pose_t *pose,
 tr64_gpu_rect_t clip,const int64_t world[3],uint16_t material,uint16_t gain,
 uint8_t water,uint32_t room,uint32_t matrix,uint8_t mat_flags,
 tr64_render_counts_t *counts,tr64_render_error_t *error)
{
 const uint8_t *tx=r->textures.data;unsigned objects=u32(tx+12),sprites=u32(tx+16);
 if(material<objects || material-objects>=sprites || gain>8192 || water>1)
  return fail(error,TR64_RENDER_REFERENCE,material);
 const uint8_t *d=tx+u32(tx+52)+22*(material-objects);
 int64_t view[3]={0,0,0};
 for(unsigned a=0;a<3;++a) {
  for(unsigned k=0;k<3;++k)view[a]+=(int64_t)pose->view.basis[3*a+k]*(world[k]-pose->view.eye[k]);
  view[a]=down(view[a]);
 }
 int64_t z=view[2];if(z<pose->projection.near_z || z>=pose->projection.far_z)return 0;
 int64_t l=view[0]+s16(d+14),t=view[1]+s16(d+16),right=view[0]+s16(d+18),bottom=view[1]+s16(d+20);
 if(right<=l || bottom<=t)return 0;
 const int64_t fx=TR64_GPU_FOCAL,fy=TR64_GPU_FOCAL;
 const int cx=TR64_GPU_WIDTH/2,cy=TR64_GPU_HEIGHT/2;
 if(fx*l>=(clip.right-cx)*z || fx*right<(clip.left-cx)*z ||
    fy*t>=(clip.bottom-cy)*z || fy*bottom<(clip.top-cy)*z)return 0;
 int64_t mx=l<0?-l:l,v=right<0?-right:right;if(v>mx)mx=v;
 int64_t my=t<0?-t:t;v=bottom<0?-bottom:bottom;if(v>my)my=v;
 if(matrix>=TR64_GPU_MATRIX_LIMIT || 3*(mx+my+z)+256>24576*32)
  return fail(error,TR64_RENDER_WORKSPACE,material);
 unsigned w=d[6]+1,h=d[7]+1,xspan=w>1?2*(w-1):2,yspan=h>1?2*(h-1):2;
 water=(uint8_t)(water|tr64_render_cam_water);
 tr64_gpu_transform_t m;memset(&m,0,sizeof(m));m.instance_id=matrix;m.view_space=1;m.logical_room=(int32_t)room;
 m.water=water;
 m.translation[0]=(int32_t)l;m.translation[1]=(int32_t)t;m.translation[2]=(int32_t)z;
 m.basis[0]=(int32_t)(((right-l)*16384+xspan/2)/xspan);
 m.basis[4]=(int32_t)(((bottom-t)*16384+yspan/2)/yspan);m.basis[8]=16384;
 tr64_gpu_scissor(clip);tr64_gpu_matrix(&m);
 if(gain>6144)gain=6144;
 unsigned red=water?(gain+24)/48:(gain+12)/24,green=water?(gain+15)/30:(gain+12)/24;
 uint8_t packed[64];memset(packed,0,sizeof(packed));
 for(unsigned i=0;i<4;++i) {
  uint8_t *c=packed+(i/2)*32+16+(i%2)*4;
  c[0]=(uint8_t)(red>255?255:red);c[1]=c[2]=(uint8_t)(green>255?255:green);c[3]=255;
 }
 unsigned first=u16(d+10),splits=u16(d+12);const uint8_t *lines=tx+u32(tx+56);
 static const uint8_t face[8]={0,1,2,3,0xe4,0xe4,0,0};
 for(unsigned tile=0;tile<=splits;++tile) {
  unsigned sy=tile?u16(lines+2*(first+tile-1)):0,ey=tile<splits?u16(lines+2*(first+tile)):h;
  if(ey<=sy || ey>h)return fail(error,TR64_RENDER_LAYOUT,material);
  int top=sy?(int)(2*sy-1):0,bot=ey<h?(int)(2*ey-1):(int)yspan;
  unsigned xs[4]={0,xspan,xspan,0};int ys[4]={top,top,bot,bot};
  for(unsigned i=0;i<4;++i) {
   uint8_t *p=packed+(i/2)*32+(i%2)*8;put16(p,xs[i]);put16(p+2,(uint16_t)ys[i]);
  }
  unsigned smax=32*(w-1)+16,tt=sy?0:16,tb=ey<h?32*(ey-sy):32*(ey-sy-1)+16;
  uint8_t st[16] __attribute__((aligned(4)));put32(st,16u<<16|tt);put32(st+4,smax<<16|tt);
  put32(st+8,smax<<16|tb);put32(st+12,16u<<16|tb);
  tr64_gpu_material((tr64_gpu_material_t){material,material,(uint8_t)tile,mat_flags});
  tr64_gpu_batch_t batch={packed,face,st,4,1,TR64_GPU_BATCH_TRANSIENT|counts->sprite_tiles++};
  tr64_gpu_batch(&batch);
 }
 return 1;
}
int tr64_render_sprite(const tr64_render_t *r,const tr64_render_pose_t *pose,
 tr64_gpu_rect_t clip,const int64_t world[3],uint16_t material,uint16_t gain,
 uint8_t water,uint32_t room,uint32_t matrix,tr64_render_counts_t *counts,
 tr64_render_error_t *error)
{
 return tr64_render_sprite_flags(r,pose,clip,world,material,gain,water,room,matrix,
                                 TR64_GPU_CUTOUT,counts,error);
}
