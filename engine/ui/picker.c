/* SPDX-License-Identifier: MIT */
/* The slot picker and the one rule behind it: no SRAM save or load reaches the
 * game without a slot the player confirmed in the picker for that request.
 *
 * Where the game acts. Inside one game tick, inventory update runs onItem in
 * state SHOW only (game/inventory.c, Inventory_update): the option cursor moves
 * on a direction *edge*, the passport page changes on a LEFT/RIGHT edge, and
 * only then an ACTION edge is read against the cursor and page the tick
 * has just produced; the game's single save call and its LOAD transition hang
 * off that ACTION. The picker therefore decides from the menu view of the tick
 * before, and only that view is exact when nothing in the tick can move
 * cursor or page. An ACTION edge is:
 *   - passed, when no menu is up or the menu is not in SHOW (the game does not
 *     read ACTION there);
 *   - withheld (the game sees the rest of the tick's input, ACTION arrives as
 *     a new edge on the next tick while the button is held), when the menu is in
 *     SHOW but not settled (the tick may finish the passport animation and act),
 *     or a direction edge is in the same tick (cursor or page may move first);
 *   - turned into the picker when the settled menu's action hint is SAVE_SLOT or
 *     LOAD_SLOT: the game sees no input until the player accepts or cancels;
 *   - passed otherwise (the hint says the ACTION neither saves nor loads).
 * Accepting passes exactly one ACTION, after checking the menu still says the
 * same thing, and records the confirmation (tr64_save_game_confirm): the save
 * hook refuses without it and the play loop does not load without it. */
#include <string.h>
#include "engine/ui/picker.h"
#include "engine/ui/front.h"
#include "engine/save/game.h"
#include "engine/core/log.h"
#include "tr64/game.h"
static tr64_ui_picker_t picker;
static uint16_t raw_previous,delivered,cancel_held;
static int enabled;
static unsigned player;
void tr64_ui_picker_start(void)
{ enabled=1; }
void tr64_ui_picker_stop(void)
{
 enabled=0;player=0;memset(&picker,0,sizeof(picker));raw_previous=delivered=cancel_held=0;
 tr64_save_game_confirm_reset();
}
void tr64_ui_picker_owner(unsigned slot)
{
 if(slot==player)return;
 player=slot;picker.open=0;raw_previous=delivered=cancel_held=0;
 tr64_save_game_confirm_reset();
}
const tr64_ui_picker_t *tr64_ui_picker_view(void)
{ return picker.open ? &picker : NULL; }
enum { DIRECTIONS=TR64_GAME_LEFT|TR64_GAME_RIGHT|TR64_GAME_FORWARD|TR64_GAME_BACK };
int tr64_ui_picker_classify(const tr64_game_menu_view_t *menu,int active,uint16_t edge)
{
 if(!(edge&TR64_GAME_ACTION) || !active)return TR64_UI_PICK_PASS;
 /* SELECT never acts on ACTION (and is the state that becomes SHOW); in SHOW the
  * hint is exact only for a settled menu and without a cursor/page move. */
 if(menu->state==TR64_GAME_MENU_STATE_SELECT)return TR64_UI_PICK_WITHHOLD;
 if(menu->state!=TR64_GAME_MENU_STATE_SHOW)return TR64_UI_PICK_PASS;
 if(menu->frameIndex!=menu->frameTarget || (edge&DIRECTIONS))return TR64_UI_PICK_WITHHOLD;
 if(menu->actionHint==TR64_GAME_MENU_ACTION_SAVE_SLOT ||
    menu->actionHint==TR64_GAME_MENU_ACTION_LOAD_SLOT)return TR64_UI_PICK_OPEN;
 return TR64_UI_PICK_PASS;
}
static void open_picker(int load)
{
 memset(&picker,0,sizeof(picker));picker.load=(uint32_t)load;picker.index=tr64_save_game_selected();
 for(unsigned i=0;i<3;++i) {
  tr64_save_info_t info;int status=tr64_save_slot_status(i,&info);
  picker.valid[i]=status==TR64_SAVE_OK;picker.title[i]=picker.valid[i]?info.stats.titleID:0;
  picker.unusable[i]=status!=TR64_SAVE_OK && status!=TR64_SAVE_EMPTY;
 }
 if(load && !picker.valid[picker.index])
  for(unsigned i=0;i<3;++i)if(picker.valid[i]) { picker.index=i;break; }
 picker.open=1;
 log_printf("ui: picker open load=%u valid=%u%u%u unusable=%u%u%u index=%u\n",(unsigned)picker.load,
  (unsigned)picker.valid[0],(unsigned)picker.valid[1],(unsigned)picker.valid[2],
  (unsigned)picker.unusable[0],(unsigned)picker.unusable[1],(unsigned)picker.unusable[2],(unsigned)picker.index);
}
uint16_t tr64_ui_picker_filter(uint16_t actions)
{
 const uint16_t raw_edge=(uint16_t)(actions&~raw_previous);
 raw_previous=actions;
 tr64_save_game_confirm_tick();
 if(!enabled) { delivered=actions;return actions; }
 /* The button that cancelled the picker stays down for a few ticks, and the game's
  * passport leaves on a held JUMP or SELECT (onItem): it never sees that button
  * until it is released, so the cancel does not also leave the passport. */
 cancel_held&=actions;actions&=(uint16_t)~cancel_held;
 if(picker.open) {
  delivered=0;
  if(raw_edge&(TR64_GAME_JUMP|TR64_GAME_SELECT)) { picker.open=0;cancel_held|=(uint16_t)(raw_edge&(TR64_GAME_JUMP|TR64_GAME_SELECT));return 0; }
  {
   const uint32_t before=picker.index;
   if(raw_edge&TR64_GAME_FORWARD)picker.index=(picker.index+2)%3;
   if(raw_edge&TR64_GAME_BACK)picker.index=(picker.index+1)%3;
   /* The shipping gate presses until it reads the wanted index here: a lost or doubled press shows. */
   if(picker.index!=before)log_printf("ui: picker index=%u\n",(unsigned)picker.index);
  }
  /* A direction in the same tick moves the cursor; accepting needs a press of its own. */
  if((raw_edge&TR64_GAME_ACTION) && !(raw_edge&(TR64_GAME_FORWARD|TR64_GAME_BACK))) {
   tr64_game_menu_view_t menu;
   const int active=game_menu_view(&menu);
   const int hint=active && menu.state==TR64_GAME_MENU_STATE_SHOW && menu.frameIndex==menu.frameTarget
    ? menu.actionHint : TR64_GAME_MENU_ACTION_NONE;
   const int wanted=picker.load ? TR64_GAME_MENU_ACTION_LOAD_SLOT : TR64_GAME_MENU_ACTION_SAVE_SLOT;
   if(hint!=wanted || (picker.load && !picker.valid[picker.index])) {
    if(hint!=wanted)picker.open=0; /* the menu moved on: nothing to confirm */
    return 0;
   }
   tr64_save_game_confirm(picker.index,(int)picker.load);
   log_printf("ui: picker accept slot=%u load=%u\n",(unsigned)picker.index,(unsigned)picker.load);
   picker.open=0;delivered=TR64_GAME_ACTION;return TR64_GAME_ACTION;
  }
  return 0;
 }
 {
  uint16_t out=actions;
  const uint16_t edge=(uint16_t)(out&~delivered);
  if(edge&TR64_GAME_ACTION) {
   tr64_game_menu_view_t menu;
   const int active=game_menu_view(&menu);
   switch(tr64_ui_picker_classify(&menu,active,edge)) {
   case TR64_UI_PICK_WITHHOLD:out&=(uint16_t)~TR64_GAME_ACTION;break;
   case TR64_UI_PICK_OPEN:
    /* The passport handles a blocked save itself. No slot is selected
     * and no confirmation is needed for the refusal. */
    if(menu.actionHint==TR64_GAME_MENU_ACTION_SAVE_SLOT && game_save_blocked())break;
    open_picker(menu.actionHint==TR64_GAME_MENU_ACTION_LOAD_SLOT);delivered=0;return 0;
   default:break;
   }
  }
  delivered=out;return out;
 }
}
