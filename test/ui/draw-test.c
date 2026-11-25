/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/ui/ui.h"
#include "engine/ui/front.h"
#include "tr64/game.h"
#include "ui_string_ids.h"
uint32_t code_call(const char *name,uint32_t argument)
{ (void)name;(void)argument;abort(); }
static uint8_t texture[2688],sequence[8];
static uint16_t picture[320*240];
static tr64_level_table_t table;
static tr64_game_hud_view_t hud;
static tr64_game_hud_view_t partner;
const tr64_ui_front_call_t *tr64_ui_front_frame;
static int loaded,begins,ends,sprites;
static int glyph_x[64],glyph_y[64];
static void put16(uint8_t *p,unsigned n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
static void put32(uint8_t *p,unsigned n)
{ p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n; }
void sys_error(const char *format,...)
{ va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);abort(); }
const tr64_level_table_t *tr64_level_table(const tr64_level_t *l,uint32_t kind)
{ (void)l;assert(kind==TR64_CORE_sprite_sequences);return &table; }
const void *tr64_level_at(const tr64_level_t *l,uint32_t kind,uint32_t index)
{ (void)l;assert(kind==TR64_CORE_sprite_sequences && !index);return sequence; }
int game_hud_view(tr64_game_hud_view_t *out)
{ if(!loaded || !out)return 0;*out=hud;return 1; }
int game_hud_view_player(unsigned slot,tr64_game_hud_view_t *out)
{
 if(!slot)return game_hud_view(out);
 assert(slot==1 && out);*out=partner;return 1;
}
void tr64_ui_backend_begin(void) { ++begins; }
void tr64_ui_backend_end(void) { ++ends; }
void tr64_ui_backend_screen_begin(void) { tr64_ui_backend_begin(); }
void tr64_ui_backend_screen_end(void) { tr64_ui_backend_end(); }
uint32_t sys_profile_ticks(void) { static uint32_t tick;return ++tick; }
uint32_t sys_profile_us(uint32_t ticks) { return ticks; }
void tr64_ui_backend_rect(int x,int y,int w,int h,uint16_t c)
{
 for(int j=0;j<h;++j)for(int i=0;i<w;++i) {
  if(x+i>=0 && x+i<320 && y+j>=0 && y+j<240)picture[(y+j)*320+x+i]=c;
 }
}
static uint16_t shade_pixel(unsigned c,unsigned keep)
{
 unsigned r=((c>>11)*keep)>>5,g=(((c>>6)&31)*keep)>>5,b=(((c>>1)&31)*keep)>>5;
 return (uint16_t)(r<<11|g<<6|b<<1|1);
}
void tr64_ui_backend_shade(int x,int y,int w,int h,unsigned keep)
{
 for(int j=0;j<h;++j)for(int i=0;i<w;++i)
  if(x+i>=0 && x+i<320 && y+j>=0 && y+j<240)
   picture[(y+j)*320+x+i]=shade_pixel(picture[(y+j)*320+x+i],keep);
}
void tr64_ui_backend_sprite(int x,int y,int w,int h,const tr64_ui_sprite_t *s)
{
 assert(w==16 && h==16 && s->width==16 && s->height==16);
 assert(sprites<32);glyph_x[sprites]=x;glyph_y[sprites]=y;++sprites;
}
static tr64_level_t fixture(void)
{
 tr64_level_t l={0};l.ready=1;l.textures=texture;l.texture_bytes=sizeof(texture);
 memcpy(texture,"TX64",4);put32(texture+4,1);put32(texture+8,sizeof(texture));
 put32(texture+16,110);put32(texture+20,1);put32(texture+24,1);
 put32(texture+40,80);put32(texture+44,96);put32(texture+52,136);
 put32(texture+80,2560);put16(texture+84,16);put16(texture+86,16);put16(texture+88,8);
 put32(texture+96,104);
 for(unsigned i=0;i<110;++i) {
  uint8_t *d=texture+136+22*i;d[6]=15;d[7]=15;
  put16(d+16,(unsigned)-16);put16(d+18,16);
 }
 put32(sequence,190);put16(sequence+4,(unsigned)-110);put16(sequence+6,0);
 table.count=1;table.stride=8;table.data=sequence;
 return l;
}
int main(void)
{
 tr64_level_t l=fixture();
 assert(tr64_ui_bind(&l));
 uint32_t string_count=0;
 const uint16_t *widths=tr64_ui_string_widths(&string_count);
 assert(string_count==TR64_UI_STRING_COUNT && tr64_ui_string_bytes()==2412);
 for(uint32_t i=0;i<string_count;++i)
  assert(widths[i]==tr64_ui_text_width(tr64_ui_string(i)));
 assert(!tr64_ui_string(string_count));
 assert(tr64_ui_text_width("")==0);
 assert(tr64_ui_text_width("A p")==31);
 assert(tr64_ui_text_width("$")==-1);
 assert(tr64_ui_text_width("\xff")==-1);
 assert(tr64_ui_text(0,20,"A p",TR64_UI_CENTRE));
 assert(sprites==2 && glyph_x[0]==144 && glyph_y[0]==4);
 assert(glyph_x[1]==165 && glyph_y[1]==3);
 assert(!tr64_ui_text(0,20,"A$",TR64_UI_LEFT) && sprites==2);
 assert(tr64_ui_text(0,20,"$\x6d",TR64_UI_LEFT) && sprites==3);
 assert(tr64_ui_text_width("$\x6d")==21);
 assert(tr64_ui_text_width("$ ")==14);
 assert(tr64_ui_text(0,20,"AAAAAAAAAAAAAAAAAAAAA ",TR64_UI_CENTRE));
 assert(glyph_x[3]==-1);
 unsigned glyph_before=sprites;
 assert(tr64_ui_glyph(-6,20,80,TR64_UI_RIGHT));
 assert(tr64_ui_text(-6,20,"[",TR64_UI_RIGHT));
 assert(glyph_x[glyph_before]==glyph_x[glyph_before+1] &&
        glyph_y[glyph_before]==glyph_y[glyph_before+1]);
 assert(!tr64_ui_glyph(0,20,110,TR64_UI_LEFT));
 hud=(tr64_game_hud_view_t){500,1000,900,1800,1,1};loaded=1;
 for(unsigned i=0;i<320*240;++i)picture[i]=0xffff;
 tr64_ui_set_active(1);tr64_ui_gpu_draw();
 assert(begins==1 && ends==1);
 assert(picture[5*320+213]==tr64_ui_colour(32));
 assert(picture[15*320+213]==tr64_ui_colour(8));
 assert(picture[15*320+262]==tr64_ui_colour(8));
 assert(picture[15*320+263]==shade_pixel(0xffff,10));
 for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)
  if(!(x>=212 && x<314 && ((y>=4 && y<11)||(y>=14 && y<21))))
   assert(picture[y*320+x]==0xffff);
 hud.show_air=hud.show_health=0;tr64_ui_gpu_draw();assert(begins==1);
 for(unsigned i=0;i<320*240;++i)picture[i]=0xffff;
 hud.show_health=1;hud.health=-10;tr64_ui_gpu_draw();
 assert(picture[5*320+213]==shade_pixel(0xffff,10));
 hud.health=2000;tr64_ui_gpu_draw();assert(picture[5*320+312]==tr64_ui_colour(8));
 tr64_ui_set_active(0);tr64_ui_gpu_draw();assert(begins==3);
 tr64_ui_set_active(1);
 for(unsigned i=0;i<320*240;++i)picture[i]=0xffff;
 hud=(tr64_game_hud_view_t){250,1000,0,1800,1,0};
 partner=(tr64_game_hud_view_t){750,1000,900,1800,1,1};
 uint8_t notice[2]={0,1};
 tr64_ui_pickups_t queues[2]={{0}};
 const tr64_ui_front_call_t split={.pickups=queues,.warp_notice=notice,.split=1};
 tr64_ui_front_frame=&split;sprites=0;tr64_ui_gpu_draw();
 assert(begins==4 && ends==4);
 assert(picture[5*320+237]==tr64_ui_colour(8) && picture[5*320+238]==shade_pixel(0xffff,10));
 assert(picture[125*320+262]==tr64_ui_colour(32) && picture[125*320+263]==shade_pixel(0xffff,10));
 assert(picture[135*320+287]==tr64_ui_colour(8) && picture[135*320+288]==shade_pixel(0xffff,10));
 assert(sprites==15);
 for(int i=0;i<sprites;++i)assert(glyph_y[i]>=120 && glyph_y[i]<240);
 tr64_ui_front_frame=NULL;
 tr64_ui_unbind();assert(!tr64_ui_text(0,0,"A",0));
 put16(sequence+4,(unsigned)-109);assert(!tr64_ui_bind(&l));
 puts("UI synthetic PASS: font mapping/alignment/escapes, health/air visibility, clipping footprint, clamping and bind rejection");
 return 0;
}
