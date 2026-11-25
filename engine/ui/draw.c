/* SPDX-License-Identifier: MIT */
/* Original presentation code; glyph identities/advances are layout facts from
 * OpenLara 2333adef src/fixed/draw.h:146-223. No platform renderer code copied. */
#include <limits.h>
#include <string.h>
#include "engine/ui/ui.h"
#include "engine/ui/front.h"
#include "tr64/code.h"
#include "tr64/game.h"
#include "engine/core/sys.h"
#include "build_id.h"
#include "ui_font_data.h"
#if TR64_TEST
extern void tr64_ui_test_overlay(void) __attribute__((weak));
#endif
extern const tr64_ui_front_call_t *tr64_ui_front_frame __attribute__((weak));
/* The profiler overlay (engine/ui/perf.c); absent from builds that do not link it. */
extern uint8_t tr64_perf_state __attribute__((weak));
extern void tr64_perf_draw(void) __attribute__((weak));
static struct {
 tr64_gpu_span_t textures;
 unsigned first;
 uint32_t submit_us;
 int active;
} ui;
static uint16_t be16(const uint8_t *p)
{ return (uint16_t)((unsigned)p[0]*256+p[1]); }
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int region(uint32_t size,uint32_t at,uint32_t count,uint32_t stride)
{ return stride && at<=size && count<=(size-at)/stride; }
static int next_glyph(const unsigned char **p,unsigned *glyph,unsigned *space)
{
 unsigned c=*(*p)++;
 *space=0;
 if(c==' ') { *space=1;*glyph=0;return 1; }
 if(c=='$') { c=*(*p)++;if(!c || c>=110)return 0;*glyph=c;return 1; }
 if(c<11)*glyph=c+81;
 else if(c<16)*glyph=c+91;
 else if(c>=32 && c-32<sizeof(remap))*glyph=remap[c-32];
 else return 0;
 return *glyph<110;
}
int tr64_ui_bind(const tr64_level_t *level)
{
 const uint8_t *tx;
 unsigned first=0,count=0;
 if(!level || !level->ready)return 0;
 const tr64_level_table_t *table=tr64_level_table(level,TR64_CORE_sprite_sequences);
#if defined(TR64_HOST)
 const unsigned font_type=level->game==3 ? 356u : level->game==2 ? 255u : 190u;
#else
 const unsigned font_type=(TR64_GAME==3) ? 356u : TR64_CART_TR2 ? 255u : 190u;
#endif
 for(unsigned i=0;i<table->count;++i) {
  const tr64_core_sprite_sequences_t *s=tr64_level_sprite_sequences(level,i);
  /* TR2's cooked glyph sequence is type 255 (TR2/TR3 Differences §3.2). */
  if(tr64_u32(s->type)==font_type) {
   if(count)return 0;
   int n=tr64_i16(s->count);
   if(n>=0)return 0;
   first=tr64_u16(s->first);count=(unsigned)-n;
  }
 }
 tx=level->textures;
 if(count<110 || !tx || level->texture_bytes<80 || memcmp(tx,"TX64",4) ||
    be32(tx+4)!=1 || be32(tx+8)!=level->texture_bytes ||
    first<be32(tx+12) || first-be32(tx+12)>be32(tx+16) ||
    count>be32(tx+16)-(first-be32(tx+12)) ||
    !region(level->texture_bytes,be32(tx+52),be32(tx+16),22) ||
    !region(level->texture_bytes,be32(tx+40),be32(tx+20),16) ||
    !region(level->texture_bytes,be32(tx+44),be32(tx+24),8))return 0;
 ui.textures=(tr64_gpu_span_t){tx,level->texture_bytes};
 ui.first=first;
 return 1;
}
void tr64_ui_unbind(void) { memset(&ui,0,sizeof(ui)); }
void tr64_ui_set_active(int active) { ui.active=!!active; }
int tr64_ui_text_width(const char *text)
{
 const unsigned char *p=(const unsigned char *)text;
 int width=0;
 if(!p)return -1;
 while(*p) {
  const unsigned char *start=p;
  unsigned glyph,space;
  if(!next_glyph(&p,&glyph,&space))return -1;
  unsigned step=space?6:advances[glyph]+1;
  /* Preserve fixed getTextWidth's escaped-arrow measurement: it remaps
   * both bytes, although drawText consumes the pair as one raw glyph. */
  if(*start=='$') {
   unsigned raw=start[1],mapped;
   if(raw<11)mapped=raw+81;
   else if(raw<16)mapped=raw+91;
   else if(raw>=32 && raw-32<sizeof(remap))mapped=remap[raw-32];
   else return -1;
   step=advances[remap['$'-32]]+1+(raw==' '?6:advances[mapped]+1);
  }
  if(width>INT_MAX-(int)step)return -1;
  width+=(int)step;
 }
 return width;
}
static tr64_ui_sprite_t sprite(unsigned glyph)
{
 const uint8_t *tx=ui.textures.data;
 const uint8_t *d=tx+be32(tx+52)+(ui.first+glyph-be32(tx+12))*22;
 unsigned cluster=be16(d),palette=be16(d+2);
 if(cluster>=be32(tx+20) || palette>=be32(tx+24))sys_error("UI sprite directory");
 const uint8_t *c=tx+be32(tx+40)+cluster*16;
 const uint8_t *p=tx+be32(tx+44)+palette*8;
 tr64_ui_sprite_t s={0};
 s.image_width=be16(c+4);s.image_height=be16(c+6);s.stride=be16(c+8);
 s.source_x=d[4];s.source_y=d[5];s.width=(unsigned)d[6]+1;s.height=(unsigned)d[7]+1;
 if(s.source_x+s.width>s.image_width || s.source_y+s.height>s.image_height ||
    !region(ui.textures.bytes,be32(c),s.image_height,s.stride) ||
    !region(ui.textures.bytes,be32(p),16,2))sys_error("UI sprite payload");
 s.image=tx+be32(c);s.palette=tx+be32(p);
 s.left=(int16_t)be16(d+14);s.top=(int16_t)be16(d+16);
 s.right=(int16_t)be16(d+18);s.bottom=(int16_t)be16(d+20);
 return s;
}
#if TR64_HAS_TR2
void tr64_ui_pickup_sprite(unsigned descriptor,int x,int y)
{
 const uint8_t *tx=ui.textures.data;
 if(!tx || descriptor-be32(tx+12)>=be32(tx+16))sys_error("UI pickup sprite descriptor");
 tr64_ui_sprite_t s=sprite(descriptor-ui.first);
 /* Preserve the texture aspect inside the 48 px notice cell. */
 unsigned scale=s.width>s.height?s.width:s.height;
 int w=(s.width*32+scale-1)/scale,h=(s.height*32+scale-1)/scale;
 tr64_ui_backend_sprite(x-w/2,y-h/2,w,h,&s);
}
#endif
int tr64_ui_text(int x,int y,const char *text,unsigned align)
{
 int width=tr64_ui_text_width(text);
 if(width<0 || width>32767 || x<-32768 || x>32767 || y<-32768 || y>32767 ||
    align>TR64_UI_RIGHT || !ui.textures.data)return 0;
 if(align==TR64_UI_CENTRE) {
  int delta=320-width;
  x+=delta>=0?delta/2:-((-delta+1)/2);
 }
 else if(align==TR64_UI_RIGHT)x+=320-width;
 const unsigned char *p=(const unsigned char *)text;
 while(*p) {
  unsigned c=*p,glyph,space;
  if(!next_glyph(&p,&glyph,&space))return 0;
  if(space) { x+=6;continue; }
  tr64_ui_sprite_t s=sprite(glyph);
  tr64_ui_backend_sprite(x+s.left,y+s.top-(c=='p'),s.right-s.left,s.bottom-s.top,&s);
  x+=advances[glyph]+1;
 }
 return 1;
}
tr64_ui_sprite_t tr64_ui_font_sprite(unsigned glyph)
{
 if(glyph>=110 || !ui.textures.data)sys_error("UI font glyph %u",glyph);
 return sprite(glyph);
}
int tr64_ui_text_glyphs(int x,int y,const char *text,unsigned align,tr64_ui_glyph_t *out,unsigned capacity)
{
 int width=tr64_ui_text_width(text);
 unsigned n=0;
 if(width<0 || width>32767 || x<-32768 || x>32767 || y<-32768 || y>32767 ||
    align>TR64_UI_RIGHT || !ui.textures.data)return -1;
 if(align==TR64_UI_CENTRE) {
  int delta=320-width;
  x+=delta>=0?delta/2:-((-delta+1)/2);
 }
 else if(align==TR64_UI_RIGHT)x+=320-width;
 const unsigned char *p=(const unsigned char *)text;
 while(*p) {
  unsigned c=*p,glyph,space;
  if(!next_glyph(&p,&glyph,&space))return -1;
  if(space) { x+=6;continue; }
  if(n==capacity)return -1;
  tr64_ui_sprite_t s=sprite(glyph);
  out[n++]=(tr64_ui_glyph_t){(int16_t)(x+s.left),(int16_t)(y+s.top-(c=='p')),(uint8_t)glyph};
  x+=advances[glyph]+1;
 }
 return (int)n;
}
int tr64_ui_glyph(int x,int y,unsigned glyph,unsigned align)
{
 if(!ui.textures.data || glyph>=110 || align>TR64_UI_RIGHT ||
    x<-32768 || x>32767 || y<-32768 || y>32767)return 0;
 unsigned width=advances[glyph]+1;
 if(align==TR64_UI_CENTRE)x+=(320-(int)width)/2;
 else if(align==TR64_UI_RIGHT)x+=320-(int)width;
 tr64_ui_sprite_t s=sprite(glyph);
 tr64_ui_backend_sprite(x+s.left,y+s.top,s.right-s.left,s.bottom-s.top,&s);
 return 1;
}
void tr64_ui_bar(int x,int y,unsigned width,int value,unsigned type)
{
 /* Shape and palette-index facts: fixed draw.h1075 and the allowed 32X
  * renderer render.cpp1235-1272. Common TITLE palette facts: n64-ui.md. */
 static const uint8_t colours[4][5]={
  {8,11,8,6,24},{32,41,32,19,21},{28,29,28,26,27},{43,44,43,42,41}
 };
 if(type>=4 || width>320)sys_error("UI bar type/width");
 if(value<0)value=0;
 if(value>256)value=256;
 int filled=(unsigned)value*width>>8;
 for(int row=0;row<5;++row)
  tr64_ui_backend_rect(x+1,y+1+row,filled,1,tr64_ui_colour(colours[type][row]));
 tr64_ui_shade(x+1+filled,y+1,(int)width-filled,5,27);
 uint16_t light=tr64_ui_colour(19),dark=tr64_ui_colour(17);
 tr64_ui_backend_rect(x+1,y,(int)width,1,light);
 tr64_ui_backend_rect(x+1,y+6,(int)width,1,dark);
 tr64_ui_backend_rect(x,y,1,7,light);
 tr64_ui_backend_rect(x+(int)width+1,y,1,7,dark);
}
void tr64_ui_shade(int x,int y,int width,int height,unsigned shade)
{
 /* sum(TITLE RGB5[lightmap[shade]]) / sum(TITLE RGB5), rounded to the
  * RDP force-blender's five-bit retention coefficient; see n64-ui.md. */
 unsigned keep;
 if(shade==25)keep=14;
 else if(shade==27)keep=10;
 else sys_error("UI shade row %u",shade);
 tr64_ui_backend_shade(x,y,width,height,keep);
}
uint16_t tr64_ui_colour(unsigned index)
{
 /* Source TITLE six-bit palette -> RGB555 (c>>1), opaque alpha. */
 switch(index) {
 case 6:return 0x5a01;case 8:return 0x72cb;case 10:return 0xa34f;
 case 11:return 0xa3d3;case 12:return 0xb415;case 14:return 0xdd19;
 case 15:return 0xee1d;case 17:return 0x741d;case 19:return 0x4a93;
 case 21:return 0x318d;case 24:return 0x5185;case 25:return 0x5a09;
 case 26:return 0x5089;case 27:return 0x3847;case 28:return 0x7909;
 case 29:return 0xa147;case 32:return 0x6399;case 41:return 0x5d27;
 case 42:return 0x5df1;case 43:return 0x8673;case 44:return 0xbef7;
 default:sys_error("UI colour index %u",index);
 }
}
static void split_hud(const tr64_ui_front_call_t *front)
{
 tr64_ui_backend_begin();
 for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
  tr64_game_hud_view_t hud;
  int y=4+(int)slot*120;
  if(game_hud_view_player(slot,&hud) && (hud.show_health || hud.show_air)) {
   if(hud.health_max<=0 || hud.air_max<=0)sys_error("UI HUD maxima");
   if(hud.show_air) { tr64_ui_bar(212,y,100,(hud.air*256)/hud.air_max,TR64_UI_AIR);y+=10; }
   if(hud.show_health)tr64_ui_bar(212,y,100,(hud.health*256)/hud.health_max,TR64_UI_HEALTH);
  }
  if(front->warp_notice && front->warp_notice[slot])
   if(!tr64_ui_text(0,45+(int)slot*120,"NO LARGE MEDIPACK",TR64_UI_CENTRE))sys_error("UI medipack notice");
 }
 tr64_ui_backend_end();
}
void tr64_ui_gpu_draw(void)
{
 tr64_game_hud_view_t hud;
 ui.submit_us=0;
 if(!ui.active)return;
 const tr64_ui_front_call_t *front=&tr64_ui_front_frame?tr64_ui_front_frame:NULL;
 int split=!TR64_CART_TR2 && front && front->split;
#if TR64_GAME == 3
 int show=split || (game_hud_view(&hud) && (hud.show_health || hud.show_air || hud.show_sprint));
#else
 int show=split || (game_hud_view(&hud) && (hud.show_health || hud.show_air));
#endif
 const int perf=&tr64_perf_state && (tr64_perf_state&1u) && tr64_perf_draw;
 if(!show && !front && !perf)return;
 uint32_t start=sys_profile_ticks();
 if(split)split_hud(front);
 else if(show) {
  if(hud.health_max<=0 || hud.air_max<=0)sys_error("UI HUD maxima");
  tr64_ui_backend_begin();int y=4;
  if(hud.show_air) {
   tr64_ui_bar(212,y,100,(hud.air*256)/hud.air_max,TR64_UI_AIR);
   y+=10;
  }
  if(hud.show_health) {
   tr64_ui_bar(212,y,100,(hud.health*256)/hud.health_max,TR64_UI_HEALTH);
   y+=10;
  }
#if TR64_GAME == 3
  if(hud.show_sprint && hud.sprint_max>0)
   tr64_ui_bar(212,y,100,(hud.sprint*256)/hud.sprint_max,TR64_UI_DASH);
#endif
#if TR64_TEST
  if(tr64_ui_test_overlay)tr64_ui_test_overlay();
#endif
  tr64_ui_backend_end();
 }
 if(front && (!split || front->hud || front->hud_second || front->pickups[0].count ||
               front->pickups[1].count || front->update==TR64_UI_UPDATE_FRAME) &&
    code_call("front",(uint32_t)(uintptr_t)front)!=1)sys_error("UI presentation unit failed");
 ui.submit_us=sys_profile_us(sys_profile_ticks()-start);
 /* Last, over everything, and outside submit_us: it measures itself (OVL). */
 if(perf)tr64_perf_draw();
}
uint32_t tr64_ui_last_submit_us(void) { return ui.submit_us; }
static int message_valid(const char *text)
{
 const unsigned char *p=(const unsigned char *)text;
 if(!p)return 0;
 while(*p) {
  unsigned glyph,space;
  if(*p=='\n') { ++p;continue; }
  if(!next_glyph(&p,&glyph,&space))return 0;
 }
 return 1;
}
static int message_lines(const char *text,int y)
{
 const unsigned char *p=(const unsigned char *)text;
 char line[96];
 unsigned used=0;
 int width=0;
 while(*p && y<=224) {
  if(*p=='\n') {
   line[used]=0;tr64_ui_text(12,y,line,TR64_UI_LEFT);
   used=0;width=0;y+=18;++p;continue;
  }
  const unsigned char *start=p;
  unsigned glyph,space;
  if(!next_glyph(&p,&glyph,&space))sys_error("UI message validation");
  unsigned bytes=(unsigned)(p-start);
  char unit[3]={(char)start[0],bytes==2?(char)start[1]:0,0};
  int step=tr64_ui_text_width(unit);
  if(used+bytes>=sizeof(line) || width+step>296) {
   line[used]=0;tr64_ui_text(12,y,line,TR64_UI_LEFT);
   used=0;width=0;y+=18;
   if(y>224)break;
  }
  for(unsigned i=0;i<bytes;++i)line[used++]=(char)start[i];
  width+=step;
 }
 if(used && y<=224) { line[used]=0;tr64_ui_text(12,y,line,TR64_UI_LEFT); }
 return y;
}
int tr64_ui_message(const char *title,const char *message)
{
 if(!ui.textures.data || !message_valid(title) || !message_valid(message))return 0;
 tr64_ui_backend_screen_begin();
 int y=message_lines(title,32);
 message_lines(message,y+32);
 tr64_ui_backend_screen_end();
 return 1;
}
