/* SPDX-License-Identifier: MIT */
/* Rotation facts: OpenLara2333adef src/fixed/level.h:319-347. */
#include <string.h>
#include "engine/render/internal.h"
#include "engine/render/texture.h"
size_t tr64_render_texture_bytes_load(const tr64_level_t *core)
{
 if(!core || !core->ready || !core->textures || core->texture_bytes<80)return SIZE_MAX;
 const uint8_t *p=core->textures;uint32_t objects=u32(p+12),words=u32(p+36),at=u32(p+64);
 if(objects>=UINT16_MAX || !words || at>core->texture_bytes || words>(core->texture_bytes-at)/2)return SIZE_MAX;
 if(!u16(p+at))return words==1?0:SIZE_MAX;
 size_t bytes=2*(size_t)objects;
 if(words-1>(SIZE_MAX-bytes)/4)return SIZE_MAX;
 return bytes+4*(size_t)(words-1);
}
static int shape(const uint8_t *tx,unsigned a,unsigned b)
{
 const uint8_t *x=tx+u32(tx+48)+24*a,*y=tx+u32(tx+48)+24*b;
 if(memcmp(x+6,y+6,12) || u16(x+22)!=u16(y+22))return 0;
 const uint8_t *lines=tx+u32(tx+56);unsigned n=u16(x+22),xf=u16(x+20),yf=u16(y+20);
 for(unsigned i=0;i<n;++i)if(u16(lines+2*(xf+i))!=u16(lines+2*(yf+i)))return 0;
 return 1;
}
int tr64_render_texture_bind_load(tr64_render_t *r,uint16_t *storage,size_t bytes,tr64_render_error_t *e)
{
 size_t need=tr64_render_texture_bytes_load(r->core);
 if(need==SIZE_MAX || need>bytes)return error(e,TR64_RENDER_WORKSPACE,TR64_RENDER_SECTION_TEXTURES,0);
 r->texture_map=NULL;r->texture_cycles=NULL;r->texture_cycle_words=0;
 r->texture_step=r->texture_requested_step=0;
 if(!need)return 0;
 const uint8_t *tx=r->textures.data,*data=tx+u32(tx+64);
 unsigned objects=u32(tx+12),words=u32(tx+36),groups=u16(data),cursor=1;
 uint16_t *map=storage,*cycles=storage+objects;unsigned used=0,capacity=2*(words-1);
 for(unsigned i=0;i<objects;++i)map[i]=(uint16_t)i;
 for(unsigned g=0;g<groups;++g) {
  if(cursor>=words)return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,u32(tx+64));
  unsigned n=u16(data+2*cursor++);
  if(!n || n>words-cursor)return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,u32(tx+64)+2*cursor);
  unsigned first=u16(data+2*cursor);
  if(first>=objects)return error(e,TR64_RENDER_REFERENCE,TR64_RENDER_SECTION_TEXTURES,u32(tx+64)+2*cursor);
  for(unsigned i=1;i<n;++i) {
   unsigned id=u16(data+2*(cursor+i));
   if(id>=objects || !shape(tx,first,id))return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,u32(tx+64)+2*(cursor+i));
  }
  uint16_t tmp=map[first];
  for(unsigned i=0;i+1<n;++i)map[u16(data+2*(cursor+i))]=map[u16(data+2*(cursor+i+1))];
  map[u16(data+2*(cursor+n-1))]=tmp;cursor+=n;
 }
 if(cursor!=words)return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,u32(tx+64)+2*cursor);
 /* Compose all source groups before decomposing. Overlapping groups remain
  * correct; each cycle stores length, current phase, then native ids. */
 for(unsigned i=0;i<objects;++i) {
  if(map[i]==i || map[i]==UINT16_MAX)continue;
  if(capacity-used<2)return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,0);
  unsigned header=used,id=i;used+=2;
  do {
   if(id>=objects || map[id]==UINT16_MAX || used==capacity)
    return error(e,TR64_RENDER_LAYOUT,TR64_RENDER_SECTION_TEXTURES,0);
   unsigned next=map[id];cycles[used++]=(uint16_t)id;map[id]=UINT16_MAX;id=next;
  } while(id!=i);
  cycles[header]=(uint16_t)(used-header-2);cycles[header+1]=0;
 }
 for(unsigned i=0;i<objects;++i)map[i]=(uint16_t)i;
 r->texture_map=map;r->texture_cycles=cycles;r->texture_cycle_words=used;return 0;
}
