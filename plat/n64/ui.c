/* SPDX-License-Identifier: MIT */
#include <libdragon.h>
#include "engine/ui/ui.h"
#include "engine/core/sys.h"
#include "plat/n64/gpu-platform.h"
#include "build_id.h"
static surface_t *message_surface;
static int mode;
static const uint8_t *last_palette;
static color_t colour(uint16_t c)
{
 unsigned r=c>>11,g=(c>>6)&31,b=(c>>1)&31;
 return RGBA32((r<<3)|(r>>2),(g<<3)|(g>>2),(b<<3)|(b>>2),255);
}
#if TR64_TEST
/* Measurement alternatives exist only in the harness build. They use the
 * same UI rectangles and target, never a separate renderer or frame. */
static unsigned test_rect_path;
void tr64_ui_test_rect_path(unsigned path)
{
 if(path>2)sys_error("UI test rectangle path");
 test_rect_path=path;
}
static void test_rectangle(int x,int y,int right,int bottom,uint16_t c)
{
 unsigned target=test_rect_path+2;
 if(mode!=(int)target) {
  rdpq_set_mode_standard();rdpq_mode_zbuf(false,false);
  rdpq_mode_tlut(TLUT_NONE);
  if(test_rect_path==1)rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
  else {
   static const uint64_t white __attribute__((aligned(16)))=UINT64_MAX;
   surface_t texture=surface_make_linear((void *)&white,FMT_RGBA16,4,1);
   rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(0,0,0,TEX0)));
   rdpq_mode_alphacompare(1);
   rdpq_tex_upload(TILE0,&texture,NULL);
  }
  mode=(int)target;last_palette=NULL;
 }
 rdpq_set_prim_color(colour(c));
 if(test_rect_path==1)rdpq_fill_rectangle(x,y,right,bottom);
 else __rdpq_texture_rectangle_scaled_fx(TILE0,x*4,y*4,right*4,bottom*4,16,16,48,48);
}
#endif
void tr64_ui_backend_begin(void)
{
 rdpq_set_scissor(0,0,320,240);
 mode=-1;last_palette=NULL;
}
void tr64_ui_backend_end(void) { }
void tr64_ui_backend_screen_begin(void)
{
 if(message_surface)sys_error("UI message frame already held");
 message_surface=display_get();
 rdpq_attach(message_surface,NULL);
 tr64_ui_backend_begin();
 tr64_ui_backend_rect(0,0,320,240,1);
}
void tr64_ui_backend_screen_end(void)
{
 tr64_ui_backend_end();rdpq_detach();rspq_wait();
 sys_gpu_present(message_surface);message_surface=NULL;
}
void tr64_ui_backend_rect(int x,int y,int w,int h,uint16_t c)
{
 if(w<=0 || h<=0 || x>=320 || y>=240 || x+w<=0 || y+h<=0)return;
 int right=x+w,bottom=y+h;
 if(x<0)x=0;
 if(y<0)y=0;
 if(right>320)right=320;
 if(bottom>240)bottom=240;
#if TR64_TEST
 if(test_rect_path) { test_rectangle(x,y,right,bottom,c);return; }
#endif
 if(mode) { rdpq_set_mode_fill(colour(c));mode=0;last_palette=NULL; }
 else rdpq_set_fill_color(colour(c));
 rdpq_fill_rectangle(x,y,right,bottom);
}
void tr64_ui_backend_shade(int x,int y,int w,int h,unsigned keep)
{
 if(keep>32)sys_error("UI shade coefficient");
 if(keep==32 || w<=0 || h<=0 || x>=320 || y>=240 || x+w<=0 || y+h<=0)return;
 if(!keep) { tr64_ui_backend_rect(x,y,w,h,1);return; }
 int right=x+w,bottom=y+h;
 if(x<0)x=0;
 if(y<0)y=0;
 if(right>320)right=320;
 if(bottom>240)bottom=240;
 if(mode!=2) {
  rdpq_set_mode_standard();rdpq_mode_zbuf(false,false);
  rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
  rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
  rdpq_set_prim_color(RGBA32(0,0,0,255));
  mode=2;last_palette=NULL;
 }
 /* FORCE_BLEND bypasses normalization and biases the memory weight by1.
  * Program its inverse-alpha input to keep-1, yielding exactly keep/32. */
 rdpq_set_fog_color(RGBA32(0,0,0,255-8*(keep-1)));
 rdpq_fill_rectangle(x,y,right,bottom);
}
void tr64_ui_backend_sprite(int x,int y,int w,int h,const tr64_ui_sprite_t *s)
{
 if(w<=0 || h<=0 || x>=320 || y>=240 || x+w<=0 || y+h<=0)return;
 if(mode!=1) {
  rdpq_set_mode_standard();rdpq_mode_zbuf(false,false);
  rdpq_mode_combiner(RDPQ_COMBINER_TEX);rdpq_mode_tlut(TLUT_RGBA16);
  rdpq_mode_alphacompare(1);mode=1;
 }
 unsigned pitch=((s->width/2)+7)&~7u;
 if((s->source_x&1) || (s->width&1) || pitch*s->height>2048)
  sys_error("UI glyph TMEM rectangle");
 surface_t image=surface_make((void *)s->image,FMT_CI4,s->image_width,s->image_height,s->stride);
 if(last_palette!=s->palette) {
  rdpq_tex_upload_tlut((uint16_t *)s->palette,0,16);last_palette=s->palette;
 }
 rdpq_tex_upload_sub(TILE0,&image,NULL,s->source_x,s->source_y,
                     s->source_x+s->width,s->source_y+s->height);
 __rdpq_texture_rectangle_scaled_fx(TILE0,x*4,y*4,(x+w)*4,(y+h)*4,
  s->source_x*32+16,s->source_y*32+16,
  (s->source_x+s->width)*32+16,(s->source_y+s->height)*32+16);
}
