/* SPDX-License-Identifier: MIT */
/* TX64 layout facts: docs/formats/n64-tex.md. No texture conversion at load. */
#include <string.h>
#include "engine/res/load-internal.h"
#include "tr64/res.h"
#include "engine/core/sys.h"
static uint32_t h(const uint8_t *p,uint32_t i) { return tr64_be32(p+i*4); }
static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0]<<8 | p[1]; }
static void bad(const char *why) { sys_error("textures: %s",why); }
static const uint8_t *region(const tr64_level_t *l,uint32_t off,uint32_t count,uint32_t stride)
{
 if(off<80 || off%8 || off>l->texture_bytes || count>(l->texture_bytes-off)/stride)bad("table offset/count out of range");
 return l->textures+off;
}
void load_validate_texture(tr64_level_t *l)
{
 const uint8_t *p=l->textures,*clusters,*palettes,*objects,*sprites,*splits,*flat,*anim;
 uint32_t no,ns,nc,np,nl,nf,na,i,j,last=0,ci,pi,x,y,w,height,first,count,previous,pos,groups,pitch,rows,boundary;
#if defined(TR64_HOST) || TR64_GAME >= 2
 unsigned odd_width=l->game==2 || l->game==3;
#else
 unsigned odd_width=l->game==2;
#endif
 if(l->texture_bytes<80 || memcmp(p,"TX64",4) || h(p,1)!=1 || h(p,2)!=l->texture_bytes || h(p,17)!=odd_width || h(p,18) || h(p,19))bad("header/version/length/reserved");
 no=h(p,3); ns=h(p,4); nc=h(p,5); np=h(p,6); nl=h(p,7); nf=h(p,8); na=h(p,9);
 if(no>TR64_LIMIT(object_descriptors,odd_width))bad("object descriptor count limit");
 if(ns>TR64_LIMIT(sprite_descriptors,odd_width) || no+ns>TR64_LIMIT(descriptor_ids,odd_width))
  bad("sprite/combined descriptor count limit");
 if(nc>TR64_LIMIT(clusters,odd_width) || np>TR64_LIMIT(palettes,odd_width) ||
    nl>TR64_LIMIT(split_lines,odd_width))bad("cluster/palette/split count limit");
 clusters=region(l,h(p,10),nc,16); palettes=region(l,h(p,11),np,8);
 objects=region(l,h(p,12),no,24); sprites=region(l,h(p,13),ns,22);
 splits=region(l,h(p,14),nl,2); flat=region(l,h(p,15),nf,8); anim=region(l,h(p,16),na,2);
 for(i=0;i<nc;++i) {
  const uint8_t *c=clusters+i*16; w=u16(c+4); height=u16(c+6); rows=u16(c+8);
  if(!w || w>256 || !height || height>256 || rows!=(w+1)/2 || u16(c+10) || h(c,3))bad("cluster extent/stride/border");
  (void)region(l,h(c,0),rows*height,1);
 }
 for(i=0;i<np;++i) {
  const uint8_t *q=palettes+i*8;
  if(u16(q+6))bad("palette reserved field");
  (void)region(l,h(q,0),16,2);
 }
 for(i=0;i<no+ns;++i) {
  int object=i<no; const uint8_t *d=object ? objects+i*24 : sprites+(i-no)*22;
  ci=u16(d); pi=u16(d+2); x=d[4]; y=d[5]; w=(uint32_t)d[6]+1; height=(uint32_t)d[7]+1;
  if(ci>=nc || pi>=np)bad("descriptor cluster/palette index");
  if((x&1) || (!odd_width && (w&1)))bad("CI4 descriptor endpoint alignment");
  if(x+w>u16(clusters+ci*16+4) || y+height>u16(clusters+ci*16+6))bad("descriptor rectangle extent");
  if(object) {
   if((d[16]!=1 && d[16]!=2 && d[16]!=4) || (d[17]!=0x24 && d[17]!=0x34))bad("descriptor flags/corner map");
   for(j=0;j<4;++j)if(d[8+j*2]>=w || d[9+j*2]>=height)bad("descriptor corner extent");
   first=u16(d+20); count=u16(d+22); j=u16(d+18);
  } else { first=u16(d+10); count=u16(d+12); j=u16(d+8); }
  if(i==0 || i==no)last=0;
  else if(j<=last)bad("descriptor source ids not sorted");
  last=j;
  if(first>nl || count>nl-first)bad("split-line offset/count");
  pitch=((((x+w+1)&~1u)-(x&~1u))/2+7)&~7u;
  if(!pitch || pitch>2048)bad("tile pitch");
  rows=2048/pitch; previous=0;
  for(j=0;j<=count;++j) {
   boundary=j<count ? u16(splits+(first+j)*2) : height;
   if(boundary<=previous || boundary>height || boundary-previous>rows)bad("split-line order/TMEM extent");
   previous=boundary;
  }
 }
 last=0;
 for(i=0;i<nf;++i) {
  uint32_t id=u16(flat+i*8);
  if((i && id<=last) || u16(flat+i*8+4)>1 || u16(flat+i*8+6))bad("flat material ids/flags");
  last=id;
 }
 if(!na)bad("missing animation range count");
 groups=u16(anim); pos=1;
 for(i=0;i<groups;++i) {
  if(pos>=na)bad("animation range count out of bounds");
  count=u16(anim+pos*2); ++pos;
  if(!count || count>na-pos)bad("animation member count out of bounds");
  for(j=0;j<count;++j)if(u16(anim+(pos+j)*2)>=no)bad("animation descriptor out of range");
  pos+=count;
 }
 if(pos!=na)bad("unexplained animation words");
}
