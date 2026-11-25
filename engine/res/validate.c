/* SPDX-License-Identifier: MIT */
/* Consumer rules: OpenLara 2333adef fixed/fmt/phd.h and fixed/room.h. */
#include <string.h>
#include "engine/res/load-internal.h"
#include "tr64/mem.h"
#include "tr64/res.h"
#include "tr64/game.h"
#include "engine/core/sys.h"
#define COUNT(l,k) ((l)->tables[TR64_CORE_##k-1].count)
static void bad(uint32_t k,const char *why)
{ sys_error("core %s: %s",tr64_level_table_name(k),why); }
static void span(const tr64_level_t *l,uint32_t k,uint32_t first,uint32_t n)
{
 uint32_t count=l->tables[k-1].count;
 if(first>count || n>count-first)bad(k,"referenced offset/count out of range");
}
static void *scratch(uint32_t k,uint32_t n)
{
 tr64_mem_stats_t s; tr64_mem_stats(TR64_MEM_io,&s);
 if(s.used || n>s.budget)bad(k,"validation scratch exceeds io pool");
 return tr64_mem_alloc(TR64_MEM_io,n);
}
static uint32_t word(const tr64_level_t *l,uint32_t k,uint32_t i)
{ span(l,k,i,1); return (uint32_t)l->tables[k-1].data[i*2]<<8 | l->tables[k-1].data[i*2+1]; }
static int next_frame(const tr64_level_t *l,uint32_t i,uint32_t f)
{
 const tr64_core_animations_t *a; uint32_t first,last;
 if(i>=COUNT(l,animations))return 0;
 a=tr64_level_animations(l,i); first=tr64_u16(a->frame_start); last=tr64_u16(a->frame_end);
 return (f>=first && f<=last) || (f>last && tr64_u16(a->next_animation)==i &&
  tr64_u16(a->next_frame)>=first && tr64_u16(a->next_frame)<=last);
}
static int terminal_range(const tr64_level_t *l,uint32_t i)
{
 const tr64_core_ranges_t *r; const tr64_core_animations_t *a; const tr64_core_states_t *s;
 uint32_t n=COUNT(l,animations),state;
 if(!n || i+1!=COUNT(l,ranges))return 0;
 r=tr64_level_ranges(l,i); a=tr64_level_animations(l,n-1); state=tr64_u16(a->state_offset);
 if(tr64_u16(r->low)!=0 || tr64_u16(r->high)!=1 || tr64_u16(r->next_animation)!=n ||
    tr64_u16(r->next_frame)!=1 || tr64_u16(a->frame_start) || tr64_u16(a->frame_end) ||
    tr64_u16(a->next_animation)!=n-1 || tr64_u16(a->next_frame) || tr64_u16(a->state_count)!=1 || state>=COUNT(l,states))return 0;
 s=tr64_level_states(l,state);
 return tr64_u16(s->state)==1 && tr64_u16(s->range_count)==1 && tr64_u16(s->range_offset)==i;
}
static void commands(const tr64_level_t *l)
{
 static const uint8_t args[7]={0,3,2,0,0,2,2};
 uint32_t n=COUNT(l,commands),i=n,op,next; uint16_t *remaining;
 if(n>=TR64_BYTES_io/2)bad(TR64_CORE_commands,"validation scratch exceeds io pool");
 remaining=scratch(TR64_CORE_commands,(n+1)*2); remaining[n]=0;
 while(i--) {
  op=word(l,TR64_CORE_commands,i); remaining[i]=0;
  if(op<7 && args[op]<=n-i-1) { next=i+1+args[op]; remaining[i]=(uint16_t)(remaining[next]+1); }
 }
 for(i=0;i<COUNT(l,animations);++i) {
  const tr64_core_animations_t *a=tr64_level_animations(l,i); uint32_t p=tr64_u16(a->command_offset);
  if(p>n || tr64_u16(a->command_count)>remaining[p])bad(TR64_CORE_commands,"unknown opcode or truncated command stream");
 }
 tr64_mem_reset(TR64_MEM_io);
}
static int bit(const uint8_t *p,uint32_t i) { return (p[i>>3]>>(i&7))&1; }
static void mark(uint8_t *p,uint32_t i) { p[i>>3]|=(uint8_t)(1u<<(i&7)); }
static void overlaps(const tr64_level_t *l)
{
 uint32_t n=COUNT(l,overlaps),i=n,v,head; uint8_t *valid=scratch(TR64_CORE_overlaps,(n+7)/8); int good=0;
 memset(valid,0,(n+7)/8);
 while(i--) { v=word(l,TR64_CORE_overlaps,i); good=(v&TR64_BOX_INDEX_MASK)<COUNT(l,boxes) && ((v&TR64_BOX_OVERLAP_END)||good); if(good)mark(valid,i); }
 for(i=0;i<COUNT(l,boxes);++i) {
  v=tr64_u16(tr64_level_boxes(l,i)->overlap);
  if(v==TR64_BOX_NO_OVERLAP)continue;
  head=v&TR64_BOX_INDEX_MASK;
  if(head>=n || !bit(valid,head))bad(TR64_CORE_overlaps,"invalid box chain or missing end bit");
 }
 tr64_mem_reset(TR64_MEM_io);
}
static uint32_t take(const tr64_level_t *l,uint32_t *p)
{
 if(*p>=COUNT(l,floor_data))bad(TR64_CORE_floor_data,"unterminated/truncated stream");
 return word(l,TR64_CORE_floor_data,(*p)++);
}
static int effect(uint32_t n)
{ return n==5 || n==6 || n==7 || n==8 || n==9 || n==11 || n==15 || n==16; }
#if TR64_HAS_TR2
static void tr2_action(const tr64_level_t *l,uint32_t kind,uint32_t arg)
{
 if(kind>TR64_TR2_FLOOR_MAX_ACTION)bad(TR64_CORE_floor_data,"unknown TR2 trigger action");
 if((TR64_TR2_FLOOR_ITEM_MASK&(1u<<kind)) && arg>=COUNT(l,items))bad(TR64_CORE_floor_data,"trigger item out of range");
 if((TR64_TR2_FLOOR_CAMERA_MASK&(1u<<kind)) && arg>=COUNT(l,cameras))bad(TR64_CORE_floor_data,"trigger camera out of range");
 if(kind==TR64_TR2_FLOOR_FIXED_ACTION) {
  /* A fixed view needs a room; a sink shares the table but stores speed here. */
  int32_t room=tr64_i16(tr64_level_cameras(l,arg)->room_or_speed);
  if(room<0 || (uint32_t)room>=COUNT(l,rooms))bad(TR64_CORE_floor_data,"fixed camera room out of range");
 }
}

/* tr2-camera.md, Camera action grammar and renewal: renewal reads the
 * owner word as an ordinary word and ends on the camera action bit,
 * not the extra word. Execution alone need not cover this suffix. */
static void tr2_renewal(const tr64_level_t *l,uint32_t p,uint8_t *verified)
{
 const uint32_t n=COUNT(l,floor_data);
 for(;;) {
  if(p>=n)bad(TR64_CORE_floor_data,"unterminated camera renewal stream");
  if(bit(verified,p))return;
  mark(verified,p);
  uint32_t a=take(l,&p),kind=(a>>10)&TR64_TR2_FLOOR_ACTION_KIND_MASK;
  /* Non-camera/target words are opaque to renewal. They can be outside
   * the execution list, including the authored terminal 0xfd00 in Wall. */
  if(kind==TR64_TR2_FLOOR_FIXED_ACTION || kind==TR64_TR2_FLOOR_TARGET_ACTION)
   tr2_action(l,kind,a&1023);
  if(TR64_TR2_FLOOR_EXTRA_MASK&(1u<<kind))(void)take(l,&p);
  if(a&32768)return;
 }
}
#endif
static uint32_t actions(const tr64_level_t *l,uint32_t start,uint16_t *ends
#if TR64_HAS_TR2
 ,unsigned tr2
#endif
 )
{
 uint32_t p=start,end,a,k,arg,at;
 for(;;) {
  if(p>=COUNT(l,floor_data))bad(TR64_CORE_floor_data,"unterminated action stream");
  if(ends[p]) { end=ends[p]; break; }
  a=take(l,&p); k=(a>>10)&31; arg=a&1023;
#if TR64_HAS_TR2
  if(tr2) {
   k&=TR64_TR2_FLOOR_ACTION_KIND_MASK;
   tr2_action(l,k,arg);
   if((TR64_TR2_FLOOR_FLIP_MASK&(1u<<k)) && arg>=TR64_TR2_FLOOR_FLIP_COUNT)bad(TR64_CORE_floor_data,"flip-map index out of range");
   if(TR64_TR2_FLOOR_EXTRA_MASK&(1u<<k))a=(a&32767)|(take(l,&p)&32768);
  } else
#endif
  {
  if((k==0 || k==6) && arg>=COUNT(l,items))bad(TR64_CORE_floor_data,"trigger item out of range");
  if((k==1 || k==2) && arg>=COUNT(l,cameras))bad(TR64_CORE_floor_data,"trigger camera out of range");
  if((k==3 || k==4 || k==5) && arg>=10)bad(TR64_CORE_floor_data,"flip-map index out of range");
  if(k==8 && arg>=64)bad(TR64_CORE_floor_data,"soundtrack state index out of range");
  if(k==9 && !effect(arg))bad(TR64_CORE_floor_data,"unadmitted effect");
  if(k==10 && arg>=8)bad(TR64_CORE_floor_data,"secret bit out of range");
  if(k==1 || k==12)a=(a&32767)|(take(l,&p)&32768);
  else if(k>13)bad(TR64_CORE_floor_data,"unknown trigger action");
  }
  if(a&32768) { end=p; break; }
 }
 p=start;
 while(p<end && !ends[p]) {
  at=p; a=take(l,&p); k=(a>>10)&31;
#if TR64_HAS_TR2
  if(tr2 ? (TR64_TR2_FLOOR_EXTRA_MASK&(1u<<(k&TR64_TR2_FLOOR_ACTION_KIND_MASK)))!=0 : (k==1 || k==12))++p;
#else
  if(k==1 || k==12)++p;
#endif
  ends[at]=(uint16_t)end;
 }
 return end;
}
static void floors(const tr64_level_t *l
#if TR64_HAS_TR2
 ,unsigned tr2
#endif
 )
{
 uint32_t n=COUNT(l,floor_data),bytes=(n+7)/8,i,p,w,k,arg,trigger; uint16_t *ends; uint8_t *seen;
#if TR64_HAS_TR2
 const uint32_t marks=tr2 ? bytes*2 : bytes;
#else
 const uint32_t marks=bytes;
#endif
 if(n>65535 || marks>TR64_BYTES_io || n>(TR64_BYTES_io-marks)/2)
  bad(TR64_CORE_floor_data,"validation scratch exceeds io pool");
 ends=scratch(TR64_CORE_floor_data,n*2+marks); seen=(uint8_t *)(ends+n);
 memset(ends,0,n*2+marks);
 for(i=0;i<COUNT(l,sectors);++i) {
  p=tr64_u16(tr64_level_sectors(l,i)->floor_index); if(!p)continue;
  for(;;) {
   if(p>=n)bad(TR64_CORE_floor_data,"unterminated/truncated stream");
   if(bit(seen,p))break;
   mark(seen,p); w=take(l,&p); k=w&31;
#if TR64_HAS_TR2
   if(tr2) {
    k=w&TR64_TR2_FLOOR_KIND_MASK;
    if(k>TR64_TR2_FLOOR_MAX_FUNCTION)bad(TR64_CORE_floor_data,"unknown TR2 floor command");
   }
#endif
   if(
#if TR64_HAS_TR2
    tr2 ? (TR64_TR2_FLOOR_PAYLOAD_MASK&(1u<<k))!=0 :
#endif
    (k==1 || k==2 || k==3)) {
    arg=take(l,&p); if(k==1 && arg>=COUNT(l,rooms))bad(TR64_CORE_floor_data,"portal room out of range");
   } else if(k==4) {
    trigger=(w>>8)&127;
#if TR64_HAS_TR2
    if(trigger>(tr2 ? TR64_TR2_FLOOR_MAX_TRIGGER : 8u))bad(TR64_CORE_floor_data,"unknown trigger type");
#else
    if(trigger>8)bad(TR64_CORE_floor_data,"unknown trigger type");
#endif
    (void)take(l,&p);
#if TR64_HAS_TR2
    if(tr2) {
     uint32_t start=p,controller=0;
     if(TR64_TR2_FLOOR_CONTROLLER_MASK&(1u<<trigger)) {
      controller=take(l,&p);
      if((controller&1023)>=COUNT(l,items))bad(TR64_CORE_floor_data,"trigger controller item out of range");
     }
     if(!(controller&32768))p=actions(l,p,ends,1);
     tr2_renewal(l,start,seen+bytes);
    } else
#endif
    {
    if(trigger==2 || trigger==3 || trigger==4) {
     arg=take(l,&p)&1023; if(arg>=COUNT(l,items))bad(TR64_CORE_floor_data,"trigger item out of range");
    }
     p=actions(l,p,ends
#if TR64_HAS_TR2
      ,0
#endif
     );
    }
   } else if(k!=0 && k!=5
#if TR64_HAS_TR2
    && (!tr2 || k!=6)
#endif
   )bad(TR64_CORE_floor_data,"unknown floor command");
   if(w&32768)break;
  }
 }
 tr64_mem_reset(TR64_MEM_io);
}
void load_validate_core(tr64_level_t *l)
{
 uint32_t i,j,rooms=COUNT(l,rooms),boxes=COUNT(l,boxes),frames=COUNT(l,frames),objects=tr64_be32(l->textures+12),sprites=tr64_be32(l->textures+16);
#if TR64_HAS_TR2 || TR64_GAME == 3
 unsigned tr2=l->game==2 || l->game==3,types_limit=TR64_LIMIT(type_ids,tr2),ids_limit=TR64_LIMIT(static_ids,tr2);
#else
 unsigned tr2=l->game==2,types_limit=TR64_LIMIT(type_ids,tr2),ids_limit=TR64_LIMIT(static_ids,tr2);
#endif
 unsigned sounds=TR64_LIMIT(sound_map,tr2),sprite_end=0;
#define CAP(k) if(COUNT(l,k)>TR64_LIMIT(k,tr2))bad(TR64_CORE_##k,"consumer count limit")
 CAP(rooms); CAP(items); CAP(models); CAP(mesh_pointers); CAP(static_meshes); CAP(cameras); CAP(boxes);
 CAP(animations); CAP(sprite_sequences); CAP(sound_sources); CAP(cinematic_frames);
#undef CAP
 if(COUNT(l,mesh_spheres)!=COUNT(l,mesh_pointers))
  bad(TR64_CORE_mesh_spheres,"count differs from mesh_pointers");
 for(i=0;i<rooms;++i) {
  const tr64_core_rooms_t *r=tr64_level_rooms(l,i); uint32_t x=tr64_u16(r->x_sectors),z=tr64_u16(r->z_sectors); int32_t alt=tr64_i16(r->alternate_room);
  if(!tr2 && (tr64_u16(r->ambient2) || tr64_u16(r->light_mode)))
   bad(TR64_CORE_rooms,"TR1 reserved lighting fields must be zero");
  if(!x || !z || x>TR64_LIMIT(room_grid,tr2) || z>TR64_LIMIT(room_grid,tr2) ||
     tr64_u32(r->sector_count)!=x*z)bad(TR64_CORE_rooms,"sector grid dimensions/count");
  unsigned subtables=TR64_LIMIT(room_subtables,tr2);
  if(tr64_u32(r->portal_count)>subtables || tr64_u32(r->light_count)>subtables ||
     tr64_u32(r->instance_count)>subtables)bad(TR64_CORE_rooms,"consumer subtable count limit");
  if(tr64_u32(r->sprite_count)>TR64_LIMIT(room_sprites,tr2))bad(TR64_CORE_room_sprites,"consumer count limit");
  span(l,TR64_CORE_sectors,tr64_u32(r->sector_start),x*z);
  span(l,TR64_CORE_portals,tr64_u32(r->portal_start),tr64_u32(r->portal_count));
  span(l,TR64_CORE_lights,tr64_u32(r->light_start),tr64_u32(r->light_count));
  span(l,TR64_CORE_static_instances,tr64_u32(r->instance_start),tr64_u32(r->instance_count));
  if(tr64_u32(r->sprite_start)!=sprite_end)bad(TR64_CORE_room_sprites,"room spans do not form a partition");
  span(l,TR64_CORE_room_sprites,tr64_u32(r->sprite_start),tr64_u32(r->sprite_count));
  sprite_end+=tr64_u32(r->sprite_count);
  if(alt!=-1) {
   const tr64_core_rooms_t *a;
   if(alt<0 || (uint32_t)alt>=rooms || (uint32_t)alt==i)bad(TR64_CORE_rooms,"alternate room topology");
   a=tr64_level_rooms(l,(uint32_t)alt);
   if(tr64_i16(a->alternate_room)!=-1 || tr64_i32(a->x)!=tr64_i32(r->x) || tr64_i32(a->z)!=tr64_i32(r->z) || tr64_u16(a->x_sectors)!=x || tr64_u16(a->z_sectors)!=z)bad(TR64_CORE_rooms,"alternate room topology");
   for(j=0;j<i;++j)if(tr64_i16(tr64_level_rooms(l,j)->alternate_room)==alt)bad(TR64_CORE_rooms,"alternate room used twice");
  }
 }
 if(sprite_end!=COUNT(l,room_sprites))bad(TR64_CORE_room_sprites,"room spans leave unclaimed records");
 for(i=0;i<COUNT(l,sectors);++i) {
  const tr64_core_sectors_t *s=tr64_level_sectors(l,i); uint32_t f=tr64_u16(s->floor_index),b=tr64_u16(s->box_index);
  if(f && f>=COUNT(l,floor_data))bad(TR64_CORE_floor_data,"sector floor offset out of range");
  if((b!=65535 && b>=boxes) || (s->room_above!=255 && s->room_above>=rooms) || (s->room_below!=255 && s->room_below>=rooms))bad(TR64_CORE_sectors,"referenced table out of range");
 }
 for(i=0;i<COUNT(l,portals);++i)if(tr64_u16(tr64_level_portals(l,i)->room)>=rooms)bad(TR64_CORE_portals,"room out of range");
 if(COUNT(l,zones)!=boxes*TR64_LIMIT(zone_arrays,tr2))bad(TR64_CORE_zones,"zone array count differs from game version");
 if(COUNT(l,bones)%4 || COUNT(l,node_flags)!=COUNT(l,bones)/4)bad(TR64_CORE_bones,"partial node");
 for(i=0;i<COUNT(l,animations);++i) {
  const tr64_core_animations_t *a=tr64_level_animations(l,i); uint32_t off=tr64_u32(a->frame_offset),first=tr64_u16(a->frame_start),last=tr64_u16(a->frame_end),words;
  if(tr64_u16(a->state)>TR64_LIMIT(states,tr2))bad(TR64_CORE_animations,"state id outside consumer domain");
  if(!a->frame_rate || last<first || (off&1) || off/2>frames)bad(TR64_CORE_animations,"frame offset/count out of range");
  words=(tr2 || a->frame_size) ? ((last-first)/a->frame_rate+1u)*a->frame_size : 10u;
  if((tr2 && !a->frame_size) || words>frames-off/2)bad(TR64_CORE_animations,"frame offset/count out of range");
  if(tr2 ? tr64_u16(a->next_animation)>=COUNT(l,animations)
         : !next_frame(l,tr64_u16(a->next_animation),tr64_u16(a->next_frame)))
   bad(TR64_CORE_animations,"next animation/frame out of range");
  span(l,TR64_CORE_states,tr64_u16(a->state_offset),tr64_u16(a->state_count));
 }
 for(i=0;i<COUNT(l,states);++i) {
  const tr64_core_states_t *s=tr64_level_states(l,i);
  if(tr64_u16(s->state)>TR64_LIMIT(states,tr2) || tr64_u16(s->range_count)>TR64_LIMIT(state_ranges,tr2))
   bad(TR64_CORE_states,"state/range count outside consumer domain");
  span(l,TR64_CORE_ranges,tr64_u16(s->range_offset),tr64_u16(s->range_count));
 }
 for(i=0;i<COUNT(l,ranges);++i) {
  const tr64_core_ranges_t *r=tr64_level_ranges(l,i);
  unsigned target=tr64_u16(r->next_animation);
  if(tr64_u16(r->low)>tr64_u16(r->high) ||
     (tr2 ? target>=COUNT(l,animations) : (!terminal_range(l,i) && !next_frame(l,target,tr64_u16(r->next_frame)))))
   bad(TR64_CORE_ranges,"target animation/frame out of range");
 }
 uint32_t ids_bytes=((types_limit>ids_limit?types_limit:ids_limit)+7)/8;
 uint8_t *ids=scratch(TR64_CORE_models,ids_bytes);
 memset(ids,0,ids_bytes);
 for(i=0;i<COUNT(l,models);++i) {
  const tr64_core_models_t *m=tr64_level_models(l,i); uint32_t type=tr64_u32(m->type),n=tr64_u16(m->mesh_count),off=tr64_u32(m->bone_offset),a=tr64_u16(m->animation),f=tr64_u32(m->frame_offset);
  if(type>=types_limit || bit(ids,type) || n>TR64_LIMIT(model_meshes,tr2))bad(TR64_CORE_models,"type/mesh count outside consumer domain");
  mark(ids,type); span(l,TR64_CORE_mesh_pointers,tr64_u16(m->mesh_start),n);
  if(off%4)bad(TR64_CORE_models,"unaligned bone offset");
  span(l,TR64_CORE_node_flags,off/4,n ? n-1 : 0);
  if(a!=65535 && a>=COUNT(l,animations))bad(TR64_CORE_models,"animation out of range");
  if((f&1) || f/2>frames)bad(TR64_CORE_models,"frame offset out of range");
 }
 memset(ids,0,ids_bytes);
 for(i=0;i<COUNT(l,static_meshes);++i) {
  const tr64_core_static_meshes_t *s=tr64_level_static_meshes(l,i); uint32_t id=tr64_u32(s->id);
  if(id>=ids_limit || bit(ids,id))bad(TR64_CORE_static_meshes,"static id outside consumer domain");
  mark(ids,id);
  if(tr64_u16(s->mesh)>=COUNT(l,mesh_pointers))bad(TR64_CORE_static_meshes,"mesh offset out of range");
 }
 for(i=0;i<COUNT(l,static_instances);++i) {
  uint32_t id=tr64_u16(tr64_level_static_instances(l,i)->mesh);
  if(id>=ids_limit || !bit(ids,id))bad(TR64_CORE_static_instances,"undefined static mesh id");
 }
 tr64_mem_reset(TR64_MEM_io);
 for(i=0;i<COUNT(l,items);++i) {
  const tr64_core_items_t *s=tr64_level_items(l,i);
  if(!tr2 && tr64_i16(s->intensity2))bad(TR64_CORE_items,"TR1 reserved intensity must be zero");
  if(tr64_u16(s->type)>=types_limit)bad(TR64_CORE_items,"type outside consumer domain");
  if(tr64_u16(s->room)>=rooms)bad(TR64_CORE_items,"room out of range");
 }
 for(i=0;!tr2 && i<COUNT(l,cameras);++i) {
  int32_t room=tr64_i16(tr64_level_cameras(l,i)->room_or_speed);
  if(room<0 || (uint32_t)room>=rooms)bad(TR64_CORE_cameras,"room out of range");
 }
 for(i=0;i<COUNT(l,sprite_sequences);++i) {
  const tr64_core_sprite_sequences_t *s=tr64_level_sprite_sequences(l,i); int32_t count=tr64_i16(s->count); uint32_t type=tr64_u32(s->type),first=tr64_u16(s->first);
  if(type>65535 || count>=0 || (!tr2 && type<types_limit && count<-128) || first<objects ||
     first>objects+sprites || (uint32_t)-count>objects+sprites-first)bad(TR64_CORE_sprite_sequences,"native type/count/descriptor range");
 }
 for(i=0;i<COUNT(l,room_sprites);++i) {
  const tr64_core_room_sprites_t *s=tr64_level_room_sprites(l,i);
  uint32_t descriptor=tr64_u16(s->descriptor);
  if(tr64_u16(s->gain)>8192 || tr64_u16(s->reserved))
   bad(TR64_CORE_room_sprites,"gain/reserved outside native domain");
  if(descriptor<objects || descriptor>=objects+sprites)
   bad(TR64_CORE_room_sprites,"native sprite descriptor out of range");
 }
 for(i=0;i<boxes;++i) {
  const tr64_core_boxes_t *b=tr64_level_boxes(l,i);
  int32_t x=tr64_i32(b->min_x),xx=tr64_i32(b->max_x),z=tr64_i32(b->min_z),zz=tr64_i32(b->max_z);
  if(x<0 || z<0 || xx<x || zz<z || xx>=255*1024 || zz>=255*1024)bad(TR64_CORE_boxes,"grid coordinates outside byte domain");
 }
 if(COUNT(l,sound_map)!=sounds)bad(TR64_CORE_sound_map,"count differs from game version");
 for(i=0;i<sounds;++i) { int32_t n=tr64_i16(*tr64_level_sound_map(l,i)); if(n!=-1 && (n<0 || (uint32_t)n>=COUNT(l,sound_details)))bad(TR64_CORE_sound_map,"detail index out of range"); }
 for(i=0;i<COUNT(l,sound_sources);++i)if(tr64_u16(tr64_level_sound_sources(l,i)->sound)>=sounds)bad(TR64_CORE_sound_sources,"sound-map index out of range");
 commands(l); overlaps(l);
 /* TR2 floor programs are opaque for a posed scene. The extra native
  * gameplay validation is requested explicitly under the load lease. */
 if(!tr2)floors(l
#if TR64_HAS_TR2
  ,0
#endif
 );
}

#if TR64_HAS_TR2
typedef struct { int64_t lo,hi; } Axis;
typedef struct { Axis x,z; } Footprint;
enum { PORTAL, ABOVE, BELOW };
enum { CLAMP_ALL, CLAMP_TR2, DIRECT };
static void room_walk(const tr64_level_t *,unsigned,Footprint,unsigned,unsigned,uint8_t *);

static int64_t max64(int64_t a,int64_t b) { return a>b?a:b; }
static int64_t min64(int64_t a,int64_t b) { return a<b?a:b; }

/* A sector owns [origin + tile*1024, origin + (tile+1)*1024 - 1].
 * Clamped edge bands extend outward over the caller's whole footprint. */
static Axis band(Axis domain,int64_t origin,int32_t tile,int32_t first,int32_t last,int clamp)
{
 if(!clamp || tile>first)domain.lo=max64(domain.lo,origin+tile*1024);
 if(!clamp || tile<last)domain.hi=min64(domain.hi,origin+(tile+1)*1024-1);
 return domain;
}
static int32_t tile_at(int64_t coordinate,int64_t origin,int32_t first,int32_t last)
{
 int64_t tile=(coordinate-origin)>>10;
 return (int32_t)max64(first,min64(last,tile));
}

/* The gameplay resolver recognizes a portal after the optional leading
 * floor and ceiling slopes. The structural validation has run first. */
static unsigned horizontal_room(const tr64_level_t *l,const tr64_core_sectors_t *sector)
{
 uint32_t p=tr64_u16(sector->floor_index);
 if(!p)return 255;
 unsigned cmd=take(l,&p);
 for(unsigned shape=2;shape<=3;++shape) {
  if((cmd&TR64_TR2_FLOOR_KIND_MASK)!=shape)continue;
  if(cmd&0x8000)return 255;
  (void)take(l,&p);
  cmd=take(l,&p);
 }
 return (cmd&TR64_TR2_FLOOR_KIND_MASK)==1 ? take(l,&p) : 255;
}

/* Walk symbolically at one fixed world X/Z: each recursive edge intersects
 * its sector's coordinate band with all preceding bands. Room-only graph
 * cycles are legal if their sectors cannot be visited at the SAME position.
 * A repeated room with a nonempty intersection necessarily repeats a sector.
 * Stack depth is bounded by the byte-sized room namespace. */
static void room_walk_band(const tr64_level_t *l,unsigned room,const tr64_core_rooms_t *r,
                           int64_t ox,int64_t oz,Footprint area,unsigned direction,
                           unsigned mapping,uint8_t *on_path)
{
 static const char *const cycle[]={
  "horizontal room cycle","room-above cycle","room-below cycle"
 };
 int32_t nx=tr64_u16(r->x_sectors),nz=tr64_u16(r->z_sectors);
 int clamp=mapping!=DIRECT;
 if(!clamp) {
  area.x.lo=max64(area.x.lo,ox); area.x.hi=min64(area.x.hi,ox+nx*1024-1);
  area.z.lo=max64(area.z.lo,oz); area.z.hi=min64(area.z.hi,oz+nz*1024-1);
  if(area.x.lo>area.x.hi || area.z.lo>area.z.hi)return;
 }
 if(bit(on_path,room))bad(TR64_CORE_floor_data,cycle[direction]);
 mark(on_path,room);
 uint32_t start=tr64_u32(r->sector_start);
 int32_t last_z=tile_at(area.z.hi,oz,0,nz-1);
 for(int32_t z=tile_at(area.z.lo,oz,0,nz-1);z<=last_z;++z) {
  int32_t first_x=0,last_x=nx-1;
  if(mapping==CLAMP_TR2 && nx>=3 && (z==0 || z==nz-1)) {
   first_x=1; last_x=nx-2;
  }
  Axis z_band=band(area.z,oz,z,0,nz-1,clamp);
  int32_t end_x=tile_at(area.x.hi,ox,first_x,last_x);
  for(int32_t x=tile_at(area.x.lo,ox,first_x,last_x);x<=end_x;++x) {
   const tr64_core_sectors_t *s=tr64_level_sectors(l,start+(uint32_t)x*nz+z);
   unsigned next=direction==PORTAL ? horizontal_room(l,s)
                : direction==ABOVE ? s->room_above : s->room_below;
   if(next!=255) {
    Footprint narrowed={band(area.x,ox,x,first_x,last_x,clamp),z_band};
    room_walk(l,next,narrowed,direction,mapping,on_path);
   }
  }
 }
 on_path[room>>3]&=(uint8_t)~(1u<<(room&7));
}

/* i32_sub(world, origin) is a wrapping subtraction. Split X/Z at its
 * signed boundaries, then use an equivalent shifted origin in each band. */
static void room_walk(const tr64_level_t *l,unsigned room,Footprint area,
                      unsigned direction,unsigned mapping,uint8_t *on_path)
{
 const tr64_core_rooms_t *r=tr64_level_rooms(l,room);
 const int64_t period=INT64_C(1)<<32;
 const int64_t origin_x=tr64_i32(r->x),origin_z=tr64_i32(r->z);
 for(int64_t oz=origin_z-period;oz<=origin_z+period;oz+=period) {
  Axis z={max64(area.z.lo,oz+INT32_MIN),min64(area.z.hi,oz+INT32_MAX)};
  if(z.lo>z.hi)continue;
  for(int64_t ox=origin_x-period;ox<=origin_x+period;ox+=period) {
   Axis x={max64(area.x.lo,ox+INT32_MIN),min64(area.x.hi,ox+INT32_MAX)};
   if(x.lo<=x.hi)room_walk_band(l,room,r,ox,oz,(Footprint){x,z},direction,mapping,on_path);
  }
 }
}

static void room_walks(const tr64_level_t *l)
{
 const Footprint world={{INT32_MIN,INT32_MAX},{INT32_MIN,INT32_MAX}};
 uint8_t on_path[(TR64_LIMIT_rooms_2+7)/8]={0};
 for(unsigned room=0;room<COUNT(l,rooms);++room)
  for(unsigned map=CLAMP_ALL;map<=DIRECT;++map)
   for(unsigned direction=map==DIRECT?ABOVE:PORTAL;direction<=BELOW;++direction)
    room_walk(l,room,world,direction,map,on_path);
}

/* Called inside the load unit after the native loader releases I/O scratch. */
void load_validate_tr2_gameplay(const tr64_level_t *l)
{
 if(!l || !l->ready || l->game!=2)bad(TR64_CORE_floor_data,"TR2 gameplay validation before level admission");
 for(uint32_t i=0;i<COUNT(l,models);++i) {
  uint32_t type=tr64_u32(tr64_level_models(l,i)->type);
  if(type>=TR2_PUBLIC_TYPES)
   sys_error("core models: unsupported Tomb Raider II gameplay model type %lu",(unsigned long)type);
 }
 floors(l,1);
 room_walks(l);
}
#endif
