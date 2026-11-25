/* SPDX-License-Identifier: MIT */
#include <libdragon.h>
/* The pinned SDK header spells C23's assertion keyword; the adapter is GNU99. */
#define static_assert _Static_assert
#include <t3d/t3d.h>
#undef static_assert
#include <malloc.h>
#include <string.h>
#include "tr64/gpu.h"
#include "engine/ui/ui.h"
#include "tr64/mem.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "plat/n64/heap.h"
#include "plat/n64/gpu-platform.h"
#include "plat/n64/gpu-ci4.h"
#include "build_id.h"
extern void tr64_ui_gpu_draw(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));
/* The profiler overlay's switch (engine/ui/perf.c). */
extern uint8_t tr64_perf_state __attribute__((weak));

enum { GPU_SCRATCH_HALF_BYTES=4*1120, GPU_MODE_SKY_1CYC=0x80u };

static struct {
 tr64_gpu_span_t tex;
 const uint8_t *tex_obj,*tex_spr,*tex_lines,*tex_clusters,*tex_palettes,*tex_flats;
 uint32_t tex_objects,tex_total,last_mat_key;
 tr64_gpu_view_t view;
 tr64_gpu_transform_t transform;
 tr64_gpu_counts_t count;
 T3DViewport viewport;
 T3DMat4FP *matrices;
 uint8_t *scratch,*records;
 /* Two 4,480-byte vertex scratch halves: bump-allocated and fenced once per half. */
 rspq_syncpoint_t fences[2];
 uint8_t fenced[2];
 uint32_t matrix_bits[8],matrix_properties[256];
 int32_t matrix_light_offsets[256];
 surface_t *surface;
 uint32_t start,record_used,matrix_id,batch_id,scratch_used;
 uint32_t tex_img,tex_row,tex_origin,tex_size;
 uint32_t palette_ids[16],palette_slot,drawflags,flat;
 tr64_gpu_rect_t scissor;
 uint8_t scissor_valid,fog_enabled,mesh_alpha,scratch_half;
 uint8_t initialized,in_frame,menu_pass,tri_dirty,frame_pending,rdp_mode;
 uint16_t pointers[70];
} gpu;
/* Keep non-hot view routing and split accounting out of the linked GPU hot group. */
static struct {
 tr64_gpu_split_counts_t counts;
 int16_t scissor_dx,scissor_dy;
 uint8_t bilinear,far_extended,fog_custom;
 int32_t z_num,z_den,z_bias;
 int32_t fog_az,fog_bz,fog_offset,fog_mult;
} views;
__attribute__((noinline,cold)) void tr64_gpu_set_filter(int bilinear)
{
 views.bilinear=bilinear?1u:0u;
}
static struct {
 tr64_gpu_light_t items[TR64_GPU_MAX_LIGHTS];
 uint8_t count,camera_water;
} room_lights;
__attribute__((noinline,cold)) void tr64_gpu_set_lights(const tr64_gpu_light_t *lights,unsigned count)
{
 if(!lights || !count) { room_lights.count=0;return; }
 if(count>TR64_GPU_MAX_LIGHTS)count=TR64_GPU_MAX_LIGHTS;
 memcpy(room_lights.items,lights,count*sizeof(*lights));
 room_lights.count=(uint8_t)count;
}
__attribute__((noinline,cold)) void tr64_gpu_set_camera_water(int underwater)
{
 room_lights.camera_water=underwater?1u:0u;
}

static uint16_t be16(const uint8_t *p) { return *(const uint16_t *)p; }
static uint32_t be32(const uint8_t *p) { return *(const uint32_t *)p; }
static inline void dcache_create(void *ptr,uint32_t bytes)
{
 uint8_t *p=(uint8_t *)ptr;
 for(uint32_t i=0;i<bytes;i+=16)__asm__ volatile("cache 0x0d, 0(%0)"::"r"(p+i):"memory");
}
static inline void dcache_wb_inv(const void *ptr,uint32_t bytes)
{
 const uint8_t *p=(const uint8_t *)ptr;
 for(uint32_t i=0;i<bytes;i+=16)__asm__ volatile("cache 0x15, 0(%0)"::"r"(p+i):"memory");
}
static inline int64_t rounded_shift(int64_t n,uint64_t d,unsigned s)
{
 int64_t sign=n>>63;
 uint64_t a=(uint64_t)((n^sign)-sign);
 uint64_t half=(d<<s)>>1;
 uint64_t q=((a+half)/d)>>s;
 return (int64_t)(q^(uint64_t)sign)-sign;
}
static inline int64_t rounded_pow2(int64_t n,unsigned s)
{
 int64_t sign=n>>63;
 uint64_t a=(uint64_t)((n^sign)-sign);
 uint64_t q=(a+(UINT64_C(1)<<(s-1)))>>s;
 return (int64_t)(q^(uint64_t)sign)-sign;
}
static inline void tri_sync(void)
{
 if(gpu.tri_dirty) { t3d_tri_sync();gpu.tri_dirty=0; }
}
void tr64_gpu_sync(void)
{
 if(gpu.frame_pending) { rspq_wait();gpu.frame_pending=0; }
}
static uint32_t *record(uint32_t bytes);
static __attribute__((noinline,cold)) uint32_t drain_records(void)
{
 uint32_t start=sys_time_us();
 /* Flush Tiny3D triangles; rspq_wait fences the RDP, switches to the
  * dynamic buffer and waits for the RSP before we overwrite its records. */
 tri_sync();rspq_wait();
 gpu.record_used=0;gpu.fenced[0]=gpu.fenced[1]=0;gpu.scratch_used=0;gpu.scratch_half=0;
 return sys_time_us()-start;
}
static __attribute__((noinline,cold)) uint32_t *record_full(uint32_t bytes)
{
 uint32_t step=(bytes+15u)&~15u;
 if(!views.counts.split || step>TR64_GPU_RDP_RECORD_BYTES) {
  ++gpu.count.rdp_record_overflows;
  sys_error("gpu rdp records overflow used=%lu request=%lu overflows=%lu",
   (unsigned long)gpu.record_used,(unsigned long)bytes,
   (unsigned long)gpu.count.rdp_record_overflows);
 }
 views.counts.rdp_spill_us+=drain_records();
 ++views.counts.rdp_record_spills;
 return record(bytes);
}
static uint32_t *record(uint32_t bytes)
{
 uint32_t step=(bytes+15u)&~15u,*p;
 if(step>TR64_GPU_RDP_RECORD_BYTES-gpu.record_used)return record_full(bytes);
 p=(uint32_t *)(gpu.records+gpu.record_used); gpu.record_used+=step;
 dcache_create(p,step);
 gpu.count.rdp_record_bytes+=bytes;
 if(gpu.record_used>gpu.count.rdp_record_peak)gpu.count.rdp_record_peak=gpu.record_used;
 return p;
}
static void word(uint32_t **p,uint32_t a,uint32_t b)
{ *(*p)++=a; *(*p)++=b; }
static __attribute__((noinline)) void execute(uint32_t *begin,uint32_t *end)
{
 uint32_t bytes=(uint32_t)((uintptr_t)end-(uintptr_t)begin);
 dcache_wb_inv(begin,bytes);
 volatile uint32_t *cur=rspq_cur_pointer;
 if(gpu.tri_dirty) {
  *cur++=T3D_RSP_ID+((uint32_t)T3D_CMD_TRI_SYNC<<24);
  gpu.tri_dirty=0;
 }
 /* Equivalent to rdpq_exec(begin, bytes): begin is 16-byte aligned and bytes is a multiple of 8;
  * tr64 never records RSPQ blocks. <= 4 words <= RSPQ_MAX_SHORT_COMMAND_SIZE (16). */
 uint32_t pb=PhysicalAddr(begin),pe=PhysicalAddr(end);
 cur[1]=pb;cur[2]=pe;cur[0]=(0x09u<<24)|pe;
 cur+=3;rspq_cur_pointer=cur;
 if(__builtin_expect(cur>rspq_cur_sentinel,0))rspq_next_buffer();
}
static __attribute__((noinline,cold)) void combine(int flat)
{
 uint64_t c;
 if(gpu.rdp_mode&GPU_MODE_SKY_1CYC) {
  c=flat ? RDPQ_COMBINER1((0,0,0,ENV),(0,0,0,1))
         : RDPQ_COMBINER1((0,0,0,TEX0),(0,0,0,1));
 } else {
  c=flat ? RDPQ_COMBINER2((ENV,0,SHADE,0),(0,0,0,1),
     (COMBINED,0,PRIM,COMBINED),(0,0,0,SHADE)) :
    RDPQ_COMBINER2((TEX0,0,SHADE,0),(0,0,0,TEX0),
     (COMBINED,0,PRIM,COMBINED),(0,0,0,SHADE));
 }
 uint32_t *p=record(16),*begin=p;
 word(&p,0xe7000000,0);word(&p,0xfc000000|((uint32_t)(c>>32)&0xffffff),(uint32_t)c);
 execute(begin,p); gpu.flat=(uint32_t)flat; ++gpu.count.state_changes;
}
void tr64_gpu_init(void)
{
 uint8_t *workspace;
 if(gpu.initialized)return;
 sys_heap_boot_pool_begin(TR64_MEM_gpu);
 rdpq_init(); t3d_init((T3DInitParams){.matrixStackSize=2});
 /* Exact unlit RGBA: ADD with zero ambient is identity, unlike MUL32767.
  * Patch before the first frame; leave this overlay before reloading it. */
 t3d_state_set_lighting_mode(T3D_LIGHTING_MODE_ADD);
 rspq_wait();rdpq_sync_pipe();rspq_wait();
 gpu.viewport=t3d_viewport_create_buffered(1);
 workspace=memalign(16,256*64+8*1120+TR64_GPU_RDP_RECORD_BYTES);
 if(!workspace)sys_error("gpu workspace allocation");
 gpu.matrices=(T3DMat4FP *)workspace;gpu.scratch=workspace+256*64;
 gpu.records=gpu.scratch+8*1120;
 sys_heap_boot_pool_end();
 for(unsigned i=0;i<70;++i)gpu.pointers[i]=(uint16_t)(0x408+36*i);
 gpu.initialized=1;
}
void tr64_gpu_bind_textures(tr64_gpu_span_t tex)
{
 if(!gpu.initialized || gpu.in_frame)sys_error("gpu texture bind lifetime");
 tr64_gpu_sync();
 gpu.last_mat_key=UINT32_MAX;
 if(!tex.data && !tex.bytes) {
  rspq_wait();gpu.tex=tex;gpu.tex_objects=gpu.tex_total=0;
  gpu.tex_obj=gpu.tex_spr=gpu.tex_lines=gpu.tex_clusters=gpu.tex_palettes=gpu.tex_flats=NULL;
  return;
 }
 if(!tex.data || tex.bytes<80 || memcmp(tex.data,"TX64",4) || be32(tex.data+8)!=tex.bytes)
  sys_error("gpu invalid TX64 section");
 gpu.tex=tex;
 gpu.tex_objects=be32(tex.data+12);
 gpu.tex_total=gpu.tex_objects+be32(tex.data+16);
 gpu.tex_clusters=tex.data+be32(tex.data+40);
 gpu.tex_palettes=tex.data+be32(tex.data+44);
 gpu.tex_obj=tex.data+be32(tex.data+48);
 gpu.tex_spr=tex.data+be32(tex.data+52);
 gpu.tex_lines=tex.data+be32(tex.data+56);
 gpu.tex_flats=tex.data+be32(tex.data+60);
}
static inline uint64_t rdp_other_modes(unsigned mode_bits)
{
 /* Dry skybox (GPU_MODE_SKY_1CYC): 1-cycle direct texel/env fill (2x RDP
  * pixel fillrate), no Z test/write, no fog blend, no alpha-key compare. */
 if(mode_bits&GPU_MODE_SKY_1CYC) {
  return SOM_CYCLE_1|SOM_TEXTURE_PERSP|SOM_TLUT_RGBA16|
         (views.bilinear?SOM_SAMPLE_BILINEAR:SOM_SAMPLE_POINT)|
         SOM_TF0_RGB|SOM_TF1_RGB|SOM_RGBDITHER_NONE|SOM_ALPHADITHER_NONE|
         SOM_COVERAGE_DEST_ZAP;
 }
 /* C0 alpha still performs texture-key comparison. C1 alpha carries fog
  * visibility, so the final blender's alpha255 bypass preserves unity.
  * IN_ALPHA avoids the second-cycle SHADE_ALPHA next-pixel hardware bug.
  * Both TR64_GPU_ADDITIVE (FOG_ALPHA=0xB0) and TR64_GPU_BLENDED_M1 (0x80)
  * use non-wrapping FORCE_BL with Z_COMPARE ON / Z_WRITE OFF. */
 uint64_t blend,z;
 if(mode_bits&(TR64_GPU_ADDITIVE|TR64_GPU_BLENDED_M1)) {
  blend=RDPQ_BLENDER2((IN_RGB,ZERO,IN_RGB,INV_MUX_ALPHA),
                      (CYCLE1_RGB,FOG_ALPHA,MEMORY_RGB,INV_MUX_ALPHA));
  z=SOM_Z_COMPARE;
 } else {
  blend=RDPQ_BLENDER2((IN_RGB,ZERO,IN_RGB,INV_MUX_ALPHA),
                      (CYCLE1_RGB,IN_ALPHA,FOG_RGB,INV_MUX_ALPHA));
  z=(mode_bits&TR64_GPU_NO_DEPTH)?0:SOM_Z_WRITE|SOM_Z_COMPARE;
 }
 return SOM_CYCLE_2|SOM_TEXTURE_PERSP|SOM_TLUT_RGBA16|
        (views.bilinear?SOM_SAMPLE_BILINEAR:SOM_SAMPLE_POINT)|
        SOM_TF0_RGB|SOM_TF1_RGB|SOM_RGBDITHER_NONE|SOM_ALPHADITHER_NONE|
        SOM_BLENDING|SOM_COVERAGE_DEST_ZAP|SOM_ALPHACOMPARE_THRESHOLD|
        z|(blend&~SOMX_BLEND_2PASS);
}
static __attribute__((noinline,cold)) void set_rdp_mode(unsigned mode_bits)
{
 unsigned effective=mode_bits;
 if(mode_bits==TR64_GPU_NO_DEPTH && !gpu.transform.water && !room_lights.camera_water)
  effective|=GPU_MODE_SKY_1CYC;
 uint64_t modes=rdp_other_modes(effective);
 int blended=!!(effective&(TR64_GPU_ADDITIVE|TR64_GPU_BLENDED_M1));
 uint32_t *p=record(blended?24u:16u),*begin=p;
 word(&p,0xe7000000,0);word(&p,0xef000000|((uint32_t)(modes>>32)&0xffffff),(uint32_t)modes);
 if(blended)word(&p,0xf8000000,(effective&TR64_GPU_ADDITIVE)?0x000000b0u:0x00000080u);
 execute(begin,p);
 if((effective^gpu.rdp_mode)&GPU_MODE_SKY_1CYC)gpu.flat=UINT32_MAX;
 gpu.rdp_mode=(uint8_t)effective; ++gpu.count.state_changes;
}
static __attribute__((noinline,cold)) void configure_depth_fog(const tr64_gpu_projection_t *projection)
{
 if(projection->far_z==TR64_GPU_FAR && projection->fog_near_z==TR64_GPU_FOG_NEAR) {
  views.far_extended=0;
  views.fog_custom=0;
  t3d_fog_set_range(16726160.0f/131072.0f,16726160.0f/131072.0f+32.0f);
  return;
 }
 if(projection->far_z>TR64_GPU_FAR) {
  uint64_t f=projection->far_z,n=TR64_GPU_NEAR,span=f-n;
  views.far_extended=1;
  views.z_num=(int32_t)(f+n);
  views.z_den=(int32_t)(span*8u);
  views.z_bias=(int32_t)((2u*f*n*2048u+span/2u)/span);
  views.fog_az=(int32_t)((2048u*(f+n)+span/2u)/span);
  views.fog_bz=views.z_bias;
 } else {
  views.far_extended=0;
  views.fog_az=2074;
  views.fog_bz=263793;
 }
 int64_t c_near=(int64_t)views.fog_az*projection->fog_near_z-views.fog_bz;
 int64_t c_far=(int64_t)views.fog_az*projection->far_z-views.fog_bz;
 int64_t off_q16=(c_near*65535+32768)/65536;
 int64_t far_q16=(c_far*65535+32768)/65536;
 int64_t steps=(far_q16-off_q16)>>16;
 if(steps<2)steps=2;
 views.fog_offset=(int32_t)off_q16;
 views.fog_mult=(int32_t)((32767+steps-1)/steps);
 views.fog_custom=1;
 float start=(float)off_q16/131072.0f;
 float span_f=(float)steps*0.5f;
 t3d_fog_set_range(start,start+span_f);
}
static void begin_3d_pass(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 uint32_t *p,*begin;uint64_t modes;
 gpu.view=*view;gpu.matrix_id=gpu.batch_id=UINT32_MAX;
 gpu.palette_slot=gpu.flat=gpu.drawflags=gpu.last_mat_key=UINT32_MAX;
 gpu.tex_row=0;gpu.scissor_valid=0;
 gpu.rdp_mode=0;
 room_lights.count=0;
 room_lights.camera_water=0;
 memset(gpu.palette_ids,0xff,sizeof(gpu.palette_ids));
 t3d_mat4_identity(&gpu.viewport.matProj);t3d_mat4_identity(&gpu.viewport.matCamera);
 gpu.viewport._normScaleW=128.0f/65535.0f;gpu.viewport.guardBandScale=1;
 tr64_gpu_rect_t area=tr64_gpu_projection_rect(projection);
 views.scissor_dx=projection->centre_x-TR64_GPU_WIDTH/2;
 views.scissor_dy=projection->centre_y-TR64_GPU_HEIGHT/2;
 t3d_viewport_set_area(&gpu.viewport,area.left,area.top,projection->width,projection->height);
 t3d_viewport_attach(&gpu.viewport);
 /* Only the center moves. The 320x240 screen scale and the matching MVP
  * preserve focal 256 with exact existing fixed-point rounding. */
 rspq_write(T3D_RSP_ID,T3D_CMD_SCREEN_SIZE,1,
             ((uint32_t)projection->centre_x<<18)|((uint32_t)projection->centre_y<<2),
             (640u<<16)|(uint16_t)-480,(16384u<<16)|128u);
 t3d_light_set_exposure(1.0f);t3d_state_set_vertex_fx(T3D_VERTEX_FX_NONE,0,0);
 static const uint8_t ambient[4] __attribute__((aligned(4)))={0,0,0,0};
 t3d_light_set_ambient(ambient);t3d_light_set_count(0);
 t3d_state_set_alpha_to_tile(false);t3d_fog_set_enabled(true);gpu.fog_enabled=1;
 configure_depth_fog(projection);
 modes=rdp_other_modes(0);
 p=record(32);begin=p;
 word(&p,0xe7000000,0);word(&p,0xef000000|((uint32_t)(modes>>32)&0xffffff),(uint32_t)modes);
 word(&p,0xf8000000,0x00000080);word(&p,0xfa000000,0x808080ff);
 execute(begin,p);p=record(8);begin=p;word(&p,0xf9000000,1);execute(begin,p);
 combine(0);
}
void tr64_gpu_frame_begin(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 if(!gpu.initialized || !gpu.tex.data || gpu.in_frame)sys_error("gpu frame without boot/textures");
 if(!view || !tr64_gpu_projection_valid(projection))sys_error("gpu unsupported projection");
 uint32_t acquire_start=sys_time_us(),audio_us=0;
 gpu.surface=display_try_get();
 if(!gpu.surface) {
  kirq_wait_t wait=kirq_begin_wait_vi();
  const uint32_t interval=TICKS_FROM_MS(8);
  uint32_t audio_at=TICKS_READ()-interval;
  RSP_WAIT_LOOP(200) {
   gpu.surface=display_try_get();
   /* kirq_wait is a no-op without the SDK thread kernel. Bound service by
    * raw elapsed time, not by retry count or an assumed VI wakeup. */
   if(tr64_snd_pump && (gpu.surface || (uint32_t)(TICKS_READ()-audio_at)>=interval)) {
    uint32_t start=sys_time_us();tr64_snd_pump();audio_us+=sys_time_us()-start;
    audio_at=TICKS_READ();
    /* A VI may have arrived during service; do not pump again on success. */
    if(!gpu.surface)gpu.surface=display_try_get();
   }
   if(gpu.surface)break;
   kirq_wait(&wait);
  }
 }
 tr64_gpu_sync();
 gpu.start=sys_time_us();
 /* Zero the RDP's own counters (clock, buffer busy, pipe busy, TMEM busy) for this frame. */
 if(&tr64_perf_state && (tr64_perf_state&1u))
  *DP_STATUS=DP_WSTATUS_RESET_CLOCK_COUNTER|DP_WSTATUS_RESET_CMD_COUNTER|DP_WSTATUS_RESET_PIPE_COUNTER|DP_WSTATUS_RESET_TMEM_COUNTER;
 memset(&gpu.count,0,sizeof(gpu.count));memset(gpu.matrix_bits,0,sizeof(gpu.matrix_bits));
 gpu.count.acquire_us=gpu.start-acquire_start;gpu.count.acquire_audio_us=audio_us;
 gpu.count.pacing_us=gpu.count.acquire_us-audio_us;
 gpu.record_used=0;gpu.fenced[0]=gpu.fenced[1]=0;gpu.scratch_used=0;gpu.scratch_half=0;
 gpu.tri_dirty=0;gpu.in_frame=1;gpu.menu_pass=0;
 views.counts.split=0;
 rdpq_attach(gpu.surface,display_get_zbuf());
 rdpq_set_mode_standard();rdpq_clear(RGBA32(0,0,0,255));rdpq_clear_z(0xfffc);
 begin_3d_pass(view,projection);
}
void tr64_gpu_split_prepare(void)
{
 if(!gpu.in_frame || views.counts.split)sys_error("gpu invalid split preparation");
 views.counts=(tr64_gpu_split_counts_t){.split=2};
}
static void split_recycle(void)
{
 /* RSP matrix loads and RDP list executions reference these pool addresses.
  * The same full fence as a record spill retires both before reuse. */
 views.counts.rdp_spill_us+=drain_records();
 ++views.counts.rdp_record_spills;
 memset(gpu.matrix_bits,0,sizeof(gpu.matrix_bits));
}
void tr64_gpu_view_begin(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 if(!gpu.in_frame || gpu.menu_pass || views.counts.split!=2 || !view || !tr64_gpu_projection_valid(projection))
  sys_error("gpu invalid second view");
 views.counts.split=1;
 views.counts.first_triangles=gpu.count.triangles;
 views.counts.first_texture_uploads=gpu.count.texture_uploads;
 split_recycle();begin_3d_pass(view,projection);
}
void tr64_gpu_split_matrices(unsigned used)
{
 if(used>views.counts.matrix_peak)views.counts.matrix_peak=(uint16_t)used;
}
void tr64_gpu_split_overlay(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection)
{
 if(!gpu.in_frame || views.counts.split!=1 || !view || !tr64_gpu_projection_valid(projection))
  sys_error("gpu invalid split overlay");
 split_recycle();
 if(!gpu.menu_pass) {
  gpu.menu_pass=1;
  rdpq_set_mode_standard();rdpq_set_scissor(0,0,320,240);rdpq_clear_z(0xfffc);
 }
 begin_3d_pass(view,projection);
}
void tr64_gpu_menu_begin(const tr64_gpu_view_t *view,int clear_depth)
{
 if(!gpu.in_frame || gpu.menu_pass || !view || (clear_depth!=0 && clear_depth!=1))
  sys_error("gpu invalid menu pass");
 gpu.menu_pass=1;tri_sync();rdpq_set_mode_standard();
 if(clear_depth)rdpq_clear_z(0xfffc);
 static const tr64_gpu_projection_t full=TR64_GPU_WORLD_PROJECTION;
 begin_3d_pass(view,&full);
}
void tr64_gpu_scissor(tr64_gpu_rect_t r)
{
 if(r.left<0 || r.top<0 || r.right>320 || r.bottom>240 || r.left>=r.right || r.top>=r.bottom)
  sys_error("gpu invalid scissor");
 if(gpu.scissor_valid && r.left==gpu.scissor.left && r.top==gpu.scissor.top &&
    r.right==gpu.scissor.right && r.bottom==gpu.scissor.bottom)return;
 gpu.scissor=r;gpu.scissor_valid=1;
 r.left+=views.scissor_dx;r.right+=views.scissor_dx;
 r.top+=views.scissor_dy;r.bottom+=views.scissor_dy;
 uint32_t *p=record(8),*begin=p;
 word(&p,0xed000000|((uint32_t)r.left*4<<12)|(uint32_t)r.top*4,
       ((uint32_t)r.right*4<<12)|(uint32_t)r.bottom*4);execute(begin,p);
}
static inline uint8_t mesh_visibility_z(int64_t z)
{
 int64_t clip=rounded_pow2(z*2074,14)-263793;
 clip=clip+((-clip)>>16)-16726160;
 int64_t fog=(clip>>16)*512;
 if(fog<0)return 255;
 if(fog>32767)return 0;
 return (uint8_t)((32767-fog)*2>>8);
}
static __attribute__((noinline,cold)) uint8_t mesh_visibility_z_custom(int64_t z)
{
 int64_t clip=rounded_pow2(z*views.fog_az,14)-views.fog_bz;
 clip=clip+((-clip)>>16)-views.fog_offset;
 int64_t fog=(clip>>16)*views.fog_mult;
 if(fog<0)return 255;
 if(fog>32767)return 0;
 return (uint8_t)((32767-fog)*2>>8);
}
static __attribute__((noinline,cold)) int64_t matrix_encode_far(T3DMat4FP *out,const tr64_gpu_transform_t *t)
{
 static const uint8_t sh_tab[4]={14,14,14,0};
 const int32_t *vb=gpu.view.basis;
 int32_t src[4][3];
 if(!t->view_space) {
  for(unsigned c=0;c<3;++c) { src[c][0]=t->basis[c];src[c][1]=t->basis[3+c];src[c][2]=t->basis[6+c]; }
  for(unsigned r=0;r<3;++r)src[3][r]=(int32_t)((int64_t)t->translation[r]-gpu.view.eye[r]);
 }
 __asm__ volatile("cache 0x0d, 0(%0)\n\tcache 0x0d, 16(%0)\n\tcache 0x0d, 32(%0)\n\tcache 0x0d, 48(%0)"::"r"(out):"memory");
 int64_t z=0;
 const int64_t znum=views.z_num;
 const uint64_t zden=(uint64_t)views.z_den;
 const int32_t zbias=views.z_bias;
 #pragma GCC unroll 1
 for(unsigned c=0;c<4;++c) {
  int64_t v0,v1,v2;
  if(t->view_space) {
   v0=(int64_t)(c<3?t->basis[c]:t->translation[0])*16384;
   v1=(int64_t)(c<3?t->basis[3+c]:t->translation[1])*16384;
   v2=(int64_t)(c<3?t->basis[6+c]:t->translation[2])*16384;
  } else {
   int32_t s0=src[c][0],s1=src[c][1],s2=src[c][2];
   v0=(int64_t)vb[0]*s0+(int64_t)vb[1]*s1+(int64_t)vb[2]*s2;
   v1=(int64_t)vb[3]*s0+(int64_t)vb[4]*s1+(int64_t)vb[5]*s2;
   v2=(int64_t)vb[6]*s0+(int64_t)vb[7]*s1+(int64_t)vb[8]*s2;
  }
  unsigned sh=sh_tab[c];
  int32_t m0=(int32_t)rounded_shift(v0,5,sh);
  int32_t m1=(int32_t)rounded_shift(-v1*4,15,sh);
  int32_t m2=(int32_t)rounded_shift(v2*znum,zden,sh)-(c==3?zbias:0);
  int32_t m3=(int32_t)rounded_pow2(v2,sh+3);
  out->m[c].i[0]=(int16_t)((uint32_t)m0>>16); out->m[c].f[0]=(uint16_t)m0;
  out->m[c].i[1]=(int16_t)((uint32_t)m1>>16); out->m[c].f[1]=(uint16_t)m1;
  out->m[c].i[2]=(int16_t)((uint32_t)m2>>16); out->m[c].f[2]=(uint16_t)m2;
  out->m[c].i[3]=(int16_t)((uint32_t)m3>>16); out->m[c].f[3]=(uint16_t)m3;
  z=v2;
 }
 __asm__ volatile("cache 0x15, 0(%0)\n\tcache 0x15, 16(%0)\n\tcache 0x15, 32(%0)\n\tcache 0x15, 48(%0)"::"r"(out):"memory");
 return z;
}
static __attribute__((noinline,cold)) int64_t matrix_encode_view(T3DMat4FP *out,const tr64_gpu_transform_t *t)
{
 static const uint8_t sh_tab[4]={14,14,14,0};
 static const int32_t zb_tab[4]={0,0,0,263793};
 __asm__ volatile("cache 0x0d, 0(%0)\n\tcache 0x0d, 16(%0)\n\tcache 0x0d, 32(%0)\n\tcache 0x0d, 48(%0)"::"r"(out):"memory");
 int64_t z=0;
 #pragma GCC unroll 1
 for(unsigned c=0;c<4;++c) {
  int64_t v0=(int64_t)(c<3?t->basis[c]:t->translation[0])*16384;
  int64_t v1=(int64_t)(c<3?t->basis[3+c]:t->translation[1])*16384;
  int64_t v2=(int64_t)(c<3?t->basis[6+c]:t->translation[2])*16384;
  unsigned sh=sh_tab[c];
  int32_t m0=(int32_t)rounded_shift(v0,5,sh);
  int32_t m1=(int32_t)rounded_shift(-v1*4,15,sh);
  int32_t m2=(int32_t)rounded_shift(v2*161,1272,sh)-zb_tab[c];
  int32_t m3=(int32_t)rounded_pow2(v2,sh+3);
  out->m[c].i[0]=(int16_t)((uint32_t)m0>>16); out->m[c].f[0]=(uint16_t)m0;
  out->m[c].i[1]=(int16_t)((uint32_t)m1>>16); out->m[c].f[1]=(uint16_t)m1;
  out->m[c].i[2]=(int16_t)((uint32_t)m2>>16); out->m[c].f[2]=(uint16_t)m2;
  out->m[c].i[3]=(int16_t)((uint32_t)m3>>16); out->m[c].f[3]=(uint16_t)m3;
  z=v2;
 }
 __asm__ volatile("cache 0x15, 0(%0)\n\tcache 0x15, 16(%0)\n\tcache 0x15, 32(%0)\n\tcache 0x15, 48(%0)"::"r"(out):"memory");
 return z;
}
static __attribute__((noinline)) int64_t matrix_encode(T3DMat4FP *out,const tr64_gpu_transform_t *t)
{
 if(t->view_space)return matrix_encode_view(out,t);
 static const uint8_t sh_tab[4]={14,14,14,0};
 static const int32_t zb_tab[4]={0,0,0,263793};
 const int32_t *vb=gpu.view.basis;
 int32_t src[4][3];
 for(unsigned c=0;c<3;++c) { src[c][0]=t->basis[c];src[c][1]=t->basis[3+c];src[c][2]=t->basis[6+c]; }
 for(unsigned r=0;r<3;++r)src[3][r]=(int32_t)((int64_t)t->translation[r]-gpu.view.eye[r]);
 __asm__ volatile("cache 0x0d, 0(%0)\n\tcache 0x0d, 16(%0)\n\tcache 0x0d, 32(%0)\n\tcache 0x0d, 48(%0)"::"r"(out):"memory");
 int64_t z=0;
 #pragma GCC unroll 1
 for(unsigned c=0;c<4;++c) {
  int32_t s0=src[c][0],s1=src[c][1],s2=src[c][2];
  int64_t v0=(int64_t)vb[0]*s0+(int64_t)vb[1]*s1+(int64_t)vb[2]*s2;
  int64_t v1=(int64_t)vb[3]*s0+(int64_t)vb[4]*s1+(int64_t)vb[5]*s2;
  int64_t v2=(int64_t)vb[6]*s0+(int64_t)vb[7]*s1+(int64_t)vb[8]*s2;
  unsigned sh=sh_tab[c];
  int32_t m0=(int32_t)rounded_shift(v0,5,sh);
  int32_t m1=(int32_t)rounded_shift(-v1*4,15,sh);
  int32_t m2=(int32_t)rounded_shift(v2*161,1272,sh)-zb_tab[c];
  int32_t m3=(int32_t)rounded_pow2(v2,sh+3);
  out->m[c].i[0]=(int16_t)((uint32_t)m0>>16); out->m[c].f[0]=(uint16_t)m0;
  out->m[c].i[1]=(int16_t)((uint32_t)m1>>16); out->m[c].f[1]=(uint16_t)m1;
  out->m[c].i[2]=(int16_t)((uint32_t)m2>>16); out->m[c].f[2]=(uint16_t)m2;
  out->m[c].i[3]=(int16_t)((uint32_t)m3>>16); out->m[c].f[3]=(uint16_t)m3;
  z=v2;
 }
 __asm__ volatile("cache 0x15, 0(%0)\n\tcache 0x15, 16(%0)\n\tcache 0x15, 32(%0)\n\tcache 0x15, 48(%0)"::"r"(out):"memory");
 return z;
}
static __attribute__((noinline,cold)) void encode_matrix_custom(unsigned id,const tr64_gpu_transform_t *t,uint32_t property,uint32_t start)
{
 int64_t z=views.far_extended
  ? matrix_encode_far(&gpu.matrices[id],t)
  : matrix_encode(&gpu.matrices[id],t);
 gpu.count.matrix_encode_us+=TICKS_READ()-start;
 uint8_t alpha=255;
 if(t->mesh) {
  if(t->view_space) {
   z=0;for(unsigned k=0;k<3;++k)z+=(int64_t)gpu.view.basis[6+k]*((int64_t)t->translation[k]-gpu.view.eye[k]);
  }
  alpha=mesh_visibility_z_custom(z);
 }
 gpu.mesh_alpha=alpha;
 gpu.matrix_properties[id]=property|((uint32_t)alpha<<24);
 gpu.matrix_light_offsets[id]=t->light_offset;
 gpu.matrix_bits[id/32]|=1u<<(id%32);
}
void tr64_gpu_matrix(const tr64_gpu_transform_t *t)
{
 unsigned id=t->instance_id;uint32_t property;
 if(id>=256 || t->water>1 || t->mesh>1 || t->view_space>1 || t->logical_room<0 || t->logical_room>65535)
  sys_error("gpu matrix identity/range");
 if(gpu.matrix_id==id)return;
 uint32_t start=TICKS_READ();
 property=t->water|((uint32_t)t->mesh<<1)|((uint32_t)t->view_space<<2)|((uint32_t)t->logical_room<<3);
 if(gpu.matrix_bits[id/32]&(1u<<(id%32))) {
  if((gpu.matrix_properties[id]&0xffffffu)!=property || gpu.matrix_light_offsets[id]!=t->light_offset)
   sys_error("gpu changed live matrix");
  gpu.mesh_alpha=(uint8_t)(gpu.matrix_properties[id]>>24);
 } else if(__builtin_expect(views.fog_custom,0)) {
  encode_matrix_custom(id,t,property,start);
 } else {
  int64_t z=matrix_encode(&gpu.matrices[id],t);
  gpu.count.matrix_encode_us+=TICKS_READ()-start;
  uint8_t alpha=255;
  if(t->mesh) {
   if(t->view_space) {
    z=0;for(unsigned k=0;k<3;++k)z+=(int64_t)gpu.view.basis[6+k]*((int64_t)t->translation[k]-gpu.view.eye[k]);
   }
   alpha=mesh_visibility_z(z);
  }
  gpu.mesh_alpha=alpha;
  gpu.matrix_properties[id]=property|((uint32_t)alpha<<24);
  gpu.matrix_light_offsets[id]=t->light_offset;
  gpu.matrix_bits[id/32]|=1u<<(id%32);
 }
 tri_sync();t3d_matrix_set(&gpu.matrices[id],false);
 uint8_t fog=!t->mesh;
 if(fog!=gpu.fog_enabled) { t3d_fog_set_enabled(fog);gpu.fog_enabled=fog; }
 gpu.transform=*t;gpu.matrix_id=id;gpu.batch_id=UINT32_MAX;++gpu.count.matrix_loads;
 gpu.count.matrix_us+=TICKS_READ()-start;
}
static __attribute__((noinline,cold)) void material_flat(tr64_gpu_material_t mat,uint32_t start)
{
 gpu.last_mat_key=UINT32_MAX;
 if(mat.content!=mat.material)sys_error("gpu animated flat material");
 uint32_t rgba;
 if(mat.material==UINT16_MAX) {
  rgba=0x000000ffu;
 } else {
  const uint8_t *d=gpu.tex_flats+mat.material*8;
  uint16_t c=be16(d+2);uint32_t r=(c>>11)&31,g=(c>>6)&31,b=(c>>1)&31;
  rgba=(((r<<3)|(r>>2))<<24)|(((g<<3)|(g>>2))<<16)|(((b<<3)|(b>>2))<<8)|255;
 }
 uint32_t *p=record(16),*begin=p;word(&p,0xe7000000,0);word(&p,0xfb000000,rgba);execute(begin,p);
 gpu.count.texture_setup_us+=TICKS_READ()-start;
}
static __attribute__((noinline,cold)) void material_split(const uint8_t *d,unsigned sprite,unsigned tile,unsigned splits,unsigned *sy,unsigned *ey)
{
 unsigned split_first=be16(d+(sprite?10:20));
 if(tile>splits)sys_error("gpu texture rectangle/tile");
 if(tile)*sy=be16(gpu.tex_lines+2*(split_first+tile-1));
 if(tile<splits)*ey=be16(gpu.tex_lines+2*(split_first+tile));
}
void tr64_gpu_material(tr64_gpu_material_t mat)
{
 uint32_t key=((uint32_t)mat.content<<16)|((uint32_t)mat.tile<<8)|
              (mat.flags&(TR64_GPU_BLENDED_M1|TR64_GPU_DOUBLE_SIDED|TR64_GPU_FLAT|TR64_GPU_NO_DEPTH|TR64_GPU_ADDITIVE));
 if(key==gpu.last_mat_key && mat.material<gpu.tex_total)return;
 uint32_t start=TICKS_READ(),*p,*begin;const uint8_t *tx=gpu.tex.data,*d,*cl,*pal;
 unsigned flat=!!(mat.flags&TR64_GPU_FLAT);
 unsigned mode=mat.flags&(TR64_GPU_BLENDED_M1|TR64_GPU_NO_DEPTH|TR64_GPU_ADDITIVE);
 unsigned flags=T3D_FLAG_SHADED|T3D_FLAG_TEXTURED|((mode&TR64_GPU_NO_DEPTH)?0u:T3D_FLAG_DEPTH);
 if(!(mat.flags&TR64_GPU_DOUBLE_SIDED))flags|=T3D_FLAG_CULL_FRONT;
 if(flags!=gpu.drawflags) { tri_sync();t3d_state_set_drawflags(flags);gpu.drawflags=flags; }
 if(__builtin_expect(mode!=(gpu.rdp_mode&0x7fu),0))set_rdp_mode(mode);
 if(flat!=gpu.flat)combine((int)flat);
 if(flat) { material_flat(mat,start);return; }
 unsigned objects=gpu.tex_objects,total=gpu.tex_total,sprite=mat.content>=objects;
 if(__builtin_expect(mat.material>=total || mat.content>=total,0))sys_error("gpu texture descriptor");
 d=sprite ? gpu.tex_spr+(mat.content-objects)*22 : gpu.tex_obj+mat.content*24;
 unsigned cluster=be16(d),palette=be16(d+2),x=d[4],y=d[5],w=d[6]+1,h=d[7]+1;
 unsigned splits=be16(d+(sprite?12:22)),sy=0,ey=h;
 if(__builtin_expect((mat.tile|splits)!=0,0))material_split(d,sprite,mat.tile,splits,&sy,&ey);
 if(__builtin_expect((x&1)!=0,0))sys_error("gpu texture rectangle/tile");
 cl=gpu.tex_clusters+cluster*16;unsigned row=be16(cl+8),row_bytes=(w+1)>>1,pitch=(row_bytes+7u)&~7u;
 unsigned height=ey-sy,tmem_bytes=height*pitch;
 if(__builtin_expect(ey<=sy || tmem_bytes>2048,0))sys_error("gpu TMEM tile overflow");
 /* CI4 has sixteen independent 16-colour TLUT slots in the existing upper
  * half of TMEM. A direct-mapped cache is bounded and collision-safe. */
 unsigned palette_slot=palette&15;
 uint32_t image=be32(cl),origin=(x<<16)|(y+sy),size=(w<<16)|height;
 int palette_change=gpu.palette_ids[palette_slot]!=palette;
 int image_change=((gpu.tex_row^row)|(gpu.tex_img^image)|(gpu.tex_origin^origin)|(gpu.tex_size^size))!=0;
 unsigned bytes=(palette_change?32u:0u)+(image_change?48u:(gpu.palette_slot!=palette_slot?8u:0u));
 gpu.last_mat_key=key;
 if(!bytes) { gpu.count.texture_setup_us+=TICKS_READ()-start;return; }
 /* Reserve/publish the complete material change once with a shared pipe+tile sync header. */
 p=record(bytes+16u);begin=p;
 word(&p,0xe7000000,0);word(&p,0xe8000000,0);
 if(palette_change) {
  pal=gpu.tex_palettes+palette*8;
  word(&p,0xfd000000|((uint32_t)FMT_RGBA16<<19)|15,PhysicalAddr(tx+be32(pal)));
  word(&p,0xf5000000|((uint32_t)FMT_I4<<19)|(256+16*palette_slot),7u<<24);
  word(&p,0xe6000000,0);word(&p,0xf0000000,(7u<<24)|(15u<<14));
  gpu.palette_ids[palette_slot]=palette;++gpu.count.palette_loads;
 }
 if(image_change) {
  tr64_ci4_rect_t rect;
  if(tr64_ci4_can_load_block(x,row_bytes,row,height)) {
   word(&p,0xfd000000|((uint32_t)FMT_RGBA16<<19)|((row>>1)-1),PhysicalAddr(tx+image+(y+sy)*row));
   word(&p,0xf5000000|((uint32_t)FMT_RGBA16<<19),1u<<24);
   word(&p,0xe6000000,0);
   rect=tr64_ci4_block(w,height,row);
   gpu.count.texture_upload_bytes+=tmem_bytes;
  } else {
   word(&p,0xfd000000|((uint32_t)FMT_I8<<19)|(row-1),PhysicalAddr(tx+image));
   word(&p,0xf5000000|((uint32_t)FMT_I8<<19)|((pitch>>3)<<9),1u<<24);
   word(&p,0xe6000000,0);
   rect=tr64_ci4_rect(x,y+sy,w,height);
   gpu.count.texture_upload_bytes+=row_bytes*height;
  }
  word(&p,rect.load_a,rect.load_b);
  word(&p,0xf5000000|((uint32_t)FMT_CI4<<19)|((pitch>>3)<<9),
       (palette_slot<<20)|(1u<<19)|(1u<<9));
  word(&p,rect.clamp_a,rect.clamp_b);
  gpu.tex_img=image;gpu.tex_row=row;gpu.tex_origin=origin;gpu.tex_size=size;gpu.palette_slot=palette_slot;
  ++gpu.count.texture_uploads;
 } else if(gpu.palette_slot!=palette_slot) {
  word(&p,0xf5000000|((uint32_t)FMT_CI4<<19)|((pitch>>3)<<9),
       (palette_slot<<20)|(1u<<19)|(1u<<9));
  gpu.palette_slot=palette_slot;
 }
 execute(begin,p);
 gpu.count.texture_setup_us+=TICKS_READ()-start;
}
static inline uint32_t shade_rgba(unsigned shade,int32_t light_offset,unsigned water,uint8_t alpha)
{
 int32_t lit=(int32_t)(shade<<5)-light_offset;
 unsigned gain=(unsigned)(lit<0?0:(lit>6144?6144:lit)),r,g;
 if(water) { r=((gain+24)*2731u)>>17;g=((gain+15)*8739u)>>18; }
 else { r=((gain+12)*2731u)>>16;if(r>255)r=255;g=r; }
 return (r<<24)|(g<<16)|(g<<8)|alpha;
}
static __attribute__((noinline,cold)) void wait_vertex_slot(unsigned group)
{
 uint32_t wait=TICKS_READ();
 ++gpu.count.vertex_slot_waits;rspq_syncpoint_wait(gpu.fences[group]);
 gpu.count.vertex_wait_us+=TICKS_READ()-wait;
}
static __attribute__((noinline,cold)) void copy_transient_verts(uint8_t *dst,const void *src,uint32_t bytes)
{
 dcache_create(dst,bytes);memcpy(dst,src,bytes);dcache_wb_inv(dst,bytes);
}
typedef struct {
 int32_t lx,ly,lz,radius,rad2,peak;
 uint8_t shift;
} room_local_light_t;
static inline uint32_t light_room_rgba(uint32_t rgba,int32_t vx,int32_t vy,int32_t vz,
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
static inline uint32_t tint_camera_water_rgba(uint32_t rgba)
{
 unsigned cr=((rgba>>24)&0xffu)>>1;
 unsigned cg=(((rgba>>16)&0xffu)*205u)>>8;
 unsigned cb=(((rgba>>8)&0xffu)*205u)>>8;
 return (cr<<24)|(cg<<16)|(cb<<8)|(rgba&0xffu);
}
static __attribute__((noinline,cold)) void recolour_room_lit_verts(uint8_t *dst,const uint8_t *src,unsigned vc)
{
 unsigned pairs=(vc+1u)>>1;
 room_local_light_t ll[TR64_GPU_MAX_LIGHTS];
 unsigned nl=0;
 const int32_t tx=gpu.transform.translation[0];
 const int32_t ty=gpu.transform.translation[1];
 const int32_t tz=gpu.transform.translation[2];
 for(unsigned i=0;i<room_lights.count;++i) {
  const tr64_gpu_light_t *l=room_lights.items+i;
  if(!l->active || l->radius<=0 || l->radius>16384 || l->shift>30u)continue;
  int64_t lx=(int64_t)l->pos[0]-(int64_t)tx;
  int64_t ly=(int64_t)l->pos[1]-(int64_t)ty;
  int64_t lz=(int64_t)l->pos[2]-(int64_t)tz;
  int64_t lim=32768+(int64_t)l->radius;
  if(lx<-lim || lx>lim || ly<-lim || ly>lim || lz<-lim || lz>lim)continue;
  ll[nl++]=(room_local_light_t){(int32_t)lx,(int32_t)ly,(int32_t)lz,
   l->radius,l->radius*l->radius,(int32_t)l->peak,l->shift};
 }
 const unsigned water=gpu.transform.water;
 const unsigned cam_tint=room_lights.camera_water && !water;
 const unsigned eff_water=water|cam_tint;
 if(!nl && !cam_tint) {
  copy_transient_verts(dst,src,pairs<<5);
  return;
 }
 const uint64_t *s64=(const uint64_t *)src;
 uint64_t *d64=(uint64_t *)dst;
 for(unsigned p=0;p<pairs;++p,s64+=4,d64+=4) {
  uint64_t w0=s64[0],w1=s64[1],w2=s64[2],w3=s64[3];
  uint32_t c0=(uint32_t)(w2>>32);
  if(cam_tint)c0=tint_camera_water_rgba(c0);
  if(nl)c0=light_room_rgba(c0,(int16_t)(w0>>48),(int16_t)(w0>>32),(int16_t)(w0>>16),ll,nl,eff_water);
  uint32_t c1=(uint32_t)w2;
  if(p*2u+1u<vc) {
   if(cam_tint)c1=tint_camera_water_rgba(c1);
   if(nl)c1=light_room_rgba(c1,(int16_t)(w1>>48),(int16_t)(w1>>32),(int16_t)(w1>>16),ll,nl,eff_water);
  }
  __asm__ volatile("cache 0x0d, 0(%0)\n\tcache 0x0d, 16(%0)"::"r"(d64):"memory");
  d64[0]=w0;d64[1]=w1;d64[2]=((uint64_t)c0<<32)|c1;d64[3]=w3;
  __asm__ volatile("cache 0x15, 0(%0)\n\tcache 0x15, 16(%0)"::"r"(d64):"memory");
 }
}
static __attribute__((noinline,cold)) void copy_or_lit_room_verts(uint8_t *dst,const tr64_gpu_batch_t *b,uint32_t bytes)
{
 if(b->batch_id&TR64_GPU_BATCH_LIT)recolour_room_lit_verts(dst,b->vertices,b->vertex_count);
 else copy_transient_verts(dst,b->vertices,bytes);
}
static __attribute__((noinline,cold)) uint8_t *scratch_next_half(unsigned bytes)
{
 unsigned half=gpu.scratch_half;
 gpu.fences[half]=rspq_syncpoint_new();gpu.fenced[half]=1;
 half^=1u;gpu.scratch_half=(uint8_t)half;gpu.scratch_used=bytes;
 if(gpu.fenced[half]) {
  if(!rspq_syncpoint_check(gpu.fences[half]))wait_vertex_slot(half);
  gpu.fenced[half]=0;
 }
 return gpu.scratch+half*GPU_SCRATCH_HALF_BYTES;
}
static __attribute__((noinline)) void recolour_mesh_verts(uint8_t *dst,const uint8_t *src,unsigned vc)
{
 unsigned pairs=(vc+1u)>>1;
 const uint64_t *s64=(const uint64_t *)src;
 uint64_t *d64=(uint64_t *)dst;
 uint32_t previous=UINT32_MAX,rgba=0;
 const uint8_t alpha=gpu.mesh_alpha;
 int32_t light_offset=gpu.transform.light_offset;
 if(light_offset>8192)light_offset=8192;
 else if(light_offset<-6144)light_offset=-6144;
 const unsigned water=gpu.transform.water|room_lights.camera_water;
 for(unsigned p=0;p<pairs;++p,s64+=4,d64+=4) {
  uint64_t w0=s64[0],w1=s64[1],w2=s64[2],w3=s64[3];
  unsigned sh0=(unsigned)(w2>>56);
  if(sh0!=previous) { rgba=shade_rgba(sh0,light_offset,water,alpha);previous=sh0; }
  uint32_t c0=rgba,c1;
  if(p*2+1<vc) {
   unsigned sh1=(unsigned)((w2>>24)&0xffu);
   if(sh1!=previous) { rgba=shade_rgba(sh1,light_offset,water,alpha);previous=sh1; }
   c1=rgba;
  } else c1=(uint32_t)w2;
  __asm__ volatile("cache 0x0d, 0(%0)\n\tcache 0x0d, 16(%0)"::"r"(d64):"memory");
  d64[0]=w0;d64[1]=w1;d64[2]=((uint64_t)c0<<32)|c1;d64[3]=w3;
  __asm__ volatile("cache 0x15, 0(%0)\n\tcache 0x15, 16(%0)"::"r"(d64):"memory");
 }
}
static const T3DVertPacked *vertices(const tr64_gpu_batch_t *b)
{
 if((!gpu.transform.mesh || (gpu.rdp_mode&GPU_MODE_SKY_1CYC)) &&
    !(b->batch_id&(TR64_GPU_BATCH_TRANSIENT|TR64_GPU_BATCH_LIT)))
  return (const T3DVertPacked *)b->vertices;
 unsigned pairs=(b->vertex_count+1u)>>1,bytes=pairs<<5;
 unsigned used=gpu.scratch_used;
 uint8_t *dst;
 if(__builtin_expect(bytes>GPU_SCRATCH_HALF_BYTES-used,0))dst=scratch_next_half(bytes);
 else { dst=gpu.scratch+gpu.scratch_half*GPU_SCRATCH_HALF_BYTES+used;gpu.scratch_used=used+bytes; }
 uint32_t start=TICKS_READ();
 if(gpu.transform.mesh)recolour_mesh_verts(dst,b->vertices,b->vertex_count);
 else copy_or_lit_room_verts(dst,b,bytes);
 gpu.count.vertex_colour_us+=TICKS_READ()-start;
 return (const T3DVertPacked *)dst;
}
static __attribute__((noinline,cold)) volatile uint32_t *batch_next_buffer(volatile uint32_t *cur)
{
 rspq_cur_pointer=cur;
 rspq_next_buffer();
 return rspq_cur_pointer;
}
void tr64_gpu_batch(const tr64_gpu_batch_t *b)
{
 if(!b->vertex_count || b->vertex_count>70 || gpu.matrix_id==UINT32_MAX)sys_error("gpu batch without matrix");
 if(gpu.batch_id!=b->batch_id) {
  uint32_t start=TICKS_READ();
  tri_sync();const T3DVertPacked *v=vertices(b);t3d_vert_load(v,0,b->vertex_count);
  gpu.batch_id=b->batch_id;gpu.count.vertex_loads+=(b->vertex_count+1)&~1u;
  gpu.count.vertex_us+=TICKS_READ()-start;
 }
 uint32_t st[4];for(unsigned i=0;i<4;++i)st[i]=be32(b->st+4*i);
 uint32_t start=TICKS_READ(),tris=0;
 const uint8_t *p=b->faces,*end=p+8u*b->face_count;
 const uint16_t *ptrs=gpu.pointers;
 if(p<end) {
  volatile uint32_t *cur=rspq_cur_pointer,*sent=rspq_cur_sentinel;
  const uint32_t cmd=T3D_RSP_ID;
  /* Invariant: cur<=sent on entry and after each face; <=10 words per face <= RSPQ_MAX_SHORT_COMMAND_SIZE (16). */
  for(;p<end;p+=8) {
   unsigned map=p[4],d=p[3];
   uint32_t cmd_pa=cmd|ptrs[p[0]],pc=ptrs[p[2]];
   uint32_t st0=st[map&3],st2=st[(map>>4)&3];
   cur[1]=((uint32_t)ptrs[p[1]]<<16)|pc;
   cur[2]=st0;cur[3]=st[(map>>2)&3];cur[4]=st2;
   cur[0]=cmd_pa;
   if(d!=255) {
    cur[6]=(pc<<16)|ptrs[d];
    cur[7]=st0;cur[8]=st2;cur[9]=st[map>>6];
    cur[5]=cmd_pa;
    cur+=10;tris+=2;
   } else { cur+=5;++tris; }
   if(__builtin_expect(cur>sent,0)) { cur=batch_next_buffer(cur);sent=rspq_cur_sentinel; }
  }
  rspq_cur_pointer=cur;
  gpu.tri_dirty=1;
 }
 gpu.count.triangles+=tris;
 gpu.count.expansion_us+=TICKS_READ()-start;gpu.count.polygons+=b->face_count;
 ++gpu.count.batch_submissions;
}
void tr64_gpu_frame_end(tr64_gpu_counts_t *out)
{
 if(views.counts.split) {
  views.counts.second_triangles=gpu.count.triangles-views.counts.first_triangles;
  views.counts.second_texture_uploads=gpu.count.texture_uploads-views.counts.first_texture_uploads;
 }
 tri_sync();if(tr64_ui_gpu_draw)tr64_ui_gpu_draw();
#if TR64_PLAY && !TR64_TEST
 if(!(&tr64_perf_state && (tr64_perf_state&1u))) {
  rdpq_detach_show();
  uint32_t now=sys_time_us();
  gpu.count.cpu_submit_us=now-gpu.start;
  gpu.count.completed_at_us=now;gpu.count.gpu_done_us=gpu.count.cpu_submit_us;
  gpu.frame_pending=1;
 } else
#endif
 {
  rdpq_detach();gpu.count.cpu_submit_us=sys_time_us()-gpu.start;
  rspq_wait();gpu.count.completed_at_us=sys_time_us();gpu.count.gpu_done_us=gpu.count.completed_at_us-gpu.start;
  if(&tr64_perf_state && (tr64_perf_state&1u)) {
   gpu.count.rdp_clock=*DP_CLOCK&0xffffffu;gpu.count.rdp_bufbusy=*DP_BUSY&0xffffffu;
   gpu.count.rdp_pipebusy=*DP_PIPE_BUSY&0xffffffu;gpu.count.rdp_tmem=*DP_TMEM_BUSY&0xffffffu;
  }
 }
 gpu.count.expansion_us=sys_profile_us(gpu.count.expansion_us);
 gpu.count.texture_setup_us=sys_profile_us(gpu.count.texture_setup_us);
 gpu.count.matrix_us=sys_profile_us(gpu.count.matrix_us);
 gpu.count.matrix_encode_us=sys_profile_us(gpu.count.matrix_encode_us);
 gpu.count.vertex_us=sys_profile_us(gpu.count.vertex_us);
 gpu.count.vertex_colour_us=sys_profile_us(gpu.count.vertex_colour_us);
 gpu.count.vertex_wait_us=sys_profile_us(gpu.count.vertex_wait_us);
 gpu.count.triangle_command_bytes=gpu.count.triangles*20;*out=gpu.count;
 gpu.in_frame=0;
 if(!gpu.frame_pending)sys_gpu_present(gpu.surface);
}
const tr64_gpu_split_counts_t *tr64_gpu_split_counts(void) { return &views.counts; }
/* ---- Glyph batches (the profiler overlay's text) ----------------------------------------------------
 * The rdpq path draws a glyph by uploading its image and palette and then a rectangle: about 2,800 CPU
 * cycles on ares for each of some 300 glyphs a frame. Every font glyph has its own 16x16 CI4 image (128 B)
 * and its own 16-colour palette, so a batch gives each DISTINCT glyph one TMEM slot of a pass (up to 16:
 * 2 KiB of images below, the 16 TLUT slots above, as the room materials use them) and draws every
 * occurrence with one 16-byte TEXTURE_RECTANGLE. The commands are built in a cached buffer, written back
 * and handed to the RSP queue once per pass. The sequences are those of tr64_gpu_material: an I8 load of
 * the CI4 rows, a TLUT load through tile 7, tile 0 as the drawing tile. */
/* Keep any pass (even an arbitrarily long run of one glyph) smaller than the
 * arena. A normal profiler overlay has at most 400 glyphs altogether. */
enum { GLYPH_SLOTS=16,GLYPH_TMEM_BYTES=2048,GLYPH_PASS_RECTS=400 };
void tr64_ui_backend_glyphs(const tr64_ui_glyph_t *glyphs,unsigned count,
                            tr64_ui_sprite_t (*font)(unsigned glyph))
{
 uint32_t *p;
 unsigned at=0;
 if(!gpu.in_frame)sys_error("gpu glyphs outside a frame");
 /* The 2D state, as the sprite path sets it: no Z, texture only, TLUT, alpha-key, point sampling. */
 rdpq_set_mode_standard();rdpq_mode_zbuf(false,false);rdpq_mode_combiner(RDPQ_COMBINER_TEX);
 rdpq_mode_tlut(TLUT_RGBA16);rdpq_mode_alphacompare(1);rdpq_mode_filter(FILTER_POINT);
 while(at<count) {
  const unsigned pass_start=at;
  tr64_ui_sprite_t slot[GLYPH_SLOTS];unsigned first[GLYPH_SLOTS+1],tmem[GLYPH_SLOTS],slots=0,used=0;
  /* A pass: as many distinct glyphs as the TLUT slots and 2 KiB of TMEM hold. */
  while(at<count && slots<GLYPH_SLOTS && at-pass_start<GLYPH_PASS_RECTS) {
   const unsigned glyph=glyphs[at].glyph;
   const tr64_ui_sprite_t s=font(glyph);
   const unsigned pitch=((s.width/2)+7)&~7u,bytes=pitch*s.height;
   unsigned run=at;
   if((s.source_x&1) || (s.width&1) || s.source_x || s.source_y || s.width!=s.image_width || s.height!=s.image_height ||
      s.stride!=s.width/2)sys_error("gpu glyph is not a whole CI4 image");
   if(used+bytes>GLYPH_TMEM_BYTES)break;
   while(run<count && glyphs[run].glyph==glyph &&
         run-pass_start<GLYPH_PASS_RECTS)++run;
   slot[slots]=s;first[slots]=at;tmem[slots]=used/8;used+=bytes;++slots;at=run;
  }
  first[slots]=at;
  if(!slots)sys_error("gpu glyph pass without room");
  /* The arena's entire world budget is available before this 2D pass. If the
   * glyphs do not fit, drain now and let the overlay timer charge the wait. */
  uint32_t bytes=4*(10+22*slots+4*(at-pass_start));
  uint32_t step=(bytes+15u)&~15u;
  if(step>TR64_GPU_RDP_RECORD_BYTES-gpu.record_used)(void)drain_records();
  p=record(bytes);
  uint32_t *const begin=p;
  word(&p,0xe7000000,0);word(&p,0xe8000000,0);
  for(unsigned i=0;i<slots;++i) {
   const tr64_ui_sprite_t *s=&slot[i];
   const unsigned pitch=((s->width/2)+7)&~7u;
   /* Palette: 16 RGBA5551 entries into TLUT slot i. */
   word(&p,0xfd000000|((uint32_t)FMT_RGBA16<<19)|15,PhysicalAddr(s->palette));
   word(&p,0xf5000000|((uint32_t)FMT_I4<<19)|(256+16*i),7u<<24);
   word(&p,0xe6000000,0);word(&p,0xf0000000,(7u<<24)|(15u<<14));
   /* Image rows, as I8 bytes, into TMEM at this glyph's address through tile 1. */
   word(&p,0xfd000000|((uint32_t)FMT_I8<<19)|(s->stride-1u),PhysicalAddr(s->image));
   word(&p,0xf5000000|((uint32_t)FMT_I8<<19)|((pitch/8)<<9)|tmem[i],1u<<24);
   word(&p,0xe6000000,0);
   word(&p,0xf4000000,(1u<<24)|(((s->width/2)-1)*4<<12)|((s->height-1)*4));
  }
  for(unsigned i=0;i<slots;++i) {
   const tr64_ui_sprite_t *s=&slot[i];
   const unsigned pitch=((s->width/2)+7)&~7u,bw=(unsigned)(s->right-s->left),bh=(unsigned)(s->bottom-s->top);
   /* Texels per pixel as s5.10: the sprite's bounds are what is drawn, its image what is sampled. */
   const uint32_t dsdx=(s->width<<10)/bw,dtdy=(s->height<<10)/bh;
   word(&p,0xe8000000,0);
   word(&p,0xf5000000|((uint32_t)FMT_CI4<<19)|((pitch/8)<<9)|tmem[i],(i<<20)|(1u<<19)|(1u<<9));
   word(&p,0xf2000000,((s->width-1)*4<<12)|((s->height-1)*4));
   for(unsigned g=first[i];g<first[i+1];++g) {
    const int x=glyphs[g].x,y=glyphs[g].y;
    /* The scissor clips the right and bottom edges; a rectangle cannot start above or left of the screen, so
     * a glyph whose top sticks out starts at row 0 with its texture coordinate advanced by the rows cut. */
    const int top=y<0 ? 0 : y;
    const uint32_t t=16u+(y<0 ? (uint32_t)(-y)*dtdy/32u : 0u);
    if(x<0 || x>=320 || top>=240 || y+(int)bh<=0)continue;
    word(&p,0xe4000000|((uint32_t)(x+(int)bw)*4<<12)|(uint32_t)(y+(int)bh)*4,((uint32_t)x*4<<12)|(uint32_t)top*4);
    word(&p,(16u<<16)|(t&0xffffu),(dsdx<<16)|dtdy);
   }
  }
  /* Leave nothing in flight that a later rdpq call would not know about. */
  word(&p,0xe7000000,0);word(&p,0xe8000000,0);word(&p,0xe6000000,0);
  /* Flush/invalidate the full step allocated by record(bytes) so skipped off-screen
   * glyphs never leave dirty tail lines in D-cache, while executing only written bytes. */
  dcache_wb_inv(begin,step);
  rdpq_exec(begin,(int)((uintptr_t)p-(uintptr_t)begin));
 }
}
