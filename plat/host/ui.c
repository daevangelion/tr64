/* SPDX-License-Identifier: MIT */
#include "engine/ui/ui.h"
#include "plat/host/gpu-platform.h"
#include "engine/core/sys.h"
#include "build_id.h"
#if TR64_TEST
void tr64_ui_test_rect_path(unsigned path)
{ if(path>2)sys_error("UI test rectangle path"); }
#endif
static uint16_t *pixels;
static uint16_t be16(const uint8_t *p)
{ return (uint16_t)((unsigned)p[0]*256+p[1]); }
void tr64_ui_backend_begin(void)
{
 pixels=tr64_gpu_host_pixels();
 if(!pixels)sys_error("UI host overlay without frame");
}
void tr64_ui_backend_end(void) { pixels=0; }
void tr64_ui_backend_screen_begin(void)
{
 tr64_ui_backend_begin();
 tr64_ui_backend_rect(0,0,320,240,1);
}
void tr64_ui_backend_screen_end(void)
{
 sys_present(pixels);tr64_ui_backend_end();
}
void tr64_ui_backend_rect(int x,int y,int w,int h,uint16_t colour)
{
 if(w<=0 || h<=0 || x>=320 || y>=240 || x+w<=0 || y+h<=0)return;
 int right=x+w,bottom=y+h;
 if(x<0)x=0;
 if(y<0)y=0;
 if(right>320)right=320;
 if(bottom>240)bottom=240;
 for(int row=y;row<bottom;++row)
  for(int col=x;col<right;++col)pixels[row*320+col]=colour;
}
void tr64_ui_backend_sprite(int x,int y,int w,int h,const tr64_ui_sprite_t *s)
{
 if(w<=0 || h<=0 || x>=320 || y>=240 || x+w<=0 || y+h<=0)return;
 int left=x<0?0:x,top=y<0?0:y;
 int right=x+w>320?320:x+w,bottom=y+h>240?240:y+h;
 for(int row=top;row<bottom;++row) {
  unsigned sy=s->source_y+(unsigned)(row-y)*s->height/(unsigned)h;
  for(int col=left;col<right;++col) {
   unsigned sx=s->source_x+(unsigned)(col-x)*s->width/(unsigned)w;
   unsigned byte=s->image[sy*s->stride+sx/2];
   unsigned index=(sx&1)?byte&15:byte>>4;
   uint16_t colour=be16(s->palette+2*index);
   if(colour&1)pixels[row*320+col]=colour;
  }
 }
}
static unsigned shade_channel(unsigned c,unsigned keep)
{
 /* FORCE_BLEND's biased memory term and RGB555 memory expansion simplify
  * to an exact five-bit multiply for a black source rectangle. */
 return c*keep>>5;
}
void tr64_ui_backend_shade(int x,int y,int w,int h,unsigned keep)
{
 if(keep>32)sys_error("UI shade coefficient");
 if(keep==32 || w<=0 || h<=0 || x>=320 || y>=240 || x+w<=0 || y+h<=0)return;
 int right=x+w,bottom=y+h;
 if(x<0)x=0;
 if(y<0)y=0;
 if(right>320)right=320;
 if(bottom>240)bottom=240;
 for(int row=y;row<bottom;++row)for(int col=x;col<right;++col) {
  unsigned c=pixels[row*320+col];
  unsigned r=shade_channel(c>>11,keep),g=shade_channel((c>>6)&31,keep),b=shade_channel((c>>1)&31,keep);
  pixels[row*320+col]=(uint16_t)(r<<11|g<<6|b<<1|1);
 }
}
void tr64_ui_backend_glyphs(const tr64_ui_glyph_t *glyphs,unsigned count,
                            tr64_ui_sprite_t (*font)(unsigned glyph))
{
 for(unsigned i=0;i<count;++i) {
  tr64_ui_sprite_t s=font(glyphs[i].glyph);
  tr64_ui_backend_sprite(glyphs[i].x,glyphs[i].y,s.right-s.left,s.bottom-s.top,&s);
 }
}
