/* SPDX-License-Identifier: MIT */
#ifndef TR64_UI_IMAGE_H
#define TR64_UI_IMAGE_H
#include <stdint.h>
#include "tr64/level.h"
enum { TR64_UI_IMAGE_CI8=1, TR64_UI_IMAGE_RGBA16=2 };
/* UI64 is fixed 320x240 with tight rows. These borrowed pointers remain in
 * the planned level arena; title presentation never makes a screen copy. */
typedef struct {
 const uint8_t *pixels,*palette;
 uint32_t format;
} tr64_ui_image_t;
/* Cold presentation-unit calls only, while a production GPU frame is open. */
void tr64_ui_title_image_draw(const tr64_level_t *level);
void tr64_ui_backend_image(const tr64_ui_image_t *image);
#endif
