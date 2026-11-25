/* SPDX-License-Identifier: MIT */
#include <libdragon.h>
#include "engine/ui/image.h"
void tr64_ui_backend_image(const tr64_ui_image_t *image)
{
 const int indexed=image->format==TR64_UI_IMAGE_CI8;
 const unsigned row_bytes=indexed?320:640;
 const unsigned tmem=indexed?2048:4096;
 const unsigned rows=tmem/row_bytes;
 surface_t source=surface_make((void *)image->pixels,indexed?FMT_CI8:FMT_RGBA16,
                                320,240,row_bytes);
 rdpq_set_scissor(0,0,320,240);
 rdpq_set_mode_copy(false);
 rdpq_mode_tlut(indexed?TLUT_RGBA16:TLUT_NONE);
 if(indexed)rdpq_tex_upload_tlut((uint16_t *)image->palette,0,256);
 for(unsigned y=0;y<240;y+=rows) {
  unsigned end=y+rows;if(end>240)end=240;
  rdpq_tex_upload_sub(TILE0,&source,NULL,0,y,320,end);
  __rdpq_texture_rectangle_fx(TILE0,0,y*4,320*4,end*4,16,y*32+16);
 }
}
