/* SPDX-License-Identifier: MIT */
/* Synthetic services exercise the real play loop's policy/order, not game parity. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/in/play.h"
#include "engine/in/input.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/game.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "tr64/render.h"
#include "engine/snd/snd.h"
#include "engine/ui/ui.h"
#include "engine/ui/frontend.h"
#include "engine/ui/perf.h"
#include "engine/save/game.h"

#define CHECK(c) do { if(!(c))sys_error("play policy line %d: %s",__LINE__,#c); } while(0)
static tr64_level_t core={.ready=1};
static tr64_game_stop_t stop;
static union { uint64_t alignment; uint8_t bytes[TR64_GAME_DRAW_CHUNK*sizeof(tr64_game_drawable_t)]; } scratch;
static unsigned ordinal,loading,alive,bound,allocated,stopped,frames,messages,ready;
static uint32_t clock_us,message_at,last_frame;
static int frame_seen;
static unsigned audio_active,audio_pending,audio_ticks,audio_starts,audio_stops,audio_reports,audio_pumps,message_pump_at;
static unsigned captures,transitions,game_ticks,recursive_pending,recursive_input,restores;
static int source_load,refuse_mode; /* refuse_mode 1: slot unusable at the LOAD stop; 2: game_load rejects it; 3: no picker confirmation */
#define REFUSED_AT_STOP (refuse_mode==1 || refuse_mode==3)
static unsigned io_frames,message_io_at;
/* --slow MS: every drawn frame costs MS more of the scripted clock, so frames carry several ticks and
 * the whole interactive script (presses, START masks, chords, picker, cutscene skip) must hold. */
static uint32_t clock_step=1000,slow_us;
static unsigned pace_mode,burst,max_burst;
static uint32_t pace_start_us;
static uint32_t pace_draw_cost(void);
static void pace_tick(uint16_t actions);
static int pace_pad(tr64_pad_t *pad);
static int reject_capture;
static unsigned preview_mode,preview_on,preview_toggles,preview_menu;
static unsigned coop_mode,paired,joins,leaves,warp_calls,warp_notices,warp_success;
static unsigned coop_ring;
static unsigned busy_leaves,busy_warps,coop_hud_clears,busy_split_frames;
static unsigned ui_bound,ui_active,ui_starts,ui_stops,front_active;
static int reject_ui;
/* The shipped loop boots to TITLE; ordinal 0 starts a new game into the former
 * boot level, so every later ordinal is the previous script shifted by one. */
/* Ordinals 13..17 are the cutscene scenario: TITLE's new game enters LEVEL7B,
 * its end enters CUT2, START skips CUT2 into LEVEL8A, L-previous then selects
 * CUT2 again (manual start), which ends by itself into LEVEL8A. */
#define FRAMES_EXPECTED 34 /* the START scenario of ordinal 3 draws three more frames, not in a load run */
static const char *expected[]={
 "title","level1","level1","level2","level2","level1","level1",
 "level1","gym","title","gym","level10c","title",
 "level7b","cut2","level8a","cut2","level8a"
};
static unsigned reads,level_ticks,skips;
static int skipped;
static int is_transition(unsigned o)
{
 if(REFUSED_AT_STOP && o==3)return 0; /* the title after a refused load starts fresh */
 if(coop_mode)return o==1;
 if(refuse_mode==2 && o==4)return 0;
 return o==1 || o==2 || o==3 || o==9 || (o>=12 && o!=16);
}
/* B on the pad; at the load passport (ordinal 2, the real picker) it takes two presses: the first
 * opens the picker (the game sees nothing), the second accepts the saved slot (the game sees ACTION). */
static int b_down(void)
{
 if(ordinal==2 && source_load)return refuse_mode!=3 && (reads==1 || reads==3);
 return !ordinal || ordinal==12 || (ordinal==1 && stop.death_ready);
}
static uint16_t expected_action(void)
{
 if(ordinal==2 && source_load)return reads==3 && refuse_mode!=3 ? TR64_GAME_ACTION : 0;
 return b_down() ? TR64_GAME_ACTION : 0;
}
static int is_cutscene(void) { return alive && (ordinal==14 || ordinal==16); }
/* START in the three places the ring must not open from (SELECT = START in game
 * actions): ordinal 2 ends its level with START held (the statistics page); ordinal 3
 * (the next level) sees START held for two frames, released, then pressed again;
 * ordinal 6 follows an L+START restart: L released with START still held, same pattern.
 * Only the new press may reach the game as SELECT. */
static int start_pattern(void)
{ return (ordinal==3 && !source_load && (reads==1 || reads==2 || reads==4)) || (ordinal==6 && (reads==1 || reads==2 || reads==4)); }
static uint16_t expected_select(void)
{
 if(ordinal==2 && !source_load)return TR64_GAME_SELECT;
 return (reads==4 && ((ordinal==3 && !source_load) || ordinal==6)) ? TR64_GAME_SELECT : 0;
}
/* The profiler overlay: the real loop must request it on the chord, begin every frame once the request
 * is applied, and end the frames drawn while it is on. Its accounting is engine/ui/perf.c's. */
uint8_t tr64_perf_state;
uint32_t tr64_perf_sim_ticks,tr64_perf_io_ticks,tr64_perf_snd_ticks;
static unsigned perf_begins,perf_ends,perf_requests;
uint32_t sys_profile_ticks(void) { return clock_us; }
void tr64_perf_begin(uint32_t due)
{
 CHECK(due>=1 && due<=4 && (tr64_perf_state&2u));
 if(!(tr64_perf_state&1u))++perf_requests;
 tr64_perf_state=3;++perf_begins;
}
void tr64_perf_end(const tr64_perf_frame_t *f)
{
 CHECK(f && f->ticks>=1 && f->level && !strcmp(f->level,expected[ordinal]));++perf_ends;
}
void sys_error(const char *fmt,...)
{
 if(reject_capture && ordinal==2 && !strcmp(fmt,"play: inventory capture failed")) {
  puts("play policy negative PASS: failed inventory capture is fatal");exit(0);
 }
 if(reject_ui && !ordinal && !strcmp(fmt,"play: UI font not ready")) {
  CHECK(!alive && !audio_active && !ui_active);
  puts("play UI negative PASS: missing font fails before gameplay/audio");exit(0);
 }
 va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);exit(1);
}
static char pace_level_line[512],pace_tick_line[512];
void log_printf(const char *fmt,...)
{
 char line[512];va_list ap;
 if(!pace_mode || strncmp(fmt,"play-pace:",10))return;
 va_start(ap,fmt);vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
 if(!strncmp(line,"play-pace: level",16))memcpy(pace_level_line,line,sizeof(line));
 else memcpy(pace_tick_line,line,sizeof(line));
}
void log_flush(void) { }
void tr64_snd_level_start(const tr64_level_t *level)
{
 CHECK(alive && bound && loading && !audio_active && !audio_pending && level==&core);
 audio_active=1;++audio_starts;
}
void tr64_snd_tick(void)
{
 CHECK(alive && audio_active && audio_pending && !loading);
 audio_pending=0;++audio_ticks;
}
/* The end of the game plays END (LEVEL10C to title, ordinal 11 to 12) unless the
 * player left through the passport; no other title transition plays anything. */
static unsigned end_checks;
static int menu_exit;
int tr64_ui_menu_was_shown(void) { return menu_exit && ordinal==11; }
static unsigned videos;
unsigned tr64_video_transition(const char *from,const char *to,int advance)
{
 if(ordinal==12) {
  CHECK(from && !strcmp(from,"level10c") && !strcmp(to,"title"));
  CHECK(advance==!menu_exit);++end_checks;
 } else if(ordinal==9) CHECK(from && !strcmp(from,"gym") && !strcmp(to,"title") && !advance);
 else if(ordinal==13) {
  /* A film between the title and LEVEL7B: polls the pad as tr64_video_play does and ends on a START
   * edge, START still held (sys_pad_read: idle on the first poll, START from the second on). */
  CHECK(from && !strcmp(from,"title") && !strcmp(to,"level7b") && advance);
  tr64_pad_t pad;uint16_t previous_buttons=0;
  for(unsigned polls=0;;++polls) {
   CHECK(polls<8);sys_pad_read(&pad,NULL);
   if((pad.buttons&TR64_PAD_START) && !(previous_buttons&TR64_PAD_START))break;
   previous_buttons=pad.buttons;
  }
  ++videos;return 1;
 }
 return 0;
}
void tr64_snd_pump(void) { CHECK(!audio_pending);++audio_pumps; }
void tr64_snd_level_stop(void)
{
 CHECK(!audio_pending && !loading);
 if(audio_active)++audio_stops;
 audio_active=0;
}
void tr64_snd_report(void)
{ CHECK(alive && !audio_active && !audio_pending);++audio_reports; }
int tr64_ui_bind(const tr64_level_t *level)
{
 CHECK(loading && bound && !alive && !ui_bound && !ui_active && level==&core);
 if(reject_ui)return 0;
 ui_bound=1;++ui_starts;return 1;
}
void tr64_ui_set_active(int active)
{
 CHECK(ui_bound && (unsigned)active!=ui_active);
 if(active)CHECK(loading && !alive);
 else CHECK(alive && !loading && !audio_active && !audio_pending);
 ui_active=(unsigned)active;
}
void tr64_ui_unbind(void)
{
 CHECK(ui_bound && !ui_active && alive && !loading && !audio_active);
 ui_bound=0;++ui_stops;
}
uint32_t tr64_ui_last_submit_us(void) { return 0; }
void tr64_ui_game_start(const tr64_level_t *level,const char *name)
{
 CHECK(loading && alive && ui_bound && ui_active && audio_active && !front_active);
 CHECK(level==&core && !strcmp(name,expected[ordinal]));front_active=1;
 if(pace_mode && !ordinal)pace_start_us=clock_us;
 (void)tr64_save_game_start();tr64_ui_picker_start();
}
void tr64_ui_game_stop(void)
{
 CHECK(front_active);front_active=0;tr64_ui_picker_stop();tr64_save_game_stop();tr64_ui_set_active(0);tr64_ui_unbind();
}
int tr64_ui_game_frame(const tr64_game_view_t *view,tr64_game_drawable_t *buffer,uint32_t capacity)
{
 CHECK(front_active);tr64_render_game_scene_frame(view,buffer,capacity);return 1;
}
static unsigned ui_ticks;
void tr64_ui_game_tick(const tr64_game_view_t *view,tr64_game_drawable_t *buffer,uint32_t capacity)
{
 CHECK(front_active && view && buffer==(void *)scratch.bytes && capacity==TR64_GAME_DRAW_CHUNK && stop.kind==TR64_GAME_STOP_NONE);
 ++ui_ticks;
}
void tr64_ui_game_poll(void) { }
void tr64_ui_coop_no_medipack(unsigned slot) { CHECK(coop_mode && slot==1);++warp_notices; }
void tr64_ui_coop_warped(unsigned slot) { CHECK(coop_mode && !slot);++warp_success; }
void tr64_ui_coop_leave(void) { CHECK(coop_mode && !paired);++coop_hud_clears; }
uint32_t sys_time_us(void) { clock_us+=clock_step;return clock_us; }
void sys_heap_check(void) { CHECK(alive && bound); }
void tr64_io_pump(void) { }
void tr64_io_frame(void) { CHECK(alive);++io_frames; }
void tr64_mem_log(void) { }
void code_loading_begin(void) { CHECK(!loading);loading=1; }
void code_loading_end(void) { CHECK(loading);loading=0; }
void code_set_frame_handler(code_frame_handler_t handler,void *context)
{ CHECK(loading && !handler && !context); }
void code_frame(uint32_t frame)
{
 CHECK(!loading && alive && bound);
 CHECK(!frame_seen || frame>last_frame);last_frame=frame;frame_seen=1;
}
void mem_level_begin(void)
{ CHECK(!loading && !alive && !bound && !allocated && !stopped && !ui_bound && !ui_active); }
void tr64_scene_load(const char *name)
{
 CHECK(loading && !bound && ordinal<sizeof(expected)/sizeof(expected[0]));
 CHECK(!strcmp(name,expected[ordinal]));bound=1;
 if(pace_mode && ordinal==1)clock_us+=900000u; /* the level swap: 900 ms in which no tick may be owed */
}
const tr64_level_t *tr64_level_current(void) { CHECK(bound);return &core; }
void game_init(void) { CHECK(!ordinal && !alive); }
const char *game_level_name(int32_t id)
{
 switch(id) { case 0:return "title";case 1:return "gym";case 2:return "level1";case 3:return "level2";
  case 4:return "level7b";case 5:return "cut2";case 6:return "level8a";default:return NULL; }
}
void tr64_save_identity_bind(const char *const *names,unsigned count)
{ CHECK(!ordinal && count==21 && !strcmp(names[0],"gym") && !strcmp(names[count-1],"title")); }
uint32_t tr64_save_level_identity(int32_t level_id) { return 0xa5000000u+(uint32_t)level_id; }
/* The real save glue, picker and medium run over a RAM device; the game's serializer is reduced to
 * what they touch. A save of "level2" (id 3) sits in slot 2. */
static uint8_t bank[TR64_SAVE_BYTES];
int tr64_save_platform_init(void) { return TR64_SAVE_OK; }
int tr64_save_platform_read(uint32_t offset,void *data,uint32_t bytes)
{ CHECK(offset<=sizeof(bank) && bytes<=sizeof(bank)-offset);memcpy(data,bank+offset,bytes);return TR64_SAVE_OK; }
int tr64_save_platform_write(uint32_t offset,const void *data,uint32_t bytes)
{ CHECK(offset<=sizeof(bank) && bytes<=sizeof(bank)-offset);memcpy(bank+offset,data,bytes);return TR64_SAVE_OK; }
extern uint32_t tr64_unit_medium(uint32_t argument);
void code_load(const char *name) { CHECK(!strcmp(name,"medium")); }
uint32_t code_call(const char *name,uint32_t argument)
{ CHECK(!strcmp(name,"medium"));return tr64_unit_medium(argument); }
/* The id of the level now loaded: the table's id where the script has one, else any other. */
int32_t game_level_id(void)
{
 for(int32_t id=0;id<7;++id)if(!strcmp(game_level_name(id),expected[ordinal]))return id;
 return 100+(int32_t)ordinal;
}
int game_save_blocked(void) { return 0; }
uint32_t game_save(void *buffer,uint32_t capacity)
{ CHECK(buffer && capacity>=100);memset(buffer,0x5a,100);return 100; }
int game_load(const void *buffer,uint32_t bytes)
{
 (void)buffer;(void)bytes;
 CHECK(source_load && ordinal==3 && front_active && !loading);
 if(refuse_mode==2)return 0;
 ++restores;return 1;
}
int game_stats_view(tr64_game_level_stats_t *out) { memset(out,0,sizeof(*out));return 1; }
const tr64_game_settings_t *game_settings_view(void) { static const tr64_game_settings_t s={0}; return &s; }
int game_settings_apply(const tr64_game_settings_t *s) { (void)s;return 1; }
/* The load passport (ordinal 2): settled, on the page whose ACTION loads a slot. */
int game_menu_view(tr64_game_menu_view_t *out)
{
 if(coop_mode && coop_ring) {
  memset(out,0,sizeof(*out));out->state=TR64_GAME_MENU_STATE_SHOW;
  if(ordinal==2)out->actionHint=TR64_GAME_MENU_ACTION_SAVE_SLOT;
  return 1;
 }
 if(preview_mode && preview_menu) {
  memset(out,0,sizeof(*out));out->state=TR64_GAME_MENU_STATE_SHOW;return 1;
 }
 if(!(alive && ordinal==2 && source_load && stop.kind==TR64_GAME_STOP_NONE))return 0;
 memset(out,0,sizeof(*out));out->state=TR64_GAME_MENU_STATE_SHOW;out->page=TR64_GAME_MENU_PAGE_OPTIONS;
 out->actionHint=TR64_GAME_MENU_ACTION_LOAD_SLOT;return 1;
}
static void prepare_device(void)
{
 static uint8_t data[128] __attribute__((aligned(16)));
 memset(bank,0xff,sizeof(bank));memset(data,0x33,sizeof(data));
 tr64_save_info_t info={.level_id=3,.identity=tr64_save_level_identity(3)};
 tr64_save_unit_load();CHECK(tr64_save_write(2,&info,data,100)==TR64_SAVE_OK);
}
static void start(const struct tr64_level *level,const char *name)
{
 CHECK(loading && bound && !alive && level==&core && !strcmp(name,expected[ordinal]));
 memset(&stop,0,sizeof(stop));alive=1;burst=0;
}
void game_level_start(const struct tr64_level *level,const char *name)
{ CHECK(!is_transition(ordinal));start(level,name); }
int game_inventory_capture(tr64_game_inventory_carry_t *out)
{
 CHECK(alive && !audio_pending && (ordinal==0 || ordinal==1 || ordinal==2 || ordinal==8 || ordinal==11 || ordinal==12 ||
        ordinal==13 || ordinal==14 || ordinal==16) &&
       stop.kind==TR64_GAME_STOP_LEVEL_END);
 if(reject_capture && ordinal==2)return 0;
 out->from_level_id=2;
 for(unsigned i=0;i<TR64_GAME_INVENTORY_SLOTS;++i)out->counts[i]=1;
 ++captures;return 1;
}
void game_level_start_transition(const struct tr64_level *level,const char *name,
                                const tr64_game_inventory_carry_t *carry)
{
 CHECK(is_transition(ordinal) && captures==transitions+1 && carry && carry->from_level_id==2);
 uint32_t reason=ordinal==1||ordinal==13?TR64_GAME_TRANSITION_NEW_GAME:ordinal==2?TR64_GAME_TRANSITION_RESTART:
  ordinal==3?(source_load?TR64_GAME_TRANSITION_LOAD:TR64_GAME_TRANSITION_CAMPAIGN):
  ordinal==14||ordinal==15||ordinal==17?TR64_GAME_TRANSITION_CAMPAIGN:TR64_GAME_TRANSITION_TITLE;
 CHECK(carry->transition==reason);
 for(unsigned i=0;i<TR64_GAME_INVENTORY_SLOTS;++i)CHECK(carry->counts[i]==1);
 ++transitions;start(level,name);recursive_pending=1;
 recursive_input=ordinal<=2 || ordinal==13 || (ordinal==3 && source_load)?TR64_GAME_ACTION:0;
}
const tr64_game_stop_t *game_stop_info(void)
{ CHECK(!audio_pending);return alive ? &stop : NULL; }
void *tr64_mem_alloc(int pool,uint32_t bytes)
{
 static uint8_t workspace[TR64_SAVE_REGION_BYTES] __attribute__((aligned(16)));
 CHECK(loading && pool==TR64_MEM_state);
 if(bytes==TR64_SAVE_REGION_BYTES)return workspace; /* tr64_save_game_start */
 CHECK(bytes==sizeof(scratch.bytes) && !allocated);
 allocated=1;return scratch.bytes;
}
void tr64_render_scene_set_live(int live) { CHECK(live==1 && !bound); }
void tr64_render_game_scene_prepare(void) { CHECK(alive && bound && allocated && loading); }
void tr64_render_preview_toggle(void)
{
 CHECK(preview_mode && ordinal==1 && reads==3);
 preview_on^=1;++preview_toggles;
}
void tr64_render_preview_stop(void) { preview_on=0; }
void game_view_camera(tr64_game_view_t *view)
{
 CHECK(alive && stop.kind==TR64_GAME_STOP_NONE);
 memset(view,0,sizeof(*view));
}
uint32_t game_player_mask(void) { return alive?(1u|(paired<<1)):0u; }
unsigned game_menu_player(void) { return coop_ring?1:0; }
int game_player_join(void)
{
 CHECK(coop_mode && !paired && ((ordinal==1 && reads==3) || (ordinal==2 && !reads)));
 paired=1;++joins;return 0;
}
int game_player_leave(void)
{
 CHECK(coop_mode && paired && ordinal==2 && (reads==3 || reads==5 || reads==7));
 if(reads!=7) {
  CHECK((tr64_ui_picker_view()!=NULL)==(reads==3));++busy_leaves;return TR64_GAME_LEAVE_BUSY;
 }
 paired=0;++leaves;return TR64_GAME_LEAVE_OK;
}
int game_player_warp(unsigned slot)
{
 CHECK(coop_mode && paired && ordinal==1 && (slot?reads==8:(reads==7 || reads==9)));
 if(reads==7) { ++busy_warps;return TR64_GAME_WARP_BUSY; }
 ++warp_calls;return slot?TR64_GAME_WARP_NO_MEDIPACK:TR64_GAME_WARP_OK;
}
void game_set_extra_input(uint32_t mask) { (void)mask; }
void game_set_extra_input_player(unsigned slot,uint32_t mask) { (void)slot;(void)mask; }
void game_tick_players(const uint16_t actions[TR64_GAME_PLAYERS])
{
 CHECK(coop_mode && paired && alive && bound && !loading && !audio_pending);
 audio_pending=1;++game_ticks;++level_ticks;++burst;
 if(ordinal==1) {
  CHECK(reads>=3 && reads<=9);
  const uint16_t first[]={TR64_GAME_SELECT,TR64_GAME_FORWARD,0,0,0,0,0};
  const uint16_t second[]={0,TR64_GAME_RIGHT,TR64_GAME_SELECT,TR64_GAME_JUMP,0,0,0};
  CHECK(actions[0]==first[reads-3] && actions[1]==second[reads-3]);
  if(reads==5)coop_ring=1;
  if(reads==6)coop_ring=0;
 } else {
  CHECK(ordinal==2 && reads && reads<=6 && !actions[0] && !actions[1]);
  if(reads==1)coop_ring=1;
  if(reads==3)CHECK(tr64_ui_picker_view() && !coop_hud_clears);
  if(reads==4) { CHECK(!tr64_ui_picker_view());coop_ring=0; }
 }
}
void tr64_render_game_scene_frame(const tr64_game_view_t *view,tr64_game_drawable_t *buffer,uint32_t capacity)
{
 CHECK(view && alive && bound && !loading && !ready);
 CHECK(audio_active && !audio_pending);
 CHECK(ui_bound && ui_active);
 CHECK(buffer==(void *)scratch.bytes && capacity==TR64_GAME_DRAW_CHUNK);++frames;
 if(coop_mode && ordinal==2 && (reads==5 || reads==6)) {
  CHECK(paired && !coop_ring && !coop_hud_clears);++busy_split_frames;
 }
 burst=0;
 clock_us+=pace_mode ? pace_draw_cost() : slow_us;
}
void tr64_play_message_ready(int enable) { ready=(unsigned)enable; }
int tr64_ui_message(const char *title,const char *name)
{
 CHECK(ready && alive);
 CHECK(ui_bound && ui_active);
 if(refuse_mode) CHECK(!strcmp(title,"SAVE NOT USABLE") && !strcmp(name,"RETURNING TO TITLE") && stop.kind==(REFUSED_AT_STOP?TR64_GAME_STOP_LEVEL_END:TR64_GAME_STOP_NONE));
 else CHECK(stop.kind==TR64_GAME_STOP_NOT_PORTED && !strcmp(title,"NOT PORTED - RESTARTING") && !strcmp(name,"synthetic missing UI"));
 ++messages;message_at=clock_us;message_pump_at=audio_pumps;message_io_at=io_frames;
 return 1;
}
int game_cutscene_active(void) { return is_cutscene(); }
uint32_t game_state_hash(void) { return 0; }
uint32_t game_trace_prefix(char *out,uint32_t capacity)
{
 static const char record[]="{\"schema\":2,\"lara\":{\"pos\":[1,2,3]}}\n";
 const uint32_t bytes=capacity<sizeof(record)-1 ? capacity : sizeof(record)-1;
 memcpy(out,record,bytes);return bytes;
}
int game_cutscene_skip(void)
{
 /* Only START's rising edge may ask, only while a cutscene runs. */
 CHECK(alive && stop.kind==TR64_GAME_STOP_NONE && !loading);
 if(!is_cutscene())return 0;
 CHECK(ordinal==14 && !skipped && level_ticks==4); /* entry tick + 3 idle frames */
 skipped=1;++skips;return 1;
}
/* ---- --pace: the real play loop against a scripted clock ------------------------------------------
 * The title level (ordinal 0) is the measured level: every drawn frame costs what the script says, in
 * phases of 20, 33, 50, 100 and 400 ms frames, a mixture, one 900 ms frame and 33 ms frames again. The pad is
 * pulsed (A, 200 ms every 500 ms) through the first four phases. The title then ends into LEVEL1 across a 900 ms
 * level swap, whose frames (20 ms) must run one tick each from a tick period after the load. */
enum { PH_END=0,P0=60,P1=60,P2=60,P3=30,P4=6,P5=90,P6=1,P7=30,FRAMES_TOTAL=P0+P1+P2+P3+P4+P5+P6+P7 };
static uint16_t script_cost[FRAMES_TOTAL];
static uint8_t script_ticks[FRAMES_TOTAL+8],level1_ticks[16];
static unsigned pf,pace_first_draw,pace_edges,level1_frames,primed_seen;
static uint32_t pace_base,pace_pulses_end_us,first_l1_tick_us,swap_end_us;
static int pace_base_set;
static uint16_t pace_previous;
static void pace_script(void)
{
 static const uint16_t mix[]={20,50,100,33,20,50,50,100,33,20,70,20,20,90,45};
 unsigned k=0,i,t;
 for(i=0;i<P0;++i)script_cost[k++]=20;
 for(i=0;i<P1;++i)script_cost[k++]=33;
 for(i=0;i<P2;++i)script_cost[k++]=50;
 for(i=0;i<P3;++i)script_cost[k++]=100;
 for(i=0;i<P4;++i)script_cost[k++]=400;
 for(t=0;t<P5/15;++t)for(i=0;i<15;++i)script_cost[k++]=mix[i];
 script_cost[k++]=900;
 for(i=0;i<P7;++i)script_cost[k++]=33;
 CHECK(k==FRAMES_TOTAL);
}
/* Cost of the frame just drawn; the first picture of a level is the load's, not a loop frame. */
static uint32_t pace_draw_cost(void)
{
 uint32_t cost;
 if(!pace_first_draw) { pace_first_draw=1;return 0; }
 if(!ordinal) {
  if(pf==P0+P1+P2+P3)pace_pulses_end_us=clock_us;
  CHECK(pf<FRAMES_TOTAL);cost=script_cost[pf]*1000u;++pf;return cost;
 }
 ++level1_frames;return 20000u;
}
static int pace_pad(tr64_pad_t *pad)
{
 if(ordinal)return 1;
 if(!pace_base_set) { pace_base=clock_us;pace_base_set=1; }
 if(pf<P0+P1+P2+P3 && (clock_us-pace_base)/1000u%500u<200u)pad->buttons=TR64_PAD_A;
 return 1;
}
static void pace_tick(uint16_t actions)
{
 CHECK(alive && bound && allocated && !loading && stop.kind==TR64_GAME_STOP_NONE);
 CHECK(audio_active && !audio_pending);audio_pending=1;++game_ticks;
 if(ordinal) {
  CHECK(ordinal==1);
  if(!primed_seen) { primed_seen=1;swap_end_us=clock_us;return; } /* the entry tick of the carried-over level */
  if(!first_l1_tick_us)first_l1_tick_us=clock_us;
  CHECK(level1_frames<sizeof(level1_ticks));++level1_ticks[level1_frames];
  if(level1_frames==10) {
   unsigned i;
   for(i=0;i<10;++i)CHECK(level1_ticks[i]==1);
   /* The first tick is a tick period after the load, not a burst for the 900 ms swap. */
   CHECK(first_l1_tick_us-swap_end_us>=33000u && first_l1_tick_us-swap_end_us<=36000u);
   CHECK(ui_ticks>=game_ticks-2u && audio_ticks+1u>=game_ticks-1u);
   printf("play pace PASS: scripted clock through the real loop (20/33/50/100/400 ms frames, a mixture, a 900 ms frame), then a 900 ms level swap: first tick %lu us after the load, one tick per 20 ms frame after it\n",
    (unsigned long)(first_l1_tick_us-swap_end_us));
   exit(0);
  }
  return;
 }
 ++script_ticks[pf<FRAMES_TOTAL?pf:FRAMES_TOTAL];
 if((actions&TR64_GAME_JUMP) && !(pace_previous&TR64_GAME_JUMP) && pf<P0+P1+P2+P3)++pace_edges;
 pace_previous=actions;
 if(pf==FRAMES_TOTAL-1 && script_ticks[pf]) {
  stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=2;stop.transition=TR64_GAME_TRANSITION_NEW_GAME;
 }
}
static unsigned count_range(unsigned from,unsigned to,unsigned value)
{ unsigned c=0,i;for(i=from;i<to;++i)c+=script_ticks[i]==value;return c; }
static unsigned field(const char *line,const char *name)
{
 const char *at=strstr(line,name);
 CHECK(at);return (unsigned)strtoul(at+strlen(name),NULL,10);
}
/* The title level ended: what the script's frames were followed by. */
static void pace_title_done(void)
{
 unsigned b0=0,b1=P0,b2=b1+P1,b3=b2+P2,b4=b3+P3,b5=b4+P4,b6=b5+P5,b7=b6+P6,i,total=0,drops;
 uint32_t elapsed_us,pulses;
 for(i=0;i<FRAMES_TOTAL;++i)total+=script_ticks[i];
 /* 20, 33 ms: faster than a tick, one tick a frame. */
 CHECK(count_range(b0+2,b1,1)==b1-b0-2 && count_range(b1+2,b2,1)==b2-b1-2);
 /* 50 ms: one or two, every other time, never three; no tick lost. */
 CHECK(count_range(b2+2,b3,1)+count_range(b2+2,b3,2)==b3-b2-2 && count_range(b2+2,b3,2)>=(b3-b2-2)*4/10 &&
       count_range(b2+2,b3,1)>=(b3-b2-2)*4/10);
 /* 100 ms: three (four now and then as the remainder carries). */
 CHECK(count_range(b3+2,b4,3)>=(b4-b3-2)*9/10 && count_range(b3+2,b4,3)+count_range(b3+2,b4,4)==b4-b3-2);
 /* 400 ms: twelve owed, four run (the bound), eight dropped per frame. */
 CHECK(count_range(b4+1,b5,4)==b5-b4-1);
 for(i=b5;i<b6;++i)CHECK(script_ticks[i]>=1 && script_ticks[i]<=4);
 /* The 900 ms frame is followed by one bounded burst of four and then by ordinary frames. */
 CHECK(script_ticks[b7]==4 && script_ticks[b7+1]==1);
 CHECK(count_range(b7+1,FRAMES_TOTAL-1,1)==FRAMES_TOTAL-1-b7-1);
 /* The loop's own account must agree with what the game was given. */
 CHECK(field(pace_level_line,"ticks=")==total && field(pace_level_line,"frames=")==FRAMES_TOTAL);
 drops=field(pace_level_line,"dropped=");
 /* 6 frames follow 400 ms draws: 6*8 dropped, and 23 for the 900 ms one: all counted, none run later. */
 CHECK(drops>=69 && drops<=73);
 /* Ticks follow the clock: run + dropped = what 30 Hz owes by the last frame (+1: the first tick at once). */
 elapsed_us=field(pace_level_line,"us=")-pace_start_us;
 CHECK(field(pace_level_line,"ticks=")+drops+2>=(unsigned)((uint64_t)elapsed_us*TR64_TICK_HZ/1000000u)+1u &&
       field(pace_level_line,"ticks=")+drops<=(unsigned)((uint64_t)elapsed_us*TR64_TICK_HZ/1000000u)+2u);
 /* Presses: every 200 ms pulse of the first four phases was one rising edge: none lost, none doubled. */
 pulses=(pace_pulses_end_us-pace_base-200000u)/500000u+1u;
 CHECK(pace_edges+1u>=pulses && pace_edges<=pulses+1u);
 /* Every tick was taken by sound and presentation before the next one. */
 CHECK(audio_ticks==game_ticks && ui_ticks==game_ticks-1u);
 printf("play pace title: frames=%u ticks=%u dropped=%u pulses=%lu edges=%u hist 1/2/3/4=%u/%u/%u/%u\n",FRAMES_TOTAL,total,drops,
  (unsigned long)pulses,pace_edges,count_range(0,FRAMES_TOTAL,1),count_range(0,FRAMES_TOTAL,2),count_range(0,FRAMES_TOTAL,3),count_range(0,FRAMES_TOTAL,4));
}
void sys_pad_read(tr64_pad_t *pad,tr64_pad_t *second)
{
 if(second)*second=(tr64_pad_t){0};
 memset(pad,0,sizeof(*pad));++reads;
 if(pace_mode && pace_pad(pad))return;
 if(coop_mode) {
  if(!ordinal) {
   pad->buttons=TR64_PAD_B;
   if(second)second->buttons=TR64_PAD_START;
  } else if(ordinal==1) {
   /* Same-poll START edges initially collide; after a release, the new pad-2
    * edge must join even though pad 1 never released its first START. */
   static const uint16_t first[]={TR64_PAD_START,TR64_PAD_START,TR64_PAD_START,TR64_PAD_UP,0,0,TR64_PAD_L|TR64_PAD_A,0,TR64_PAD_L|TR64_PAD_A,TR64_PAD_L|TR64_PAD_START};
   static const uint16_t other[]={TR64_PAD_START,0,TR64_PAD_START,TR64_PAD_RIGHT,TR64_PAD_START,TR64_PAD_A,0,TR64_PAD_L|TR64_PAD_A,0,TR64_PAD_START};
   CHECK(reads<=sizeof(first)/sizeof(*first));
   pad->buttons=first[reads-1];if(second)second->buttons=other[reads-1];
  } else {
   static const uint16_t other[]={TR64_PAD_START,TR64_PAD_B,TR64_PAD_L|TR64_PAD_START,TR64_PAD_A,
                                 TR64_PAD_L|TR64_PAD_START,0,TR64_PAD_L|TR64_PAD_START|TR64_PAD_A};
   CHECK(ordinal==2 && reads<=sizeof(other)/sizeof(*other));
   if(second)second->buttons=other[reads-1];
  }
  return;
 }
 if(preview_mode && ordinal==1) {
  static const uint16_t script[]={TR64_PAD_L|TR64_PAD_UP|TR64_PAD_DOWN,0,
   TR64_PAD_L|TR64_PAD_UP,0,TR64_PAD_L|TR64_PAD_UP|TR64_PAD_Z,0,
   TR64_PAD_L|TR64_PAD_UP,0,TR64_PAD_L|TR64_PAD_UP};
  CHECK(reads<=sizeof(script)/sizeof(*script));
  pad->buttons=script[reads-1];
  return;
 }
 if(ordinal==14 && reads==2)pad->buttons=TR64_PAD_L|TR64_PAD_UP;
 if(b_down())pad->buttons=TR64_PAD_B;
 if(ordinal==14 && reads>3)pad->buttons=TR64_PAD_START; /* 3 idle frames, then START */
 /* START is still held from the skip: no skip edge, no SELECT into the level */
 if(ordinal==15)pad->buttons=reads>3 ? TR64_PAD_L|TR64_PAD_CLEFT : TR64_PAD_START;
 /* The first loop tick of LEVEL1 (ordinal 1): L + D-pad Down. L suppresses the game's input, so the tick
  * the game sees is as idle as the script expects, and the chord toggles the overlay exactly once. */
 if(ordinal==1 && reads==1 && !perf_requests && !(tr64_perf_state&2u))pad->buttons=TR64_PAD_L|TR64_PAD_DOWN;
 if(ordinal==4 && refuse_mode!=2)pad->buttons=TR64_PAD_L|TR64_PAD_CLEFT;
 if(ordinal==5)pad->buttons=TR64_PAD_L|TR64_PAD_START;
 if(start_pattern() || (ordinal==2 && !source_load) || (ordinal==13 && reads>=2))pad->buttons|=TR64_PAD_START;
 if(ordinal==7 || ordinal==9)pad->buttons=TR64_PAD_L|TR64_PAD_CDOWN;
 if(ordinal==10)pad->buttons=TR64_PAD_L|TR64_PAD_CLEFT;
}
void game_tick(uint16_t actions)
{
 if(pace_mode) { pace_tick(actions);return; }
 CHECK(alive && bound && allocated && !loading && stop.kind==TR64_GAME_STOP_NONE);
 CHECK(audio_active && !audio_pending);audio_pending=1;++game_ticks;++level_ticks;if(++burst>max_burst)max_burst=burst; /* ticks since the last picture (or level start) */
 if(refuse_mode && ordinal==(REFUSED_AT_STOP?3u:4u)) {
  /* The refused load went to a freshly started title: no carry, no restore, one message, no capture beyond the first two levels. */
  CHECK(!recursive_pending && !restores && messages==1 && captures==(REFUSED_AT_STOP?2u:3u) && transitions==captures);
  CHECK(!strcmp(expected[ordinal],"title"));
  puts(REFUSED_AT_STOP ? "play refusal PASS: an unusable or unconfirmed slot at the LOAD stop returns to the title with a message, nothing released for it"
                       : "play refusal PASS: a load the game rejects after the level is loaded returns to the title with a message");
  exit(0);
 }
 if(recursive_pending) { CHECK(actions==recursive_input);recursive_pending=0;return; }
 if(coop_mode && ordinal) {
  if(ordinal==1) { CHECK(reads<=2 && actions==TR64_GAME_SELECT);return; }
  CHECK(ordinal==2 && reads==7 && !actions && joins==2 && leaves==1 && warp_calls==2 &&
        busy_leaves==2 && busy_warps==1 && busy_split_frames==2 && coop_hud_clears==1 &&
        warp_notices==1 && warp_success==1 && !paired && !coop_ring);
  puts("play co-op PASS: BUSY preserved picker, split and HUD; retry and session restart worked");
  exit(0);
 }
 if(preview_mode && ordinal==1) {
  CHECK(!actions && !tr64_perf_state);
  if(reads==3) { CHECK(preview_on && preview_toggles==1);preview_menu=1; }
  if(reads==7) { CHECK(preview_toggles==1);preview_menu=0;stop.death_ready=1; }
  if(reads==9) {
   CHECK(!preview_on && preview_toggles==1);
   puts("play preview PASS: on in gameplay; ambiguous/gate, menu and death-ready presses ignored");
   exit(0);
  }
  return;
 }
 CHECK(actions==(expected_action()|expected_select())); /* a cutscene, the level after its skip and START held over a level change see no SELECT */
 switch(ordinal) {
 case 0:
  stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=2;stop.transition=TR64_GAME_TRANSITION_NEW_GAME;break;
 case 1:
  if(stop.death_ready) { stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=2;stop.transition=TR64_GAME_TRANSITION_RESTART; }
  else stop.death_ready=1;
  break;
 case 2:
  /* A load request: ACTION the picker passed (its second press), or, in the control, one the picker never saw. */
  if(source_load && refuse_mode!=3 && !(actions&TR64_GAME_ACTION))break;
  if(refuse_mode==1)tr64_save_refuse(2); /* the slot stops being usable between the picker and the stop */
  stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=source_load?25:3;
  stop.transition=source_load?TR64_GAME_TRANSITION_LOAD:TR64_GAME_TRANSITION_CAMPAIGN;break;
 case 3:
  if(!source_load && level_ticks<5)break; /* START held, released, pressed: see start_pattern */
  stop.kind=TR64_GAME_STOP_NOT_PORTED;
  memcpy(stop.name,"synthetic missing UI",sizeof("synthetic missing UI"));break;
 case 6:if(level_ticks==4)stop.kind=TR64_GAME_STOP_DEATH;break;
 case 8:case 11:
  stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=0;stop.transition=TR64_GAME_TRANSITION_TITLE;break;
 case 12:
  stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=4;stop.transition=TR64_GAME_TRANSITION_NEW_GAME;break;
 case 13:
  stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=5;stop.transition=TR64_GAME_TRANSITION_CAMPAIGN;break;
 case 14:case 16:
  /* ordinal 14 never ends by itself: only the skip; 16 runs its table out */
  CHECK(ordinal==16 || skipped || level_ticks<=4);
  if((ordinal==14 && skipped) || (ordinal==16 && level_ticks==5)) {
   stop.kind=TR64_GAME_STOP_LEVEL_END;stop.next_level_id=6;stop.transition=TR64_GAME_TRANSITION_CAMPAIGN;
  }
  break;
 case 15:break;
 case 17:
  CHECK((slow_us ? frames<=FRAMES_EXPECTED : frames==FRAMES_EXPECTED-(source_load?1u:0u)) && messages==1 && skips==1);
  CHECK(!slow_us || max_burst>=(slow_us>=100000u ? 3u : 2u)); /* the slow frames did carry several ticks (the script's levels are short: the bound of four is --pace's) */
  CHECK(audio_ticks+1==game_ticks && audio_starts==18 && audio_stops==17 && audio_reports==17);
  CHECK(ui_starts==18 && ui_stops==17 && ui_bound && ui_active);
  CHECK(perf_requests==1 && perf_begins>=2 && perf_ends>=1 && perf_ends<=perf_begins && tr64_perf_state==3);
  CHECK(captures==9 && transitions==9 && !reject_capture && restores==(unsigned)source_load && end_checks==1 && videos==1);
  printf("play policy PASS (extra %lu ms a frame, up to %u ticks in one): interactive death/restart, source transition reasons and repeated input, actual selected-load target, ordered UI/audio release, cutscene entry/skip/natural end/level select\n",(unsigned long)(slow_us/1000u),max_burst);
  exit(0);
 default:sys_error("manual switch did not precede game tick ordinal=%u",ordinal);
 }
}
void game_level_stop(void)
{
 CHECK(alive && bound && !loading && !stopped);
 CHECK(!audio_active && !audio_pending);
 CHECK(!ui_bound && !ui_active);
 CHECK(!front_active);
 alive=0;stopped=1;memset(&stop,0,sizeof(stop));
 paired=0;coop_ring=0;
}
void tr64_render_scene_release(void) { CHECK(!alive && bound && stopped && !loading);bound=0; }
static void pace_title_done(void);
void eng_level_release(void)
{
 if(pace_mode && !ordinal)pace_title_done();
 pace_first_draw=0;
 CHECK(!alive && !bound && stopped && !loading && allocated);
 const unsigned message_ordinal=REFUSED_AT_STOP?2u:3u;
 if(ordinal==message_ordinal)CHECK(clock_us-message_at>=2000000u);
 if(ordinal==message_ordinal)CHECK(audio_pumps>message_pump_at);
 if(ordinal==message_ordinal)CHECK(io_frames-message_io_at>=2u*TR64_TICK_HZ-1u);
 if(refuse_mode==2 && ordinal==3)CHECK(recursive_pending); /* no tick ran in the refused level */
 recursive_pending=0;
 memset(scratch.bytes,0,sizeof(scratch.bytes));
 allocated=stopped=0;frame_seen=0;++ordinal;reads=level_ticks=0;skipped=0;
}
int main(int argc,char **argv)
{
 if(argc>=3 && !strcmp(argv[1],"--slow")) { slow_us=(uint32_t)atoi(argv[2])*1000u;argv[2]=argv[0];argv+=2;argc-=2; }
 if(argc==2 && !strcmp(argv[1],"--invalid-carry"))reject_capture=1;
 else if(argc==2 && !strcmp(argv[1],"--invalid-ui"))reject_ui=1;
 else if(argc==2 && !strcmp(argv[1],"--source-load"))source_load=1;
 else if(argc==2 && !strcmp(argv[1],"--load-unusable")) { source_load=refuse_mode=1;expected[3]="title"; }
 else if(argc==2 && !strcmp(argv[1],"--load-refused")) { source_load=refuse_mode=2;expected[4]="title"; }
 else if(argc==2 && !strcmp(argv[1],"--end-from-menu"))menu_exit=1;
 else if(argc==2 && !strcmp(argv[1],"--load-unconfirmed")) { source_load=refuse_mode=3;expected[3]="title"; }
 else if(argc==2 && !strcmp(argv[1],"--pace")) { pace_mode=1;clock_step=250;pace_script(); }
 else if(argc==2 && !strcmp(argv[1],"--preview"))preview_mode=1;
 else if(argc==2 && !strcmp(argv[1],"--coop"))coop_mode=1;
 else CHECK(argc==1);
 prepare_device();
 tr64_play_run();
}
