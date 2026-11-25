/* SPDX-License-Identifier: MIT */
#ifndef TR64_N64_GPU_PLATFORM_H
#define TR64_N64_GPU_PLATFORM_H
#include <libdragon.h>
/* Publish a display_get() framebuffer after GPU completion. No pixel copy;
 * the same surface becomes the guest screenshot source. */
void sys_gpu_present(surface_t *surface);
#endif
