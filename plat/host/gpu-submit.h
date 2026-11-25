/* SPDX-License-Identifier: MIT */
#ifndef TR64_HOST_GPU_SUBMIT_H
#define TR64_HOST_GPU_SUBMIT_H
#include "tr64/gpu.h"

/* Synchronous consumer boundary for the software rasterizer. All fields and
 * command words are host-order except textures/packed vertices, which retain
 * the native big-endian format. Pointers are borrowed until callback return.
 * Vertex RGBA is cooked baseline data; matrix.light_offset/water describe the
 * instance adjustment, and view/projection describe the native transform. */
typedef struct {
 tr64_gpu_view_t view;
 tr64_gpu_projection_t projection;
 tr64_gpu_rect_t scissor;
 tr64_gpu_transform_t matrix;
 tr64_gpu_material_t material;
 tr64_gpu_span_t textures;
 const uint8_t *vertices;
 uint32_t batch_id;
 uint16_t vertex_count;
} tr64_gpu_host_state_t;
typedef struct {
 uint32_t words[5]; /* Exact 20-byte corner-UV command, in host word order. */
 uint16_t source_polygon;
 uint8_t source_corner_map;
 uint8_t indices[3];
} tr64_gpu_host_triangle_t;

/* A rasterizer implements these hooks. Without one, the backend still validates
 * and expands actual submissions/counts, but never presents a fabricated image.
 * The triangle hook receives quads as (0,1,2), then (0,2,3), exactly as on cart. */
void tr64_gpu_host_begin_frame(const tr64_gpu_host_state_t *state);
/* Clear only depth between world and inventory passes; keep colour/fill counts. */
void tr64_gpu_host_clear_depth(void);
void tr64_gpu_host_emit_triangle(const tr64_gpu_host_state_t *state,
                                  const tr64_gpu_host_triangle_t *triangle);
void tr64_gpu_host_end_frame(const tr64_gpu_host_state_t *state);
int tr64_gpu_host_capture_sink(const char *stem,const char *metadata);
/* Optional diagnostic, called only after the GPU timing interval ends. */
void tr64_gpu_host_log_fill(void);
#endif
