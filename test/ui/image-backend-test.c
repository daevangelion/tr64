/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine/ui/image.h"
static uint8_t blob[153632];
static uint16_t pixels[320*240];
static uint32_t blob_bytes;
uint16_t *tr64_gpu_host_pixels(void) { return pixels; }
const uint8_t *tr64_level_title_image(const tr64_level_t *level,uint32_t *bytes)
{ assert(level && level->ready);*bytes=blob_bytes;return blob; }
void sys_error(const char *format,...)
{ va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);abort(); }
static void put16(uint8_t *p,uint16_t value) { p[0]=value>>8;p[1]=value; }
static void put32(uint8_t *p,uint32_t value)
{ p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value; }
int main(void)
{
 tr64_level_t level={.ready=1};
 blob_bytes=77344;put32(blob+20,TR64_UI_IMAGE_CI8);
 for(unsigned i=0;i<256;++i)put16(blob+76832+2*i,(uint16_t)(i*126+1));
 for(unsigned i=0;i<320*240;++i)blob[32+i]=(uint8_t)i;
 tr64_ui_title_image_draw(&level);
 for(unsigned i=0;i<320*240;++i)assert(pixels[i]==(uint16_t)((i&255)*126+1));
 blob_bytes=153632;put32(blob+20,TR64_UI_IMAGE_RGBA16);
 for(unsigned i=0;i<320*240;++i)put16(blob+32+2*i,(uint16_t)(i*2+1));
 tr64_ui_title_image_draw(&level);
 for(unsigned i=0;i<320*240;++i)assert(pixels[i]==(uint16_t)(i*2+1));
 puts("UI title image consumer PASS: actual host CI8/palette and RGBA16 paths, all 76800 pixels each");
 return 0;
}
