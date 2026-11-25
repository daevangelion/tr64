/* SPDX-License-Identifier: MIT */
#ifndef TR64_GPU_H
#define TR64_GPU_H
#include <stdint.h>

/* Submission ABI: docs/formats/n64-geo.md. No SDK types cross this boundary.
 * All wire spans remain big-endian on both platforms. Matrices/view below
 * are host-order integers. Caller-owned spans live through frame_end(). */
enum {
 TR64_GPU_WIDTH=320, TR64_GPU_HEIGHT=240,
 TR64_GPU_FOCAL=256, TR64_GPU_NEAR=64, TR64_GPU_FAR=10240, TR64_GPU_FOG_NEAR=8192,
 TR64_GPU_FAR_MIN=4096, TR64_GPU_FAR_MAX=14336, TR64_GPU_FOG_NEAR_MIN=2048,
 TR64_GPU_VERTEX_LIMIT=70, TR64_GPU_PAIR_BYTES=32,
 TR64_GPU_FACE_BYTES=8, TR64_GPU_RUN_BYTES=24,
 TR64_GPU_TRIANGLE_COMMAND_BYTES=20,
 TR64_GPU_MATRIX_LIMIT=256, TR64_GPU_VERTEX_SLOTS=8,
 TR64_GPU_RDP_RECORD_BYTES=65536, TR64_GPU_UNIT_SHIFT=5,
 TR64_GPU_NORM_W=128, TR64_GPU_BASIS_ONE=16384
};
enum {
 TR64_GPU_OPAQUE=0, TR64_GPU_CUTOUT=1,
 /* Preserve source class; M1 uses colour-key/opaque depth writes, NOT STP
  * semitransparency. No source attribute is silently discarded. */
 TR64_GPU_BLENDED_M1=2,
 TR64_GPU_CLASS_MASK=3, TR64_GPU_DOUBLE_SIDED=4,
 TR64_GPU_FLAT=8, TR64_GPU_ONE_CYCLE_ELIGIBLE=16,
 TR64_GPU_NO_DEPTH=32, TR64_GPU_ADDITIVE=64
};
typedef struct {
 const uint8_t *data;
 uint32_t bytes;
} tr64_gpu_span_t;
typedef struct {
 int16_t left,top,right,bottom; /* Half-open pixel rectangle; empty if l>=r/t>=b. */
} tr64_gpu_rect_t;
typedef struct {
 /* Rows are right, down, forward, Q14; world Y points down. Eye is integer
  * world XYZ. Basis comes from the game or the engine's integer look-at. */
 int32_t basis[9],eye[3];
} tr64_gpu_view_t;
typedef struct {
 uint16_t width,height,focal_x,focal_y;
 int16_t centre_x,centre_y;
 /* M1 near clip64; far10240 is all-vertices rejection, not clipping.
  * Fog is per-vertex/clamped, screen-affine. Matrices use fixed1/32 units
  * and the exact screen/W/fog words in n64-geo.md, not viewport defaults. */
 uint16_t near_z,far_z,fog_near_z,fog_far_z;
} tr64_gpu_projection_t;
#define TR64_GPU_WORLD_PROJECTION {TR64_GPU_WIDTH,TR64_GPU_HEIGHT,TR64_GPU_FOCAL,TR64_GPU_FOCAL, \
 TR64_GPU_WIDTH/2,TR64_GPU_HEIGHT/2,TR64_GPU_NEAR,TR64_GPU_FAR,TR64_GPU_FOG_NEAR,TR64_GPU_FAR}
static inline tr64_gpu_rect_t tr64_gpu_projection_rect(const tr64_gpu_projection_t *p)
{
 int x=p->centre_x-p->width/2,y=p->centre_y-p->height/2;
 return (tr64_gpu_rect_t){x,y,x+p->width,y+p->height};
}
/* Room clips and batch planes use the original optical center. Only GPU
 * scissor submission translates them into framebuffer coordinates. */
static inline tr64_gpu_rect_t tr64_gpu_projection_clip(const tr64_gpu_projection_t *p)
{
 const int x=TR64_GPU_WIDTH/2-p->width/2,y=TR64_GPU_HEIGHT/2-p->height/2;
 return (tr64_gpu_rect_t){x,y,x+p->width,y+p->height};
}
static inline int tr64_gpu_projection_valid(const tr64_gpu_projection_t *p)
{
 if(!p || !p->width || !p->height || p->width>TR64_GPU_WIDTH || p->height>TR64_GPU_HEIGHT ||
    p->focal_x!=TR64_GPU_FOCAL || p->focal_y!=TR64_GPU_FOCAL || p->near_z!=TR64_GPU_NEAR ||
    p->far_z<TR64_GPU_FAR_MIN || p->far_z>TR64_GPU_FAR_MAX ||
    p->fog_far_z!=p->far_z || p->fog_near_z<TR64_GPU_FOG_NEAR_MIN || p->fog_near_z>=p->far_z)
  return 0;
 int x=p->centre_x-p->width/2,y=p->centre_y-p->height/2;
 return x>=0 && y>=0 && x+p->width<=TR64_GPU_WIDTH && y+p->height<=TR64_GPU_HEIGHT;
}
typedef struct {
 /* Local-to-world row-major Q14 plus INTEGER world translation. Backend
  * composes P*V*M on CPU with64-bit intermediates and eye-relative origin,
  * encodes1/32 homogeneous MVP, loads it against identity RSP projection.
  * Positive screen area(Y down) is front: CULL_FRONT in this +Z path;
  * neither cull flag is set for double-sided runs. */
 int32_t basis[9],translation[3];
 uint32_t instance_id; /* Unique in frame; stable across material switches. */
 int32_t light_offset; /* Signed darkness delta:4096 subtracts one gain unit.
                       * Rooms0; statics intensity-4096; actors normalized delta. */
 uint8_t view_space; /* Billboard matrices compose P*M, not P*V*M. */
 uint8_t water,mesh; /* Booleans. Room tint is already cooked; mesh is not. */
 int32_t logical_room;
} tr64_gpu_transform_t;
typedef struct {
 uint16_t material,content; /* Stable logical id and selected TX64 content (flat:equal). */
 uint8_t tile,flags; /* Tile ordinal; flags as above, including M1 class. */
} tr64_gpu_material_t;
enum { TR64_GPU_BATCH_TRANSIENT=1u<<30 };
enum { TR64_GPU_MAX_LIGHTS=2, TR64_GPU_BATCH_LIT=1u<<29 };
typedef struct {
 int32_t pos[3];
 int32_t radius;   /* 2048 for active flare/muzzle, 32 for flicker-blackout core */
 uint16_t peak;    /* 64..4096 from tr2-water-and-flares.md:184-190 */
 uint8_t shift;    /* 10..16: (22 - exp) for radius=2048, 0 for radius=32 */
 uint8_t active;
} tr64_gpu_light_t;
typedef struct {
 const uint8_t *vertices; /* Packed bytes; TRANSIENT copies them before return. */
 const uint8_t *faces; /* face_count*8 immutable compact bytes, never indices to IR. */
 const uint8_t *st; /* Four BE S,T words; integer texel centre k encodes32*k+16. */
 uint16_t vertex_count,face_count;
 uint32_t batch_id; /* Unique per frame; TRANSIENT for caller-lived vertices. */
} tr64_gpu_batch_t;
typedef struct {
 uint32_t polygons,triangles,vertex_loads,batch_submissions;
 uint32_t texture_uploads,palette_loads,triangle_command_bytes;
 uint32_t matrix_loads,state_changes;
 /* Timing starts just after obtaining the back buffer; frame_end waits for
  * BOTH RSP and RDP completion. Work is gpu_done_us, not CPU+GPU summed.
  * No pacing/log/capture is inside these intervals. Host times are not N64
  * performance evidence; it still returns the identical submission counts. */
 uint32_t cpu_submit_us,gpu_done_us,expansion_us,texture_setup_us,pacing_us;
 /* Captured post-completion sys_time_us stamp. pacing_us is acquisition time
  * minus measured resident audio service, so caller work includes that service.
  * Full caller work=(completed_at_us-caller_start)-pacing_us, unsigned wrap. */
 uint32_t completed_at_us;
 uint32_t vertex_slot_waits;
 /* CPU phase costs, including queue backpressure, not isolated RSP time. */
 uint32_t matrix_us,vertex_us,vertex_colour_us,vertex_wait_us;
 /* Cart-only MVP arithmetic, before uncached copy/queueing; host has no pack. */
 uint32_t matrix_encode_us;
 uint32_t rdp_record_bytes,rdp_record_peak,rdp_record_overflows;
 /* Cart acquisition audit: gross interval and audio pump spans on the same
  * sys_time_us clock. pacing_us = acquire_us - acquire_audio_us. */
 uint32_t acquire_us,acquire_audio_us;
 /* Performance overlay (engine/ui/perf.c). texture_upload_bytes: RDRAM bytes the frame's texture
  * loads read (image rows only; each palette load is another 32). The rdp_* fields are the RDP's
  * own counters (DPC_CLOCK, DPC_BUFBUSY, DPC_PIPEBUSY, DPC_TMEM; 24-bit RDP clock cycles) read
  * after the frame's sync, cleared at its start, only while the overlay is on: zero otherwise, and
  * zero on a platform whose registers do not count. */
 uint32_t texture_upload_bytes,rdp_clock,rdp_bufbusy,rdp_pipebusy,rdp_tmem;
} tr64_gpu_counts_t;
typedef struct {
 /* World-only counts in screen order. Populated only for split frames. */
 uint32_t first_triangles,first_texture_uploads,second_triangles,second_texture_uploads;
 uint32_t rdp_record_spills,rdp_spill_us; /* Split-only arena reuse and its full-drain wait. */
 uint8_t split;
 uint16_t matrix_peak; /* Highest live slot count between split fences. */
} tr64_gpu_split_counts_t;

/* Boot only, inside the platform's gpu allocation scope, before heap freeze.
 * Current platform surfaces provide the two 320x240 RGBA5551 framebuffers and
 * 16-bit Z. Neither backend allocates surfaces from a different pool. */
void tr64_gpu_init(void);
/* Textures are a validated resident TX64 section. Binding invalidates the
 * texture/palette caches. No copy, relocation, upload-command cache or
 * source-edition knowledge; changes write specialized passthrough words. */
/* Binding{NULL,0} releases level references only at a completed-frame boundary. */
void tr64_gpu_bind_textures(tr64_gpu_span_t textures);
/* begin obtains/clears back buffer+depth, sets view/projection, resets counts.
 * One view reclaims records at the frame fence only; split can also drain when full.
 * Eye must be within bind-time world bounds and matrix range must be proved. */
void tr64_gpu_frame_begin(const tr64_gpu_view_t *view,
                          const tr64_gpu_projection_t *projection);
/* Arm record reuse before the first split submission. The second view reuses
 * both records and matrices after a full GPU fence, without clearing colour/Z. */
void tr64_gpu_split_prepare(void);
void tr64_gpu_view_begin(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection);
void tr64_gpu_split_matrices(unsigned used);
/* Pickup overlay pass for one half. The first call clears Z, not colour; each
 * call fences the preceding pass before borrowing its matrix addresses. */
void tr64_gpu_split_overlay(const tr64_gpu_view_t *view,const tr64_gpu_projection_t *projection);
/* One menu pass inside an active frame: replace camera, invalidate all material
 * state/TMEM, optionally clear Z only, preserving colour and matrix slots,
 * records, fences and counters. clear_depth is0 for a freshly empty frame. */
void tr64_gpu_menu_begin(const tr64_gpu_view_t *view,int clear_depth);
/* During3D this appends a raw8B scissor record after t3d_tri_sync, submits
 * with the core-buffer command. Rectangles use the original optical center;
 * the backend applies the current view's framebuffer offset. */
void tr64_gpu_scissor(tr64_gpu_rect_t rectangle);
/* Selecting an existing instance reuses its stable matrix until end. Changing
 * values for the same instance_id within a frame is an error. */
void tr64_gpu_matrix(const tr64_gpu_transform_t *transform);
/* All texture classes alpha-test at1, depth-test/write; class2 is not STP
 * blending in M1. Flat is opaque. On a change, t3d_tri_sync first; construct
 * image64B/palette48B raw records only when their TMEM contents change.
 * Image identity is payload offset, source row bytes, rectangle origin/extent,
 * not logical descriptor id (UV variants may share one image). Palette loads
 * remain independently cached across16CI4 TLUT slots (id&15, full-id tags).
 * Enqueue with the core-buffer command, NOT another overlay. Point ST has a
 * half-texel bias. Local X is even; the last upload byte may have one
 * nibble beyond the exact drawable width. Gain capped1.5, PRIM RGB128. */
void tr64_gpu_material(tr64_gpu_material_t material);
/* Submit one batch's current material run. Expand the compact faces directly
 * into20-byte commands on cart; host consumes the same corners and provenance.
 * Each quad emits(0,1,2),(0,2,3). Reload vertices only on batch/instance change;
 * same-batch material changes retain it. Sync before matrix/vertex change.
 * Scratch DMA is fenced; no per-frame malloc or source-UV computation.
 * A split frame may drain/reuse its record arena; other overflow is fatal. */
void tr64_gpu_batch(const tr64_gpu_batch_t *batch);
/* Store active per-view dynamic lights (cleared on every begin_3d_pass/begin_view). */
void tr64_gpu_set_lights(const tr64_gpu_light_t *lights,unsigned count);
/* Enable cyan water tint on air rooms/meshes when the camera is submerged. */
void tr64_gpu_set_camera_water(int underwater);
/* Select 3D texture filter (0 = Point, nonzero = Bilinear); applied on the
 * next 3D pass begin. Defaults to 0 (Point). */
void tr64_gpu_set_filter(int bilinear);
void tr64_gpu_frame_end(tr64_gpu_counts_t *counts);
/* Extras for the most recently completed split frame, until the next begin. */
const tr64_gpu_split_counts_t *tr64_gpu_split_counts(void);
/* Scalar observer for schema-independent callers; outputs must be non-NULL.
 * Returns1 for the last completed scene frame,0 before one or after release. */
int tr64_render_scene_work_stamp(uint32_t *completed_at_us,uint32_t *pacing_us);

#ifdef TR64_HOST
/* Test-only capture export after frame_end, before the next begin. The host
 * backend writes stem.png (RGB320x240), stem.npz (coverage, room, texture,
 * depth, texel per camera-and-projection.md), and stem.json. map pixel units:
 * camera-Z world units; continuous texels relative to original face UV minima.
 * Source identity/pose/visible-room/static metadata is supplied as a JSON
 * object string, copied into stem.json with backend counts/write observables.
 * Return0 on success, nonzero on I/O/encoding error; never omit a map and pass.
 * Host diagnostic arrays/export memory are reported separately, never passed
 * off as cart-resident pool capacity. No alternate geometry/render entrypoint. */
int tr64_gpu_host_capture(const char *stem,const char *scene_metadata_json);
#endif
#endif
