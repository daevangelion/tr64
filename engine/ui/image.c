/* SPDX-License-Identifier: MIT */
#include "engine/ui/image.h"
#include "engine/core/sys.h"
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
void tr64_ui_title_image_draw(const tr64_level_t *level)
{
 uint32_t bytes;
 const uint8_t *blob=tr64_level_title_image(level,&bytes);
 if(!blob)sys_error("UI title image unavailable");
 /* Core validates UI64 before publication. The exact sizes below also keep
  * this consumer from silently drawing a different image ABI. */
 uint32_t format=be32(blob+20);
 if((format!=TR64_UI_IMAGE_CI8 && format!=TR64_UI_IMAGE_RGBA16) ||
    bytes!=(format==TR64_UI_IMAGE_CI8?77344u:153632u))
  sys_error("UI title image ABI");
 tr64_ui_image_t image={blob+32,format==TR64_UI_IMAGE_CI8?blob+76832:NULL,format};
 tr64_ui_backend_image(&image);
}
