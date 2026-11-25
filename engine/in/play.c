/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include "engine/in/play.h"
#include <string.h>
#include "engine/in/input.h"
#include "engine/in/pace.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/snd/snd.h"
#include "engine/ui/ui.h"
#include "engine/ui/frontend.h"
#include "engine/ui/perf.h"
#include "engine/save/game.h"
#include "tr64/game.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "tr64/render.h"
#include "tr64/route.h"
#include "tr64/video.h"
#ifdef TR64_HOST
#include "build_id.h"
#else
#include "cart_identity.h"
#endif

static tr64_game_drawable_t *drawables;
/* Source campaign order (tr1_campaign.h), the four in-engine cutscenes in
 * place: L/R level select steps through them as through any level. TITLE is
 * the final entry, outside the twenty-level manual-selection cycle. */
static const char *const levels[]={
 "gym","level1","level2","level3a","level3b","cut1","level4","level5","level6",
 "level7a","level7b","cut2","level8a","level8b","level8c","level10a","cut3",
 "level10b","cut4","level10c","title"
};
#define LEVEL_COUNT (sizeof(levels)/sizeof(levels[0])-1)
static unsigned select_level(unsigned command,unsigned current)
{
 switch(command) {
 case TR64_DEMO_RESTART:return current;
 case TR64_DEMO_PREVIOUS:return current ? current-1 : LEVEL_COUNT-1;
 case TR64_DEMO_NEXT:return current+1>=LEVEL_COUNT ? 0 : current+1;
 case TR64_DEMO_FIRST:return 1;
 case TR64_DEMO_GYM:return 0;
 default:sys_error("play: invalid demo command");
 }
}
static unsigned source_level(int32_t id,unsigned current,uint32_t reason)
{
 const char *name=game_level_name(id);
 if(name)for(unsigned i=0;i<=LEVEL_COUNT;++i)if(!strcmp(name,levels[i]))return i;
 sys_error("play: source level Core unavailable id=%ld from=%s reason=%lu",
  (long)id,levels[current],(unsigned long)reason);
}
static unsigned stop_message(const char *title,const char *name,unsigned current,uint16_t previous[2])
{
 uint32_t start=sys_time_us();tr64_pace_t pace;
 tr64_pace_reset(&pace,start,1);
 if(!tr64_ui_message(title,name))
  sys_error("play: UI stop text unavailable");
 /* The notice lasts two seconds of the clock; the pad is read once per due tick, as in play. */
 while((uint32_t)(sys_time_us()-start)<2000000u) {
  uint32_t due;
  tr64_io_pump();
  tr64_snd_pump();
  due=tr64_pace_due(&pace,sys_time_us());
  if(due)tr64_io_frame();
  for(;due;--due) {
   tr64_input_t pad[2];tr64_input_read_pair(pad);
   unsigned command=tr64_input_demo(pad[0],previous[0]);
   previous[0]=pad[0].buttons;previous[1]=pad[1].buttons;
   if(command)current=select_level(command,current);
  }
 }
 return current;
}
/* Lara's state for the shipping gate: the head of the game's own trace record
 * (position, room, animation, state, health), logged every tenth tick and at a
 * restored save. The gate reads displacement and the restored position from it. */
static void log_lara(const char *what,uint32_t tick,uint32_t us)
{
 char text[240];
 text[game_trace_prefix(text,sizeof(text)-1)]=0;
 log_printf("play-lara: %s tick=%lu us=%lu %s\n",what,(unsigned long)tick,(unsigned long)us,text);
}
/* The pacer's account of this level (engine/in/pace.h), with the hardware clock at the frame it was
 * taken: ticks+dropped equals the clock's 30 Hz count since the level began, to a tick. */
static void log_pace(const char *what,const tr64_pace_t *pace,uint32_t tick)
{
 log_printf("play-pace: %s us=%lu tick=%lu frames=%lu ticks=%lu hist=%lu/%lu/%lu/%lu dropped=%lu restarts=%lu\n",
  what,(unsigned long)pace->last,(unsigned long)tick,(unsigned long)pace->frames,(unsigned long)pace->ticks,
  (unsigned long)pace->histogram[1],(unsigned long)pace->histogram[2],(unsigned long)pace->histogram[3],
  (unsigned long)pace->histogram[4],(unsigned long)pace->dropped,(unsigned long)pace->restarts);
}
/* Gate lane only: tools/ship/gate-place.c, linked into the gate ROM and nowhere else. */
extern void tr64_play_gate(const char *level,uint16_t buttons,uint16_t previous) __attribute__((weak));
/* Gate lane only, once after each drawn frame: the load knob of tools/ship/gate-pace.c. */
extern void tr64_play_gate_frame(uint32_t tick,uint32_t frame_us) __attribute__((weak));
#if TR64_TEST
extern void tr64_play_probe_start(const char *level);
extern void tr64_play_probe_pad(const tr64_input_t pads[2],const uint16_t previous[2]);
extern void tr64_play_probe_tick(const uint16_t actions[TR64_GAME_PLAYERS]);
extern void tr64_play_probe_frame(void);
#endif
/* Run the gate's odometer every tick, but only one chord on the primary pad.
 * Z, R, B and A are gate-only; L+A is warp in an ordinary ROM. */
static void gate_tick(const char *level,uint16_t buttons,uint16_t previous,int claimed)
{
 if(!tr64_play_gate)return;
 if(claimed)buttons&=(uint16_t)~TR64_PAD_L;
 else {
  const uint16_t edges=(uint16_t)(buttons&~previous);
  if(edges&TR64_PAD_Z)buttons&=(uint16_t)~(TR64_PAD_R|TR64_PAD_B|TR64_PAD_A);
  else if(edges&TR64_PAD_R)buttons&=(uint16_t)~(TR64_PAD_B|TR64_PAD_A);
  else if(edges&TR64_PAD_B)buttons&=(uint16_t)~TR64_PAD_A;
 }
 tr64_play_gate(level,buttons,previous);
}
static int coop_allowed(unsigned current,const tr64_game_stop_t *stop)
{
 tr64_game_menu_view_t menu;
 return current!=LEVEL_COUNT && !stop->death_ready && !game_cutscene_active() && !game_menu_view(&menu);
}
static void coop_command(unsigned command,unsigned slot,int *session)
{
 switch(command) {
 case TR64_COOP_JOIN:
  if(!game_player_join()) {
   *session=1;tr64_render_preview_stop();log_printf("play-coop: joined\n");
  }
  break;
 case TR64_COOP_LEAVE: {
  const int result=game_player_leave();
  if(result==TR64_GAME_LEAVE_OK) {
   *session=0;tr64_ui_picker_owner(0);tr64_ui_coop_leave();log_printf("play-coop: left\n");
  }
  else if(result==TR64_GAME_LEAVE_BUSY)log_printf("play-coop: leave busy\n");
  break;
 }
 case TR64_COOP_WARP: {
  const int result=game_player_warp(slot);
  if(result==TR64_GAME_WARP_NO_MEDIPACK) {
   tr64_ui_coop_no_medipack(slot);log_printf("play-coop: no-medipack player=%u\n",slot+1);
  } else if(result==TR64_GAME_WARP_OK)tr64_ui_coop_warped(slot);
  else if(result==TR64_GAME_WARP_BUSY)log_printf("play-coop: warp busy player=%u\n",slot+1);
  break;
 }
 }
}
static void preview_chord(unsigned current,const tr64_game_stop_t *stop)
{
 if(!(game_player_mask()&2u) && coop_allowed(current,stop))tr64_render_preview_toggle();
}
static void draw(void)
{
 tr64_game_view_t view;
 game_view_camera(&view);
 tr64_play_message_ready(0);
 if(!tr64_ui_game_frame(&view,drawables,TR64_GAME_DRAW_CHUNK))sys_error("play: UI frame unavailable");
 tr64_play_message_ready(1);
 log_printf("ui: submit_us=%lu\n",(unsigned long)tr64_ui_last_submit_us());
}
/* After every game tick that leaves the game running: what the presentation takes from that tick
 * (pickup events last only until the next tick). */
static void ui_tick(void)
{
 tr64_game_view_t view;
 game_view_camera(&view);
 tr64_ui_game_tick(&view,drawables,TR64_GAME_DRAW_CHUNK);
}
static uint32_t start_hash(void)
{
#if !defined(TR64_HOST) && TR64_PLAY && !TR64_TEST
 code_loading_begin();
 const int had_front=code_is_loaded("front");
 if(had_front)code_unload("front");
 code_load("trace");
 const uint32_t hash=code_call("trace",0);
 code_unload("trace");
 if(had_front)code_load("front");
 code_loading_end();
 return hash;
#else
 return game_state_hash();
#endif
}
static void load(const char *name,const tr64_game_inventory_carry_t *carry)
{
 const tr64_level_t *level;
 mem_level_begin();
 code_loading_begin();code_set_frame_handler(NULL,NULL);
 log_printf("play: loading=%s\n",name);log_flush();
 tr64_scene_load(name);level=tr64_level_current();
 if(!level)sys_error("play: core not ready");
 if(!tr64_ui_bind(level))sys_error("play: UI font not ready");
 tr64_ui_set_active(1);
 tr64_play_message_ready(1);
 if(carry)game_level_start_transition(level,name,carry);
 else game_level_start(level,name);
 const tr64_game_stop_t *stop=game_stop_info();
 if(!stop)sys_error("play: game not loaded");
 if(stop->kind!=TR64_GAME_STOP_NONE) { code_loading_end();return; }
 tr64_snd_level_start(level);tr64_snd_pump();
 drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*drawables));
 tr64_render_game_scene_prepare();
 tr64_ui_game_start(level,name);
 code_loading_end();
}
void tr64_play_run(void)
{
 uint32_t tick=0,frame=0;unsigned current=LEVEL_COUNT; /* boots to TITLE */
 uint16_t previous[2]={0},last_actions[2]={0};unsigned hold_select=0;int coop_session=0;
 unsigned saves=tr64_save_game_saves();
 tr64_game_inventory_carry_t carry;int carry_next=0,restore_next=0;
 const char *video_from=NULL;int video_advance=0; /* NULL from = boot */
#ifdef TR64_HOST
 log_printf("play: build_id=%s mode=live\n",TR64_BUILD_ID);
#else
 log_printf("play: build_id=" TR64_BUILD_ID " config_id=" TR64_CONFIG_ID " mode=live\n");
#endif
 log_flush();
 tr64_render_scene_set_live(1);game_init();
 /* Before the first level: what each level of this pack is, for the slot picker. */
 tr64_save_identity_bind(levels,LEVEL_COUNT+1);
 tr64_save_game_require_confirmation();
 for(;;) {
  tr64_pace_t pace;
  int recursive=carry_next,restore=restore_next;
  /* No level is resident here: the one point for videos. */
  if(tr64_video_transition(video_from,levels[current],video_advance)) {
   /* A video was skipped with START: still held, it is neither an edge nor a
    * SELECT (ring) for the level that follows; released during the film, it is
    * no longer held either. */
   tr64_input_t held[2];tr64_input_read_pair(held);
   for(unsigned slot=0;slot<2;++slot) {
    previous[slot]=held[slot].buttons;
    if(held[slot].buttons&TR64_PAD_START)hold_select|=1u<<slot;
    else hold_select&=~(1u<<slot);
   }
  }
  video_from=levels[current];video_advance=0;
  load(levels[current],carry_next ? &carry : NULL);carry_next=restore_next=0;
  const tr64_game_stop_t *loaded=game_stop_info();const int primed=recursive || game_cutscene_active();
  if(!loaded)sys_error("play: loaded game unavailable");
  int load_refused=0;
  if(loaded->kind==TR64_GAME_STOP_NONE) {
   if(restore && !tr64_save_game_load()) {
    /* The game's own check rejected the slot (another build's save, a changed
     * cook): the level of the save is loaded but nothing of it plays. */
    log_printf("play: selected SRAM load refused status=%d\n",tr64_save_game_status());log_flush();
    current=stop_message("SAVE NOT USABLE","RETURNING TO TITLE",LEVEL_COUNT,previous);
    load_refused=1;
   } else {
    if(restore)log_lara("restored",tick,sys_time_us());
    if(coop_session && current!=LEVEL_COUNT && !game_cutscene_active()) {
     if(game_player_join())sys_error("play: could not rejoin co-op session");
     log_printf("play-coop: rejoined\n");
    }
#if TR64_TEST
    tr64_play_probe_start(levels[current]);
#endif
    if(primed) {
     /* Source startLevel/gameLoad recurses with the action that requested it.
      * This is the new level's first frame, still before its simulation. A
      * cutscene (also a level-select start) only has its camera after its first
      * tick: take it before the first picture, which then is not empty. */
     code_frame(frame++);tr64_io_frame();
     if(game_player_mask()&2u)game_tick_players(recursive?last_actions:(const uint16_t[2]){0,0});
     else game_tick(recursive?last_actions[0]:0);
#if TR64_TEST
     tr64_play_probe_tick(recursive?last_actions:(const uint16_t[2]){0,0});
#endif
     tr64_snd_tick();tr64_snd_pump();++tick;
     loaded=game_stop_info();
     if(loaded && loaded->kind==TR64_GAME_STOP_NONE)ui_tick();
    }
    loaded=game_stop_info();
    if(loaded && loaded->kind==TR64_GAME_STOP_NONE) {
     draw();tr64_mem_log();
#if TR64_TEST
     tr64_play_probe_frame();
#endif
     log_printf("play: start_hash=%s %08lx\n",levels[current],(unsigned long)start_hash());
     log_printf("play: ready=%s hz=%u\n",levels[current],TR64_TICK_HZ);log_flush();
    }
   }
  }
  /* Game time starts here: a level load, a video or a long first frame never owes ticks. The first
   * tick of a level that already ran its entry tick (primed) is one tick period away. */
  tr64_pace_reset(&pace,sys_time_us(),primed ? 0u : 1u);
  while(!load_refused) {
   const tr64_game_stop_t *stop=game_stop_info();
   uint32_t due,run,tick_us=0;uint16_t frame_actions=0;int leave=0;
   if(!stop)sys_error("play: stopped game not loaded");
   if(stop->kind!=TR64_GAME_STOP_NONE) {
    tr64_render_preview_stop();
    /* START may have ended this level (the statistics page): its SELECT is then
     * neither replayed into the next level's first tick (a minute of film may lie
     * between) nor delivered while START stays held. */
    for(unsigned slot=0;slot<2;++slot)if(previous[slot]&TR64_PAD_START) {
     hold_select|=1u<<slot;last_actions[slot]&=(uint16_t)~TR64_GAME_SELECT;
    }
    log_printf("play-stop: level=%s kind=%lu next_level_id=%ld death_ready=%lu name=%s\n",
     levels[current],(unsigned long)stop->kind,(long)stop->next_level_id,
     (unsigned long)stop->death_ready,stop->name);log_flush();
    if(stop->kind==TR64_GAME_STOP_LEVEL_END) {
     int32_t target=stop->next_level_id;
     if(stop->transition>TR64_GAME_TRANSITION_LOAD)sys_error("play: invalid source transition");
     if(stop->transition==TR64_GAME_TRANSITION_LOAD) {
      tr64_save_info_t info;
      /* The player confirmed this slot in the picker, and this pack can load
       * it. The ring is still resident here, so anything else (no confirmation
       * from the picker, an empty or foreign slot) goes to the title, never
       * fatal and never to a slot nobody chose. */
      const int confirmed=tr64_save_game_take_load_confirmation();
      if(!confirmed || tr64_save_slot_status(tr64_save_game_selected(),&info)!=TR64_SAVE_OK) {
       log_printf("play: selected SRAM slot refused confirmed=%d\n",confirmed);log_flush();
       current=stop_message("SAVE NOT USABLE","RETURNING TO TITLE",LEVEL_COUNT,previous);
       break;
      }
      target=info.level_id;restore_next=1;
     }
     unsigned next=source_level(target,current,stop->transition);
     if(!game_inventory_capture(&carry))sys_error("play: inventory capture failed");
     /* The end of the game is a TITLE transition out of LEVEL10C; the stop
      * carries no cause, so leaving through the passport (its closing menu was
      * the last picture) is told apart from finishing the level. */
     const int game_end=stop->transition==TR64_GAME_TRANSITION_TITLE && !strcmp(levels[current],"level10c") &&
      !tr64_ui_menu_was_shown();
     carry.transition=stop->transition;carry_next=1;current=next;
     video_advance=!restore_next && (stop->transition==TR64_GAME_TRANSITION_CAMPAIGN ||
      stop->transition==TR64_GAME_TRANSITION_NEW_GAME || stop->transition==TR64_GAME_TRANSITION_GYM || game_end);
    }
    else if(stop->kind==TR64_GAME_STOP_NOT_PORTED)
     current=stop_message("NOT PORTED - RESTARTING",stop->name,current,previous);
    else if(stop->kind!=TR64_GAME_STOP_NONE && stop->kind!=TR64_GAME_STOP_DEATH)
     sys_error("play: unknown game stop kind");
    break;
   }
   {const uint32_t io_stamp=tr64_perf_stamp();tr64_io_pump();TR64_PERF_ADD(tr64_perf_io_ticks,io_stamp);}
   tr64_snd_pump();
   tr64_ui_game_poll();
   due=tr64_pace_due(&pace,sys_time_us());
   if(!due)continue; /* nothing is owed: never draw the same state again */
   tr64_perf_frame_begin(due);
   /* One code-unit transition point and one I/O allowance per drawn frame, before its simulation,
    * however many ticks it runs: units never change between the ticks of a frame. */
   code_frame(frame++);tr64_io_frame();
   for(run=0;run<due && !leave;++run) {
    /* Each tick reads the pad afresh; edges (commands, START, the picker's and the game's own) are
     * against the previous tick's state, so a press that lies in two ticks is one edge and a press
     * seen by any tick is seen once. */
    tr64_input_t pads[2];tr64_input_read_pair(pads);
    const tr64_input_t pad=pads[0];uint32_t start;
#if TR64_TEST
    tr64_play_probe_pad(pads,previous);
#endif
    const tr64_game_settings_t *cfg=game_settings_view();
    const int tr3_moves=cfg && cfg->controls_abilities==TR64_LARA_ABILITIES_TR3;
    const unsigned command=(!tr3_moves || (pad.buttons&TR64_PAD_R)) ? tr64_input_demo(pad,previous[0]) : TR64_DEMO_NONE;
    const unsigned perf_chord=(!tr3_moves || (pad.buttons&TR64_PAD_R)) ? tr64_input_perf(pad,previous[0]) : TR64_PERF_CHORD_NONE;
    if(perf_chord==TR64_PERF_CHORD_OVERLAY)tr64_perf_toggle();
    else if(perf_chord==TR64_PERF_CHORD_PREVIEW)preview_chord(current,stop);
    gate_tick(levels[current],pad.buttons,previous[0],command || perf_chord);
    /* START ends a cutscene through its own LEVEL_END (next tick); it is not
     * an input to the cutscene or, still held, to the level that follows. */
    if(!command && (pad.buttons&TR64_PAD_START) && !(previous[0]&TR64_PAD_START) && game_cutscene_skip()) {
     log_printf("play-skip: level=%s\n",levels[current]);log_flush();hold_select|=1u;
    }
    if(!(pad.buttons&TR64_PAD_START))hold_select&=~1u;
    if(!(pads[1].buttons&TR64_PAD_START))hold_select&=~2u;
    /* A restart's held START must not reach the next level's ring. */
    if(command && (pad.buttons&TR64_PAD_START))hold_select|=1u;
    if(command) {
     unsigned next=select_level(command,current);
     log_printf("play-select: from=%s to=%s command=%u\n",levels[current],levels[next],command);log_flush();
     current=next;leave=1;
     previous[0]=pad.buttons;previous[1]=pads[1].buttons;
     if(pads[1].buttons&TR64_PAD_START)hold_select|=2u;
     break;
    }
    const int paired=(game_player_mask()&2u)!=0;
    const unsigned p0=!tr64_play_gate && !perf_chord
      ? tr64_input_coop(pad,previous[0],0,paired,0):TR64_COOP_NONE;
    const uint16_t primary_pressed=(uint16_t)(pad.buttons&~previous[0]);
    const unsigned p1=tr64_input_coop(pads[1],previous[1],1,paired,primary_pressed);
    /* An ignored join/leave edge must not become a ring press after L is released. */
    if(p1==TR64_COOP_JOIN || p1==TR64_COOP_LEAVE ||
       (pads[1].buttons&(TR64_PAD_L|TR64_PAD_START))==(TR64_PAD_L|TR64_PAD_START))hold_select|=2u;
    previous[0]=pad.buttons;previous[1]=pads[1].buttons;
    if((p0 || (p1 && p1!=TR64_COOP_LEAVE)) && coop_allowed(current,stop)) {
     coop_command(p0,0,&coop_session);
     if(p1!=TR64_COOP_LEAVE)coop_command(p1,1,&coop_session);
    }
    if(p1==TR64_COOP_LEAVE)coop_command(p1,1,&coop_session);
    uint16_t actions[2]={0};
    uint32_t extra_in[2]={0};
    const int cutscene=game_cutscene_active();
    if(!cutscene) {
     if(tr3_moves) {
      if((pad.buttons&(TR64_PAD_L|TR64_PAD_R))!=(TR64_PAD_L|TR64_PAD_R)) {
       tr64_input_t p0_in=pad;
       p0_in.buttons&=(uint16_t)~(TR64_PAD_L|TR64_PAD_CLEFT);
       actions[0]=tr64_input_game(p0_in);
       if(pad.buttons&TR64_PAD_L)extra_in[0]|=TR64_GAME_EXTRA_SPRINT;
       if(pad.buttons&TR64_PAD_CLEFT)extra_in[0]|=TR64_GAME_EXTRA_CROUCH;
      }
     } else if(!(pad.buttons&TR64_PAD_L)) {
      actions[0]=tr64_input_game(pad);
     }
    }
    if(hold_select&1u)actions[0]&=(uint16_t)~TR64_GAME_SELECT;
    const int now_paired=p0 || p1 ? (game_player_mask()&2u)!=0 : paired;
    if(now_paired) {
     if(!cutscene) {
      if(tr3_moves) {
       if((pads[1].buttons&(TR64_PAD_L|TR64_PAD_R))!=(TR64_PAD_L|TR64_PAD_R)) {
        tr64_input_t p1_in=pads[1];
        p1_in.buttons&=(uint16_t)~(TR64_PAD_L|TR64_PAD_CLEFT);
        actions[1]=tr64_input_game(p1_in);
        if(pads[1].buttons&TR64_PAD_L)extra_in[1]|=TR64_GAME_EXTRA_SPRINT;
        if(pads[1].buttons&TR64_PAD_CLEFT)extra_in[1]|=TR64_GAME_EXTRA_CROUCH;
       }
      } else if(!(pads[1].buttons&TR64_PAD_L)) {
       actions[1]=tr64_input_game(pads[1]);
      }
     }
     if(hold_select&2u)actions[1]&=(uint16_t)~TR64_GAME_SELECT;
     const unsigned owner=game_menu_player();
     tr64_ui_picker_owner(owner);
     actions[owner]=tr64_ui_picker_filter(actions[owner]);
    } else actions[0]=tr64_ui_picker_filter(actions[0]);
    last_actions[0]=actions[0];last_actions[1]=actions[1];frame_actions|=actions[0];start=sys_time_us();
    {const uint32_t sim_stamp=tr64_perf_stamp();
     game_set_extra_input_player(0,extra_in[0]);
     game_set_extra_input_player(1,extra_in[1]);
     if(now_paired)game_tick_players(actions);
     else game_tick(actions[0]);
     TR64_PERF_ADD(tr64_perf_sim_ticks,sim_stamp);}
    {const uint32_t took=sys_time_us()-start;if(took>tick_us)tick_us=took;}
#if TR64_TEST
    tr64_play_probe_tick(actions);
#endif
    /* Everything the game hands out for one tick is taken before the next tick replaces it. */
    {const uint32_t snd_stamp=tr64_perf_stamp();tr64_snd_tick();TR64_PERF_ADD(tr64_perf_snd_ticks,snd_stamp);}
    tr64_snd_pump();++tick;
    /* The ring is up in the tick that saves, so Lara has not moved since. A save is a stall. */
    if(tr64_save_game_saves()!=saves) {
     saves=tr64_save_game_saves();log_lara("saved",tick,sys_time_us());tr64_pace_restart(&pace,sys_time_us());
    }
    stop=game_stop_info();
    if(!stop)sys_error("play: tick lost game");
    if(stop->kind!=TR64_GAME_STOP_NONE || stop->death_ready)tr64_render_preview_stop();
    if(stop->kind!=TR64_GAME_STOP_NONE)break;
    ui_tick();
    if(!(tick%10))log_lara("tick",tick,pace.last);
    if(!(tick%TR64_TICK_HZ))log_pace("tick",&pace,tick);
   }
   if(leave)break;
   stop=game_stop_info();
   if(!stop)sys_error("play: tick lost game");
   if(stop->kind!=TR64_GAME_STOP_NONE)continue;
   draw();sys_heap_check();
#if TR64_TEST
   tr64_play_probe_frame();
#endif
   if(TR64_PERF_ON()) {
    tr64_game_view_t view;game_view_camera(&view);
    tr64_perf_end(&(tr64_perf_frame_t){due,pace.dropped,view.camera_room,levels[current]});
   }
   /* input: every action any of the frame's ticks received; tick_us: the slowest of them. */
   log_printf("play-frame: tick=%lu input=%04x tick_us=%lu ticks=%lu\n",
    (unsigned long)tick,frame_actions,(unsigned long)tick_us,(unsigned long)due);
   log_flush();
   if(tr64_play_gate_frame)tr64_play_gate_frame((uint32_t)tick,pace.last);
  }
  if(!load_refused)log_pace("level",&pace,tick);
  log_flush();
  /* No game/unit/renderer pointer may outlive these ordered release steps. */
  tr64_snd_level_stop();tr64_snd_report();
  tr64_ui_game_stop();
  game_level_stop();tr64_render_scene_release();
  drawables=NULL;
  eng_level_release();
 }
}
