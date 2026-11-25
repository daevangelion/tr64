/* SPDX-License-Identifier: MIT */
#include <limits.h>
#include <string.h>
#include "engine/core/sys.h"
#include "tr64/render.h"
#include "engine/render/sprite.h"
#include "engine/render/internal.h"
#include "engine/render/texture.h"

static uint64_t isqrt(uint64_t n)
{
 uint64_t bit=UINT64_C(1)<<62,r=0;
 while(bit>n)bit>>=2;
 while(bit) { if(n>=r+bit) { n-=r+bit;r=(r>>1)+bit; } else r>>=1;bit>>=2; }
 return r;
}
static int normalize(int32_t *out,const int32_t *v)
{
 uint64_t sum=0;
 for(unsigned k=0;k<3;++k)sum+=(uint64_t)((int64_t)v[k]*v[k]);
 uint64_t length=isqrt(sum);if(!length)return -1;
 for(unsigned k=0;k<3;++k)out[k]=(int32_t)((int64_t)v[k]*16384/(int64_t)length);
 return 0;
}
int tr64_render_camera(tr64_render_pose_t *out,const int32_t pos[3],const int32_t look[3],
                       uint32_t room,uint8_t flipped,tr64_render_error_t *e)
{
 int32_t f[3],r[3],d[3],v[3];
 if(flipped>1 || normalize(f,look))return error(e,TR64_RENDER_CAMERA,0,0);
 v[0]=f[2];v[1]=0;v[2]=-f[0];
 if(normalize(r,v))return error(e,TR64_RENDER_CAMERA,0,0);
 v[0]=(int32_t)(((int64_t)f[1]*r[2]-(int64_t)f[2]*r[1])/16384);
 v[1]=(int32_t)(((int64_t)f[2]*r[0]-(int64_t)f[0]*r[2])/16384);
 v[2]=(int32_t)(((int64_t)f[0]*r[1]-(int64_t)f[1]*r[0])/16384);
 if(normalize(d,v))return error(e,TR64_RENDER_CAMERA,0,0);
 memset(out,0,sizeof(*out));memcpy(out->view.eye,pos,12);
 memcpy(out->view.basis,r,12);memcpy(out->view.basis+3,d,12);memcpy(out->view.basis+6,f,12);
 out->projection=(tr64_gpu_projection_t)TR64_GPU_WORLD_PROJECTION;
 out->camera_room=room;out->flipped=flipped;return 0;
}
void tr64_render_half(tr64_render_pose_t *pose,unsigned lower)
{
 pose->projection.height=TR64_GPU_HEIGHT/2;
 pose->projection.centre_y=TR64_GPU_HEIGHT/4+(lower ? TR64_GPU_HEIGHT/2 : 0);
}
static int empty(tr64_gpu_rect_t a) { return a.left>=a.right || a.top>=a.bottom; }
static inline void identity(tr64_gpu_transform_t *t)
{
 *t=(tr64_gpu_transform_t){.basis={16384,0,0,0,16384,0,0,0,16384}};
}
static inline void view_point(const tr64_gpu_view_t *v,int64_t dx,int64_t dy,int64_t dz,int64_t out[3])
{
 const int32_t *vb=v->basis;
 out[0]=(int64_t)vb[0]*dx+(int64_t)vb[1]*dy+(int64_t)vb[2]*dz;
 out[1]=(int64_t)vb[3]*dx+(int64_t)vb[4]*dy+(int64_t)vb[5]*dz;
 out[2]=(int64_t)vb[6]*dx+(int64_t)vb[7]*dy+(int64_t)vb[8]*dz;
}
static inline void floor_ceil_div(int64_t n,int64_t d,int64_t *fl,int64_t *ce)
{
 if(n>=-INT32_MAX && n<=INT32_MAX) {
  int32_t q=(int32_t)n/(int32_t)d,rem=(int32_t)n%(int32_t)d;
  *fl=q-(rem<0);
  *ce=q+(rem>0);
  return;
 }
 int64_t q=n/d,rem=n%d;
 *fl=q-(rem<0);
 *ce=q+(rem>0);
}
static int32_t active_far_q14=TR64_GPU_FAR*16384;
extern void tr64_render_apply_scene_options(tr64_gpu_projection_t *projection) __attribute__((weak));
__attribute__((noinline,cold))
void tr64_render_fog_range(int32_t video_fog,uint16_t *far_out,uint16_t *fog_near_out)
{
 if(video_fog<0)video_fog=0;
 if(video_fog>16)video_fog=16;
 uint16_t far_z=video_fog<=8 ? (uint16_t)(4096+video_fog*768)
                             : (uint16_t)(10240+(video_fog-8)*512);
 uint16_t fog_near=(uint16_t)(((uint32_t)far_z*4u)/5u);
 if(far_out)*far_out=far_z;
 if(fog_near_out)*fog_near_out=fog_near;
}
static __attribute__((noinline,cold))
void apply_game_projection(tr64_gpu_projection_t *projection)
{
 if(tr64_render_apply_scene_options)tr64_render_apply_scene_options(projection);
}
static int portal_rect(const tr64_core_rooms_t *room,const tr64_core_portals_t *p,
                       const tr64_gpu_view_t *v,int64_t far_limit,
                       tr64_gpu_rect_t parent,tr64_gpu_rect_t *result)
{
 int64_t rx=(int64_t)tr64_i32(room->x)-v->eye[0],ry=-(int64_t)v->eye[1],rz=(int64_t)tr64_i32(room->z)-v->eye[2];
 int64_t dx0=rx+tr64_i16(p->x0),dy0=ry+tr64_i16(p->y0),dz0=rz+tr64_i16(p->z0);
 int64_t facing=(int64_t)tr64_i16(p->normal_x)*dx0+
                (int64_t)tr64_i16(p->normal_y)*dy0+
                (int64_t)tr64_i16(p->normal_z)*dz0;
 if(facing>=0)return 0;
 int64_t dxyz[4][3]={
  {dx0,dy0,dz0},
  {rx+tr64_i16(p->x1),ry+tr64_i16(p->y1),rz+tr64_i16(p->z1)},
  {rx+tr64_i16(p->x2),ry+tr64_i16(p->y2),rz+tr64_i16(p->z2)},
  {rx+tr64_i16(p->x3),ry+tr64_i16(p->y3),rz+tr64_i16(p->z3)}};
 int64_t xyz[4][3];
 int behind=0,far=0;
 for(unsigned i=0;i<4;++i) {
  view_point(v,dxyz[i][0],dxyz[i][1],dxyz[i][2],xyz[i]);
  int64_t z=xyz[i][2];
  if(z<=0)++behind;
  if(z>far_limit)++far;
 }
 if(behind==4 || far==4)return 0;
 int64_t l=parent.right,t=parent.bottom,r=parent.left,b=parent.top;
 const int64_t pl=parent.left-TR64_GPU_WIDTH/2,pr=parent.right-TR64_GPU_WIDTH/2;
 const int64_t pt=parent.top-TR64_GPU_HEIGHT/2,pb=parent.bottom-TR64_GPU_HEIGHT/2;
 int ge_clip=0;
 if(behind) {
  for(unsigned i=0;i<4;++i)if(xyz[i][2]>=16384) { ge_clip=1;break; }
 }
 for(unsigned i=0;i<4;++i) {
  int64_t z=xyz[i][2];
  if(ge_clip ? (z>=16384) : (z>0)) {
   int64_t depth=z>far_limit?far_limit:z;
   int64_t nx=(int64_t)((uint64_t)xyz[i][0]<<8);
   if(nx<=pl*depth) { l=parent.left; }
   else if(nx>=pr*depth) { r=parent.right; }
   else {
    int64_t lx,rx2;
    floor_ceil_div(nx,depth,&lx,&rx2);
    lx+=TR64_GPU_WIDTH/2;rx2+=TR64_GPU_WIDTH/2;
    if(lx<l)l=lx;
    if(rx2>r)r=rx2;
   }
   int64_t ny=(int64_t)((uint64_t)xyz[i][1]<<8);
   if(ny<=pt*depth) { t=parent.top; }
   else if(ny>=pb*depth) { b=parent.bottom; }
   else {
    int64_t ty,by;
    floor_ceil_div(ny,depth,&ty,&by);
    ty+=TR64_GPU_HEIGHT/2;by+=TR64_GPU_HEIGHT/2;
    if(ty<t)t=ty;
    if(by>b)b=by;
   }
  }
 }
 if(ge_clip) {
  /* Clip straddling edges against z = 1 world unit (64 in >>8 space) so large
   * outdoor portals passing behind the camera stay tightly bounded. */
  for(unsigned i=0;i<4;++i) {
   unsigned j=(i+1)&3u;
   if((xyz[i][2]>=16384)!=(xyz[j][2]>=16384)) {
    int64_t ax=xyz[i][0]>>8,ay=xyz[i][1]>>8,az=xyz[i][2]>>8;
    int64_t bx=xyz[j][0]>>8,by=xyz[j][1]>>8,bz=xyz[j][2]>>8;
    int64_t dz=bz-az;
    int64_t cx=ax+((bx-ax)*(64-az))/dz;
    int64_t cy=ay+((by-ay)*(64-az))/dz;
    int64_t sx=(cx<<2)+TR64_GPU_WIDTH/2;
    int64_t sy=(cy<<2)+TR64_GPU_HEIGHT/2;
    if(sx-1<l)l=sx-1;
    if(sx+1>r)r=sx+1;
    if(sy-1<t)t=sy-1;
    if(sy+1>b)b=sy+1;
   }
  }
 } else if(behind)for(unsigned i=0;i<4;++i) {
  unsigned j=(i+1)&3u;
  if((xyz[i][2]<=0)!=(xyz[j][2]<=0)) {
   if(!(xyz[i][0]>0 && xyz[j][0]>0))l=parent.left;
   if(!(xyz[i][0]<0 && xyz[j][0]<0))r=parent.right;
   if(!(xyz[i][1]>0 && xyz[j][1]>0))t=parent.top;
   if(!(xyz[i][1]<0 && xyz[j][1]<0))b=parent.bottom;
  }
 }
 if(l<parent.left)l=parent.left;
 if(r>parent.right)r=parent.right;
 if(t<parent.top)t=parent.top;
 if(b>parent.bottom)b=parent.bottom;
 if(l>=r || t>=b)return 0;
 *result=(tr64_gpu_rect_t){(int16_t)l,(int16_t)t,(int16_t)r,(int16_t)b};return 1;
}
static void visibility(tr64_render_t *r,const tr64_render_pose_t *pose,tr64_render_counts_t *counts)
{
 unsigned n=r->rooms.objects,head=0,tail=0,pending=1;uint16_t *q=queue(r),*mate=mates(r);uint8_t *in=queued(r);
 tr64_gpu_rect_t *clip=clips(r);memset(clip,0,n*sizeof(*clip));memset(in,0,n);
 const tr64_level_table_t *rt=tr64_level_table(r->core,TR64_CORE_rooms);
 const tr64_level_table_t *pt=tr64_level_table(r->core,TR64_CORE_portals);
 const tr64_core_rooms_t *rooms=(const tr64_core_rooms_t *)rt->data;
 const tr64_core_portals_t *portals=(const tr64_core_portals_t *)pt->data;
 clip[pose->camera_room]=tr64_gpu_projection_clip(&pose->projection);
 q[tail++]=(uint16_t)pose->camera_room;
 if(tail==n)tail=0;
 in[pose->camera_room]=1;
 while(pending) {
  unsigned id=q[head++];
  if(head==n)head=0;
  --pending;in[id]=0;
  unsigned physical=pose->flipped?mate[id]:id;
  const tr64_core_rooms_t *room=physical<rt->count?rooms+physical:tr64_level_rooms(r->core,physical);
  unsigned start=tr64_u32(room->portal_start),pc=tr64_u32(room->portal_count);
  for(unsigned i=0;i<pc;++i) {
   unsigned pi=start+i;
   const tr64_core_portals_t *p=pi<pt->count?portals+pi:tr64_level_portals(r->core,pi);
   tr64_gpu_rect_t rect;
   ++counts->portal_tests;if(!portal_rect(room,p,&pose->view,(int64_t)pose->projection.far_z*16384,clip[id],&rect))continue;
   unsigned dest=tr64_u16(p->room);tr64_gpu_rect_t old=clip[dest],merged=rect;
   if(!empty(old)) {
    if(old.left<merged.left)merged.left=old.left;
    if(old.top<merged.top)merged.top=old.top;
    if(old.right>merged.right)merged.right=old.right;
    if(old.bottom>merged.bottom)merged.bottom=old.bottom;
   }
   if(old.left!=merged.left || old.top!=merged.top || old.right!=merged.right || old.bottom!=merged.bottom) {
    clip[dest]=merged;++counts->clip_growths;
    if(!in[dest]) {
     q[tail++]=(uint16_t)dest;
     if(tail==n)tail=0;
     ++pending;in[dest]=1;
    }
   }
  }
 }
 for(unsigned i=0;i<n;++i)counts->visible_rooms+=!empty(clip[i]);
}
static inline uint32_t render_ticks(void)
{
#if defined(N64) && !defined(TR64_HOST)
 uint32_t t;
 __asm__ volatile("mfc0 %0, $9" : "=r"(t));
 return t;
#else
 return sys_profile_ticks();
#endif
}
static inline int64_t mul160(int64_t x) { return (int64_t)(((uint64_t)x<<7)+((uint64_t)x<<5)); }
static inline int64_t mul120(int64_t x) { return (int64_t)(((uint64_t)x<<7)-((uint64_t)x<<3)); }
static inline int plane_pair_ok(int64_t o,int64_t oz,const int64_t d[3],const int64_t dz[3],
                                int32_t c_lo,int32_t c_hi)
{
 int64_t o8=(int64_t)((uint64_t)o<<8),m_lo=o8+c_lo*oz,m_hi=c_hi*oz-o8;
 for(unsigned k=0;k<3;++k) {
  int64_t dk=(int64_t)((uint64_t)d[k]<<8);
  int64_t t0=dk+c_lo*dz[k];if(t0>0)m_lo+=t0;
  int64_t t1=c_hi*dz[k]-dk;if(t1>0)m_hi+=t1;
 }
 return m_lo>=0 && m_hi>=0;
}
/* Room and mesh AABB culling split into dedicated hot functions so each fits in
 * registers without cross-path spills inside tr64_render_draw_object. */
static __attribute__((noinline)) int batch_visible_room(const uint8_t *b,const int32_t *vb,
                                                        const int64_t base_v[3],tr64_gpu_rect_t clip)
{
 int32_t lo0=s16_a(b),lo1=s16_a(b+2),lo2=s16_a(b+4);
 int32_t e0=s16_a(b+6)-lo0,e1=s16_a(b+8)-lo1,e2=s16_a(b+10)-lo2;
 int64_t oz=base_v[2]+(int64_t)(vb[6]*lo0)+(int64_t)(vb[7]*lo1)+(int64_t)(vb[8]*lo2);
 int64_t dz[3]={(int32_t)((uint32_t)vb[6]*(uint32_t)e0),
                (int32_t)((uint32_t)vb[7]*(uint32_t)e1),
                (int32_t)((uint32_t)vb[8]*(uint32_t)e2)};
 if(oz+(dz[0]>0?dz[0]:0)+(dz[1]>0?dz[1]:0)+(dz[2]>0?dz[2]:0)<TR64_GPU_NEAR*16384)return 0;
 if(oz+(dz[0]<0?dz[0]:0)+(dz[1]<0?dz[1]:0)+(dz[2]<0?dz[2]:0)>(int64_t)active_far_q14)return 0;
 for(unsigned axis=0;axis<2;++axis) {
  const int32_t *vr=vb+3*axis;
  int64_t o=base_v[axis]+(int64_t)(vr[0]*lo0)+(int64_t)(vr[1]*lo1)+(int64_t)(vr[2]*lo2);
  int64_t d[3]={(int32_t)((uint32_t)vr[0]*(uint32_t)e0),
                (int32_t)((uint32_t)vr[1]*(uint32_t)e1),
                (int32_t)((uint32_t)vr[2]*(uint32_t)e2)};
  int32_t c_lo=axis?(TR64_GPU_HEIGHT/2-clip.top):(TR64_GPU_WIDTH/2-clip.left);
  int32_t c_hi=axis?(clip.bottom-TR64_GPU_HEIGHT/2):(clip.right-TR64_GPU_WIDTH/2);
  if(!plane_pair_ok(o,oz,d,dz,c_lo,c_hi))return 0;
 }
 return 1;
}
static __attribute__((noinline,cold)) int plane_pair_clipped(int64_t o,int64_t oz,const int64_t d[3],
                                                            const int64_t dz[3],tr64_gpu_rect_t clip,unsigned axis)
{
 int32_t c_lo=axis?(TR64_GPU_HEIGHT/2-clip.top):(TR64_GPU_WIDTH/2-clip.left);
 int32_t c_hi=axis?(clip.bottom-TR64_GPU_HEIGHT/2):(clip.right-TR64_GPU_WIDTH/2);
 return plane_pair_ok(o,oz,d,dz,c_lo,c_hi);
}
static __attribute__((noinline)) int batch_visible_mesh(const uint8_t *b,const tr64_gpu_transform_t *m,
                                                        const tr64_gpu_view_t *v,tr64_gpu_rect_t clip)
{
 /* Preserve the original per-term model truncation exactly, short-circuiting
  * scalar planes in Z -> X -> Y order before evaluating later view axes. */
 const int32_t *vb=v->basis;
 int32_t lo0=s16_a(b),lo1=s16_a(b+2),lo2=s16_a(b+4);
 int32_t hi0=s16_a(b+6),hi1=s16_a(b+8),hi2=s16_a(b+10);
 int32_t e[3][3];
 int64_t w[3];
 for(unsigned a=0;a<3;++a) {
  const int32_t *mb=m->basis+3*a;
  int32_t l0=(mb[0]*lo0)/16384,h0=(mb[0]*hi0)/16384;
  int32_t l1=(mb[1]*lo1)/16384,h1=(mb[1]*hi1)/16384;
  int32_t l2=(mb[2]*lo2)/16384,h2=(mb[2]*hi2)/16384;
  w[a]=(int64_t)m->translation[a]-v->eye[a]+l0+l1+l2;
  e[0][a]=h0-l0;e[1][a]=h1-l1;e[2][a]=h2-l2;
 }
 int64_t oz=(int64_t)vb[6]*w[0]+(int64_t)vb[7]*w[1]+(int64_t)vb[8]*w[2];
 int64_t dz[3];
 for(unsigned k=0;k<3;++k)
  dz[k]=(int64_t)vb[6]*e[k][0]+(int64_t)vb[7]*e[k][1]+(int64_t)vb[8]*e[k][2];
 if(oz+(dz[0]>0?dz[0]:0)+(dz[1]>0?dz[1]:0)+(dz[2]>0?dz[2]:0)<TR64_GPU_NEAR*16384)return 0;
 if(oz+(dz[0]<0?dz[0]:0)+(dz[1]<0?dz[1]:0)+(dz[2]<0?dz[2]:0)>(int64_t)active_far_q14)return 0;
 int full=(clip.left==0 && clip.top==0 && clip.right==TR64_GPU_WIDTH && clip.bottom==TR64_GPU_HEIGHT);
 for(unsigned axis=0;axis<2;++axis) {
  const int32_t *vr=vb+3*axis;
  int64_t o=(int64_t)vr[0]*w[0]+(int64_t)vr[1]*w[1]+(int64_t)vr[2]*w[2],d[3];
  for(unsigned k=0;k<3;++k)
   d[k]=(int64_t)vr[0]*e[k][0]+(int64_t)vr[1]*e[k][1]+(int64_t)vr[2]*e[k][2];
  if(__builtin_expect(full,1)) {
   int64_t o8=(int64_t)((uint64_t)o<<8),cz=axis?mul120(oz):mul160(oz);
   int64_t m_lo=cz+o8,m_hi=cz-o8;
   for(unsigned k=0;k<3;++k) {
    int64_t dk=(int64_t)((uint64_t)d[k]<<8),czk=axis?mul120(dz[k]):mul160(dz[k]);
    int64_t t0=czk+dk;if(t0>0)m_lo+=t0;
    int64_t t1=czk-dk;if(t1>0)m_hi+=t1;
   }
   if(m_lo<0 || m_hi<0)return 0;
  } else if(!plane_pair_clipped(o,oz,d,dz,clip,axis))return 0;
 }
 return 1;
}
/* Cart placement of this and the gpu.c submit path: plat/n64/hot.ld. */
int tr64_render_draw_object(const tr64_geo_t *g,const uint32_t *span,tr64_gpu_transform_t *m,
                        const tr64_render_pose_t *pose,tr64_gpu_rect_t clip,tr64_render_counts_t *counts,int mesh,
                        tr64_render_error_t *e,const uint16_t *texture_map)
{
 uint32_t run_count=span[2];
 if(!run_count)return 0;
 unsigned previous=UINT_MAX;
 int visible=0,bound=0;
 uint32_t start=render_ticks(),cull_ticks=0,submit_ticks=0,tested=0,rejected=0;
 const uint8_t *const base=g->bytes.data,*const batches=base+g->batch_offset;
 const uint8_t *face_ptr=base+g->face_offset+8u*span[1];
 const uint8_t *run=base+g->run_offset+24u*span[0];
 const uint8_t *batch_verts=NULL;
 uint16_t batch_vcount=0;
 const uint32_t mesh_bit=(uint32_t)mesh<<31;
 int64_t base_v[3];
 if(!mesh) {
  const int32_t *vb=pose->view.basis;
  int64_t tx=(int64_t)m->translation[0]-pose->view.eye[0];
  int64_t ty=(int64_t)m->translation[1]-pose->view.eye[1];
  int64_t tz=(int64_t)m->translation[2]-pose->view.eye[2];
  #pragma GCC unroll 1
  for(unsigned a=0;a<3;++a)
   base_v[a]=(int64_t)vb[3*a]*tx+(int64_t)vb[3*a+1]*ty+(int64_t)vb[3*a+2]*tz;
 }
 for(uint32_t i=0;i<run_count;++i,run+=24) {
  unsigned b=u16_a(run+4),n=u16_a(run+6);
  if(b!=previous) {
   uint32_t phase=render_ticks();
   const uint8_t *batch=batches+32u*b;
   ++tested;
   visible=mesh ? batch_visible_mesh(batch,m,&pose->view,clip)
                : batch_visible_room(batch,pose->view.basis,base_v,clip);
   rejected+=!visible;previous=b;
   if(visible) { batch_verts=base+u32_a(batch+16);batch_vcount=u16_a(batch+12); }
   cull_ticks+=render_ticks()-phase;
  }
  if(visible) {
   uint32_t phase=render_ticks();
   if(!bound) {
    if(counts->matrix_slots>=TR64_GPU_MATRIX_LIMIT) {
     counts->stream_runs+=i+1u;counts->batches_tested+=tested;counts->batches_rejected+=rejected;
     return error(e,TR64_RENDER_WORKSPACE,0,counts->matrix_slots);
    }
    m->instance_id=counts->matrix_slots++;tr64_gpu_matrix(m);bound=1;
   }
   unsigned logical=u16_a(run),content=logical;
   if(!(run[3]&TR64_GPU_FLAT) && texture_map)content=texture_map[logical];
   tr64_gpu_material((tr64_gpu_material_t){(uint16_t)logical,(uint16_t)content,run[2],run[3]});
   tr64_gpu_batch_t packet={batch_verts,face_ptr,run+8,batch_vcount,(uint16_t)n,b|mesh_bit};
   tr64_gpu_batch(&packet);
   submit_ticks+=render_ticks()-phase;
  }
  face_ptr+=8u*n;
 }
 counts->stream_runs+=run_count;counts->batches_tested+=tested;counts->batches_rejected+=rejected;
 counts->culling_us+=cull_ticks;counts->gpu_submit_us+=submit_ticks;
 counts->stream_scan_us+=render_ticks()-start-cull_ticks-submit_ticks;
 return bound;
}
static unsigned validate_eye(const tr64_render_t *r,const tr64_render_pose_t *pose)
{
 if(pose->camera_room>=r->rooms.objects || pose->flipped>1)return 0;
 int64_t distance=0;
 for(unsigned a=0;a<3;++a) {
  int64_t lo=(int64_t)pose->view.eye[a]-r->world_min[a],hi=(int64_t)pose->view.eye[a]-r->world_max[a];
  if(lo<0)lo=-lo;
  if(hi<0)hi=-hi;
  distance+=lo>hi?lo:hi;
 }
 unsigned view_scale=basis_scale(pose->view.basis);
 if(!view_scale || view_scale==UINT_MAX || 3*((distance*view_scale+16383)/16384)+256>24576*32)return 0;
 return view_scale;
}
#if !defined(N64) || TR64_GAME >= 2
static __attribute__((noinline,cold))
int submit_skybox(const tr64_render_t *r,const tr64_render_pose_t *pose,
                  tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 const tr64_level_table_t *rt=r->core->tables+(TR64_CORE_rooms-1);
 const tr64_core_rooms_t *rooms=(const tr64_core_rooms_t *)rt->data;
 const tr64_gpu_rect_t *room_clips=clips(r);
 const uint16_t *mate=mates(r);
 int outdoor=0;
 uint32_t room_id=0;
 for(unsigned id=0;id<r->rooms.objects;++id) {
  if(empty(room_clips[id]))continue;
  unsigned physical=pose->flipped?mate[id]:id;
  const tr64_core_rooms_t *room=physical<rt->count?rooms+physical:tr64_level_rooms(r->core,physical);
  if(!(tr64_u16(room->flags)&TR64_ROOM_FLAG_OUTSIDE))continue;
  room_id=id;outdoor=1;break;
 }
 if(!outdoor)return 0;
 /* Draw across the active view's full clip rect whenever any outdoor room is
  * visible: open-air courtyard rooms below sky-cap rooms omit ROOM_FLAG_OUTSIDE,
  * so scissoring to outside-flagged portals clips the horizon into black bands. */
 const tr64_gpu_rect_t clip=tr64_gpu_projection_clip(&pose->projection);
 const tr64_skybox_t *sb=skybox_state(r);
 const tr64_geo_t *g=&r->meshes;
 const uint32_t *span=object_spans(r,1)+3u*sb->mesh;
 uint32_t run_count=span[2];
 if(!run_count)return 0;
 tr64_gpu_transform_t m;
 memset(&m,0,sizeof(m));
 memcpy(m.basis,sb->basis,sizeof(m.basis));
 for(unsigned k=0;k<3;++k) {
  int64_t t=(int64_t)pose->view.eye[k]+(int64_t)sb->offset[k];
  if(t<INT32_MIN || t>INT32_MAX)return error(e,TR64_RENDER_BOUNDS,TR64_RENDER_SECTION_MESHES,sb->mesh);
  m.translation[k]=(int32_t)t;
 }
 m.mesh=1;
 m.water=tr64_render_cam_water;
 m.logical_room=(int32_t)room_id;
 unsigned previous=UINT_MAX;
 int visible=0,bound=0;
 uint32_t start=render_ticks(),cull_ticks=0,submit_ticks=0,tested=0,rejected=0;
 const uint8_t *const base=g->bytes.data,*const batches=base+g->batch_offset;
 const uint8_t *face_ptr=base+g->face_offset+8u*span[1];
 const uint8_t *run=base+g->run_offset+24u*span[0];
 const uint8_t *batch_verts=NULL;
 uint16_t batch_vcount=0;
 const uint16_t *texture_map=r->texture_map;
 for(uint32_t i=0;i<run_count;++i,run+=24) {
  unsigned b=u16_a(run+4),n=u16_a(run+6);
  if(b!=previous) {
   uint32_t phase=render_ticks();
   const uint8_t *batch=batches+32u*b;
   ++tested;
   visible=batch_visible_mesh(batch,&m,&pose->view,clip);
   rejected+=(uint32_t)!visible;
   previous=b;
   if(visible) { batch_verts=base+u32_a(batch+16);batch_vcount=u16_a(batch+12); }
   cull_ticks+=render_ticks()-phase;
  }
  if(visible) {
   uint32_t phase=render_ticks();
   if(!bound) {
    if(counts->matrix_slots>=TR64_GPU_MATRIX_LIMIT) {
     counts->stream_runs+=i+1u;counts->batches_tested+=tested;counts->batches_rejected+=rejected;
     return error(e,TR64_RENDER_WORKSPACE,0,counts->matrix_slots);
    }
    m.instance_id=counts->matrix_slots++;
    tr64_gpu_scissor(clip);
    tr64_gpu_matrix(&m);
    bound=1;
   }
   unsigned logical=u16_a(run),content=logical;
   if(!(run[3]&TR64_GPU_FLAT) && texture_map)content=texture_map[logical];
   tr64_gpu_material((tr64_gpu_material_t){(uint16_t)logical,(uint16_t)content,run[2],(uint8_t)(run[3]|TR64_GPU_NO_DEPTH)});
   tr64_gpu_batch_t packet={batch_verts,face_ptr,run+8,batch_vcount,(uint16_t)n,b|(1u<<31)};
   tr64_gpu_batch(&packet);
   submit_ticks+=render_ticks()-phase;
  }
  face_ptr+=8u*n;
 }
 counts->stream_runs+=run_count;counts->batches_tested+=tested;counts->batches_rejected+=rejected;
 counts->culling_us+=cull_ticks;counts->gpu_submit_us+=submit_ticks;
 counts->stream_scan_us+=render_ticks()-start-cull_ticks-submit_ticks;
 return 0;
}
#endif
extern uint32_t game_view_lights(const int32_t eye[3],tr64_gpu_light_t *out,uint32_t max_lights) __attribute__((weak));
extern uint32_t game_view_shadows(tr64_game_shadow_t *out,uint32_t capacity) __attribute__((weak));
uint8_t tr64_render_cam_water;
enum { VFX_DEFER_CAPACITY=64, SHADOW_CAPACITY=16 };
static tr64_game_drawable_t vfx_deferred[VFX_DEFER_CAPACITY];
static uint32_t vfx_deferred_count;
static struct {
 tr64_gpu_light_t lights[TR64_GPU_MAX_LIGHTS];
 uint32_t view_scale;
 uint8_t count,external_count,lit_pass,live_game;
} frame_lights;
void tr64_render_set_lights(const tr64_gpu_light_t *lights,unsigned count)
{
 if(!lights || !count) {
  frame_lights.count=0;frame_lights.external_count=0;return;
 }
 if(count>TR64_GPU_MAX_LIGHTS)count=TR64_GPU_MAX_LIGHTS;
 memcpy(frame_lights.lights,lights,count*sizeof(*lights));
 frame_lights.external_count=(uint8_t)count;
}
static inline int aabb_overlaps_light(const int64_t lo[3],const int64_t hi[3],const tr64_gpu_light_t *l)
{
 int64_t rad=(int64_t)l->radius;
 if(!l->active || rad<=0)return 0;
 for(unsigned a=0;a<3u;++a) {
  int64_t p=(int64_t)l->pos[a];
  if(hi[a]<p-rad || lo[a]>p+rad)return 0;
 }
 return 1;
}
static __attribute__((noinline))
void prepare_view_fx(const tr64_render_t *r,const tr64_render_pose_t *pose,int live_game,unsigned view_scale)
{
 tr64_render_cam_water=0;
 vfx_deferred_count=0;
 frame_lights.lit_pass=0;
 frame_lights.live_game=(uint8_t)live_game;
 frame_lights.view_scale=view_scale;
 unsigned physical=pose->flipped?mates(r)[pose->camera_room]:pose->camera_room;
 const tr64_level_table_t *rt=r->core->tables+(TR64_CORE_rooms-1);
 const tr64_core_rooms_t *room=physical<rt->count?(const tr64_core_rooms_t *)rt->data+physical:tr64_level_rooms(r->core,physical);
 uint8_t cam_water=(uint8_t)(tr64_u16(room->flags)&1u);
 tr64_render_cam_water=cam_water;
 if(cam_water)tr64_gpu_set_camera_water(1);
 if(frame_lights.external_count) {
  frame_lights.count=frame_lights.external_count;
 } else if(live_game && game_view_lights) {
  uint32_t n=game_view_lights(pose->view.eye,frame_lights.lights,TR64_GPU_MAX_LIGHTS);
  frame_lights.count=(uint8_t)(n>TR64_GPU_MAX_LIGHTS?TR64_GPU_MAX_LIGHTS:n);
 } else {
  frame_lights.count=0;
 }
 if(frame_lights.count)tr64_gpu_set_lights(frame_lights.lights,frame_lights.count);
 frame_lights.lit_pass=(uint8_t)(frame_lights.count!=0 || cam_water!=0);
}
static __attribute__((noinline,cold))
int submit_room_lit(const tr64_render_t *r,unsigned physical,const uint32_t *span,
                    tr64_gpu_transform_t *m,const tr64_render_pose_t *pose,
                    tr64_gpu_rect_t clip,tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 uint32_t run_count=span[2];
 if(!run_count)return 0;
 const tr64_geo_t *g=&r->rooms;
 const uint8_t *obj=object(g,physical);
 int64_t rlo[3]={(int64_t)m->translation[0]+s16_a(obj+12),
                 (int64_t)m->translation[1]+s16_a(obj+14),
                 (int64_t)m->translation[2]+s16_a(obj+16)};
 int64_t rhi[3]={(int64_t)m->translation[0]+s16_a(obj+18),
                 (int64_t)m->translation[1]+s16_a(obj+20),
                 (int64_t)m->translation[2]+s16_a(obj+22)};
 uint8_t room_mask=0;
 for(unsigned li=0;li<frame_lights.count;++li)
  if(aabb_overlaps_light(rlo,rhi,frame_lights.lights+li))room_mask|=(uint8_t)(1u<<li);
 const int cam_tint=(tr64_render_cam_water && !m->water);
 if(!room_mask && !cam_tint)
  return tr64_render_draw_object(g,span,m,pose,clip,counts,0,e,r->texture_map);

 unsigned previous=UINT_MAX;
 int visible=0,bound=0;
 uint32_t lit_bit=0;
 uint32_t start=render_ticks(),cull_ticks=0,submit_ticks=0,tested=0,rejected=0;
 const uint8_t *const base=g->bytes.data,*const batches=base+g->batch_offset;
 const uint8_t *face_ptr=base+g->face_offset+8u*span[1];
 const uint8_t *run=base+g->run_offset+24u*span[0];
 const uint8_t *batch_verts=NULL;
 uint16_t batch_vcount=0;
 const uint16_t *texture_map=r->texture_map;
 int64_t base_v[3];
 {
  const int32_t *vb=pose->view.basis;
  int64_t tx=(int64_t)m->translation[0]-pose->view.eye[0];
  int64_t ty=(int64_t)m->translation[1]-pose->view.eye[1];
  int64_t tz=(int64_t)m->translation[2]-pose->view.eye[2];
  for(unsigned a=0;a<3;++a)
   base_v[a]=(int64_t)vb[3*a]*tx+(int64_t)vb[3*a+1]*ty+(int64_t)vb[3*a+2]*tz;
 }
 for(uint32_t i=0;i<run_count;++i,run+=24) {
  unsigned b=u16_a(run+4),n=u16_a(run+6);
  if(b!=previous) {
   uint32_t phase=render_ticks();
   const uint8_t *batch=batches+32u*b;
   ++tested;
   visible=batch_visible_room(batch,pose->view.basis,base_v,clip);
   rejected+=!visible;previous=b;
   if(visible) {
    batch_verts=base+u32_a(batch+16);batch_vcount=u16_a(batch+12);
    lit_bit=cam_tint ? TR64_GPU_BATCH_LIT : 0u;
    if(!lit_bit) {
     int64_t blo[3]={(int64_t)m->translation[0]+s16_a(batch+0),
                     (int64_t)m->translation[1]+s16_a(batch+2),
                     (int64_t)m->translation[2]+s16_a(batch+4)};
     int64_t bhi[3]={(int64_t)m->translation[0]+s16_a(batch+6),
                     (int64_t)m->translation[1]+s16_a(batch+8),
                     (int64_t)m->translation[2]+s16_a(batch+10)};
     for(unsigned li=0;li<frame_lights.count;++li) {
      if((room_mask&(1u<<li)) && aabb_overlaps_light(blo,bhi,frame_lights.lights+li)) {
       lit_bit=TR64_GPU_BATCH_LIT;
       break;
      }
     }
    }
   }
   cull_ticks+=render_ticks()-phase;
  }
  if(visible) {
   uint32_t phase=render_ticks();
   if(!bound) {
    if(counts->matrix_slots>=TR64_GPU_MATRIX_LIMIT) {
     counts->stream_runs+=i+1u;counts->batches_tested+=tested;counts->batches_rejected+=rejected;
     return error(e,TR64_RENDER_WORKSPACE,0,counts->matrix_slots);
    }
    m->instance_id=counts->matrix_slots++;tr64_gpu_matrix(m);bound=1;
   }
   unsigned logical=u16_a(run),content=logical;
   if(!(run[3]&TR64_GPU_FLAT) && texture_map)content=texture_map[logical];
   tr64_gpu_material((tr64_gpu_material_t){(uint16_t)logical,(uint16_t)content,run[2],run[3]});
   tr64_gpu_batch_t packet={batch_verts,face_ptr,run+8,batch_vcount,(uint16_t)n,b|lit_bit};
   tr64_gpu_batch(&packet);
   submit_ticks+=render_ticks()-phase;
  }
  face_ptr+=8u*n;
 }
 counts->stream_runs+=run_count;counts->batches_tested+=tested;counts->batches_rejected+=rejected;
 counts->culling_us+=cull_ticks;counts->gpu_submit_us+=submit_ticks;
 counts->stream_scan_us+=render_ticks()-start-cull_ticks-submit_ticks;
 return bound;
}
static inline int32_t static_dynamic_light(const int32_t pos[3],int32_t offset)
{
 int32_t best=0;
 for(unsigned li=0;li<frame_lights.count;++li) {
  const tr64_gpu_light_t *l=frame_lights.lights+li;
  int32_t rad=l->radius;
  if(!l->active || rad<=0 || rad>16384 || l->shift>30u)continue;
  int64_t dx=(int64_t)pos[0]-(int64_t)l->pos[0];
  if(dx<-rad || dx>rad)continue;
  int64_t dy=(int64_t)pos[1]-(int64_t)l->pos[1];
  if(dy<-rad || dy>rad)continue;
  int64_t dz=(int64_t)pos[2]-(int64_t)l->pos[2];
  if(dz<-rad || dz>rad)continue;
  int32_t dist2=(int32_t)(dx*dx+dy*dy+dz*dz);
  int32_t v;
  if(rad==32)v=(dist2<=1024)?(int32_t)l->peak:0;
  else {
   if(dist2>=rad*rad)continue;
   v=(int32_t)l->peak-(dist2>>l->shift);
  }
  if(v>best)best=v;
 }
 if(best>0) {
  offset-=best;
  if(offset<-4096)offset=-4096;
 }
 return offset;
}
static __attribute__((noinline,cold))
int submit_rooms_lit(const tr64_render_t *r,const tr64_render_pose_t *pose,
                     tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 const uint16_t *const smap=static_mesh_map(r);
 const uint32_t *const room_spans=object_spans(r,0);
 const uint32_t *const mesh_spans=object_spans(r,1);
 for(unsigned id=0;id<r->rooms.objects;++id) {
  tr64_gpu_rect_t clip=clips(r)[id];if(empty(clip))continue;
  unsigned physical=pose->flipped?mates(r)[id]:id;
  const tr64_level_table_t *rt=r->core->tables+(TR64_CORE_rooms-1);
  const tr64_core_rooms_t *room=physical<rt->count?(const tr64_core_rooms_t *)rt->data+physical:tr64_level_rooms(r->core,physical);
  tr64_gpu_transform_t m;identity(&m);m.translation[0]=tr64_i32(room->x);m.translation[2]=tr64_i32(room->z);
  m.logical_room=(int32_t)id;m.water=(uint8_t)(tr64_u16(room->flags)&1);
  tr64_gpu_scissor(clip);
  if(submit_room_lit(r,physical,room_spans+3*physical,&m,pose,clip,counts,e)<0)return -1;
  unsigned begin=tr64_u32(room->instance_start),n=tr64_u32(room->instance_count);
  if(n) {
   const tr64_core_static_instances_t *sinsts=(const tr64_core_static_instances_t *)r->core->tables[TR64_CORE_static_instances-1].data;
   uint8_t water=(uint8_t)((tr64_u16(room->flags)&1u)|tr64_render_cam_water);
   for(unsigned i=0;i<n;++i) {
    unsigned si=begin+i;
    unsigned mi=smap[si];if(mi==UINT16_MAX)continue;
    const tr64_core_static_instances_t *s=sinsts+si;
    identity(&m);m.mesh=1;m.logical_room=(int32_t)id;m.water=water;
    m.translation[0]=tr64_i32(s->x);m.translation[1]=tr64_i32(s->y);m.translation[2]=tr64_i32(s->z);
    m.light_offset=static_light_offset(r->static_shade_offset,s);
    if(frame_lights.count)m.light_offset=static_dynamic_light(m.translation,m.light_offset);
    unsigned q=((uint16_t)tr64_i16(s->angle))/16384;static const int c[4]={16384,0,-16384,0},sn[4]={0,16384,0,-16384};
    m.basis[0]=m.basis[8]=c[q];m.basis[2]=sn[q];m.basis[6]=-sn[q];
    ++counts->static_instances;
    if(tr64_render_draw_object(&r->meshes,mesh_spans+3*mi,&m,pose,clip,counts,1,e,r->texture_map)<0)return -1;
   }
  }
 }
 return 0;
}
static __attribute__((noinline,cold))
uint32_t defer_blended_drawables(const tr64_render_t *r,tr64_game_drawable_t *buffer,uint32_t n)
{
 const uint32_t item_count=r->rest_mask ? r->core->tables[TR64_CORE_items-1].count : 0;
 uint32_t out=0;
 for(uint32_t i=0;i<n;++i) {
  if(buffer[i].blend) {
   uint32_t id=buffer[i].instance_id,source=id-0x80000000u;
   if((r->live_item_filter && id!=r->live_instance_id) ||
      (r->rest_mask && source<item_count && (r->rest_mask[source/8]&(1u<<(source%8)))))
    continue;
   if(vfx_deferred_count<VFX_DEFER_CAPACITY) {
    vfx_deferred[vfx_deferred_count++]=buffer[i];
    continue;
   }
  }
  if(out!=i)buffer[out]=buffer[i];
  ++out;
 }
 return out;
}
static inline int32_t shadow_sin_q14(uint16_t a)
{
 static const int16_t qsin[17]={0,1606,3196,4756,6270,7723,9102,10394,11585,
  12665,13623,14449,15137,15679,16069,16305,16384};
 unsigned q=a>>14,p=a&16383u;
 if(q&1u)p=16384u-p;
 int32_t v;
 if(p>=16384u)v=16384;
 else {
  unsigned idx=p>>10,frac=p&1023u;
  v=(int32_t)qsin[idx]+(((int32_t)(qsin[idx+1]-qsin[idx])*(int32_t)frac)>>10);
 }
 return (q>=2u)?-v:v;
}
static inline void put_be16(uint8_t *p,int16_t v)
{
 uint16_t u=(uint16_t)v;
 p[0]=(uint8_t)(u>>8);p[1]=(uint8_t)u;
}
static __attribute__((noinline,cold))
void submit_shadows(const tr64_render_t *r,const tr64_render_pose_t *pose,
                    tr64_render_counts_t *counts)
{
 if(!game_view_shadows)return;
 tr64_game_shadow_t shadows[SHADOW_CAPACITY];
 uint32_t n=game_view_shadows(shadows,SHADOW_CAPACITY);
 if(n>SHADOW_CAPACITY)n=SHADOW_CAPACITY;
 if(!n)return;
 static const int16_t oct_c[8]={16384,11585,0,-11585,-16384,-11585,0,11585};
 static const int16_t oct_s[8]={0,11585,16384,11585,0,-11585,-16384,-11585};
 static const uint8_t shadow_faces[64]={
  0,1,2,255,0,0,0,0, 0,2,3,255,0,0,1,0, 0,3,4,255,0,0,2,0, 0,4,5,255,0,0,3,0,
  0,5,6,255,0,0,4,0, 0,6,7,255,0,0,5,0, 0,7,8,255,0,0,6,0, 0,8,1,255,0,0,7,0
 };
 static const uint8_t shadow_st[16] __attribute__((aligned(4)))={0};
 const tr64_gpu_rect_t *room_clips=clips(r);
 const int32_t *vb=pose->view.basis,*eye=pose->view.eye;
 for(uint32_t si=0;si<n;++si) {
  if(counts->matrix_slots>=TR64_GPU_MATRIX_LIMIT)break;
  const tr64_game_shadow_t *sh=shadows+si;
  if(sh->room>=r->rooms.objects || !sh->rx || !sh->rz || sh->rx>32767u || sh->rz>32767u)continue;
  tr64_gpu_rect_t clip=room_clips[sh->room];
  if(empty(clip))continue;
  int64_t dx=(int64_t)sh->x-eye[0],dy=(int64_t)sh->y-eye[1],dz=(int64_t)sh->z-eye[2];
  int64_t vx=((int64_t)vb[0]*dx+(int64_t)vb[1]*dy+(int64_t)vb[2]*dz)>>14;
  int64_t vy=((int64_t)vb[3]*dx+(int64_t)vb[4]*dy+(int64_t)vb[5]*dz)>>14;
  int64_t vz=((int64_t)vb[6]*dx+(int64_t)vb[7]*dy+(int64_t)vb[8]*dz)>>14;
  int64_t rad=sh->rx>sh->rz?sh->rx:sh->rz;
  if(vz+rad<TR64_GPU_NEAR || vz-rad>pose->projection.far_z)continue;
  int64_t zmax=vz+rad,zmin=vz>rad?vz-rad:1;
  int64_t cl=clip.left-TR64_GPU_WIDTH/2,cr=clip.right-TR64_GPU_WIDTH/2;
  int64_t ct=clip.top-TR64_GPU_HEIGHT/2,cb=clip.bottom-TR64_GPU_HEIGHT/2;
  if(256*(vx+rad)<cl*(cl<0?zmax:zmin) || 256*(vx-rad)>cr*(cr>0?zmax:zmin) ||
     256*(vy+rad)<ct*(ct<0?zmax:zmin) || 256*(vy-rad)>cb*(cb>0?zmax:zmin))continue;
  int64_t adx=dx<0?-dx:dx,ady=dy<0?-dy:dy,adz=dz<0?-dz:dz;
  if(adx+ady+adz>16384)continue;
  uint8_t packed[160] __attribute__((aligned(16)));
  memset(packed,0,sizeof(packed));
  packed[19]=255;
  for(unsigned k=0;k<8u;++k) {
   unsigned vi=k+1u;
   uint8_t *pair=packed+(vi>>1)*32u;
   uint8_t *vp=pair+(vi&1u)*8u;
   int16_t vx=(int16_t)(((int32_t)sh->rx*oct_c[k])>>14);
   int16_t vz_l=(int16_t)(((int32_t)sh->rz*oct_s[k])>>14);
   put_be16(vp+0,vx);put_be16(vp+2,0);put_be16(vp+4,vz_l);
   pair[16u+(vi&1u)*4u+3u]=255;
  }
  int32_t s=shadow_sin_q14((uint16_t)sh->yaw);
  int32_t c=shadow_sin_q14((uint16_t)((uint16_t)sh->yaw+16384u));
  tr64_gpu_transform_t m;memset(&m,0,sizeof(m));
  m.basis[0]=c;m.basis[2]=s;m.basis[4]=16384;m.basis[6]=-s;m.basis[8]=c;
  m.translation[0]=sh->x;m.translation[1]=sh->y;m.translation[2]=sh->z;
  m.instance_id=counts->matrix_slots++;
  m.logical_room=(int32_t)sh->room;
  tr64_gpu_scissor(clip);
  tr64_gpu_matrix(&m);
  tr64_gpu_material((tr64_gpu_material_t){UINT16_MAX,UINT16_MAX,0,
   TR64_GPU_FLAT|TR64_GPU_BLENDED_M1|TR64_GPU_DOUBLE_SIDED});
  tr64_gpu_batch_t batch={packed,shadow_faces,shadow_st,9,8,
   TR64_GPU_BATCH_TRANSIENT|(0x10000000u+si)};
  tr64_gpu_batch(&batch);
 }
}
static __attribute__((noinline,cold))
int draw_object_blend(const tr64_geo_t *g,const uint32_t *span,tr64_gpu_transform_t *m,
                      const tr64_render_pose_t *pose,tr64_gpu_rect_t clip,
                      tr64_render_counts_t *counts,tr64_render_error_t *e,
                      const uint16_t *texture_map,uint8_t blend_flag)
{
 uint32_t run_count=span[2];
 if(!run_count)return 0;
 unsigned previous=UINT_MAX;
 int visible=0,bound=0;
 const uint8_t *const base=g->bytes.data,*const batches=base+g->batch_offset;
 const uint8_t *face_ptr=base+g->face_offset+8u*span[1];
 const uint8_t *run=base+g->run_offset+24u*span[0];
 const uint8_t *batch_verts=NULL;
 uint16_t batch_vcount=0;
 for(uint32_t i=0;i<run_count;++i,run+=24) {
  unsigned b=u16_a(run+4),n=u16_a(run+6);
  if(b!=previous) {
   const uint8_t *batch=batches+32u*b;
   ++counts->batches_tested;
   visible=batch_visible_mesh(batch,m,&pose->view,clip);
   counts->batches_rejected+=(uint32_t)!visible;previous=b;
   if(visible) { batch_verts=base+u32_a(batch+16);batch_vcount=u16_a(batch+12); }
  }
  if(visible) {
   if(!bound) {
    if(counts->matrix_slots>=TR64_GPU_MATRIX_LIMIT)
     return error(e,TR64_RENDER_WORKSPACE,0,counts->matrix_slots);
    m->instance_id=counts->matrix_slots++;tr64_gpu_matrix(m);bound=1;
   }
   unsigned logical=u16_a(run),content=logical;
   if(!(run[3]&TR64_GPU_FLAT) && texture_map)content=texture_map[logical];
   uint8_t flags=(blend_flag&TR64_GPU_ADDITIVE)
    ?(uint8_t)(run[3]|TR64_GPU_ADDITIVE)
    :(uint8_t)((run[3]&~TR64_GPU_CLASS_MASK)|TR64_GPU_BLENDED_M1);
   tr64_gpu_material((tr64_gpu_material_t){(uint16_t)logical,(uint16_t)content,run[2],flags});
   tr64_gpu_batch_t packet={batch_verts,face_ptr,run+8,batch_vcount,(uint16_t)n,b|(1u<<31)};
   tr64_gpu_batch(&packet);
  }
  face_ptr+=8u*n;
 }
 counts->stream_runs+=run_count;
 return bound;
}
static __attribute__((noinline,cold))
int submit_deferred_vfx(const tr64_render_t *r,const tr64_render_pose_t *pose,
                        unsigned view_scale,tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 const tr64_gpu_rect_t viewport=tr64_gpu_projection_clip(&pose->projection);
 const uint32_t mesh_ptr_count=r->core->tables[TR64_CORE_mesh_pointers-1].count;
 const uint16_t *const slot_map=mesh_slot_map(r);
 const tr64_mesh_prep_t *const prep=mesh_prep(r);
 const uint32_t *const mesh_spans=object_spans(r,1);
 const int32_t *const eye=pose->view.eye;
 const int64_t max_dist=(int64_t)(((UINT64_C(262059)<<14)-16384u)/view_scale);
 for(unsigned pass=0;pass<2u;++pass) {
  const uint8_t want=pass==0u ? TR64_GAME_DRAW_TRANSLUCENT : TR64_GAME_DRAW_ADDITIVE;
  const uint8_t gpu_flag=pass==0u ? TR64_GPU_BLENDED_M1 : (TR64_GPU_CUTOUT|TR64_GPU_ADDITIVE);
  const tr64_gpu_rect_t *room_clips=clips(r);
  for(uint32_t i=0;i<vfx_deferred_count;++i) {
   const tr64_game_drawable_t *part=vfx_deferred+i;
   if(!(part->blend&want))continue;
   if(part->room>=r->rooms.objects || part->water>1u || part->kind>TR64_GAME_DRAW_SPRITE)continue;
   if(empty(room_clips[part->room]))continue;
   if(counts->matrix_slots>=TR64_GPU_MATRIX_LIMIT)break;
   if(part->kind==TR64_GAME_DRAW_SPRITE) {
    if(part->mesh_slot>=65536u)continue;
    int64_t world[3]={part->translation[0],part->translation[1],part->translation[2]};
    int64_t g=4095-(int64_t)part->light_offset;unsigned gain=g<0?0:(g>8192?8192:(unsigned)g);
    int drawn=tr64_render_sprite_flags(r,pose,viewport,world,(uint16_t)part->mesh_slot,(uint16_t)gain,
     part->water,part->room,counts->matrix_slots,gpu_flag,counts,e);
    if(drawn<0)return -1;
    counts->item_sprites+=(uint32_t)drawn;counts->matrix_slots+=(uint32_t)drawn;
   } else {
    if(part->mesh_slot>=mesh_ptr_count)continue;
    unsigned mi=slot_map[part->mesh_slot];
    if(mi==UINT16_MAX)continue;
    unsigned scale=basis_scale(part->basis);
    if(scale==UINT_MAX)continue;
    const tr64_mesh_prep_t *mp=prep+mi;
    int64_t dx=(int64_t)part->translation[0]-eye[0];if(dx<0)dx=-dx;
    int64_t dy=(int64_t)part->translation[1]-eye[1];if(dy<0)dy=-dy;
    int64_t dz=(int64_t)part->translation[2]-eye[2];if(dz<0)dz=-dz;
    int64_t dist=dx+dy+dz+(int64_t)(((uint64_t)(3u*mp->radius)*scale+16383u)>>14);
    if(dist>max_dist || mp->sort_key==UINT32_MAX)continue;
    tr64_gpu_transform_t m;
    for(unsigned k=0;k<9;++k)m.basis[k]=part->basis[k];
    m.translation[0]=part->translation[0];m.translation[1]=part->translation[1];m.translation[2]=part->translation[2];
    m.instance_id=0;m.light_offset=part->light_offset;
    m.view_space=0;m.water=(uint8_t)(part->water|tr64_render_cam_water);m.mesh=1;m.logical_room=(int32_t)part->room;
    tr64_gpu_scissor(viewport);
    int drawn=draw_object_blend(&r->meshes,mesh_spans+3*mi,&m,pose,viewport,counts,e,r->texture_map,gpu_flag);
    if(drawn<0)return -1;
    counts->entity_instances+=(uint32_t)drawn;
   }
  }
 }
 return 0;
}
static __attribute__((noinline))
int submit_post_opaque(const tr64_render_t *r,const tr64_render_pose_t *pose,
                       tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 int rc=0;
 if(frame_lights.live_game)submit_shadows(r,pose,counts);
 if(vfx_deferred_count) {
  rc=submit_deferred_vfx(r,pose,frame_lights.view_scale,counts,e);
  vfx_deferred_count=0;
 }
 tr64_render_cam_water=0;
 return rc<0?-1:0;
}
static inline __attribute__((always_inline))
int submit_view(tr64_render_t *r,const tr64_render_pose_t *pose,unsigned view_scale,
                tr64_game_drawable_t *buffer,uint32_t capacity,uint32_t first_item_sprites,
                tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 /* Phase accumulators hold raw profile ticks until all views are submitted. */
 active_far_q14=(int32_t)pose->projection.far_z*16384;
 uint32_t phase=sys_profile_ticks();visibility(r,pose,counts);counts->visibility_us+=sys_profile_ticks()-phase;
 (void)tr64_level_table(r->core,TR64_CORE_rooms);
 prepare_view_fx(r,pose,buffer!=NULL,view_scale);
#if !defined(N64) || TR64_GAME >= 2
 if(__builtin_expect(r->bound&TR64_RENDER_BOUND_SKYBOX,0) && submit_skybox(r,pose,counts,e)<0)return -1;
#endif
 if(__builtin_expect(frame_lights.lit_pass,0)) {
  if(submit_rooms_lit(r,pose,counts,e)<0)return -1;
 } else {
  const uint16_t *const smap=static_mesh_map(r);
  const uint32_t *const room_spans=object_spans(r,0);
  const uint32_t *const mesh_spans=object_spans(r,1);
  for(unsigned id=0;id<r->rooms.objects;++id) {
   tr64_gpu_rect_t clip=clips(r)[id];if(empty(clip))continue;
   unsigned physical=pose->flipped?mates(r)[id]:id;
   const tr64_level_table_t *rt=r->core->tables+(TR64_CORE_rooms-1);
   const tr64_core_rooms_t *room=physical<rt->count?(const tr64_core_rooms_t *)rt->data+physical:tr64_level_rooms(r->core,physical);
   tr64_gpu_transform_t m;identity(&m);m.translation[0]=tr64_i32(room->x);m.translation[2]=tr64_i32(room->z);
   m.logical_room=(int32_t)id;m.water=(uint8_t)(tr64_u16(room->flags)&1);
   tr64_gpu_scissor(clip);
   if(tr64_render_draw_object(&r->rooms,room_spans+3*physical,&m,pose,clip,counts,0,e,r->texture_map)<0)return -1;
   unsigned begin=tr64_u32(room->instance_start),n=tr64_u32(room->instance_count);
   if(n) {
    const tr64_core_static_instances_t *sinsts=(const tr64_core_static_instances_t *)r->core->tables[TR64_CORE_static_instances-1].data;
    uint8_t water=(uint8_t)(tr64_u16(room->flags)&1);
    for(unsigned i=0;i<n;++i) {
     unsigned si=begin+i;
     unsigned mi=smap[si];if(mi==UINT16_MAX)continue;
     const tr64_core_static_instances_t *s=sinsts+si;
     identity(&m);m.mesh=1;m.logical_room=(int32_t)id;m.water=water;
     m.light_offset=static_light_offset(r->static_shade_offset,s);
     m.translation[0]=tr64_i32(s->x);m.translation[1]=tr64_i32(s->y);m.translation[2]=tr64_i32(s->z);
     unsigned q=((uint16_t)tr64_i16(s->angle))/16384;static const int c[4]={16384,0,-16384,0},sn[4]={0,16384,0,-16384};
     m.basis[0]=m.basis[8]=c[q];m.basis[2]=sn[q];m.basis[6]=-sn[q];
     ++counts->static_instances;
     if(tr64_render_draw_object(&r->meshes,mesh_spans+3*mi,&m,pose,clip,counts,1,e,r->texture_map)<0)return -1;
    }
   }
  }
 }
 if(buffer)for(unsigned id=0;id<r->rooms.objects;++id) {
  if(empty(clips(r)[id]))continue;
  uint32_t cursor=0;
  while(cursor!=TR64_GAME_VIEW_END) {
   uint32_t before=cursor;phase=sys_profile_ticks();
   uint32_t n=game_view_room(id,&cursor,buffer,capacity);
   counts->view_us+=sys_profile_ticks()-phase;++counts->room_pulls;
   if(n>capacity)return error(e,TR64_RENDER_WORKSPACE,0,n);
   if(cursor==before)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_CORE,id);
   counts->drawables_pulled+=n;
   n=defer_blended_drawables(r,buffer,n);
   if(n && tr64_render_draw_items(r,pose,buffer,n,TR64_RENDER_ITEMS_WORLD,view_scale,counts,e))return -1;
  }
 }
 if(buffer)for(uint32_t i=0;i<r->rest_model_count;++i) {
  const tr64_core_items_t *item=tr64_level_items(r->core,r->rest_model_ids[i]);
  if(empty(clips(r)[tr64_u16(item->room)]))continue;
  phase=sys_profile_ticks();
  uint32_t n=game_rest_item_view(r->rest_model_ids[i],r->rest_buffer,r->rest_capacity);
  counts->view_us+=sys_profile_ticks()-phase;
  if(n>r->rest_capacity)return error(e,TR64_RENDER_WORKSPACE,0,n);
  if(tr64_render_draw_items(r,pose,r->rest_buffer,n,TR64_RENDER_ITEMS_REST,view_scale,counts,e))return -1;
 }
 for(unsigned id=0;id<r->rooms.objects;++id) {
  tr64_gpu_rect_t clip=clips(r)[id];if(empty(clip))continue;
  unsigned physical=pose->flipped?mates(r)[id]:id;
  const tr64_level_table_t *rt=r->core->tables+(TR64_CORE_rooms-1);
  const tr64_core_rooms_t *room=physical<rt->count?(const tr64_core_rooms_t *)rt->data+physical:tr64_level_rooms(r->core,physical);
  unsigned begin=tr64_u32(room->sprite_start),n=tr64_u32(room->sprite_count);
  for(unsigned i=0;i<n;++i) {
   const tr64_level_table_t *rst=r->core->tables+(TR64_CORE_room_sprites-1);
   unsigned si=begin+i;
   const tr64_core_room_sprites_t *s=si<rst->count?(const tr64_core_room_sprites_t *)rst->data+si:tr64_level_room_sprites(r->core,si);
   int64_t world[3]={(int64_t)tr64_i32(room->x)+tr64_i16(s->x),tr64_i16(s->y),(int64_t)tr64_i32(room->z)+tr64_i16(s->z)};
   unsigned matrix=counts->matrix_slots;
   int result=tr64_render_sprite(r,pose,clip,world,tr64_u16(s->descriptor),tr64_u16(s->gain),
    (uint8_t)(tr64_u16(room->flags)&1),id,matrix,counts,e);
   if(result<0)return -1;
   counts->room_sprites+=(uint32_t)result;
   counts->matrix_slots+=(uint32_t)result;
  }
 }
 if(r->rest_items)for(unsigned i=0;i<tr64_level_table(r->core,TR64_CORE_items)->count &&
     counts->item_sprites-first_item_sprites<r->rest_items;++i) {
  unsigned descriptor=item_sprites(r)[i];if(descriptor==UINT16_MAX)continue;
  const tr64_core_items_t *s=tr64_level_items(r->core,i);unsigned room=tr64_u16(s->room);
  tr64_gpu_rect_t clip=clips(r)[room];if(empty(clip))continue;
  unsigned physical=pose->flipped?mates(r)[room]:room;
  int64_t world[3]={tr64_i32(s->x),tr64_i32(s->y),tr64_i32(s->z)};
  int intensity=tr64_i16(s->intensity);
  unsigned darkness=intensity<0?0:(unsigned)(uint8_t)(intensity>>5)*32;
  unsigned matrix=counts->matrix_slots;
  int result=tr64_render_sprite(r,pose,clip,world,(uint16_t)descriptor,(uint16_t)(8191-darkness),
   (uint8_t)(tr64_u16(tr64_level_rooms(r->core,physical)->flags)&1),room,matrix,counts,e);
  if(result<0)return -1;
  counts->item_sprites+=(uint32_t)result;
  counts->matrix_slots+=(uint32_t)result;
 }
 if(submit_post_opaque(r,pose,counts,e)<0)return -1;
 return 0;
}
static __attribute__((noinline,cold))
int submit_view_split(tr64_render_t *r,const tr64_render_pose_t *pose,unsigned view_scale,
                      tr64_game_drawable_t *buffer,uint32_t capacity,uint32_t first_item_sprites,
                      tr64_render_counts_t *counts,tr64_render_error_t *e)
{ return submit_view(r,pose,view_scale,buffer,capacity,first_item_sprites,counts,e); }
static __attribute__((noinline,cold))
int submit_pair(tr64_render_t *r,const tr64_render_pose_t *first,const tr64_render_pose_t *second,
                unsigned scale,unsigned second_scale,tr64_game_drawable_t *buffer,uint32_t capacity,
                tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 tr64_gpu_split_prepare();
 if(submit_view_split(r,first,scale,buffer,capacity,0,counts,e))return -1;
 unsigned n=r->rooms.objects;
 uint8_t first_visible[(n+7u)/8u];memset(first_visible,0,sizeof(first_visible));
 for(unsigned i=0;i<n;++i)if(!empty(clips(r)[i]))first_visible[i/8]|=1u<<(i%8);
 tr64_gpu_split_matrices(counts->matrix_slots);
 tr64_gpu_view_begin(&second->view,&second->projection);
 counts->matrix_slots=0;
 if(submit_view_split(r,second,second_scale,buffer,capacity,counts->item_sprites,counts,e))return -1;
 tr64_gpu_split_matrices(counts->matrix_slots);
 /* The portal work queue is idle. Preserve the first list for observers
  * without polluting the second viewport's clips or growing the workspace. */
 for(unsigned i=0;i<n;++i)queued(r)[i]=(first_visible[i/8]>>(i%8))&1u;
 return 0;
}
/* Keep the hot first view's traversal and submission locals in one frame. */
static __attribute__((noinline))
int frame(tr64_render_t *r,const tr64_render_pose_t *pose,
          tr64_game_drawable_t *buffer,uint32_t capacity,const tr64_render_pose_t *second,
          tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 if(!r || !r->bound || !pose || !counts)return error(e,TR64_RENDER_CAMERA,0,0);
 unsigned scale=validate_eye(r,pose),second_scale=second?validate_eye(r,second):0;
 if(!scale || (second && !second_scale))return error(e,TR64_RENDER_CAMERA,0,0);
 if(buffer && r->rest_model_count && !r->rest_prepared)return error(e,TR64_RENDER_WORKSPACE,0,0);
 memset(counts,0,sizeof(*counts));counts->workspace_bytes=(uint32_t)r->workspace_bytes;
 tr64_gpu_frame_begin(&pose->view,&pose->projection);
 tr64_render_texture_apply(r,counts);
 if(second ? submit_pair(r,pose,second,scale,second_scale,buffer,capacity,counts,e)
           : submit_view(r,pose,scale,buffer,capacity,0,counts,e)) {
  active_far_q14=TR64_GPU_FAR*16384;
  tr64_render_cam_water=0;vfx_deferred_count=0;
  return -1;
 }
 active_far_q14=TR64_GPU_FAR*16384;
 counts->visibility_us=sys_profile_us(counts->visibility_us);
 counts->culling_us=sys_profile_us(counts->culling_us);
 counts->stream_scan_us=sys_profile_us(counts->stream_scan_us);
 counts->gpu_submit_us=sys_profile_us(counts->gpu_submit_us);
 counts->view_us=sys_profile_us(counts->view_us);
 tr64_gpu_frame_end(&counts->gpu);
 if(second)counts->split=*tr64_gpu_split_counts();
 return 0;
}
int tr64_render_frame(tr64_render_t *r,const tr64_render_pose_t *pose,
                      tr64_render_counts_t *counts,tr64_render_error_t *e)
{ return frame(r,pose,NULL,0,NULL,counts,e); }
int tr64_render_split_frame(tr64_render_t *r,const tr64_render_pose_t *top,const tr64_render_pose_t *bottom,
                            tr64_render_counts_t *counts,tr64_render_error_t *e)
{ return frame(r,top,NULL,0,bottom,counts,e); }
static inline __attribute__((always_inline))
int game_frame(tr64_render_t *r,const tr64_game_view_t *view,const tr64_render_pose_t *second,
                      tr64_game_drawable_t *buffer,uint32_t capacity,
                      tr64_render_counts_t *counts,tr64_render_error_t *e)
{
 if(!r || !r->bound || !view || view->flipped>1)return error(e,TR64_RENDER_CAMERA,0,0);
 if(!buffer || !capacity)return error(e,TR64_RENDER_WORKSPACE,0,capacity);
 r->texture_requested_step=view->texture_animation_step;
 tr64_render_pose_t pose;memset(&pose,0,sizeof(pose));
 memcpy(pose.view.basis,view->camera_basis,sizeof(pose.view.basis));
 memcpy(pose.view.eye,view->camera_eye,sizeof(pose.view.eye));
 pose.projection=(tr64_gpu_projection_t)TR64_GPU_WORLD_PROJECTION;
 apply_game_projection(&pose.projection);
 pose.camera_room=view->camera_room;pose.flipped=(uint8_t)view->flipped;
 if(second) {
  tr64_render_half(&pose,0);
  tr64_render_pose_t s2=*second;
  s2.projection.far_z=s2.projection.fog_far_z=pose.projection.far_z;
  s2.projection.fog_near_z=pose.projection.fog_near_z;
  return frame(r,&pose,buffer,capacity,&s2,counts,e);
 }
 return frame(r,&pose,buffer,capacity,NULL,counts,e);
}
int tr64_render_game_frame(tr64_render_t *r,const tr64_game_view_t *view,
                           tr64_game_drawable_t *buffer,uint32_t capacity,
                           tr64_render_counts_t *counts,tr64_render_error_t *e)
{
#ifndef TR64_HOST
 /* Keep room submission at the pre-split stack phase on the 8 KiB direct-mapped
  * D-cache. This reserves stack addresses without reading or writing them. */
 char stack_phase[120] __attribute__((aligned(8)));
 __asm__ volatile("" : "=m"(stack_phase));
#endif
 return game_frame(r,view,NULL,buffer,capacity,counts,e);
}
int tr64_render_game_split_frame(tr64_render_t *r,const tr64_game_view_t *view,
                                 const tr64_render_pose_t *second,tr64_game_drawable_t *buffer,uint32_t capacity,
                                 tr64_render_counts_t *counts,tr64_render_error_t *e)
{ return game_frame(r,view,second,buffer,capacity,counts,e); }
uint32_t tr64_render_visible_rooms(const tr64_render_t *r,uint16_t *out,uint32_t capacity)
{
 uint32_t n=0;for(unsigned i=0;i<r->rooms.objects;++i)n+=!empty(clips(r)[i]) || queued(r)[i];
 if(capacity>=n) {
  unsigned j=0;for(unsigned i=0;i<r->rooms.objects;++i)
   if(!empty(clips(r)[i]) || queued(r)[i])out[j++]=(uint16_t)i;
 }
 return n;
}
