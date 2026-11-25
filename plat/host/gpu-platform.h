/* SPDX-License-Identifier: MIT */
#ifndef TR64_HOST_GPU_PLATFORM_H
#define TR64_HOST_GPU_PLATFORM_H
#include <stdint.h>
/* UI adapter access only during tr64_ui_gpu_draw:320-pixel stride,240rows,
 * native uint16 RGBA5551 values. Borrowed until the frame completes. */
uint16_t *tr64_gpu_host_pixels(void);
#endif
