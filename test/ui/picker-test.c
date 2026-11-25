/* SPDX-License-Identifier: MIT */
/* The slot picker against a transliteration of the game's passport tick
 * (game/inventory.c Inventory_update SHOW/READY/SELECT, onOption, onPassport,
 * actionHint, saveResult; game/game.c game_tick's LOAD stop), the real picker,
 * the real save glue and the real medium over a RAM device. Invariants over
 * exhaustive short and many random input sequences: a save reaches the medium
 * only for the slot the player accepted in the picker for that request, a LOAD
 * stop is preceded by a confirmed slot, and every refused attempt is counted
 * (zero with the picker; plenty without it, which is the negative control). */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/ui/picker.h"
#include "engine/save/game.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/res.h"
/* ---- device and services ---- */
static uint8_t bank[TR64_SAVE_BYTES];
static int unit_loaded;
static int blocked_save, fail_write, save_result_code;
static unsigned writes, serializations;
extern uint32_t tr64_unit_medium(uint32_t argument);
void code_load(const char *name) { assert(!strcmp(name,"medium"));unit_loaded=1; }
uint32_t code_call(const char *name,uint32_t argument)
{ assert(unit_loaded && !strcmp(name,"medium"));return tr64_unit_medium(argument); }
int tr64_save_platform_init(void) { return TR64_SAVE_OK; }
int tr64_save_platform_read(uint32_t offset,void *data,uint32_t bytes)
{ assert(offset<=sizeof(bank) && bytes<=sizeof(bank)-offset);memcpy(data,bank+offset,bytes);return 0; }
int tr64_save_platform_write(uint32_t offset,const void *data,uint32_t bytes)
{ assert(offset<=sizeof(bank) && bytes<=sizeof(bank)-offset);++writes;
  if(fail_write)return -1;
  memcpy(bank+offset,data,bytes);return 0; }
void sys_error(const char *fmt,...)
{ va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);exit(1); }
/* The gate reads the cursor from the picker's log: one line per move, none when it does not move. */
static unsigned index_lines;static int last_index=-1;
void log_printf(const char *fmt,...)
{
 if(!strcmp(fmt,"ui: picker index=%u\n")) { va_list ap;va_start(ap,fmt);last_index=(int)va_arg(ap,unsigned);va_end(ap);++index_lines; }
}
void log_flush(void) { }
uint32_t sys_time_us(void) { static uint32_t now;return now+=1000; }
static uint8_t workspace[8192] __attribute__((aligned(16)));
void *tr64_mem_alloc(int pool,uint32_t bytes) { assert(pool==TR64_MEM_state && bytes==TR64_SAVE_REGION_BYTES && bytes<=sizeof(workspace));return workspace; }
uint32_t tr64_save_level_identity(int32_t level_id) { return 0xa5000000u+(uint32_t)level_id; }
/* ---- the game's save serializer, reduced to what the glue touches ---- */
int32_t game_level_id(void) { return 2; }
int game_save_blocked(void) { return blocked_save; }
uint32_t game_save(void *buffer,uint32_t capacity)
{ assert(buffer && capacity>=100);++serializations;memset(buffer,0x5a,100);return 100; }
int game_load(const void *buffer,uint32_t bytes) { (void)buffer;(void)bytes;return 1; }
int game_stats_view(tr64_game_level_stats_t *out) { memset(out,0,sizeof(*out));out->titleID=7;return 1; }
const tr64_game_settings_t *game_settings_view(void) { static const tr64_game_settings_t s={0}; return &s; }
int game_settings_apply(const tr64_game_settings_t *s) { (void)s;return 1; }
/* ---- the passport, as game/inventory.c runs it ---- */
enum { LVL_NONE=-1,LVL_L1=1,LVL_L2=2,LVL_LOAD=100,LVL_TITLE=0 };
enum { PP_LOAD=0,PP_SAVE=1,PP_EXIT=2 };
enum { OPT_BUTTON=1,OPT_SPACE=0 };
enum { ANIM=6 };
static struct {
 int active,state,nextState,timer,page,passportPage,frameIndex,frameTarget,optionIndex,optionsCount;
 int type[4],value[4];
 uint16_t input,lastInput;
 int pending,stopped,has_save;
} m;
static unsigned stops,loads,save_ok,save_refused,load_unconfirmed,slot_mismatch,picker_opened,picker_accepts;
static int expected_slot;
static int hint(void)
{
 if(m.state!=TR64_GAME_MENU_STATE_SHOW || m.frameIndex!=m.frameTarget)return TR64_GAME_MENU_ACTION_NONE;
 if(m.passportPage==PP_SAVE) {
  if(m.page==TR64_GAME_MENU_PAGE_DEATH || m.page==TR64_GAME_MENU_PAGE_TITLE)return TR64_GAME_MENU_ACTION_NONE;
  return TR64_GAME_MENU_ACTION_SAVE_SLOT;
 }
 if(m.passportPage==PP_LOAD && m.optionsCount>0 && m.value[m.optionIndex]==LVL_LOAD)
  return TR64_GAME_MENU_ACTION_LOAD_SLOT;
 return TR64_GAME_MENU_ACTION_NONE;
}
int game_menu_view(tr64_game_menu_view_t *out)
{
 if(!m.active)return 0;
 memset(out,0,sizeof(*out));
 out->state=m.state;out->page=m.page;out->passportPage=m.passportPage;
 out->frameIndex=m.frameIndex;out->frameTarget=m.frameTarget;out->optionIndex=m.optionIndex;
 out->optionsCount=m.optionsCount;out->actionHint=hint();
 return 1;
}
static void init_options(void)
{
 m.optionsCount=m.optionIndex=0;
 if(m.passportPage==PP_LOAD) {
  m.type[0]=m.type[1]=OPT_BUTTON;m.value[0]=LVL_L1;m.value[1]=LVL_L2;m.optionsCount=2;
  if(m.has_save) { m.type[2]=OPT_SPACE;m.value[2]=0;m.type[3]=OPT_BUTTON;m.value[3]=LVL_LOAD;m.optionsCount=4;m.optionIndex=3; }
 }
}
static int key_hit(unsigned key) { return (m.input&key) && !(m.lastInput&key); }
static void close_menu(void) { m.state=TR64_GAME_MENU_STATE_CLOSING;m.nextState=TR64_GAME_MENU_STATE_NONE;m.timer=16; }
static void set_state(int state,int next,int timer) { m.state=state;m.nextState=next;m.timer=timer; }
static void select_passport(void)
{
 set_state(TR64_GAME_MENU_STATE_SELECT,TR64_GAME_MENU_STATE_SHOW,8);
 m.passportPage=(m.page==TR64_GAME_MENU_PAGE_OPTIONS || m.page==TR64_GAME_MENU_PAGE_DEATH) ? PP_SAVE : PP_LOAD;
 m.frameTarget=ANIM+5*m.passportPage;
 init_options();
}
static void next_level(int level) { m.pending=level;m.frameTarget=0;m.nextState=TR64_GAME_MENU_STATE_CLOSE; }
static int animate(void)
{
 if(m.frameIndex==m.frameTarget)return 0;
 if(m.frameIndex<m.frameTarget) { if(++m.frameIndex>=m.frameTarget) { m.frameIndex=m.frameTarget;return 0; } }
 else if(--m.frameIndex<=m.frameTarget) { m.frameIndex=m.frameTarget;return 0; }
 return 1;
}
static void finish_show(void)
{
 if(m.nextState!=TR64_GAME_MENU_STATE_NONE && m.frameIndex==m.frameTarget) {
  set_state(TR64_GAME_MENU_STATE_DESELECT,m.nextState,8);
 }
}
static int save_result(int success)
{
 if(success) { m.frameTarget=0;m.nextState=TR64_GAME_MENU_STATE_CLOSE; }
 else { m.stopped=1; } /* a failed save: the scenario ends, the counter says why */
 return success;
}
static void on_passport(int *save_action)
{
 if(m.page==TR64_GAME_MENU_PAGE_OPTIONS || m.page==TR64_GAME_MENU_PAGE_DEATH) {
  int change=0;
  if(m.passportPage==PP_LOAD && key_hit(TR64_GAME_RIGHT)) { m.passportPage=PP_SAVE;m.frameTarget+=5;change=1; }
  else if(m.passportPage==PP_SAVE && key_hit(TR64_GAME_RIGHT)) { m.passportPage=PP_EXIT;m.frameTarget+=5;change=1; }
  else if(m.passportPage==PP_EXIT && key_hit(TR64_GAME_LEFT)) { m.passportPage=PP_SAVE;m.frameTarget-=5;change=1; }
  else if(m.passportPage==PP_SAVE && key_hit(TR64_GAME_LEFT)) { m.passportPage=PP_LOAD;m.frameTarget-=5;change=1; }
  if(change)init_options();
 }
 if(!key_hit(TR64_GAME_ACTION))return;
 if(m.page==TR64_GAME_MENU_PAGE_DEATH && m.passportPage==PP_SAVE) { next_level(LVL_L2);return; }
 if(m.passportPage==PP_LOAD) {
  if(m.optionsCount<=0)return;
  next_level(m.value[m.optionIndex]);
 }
 if(m.passportPage==PP_SAVE) { *save_action=1;return; }
 if(m.passportPage==PP_EXIT) {
  m.frameTarget=0;
  if(m.page==TR64_GAME_MENU_PAGE_TITLE)m.nextState=TR64_GAME_MENU_STATE_READY;
  else { next_level(LVL_TITLE);m.nextState=TR64_GAME_MENU_STATE_CLOSE; }
 }
}
static void on_item(int *save_action)
{
 /* onOption */
 if(m.optionsCount) {
  if(key_hit(TR64_GAME_BACK)) {
   for(int i=0;i<m.optionsCount;++i) {
    if(++m.optionIndex>=m.optionsCount)m.optionIndex=0;
    if(m.type[m.optionIndex]==OPT_SPACE)continue;
    break;
   }
  }
  if(key_hit(TR64_GAME_FORWARD)) {
   for(int i=0;i<m.optionsCount;++i) {
    if(--m.optionIndex<0)m.optionIndex=m.optionsCount-1;
    if(m.type[m.optionIndex]==OPT_SPACE)continue;
    break;
   }
  }
 }
 if(m.input&(TR64_GAME_JUMP|TR64_GAME_SELECT)) { m.frameTarget=0;m.nextState=TR64_GAME_MENU_STATE_READY; }
 on_passport(save_action);
}
static void game_inventory_tick(uint16_t input)
{
 m.lastInput=m.input;m.input=input;
 if(m.state!=TR64_GAME_MENU_STATE_SHOW) { /* rotItem */ }
 if(m.timer>0) {
  m.timer-=1;
  if(m.timer<=0) { m.timer=0;m.state=m.nextState;m.nextState=TR64_GAME_MENU_STATE_NONE; }
 }
 switch(m.state) {
 case TR64_GAME_MENU_STATE_CLOSE:close_menu();break;
 case TR64_GAME_MENU_STATE_READY:
  if(m.input&TR64_GAME_LEFT) { }
  else if(m.input&TR64_GAME_RIGHT) { }
  else if(m.input&TR64_GAME_FORWARD) { if(m.page==TR64_GAME_MENU_PAGE_OPTIONS)m.stopped=1; /* leaves for the main page */ }
  else if(m.input&TR64_GAME_BACK) { }
  else if((key_hit(TR64_GAME_SELECT) || key_hit(TR64_GAME_JUMP)) && m.page!=TR64_GAME_MENU_PAGE_TITLE) close_menu();
  else if(m.input&TR64_GAME_ACTION)select_passport();
  break;
 case TR64_GAME_MENU_STATE_DEATH:select_passport();break;
 case TR64_GAME_MENU_STATE_SHOW: {
  int save_action=0;
  if(animate())break;
  on_item(&save_action);
  /* A requested save returns from the update before finishShow; the frontend
   * then calls the save hook and saveResult runs finishShow itself. */
  if(m.nextState!=TR64_GAME_MENU_STATE_NONE && !save_action)finish_show();
  if(save_action) {
   save_result_code=eng_game_save();
   if(save_result_code==TR64_ENG_SAVE_OK) {
    ++save_ok;if(tr64_save_game_selected()!=(unsigned)expected_slot)++slot_mismatch;
   } else ++save_refused;
   save_result(save_result_code==TR64_ENG_SAVE_OK);
   finish_show();
  }
  break;
 }
 default:break;
 }
 if(m.state==TR64_GAME_MENU_STATE_NONE) {
  m.active=0;
  if(m.pending!=LVL_NONE)m.stopped=1;
 }
}
/* ---- driver ---- */
/* Negative control: the previous implementation (engine/ui/frontend.c at
 * 0368750), which opened the picker from the ACTION edge and the menu view of the
 * tick before and passed everything else, direction and ACTION in one tick too. */
static int bypass;
static struct { int open,load; unsigned index; uint16_t previous; } old;
static uint16_t old_filter(uint16_t actions)
{
 const uint16_t edge=(uint16_t)(actions&~old.previous);old.previous=actions;
 tr64_save_game_confirm_tick();
 if(!old.open) {
  tr64_game_menu_view_t v;
  if(!(edge&TR64_GAME_ACTION) || !game_menu_view(&v) || v.actionHint==TR64_GAME_MENU_ACTION_NONE)return actions;
  old.load=v.actionHint==TR64_GAME_MENU_ACTION_LOAD_SLOT;old.index=tr64_save_game_selected();old.open=1;return 0;
 }
 if(edge&(TR64_GAME_JUMP|TR64_GAME_SELECT)) { old.open=0;return 0; }
 if(edge&TR64_GAME_FORWARD)old.index=(old.index+2)%3;
 if(edge&TR64_GAME_BACK)old.index=(old.index+1)%3;
 if(edge&TR64_GAME_ACTION) {
  tr64_save_info_t info;
  if(old.load && tr64_save_slot_status(old.index,&info)!=TR64_SAVE_OK)return 0;
  tr64_save_game_confirm(old.index,old.load);old.open=0;return TR64_GAME_ACTION;
 }
 return 0;
}
static void open_scenario(int page,int has_save)
{
 memset(&m,0,sizeof(m));
 m.page=page;m.has_save=has_save;m.pending=LVL_NONE;m.active=1;
 m.state=page==TR64_GAME_MENU_PAGE_DEATH?TR64_GAME_MENU_STATE_DEATH:TR64_GAME_MENU_STATE_READY;
 tr64_ui_picker_stop();tr64_ui_picker_start();memset(&old,0,sizeof(old));
}
static void settle_to_passport(void)
{
 /* Select the passport and let its animation finish: ACTION once, then idle. */
 if(m.state==TR64_GAME_MENU_STATE_READY) { game_inventory_tick(tr64_ui_picker_filter(TR64_GAME_ACTION)); }
 for(int i=0;i<64 && !(m.state==TR64_GAME_MENU_STATE_SHOW && m.frameIndex==m.frameTarget);++i)
  game_inventory_tick(tr64_ui_picker_filter(0));
 assert(m.state==TR64_GAME_MENU_STATE_SHOW && m.frameIndex==m.frameTarget);
}
static void step(uint16_t raw)
{
 const tr64_ui_picker_t *open=tr64_ui_picker_view();
 const int index=open?(int)open->index:-1;
 uint16_t delivered=bypass?old_filter(raw):tr64_ui_picker_filter(raw);
 if(!bypass && open && !tr64_ui_picker_view() && (delivered&TR64_GAME_ACTION)) { expected_slot=index;++picker_accepts; }
 if(!bypass && !open && tr64_ui_picker_view())++picker_opened;
 if(bypass)expected_slot=old.open?-2:expected_slot;
 const int was_stopped=m.stopped;
 game_inventory_tick(delivered);
 if(m.stopped) {
  ++stops;
  if(m.pending==LVL_LOAD && !was_stopped) {
   ++loads;if(!tr64_save_game_take_load_confirmation())++load_unconfirmed;
   else if(tr64_save_game_selected()!=(unsigned)expected_slot)++slot_mismatch;
  }
 }
}
static uint32_t rng=0x1234567u;
static uint32_t rnd(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static const uint16_t symbol[]={
 0,TR64_GAME_ACTION,TR64_GAME_JUMP,TR64_GAME_SELECT,TR64_GAME_FORWARD,TR64_GAME_BACK,
 TR64_GAME_LEFT,TR64_GAME_RIGHT,
 TR64_GAME_ACTION|TR64_GAME_FORWARD,TR64_GAME_ACTION|TR64_GAME_BACK,
 TR64_GAME_ACTION|TR64_GAME_LEFT,TR64_GAME_ACTION|TR64_GAME_RIGHT,
 TR64_GAME_ACTION|TR64_GAME_JUMP,TR64_GAME_ACTION|TR64_GAME_SELECT
};
enum { SYMBOLS=sizeof(symbol)/sizeof(symbol[0]) };
static void make_device(int with_slot1)
{
 memset(bank,0xff,sizeof(bank));
 tr64_save_game_stop();tr64_save_game_confirm(0,0);tr64_save_game_confirm_reset(); /* remembered slot 0 */
 tr64_save_unit_load();(void)tr64_save_game_start();
 if(with_slot1) {
  tr64_save_info_t info={.level_id=2,.identity=tr64_save_level_identity(2)};
  static uint8_t data[128] __attribute__((aligned(16)));memset(data,0x33,sizeof(data));
  assert(tr64_save_write(1,&info,data,100)==TR64_SAVE_OK);
 }
}
static void reset_counts(void)
{ stops=loads=save_ok=save_refused=load_unconfirmed=slot_mismatch=picker_opened=picker_accepts=0; }
/* Exhaustive: every sequence of `depth` symbols from the passport selected and settled. */
static void exhaustive(int page,int has_save,int depth)
{
 unsigned long total=1;for(int i=0;i<depth;++i)total*=SYMBOLS;
 for(unsigned long n=0;n<total;++n) {
  make_device(has_save);
  open_scenario(page,has_save);settle_to_passport();
  unsigned long v=n;
  for(int i=0;i<depth && !m.stopped && m.active;++i) { step(symbol[v%SYMBOLS]);v/=SYMBOLS; }
 }
}
static void random_run(int page,int has_save,unsigned sequences,int length)
{
 for(unsigned s=0;s<sequences;++s) {
  make_device(has_save);
  open_scenario(page,has_save);
  uint16_t last=0;
  for(int i=0;i<length && !m.stopped && m.active;++i) {
   if(rnd()&1)last=symbol[rnd()%SYMBOLS]; /* else hold: buttons stay down across ticks */
   step(last);
  }
 }
}
static void run_all(void)
{
 static const int pages[]={TR64_GAME_MENU_PAGE_OPTIONS,TR64_GAME_MENU_PAGE_TITLE,TR64_GAME_MENU_PAGE_DEATH};
 for(unsigned p=0;p<3;++p)for(int has_save=0;has_save<2;++has_save) {
  exhaustive(pages[p],has_save,4);
  random_run(pages[p],has_save,4000,60);
 }
}
static void idle(int n) { for(int i=0;i<n;++i)step(0); }
static void settled(void) { idle(24);assert(m.state==TR64_GAME_MENU_STATE_SHOW && m.frameIndex==m.frameTarget); }
/* Two of the review's bypasses, with the exact answer of the picker. */
static void named_cases(void)
{
 /* Options page, LOAD page, LEVEL1 highlighted, a save only in slot 1 (remembered
  * slot 0 is empty): UP+ACTION in one tick moves the cursor to CURRENT POSITION.
  * Unfiltered, the game loads slot 0; filtered, ACTION is withheld, then the
  * (still held) button opens the load picker, which offers slot 1 first. */
 reset_counts();
 make_device(1);open_scenario(TR64_GAME_MENU_PAGE_OPTIONS,1);settle_to_passport();
 step(TR64_GAME_LEFT);idle(1);settled();
 assert(m.passportPage==PP_LOAD && m.value[m.optionIndex]==LVL_LOAD);
 step(TR64_GAME_FORWARD);idle(1);step(TR64_GAME_FORWARD);idle(1);settled();
 assert(m.value[m.optionIndex]==LVL_L1 && !tr64_ui_picker_view());
 step(TR64_GAME_FORWARD|TR64_GAME_ACTION);
 assert(m.value[m.optionIndex]==LVL_LOAD && m.pending==LVL_NONE && !tr64_ui_picker_view());
 step(TR64_GAME_FORWARD|TR64_GAME_ACTION);
 assert(m.pending==LVL_NONE && tr64_ui_picker_view() && tr64_ui_picker_view()->load && tr64_ui_picker_view()->index==1);
 assert(tr64_ui_picker_view()->valid[1] && !tr64_ui_picker_view()->valid[0]);
 idle(1);step(TR64_GAME_ACTION); /* accept slot 1 */
 idle(80);
 assert(m.stopped && m.pending==LVL_LOAD && loads==1 && !load_unconfirmed && !slot_mismatch);
 assert(tr64_save_game_selected()==1);
 /* LOAD page, RIGHT+ACTION: the page becomes SAVE in the same tick. No save
  * without the picker, and the held button opens the save picker once settled. */
 reset_counts();
 make_device(1);open_scenario(TR64_GAME_MENU_PAGE_OPTIONS,1);settle_to_passport();
 step(TR64_GAME_LEFT);idle(1);settled();
 step(TR64_GAME_RIGHT|TR64_GAME_ACTION);
 assert(!save_ok && !save_refused && !tr64_ui_picker_view() && m.passportPage==PP_SAVE);
 for(int i=0;i<30 && !tr64_ui_picker_view();++i)step(TR64_GAME_RIGHT|TR64_GAME_ACTION);
 assert(tr64_ui_picker_view() && !tr64_ui_picker_view()->load && !save_ok && !save_refused);
 index_lines=0;last_index=-1;
 idle(1);step(TR64_GAME_BACK);assert(index_lines==1 && last_index==1);
 idle(1);step(TR64_GAME_BACK);assert(index_lines==2 && last_index==2);
 idle(1);step(TR64_GAME_BACK|TR64_GAME_FORWARD);assert(index_lines==2 && last_index==2); /* no move, no line */
 idle(1);step(TR64_GAME_ACTION); /* slot 0 -> 2 */
 assert(index_lines==2);
 assert(save_ok==1 && !slot_mismatch && tr64_save_game_selected()==2 && !save_refused);
 /* Cancelling the picker with A (JUMP) or START (SELECT): the game's passport leaves on a
  * *held* JUMP or SELECT, so the cancelling button, still down, must not reach it; once
  * released, the next press leaves the passport as always. */
 for(int k=0;k<2;++k) {
  const uint16_t cancel=k?TR64_GAME_SELECT:TR64_GAME_JUMP;
  reset_counts();
  make_device(1);open_scenario(TR64_GAME_MENU_PAGE_OPTIONS,1);settle_to_passport();
  step(TR64_GAME_ACTION);
  assert(tr64_ui_picker_view() && !tr64_ui_picker_view()->load);
  idle(1);
  step(cancel);
  assert(!tr64_ui_picker_view());
  for(int i=0;i<12;++i) {
   step(cancel);
   assert(m.active && m.state==TR64_GAME_MENU_STATE_SHOW && m.nextState==TR64_GAME_MENU_STATE_NONE && !tr64_ui_picker_view());
  }
  idle(2);
  assert(m.state==TR64_GAME_MENU_STATE_SHOW && m.nextState==TR64_GAME_MENU_STATE_NONE);
  step(cancel); /* a new press leaves the passport */
  assert(m.nextState==TR64_GAME_MENU_STATE_READY || m.state!=TR64_GAME_MENU_STATE_SHOW);
  assert(!save_ok && !save_refused);
 }
}
static void save_result_paths(void)
{
 uint8_t previous[sizeof(bank)];
 make_device(1);open_scenario(TR64_GAME_MENU_PAGE_OPTIONS,1);settle_to_passport();
 memcpy(previous,bank,sizeof(bank));
 const unsigned selected=tr64_save_game_selected(), before_write=writes, before_serial=serializations;
 const unsigned before_saves=tr64_save_game_saves();
 blocked_save=1;step(TR64_GAME_ACTION);
 assert(save_result_code==TR64_ENG_SAVE_BLOCKED && !tr64_ui_picker_view());
 assert(selected==tr64_save_game_selected() && writes==before_write && serializations==before_serial);
 assert(!memcmp(previous,bank,sizeof(bank)) && before_saves==tr64_save_game_saves());
 blocked_save=0;
 open_scenario(TR64_GAME_MENU_PAGE_OPTIONS,1);settle_to_passport();
 step(TR64_GAME_ACTION);assert(tr64_ui_picker_view() && !tr64_ui_picker_view()->load);
 idle(1);fail_write=1;step(TR64_GAME_ACTION);fail_write=0;
 assert(save_result_code==TR64_ENG_SAVE_FAILED && tr64_save_game_status()==TR64_SAVE_IO);
 assert(writes>before_write && serializations==before_serial+1 && !memcmp(previous,bank,sizeof(bank)));
 assert(before_saves==tr64_save_game_saves());
 puts("passport save PASS: blocked never selects or writes a slot; actual write failure remains distinct");
}
int main(int argc,char **argv)
{
 (void)argc;(void)argv;
 setvbuf(stdout,NULL,_IONBF,0);
 tr64_save_game_require_confirmation();
 save_result_paths();
 reset_counts();bypass=0;run_all();
 printf("picker: stops=%u saves=%u loads=%u picker_opened=%u accepts=%u refused=%u unconfirmed_loads=%u slot_mismatch=%u\n",
  stops,save_ok,loads,picker_opened,picker_accepts,save_refused,load_unconfirmed,slot_mismatch);
 assert(!save_refused && !load_unconfirmed && !slot_mismatch);
 assert(save_ok>1000 && loads>50 && picker_opened>1000 && picker_accepts>1000); /* the picker is live */
 named_cases();
 reset_counts();bypass=1;run_all();
 printf("picker negative control (previous rule): saves=%u refused=%u unconfirmed_loads=%u\n",save_ok,save_refused,load_unconfirmed);
 assert(save_refused>50 && load_unconfirmed>10); /* the previous rule is bypassed */
 puts("slot picker PASS: no save or load reaches the game unconfirmed; direction+ACTION, unsettled-passport and same-tick page moves are withheld; a cancelling A or START does not also leave the passport; the previous rule is bypassed");
 return 0;
}
