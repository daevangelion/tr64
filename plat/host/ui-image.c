/* SPDX-License-Identifier: MIT */
#include "engine/ui/image.h"
#include "plat/host/gpu-platform.h"
#include "engine/core/sys.h"
static uint16_t be16(const uint8_t *p)
{ return (uint16_t)((unsigned)p[0]*256+p[1]); }
void tr64_ui_backend_image(const tr64_ui_image_t *image)
{
 uint16_t *pixels=tr64_gpu_host_pixels();
 if(!pixels)sys_error("UI title image outside frame");
 if(image->format==TR64_UI_IMAGE_CI8) {
  for(unsigned i=0;i<320u*240u;++i)
   pixels[i]=be16(image->palette+2*image->pixels[i]);
 } else {
  for(unsigned i=0;i<320u*240u;++i)pixels[i]=be16(image->pixels+2*i);
 }
}
