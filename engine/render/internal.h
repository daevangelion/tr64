/* SPDX-License-Identifier: MIT */
#ifndef TR64_RENDER_INTERNAL_H
#define TR64_RENDER_INTERNAL_H
#include <limits.h>
#include "tr64/render.h"
/* Arithmetic and layouts used by hot drawing and cold binding. No stored
 * pointer here names code-unit storage; all spans/arrays belong to pools. */
enum { TR64_RENDER_ITEMS_WORLD,TR64_RENDER_ITEMS_REST,TR64_RENDER_ITEMS_MENU };
static inline uint16_t u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static inline int16_t s16(const uint8_t *p) { unsigned v=u16(p);return (int16_t)((int32_t)(v&32767)-(int32_t)(v&32768)); }
static inline uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
/* Naturally aligned big-endian loads for validated 8-byte-aligned RG64/MG64/TX64 tables. */
static inline uint16_t u16_a(const uint8_t *p)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
 return *(const uint16_t *)p;
#else
 return u16(p);
#endif
}
static inline int16_t s16_a(const uint8_t *p)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
 return *(const int16_t *)p;
#else
 return s16(p);
#endif
}
static inline uint32_t u32_a(const uint8_t *p)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
 return *(const uint32_t *)p;
#else
 return u32(p);
#endif
}
static inline int32_t static_light_offset(unsigned shade_offset,const tr64_core_static_instances_t *s)
{ return (int32_t)tr64_u16(*(const tr64_u16_be_t *)((const uint8_t *)s+shade_offset))-4096; }
static inline int error(tr64_render_error_t *e,uint32_t code,uint32_t section,uint32_t offset)
{
 if(e)*e=(tr64_render_error_t){code,section,offset};
 return -1;
}
enum {
 TR64_RENDER_BOUND_SKYBOX=2u,
 TR64_ROOM_FLAG_OUTSIDE=0x0008u,
 TR64_SKYBOX_MODEL_ID=254u
};
typedef struct { uint32_t sort_key,radius; } tr64_mesh_prep_t;
typedef struct {
 int32_t basis[9],offset[3];
 uint16_t mesh,reserved;
} tr64_skybox_t;
static inline tr64_gpu_rect_t *clips(const tr64_render_t *r) { return r->workspace; }
static inline uint16_t *mates(const tr64_render_t *r) { return (uint16_t *)(clips(r)+r->rooms.objects); }
static inline uint16_t *queue(const tr64_render_t *r) { return mates(r)+r->rooms.objects; }
static inline uint8_t *queued(const tr64_render_t *r) { return (uint8_t *)(queue(r)+r->rooms.objects); }
static inline uint32_t *object_spans(const tr64_render_t *r,int mesh)
{
 size_t offset=((size_t)r->rooms.objects*13+7)&~(size_t)7;
 return (uint32_t *)((uint8_t *)r->workspace+offset)+(mesh?3*r->rooms.objects:0);
}
static inline tr64_mesh_prep_t *mesh_prep(const tr64_render_t *r)
{
 size_t offset=((size_t)r->rooms.objects*13+7)&~(size_t)7;
 offset+=12*((size_t)r->rooms.objects+r->meshes.objects);
 offset=(offset+7)&~(size_t)7;
 return (tr64_mesh_prep_t *)((uint8_t *)r->workspace+offset);
}
static inline uint16_t *item_sprites(const tr64_render_t *r)
{ return (uint16_t *)(mesh_prep(r)+r->meshes.objects); }
static inline uint16_t *mesh_slot_map(const tr64_render_t *r)
{ return item_sprites(r)+r->core->tables[TR64_CORE_items-1].count; }
static inline uint16_t *static_mesh_map(const tr64_render_t *r)
{ return mesh_slot_map(r)+r->core->tables[TR64_CORE_mesh_pointers-1].count; }
static inline tr64_skybox_t *skybox_state(const tr64_render_t *r)
{
 uintptr_t p=(uintptr_t)(static_mesh_map(r)+r->core->tables[TR64_CORE_static_instances-1].count);
 return (tr64_skybox_t *)((p+3u)&~(uintptr_t)3u);
}
static inline const uint8_t *object(const tr64_geo_t *g,uint32_t id)
{ return g->bytes.data+g->object_offset+32*id; }
int tr64_render_draw_object(const tr64_geo_t *,const uint32_t *,tr64_gpu_transform_t *,
 const tr64_render_pose_t *,tr64_gpu_rect_t,tr64_render_counts_t *,int,tr64_render_error_t *,const uint16_t *);
int tr64_render_draw_items(tr64_render_t *,const tr64_render_pose_t *,
 const tr64_game_drawable_t *,uint32_t,int,unsigned,tr64_render_counts_t *,tr64_render_error_t *);
typedef struct {
 uint32_t ema_us;
 uint16_t cur_far;
 uint8_t cooldown;
 uint8_t reserved;
} tr64_render_auto_dist_t;
void tr64_render_fog_range(int32_t video_fog,uint16_t *far_out,uint16_t *fog_near_out);
void tr64_render_auto_dist_reset(tr64_render_auto_dist_t *a,uint16_t max_far);
uint16_t tr64_render_auto_dist_step(tr64_render_auto_dist_t *a,uint32_t frame_us,uint16_t max_far);
void tr64_render_apply_scene_options(tr64_gpu_projection_t *projection);
void tr64_render_set_lights(const tr64_gpu_light_t *lights,unsigned count);
int tr64_render_sprite_flags(const tr64_render_t *,const tr64_render_pose_t *,
 tr64_gpu_rect_t,const int64_t [3],uint16_t,uint16_t,uint8_t,uint32_t,uint32_t,uint8_t,
 tr64_render_counts_t *,tr64_render_error_t *);
extern uint8_t tr64_render_cam_water;
static inline unsigned mesh_index(const tr64_geo_t *g,uint32_t source)
{
 const uint8_t *base=g->bytes.data+g->object_offset;
 unsigned lo=0,hi=g->objects;
 while(lo<hi) { unsigned mid=lo+(hi-lo)/2;uint32_t id=u32_a(base+32*mid);if(id<source)lo=mid+1;else hi=mid; }
 return lo<g->objects && u32_a(base+32*lo)==source?lo:UINT_MAX;
}
/* Signed Q1.14 domain; use actual coefficients, not a fitted pose margin. */
static inline unsigned basis_scale(const int32_t basis[9])
{
 unsigned maximum=0;uint32_t bad=0;
 for(unsigned i=0;i<9;++i) {
  int32_t b=basis[i];
  bad|=((uint32_t)b+32768u)>>16;
  uint32_t u=(uint32_t)b;
  unsigned magnitude=b<0 ? (0u-u) : u;
  if(magnitude>maximum)maximum=magnitude;
 }
 return bad ? UINT_MAX : maximum;
}
#endif
