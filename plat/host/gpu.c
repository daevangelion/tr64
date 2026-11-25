/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include "plat/host/gpu-submit.h"
#include "tr64/mem.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"

extern void tr64_gpu_host_begin_frame(const tr64_gpu_host_state_t *) __attribute__((weak));
extern void tr64_gpu_host_emit_triangle(const tr64_gpu_host_state_t *,const tr64_gpu_host_triangle_t *) __attribute__((weak));
extern void tr64_gpu_host_end_frame(const tr64_gpu_host_state_t *) __attribute__((weak));
extern void tr64_gpu_host_clear_depth(void) __attribute__((weak));
extern int tr64_gpu_host_capture_sink(const char *,const char *) __attribute__((weak));
extern void tr64_gpu_host_log_fill(void) __attribute__((weak));
extern void tr64_ui_gpu_draw(void) __attribute__((weak));

enum {
 /* Cart resource model: rspq/rdpq buffers, two matrix allocations, workspace,
  * nine16-byte allocation headers; SP/DP callback_link is two32-bit pointers,
  * each charged round_up(8+16,16)=32 by the scoped allocator. Sources: pinned
  * rspq.c, t3d.c and interrupt.c, not a fitted frame/picture constant. */
 QUEUE_BYTES=4*2048, RDP_BYTES=2*65536, MATRIX_BYTES=2*64,
 INSTANCE_BYTES=TR64_GPU_MATRIX_LIMIT*64,
 VERTEX_BYTES=TR64_GPU_VERTEX_SLOTS*TR64_GPU_VERTEX_LIMIT*16,
 GPU_BOOT_BYTES=QUEUE_BYTES+RDP_BYTES+2*MATRIX_BYTES+INSTANCE_BYTES+
                VERTEX_BYTES+TR64_GPU_RDP_RECORD_BYTES+9*16+2*32
};
typedef char matrix_storage_fits[(sizeof(tr64_gpu_transform_t)<=64)?1:-1];
static struct {
 tr64_gpu_host_state_t state;
 tr64_gpu_counts_t count;
 tr64_gpu_split_counts_t split;
 tr64_gpu_transform_t *matrices;
 uint8_t *vertices;
 uint32_t matrix_bits[8],texture_image[4],palette_ids[16],palette_slot,flat,rdp_mode,matrix_id;
 uint32_t start,record_used;
 int initialized,in_frame,material_set,texture_valid,scissor_valid,menu_pass;
 tr64_gpu_light_t lights[TR64_GPU_MAX_LIGHTS];
 uint8_t light_count,camera_water,bilinear;
} gpu;
static uint16_t u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static int16_t s16(const uint8_t *p) { unsigned v=u16(p);return (int16_t)((int32_t)(v&32767u)-(int32_t)(v&32768u)); }
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int span_ok(uint32_t bytes,uint32_t offset,uint32_t count,uint32_t stride)
{ return stride && offset<=bytes && count<=(bytes-offset)/stride; }
static void require_frame(void)
{ if(!gpu.in_frame)sys_error("host gpu submission outside frame"); }
static void records(uint32_t bytes)
{
 if(bytes>TR64_GPU_RDP_RECORD_BYTES-gpu.record_used) {
  if(!gpu.split.split || bytes>TR64_GPU_RDP_RECORD_BYTES) {
   ++gpu.count.rdp_record_overflows;
   sys_error("host gpu records overflow used=%lu request=%lu overflows=%lu",
    (unsigned long)gpu.record_used,(unsigned long)bytes,
    (unsigned long)gpu.count.rdp_record_overflows);
  }
  ++gpu.split.rdp_record_spills;gpu.record_used=0; /* Host consumes records synchronously. */
 }
 gpu.count.rdp_record_bytes+=bytes;gpu.record_used+=bytes;
 if(gpu.record_used>gpu.count.rdp_record_peak)gpu.count.rdp_record_peak=gpu.record_used;
}
void tr64_gpu_init(void)
{
 uint8_t *model;
 if(gpu.initialized)return;
 if(!mem_is_booted())sys_error("host gpu initialized before carve");
 model=tr64_mem_alloc(TR64_MEM_gpu,GPU_BOOT_BYTES);
 gpu.matrices=(tr64_gpu_transform_t *)(model+QUEUE_BYTES+RDP_BYTES+2*MATRIX_BYTES);
 gpu.vertices=model+QUEUE_BYTES+RDP_BYTES+2*MATRIX_BYTES+INSTANCE_BYTES;
 gpu.initialized=1;
 log_printf("host-gpu: submission backend gpu_model_bytes=%u rasterizer=%s\n",
            GPU_BOOT_BYTES,tr64_gpu_host_emit_triangle?"installed":"not-installed");
}
void tr64_gpu_bind_textures(tr64_gpu_span_t tx)
{
 if(!gpu.initialized || gpu.in_frame)sys_error("host gpu texture bind lifetime");
 if(!tx.data && !tx.bytes) {
  gpu.state.textures=tx;gpu.state.vertices=NULL;gpu.material_set=0;return;
 }
 const uint8_t *p=tx.data;
 if(!p || tx.bytes<80 || memcmp(p,"TX64",4) ||
    u32(p+4)!=1 || u32(p+8)!=tx.bytes ||
    !span_ok(tx.bytes,u32(p+40),u32(p+20),16) ||
    !span_ok(tx.bytes,u32(p+44),u32(p+24),8) ||
    !span_ok(tx.bytes,u32(p+48),u32(p+12),24) ||
    !span_ok(tx.bytes,u32(p+56),u32(p+28),2) ||
    !span_ok(tx.bytes,u32(p+60),u32(p+32),8))sys_error("host gpu invalid TX64 section");
 gpu.state.textures=tx;
}
static void begin_view(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 gpu.state.view=*view;gpu.state.projection=*projection;
 gpu.state.scissor=tr64_gpu_projection_rect(projection);
 gpu.state.vertices=NULL;gpu.state.vertex_count=0;gpu.state.batch_id=UINT32_MAX;
 gpu.palette_slot=gpu.matrix_id=UINT32_MAX;gpu.texture_valid=gpu.scissor_valid=0;
 memset(gpu.palette_ids,0xff,sizeof(gpu.palette_ids));
 gpu.flat=0;gpu.rdp_mode=0;gpu.material_set=0;gpu.light_count=0;gpu.camera_water=0;
 records(56);++gpu.count.state_changes; /* Same raw mode/alpha/combiner setup as cart. */
}
void tr64_gpu_frame_begin(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 if(!gpu.initialized || !gpu.state.textures.data || gpu.in_frame || !view || !tr64_gpu_projection_valid(projection))
  sys_error("host gpu invalid frame setup");
 gpu.start=sys_time_us();memset(&gpu.count,0,sizeof(gpu.count));memset(gpu.matrix_bits,0,sizeof(gpu.matrix_bits));
 gpu.record_used=0;gpu.in_frame=1;gpu.menu_pass=0;gpu.split.split=0;
 begin_view(view,projection);
 if(tr64_gpu_host_begin_frame)tr64_gpu_host_begin_frame(&gpu.state);
}
void tr64_gpu_split_prepare(void)
{
 require_frame();if(gpu.split.split)sys_error("host gpu invalid split preparation");
 gpu.split=(tr64_gpu_split_counts_t){.split=2};
}
static void split_recycle(void)
{
 /* The host consumes all world records and matrices synchronously. */
 gpu.record_used=0;++gpu.split.rdp_record_spills;
 memset(gpu.matrix_bits,0,sizeof(gpu.matrix_bits));
}
void tr64_gpu_view_begin(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 require_frame();
 if(gpu.menu_pass || gpu.split.split!=2 || !view || !tr64_gpu_projection_valid(projection))
  sys_error("host gpu invalid second view");
 gpu.split.split=1;
 gpu.split.first_triangles=gpu.count.triangles;
 gpu.split.first_texture_uploads=gpu.count.texture_uploads;
 split_recycle();begin_view(view,projection);
}
void tr64_gpu_split_matrices(unsigned used)
{
 if(used>gpu.split.matrix_peak)gpu.split.matrix_peak=(uint16_t)used;
}
void tr64_gpu_split_overlay(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 require_frame();
 if(gpu.split.split!=1 || !view || !tr64_gpu_projection_valid(projection) || !tr64_gpu_host_clear_depth)
  sys_error("host gpu invalid split overlay");
 split_recycle();
 if(!gpu.menu_pass) { gpu.menu_pass=1;tr64_gpu_host_clear_depth(); }
 begin_view(view,projection);
}
void tr64_gpu_menu_begin(const tr64_gpu_view_t *view,int clear_depth)
{
 require_frame();
 if(gpu.menu_pass || !view || (clear_depth!=0 && clear_depth!=1))
  sys_error("host gpu invalid menu pass");
 if(clear_depth && !tr64_gpu_host_clear_depth)sys_error("host menu depth clear unavailable");
 static const tr64_gpu_projection_t full=TR64_GPU_WORLD_PROJECTION;
 gpu.menu_pass=1;begin_view(view,&full);
 if(clear_depth)tr64_gpu_host_clear_depth();
}
void tr64_gpu_scissor(tr64_gpu_rect_t r)
{
 require_frame();
 const int dx=gpu.state.projection.centre_x-TR64_GPU_WIDTH/2;
 const int dy=gpu.state.projection.centre_y-TR64_GPU_HEIGHT/2;
 r.left+=dx;r.right+=dx;r.top+=dy;r.bottom+=dy;
 if(r.left<0 || r.top<0 || r.right>320 || r.bottom>240 || r.left>=r.right || r.top>=r.bottom)
  sys_error("host gpu invalid scissor");
 if(gpu.scissor_valid && r.left==gpu.state.scissor.left && r.top==gpu.state.scissor.top &&
    r.right==gpu.state.scissor.right && r.bottom==gpu.state.scissor.bottom)return;
 gpu.state.scissor=r;gpu.scissor_valid=1;records(8);
}
static int same_matrix(const tr64_gpu_transform_t *a,const tr64_gpu_transform_t *b)
{
 return !memcmp(a->basis,b->basis,sizeof(a->basis)) &&
        !memcmp(a->translation,b->translation,sizeof(a->translation)) &&
        a->light_offset==b->light_offset && a->water==b->water && a->mesh==b->mesh &&
        a->view_space==b->view_space && a->logical_room==b->logical_room;
}
void tr64_gpu_matrix(const tr64_gpu_transform_t *m)
{
 require_frame();
 uint32_t start=sys_profile_ticks();
 if(!m || m->instance_id>=256 || m->water>1 || m->mesh>1 || m->view_space>1 || m->logical_room<0 || m->logical_room>65535)
  sys_error("host gpu invalid matrix");
 unsigned id=m->instance_id;
 if(gpu.matrix_bits[id/32]&(1u<<(id%32))) {
  if(!same_matrix(m,&gpu.matrices[id]))sys_error("host gpu changed live matrix");
 } else { gpu.matrices[id]=*m;gpu.matrix_bits[id/32]|=1u<<(id%32); }
 if(gpu.matrix_id==id)return;
 gpu.state.matrix=*m;gpu.matrix_id=id;gpu.state.batch_id=UINT32_MAX;
 gpu.state.vertices=NULL;gpu.state.vertex_count=0;++gpu.count.matrix_loads;
 gpu.count.matrix_us+=sys_profile_ticks()-start;
}
void tr64_gpu_material(tr64_gpu_material_t m)
{
 uint32_t start=sys_profile_ticks();require_frame();const uint8_t *tx=gpu.state.textures.data;
 unsigned flat=!!(m.flags&TR64_GPU_FLAT);
 if((m.flags&128) || (m.flags&3)==3)sys_error("host gpu invalid material flags");
 unsigned mode=(m.flags&TR64_GPU_ADDITIVE)?3u:
               ((m.flags&TR64_GPU_CLASS_MASK)==TR64_GPU_BLENDED_M1)?2u:
               (m.flags&TR64_GPU_NO_DEPTH)?1u:0u;
 if(mode!=gpu.rdp_mode) { records(mode>=2u?24u:16u);++gpu.count.state_changes;gpu.rdp_mode=mode; }
 if(flat!=gpu.flat) { records(16);++gpu.count.state_changes;gpu.flat=flat; }
 if(flat) {
  if(!(m.material==UINT16_MAX && m.content==UINT16_MAX && !m.tile) &&
     (m.material>=u32(tx+32) || m.content!=m.material || m.tile))
   sys_error("host gpu invalid flat material");
  records(16);
 } else {
  unsigned objects=u32(tx+12),sprites=u32(tx+16),sprite=m.content>=objects;
  if(m.material>=objects+sprites || m.content>=objects+sprites)sys_error("host gpu invalid descriptor");
  const uint8_t *d=tx+u32(tx+(sprite?52:48))+(m.content-(sprite?objects:0))*(sprite?22:24);
  unsigned cluster=u16(d),palette=u16(d+2),splits=u16(d+(sprite?12:22)),first=u16(d+(sprite?10:20));
  unsigned x=d[4],w=d[6]+1,h=d[7]+1,sy=0,ey=h;
  if(cluster>=u32(tx+20) || palette>=u32(tx+24) || m.tile>splits ||
     first>u32(tx+28) || splits>u32(tx+28)-first || (x&1))
   sys_error("host gpu invalid texture rectangle/tile");
  const uint8_t *lines=tx+u32(tx+56);
  if(m.tile)sy=u16(lines+2*(first+m.tile-1));
  if(m.tile<splits)ey=u16(lines+2*(first+m.tile));
  const uint8_t *cl=tx+u32(tx+40)+16*cluster,*pal=tx+u32(tx+44)+8*palette;
  unsigned row_bytes=(w+1)/2,pitch=(row_bytes+7)&~7u;
  if(ey<=sy || ey>h || (ey-sy)*pitch>2048 || x+w>u16(cl+4) || d[5]+h>u16(cl+6) ||
     !span_ok(gpu.state.textures.bytes,u32(cl),u16(cl+6),u16(cl+8)) ||
     !span_ok(gpu.state.textures.bytes,u32(pal),32,1))sys_error("host gpu texture payload/TMEM bounds");
  unsigned palette_slot=palette&15;
  if(palette!=gpu.palette_ids[palette_slot]) {
   records(48);++gpu.count.palette_loads;gpu.palette_ids[palette_slot]=palette;
  }
  uint32_t image=u32(cl),origin=(x<<16)|(d[5]+sy),size=(w<<16)|(ey-sy),row=u16(cl+8);
  if(!gpu.texture_valid || gpu.texture_image[0]!=image || gpu.texture_image[1]!=row ||
     gpu.texture_image[2]!=origin || gpu.texture_image[3]!=size) {
   records(64);++gpu.count.texture_uploads;gpu.count.texture_upload_bytes+=row_bytes*(ey-sy);
   gpu.texture_image[0]=image;gpu.texture_image[1]=row;
   gpu.texture_image[2]=origin;gpu.texture_image[3]=size;gpu.texture_valid=1;gpu.palette_slot=palette_slot;
  }
  else if(gpu.palette_slot!=palette_slot) { records(24);gpu.palette_slot=palette_slot; }
 }
 gpu.state.material=m;gpu.material_set=1;gpu.count.texture_setup_us+=sys_profile_ticks()-start;
}
static void emit(const uint8_t *face,const uint32_t st[4],unsigned a,unsigned b,unsigned c)
{
 unsigned indices[3]={face[a],face[b],face[c]},corners[3]={a,b,c};
 tr64_gpu_host_triangle_t triangle;
 for(unsigned i=0;i<3;++i) {
  if(indices[i]>=gpu.state.vertex_count)sys_error("host gpu face index out of range");
  triangle.indices[i]=(uint8_t)indices[i];triangle.words[i+2]=st[(face[4]>>(corners[i]*2))&3];
 }
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<i;++j)
  if(indices[i]==indices[j] && triangle.words[i+2]!=triangle.words[j+2])sys_error("host gpu repeated index with distinct ST");
 triangle.words[0]=0x10000000u|(0x408u+36*indices[0]);
 triangle.words[1]=(0x408u+36*indices[1])<<16|(0x408u+36*indices[2]);
 triangle.source_polygon=u16(face+6);triangle.source_corner_map=face[5];
 ++gpu.count.triangles;gpu.count.triangle_command_bytes+=20;
 if(tr64_gpu_host_emit_triangle)tr64_gpu_host_emit_triangle(&gpu.state,&triangle);
}
void tr64_gpu_set_lights(const tr64_gpu_light_t *lights,unsigned count)
{
 if(!lights || !count) { gpu.light_count=0;return; }
 if(count>TR64_GPU_MAX_LIGHTS)count=TR64_GPU_MAX_LIGHTS;
 memcpy(gpu.lights,lights,count*sizeof(*lights));
 gpu.light_count=(uint8_t)count;
}
void tr64_gpu_set_camera_water(int underwater)
{
 gpu.camera_water=underwater?1u:0u;
}
int tr64_gpu_host_camera_water(void)
{
 return (int)gpu.camera_water;
}
void tr64_gpu_set_filter(int bilinear)
{
 gpu.bilinear=bilinear?1u:0u;
}
int tr64_gpu_host_filter(void)
{
 return (int)gpu.bilinear;
}
typedef struct {
 int32_t lx,ly,lz,radius,rad2,peak;
 uint8_t shift;
} room_local_light_t;
static uint32_t light_room_rgba(uint32_t rgba,int32_t vx,int32_t vy,int32_t vz,
                                const room_local_light_t *ll,unsigned nl,unsigned water)
{
 int32_t best=0;
 for(unsigned i=0;i<nl;++i) {
  int32_t r=ll[i].radius;
  int32_t dx=vx-ll[i].lx;
  if(dx<-r || dx>r)continue;
  int32_t dy=vy-ll[i].ly;
  if(dy<-r || dy>r)continue;
  int32_t dz=vz-ll[i].lz;
  if(dz<-r || dz>r)continue;
  int32_t dist2=dx*dx+dy*dy+dz*dz;
  int32_t v;
  if(r==32) {
   v=(dist2<=1024)?ll[i].peak:0;
  } else {
   if(dist2>=ll[i].rad2)continue;
   v=ll[i].peak-(dist2>>ll[i].shift);
  }
  if(v>best)best=v;
 }
 if(best<=0)return rgba;
 unsigned dr,dg;
 if(water) {
  dr=((uint32_t)best*2731u)>>17;
  dg=((uint32_t)best*8739u)>>18;
 } else {
  dr=dg=((uint32_t)best*2731u)>>16;
 }
 unsigned cr=((rgba>>24)&0xffu)+dr;
 unsigned cg=((rgba>>16)&0xffu)+dg;
 unsigned cb=((rgba>>8)&0xffu)+dg;
 unsigned max_r=water?128u:255u,max_g=water?205u:255u;
 if(cr>max_r)cr=max_r;
 if(cg>max_g)cg=max_g;
 if(cb>max_g)cb=max_g;
 return (cr<<24)|(cg<<16)|(cb<<8)|(rgba&0xffu);
}
static uint32_t tint_camera_water_rgba(uint32_t rgba)
{
 unsigned cr=((rgba>>24)&0xffu)>>1;
 unsigned cg=(((rgba>>16)&0xffu)*205u)>>8;
 unsigned cb=(((rgba>>8)&0xffu)*205u)>>8;
 return (cr<<24)|(cg<<16)|(cb<<8)|(rgba&0xffu);
}
static void recolour_room_lit_verts(uint8_t *verts,unsigned vc)
{
 room_local_light_t ll[TR64_GPU_MAX_LIGHTS];
 unsigned nl=0;
 const int32_t tx=gpu.state.matrix.translation[0];
 const int32_t ty=gpu.state.matrix.translation[1];
 const int32_t tz=gpu.state.matrix.translation[2];
 for(unsigned i=0;i<gpu.light_count;++i) {
  const tr64_gpu_light_t *l=gpu.lights+i;
  if(!l->active || l->radius<=0 || l->radius>16384 || l->shift>30u)continue;
  int64_t lx=(int64_t)l->pos[0]-(int64_t)tx;
  int64_t ly=(int64_t)l->pos[1]-(int64_t)ty;
  int64_t lz=(int64_t)l->pos[2]-(int64_t)tz;
  int64_t lim=32768+(int64_t)l->radius;
  if(lx<-lim || lx>lim || ly<-lim || ly>lim || lz<-lim || lz>lim)continue;
  ll[nl++]=(room_local_light_t){(int32_t)lx,(int32_t)ly,(int32_t)lz,
   l->radius,l->radius*l->radius,(int32_t)l->peak,l->shift};
 }
 const unsigned water=gpu.state.matrix.water;
 const unsigned cam_tint=gpu.camera_water && !water;
 const unsigned eff_water=water|cam_tint;
 if(!nl && !cam_tint)return;
 for(unsigned idx=0;idx<vc;++idx) {
  uint8_t *pair=verts+(idx>>1)*32u;
  const uint8_t *p=pair+(idx&1u)*8u;
  uint8_t *c=pair+16u+(idx&1u)*4u;
  uint32_t rgba=u32(c);
  if(cam_tint)rgba=tint_camera_water_rgba(rgba);
  if(nl)rgba=light_room_rgba(rgba,s16(p),s16(p+2),s16(p+4),ll,nl,eff_water);
  c[0]=(uint8_t)(rgba>>24);
  c[1]=(uint8_t)(rgba>>16);
  c[2]=(uint8_t)(rgba>>8);
  c[3]=(uint8_t)rgba;
 }
}
void tr64_gpu_batch(const tr64_gpu_batch_t *b)
{
 require_frame();
 if(!b || !b->vertices || !b->faces || !b->st || !b->vertex_count || b->vertex_count>70 ||
    !b->face_count || gpu.matrix_id==UINT32_MAX || !gpu.material_set)sys_error("host gpu invalid batch");
 if(gpu.state.batch_id!=b->batch_id) {
  uint32_t start=sys_profile_ticks();
  unsigned bytes=((b->vertex_count+1)/2)*32;
  memcpy(gpu.vertices,b->vertices,bytes);
  if(!gpu.state.matrix.mesh && (b->batch_id&TR64_GPU_BATCH_LIT)) {
   uint32_t cstart=sys_profile_ticks();
   recolour_room_lit_verts(gpu.vertices,b->vertex_count);
   gpu.count.vertex_colour_us+=sys_profile_ticks()-cstart;
  }
  gpu.state.vertices=gpu.vertices;
  gpu.state.vertex_count=b->vertex_count;gpu.state.batch_id=b->batch_id;
  gpu.count.vertex_loads+=(b->vertex_count+1)&~1u;
  gpu.count.vertex_us+=sys_profile_ticks()-start;
 } else if(gpu.state.vertex_count!=b->vertex_count)sys_error("host gpu changed live batch size");
 uint32_t st[4];for(unsigned i=0;i<4;++i)st[i]=u32(b->st+4*i);
 uint32_t start=sys_profile_ticks();
 for(unsigned i=0;i<b->face_count;++i) {
  const uint8_t *f=b->faces+8*i;
  emit(f,st,0,1,2);
  if(f[3]!=255)emit(f,st,0,2,3);
 }
 gpu.count.expansion_us+=sys_profile_ticks()-start;gpu.count.polygons+=b->face_count;++gpu.count.batch_submissions;
}
void tr64_gpu_frame_end(tr64_gpu_counts_t *out)
{
 require_frame();if(!out)sys_error("host gpu null frame counters");
 if(gpu.split.split) {
  gpu.split.second_triangles=gpu.count.triangles-gpu.split.first_triangles;
  gpu.split.second_texture_uploads=gpu.count.texture_uploads-gpu.split.first_texture_uploads;
 }
 if(tr64_ui_gpu_draw)tr64_ui_gpu_draw();
 gpu.count.cpu_submit_us=sys_time_us()-gpu.start;
 if(tr64_gpu_host_end_frame)tr64_gpu_host_end_frame(&gpu.state);
 gpu.count.completed_at_us=sys_time_us();gpu.count.gpu_done_us=gpu.count.completed_at_us-gpu.start;
 if(tr64_gpu_host_log_fill)tr64_gpu_host_log_fill();
 gpu.count.expansion_us=sys_profile_us(gpu.count.expansion_us);
 gpu.count.texture_setup_us=sys_profile_us(gpu.count.texture_setup_us);
 gpu.count.matrix_us=sys_profile_us(gpu.count.matrix_us);
 gpu.count.vertex_us=sys_profile_us(gpu.count.vertex_us);
 gpu.count.vertex_colour_us=sys_profile_us(gpu.count.vertex_colour_us);
 *out=gpu.count;gpu.in_frame=0;
}
const tr64_gpu_split_counts_t *tr64_gpu_split_counts(void) { return &gpu.split; }
int tr64_gpu_host_capture(const char *stem,const char *metadata)
{
 if(gpu.in_frame || !stem || !metadata) { errno=EINVAL;return -1; }
 if(!tr64_gpu_host_capture_sink) { errno=ENOTSUP;return -1; }
 return tr64_gpu_host_capture_sink(stem,metadata);
}
