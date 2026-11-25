/* SPDX-License-Identifier: MIT */
/* 3x5 project-owned diagnostic font from engine/test/card.c, ac3n64 04134cc. */
#include <libdragon.h>
#include <string.h>
#include "plat/n64/gpu-platform.h"
static int message_ready;
void tr64_play_message_ready(int ready) { message_ready=ready; }

static const char glyphs[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-/:";
static const uint16_t glyph[]={
 31599,11415,29671,29647,23497,31183,31215,29257,31727,31695,
 11245,27566,31015,27502,31207,31204,31083,23533,29847,4718,
 23469,18727,24429,24445,31599,31716,31609,31661,31183,29842,
 23407,23402,23421,23213,23186,29351,448,4772,1040
};
static void text(uint16_t *pixels,const char *message,int y)
{
 int x=12;
 for(;*message && y<226;++message) {
  char c=*message;const char *g;uint16_t bits;
  if(c=='\n' || x>300) { x=12;y+=14;if(c=='\n')continue; }
  if(y>228)break;
  if(c>='a' && c<='z')c=(char)(c-'a'+'A');
  g=strchr(glyphs,c);
  if(c!=' ') {
   /* Unrepresented punctuation is an explicit box, never recursive fatal. */
   bits=g ? glyph[g-glyphs] : 0x7b6fu;
   for(int row=0;row<5;++row)for(int col=0;col<3;++col)
    if(bits&(1u<<(14-row*3-col)))
     for(int a=0;a<2;++a)for(int b=0;b<2;++b)
      pixels[(y+row*2+a)*320+x+col*2+b]=0xffff;
  }
  x+=8;
 }
}
void tr64_play_message(const char *title,const char *message)
{
 surface_t *surface;
 int ready=message_ready;message_ready=0;
 /* Enabled only at game boundaries, never while the renderer holds a surface. */
 surface=display_get();
 uint16_t *pixels=surface->buffer;
 for(unsigned i=0;i<320u*240u;++i)pixels[i]=0x0845;
 text(pixels,title,12);text(pixels,message,40);
 /* Fixed display surfaces are uncached (plat/n64/surfaces.c). */
 sys_gpu_present(surface);
 message_ready=ready;
}
