/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "engine/ui/frontend.h"
#include "engine/ui/front.h"
#include "engine/ui/picker.h"
#include "engine/ui/ui.h"
#include "engine/save/game.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/render.h"
const tr64_ui_front_call_t *tr64_ui_front_frame;
static tr64_ui_pickups_t pickups[TR64_GAME_PLAYERS];
static struct {
 const tr64_level_t *level;
 uint32_t title,rumble_started[TR64_GAME_PLAYERS];
 int32_t shown;uint32_t frames;
 unsigned rumble;
 int menu_shown,ticked;
 uint8_t warp_notice[TR64_GAME_PLAYERS];
} presentation;
static void rumble_stop(unsigned slot)
{
 if(presentation.rumble&(1u<<slot)) {
  (void)tr64_ui_backend_rumble(slot,0);
  presentation.rumble&=~(1u<<slot);
 }
}
void eng_game_vibrate(void)
{
 const tr64_game_settings_t *settings=game_settings_view();
 if(!settings)sys_error("UI vibration settings unavailable");
 if(!settings->controls_vibration) {
  for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)rumble_stop(slot);
  return;
 }
 const unsigned slot=game_menu_player(); /* The vibration option is the only game rumble event. */
 if(tr64_ui_backend_rumble(slot,1)) {
  presentation.rumble|=1u<<slot;
  presentation.rumble_started[slot]=sys_time_us();
 } else presentation.rumble&=~(1u<<slot);
}
#if TR64_HAS_TR2
static void front_lease(int needed)
{
 if(presentation.level->game!=2 || code_is_loaded("front")==needed)return;
 code_loading_begin();
 if(needed)code_load("front");else code_unload("front");
 code_loading_end();
}
#endif
void tr64_ui_game_start(const tr64_level_t *level,const char *name)
{
 if(presentation.level || !name || !level || !level->ready)sys_error("UI presentation start order");
#if TR64_HAS_TR2
 if(TR64_CART_TR2 || level->game==2) {
  if(level->game!=2)sys_error("Tomb Raider II: UI requires a sequel level");
  if(!tr64_ui_bind(level))sys_error("Tomb Raider II: UI font unavailable");
  presentation.level=level;presentation.title=!strcmp(name,"title");
  code_loading_begin();(void)tr64_save_game_start();code_loading_end();
  tr64_ui_set_active(1);tr64_ui_picker_start();
  return;
 }
#endif
#if TR64_GAME == 3
 if(level->game==3) {
  if(!tr64_ui_bind(level))sys_error("Tomb Raider III: UI font unavailable");
  presentation.level=level;presentation.title=!strcmp(name,"title");
  code_loading_begin();
  (void)tr64_save_game_start();
  if(!code_is_loaded("front"))code_load("front");
  code_loading_end();
  tr64_ui_set_active(1);tr64_ui_picker_start();
  return;
 }
#endif
 presentation.level=level;presentation.title=!strcmp(name,"title");
 (void)tr64_save_game_start();code_load("front");tr64_ui_set_active(1);tr64_ui_picker_start();
}
void tr64_ui_game_stop(void)
{
 if(tr64_ui_front_frame)sys_error("UI release during frame");
#if TR64_HAS_TR2
 if(presentation.level && presentation.level->game==2) {
  for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)rumble_stop(slot);
  tr64_ui_picker_stop();tr64_save_game_stop();
  front_lease(0);
  if(code_is_loaded("medium")) {
   code_loading_begin();code_unload("medium");code_loading_end();
  }
  tr64_ui_set_active(0);tr64_ui_unbind();
  memset(&presentation,0,sizeof(presentation));memset(pickups,0,sizeof(pickups));
  return;
 }
#endif
#if TR64_GAME == 3
 if(presentation.level && presentation.level->game==3) {
  for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)rumble_stop(slot);
  tr64_ui_picker_stop();tr64_save_game_stop();
  code_loading_begin();
  if(code_is_loaded("front"))code_unload("front");
  if(code_is_loaded("medium"))code_unload("medium");
  code_loading_end();
  tr64_ui_set_active(0);tr64_ui_unbind();
  memset(&presentation,0,sizeof(presentation));memset(pickups,0,sizeof(pickups));
  return;
 }
#endif
 for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)rumble_stop(slot);
 tr64_ui_picker_stop();tr64_save_game_stop();tr64_ui_set_active(0);tr64_ui_unbind();memset(&presentation,0,sizeof(presentation));memset(pickups,0,sizeof(pickups));
}
/* The rumble pulse is a quarter second of the clock, however slowly frames come. */
static void rumble_service(void)
{
 if(!presentation.rumble)return;
 const uint32_t now=sys_time_us();
 for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
  if((presentation.rumble&(1u<<slot)) && (uint32_t)(now-presentation.rumble_started[slot])>=250000u)
   rumble_stop(slot);
}
void tr64_ui_game_poll(void) { rumble_service(); }
void tr64_ui_coop_no_medipack(unsigned slot)
{
 presentation.warp_notice[slot]=2*TR64_TICK_HZ;
}
void tr64_ui_coop_warped(unsigned slot) { presentation.warp_notice[slot]=0; }
void tr64_ui_coop_leave(void)
{
 rumble_stop(1);
 memset(&pickups[1],0,sizeof(pickups[1]));
 memset(presentation.warp_notice,0,sizeof(presentation.warp_notice));
}
void tr64_ui_game_tick(const tr64_game_view_t *view,tr64_game_drawable_t *chunk,uint32_t capacity)
{
 tr64_game_menu_view_t menu;
 if(!presentation.level)return;
 presentation.ticked=1;rumble_service();
 /* Pickup notifications wait while the ring or the title is up (as they always did) and run, with the
  * game's per-tick pickup events, once per tick: the game replaces those events at its next tick. */
 if(presentation.title || game_menu_view(&menu)) {
  tr64_render_preview_stop();return;
 }
 const int split=!TR64_CART_TR2 && (game_player_mask()&2u)!=0;
 if(split)for(unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
  if(presentation.warp_notice[slot])--presentation.warp_notice[slot];
 if(!pickups[0].count && !pickups[1].count && !game_pickup_event_count())return;
#if TR64_HAS_TR2
 tr64_game_view_t current;
 if(presentation.level->game==2) {
  if(!view) { game_view_camera(&current);view=&current; }
  front_lease(1);
 }
#endif
 if(!view || !chunk || !capacity || tr64_ui_front_frame)sys_error("UI invalid/reentrant tick");
 tr64_ui_front_call_t call={.level=presentation.level,.view=view,.pickups=pickups,.chunk=chunk,.capacity=capacity,
                            .update=TR64_UI_UPDATE_TICK,.split=(uint8_t)split};
 if(code_call("front",(uint32_t)(uintptr_t)&call)!=1)sys_error("UI presentation unit failed");
}
int tr64_ui_menu_was_shown(void) { return presentation.menu_shown; }
int tr64_ui_game_frame(const tr64_game_view_t *view,tr64_game_drawable_t *chunk,uint32_t capacity)
{
 if(!presentation.level)return 0;
 if(!view || !chunk || !capacity || tr64_ui_front_frame)sys_error("UI invalid/reentrant frame");
 rumble_service();
 tr64_game_menu_view_t menu;
 ++presentation.frames;
 int active=game_menu_view(&menu);presentation.menu_shown=active;
 if(active || presentation.title)tr64_render_preview_stop();
 const int split=!TR64_CART_TR2 && !active && !presentation.title && (game_player_mask()&2u);
 int32_t shown=active ? menu.state|menu.page<<8|menu.passportPage<<16 : 0;
 if(shown!=presentation.shown) {
  presentation.shown=shown;
  log_printf("ui: menu frame=%lu state=%ld page=%ld passport=%ld item=%ld\n",(unsigned long)presentation.frames,active ? (long)menu.state : 0,
   active ? (long)menu.page : 0,active ? (long)menu.passportPage : 0,active ? (long)menu.itemIndex : 0);
 }
 tr64_game_menu_text_t hud,hud_second;
 uint32_t hud_count=active?0:game_hud_text_view(&hud,1);
 uint32_t second_count=split?game_hud_text_view_player(1,&hud_second,1):0;
 if(hud_count>1 || second_count>1)sys_error("UI HUD text capacity");
 if(second_count)hud_second.y+=120;
 tr64_gpu_view_t camera;const tr64_gpu_view_t *menu_camera=NULL;
 if(active || presentation.title) {
  if(!active) { memset(&menu,0,sizeof(menu));menu.state=TR64_GAME_MENU_STATE_NONE; }
  memcpy(camera.basis,active?menu.camera_basis:view->camera_basis,sizeof(camera.basis));
  memcpy(camera.eye,active?menu.camera_eye:view->camera_eye,sizeof(camera.eye));menu_camera=&camera;
 }
 tr64_ui_front_call_t call;
 const tr64_ui_picker_t *open_picker=tr64_ui_picker_view();
 if(active || presentation.title || split || hud_count || pickups[0].count || open_picker || game_pickup_event_count()) {
  call=(tr64_ui_front_call_t){.level=presentation.level,.view=view,.pickups=pickups,.picker=open_picker,.menu=menu_camera?&menu:NULL,
   .camera=menu_camera,.hud=hud_count?&hud:NULL,.hud_second=second_count?&hud_second:NULL,
   .warp_notice=split?presentation.warp_notice:NULL,
   .chunk=chunk,.capacity=capacity,.title=presentation.title,
   .update=presentation.ticked ? TR64_UI_UPDATE_NONE : TR64_UI_UPDATE_FRAME,.split=(uint8_t)(split!=0)};
  tr64_ui_front_frame=&call;
 }
#if TR64_HAS_TR2
 front_lease(tr64_ui_front_frame!=NULL);
#endif
 presentation.ticked=0;
 if(presentation.title)tr64_render_menu_scene_frame(&camera);
 else if(split) {
  tr64_game_view_t second;game_view_camera_player(1,&second);
  tr64_render_game_scene_coop_frame(view,&second,chunk,capacity);
 }
 else tr64_render_game_scene_frame(view,chunk,capacity);
 tr64_ui_front_frame=NULL;return 1;
}
