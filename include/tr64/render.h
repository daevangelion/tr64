/* SPDX-License-Identifier: MIT */
#ifndef TR64_RENDER_H
#define TR64_RENDER_H
#include <stddef.h>
#include <stdint.h>
#include "tr64/gpu.h"
#include "tr64/level.h"
#include "tr64/game.h"

/* RG64/MG64 wire offsets/strides, not native C structure casts. */
enum {
 TR64_GEO_HEADER_BYTES=64, TR64_GEO_OBJECT_BYTES=32,
 TR64_GEO_BATCH_BYTES=32, TR64_GEO_RUN_BYTES=24, TR64_GEO_FACE_BYTES=8,
 TR64_GEO_VERSION=1, TR64_GEO_COMMAND_ABI=1,
 TR64_GEO_VERSION_AT=4, TR64_GEO_BYTES_AT=8,
 TR64_GEO_OBJECT_COUNT_AT=12, TR64_GEO_BATCH_COUNT_AT=16,
 TR64_GEO_RUN_COUNT_AT=20, TR64_GEO_FACE_COUNT_AT=24,
 TR64_GEO_PAIR_COUNT_AT=28, TR64_GEO_OBJECTS_AT=32,
 TR64_GEO_BATCHES_AT=36, TR64_GEO_RUNS_AT=40,
 TR64_GEO_FACES_AT=44, TR64_GEO_VERTICES_AT=48,
 TR64_GEO_TRIANGLE_COUNT_AT=52, TR64_GEO_COMMAND_ABI_AT=56
};
enum {
 TR64_RENDER_SECTION_CORE, TR64_RENDER_SECTION_TEXTURES,
 TR64_RENDER_SECTION_ROOMS, TR64_RENDER_SECTION_MESHES
};
enum {
 TR64_RENDER_OK, TR64_RENDER_TRUNCATED, TR64_RENDER_VERSION,
 TR64_RENDER_LAYOUT, TR64_RENDER_REFERENCE, TR64_RENDER_FLAGS,
 TR64_RENDER_BOUNDS, TR64_RENDER_WORKSPACE, TR64_RENDER_CAMERA
};
typedef struct {
 uint32_t code,section,offset;
} tr64_render_error_t;
typedef struct {
 tr64_gpu_span_t bytes;
 uint32_t objects,batches,runs,faces,pairs,triangles;
 uint32_t object_offset,batch_offset,run_offset,face_offset,vertex_offset;
} tr64_geo_t;
typedef struct {
 const tr64_level_t *core;
 tr64_gpu_span_t textures;
 tr64_geo_t rooms,meshes;
 void *workspace;
 size_t workspace_bytes;
 int32_t world_min[3],world_max[3]; /* Bind-time room/static range proof. */
 uint32_t rest_items; /* Scene-only initial core placements:0 off, maximum visible count. */
 uint16_t *texture_map,*texture_cycles; /* State-owned native-id permutation. */
 uint32_t texture_cycle_words,texture_step,texture_requested_step;
 const uint32_t *rest_model_ids;
 tr64_game_drawable_t *rest_buffer;
 uint8_t *rest_mask;
 uint32_t rest_model_count,rest_capacity,live_instance_id;
 uint8_t live_item_filter,rest_prepared,static_shade_offset;
 uint32_t bound;
} tr64_render_t;
typedef struct {
 tr64_gpu_view_t view;
 tr64_gpu_projection_t projection;
 uint32_t camera_room;
 uint8_t flipped;
} tr64_render_pose_t;
typedef struct {
 tr64_gpu_counts_t gpu;
 uint32_t visible_rooms,static_instances,portal_tests,clip_growths;
 uint32_t batches_tested,batches_rejected;
 uint32_t workspace_bytes;
 uint32_t visibility_us,culling_us,stream_scan_us,gpu_submit_us,stream_runs;
 uint32_t entity_instances;
 uint32_t room_sprites,item_sprites,sprite_tiles;
 uint32_t matrix_slots;
 uint32_t item_instances;
 uint32_t drawables_pulled,room_pulls,view_us;
 /* Animated-texture ranges rotated at this frame's start (0 when no tick advanced the animation), and
  * the rotation steps they were advanced by. */
 uint32_t anim_ranges,anim_steps;
 tr64_gpu_split_counts_t split;
} tr64_render_counts_t;

/* These three loading facades require Core's active tr64_load_scope lease.
 * They dispatch on every call; no unit function pointer is retained.
 * Validation is byte-safe; inputs are borrowed/immutable. Errors name section
 * and byte offset; no fallback empty scene. */
int tr64_geo_open(tr64_geo_t *out,tr64_gpu_span_t section,int meshes,
                  tr64_render_error_t *error);
/* State-pool workspace includes room clips, flip mates, integer work queue,
 * batch visibility and visible-instance records. Checked arithmetic; SIZE_MAX
 * means overflow/invalid table counts. No recursive portal traversal/list cap. */
size_t tr64_render_workspace_bytes(const tr64_level_t *core,
                                  const tr64_geo_t *rooms,
                                  const tr64_geo_t *meshes);
/* core must be ready through Core's real loader, and all three native spans
 * resident in their named pools. No input source files, synchronous side load,
 * hidden unpacked geometry or heap allocations. Workspace comes from state.
 * bind validates cross-section references, including reciprocal flip mates. */
int tr64_render_bind(tr64_render_t *render,const tr64_level_t *core,
                     tr64_gpu_span_t textures,tr64_gpu_span_t rooms,
                     tr64_gpu_span_t meshes,void *workspace,size_t bytes,
                     tr64_render_error_t *error);
/* Integer look-at: Q14 basis from integer world eye and signed16.16 LOOK
 * VECTOR (target-eye), preserving decimal JSON targets without overflowing
 * absolute-world16.16. Reject zero/vertical look. Integer sqrt normalization,
 * divisions truncate toward zero; right=cross(+Y,forward), down=cross(f,r).
 * M1 focal256 centre160,120 near64 far-reject10240 fog8192..10240. */
int tr64_render_camera(tr64_render_pose_t *out,const int32_t position[3],
                       const int32_t look_q16[3],uint32_t room,uint8_t flipped,
                       tr64_render_error_t *error);
/* Framebuffer layout for the split instrument and its standalone half check. */
void tr64_render_half(tr64_render_pose_t *pose,unsigned lower);
/* Integer portal fixed-point traversal (requeue only on rectangle growth),
 * conservative integer batch-frustum/portal rejection, stored-order/batch-major
 * submission of reachable room geometry and visible-definition statics.
 * Calls begin/end once; counts cover actual engine path, not a separate probe.
 * Frame failure is explicit; do not return success after a partial picture. */
int tr64_render_frame(tr64_render_t *render,const tr64_render_pose_t *pose,
                      tr64_render_counts_t *counts,tr64_render_error_t *error);
int tr64_render_split_frame(tr64_render_t *render,const tr64_render_pose_t *top,
                            const tr64_render_pose_t *bottom,tr64_render_counts_t *counts,
                            tr64_render_error_t *error);
/* Pull whole-item groups only for visible rooms into caller-owned reusable
 * storage (normally TR64_GAME_DRAW_CHUNK drawables). Pose generation is inside
 * the frame timing. Neither the buffer nor camera snapshot is retained. */
int tr64_render_game_frame(tr64_render_t *render,const tr64_game_view_t *view,
                           tr64_game_drawable_t *buffer,uint32_t capacity,
                           tr64_render_counts_t *counts,tr64_render_error_t *error);
/* The first camera and animation clock are from view; second is render only.
 * Visibility workspace is reused; the observer returns the union of both views. */
int tr64_render_game_split_frame(tr64_render_t *render,const tr64_game_view_t *view,
                                 const tr64_render_pose_t *second,tr64_game_drawable_t *buffer,
                                 uint32_t capacity,tr64_render_counts_t *counts,
                                 tr64_render_error_t *error);
/* Prepare optional Core-rest fixtures once after game start/place,
 * while loading. Live rendering never allocates or changes simulation. */
void tr64_render_game_scene_prepare(void);
/* Select shipping live binding before the first load; default is posed scenes.
 * Release at a completed frame boundary, before level/state pool reset. */
void tr64_render_scene_set_live(int live);
void tr64_render_scene_release(void);
/* Scene-bound renderer; fatal on missing geometry or invalid live view.
 * Posed-camera override is permitted only by an explicit fixture setting. */
void tr64_render_game_scene_frame(const tr64_game_view_t *view,
                                 tr64_game_drawable_t *buffer,uint32_t capacity);
void tr64_render_game_scene_coop_frame(const tr64_game_view_t *first,const tr64_game_view_t *second,
                                      tr64_game_drawable_t *buffer,uint32_t capacity);
void tr64_render_preview_toggle(void);
void tr64_render_preview_stop(void);
/* Empty scene frame for TITLE, selected by the caller's explicit title context,
 * never room count. The normal GPU overlay hook supplies background/ring/text. */
void tr64_render_menu_scene_frame(const tr64_gpu_view_t *camera);
/* Inside that hook: one roomless mesh pass over the preserved colour buffer,
 * clear Z once (not again for a fresh empty frame), retain dense matrix slots.
 * Parts use Game's menu namespace/UINT32_MAX room, zero water and mesh kind.
 * All state and caller buffers are borrowed only until frame completion. */
void tr64_render_scene_menu_begin(const tr64_gpu_view_t *camera);
/* A pickup-only 3D pass clipped to one half of the screen. */
void tr64_render_scene_pickups_begin(const tr64_gpu_view_t *camera,unsigned slot);
void tr64_render_scene_menu_draw(const tr64_game_drawable_t *parts,uint32_t count);
/* Last completed scene frame, or NULL before the first completed frame. */
const tr64_render_counts_t *tr64_render_scene_counts(void);
/* Last frame's logical room ids, ascending, into caller storage for scene
 * metadata. Return required count; write only when capacity is sufficient. */
uint32_t tr64_render_visible_rooms(const tr64_render_t *render,
                                   uint16_t *out,uint32_t capacity);
#endif
