/* SPDX-License-Identifier: MIT */
#include <limits.h>
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/load.h"
#include "engine/render/texture.h"
#if !defined(N64) || TR64_GAME >= 2
static const tr64_core_models_t *find_skybox_model(const tr64_level_t *core)
{
 uint32_t sky_id;
 if(core->game==2)sky_id=TR64_SKYBOX_MODEL_ID;
 else if(core->game==3)sky_id=355u;
 else return NULL;
 const tr64_level_table_t *mt=tr64_level_table(core,TR64_CORE_models);
 for(uint32_t i=0;i<mt->count;++i) {
  const tr64_core_models_t *m=tr64_level_models(core,i);
  if(tr64_u32(m->type)==sky_id && tr64_u16(m->mesh_count))return m;
 }
 return NULL;
}
static int32_t sky_sin_q14(int32_t angle)
{
 uint32_t a=(uint32_t)angle&0xffffu;
 int32_t sign=1;
 if(a>=32768u) { a-=32768u;sign=-1; }
 if(a>16384u)a=32768u-a;
 if(!a)return 0;
 if(a==16384u)return 16384*sign;
 int64_t t=((int64_t)a*411775+8)>>4;
 int64_t t2=(t*t)>>28;
 int64_t acc=(INT64_C(1)<<28)-t2/72;
 acc=(INT64_C(1)<<28)-(t2*acc)/(INT64_C(42)<<28);
 acc=(INT64_C(1)<<28)-(t2*acc)/(INT64_C(20)<<28);
 acc=(INT64_C(1)<<28)-(t2*acc)/(INT64_C(6)<<28);
 int64_t val=(t*acc)>>28;
 return (int32_t)((val+(1<<13))>>14)*sign;
}
static void sky_rot(int32_t m[9],unsigned axis,int32_t angle)
{
 static const uint8_t c0_tab[3]={2,0,1},c1_tab[3]={1,2,0};
 int32_t s=sky_sin_q14(angle),c=sky_sin_q14(angle+16384);
 unsigned c0=c0_tab[axis],c1=c1_tab[axis];
 for(unsigned r=0;r<3;++r) {
  int32_t a0=m[3*r+c0],a1=m[3*r+c1];
  m[3*r+c0]=(int32_t)(((int64_t)a0*c-(int64_t)a1*s)>>14);
  m[3*r+c1]=(int32_t)(((int64_t)a1*c+(int64_t)a0*s)>>14);
 }
}
static int bind_skybox(const tr64_render_t *r,const tr64_level_t *core,
                       const tr64_core_models_t *sky,tr64_skybox_t *sb)
{
 uint32_t mstart=tr64_u16(sky->mesh_start);
 if(mstart>=tr64_level_table(core,TR64_CORE_mesh_pointers)->count)return 0;
 uint16_t mi=mesh_slot_map(r)[mstart];
 if(mi==UINT16_MAX || !object_spans(r,1)[3u*mi+2])return 0;
 memset(sb,0,sizeof(*sb));
 sb->mesh=mi;
 sb->basis[0]=sb->basis[4]=sb->basis[8]=16384;
 const tr64_level_table_t *ft=tr64_level_table(core,TR64_CORE_frames);
 const tr64_level_table_t *at=tr64_level_table(core,TR64_CORE_animations);
 uint32_t anim_id=tr64_u16(sky->animation),foff=0,fsize=0;
 if(anim_id!=65535u && anim_id<at->count) {
  const tr64_core_animations_t *anim=tr64_level_animations(core,anim_id);
  foff=tr64_u32(anim->frame_offset)/2u;
  fsize=anim->frame_size;
 } else if(anim_id==65535u) {
  foff=tr64_u32(sky->frame_offset)/2u;
  if(foff<ft->count)fsize=ft->count-foff;
 }
 if(fsize>=10u && foff<=ft->count && fsize<=ft->count-foff) {
  const tr64_core_frames_t *fw=tr64_level_frames(core,foff);
  for(unsigned k=0;k<3;++k) {
   uint32_t v=tr64_u16(fw[6+k]);
   sb->offset[k]=(int32_t)(v&32767u)-(int32_t)(v&32768u);
  }
  uint32_t hi=tr64_u16(fw[9]);
  unsigned mode=hi>>14;
  if(mode) {
   uint32_t single=(hi&1023u)<<6;
   if(single)sky_rot(sb->basis,mode-1u,(int32_t)single);
  } else if(fsize>=11u) {
   uint32_t lo=tr64_u16(fw[10]);
   uint32_t ay=((hi&15u)<<12)|((lo&0xfc00u)>>4);
   uint32_t ax=(hi&0x3ff0u)<<2;
   uint32_t az=(lo&1023u)<<6;
   if(ay)sky_rot(sb->basis,1,(int32_t)ay);
   if(ax)sky_rot(sb->basis,0,(int32_t)ax);
   if(az)sky_rot(sb->basis,2,(int32_t)az);
  }
 }
 return 1;
}
#endif
size_t tr64_render_workspace_bytes_load(const tr64_level_t *core,const tr64_geo_t *rooms,const tr64_geo_t *meshes)
{
 uint32_t n=tr64_level_table(core,TR64_CORE_rooms)->count;
 if(n!=rooms->objects || n>65535 || meshes->objects>=65535)return SIZE_MAX;
 size_t bytes=((size_t)n*13+7)&~(size_t)7;
 bytes+=12*((size_t)rooms->objects+meshes->objects);
 bytes=(bytes+7)&~(size_t)7;
 bytes+=8*(size_t)meshes->objects;
 bytes+=2*(size_t)tr64_level_table(core,TR64_CORE_items)->count;
 bytes+=2*(size_t)tr64_level_table(core,TR64_CORE_mesh_pointers)->count;
 bytes+=2*(size_t)tr64_level_table(core,TR64_CORE_static_instances)->count;
#if !defined(N64) || TR64_GAME >= 2
 if(find_skybox_model(core)) {
  if(bytes>SIZE_MAX-sizeof(tr64_skybox_t)-3)return SIZE_MAX;
  bytes=((bytes+3)&~(size_t)3)+sizeof(tr64_skybox_t);
 }
#endif
 size_t extra=tr64_render_texture_bytes_load(core);
 if(extra==SIZE_MAX || bytes>SIZE_MAX-15 || extra>SIZE_MAX-15-bytes)return SIZE_MAX;
 bytes+=extra;
 return (bytes+15)&~(size_t)15;
}
static int bind_spans(const tr64_geo_t *g,uint32_t *spans,tr64_render_error_t *e,int mesh)
{
 uint32_t run=0,face=0;
 for(uint32_t i=0;i<g->objects;++i) {
  const uint8_t *o=object(g,i);uint32_t first=u32(o+4),last=first+u16(o+8),begin=run;
  spans[3*i]=run;spans[3*i+1]=face;
  while(run<g->runs) {
   const uint8_t *p=g->bytes.data+g->run_offset+24*run;unsigned batch=u16(p+4);
   if(batch>=last)break;
   if(batch<first)return error(e,TR64_RENDER_LAYOUT,mesh?TR64_RENDER_SECTION_MESHES:TR64_RENDER_SECTION_ROOMS,g->run_offset+24*run);
   face+=u16(p+6);++run;
  }
  spans[3*i+2]=run-begin;
 }
 if(run!=g->runs || face!=g->faces)return error(e,TR64_RENDER_LAYOUT,mesh?TR64_RENDER_SECTION_MESHES:TR64_RENDER_SECTION_ROOMS,g->run_offset+24*run);
 return 0;
}
static void extend(tr64_render_t *r,int64_t x,int64_t y,int64_t z)
{
 int64_t p[3]={x,y,z};
 for(unsigned a=0;a<3;++a) {
  if(p[a]<INT32_MIN || p[a]>INT32_MAX) { r->bound=0;return; }
  if(p[a]<r->world_min[a])r->world_min[a]=(int32_t)p[a];
  if(p[a]>r->world_max[a])r->world_max[a]=(int32_t)p[a];
 }
}
static int materials(const tr64_geo_t *g,tr64_gpu_span_t tx,tr64_render_error_t *e)
{
 const uint8_t *p=tx.data;
 for(uint32_t i=0;i<g->runs;++i) {
  const uint8_t *run=g->bytes.data+g->run_offset+24*i;unsigned mat=u16(run),flags=run[3];
  if(mat>=u32(p+((flags&TR64_GPU_FLAT)?32:12)))return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_TEXTURES,48);
  if(!(flags&TR64_GPU_FLAT)) {
   const uint8_t *d=p+u32(p+48)+24*mat;
   if(run[2]>u16(d+22))
    return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_TEXTURES,(uint32_t)(d-p));
  }
 }
 return 0;
}
int tr64_render_bind_load(tr64_render_t *r,const tr64_level_t *core,tr64_gpu_span_t tx,
                     tr64_gpu_span_t rg,tr64_gpu_span_t mg,void *workspace,size_t bytes,
                     tr64_render_error_t *e)
{
 memset(r,0,sizeof(*r));r->core=core;r->textures=tx;
 if(!core->ready || tx.data!=core->textures || tx.bytes!=core->texture_bytes ||
    tx.bytes<80 || memcmp(tx.data,"TX64",4) || u32(tx.data+8)!=tx.bytes)
  return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,0);
 if(tr64_geo_open_load(&r->rooms,rg,0,e) || tr64_geo_open_load(&r->meshes,mg,1,e))return -1;
 size_t need=tr64_render_workspace_bytes_load(core,&r->rooms,&r->meshes);
 if(need==SIZE_MAX || bytes<need || ((uintptr_t)workspace&7))return error(e,TR64_RENDER_WORKSPACE,0,0);
 r->workspace=workspace;r->workspace_bytes=need;memset(workspace,0,need);r->bound=1;
 r->static_shade_offset=core->game==2 ? offsetof(tr64_core_static_instances_t,reserved)
                                     : offsetof(tr64_core_static_instances_t,intensity);
 if(bind_spans(&r->rooms,object_spans(r,0),e,0) ||
    bind_spans(&r->meshes,object_spans(r,1),e,1))return -1;
 {
  tr64_mesh_prep_t *prep=mesh_prep(r);
  const uint32_t *spans=object_spans(r,1);
  const uint8_t *runs=r->meshes.bytes.data+r->meshes.run_offset;
  for(uint32_t i=0;i<r->meshes.objects;++i) {
   const uint8_t *o=object(&r->meshes,i);uint32_t radius=0;
   for(unsigned a=0;a<3;++a) {
    int32_t lo=s16(o+12+2*a),hi=s16(o+18+2*a);
    if(lo<0)lo=-lo;
    if(hi<0)hi=-hi;
    radius+=(uint32_t)(lo>hi?lo:hi);
   }
   const uint32_t *span=spans+3*i;uint32_t key=UINT32_MAX;
   if(span[2]) {
    const uint8_t *run=runs+24*span[0];
    key=((uint32_t)!!(run[3]&TR64_GPU_FLAT)<<24)|((uint32_t)u16(run)<<8);
   }
   prep[i]=(tr64_mesh_prep_t){key,radius};
  }
  uint16_t *slots=mesh_slot_map(r);
  const tr64_level_table_t *mpt=tr64_level_table(core,TR64_CORE_mesh_pointers);
  for(uint32_t i=0;i<mpt->count;++i) {
   unsigned mi=mesh_index(&r->meshes,tr64_u32(*tr64_level_mesh_pointers(core,i)));
   slots[i]=mi==UINT_MAX ? UINT16_MAX : (uint16_t)mi;
  }
 }
 for(uint32_t i=0;i<tr64_level_table(core,TR64_CORE_items)->count;++i) {
  uint32_t type=tr64_u16(tr64_level_items(core,i)->type);item_sprites(r)[i]=UINT16_MAX;
  for(uint32_t j=0;j<tr64_level_table(core,TR64_CORE_sprite_sequences)->count;++j) {
   const tr64_core_sprite_sequences_t *s=tr64_level_sprite_sequences(core,j);
   if(tr64_u32(s->type)==type) { item_sprites(r)[i]=tr64_u16(s->first);break; }
  }
 }
 for(unsigned a=0;a<3;++a) { r->world_min[a]=INT32_MAX;r->world_max[a]=INT32_MIN; }
 uint16_t *mate=mates(r);for(uint32_t i=0;i<r->rooms.objects;++i)mate[i]=(uint16_t)i;
 for(uint32_t i=0;i<r->rooms.objects;++i) {
  const tr64_core_rooms_t *room=tr64_level_rooms(core,i);int alt=tr64_i16(room->alternate_room);
  if(alt>=0) {
   if((uint32_t)alt>=r->rooms.objects || (mate[i]!=i && mate[i]!=(uint32_t)alt) || (mate[alt]!=(uint32_t)alt && mate[alt]!=i))
    return error(e,TR64_RENDER_REFERENCE,0,i);
   mate[i]=(uint16_t)alt;mate[alt]=(uint16_t)i;
  }
  const uint8_t *o=object(&r->rooms,i);int64_t x=tr64_i32(room->x),z=tr64_i32(room->z);
  extend(r,x+s16(o+12),s16(o+14),z+s16(o+16));extend(r,x+s16(o+18),s16(o+20),z+s16(o+22));
  unsigned begin=tr64_u32(room->sprite_start),count=tr64_u32(room->sprite_count);
  for(unsigned j=0;j<count;++j) {
   const tr64_core_room_sprites_t *s=tr64_level_room_sprites(core,begin+j);
   extend(r,x+tr64_i16(s->x),tr64_i16(s->y),z+tr64_i16(s->z));
  }
 }
 /* Movable meshes are checked at submission. Only statics contribute to
  * the world bound, each with its own mesh AABB and authored quadrant. */
 const tr64_level_table_t *defs=tr64_level_table(core,TR64_CORE_static_meshes);
 uint16_t *smap=static_mesh_map(r);
 const uint32_t static_count=tr64_level_table(core,TR64_CORE_static_instances)->count;
 for(uint32_t i=0;i<static_count;++i) {
  smap[i]=UINT16_MAX;
  const tr64_core_static_instances_t *s=tr64_level_static_instances(core,i);
  unsigned id=tr64_u16(s->mesh);
  const tr64_core_static_meshes_t *def=NULL;
  for(uint32_t j=0;j<defs->count;++j) {
   const tr64_core_static_meshes_t *d=tr64_level_static_meshes(core,j);
   if(tr64_u32(d->id)==id) { def=d;break; }
  }
  if(!def)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_MESHES,id);
  if(!(tr64_u16(def->flags)&2))continue;
  const tr64_core_mesh_pointers_t *ptr=tr64_level_mesh_pointers(core,tr64_u16(def->mesh));
  if(!ptr)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_MESHES,id);
  uint32_t source=tr64_u32(*ptr);
  unsigned mesh=mesh_index(&r->meshes,source);
  if(mesh==UINT_MAX)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_MESHES,source);
  smap[i]=(uint16_t)mesh;
  const uint8_t *box=object(&r->meshes,mesh)+12;
  int64_t x=tr64_i32(s->x),y=tr64_i32(s->y),z=tr64_i32(s->z);
  unsigned q=(uint16_t)tr64_i16(s->angle)/16384;
  int lx=s16(box),lz=s16(box+4),hx=s16(box+6),hz=s16(box+10);
  if(q==1) { int t=lx;lx=lz;lz=-hx;hx=hz;hz=-t; }
  else if(q==2) { int t=lx;lx=-hx;hx=-t;t=lz;lz=-hz;hz=-t; }
  else if(q==3) { int t=lx;lx=-hz;hz=hx;hx=-lz;lz=t; }
  extend(r,x+lx,y+s16(box+2),z+lz);extend(r,x+hx,y+s16(box+8),z+hz);
 }
 int64_t extent=0;for(unsigned a=0;a<3;++a)extent+=(int64_t)r->world_max[a]-r->world_min[a];
 if(!r->bound || (3*extent+256)>24576*32)return error(e,TR64_RENDER_BOUNDS,0,0);
 if(materials(&r->rooms,tx,e) || materials(&r->meshes,tx,e))return -1;
#if !defined(N64) || TR64_GAME >= 2
 uint8_t *tail=(uint8_t *)(smap+static_count);
 const tr64_core_models_t *sky=find_skybox_model(core);
 if(sky) {
  tr64_skybox_t *sb=skybox_state(r);
  if(bind_skybox(r,core,sky,sb))r->bound|=TR64_RENDER_BOUND_SKYBOX;
  tail=(uint8_t *)(sb+1);
 }
 uint16_t *animation=(uint16_t *)tail;
#else
 uint16_t *animation=smap+static_count;
#endif
 size_t offset=(size_t)((uint8_t *)animation-(uint8_t *)workspace);
 if(tr64_render_texture_bind_load(r,animation,bytes-offset,e))return -1;
 tr64_gpu_bind_textures(tx);return 0;
}
