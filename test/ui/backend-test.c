/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine/ui/ui.h"
static uint16_t pixels[320*240];
static int presented;
uint16_t *tr64_gpu_host_pixels(void) { return pixels; }
void sys_present(const uint16_t *p) { assert(p==pixels);++presented; }
void sys_error(const char *format,...)
{ va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);abort(); }
int main(void)
{
 static const uint8_t image[]={0x12,0x01,0x21,0x10};
 static const uint8_t palette[32]={0,0,0xf8,1,0x07,0xc1};
 tr64_ui_sprite_t sprite={image,palette,4,2,2,0,0,4,2,0,0,4,2};
 tr64_ui_backend_screen_begin();
 tr64_ui_backend_rect(-1,-1,3,3,0x1235);
 assert(pixels[0]==0x1235 && pixels[321]==0x1235 && pixels[322]==1);
 tr64_ui_backend_sprite(10,10,4,2,&sprite);
 assert(pixels[10*320+10]==0xf801 && pixels[10*320+11]==0x07c1);
 assert(pixels[10*320+12]==1 && pixels[10*320+13]==0xf801);
 tr64_ui_backend_sprite(-2,20,4,2,&sprite);
 assert(pixels[20*320]==1 && pixels[20*320+1]==0xf801);
 tr64_ui_backend_sprite(20,20,8,4,&sprite);
 assert(pixels[20*320+20]==0xf801 && pixels[20*320+21]==0xf801);
 assert(pixels[20*320+22]==0x07c1 && pixels[21*320+22]==0x07c1);
 for(unsigned keep=10;keep<=14;keep+=4) {
  for(unsigned i=0;i<32;++i)pixels[100+i]=(uint16_t)(i<<11|i<<6|i<<1|1);
  tr64_ui_backend_shade(100,0,32,1,keep);
  for(unsigned i=0;i<32;++i) {
   unsigned c=i*keep/32;
   assert(pixels[100+i]==(uint16_t)(c<<11|c<<6|c<<1|1));
  }
 }
 tr64_ui_backend_screen_end();assert(presented==1);
 puts("UI host backend PASS: production CI4/palette sampling, alpha, scale, clip, screen lifetime");
 return 0;
}
