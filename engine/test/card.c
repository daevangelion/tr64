/* SPDX-License-Identifier: MIT */
/* Project-owned 3x5 diagnostic font, ported from MIT ac3n64 04134cc. */
#include "engine/core/fmt.h"
#include <string.h>
#include "tr64/engine.h"
#include "tr64/mem.h"
#include "tr64/limits.h"
#include "build_id.h"
#include "engine/core/sys.h"
static const char glyphs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-/:";
static const uint16_t glyph[] = {
 31599,11415,29671,29647,23497,31183,31215,29257,31727,31695,
 11245,27566,31015,27502,31207,31204,31083,23533,29847,4718,
 23469,18727,24429,24445,31599,31716,31609,31661,31183,29842,
 23407,23402,23421,23213,23186,29351,448,4772,1040
};
static void text(uint16_t *fb,int x,int y,const char *s,int scale,uint16_t color)
{
 for(;*s;++s,x+=4*scale) {
  char c=*s>='a' && *s<='z' ? (char)(*s-'a'+'A') : *s;
  const char *g=strchr(glyphs,c); uint16_t bits; int row,col,a,b;
  if(c==' ')continue;
  if(!g)sys_error("boot card unknown glyph %u",(unsigned)(uint8_t)c);
  bits=glyph[g-glyphs];
  for(row=0;row<5;++row)for(col=0;col<3;++col)
   if(bits & (1u << (14-row*3-col)))
    for(a=0;a<scale;++a)for(b=0;b<scale;++b) {
     int px=x+col*scale+b,py=y+row*scale+a;
     if(px>=0 && px<TR64_WIDTH && py>=0 && py<TR64_HEIGHT)fb[py*TR64_WIDTH+px]=color;
    }
 }
}
void tr64_boot_card(uint16_t *p)
{
 unsigned i; int pool; char line[64]; tr64_mem_stats_t m;
 for(i=0;i<TR64_WIDTH*TR64_HEIGHT;++i)p[i]=0x0845u;
 text(p,16,12,"TR64",4,0x5fffu);
 text(p,16,42,"BUILD " TR64_BUILD_ID,2,0xffffu);
 text(p,16,60,"POOL / USED BYTES",2,0xafffu);
 for(pool=0;pool<TR64_MEM_COUNT;++pool) {
  tr64_mem_stats(pool,&m);
  tr64_snprintf(line,sizeof(line),"%-10s %7lu",m.name,(unsigned long)m.used);
  text(p,16,76+pool*9,line,1,0xffffu);
 }
 tr64_snprintf(line,sizeof(line),"ROUTE TICKS %lu",(unsigned long)tr64_eng_state()->tick);
 text(p,16,214,line,2,0x5fffu);
}
