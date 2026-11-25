/* SPDX-License-Identifier: MIT */
/* Software consumer of the production expanded GPU commands. */
#include <stdint.h>
#include <string.h>
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "plat/host/gpu-submit.h"
#include "plat/host/gpu-platform.h"

extern int tr64_gpu_host_camera_water(void) __attribute__((weak));

enum { W=320, H=240 };
static uint16_t pixels[W*H];
/* Diagnostic host storage, not part of the emulated cart memory budget. */
static double depth[W*H];
static struct { uint64_t triangles,samples,zpass,writes; } fill;
typedef struct { double x,y,z,s,t,c[3],fog; } vertex;
static unsigned be16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static double min_d(double a,double b) { return a<b?a:b; }
static double max_d(double a,double b) { return a>b?a:b; }
static double edge(double ax,double ay,double bx,double by,double x,double y)
{ return (bx-ax)*(y-ay)-(by-ay)*(x-ax); }
static double visibility(const tr64_gpu_projection_t *p,double z)
{
 /* The cart quantizes fog after its encoded clip-Z transform. */
 if(p->far_z==TR64_GPU_FAR && p->fog_near_z==TR64_GPU_FOG_NEAR) {
  double clip=(z*2074.0-263793.0)*65535.0/65536.0-16726160.0;
  int q=(int)(clip/65536.0);
  if(clip<0 && q*65536.0!=clip) --q;
  if(q<0)return 255;
  if(q>=64)return 0;
  return ((32767-q*512)*2)>>8;
 }
 int64_t az=2074,bz=263793;
 if(p->far_z>TR64_GPU_FAR) {
  uint64_t f=p->far_z,n=TR64_GPU_NEAR,span=f-n;
  az=(int64_t)((2048u*(f+n)+span/2u)/span);
  bz=(int64_t)((2u*f*n*2048u+span/2u)/span);
 }
 int64_t c_near=az*p->fog_near_z-bz;
 int64_t c_far=az*p->far_z-bz;
 int64_t off_q16=(c_near*65535+32768)/65536;
 int64_t far_q16=(c_far*65535+32768)/65536;
 int64_t steps=(far_q16-off_q16)>>16;
 if(steps<1)steps=1;
 int64_t mult=(32767+steps-1)/steps;
 double clip=(z*(double)az-(double)bz)*65535.0/65536.0-(double)off_q16;
 int q=(int)(clip/65536.0);
 if(clip<0 && q*65536.0!=clip) --q;
 int64_t fog=(int64_t)q*mult;
 if(fog<0)return 255;
 if(fog>32767)return 0;
 return ((32767-(int)fog)*2)>>8;
}
static vertex load_vertex(const tr64_gpu_host_state_t *s,unsigned index,uint32_t st)
{
 const uint8_t *p=s->vertices+(index/2)*32+(index%2)*8;
 const uint8_t *c=s->vertices+(index/2)*32+16+(index%2)*4;
 double world[3],local[3],v[3]; vertex out;
 for(unsigned k=0;k<3;++k)local[k]=(int16_t)be16(p+2*k);
 for(unsigned k=0;k<3;++k) {
  world[k]=s->matrix.translation[k];
  for(unsigned j=0;j<3;++j)world[k]+=s->matrix.basis[k*3+j]*local[j]/16384.0;
 }
 for(unsigned k=0;k<3;++k) {
  v[k]=s->matrix.view_space?world[k]:0;
  if(!s->matrix.view_space)
   for(unsigned j=0;j<3;++j)v[k]+=s->view.basis[k*3+j]*(world[j]-s->view.eye[j])/16384.0;
 }
 out.x=v[0];out.y=v[1];out.z=v[2];
 out.s=(int16_t)(st>>16)/32.0;out.t=(int16_t)st/32.0;
 const int cam_water=tr64_gpu_host_camera_water ? tr64_gpu_host_camera_water() : 0;
 if(s->matrix.mesh) {
  const int water=s->matrix.water || cam_water;
  int64_t lit=(int64_t)c[0]*32-s->matrix.light_offset;
  int gain=lit<0?0:(lit>6144?6144:(int)lit);
  int r=water?(gain+24)/48:(gain+12)/24;
  int g=water?(gain+15)/30:(gain+12)/24;
  out.c[0]=r>255?255:r;out.c[1]=out.c[2]=g>255?255:g;
  double origin=0;
  for(unsigned j=0;j<3;++j)origin+=s->view.basis[6+j]*(s->matrix.translation[j]-s->view.eye[j])/16384.0;
  out.fog=visibility(&s->projection,origin);
 } else {
  for(unsigned k=0;k<3;++k)out.c[k]=c[k];
  if(cam_water && !s->matrix.water && !(s->batch_id&TR64_GPU_BATCH_LIT)) {
   out.c[0]=(double)((int)out.c[0]>>1);
   out.c[1]=(double)(((int)out.c[1]*205)>>8);
   out.c[2]=(double)(((int)out.c[2]*205)>>8);
  }
  out.fog=visibility(&s->projection,out.z);
 }
 return out;
}
static vertex mix(vertex a,vertex b,double t)
{
 vertex v;
 v.x=a.x+(b.x-a.x)*t;v.y=a.y+(b.y-a.y)*t;v.z=a.z+(b.z-a.z)*t;
 v.s=a.s+(b.s-a.s)*t;v.t=a.t+(b.t-a.t)*t;
 for(unsigned i=0;i<3;++i)v.c[i]=a.c[i]+(b.c[i]-a.c[i])*t;
 v.fog=a.fog+(b.fog-a.fog)*t;return v;
}
static unsigned sample(const tr64_gpu_host_state_t *state,double ss,double tt)
{
 const uint8_t *tx=state->textures.data;
 if(state->material.flags&TR64_GPU_FLAT) {
  if(state->material.material==UINT16_MAX)return 1u;
  return be16(tx+be32(tx+60)+8*state->material.material+2);
 }
 unsigned objects=be32(tx+12),sprite=state->material.content>=objects;
 const uint8_t *d=tx+be32(tx+(sprite?52:48))+(state->material.content-(sprite?objects:0))*(sprite?22:24);
 const uint8_t *cl=tx+be32(tx+40)+16*be16(d);
 const uint8_t *pal=tx+be32(tx+44)+8*be16(d+2);
 unsigned first=be16(d+(sprite?10:20)),splits=be16(d+(sprite?12:22)),tile=state->material.tile;
 const uint8_t *lines=tx+be32(tx+56);
 unsigned sy=tile?be16(lines+2*(first+tile-1)):0;
 unsigned ey=tile<splits?be16(lines+2*(first+tile)):(unsigned)d[7]+1;
 int u=(int)max_d(0,min_d(ss,d[6]));
 int v=(int)max_d(0,min_d(tt,(int)(ey-sy-1)));
 unsigned x=d[4]+u,y=d[5]+sy+v;
 unsigned b=tx[be32(cl)+y*be16(cl+8)+x/2];
 unsigned index=(x&1)?b&15:b>>4;
 return be16(tx+be32(pal)+2*index);
}
static void triangle(const tr64_gpu_host_state_t *s,vertex a,vertex b,vertex c)
{
 vertex v[3]={a,b,c};double x[3],y[3],iz[3];
 /* Evaluate edge/interpolant arithmetic in the viewport's own coordinates.
  * A different framebuffer offset must not change floating-point rounding. */
 const tr64_gpu_rect_t rect=tr64_gpu_projection_rect(&s->projection);
 for(unsigned i=0;i<3;++i) {
  iz[i]=1.0/v[i].z;
  x[i]=(s->projection.centre_x-rect.left)+s->projection.focal_x*v[i].x*iz[i];
  y[i]=(s->projection.centre_y-rect.top)+s->projection.focal_y*v[i].y*iz[i];
 }
 double area=edge(x[0],y[0],x[1],y[1],x[2],y[2]);
 if(area==0 || (area<0 && !(s->material.flags&TR64_GPU_DOUBLE_SIDED)))return;
 double l=max_d(s->scissor.left-rect.left,min_d(x[0],min_d(x[1],x[2])));
 double r=min_d(s->scissor.right-rect.left,max_d(x[0],max_d(x[1],x[2]))+1);
 double t=max_d(s->scissor.top-rect.top,min_d(y[0],min_d(y[1],y[2])));
 double btm=min_d(s->scissor.bottom-rect.top,max_d(y[0],max_d(y[1],y[2]))+1);
 if(l>=r || t>=btm)return;
 ++fill.triangles;
 const int no_depth=!!(s->material.flags&TR64_GPU_NO_DEPTH);
 const int additive=!!(s->material.flags&TR64_GPU_ADDITIVE);
 const int translucent=!additive && ((s->material.flags&TR64_GPU_CLASS_MASK)==TR64_GPU_BLENDED_M1);
 const int z_write=!no_depth && !additive && !translucent;
 unsigned base=rect.top*W+rect.left;
 for(int py=(int)t;py<(int)btm;++py)for(int px=(int)l;px<(int)r;++px) {
  double w0=edge(x[1],y[1],x[2],y[2],px+.5,py+.5)/area;
  double w1=edge(x[2],y[2],x[0],y[0],px+.5,py+.5)/area;
  double w2=1-w0-w1;
  if(w0<0 || w1<0 || w2<0)continue;
  double inv=w0*iz[0]+w1*iz[1]+w2*iz[2];
  if(inv<=0)continue;
  ++fill.samples;
  double z=1/inv;unsigned pos=base+py*W+px;
  if(!no_depth && z>=depth[pos])continue;
  ++fill.zpass;
  double ss=(w0*v[0].s*iz[0]+w1*v[1].s*iz[1]+w2*v[2].s*iz[2])*z;
  double tt=(w0*v[0].t*iz[0]+w1*v[1].t*iz[1]+w2*v[2].t*iz[2])*z;
  unsigned tex=sample(s,ss,tt);if(!(tex&1))continue;
  int fog=(int)(v[0].fog+w1*(v[1].fog-v[0].fog)+w2*(v[2].fog-v[0].fog));
  unsigned dst=pixels[pos],rgb=1;
  for(unsigned k=0;k<3;++k) {
   unsigned shift=11-5*k;
   unsigned tex5=(tex>>shift)&31,tex8=(tex5<<3)|(tex5>>2);
   int shade=(int)(v[0].c[k]+w1*(v[1].c[k]-v[0].c[k])+w2*(v[2].c[k]-v[0].c[k]));
   int lit=(tex8*shade+128)>>8;lit+=((lit*128+128)>>8);
   if(lit>255)lit=255;
   if(fog<255)lit=(lit*(fog>>3))>>5;
   unsigned out5=(unsigned)(lit>>3);
   if(additive) { out5=(out5*176u+((dst>>shift)&31u)*80u)>>8; }
   else if(translucent) { out5=(out5+((dst>>shift)&31u))>>1; }
   rgb|=out5<<shift;
  }
  ++fill.writes;
  pixels[pos]=(uint16_t)rgb;
  if(z_write)depth[pos]=z;
 }
}
void tr64_gpu_host_clear_depth(void)
{
 for(unsigned i=0;i<W*H;++i)depth[i]=1.0e30;
}
void tr64_gpu_host_begin_frame(const tr64_gpu_host_state_t *state)
{
 (void)state;memset(pixels,0,sizeof(pixels));
 memset(&fill,0,sizeof(fill));
 tr64_gpu_host_clear_depth();
}
void tr64_gpu_host_emit_triangle(const tr64_gpu_host_state_t *state,const tr64_gpu_host_triangle_t *command)
{
 vertex input[3],out[4];unsigned n=0;
 for(unsigned i=0;i<3;++i)input[i]=load_vertex(state,command->indices[i],command->words[i+2]);
 if(input[0].z>state->projection.far_z && input[1].z>state->projection.far_z && input[2].z>state->projection.far_z)return;
 for(unsigned i=0;i<3;++i) {
  vertex a=input[i],b=input[(i+1)%3];
  int ia=a.z>=state->projection.near_z,ib=b.z>=state->projection.near_z;
  if(ia)out[n++]=a;
  if(ia!=ib)out[n++]=mix(a,b,(state->projection.near_z-a.z)/(b.z-a.z));
 }
 for(unsigned i=1;i+1<n;++i)triangle(state,out[0],out[i],out[i+1]);
}
void tr64_gpu_host_end_frame(const tr64_gpu_host_state_t *state)
{ (void)state;sys_present(pixels); }
void tr64_gpu_host_log_fill(void)
{
 unsigned visible=0;for(unsigned i=0;i<W*H;++i)visible+=depth[i]<1.0e30;
 log_printf("raster-fill: triangles=%llu samples=%llu zpass=%llu writes=%llu visible_pixels=%u\n",
  (unsigned long long)fill.triangles,(unsigned long long)fill.samples,
  (unsigned long long)fill.zpass,(unsigned long long)fill.writes,visible);log_flush();
}
uint16_t *tr64_gpu_host_pixels(void) { return pixels; }
