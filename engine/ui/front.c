/* SPDX-License-Identifier: MIT */
/* Original painter; layout facts from OpenLara2333adef fixed/inventory.h
 * drawOptions/drawSlot/draw and fixed/level_stats.h. Simulation stays in Game. */
#include <string.h>
#include "engine/ui/front.h"
#include "engine/ui/ui.h"
#include "engine/ui/format.h"
#include "engine/ui/image.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/render.h"
static void border(int x,int y,int w,int h,unsigned light,unsigned dark,int size)
{
 uint16_t a=tr64_ui_colour(light),b=tr64_ui_colour(dark);
 tr64_ui_backend_rect(x,y,w,size,a);tr64_ui_backend_rect(x,y,size,h,a);
 tr64_ui_backend_rect(x,y+h-size,w,size,b);tr64_ui_backend_rect(x+w-size,y,size,h,b);
}
static void options(const tr64_game_menu_view_t *v)
{
 if(v->state!=TR64_GAME_MENU_STATE_SHOW || v->frameIndex!=v->frameTarget || !v->optionsCount)return;
 int w=v->optionsWidth,h=v->optionsHeight,y=(240-h)/2-12;
 tr64_ui_shade((320-w)/2+1,y+1,w-2,h-2,25);
 border((320-w)/2,y,w,h,14,10,2);w-=4;y+=2;
 for(int i=0;i<v->optionsCount;++i) {
  const tr64_game_menu_option_t *o=v->options+i;
  if(v->optionIndex==i)border((320-w)/2,y,w,18,15,15,1);
  if(o->type==TR64_GAME_MENU_OPTION_SPACE)y-=10;
  else if(o->type==TR64_GAME_MENU_OPTION_BAR)
   tr64_ui_bar(160,y+6,80,o->value,TR64_UI_OPTION);
  y+=18;
 }
}
static void paint_text(const tr64_game_menu_text_t *t,const tr64_game_level_stats_t *stats)
{
 int ok=0;
 switch(t->kind) {
 case TR64_GAME_TEXT_STRING:
  ok=t->value>=0 && tr64_ui_text_id(t->x,t->y,(unsigned)t->value,t->alignment);break;
 case TR64_GAME_TEXT_GLYPH:
  ok=t->value>=0 && tr64_ui_glyph(t->x,t->y,(unsigned)t->value,t->alignment);break;
 case TR64_GAME_TEXT_NUMBER: {
  char number[16];if(t->value<0)break;
  tr64_ui_count_text(number,(uint32_t)t->value,t->suffix);
  ok=tr64_ui_text(t->x,t->y,number,t->alignment);break;
 }
 case TR64_GAME_TEXT_STATS_BLOCK: {
  char block[192];if(!t->line_height || !tr64_ui_stats_text(block,stats))break;
  char *line=block;int y=t->y;ok=1;
  for(;;) {
   char *next=strchr(line,'@');if(next)*next=0;
   if(!tr64_ui_text(t->x,y,line,t->alignment)) { ok=0;break; }
   if(!next)break;
   line=next+1;y+=t->line_height;
  }
  break;
 }
 default:break;
 }
 if(!ok)sys_error("UI text kind=%u value=%ld unavailable",t->kind,(long)t->value);
}
static void slots(const tr64_ui_picker_t *p)
{
 int w=216,h=4+3*18,x=(320-w)/2,y=(240-h)/2-12;
 tr64_ui_backend_rect(x,y,w,h,0x0001); /* opaque: hides the options list or passport behind */
 tr64_ui_shade(x+1,y+1,w-2,h-2,25);border(x,y,w,h,14,10,2);
 for(unsigned i=0;i<3;++i) {
  int row=y+2+(int)i*18;char number[16];
  if(p->index==i)border(x+2,row,w-4,18,15,15,1);
  tr64_ui_count_text(number,i+1,0);
  if(!tr64_ui_text(x+12,row+16,number,TR64_GAME_TEXT_LEFT))sys_error("UI slot number");
  const char *name=p->valid[i]?tr64_ui_string(p->title[i]):p->unusable[i]?"Unusable":"Empty";
  if(!name)name="?";
  if(!tr64_ui_text(x+36,row+16,name,TR64_GAME_TEXT_LEFT))sys_error("UI slot name");
 }
}
static void texts(const tr64_game_menu_view_t *v)
{
 enum { CAPACITY=2*TR64_GAME_MENU_OPTIONS+10 };
 tr64_game_menu_text_t text[CAPACITY];uint32_t widths_count;
 const uint16_t *widths=tr64_ui_string_widths(&widths_count);
 uint32_t count=game_menu_text_view(v,widths,widths_count,text,CAPACITY);
 if(count>CAPACITY)sys_error("UI menu text capacity required=%lu",(unsigned long)count);
 for(uint32_t i=0;i<count;++i)paint_text(text+i,&v->stats);
}
/* Pickup notification: non-fixed OpenLara src/ui.h at 2333adef (PICKUP_SHOW_TIME,
 * update lerp, renderPickups offset/spin/fade). Declared differences: layout is
 * halved (pitch 48, margin 24, 0.25 pixel per model unit) for a 320x240 frame,
 * the reference's four point lights are the existing scalar lighting, and the
 * one-second alpha fade-in is a brightness ramp (the menu pass has no blend). */
enum { PICKUP_TICKS=150,PICKUP_FADE=30,PICKUP_PITCH=48,PICKUP_MARGIN=24,PICKUP_DEPTH=1024 };
#if TR64_HAS_TR2
static int dragon_sprite(const tr64_level_t *level,int32_t type)
{
 /* WALL.TR2 has one sprite each for the three dragon pickup types, no
  * ring mesh. They share the notice queue, but draw in the 2D pass. */
 if(level->game!=2 || type<190 || type>192)return -1;
 const tr64_level_table_t *seq=tr64_level_table(level,TR64_CORE_sprite_sequences);
 for(uint32_t i=0;i<seq->count;++i) {
  const tr64_core_sprite_sequences_t *entry=tr64_level_sprite_sequences(level,i);
  if(tr64_u32(entry->type)==(uint32_t)type)return tr64_u16(entry->first);
 }
 return -1;
}
static void dragon_notices(const tr64_ui_front_call_t *call)
{
 const tr64_ui_pickups_t *p=call->pickups;
 int drawing=0;
 for(uint32_t i=0;i<p->count;++i) {
  const tr64_ui_pickup_t *item=p->item+i;
  int sprite=dragon_sprite(call->level,item->type);
  if(sprite<0)continue;
  int64_t late=item->ticks<PICKUP_FADE?PICKUP_FADE-item->ticks:0;
  if(!drawing) { tr64_ui_backend_begin();drawing=1; }
  tr64_ui_pickup_sprite((unsigned)sprite,item->x/256+(int)(512*late*late*late*late/810000),
                        item->y/256);
 }
 if(drawing)tr64_ui_backend_end();
}
#endif
static void pickups_update(const tr64_ui_front_call_t *call)
{
 uint32_t n=game_pickup_event_count(),latest[TR64_GAME_PLAYERS]={0};
 tr64_game_view_t second;int have_second=0;
 unsigned seen=0;
 for(uint32_t i=0;i<n;++i) {
  tr64_game_pickup_event_t e;int64_t v[3];int32_t sx=160,sy=120;
  if(!game_pickup_event(i,&e))sys_error("UI pickup event %lu unavailable",(unsigned long)i);
  if(e.player>=TR64_GAME_PLAYERS)sys_error("UI pickup has invalid owner");
  if(e.player && !call->split)continue;
  tr64_ui_pickups_t *p=call->pickups+e.player;
  if(p->seen && e.tick<=p->last_tick)continue;
  if(e.tick>latest[e.player])latest[e.player]=e.tick;
  seen|=1u<<e.player;
  if(p->count>=TR64_UI_PICKUPS)continue;
  uint32_t parts=game_pickup_item_view(e.type,0,call->chunk,call->capacity);
#if TR64_HAS_TR2
  if(!parts && dragon_sprite(call->level,e.type)<0)continue;
#else
  if(!parts)continue; /* Source models without an inventory icon are not shown. */
#endif
  if(parts>call->capacity)sys_error("UI pickup capacity required=%lu",(unsigned long)parts);
  const tr64_game_view_t *view=call->view;
  if(e.player) {
   if(!have_second) { game_view_camera_player(1,&second);have_second=1; }
   view=&second;
  }
  for(unsigned a=0;a<3;++a) {
   v[a]=0;
   for(unsigned k=0;k<3;++k)v[a]+=(int64_t)view->camera_basis[3*a+k]*(e.position[k]-view->camera_eye[k]);
  }
  if(v[2]>0) {
   int64_t x=160+256*v[0]/v[2],y=120+256*v[1]/v[2];
   sx=(int32_t)(x<-2048?-2048:x>2048?2048:x);sy=(int32_t)(y<-2048?-2048:y>2048?2048:y);
  }
  if(e.player)log_printf("ui: pickup player=2 type=%ld tick=%lu screen=%ld,%ld\n",
                        (long)e.type,(unsigned long)e.tick,(long)sx,(long)sy+60);
  else log_printf("ui: pickup type=%ld tick=%lu screen=%ld,%ld\n",(long)e.type,(unsigned long)e.tick,(long)sx,(long)sy);
  p->item[p->count++]=(tr64_ui_pickup_t){e.type,sx*256,sy*256,PICKUP_TICKS};
 }
 for(unsigned slot=0;slot<(call->split?TR64_GAME_PLAYERS:1);++slot) {
  tr64_ui_pickups_t *p=call->pickups+slot;
  if(seen&(1u<<slot)) { p->seen=1;p->last_tick=latest[slot]; }
  for(uint32_t i=0;i<p->count;) {
   tr64_ui_pickup_t *item=p->item+i;
   if(!--item->ticks) { memmove(item,item+1,(p->count-i-1)*sizeof(*item));--p->count;continue; }
   int32_t tx=(320-PICKUP_MARGIN-(int32_t)(i%4)*PICKUP_PITCH)*256;
   int32_t ty=(call->split?180-PICKUP_MARGIN-(int32_t)(i/4)*32
                          :240-PICKUP_MARGIN-(int32_t)(i/4)*PICKUP_PITCH)*256;
   item->x+=(tx-item->x)/6;item->y+=(ty-item->y)/6;++i;
  }
 }
}
static void pickups_draw(const tr64_ui_front_call_t *call,unsigned slot)
{
 static const tr64_gpu_view_t camera={{16384,0,0,0,16384,0,0,0,16384},{0,0,0}};
 const tr64_ui_pickups_t *p=call->pickups+slot;
 if(call->split)tr64_render_scene_pickups_begin(&camera,slot);
 else tr64_render_scene_menu_begin(&camera);
 for(uint32_t i=0;i<p->count;++i) {
  const tr64_ui_pickup_t *item=p->item+i;
  /* fade: (1-min(elapsed,1 s))^4 of the neutral darkness; slide-out: (1-time)^4*512 px. */
  int64_t early=PICKUP_TICKS-item->ticks<PICKUP_FADE?PICKUP_FADE-(PICKUP_TICKS-item->ticks):0;
  int64_t late=item->ticks<PICKUP_FADE?PICKUP_FADE-item->ticks:0;
  int64_t dark=4096*early*early*early*early/810000,offset=512*late*late*late*late/810000;
  int32_t x=(int32_t)((item->x+offset*256-160*256)*4/256),y=(int32_t)((item->y-120*256)*4/256);
  uint32_t n=game_pickup_item_view(item->type,(int16_t)(uint16_t)(item->ticks*16384u/PICKUP_FADE),call->chunk,call->capacity);
  if(n>call->capacity)sys_error("UI pickup capacity required=%lu",(unsigned long)n);
  for(uint32_t j=0;j<n;++j) {
   call->chunk[j].translation[0]+=x;call->chunk[j].translation[1]+=y;call->chunk[j].translation[2]+=PICKUP_DEPTH;
   call->chunk[j].instance_id=0x40000100u+i;call->chunk[j].light_offset=-32-(int32_t)dark;
  }
  if(n)tr64_render_scene_menu_draw(call->chunk,n);
 }
}
uint32_t tr64_unit_front(uint32_t argument)
{
 const tr64_ui_front_call_t *call=(const tr64_ui_front_call_t *)(uintptr_t)argument;
 if(!call || !call->level || !call->chunk || !call->capacity || call->title>1)
  sys_error("UI invalid presentation call");
 if(!call->menu) {
  if(call->title || !call->pickups || !call->view)sys_error("UI HUD presentation unavailable");
  if(call->update==TR64_UI_UPDATE_TICK) { pickups_update(call);return 1; }
  if(call->update==TR64_UI_UPDATE_FRAME)pickups_update(call);
  if(call->pickups[0].count)pickups_draw(call,0);
  if(call->split && call->pickups[1].count)pickups_draw(call,1);
#if TR64_HAS_TR2
  if(call->level->game==2)dragon_notices(call);
#endif
  if(call->hud || call->hud_second) {
   tr64_ui_backend_begin();
   if(call->hud)paint_text(call->hud,NULL);
   if(call->hud_second)paint_text(call->hud_second,NULL);
   tr64_ui_backend_end();
  }
  return 1;
 }
 if(!call->camera)sys_error("UI menu camera unavailable");
 const tr64_game_menu_view_t *v=call->menu;
 if(v->itemsCount<0 || v->itemsCount>TR64_GAME_MENU_ITEMS ||
    v->optionsCount<0 || v->optionsCount>TR64_GAME_MENU_OPTIONS)
  sys_error("UI invalid menu view counts");
#if TR64_HAS_TR2 || TR64_GAME == 3
 if(call->title) {
  uint32_t img_bytes=0;
  if((call->level->game!=2 && call->level->game!=3) || tr64_level_title_image(call->level,&img_bytes))
   tr64_ui_title_image_draw(call->level);
 }
#else
 if(call->title)tr64_ui_title_image_draw(call->level);
#endif
 if(v->page!=TR64_GAME_MENU_PAGE_END && v->page!=TR64_GAME_MENU_PAGE_STATS && v->itemsCount) {
  tr64_render_scene_menu_begin(call->camera);
  for(uint32_t i=(uint32_t)v->itemsCount;i;--i) {
   uint32_t n=game_menu_item_view(v,i-1,call->chunk,call->capacity);
   if(n>call->capacity)sys_error("UI menu item capacity required=%lu",(unsigned long)n);
   tr64_render_scene_menu_draw(call->chunk,n);
  }
 }
 tr64_ui_backend_begin();options(v);
 if(v->showHealth)tr64_ui_bar(108,24,100,v->healthQ8,TR64_UI_HEALTH);
 texts(v);if(call->picker)slots(call->picker);tr64_ui_backend_end();return 1;
}
