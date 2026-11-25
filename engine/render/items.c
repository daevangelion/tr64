/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/sprite.h"
typedef struct { uint32_t key;uint16_t index,object; } draw_work;
static void sift(draw_work *w,unsigned at,unsigned n)
{
 draw_work value=w[at];
 while(at<n/2) {
  unsigned child=at*2+1;
  if(child+1<n && w[child].key<w[child+1].key)++child;
  if(value.key>=w[child].key)break;
  w[at]=w[child];at=child;
 }
 w[at]=value;
}
static __attribute__((noinline)) int prepare_item(
 const tr64_render_t *r,const tr64_render_pose_t *pose,const tr64_game_drawable_t *d,
 unsigned count,int menu,unsigned view_scale,tr64_gpu_rect_t viewport,
 draw_work *work,uint32_t begin,tr64_render_error_t *e)
{
 const uint32_t mesh_ptr_count=r->core->tables[TR64_CORE_mesh_pointers-1].count;
 const uint16_t *const slot_map=mesh_slot_map(r);
 const tr64_mesh_prep_t *const prep=mesh_prep(r);
 const uint32_t tex_objects=u32_a(r->textures.data+12),tex_sprites=u32_a(r->textures.data+16);
 const tr64_gpu_rect_t *const room_clips=clips(r);
 const int32_t *const eye=pose->view.eye;
 const int64_t max_dist=(int64_t)(((UINT64_C(262059)<<14)-16384u)/view_scale);
 uint32_t bones[8]={0,0,0,0,0,0,0,0};
 unsigned used=0;
 const tr64_game_drawable_t *p=d;
 for(unsigned i=0;i<count;++i,++p) {
  unsigned bone=p->bone;
  if((menu ? (p->room!=UINT32_MAX || p->water || p->kind!=TR64_GAME_DRAW_MESH) :
              p->room>=r->rooms.objects) ||
     p->water>1 || p->kind>TR64_GAME_DRAW_SPRITE ||
     (bones[bone>>5]&(1u<<(bone&31u))))
   return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_CORE,begin+i);
  bones[bone>>5]|=1u<<(bone&31u);
  tr64_gpu_rect_t clip=menu?viewport:room_clips[p->room];
  if(clip.left>=clip.right || clip.top>=clip.bottom)continue;
  draw_work w={bone,(uint16_t)i,UINT16_MAX};
  if(p->kind==TR64_GAME_DRAW_SPRITE) {
   if(p->mesh_slot<tex_objects || p->mesh_slot-tex_objects>=tex_sprites)
    return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_TEXTURES,p->mesh_slot);
   w.key|=((uint32_t)(uint16_t)p->mesh_slot<<8);
  } else {
   if(p->mesh_slot>=mesh_ptr_count)
    return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_CORE,p->mesh_slot);
   unsigned mi=slot_map[p->mesh_slot];
   if(mi==UINT16_MAX) {
    const tr64_u32_be_t *mesh_ptrs=(const tr64_u32_be_t *)r->core->tables[TR64_CORE_mesh_pointers-1].data;
    return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_MESHES,tr64_u32(mesh_ptrs[p->mesh_slot]));
   }
   unsigned scale=basis_scale(p->basis);
   if(scale==UINT_MAX)return error(e,TR64_RENDER_BOUNDS,TR64_RENDER_SECTION_MESHES,begin+i);
   const tr64_mesh_prep_t *mp=prep+mi;
   int64_t dx=(int64_t)p->translation[0]-eye[0];if(dx<0)dx=-dx;
   int64_t dy=(int64_t)p->translation[1]-eye[1];if(dy<0)dy=-dy;
   int64_t dz=(int64_t)p->translation[2]-eye[2];if(dz<0)dz=-dz;
   int64_t distance=dx+dy+dz+(((uint64_t)(3u*mp->radius)*scale+16383u)>>14);
   if(distance>max_dist)
    return error(e,TR64_RENDER_BOUNDS,TR64_RENDER_SECTION_MESHES,begin+i);
   if(mp->sort_key==UINT32_MAX)continue;
   w.object=(uint16_t)mi;
   w.key|=mp->sort_key;
  }
  work[used++]=w;
 }
 if(used>1) {
  if(used<=24) {
   for(unsigned i=1;i<used;++i) {
    draw_work v=work[i];unsigned j=i;
    while(j && work[j-1].key>v.key) { work[j]=work[j-1];--j; }
    work[j]=v;
   }
  } else {
   for(unsigned i=used/2;i;i--)sift(work,i-1,used);
   for(unsigned n=used;n>1;--n) { draw_work tmp=work[0];work[0]=work[n-1];work[n-1]=tmp;sift(work,0,n-1); }
  }
 }
 return (int)used;
}
int tr64_render_draw_items(tr64_render_t *r,const tr64_render_pose_t *pose,
 const tr64_game_drawable_t *drawables,uint32_t count,int mode,unsigned view_scale,
 tr64_render_counts_t *counts,tr64_render_error_t *e)
{
#ifndef TR64_HOST
 /* Keep item submission at the pre-refactor stack phase on the 8 KiB D-cache. */
 char stack_phase[32] __attribute__((aligned(8)));
 __asm__ volatile("" : "=m"(stack_phase));
#endif
 if(mode<TR64_RENDER_ITEMS_WORLD || mode>TR64_RENDER_ITEMS_MENU)
  return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_CORE,0);
 int menu=(int)((unsigned)mode>>1);
 if(count && !drawables)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_CORE,0);
 const tr64_gpu_rect_t viewport=tr64_gpu_projection_clip(&pose->projection);
 const uint32_t item_count=r->rest_mask ? r->core->tables[TR64_CORE_items-1].count : 0;
 const uint32_t *const mesh_spans=object_spans(r,1);
 for(uint32_t begin=0;begin<count;) {
  uint32_t end=begin+1,id=drawables[begin].instance_id;
  while(end<count && drawables[end].instance_id==id)++end;
  uint32_t source=id-0x80000000u;
  if(mode==TR64_RENDER_ITEMS_WORLD && ((r->live_item_filter && id!=r->live_instance_id) ||
     (r->rest_mask && source<item_count && (r->rest_mask[source/8]&(1u<<(source%8)))))) {
   begin=end;continue;
  }
  /* One item has u8 bone identities, not MAX_ITEMS*MAX_BONES storage. */
  if(end-begin>256)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_CORE,begin);
  const tr64_game_drawable_t *d=drawables+begin;draw_work work[256];
  int prepared=prepare_item(r,pose,d,end-begin,menu,view_scale,viewport,work,begin,e);
  if(prepared<0)return -1;
  unsigned used=(unsigned)prepared;
  int visible=0;
  if(used)tr64_gpu_scissor(viewport);
  for(unsigned i=0;i<used;++i) {
   const tr64_game_drawable_t *part=d+work[i].index;int drawn;
   /* A reachable item's animated bounds can cross its root-room portal
    * (notably Lara climbing from a lower room into the camera's room).
    * Room clips select items; opaque room depth occludes their geometry.
    * Do not crop articulated bones back to the root-room screen rectangle. */
   if(part->kind==TR64_GAME_DRAW_SPRITE) {
    int64_t world[3]={part->translation[0],part->translation[1],part->translation[2]};
    int64_t g=4095-(int64_t)part->light_offset;unsigned gain=g<0?0:(g>8192?8192:(unsigned)g);
    drawn=tr64_render_sprite(r,pose,viewport,world,(uint16_t)part->mesh_slot,(uint16_t)gain,part->water,part->room,counts->matrix_slots,counts,e);
    if(drawn<0)return -1;
    counts->item_sprites+=(uint32_t)drawn;counts->matrix_slots+=(uint32_t)drawn;
   } else {
    tr64_gpu_transform_t m;
    for(unsigned k=0;k<9;++k)m.basis[k]=part->basis[k];
    m.translation[0]=part->translation[0];m.translation[1]=part->translation[1];m.translation[2]=part->translation[2];
    m.instance_id=0;m.light_offset=part->light_offset;
    m.view_space=0;m.water=part->water;m.mesh=1;m.logical_room=menu?0:(int32_t)part->room;
    drawn=tr64_render_draw_object(&r->meshes,mesh_spans+3*work[i].object,&m,pose,viewport,counts,1,e,r->texture_map);
    if(drawn<0)return -1;
    counts->entity_instances+=(uint32_t)drawn;
   }
   visible|=drawn;
  }
  counts->item_instances+=(uint32_t)visible;begin=end;
 }
 return 0;
}
