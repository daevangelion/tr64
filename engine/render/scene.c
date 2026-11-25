/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "tr64/render.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/render/internal.h"
#include "engine/ui/ui.h"
#include "render_pose.h"
extern void tr64_snd_pump(void) __attribute__((weak));
extern const uint32_t tr64_scene_test_no_hash __attribute__((weak));
extern int tr64_ui_bind(const tr64_level_t *) __attribute__((weak));
extern void tr64_ui_set_active(int) __attribute__((weak));
extern void tr64_ui_unbind(void) __attribute__((weak));
extern uint32_t tr64_ui_last_submit_us(void) __attribute__((weak));
extern void tr64_ui_test_rect_path(unsigned) __attribute__((weak));
extern const tr64_game_settings_t *game_settings_view(void) __attribute__((weak));
static int scene_hud_bound;
static struct {
 tr64_render_auto_dist_t ctrl;
 uint32_t last_completed_us;
 int32_t last_fog;
 uint8_t active;
 uint8_t have_stamp;
} auto_dist;

static tr64_render_t renderer;
static tr64_render_counts_t last_counts;
static int frame_ready;
static int live_mode;
static uint8_t preview_split;
static tr64_render_pose_t menu_pose;
static tr64_render_counts_t menu_counts;
static int menu_empty,menu_ready;
static unsigned menu_view_scale;
static const tr64_gpu_projection_t menu_projection=TR64_GPU_WORLD_PROJECTION;
static tr64_render_pose_t fixture_pose;
#if TR64_RENDER_SPLIT
static tr64_render_pose_t fixture_second;
#endif
static const uint32_t rest_models[]=TR64_RENDER_REST_MODEL_IDS;
__attribute__((noinline,cold))
void tr64_render_auto_dist_reset(tr64_render_auto_dist_t *a,uint16_t max_far)
{
 if(!a)return;
 if(max_far<TR64_GPU_FAR_MIN)max_far=TR64_GPU_FAR_MIN;
 if(max_far>TR64_GPU_FAR_MAX)max_far=TR64_GPU_FAR_MAX;
 a->ema_us=36500u;
 a->cur_far=max_far;
 a->cooldown=0u;
 a->reserved=0u;
}
__attribute__((noinline,cold))
uint16_t tr64_render_auto_dist_step(tr64_render_auto_dist_t *a,uint32_t frame_us,uint16_t max_far)
{
 if(!a)return TR64_GPU_FAR;
 if(max_far<TR64_GPU_FAR_MIN)max_far=TR64_GPU_FAR_MIN;
 if(max_far>TR64_GPU_FAR_MAX)max_far=TR64_GPU_FAR_MAX;
 uint32_t clamped=frame_us<5000u ? 5000u : (frame_us>100000u ? 100000u : frame_us);
 a->ema_us=(a->ema_us*7u+clamped)>>3;
 uint16_t floor_far=max_far<4608u ? max_far : 4608u;
 if(a->cur_far>max_far)a->cur_far=max_far;
 if(a->cur_far<floor_far)a->cur_far=floor_far;
 if(a->cooldown) {
  --a->cooldown;
 } else if(a->ema_us>38000u) {
  if(a->cur_far>floor_far) {
   uint16_t next=(uint16_t)(a->cur_far-256u);
   a->cur_far=next<floor_far ? floor_far : next;
   a->cooldown=3u;
  }
 } else if(a->ema_us<35000u) {
  if(a->cur_far<max_far) {
   uint16_t next=(uint16_t)(a->cur_far+128u);
   a->cur_far=next>max_far ? max_far : next;
   a->cooldown=3u;
  }
 }
 return a->cur_far;
}
static __attribute__((noinline,cold)) void reset_graphics_options(void)
{
 memset(&auto_dist,0,sizeof(auto_dist));
 tr64_gpu_set_filter(0);
}
__attribute__((noinline,cold))
void tr64_render_apply_scene_options(tr64_gpu_projection_t *projection)
{
 const tr64_game_settings_t *s=game_settings_view ? game_settings_view() : NULL;
 tr64_gpu_set_filter(s && s->video_filter!=0);
 int32_t vfog=s ? s->video_fog : 8;
 uint16_t max_far,fog_near;
 tr64_render_fog_range(vfog,&max_far,&fog_near);
#if TR64_PLAY && !TR64_TEST
 if(s && s->video_auto_dist) {
  if(!auto_dist.active || auto_dist.last_fog!=vfog) {
   tr64_render_auto_dist_reset(&auto_dist.ctrl,max_far);
   auto_dist.active=1u;
   auto_dist.have_stamp=0u;
   auto_dist.last_fog=vfog;
  } else if(frame_ready) {
   uint32_t stamp=last_counts.gpu.completed_at_us;
   if(!auto_dist.have_stamp) {
    auto_dist.last_completed_us=stamp;
    auto_dist.have_stamp=1u;
   } else if(stamp!=auto_dist.last_completed_us) {
    uint32_t dt=stamp-auto_dist.last_completed_us;
    auto_dist.last_completed_us=stamp;
    if(dt<=100000u)
     (void)tr64_render_auto_dist_step(&auto_dist.ctrl,dt,max_far);
   }
  }
  uint16_t cur_far=auto_dist.ctrl.cur_far;
  uint16_t cur_near=(uint16_t)(((uint32_t)cur_far*4u)/5u);
  projection->far_z=projection->fog_far_z=cur_far;
  projection->fog_near_z=cur_near;
  return;
 }
 auto_dist.active=0u;
 auto_dist.have_stamp=0u;
#endif
 projection->far_z=projection->fog_far_z=max_far;
 projection->fog_near_z=fog_near;
}
static void finish_io(tr64_pack_t *pack)
{
 uint32_t start=sys_time_us();int status;
 while((status=tr64_pack_poll(pack))==0) {
  tr64_io_frame();tr64_io_pump();
  if(tr64_snd_pump)tr64_snd_pump();
  if((uint32_t)(sys_time_us()-start)>10000000u)sys_error("render section loading deadline");
 }
 if(status<0)sys_error("render section load failed");
}
#if TR64_RENDER_MEASURE
static void log_measure(const tr64_render_counts_t *counts)
{
 const tr64_gpu_counts_t *g=&counts->gpu;
 const tr64_gpu_split_counts_t *s=&counts->split;
 log_printf("render-measure: views=%u rnd_us=%lu gpu_us=%lu tri=%lu tex=%lu tex_bytes=%lu pal=%lu mat=%lu rms=%lu record_peak=%lu record_total=%lu spills=%lu spill_us=%lu\n",
  1u+s->split,(unsigned long)(g->cpu_submit_us-s->rdp_spill_us),(unsigned long)(g->gpu_done_us-g->cpu_submit_us),
  (unsigned long)g->triangles,(unsigned long)g->texture_uploads,(unsigned long)g->texture_upload_bytes,
  (unsigned long)g->palette_loads,(unsigned long)g->matrix_loads,(unsigned long)counts->visible_rooms,
  (unsigned long)g->rdp_record_peak,(unsigned long)g->rdp_record_bytes,
  (unsigned long)s->rdp_record_spills,(unsigned long)s->rdp_spill_us);
 if(s->split)log_printf("render-matrices: peak=%u\n",(unsigned)s->matrix_peak);
}
#endif
void tr64_render_scene_load(const tr64_level_t *level,const char *name,uint32_t pack_bytes)
{
 tr64_pack_t pack;tr64_gpu_span_t rooms,meshes;tr64_render_error_t error;
 tr64_render_counts_t counts;frame_ready=0;
 reset_graphics_options();
 const int32_t eye[3]=TR64_RENDER_EYE,look[3]=TR64_RENDER_LOOK;
 if(!live_mode && strcmp(name,TR64_RENDER_LEVEL))sys_error("render pose/level mismatch");
#if TR64_HAS_TR2
 if(level->game==2 && !live_mode && (TR64_RENDER_REST_MODEL_COUNT || TR64_RENDER_REST_ITEMS ||
                                    TR64_RENDER_LIVE_ITEM!=UINT32_MAX))
#else
 if(level->game==2 && (live_mode || TR64_RENDER_REST_MODEL_COUNT || TR64_RENDER_REST_ITEMS ||
                       TR64_RENDER_LIVE_ITEM!=UINT32_MAX))
#endif
  sys_error("render %s: Tomb Raider II requires a static camera and no items",name);
 tr64_pack_mount(&pack,pack_bytes,name);finish_io(&pack);
 tr64_pack_begin_kind(&pack,TR64_PACK_ROOMS,TR64_MEM_level);finish_io(&pack);
 rooms.data=tr64_pack_data(&pack,&rooms.bytes);
 tr64_pack_begin_kind(&pack,TR64_PACK_MESHES,TR64_MEM_level);finish_io(&pack);
 meshes.data=tr64_pack_data(&pack,&meshes.bytes);
 tr64_pack_unmount(&pack);
 tr64_geo_t rg,mg;
 if(tr64_geo_open(&rg,rooms,0,&error) || tr64_geo_open(&mg,meshes,1,&error))
  sys_error("render geometry code=%lu section=%lu offset=%lu",(unsigned long)error.code,(unsigned long)error.section,(unsigned long)error.offset);
 size_t bytes=tr64_render_workspace_bytes(level,&rg,&mg);
 if(bytes==SIZE_MAX || bytes>UINT32_MAX)sys_error("render workspace overflow");
 void *workspace=tr64_mem_alloc(TR64_MEM_state,(uint32_t)bytes);
 if(tr64_render_bind(&renderer,level,(tr64_gpu_span_t){level->textures,level->texture_bytes},rooms,meshes,workspace,bytes,&error))
  sys_error("render bind code=%lu section=%lu offset=%lu",(unsigned long)error.code,(unsigned long)error.section,(unsigned long)error.offset);
 if(live_mode)return;
 renderer.rest_model_ids=rest_models;renderer.rest_model_count=TR64_RENDER_REST_MODEL_COUNT;
 if(TR64_RENDER_LIVE_ITEM!=UINT32_MAX) {
  if(TR64_RENDER_LIVE_ITEM>=tr64_level_table(level,TR64_CORE_items)->count)sys_error("render fixture live item range");
  renderer.live_item_filter=1;renderer.live_instance_id=0x80000000u+TR64_RENDER_LIVE_ITEM;
 }
 renderer.rest_items=TR64_RENDER_REST_ITEMS;
 renderer.texture_requested_step=TR64_RENDER_TEXTURE_STEP;
 if(tr64_render_camera(&fixture_pose,eye,look,TR64_RENDER_ROOM,TR64_RENDER_FLIPPED,&error))sys_error("render invalid camera");
#if TR64_RENDER_SPLIT
 const int32_t eye2[3]=TR64_RENDER_EYE_SECOND,look2[3]=TR64_RENDER_LOOK_SECOND;
 if(tr64_render_camera(&fixture_second,eye2,look2,TR64_RENDER_ROOM_SECOND,TR64_RENDER_FLIPPED_SECOND,&error))
  sys_error("render invalid second camera");
 tr64_render_half(&fixture_pose,0);tr64_render_half(&fixture_second,1);
 if(tr64_render_split_frame(&renderer,&fixture_pose,&fixture_second,&counts,&error))
#else
 if(TR64_RENDER_HALF_VIEW)tr64_render_half(&fixture_pose,0);
 if(tr64_render_frame(&renderer,&fixture_pose,&counts,&error))
#endif
  sys_error("render frame code=%lu section=%lu offset=%lu",(unsigned long)error.code,(unsigned long)error.section,(unsigned long)error.offset);
 last_counts=counts;frame_ready=1;
#if TR64_RENDER_MEASURE
 log_measure(&counts);
#endif
 log_printf("render: pose=%s sha256=%s visible_rooms=%lu statics=%lu batches=%lu rejected=%lu triangles=%lu textures=%lu palettes=%lu vertices=%lu command_bytes=%lu rdp_records=%lu cpu_us=%lu done_us=%lu expand_us=%lu texture_setup_us=%lu matrices=%lu batch_submissions=%lu slot_waits=%lu visibility_us=%lu cull_us=%lu stream_us=%lu stream_runs=%lu submit_us=%lu matrix_us=%lu vertex_us=%lu recolour_us=%lu vertex_wait_us=%lu\n",
  TR64_RENDER_POSE,TR64_RENDER_POSE_SHA,(unsigned long)counts.visible_rooms,(unsigned long)counts.static_instances,
  (unsigned long)counts.batches_tested,(unsigned long)counts.batches_rejected,(unsigned long)counts.gpu.triangles,
  (unsigned long)counts.gpu.texture_uploads,(unsigned long)counts.gpu.palette_loads,(unsigned long)counts.gpu.vertex_loads,
  (unsigned long)counts.gpu.triangle_command_bytes,(unsigned long)counts.gpu.rdp_record_bytes,
  (unsigned long)counts.gpu.cpu_submit_us,(unsigned long)counts.gpu.gpu_done_us,(unsigned long)counts.gpu.expansion_us,
  (unsigned long)counts.gpu.texture_setup_us,(unsigned long)counts.gpu.matrix_loads,(unsigned long)counts.gpu.batch_submissions,
  (unsigned long)counts.gpu.vertex_slot_waits,(unsigned long)counts.visibility_us,(unsigned long)counts.culling_us,
  (unsigned long)counts.stream_scan_us,(unsigned long)counts.stream_runs,(unsigned long)counts.gpu_submit_us,
  (unsigned long)counts.gpu.matrix_us,(unsigned long)counts.gpu.vertex_us,(unsigned long)counts.gpu.vertex_colour_us,
  (unsigned long)counts.gpu.vertex_wait_us);
 {
  log_printf("render-visible:");
  for(uint32_t i=0;i<renderer.rooms.objects;++i) {
   tr64_gpu_rect_t c=clips(&renderer)[i];
   if((c.left<c.right && c.top<c.bottom) || queued(&renderer)[i])log_printf(" %lu",(unsigned long)i);
  }
  log_printf("\n");
 }
 tr64_mem_log();log_flush();
 log_printf("sprites: rooms=%lu items=%lu tiles=%lu\n",(unsigned long)counts.room_sprites,
  (unsigned long)counts.item_sprites,(unsigned long)counts.sprite_tiles);log_flush();
 log_printf("texture-animation: requested=%lu applied=%lu cycle_words=%lu\n",
  (unsigned long)renderer.texture_requested_step,(unsigned long)renderer.texture_step,
  (unsigned long)renderer.texture_cycle_words);log_flush();
}
void tr64_render_scene_set_live(int live)
{
 if((live!=0 && live!=1) || renderer.bound)sys_error("render live mode requires unloaded scene");
 live_mode=live;
}
void tr64_render_scene_release(void)
{
 tr64_render_preview_stop();
 if(scene_hud_bound) {
  tr64_ui_set_active(0);tr64_ui_unbind();scene_hud_bound=0;
 }
 if(renderer.bound)tr64_gpu_bind_textures((tr64_gpu_span_t){NULL,0});
#if !defined(N64) || TR64_GAME >= 2
 tr64_render_set_lights(NULL,0);
#endif
 memset(&renderer,0,sizeof(renderer));frame_ready=0;
 menu_empty=menu_ready=0;
 reset_graphics_options();
}
void tr64_render_game_scene_prepare(void)
{
 if(!renderer.bound || renderer.rest_prepared)sys_error("render game prepare order");
 if(TR64_RENDER_HUD && !live_mode) {
  if(!tr64_ui_bind || !tr64_ui_set_active || !tr64_ui_unbind ||
     !tr64_ui_last_submit_us || !tr64_ui_bind(renderer.core))
   sys_error("render HUD requested but font/UI unavailable");
  tr64_ui_set_active(1);scene_hud_bound=1;
  if(TR64_RENDER_HUD_RECT_PATH) {
   if(!tr64_ui_test_rect_path)sys_error("render HUD path requires test adapter");
   tr64_ui_test_rect_path(TR64_RENDER_HUD_RECT_PATH);
  }
 }
 uint32_t items=tr64_level_table(renderer.core,TR64_CORE_items)->count,maximum=0;
 if(renderer.rest_model_count) {
  uint32_t bytes=(items+7)/8;
  renderer.rest_mask=tr64_mem_alloc(TR64_MEM_state,bytes);memset(renderer.rest_mask,0,bytes);
  for(uint32_t i=0;i<renderer.rest_model_count;++i) {
   uint32_t id=renderer.rest_model_ids[i];
   if(id>=items || (renderer.rest_mask[id/8]&(1u<<(id%8))) ||
      (renderer.live_item_filter && renderer.live_instance_id==0x80000000u+id))
    sys_error("render rest item range/duplicate");
   renderer.rest_mask[id/8]|=(uint8_t)(1u<<(id%8));
   uint32_t n=game_rest_item_view(id,NULL,0);if(n>maximum)maximum=n;
  }
  if(maximum>256 || maximum>UINT32_MAX/sizeof(*renderer.rest_buffer))sys_error("render rest drawable capacity");
  if(maximum)renderer.rest_buffer=tr64_mem_alloc(TR64_MEM_state,maximum*sizeof(*renderer.rest_buffer));
 }
 renderer.rest_capacity=maximum;renderer.rest_prepared=1;
}
void tr64_render_menu_scene_frame(const tr64_gpu_view_t *camera)
{
 if(!renderer.bound || !camera)sys_error("menu renderer: level geometry/camera unavailable");
 menu_empty=1;menu_ready=0;
 const tr64_game_settings_t *s=game_settings_view ? game_settings_view() : NULL;
 tr64_gpu_set_filter(s && s->video_filter!=0);
 memset(&last_counts,0,sizeof(last_counts));last_counts.workspace_bytes=(uint32_t)renderer.workspace_bytes;
 tr64_gpu_frame_begin(camera,&menu_projection);
 tr64_gpu_frame_end(&last_counts.gpu);frame_ready=1;
}
void tr64_render_scene_menu_begin(const tr64_gpu_view_t *camera)
{
 if(!renderer.bound || menu_ready || !camera)sys_error("menu renderer: invalid pass");
 unsigned scale=basis_scale(camera->basis);
 if(!scale || scale==UINT_MAX)sys_error("menu renderer: invalid camera basis");
 menu_view_scale=scale;
 menu_pose.view=*camera;menu_pose.projection=menu_projection;
 menu_pose.camera_room=UINT32_MAX;menu_pose.flipped=0;
 memset(&menu_counts,0,sizeof(menu_counts));
 menu_counts.workspace_bytes=(uint32_t)renderer.workspace_bytes;
 menu_counts.matrix_slots=last_counts.matrix_slots;
 tr64_gpu_menu_begin(camera,!menu_empty);menu_ready=1;
}
void tr64_render_scene_pickups_begin(const tr64_gpu_view_t *camera,unsigned slot)
{
 if(!renderer.bound || !camera || slot>=TR64_GAME_PLAYERS)sys_error("split pickup pass");
 unsigned scale=basis_scale(camera->basis);
 if(!scale || scale==UINT_MAX)sys_error("split pickup camera basis");
 menu_view_scale=scale;
 menu_pose=(tr64_render_pose_t){.view=*camera,.projection=TR64_GPU_WORLD_PROJECTION,.camera_room=UINT32_MAX};
 tr64_render_half(&menu_pose,slot);
 memset(&menu_counts,0,sizeof(menu_counts));
 tr64_gpu_split_overlay(camera,&menu_pose.projection);menu_ready=1;
}
void tr64_render_scene_menu_draw(const tr64_game_drawable_t *parts,uint32_t count)
{
 tr64_render_error_t error;
 if(!menu_ready)sys_error("menu renderer: draw before pass");
 if(tr64_render_draw_items(&renderer,&menu_pose,parts,count,TR64_RENDER_ITEMS_MENU,
                           menu_view_scale,&menu_counts,&error))
  sys_error("menu render code=%lu section=%lu offset=%lu",(unsigned long)error.code,
            (unsigned long)error.section,(unsigned long)error.offset);
 if(menu_pose.projection.height==TR64_GPU_HEIGHT/2)tr64_gpu_split_matrices(menu_counts.matrix_slots);
}
void tr64_render_preview_toggle(void)
{
 preview_split^=1;
 log_printf("split-preview: requested=%u\n",(unsigned)preview_split);log_flush();
}
void tr64_render_preview_stop(void)
{
 if(preview_split)tr64_render_preview_toggle();
}
static int room_contains(uint32_t id,const tr64_render_pose_t *pose)
{
 const tr64_core_rooms_t *room=tr64_level_rooms(renderer.core,pose->flipped?mates(&renderer)[id]:id);
 const int32_t *eye=pose->view.eye;
 int64_t dx=(int64_t)eye[0]-tr64_i32(room->x),dz=(int64_t)eye[2]-tr64_i32(room->z);
 return dx>=0 && dx<(int32_t)tr64_u16(room->x_sectors)*1024 &&
        dz>=0 && dz<(int32_t)tr64_u16(room->z_sectors)*1024 &&
        eye[1]>=tr64_i32(room->bounds_top) && eye[1]<=tr64_i32(room->bounds_bottom);
}
static __attribute__((noinline,cold)) int preview_frame(const tr64_game_view_t *view,
                         tr64_game_drawable_t *buffer,uint32_t capacity,tr64_render_error_t *e)
{
 tr64_render_pose_t second={.projection=TR64_GPU_WORLD_PROJECTION,
  .camera_room=view->camera_room,.flipped=(uint8_t)view->flipped};
 memcpy(second.view.eye,view->camera_eye,sizeof(second.view.eye));
 memcpy(second.view.basis,view->camera_basis,sizeof(second.view.basis));
 /* R_y(pi) about the game camera's target; rotating the whole basis keeps
  * pitch, roll, arm length and height without a new look-at normalization. */
 if(view->camera_eye[0]!=view->camera_target[0] || view->camera_eye[2]!=view->camera_target[2]) {
  for(unsigned k=0;k<3;k+=2) {
   second.view.eye[k]=2*view->camera_target[k]-view->camera_eye[k];
   for(unsigned a=k;a<9;a+=3)second.view.basis[a]=-second.view.basis[a];
  }
  if(!room_contains(second.camera_room,&second))
   for(unsigned i=0;i<renderer.rooms.objects;++i)
    if(room_contains(i,&second)) { second.camera_room=i;break; }
 }
 tr64_render_half(&second,1);
 return tr64_render_game_split_frame(&renderer,view,&second,buffer,capacity,&last_counts,e);
}
void tr64_render_game_scene_frame(const tr64_game_view_t *view,
                                 tr64_game_drawable_t *buffer,uint32_t capacity)
{
 menu_empty=menu_ready=0;
 tr64_render_error_t error;
 tr64_game_view_t posed;
 if(!live_mode && TR64_RENDER_POSED_GAME_CAMERA) {
  posed=*view;memcpy(posed.camera_basis,fixture_pose.view.basis,sizeof(posed.camera_basis));
  memcpy(posed.camera_eye,fixture_pose.view.eye,sizeof(posed.camera_eye));
  posed.camera_room=fixture_pose.camera_room;posed.flipped=fixture_pose.flipped;view=&posed;
 }
 if(!renderer.bound)sys_error("game renderer: level geometry not bound");
 int result;
 if(!TR64_CART_TR2 && live_mode && preview_split && !game_cutscene_active())
  result=preview_frame(view,buffer,capacity,&error);
#if TR64_RENDER_SPLIT
 else if(!live_mode) {
  fixture_second.flipped=(uint8_t)view->flipped;
  result=tr64_render_game_split_frame(&renderer,view,&fixture_second,buffer,capacity,&last_counts,&error);
 }
#endif
 else result=tr64_render_game_frame(&renderer,view,buffer,capacity,&last_counts,&error);
 if(result)
  sys_error("game render code=%lu section=%lu offset=%lu",(unsigned long)error.code,
            (unsigned long)error.section,(unsigned long)error.offset);
 frame_ready=1;
#if TR64_RENDER_MEASURE
 log_measure(&last_counts);
#endif
 /* Performance-only probes keep one completion audit, not verbose frame logs. */
 if(&tr64_scene_test_no_hash && tr64_scene_test_no_hash) {
  log_printf("render-acquire: gross_us=%lu audio_us=%lu done_us=%lu\n",
   (unsigned long)last_counts.gpu.acquire_us,(unsigned long)last_counts.gpu.acquire_audio_us,
   (unsigned long)last_counts.gpu.gpu_done_us);
  return;
 }
 if(scene_hud_bound)
  log_printf("ui: submit_us=%lu\n",(unsigned long)tr64_ui_last_submit_us());
 log_printf("game-render: room=%lu eye=%ld,%ld,%ld drawables=%lu entities=%lu triangles=%lu matrices=%lu textures=%lu palettes=%lu cpu_us=%lu done_us=%lu items=%lu rest_models=%lu\n",
  (unsigned long)view->camera_room,(long)view->camera_eye[0],(long)view->camera_eye[1],(long)view->camera_eye[2],
  (unsigned long)last_counts.drawables_pulled,(unsigned long)last_counts.entity_instances,(unsigned long)last_counts.gpu.triangles,
  (unsigned long)last_counts.gpu.matrix_loads,(unsigned long)last_counts.gpu.texture_uploads,
  (unsigned long)last_counts.gpu.palette_loads,(unsigned long)last_counts.gpu.cpu_submit_us,
  (unsigned long)last_counts.gpu.gpu_done_us,(unsigned long)last_counts.item_instances,(unsigned long)renderer.rest_model_count);
 log_printf("render-phases: view_us=%lu pulls=%lu visibility_us=%lu cull_us=%lu stream_us=%lu submit_us=%lu expand_us=%lu texture_us=%lu matrix_us=%lu vertex_us=%lu recolour_us=%lu vertex_wait_us=%lu slot_waits=%lu command_bytes=%lu rdp_records=%lu encode_us=%lu batches=%lu rejected=%lu runs=%lu\n",
  (unsigned long)last_counts.view_us,(unsigned long)last_counts.room_pulls,
  (unsigned long)last_counts.visibility_us,(unsigned long)last_counts.culling_us,
  (unsigned long)last_counts.stream_scan_us,(unsigned long)last_counts.gpu_submit_us,
  (unsigned long)last_counts.gpu.expansion_us,(unsigned long)last_counts.gpu.texture_setup_us,
  (unsigned long)last_counts.gpu.matrix_us,(unsigned long)last_counts.gpu.vertex_us,
  (unsigned long)last_counts.gpu.vertex_colour_us,(unsigned long)last_counts.gpu.vertex_wait_us,
  (unsigned long)last_counts.gpu.vertex_slot_waits,(unsigned long)last_counts.gpu.triangle_command_bytes,
  (unsigned long)last_counts.gpu.rdp_record_bytes,(unsigned long)last_counts.gpu.matrix_encode_us,
  (unsigned long)last_counts.batches_tested,(unsigned long)last_counts.batches_rejected,(unsigned long)last_counts.stream_runs);
 log_flush();
 log_printf("sprites: rooms=%lu items=%lu tiles=%lu\n",(unsigned long)last_counts.room_sprites,
  (unsigned long)last_counts.item_sprites,(unsigned long)last_counts.sprite_tiles);log_flush();
 log_printf("texture-animation: requested=%lu applied=%lu cycle_words=%lu\n",
  (unsigned long)renderer.texture_requested_step,(unsigned long)renderer.texture_step,
  (unsigned long)renderer.texture_cycle_words);log_flush();
}
void tr64_render_game_scene_coop_frame(const tr64_game_view_t *first,const tr64_game_view_t *second,
                                      tr64_game_drawable_t *buffer,uint32_t capacity)
{
 menu_empty=menu_ready=0;
 tr64_render_error_t error;
 tr64_render_pose_t bottom={.projection=TR64_GPU_WORLD_PROJECTION,.camera_room=second->camera_room,
                            .flipped=(uint8_t)second->flipped};
 memcpy(bottom.view.basis,second->camera_basis,sizeof(bottom.view.basis));
 memcpy(bottom.view.eye,second->camera_eye,sizeof(bottom.view.eye));
 tr64_render_half(&bottom,1);
 if(first->flipped!=second->flipped)sys_error("co-op render: inconsistent flip");
 if(tr64_render_game_split_frame(&renderer,first,&bottom,buffer,capacity,&last_counts,&error))
  sys_error("co-op render code=%lu section=%lu offset=%lu",(unsigned long)error.code,
            (unsigned long)error.section,(unsigned long)error.offset);
 frame_ready=1;
#if TR64_RENDER_MEASURE
 log_measure(&last_counts);
#endif
}
const tr64_render_counts_t *tr64_render_scene_counts(void)
{ return frame_ready?&last_counts:NULL; }
int tr64_render_scene_work_stamp(uint32_t *completed_at_us,uint32_t *pacing_us)
{
 if(!frame_ready)return 0;
 *completed_at_us=last_counts.gpu.completed_at_us;*pacing_us=last_counts.gpu.pacing_us;
 return 1;
}
