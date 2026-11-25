/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/io.h"
#include "tr64/mem.h"
#include "tr64/pack.h"
#include "tr64/res.h"
#include "tr64/render.h"
#include "tr64/level.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/in/input.h"
#include "engine/in/pace.h"
#include "engine/in/play.h"
#include "engine/save/game.h"
#include "engine/snd/snd.h"
#include "engine/ui/frontend.h"
#include "engine/ui/perf.h"
#include "engine/ui/picker.h"
#include "engine/ui/ui.h"
#include "build_id.h"

enum { PACK_MAX_LEVELS = 24 };

static char level_name[32];
static char pack_levels[PACK_MAX_LEVELS][32];
static unsigned pack_level_count;
static bool pack_has_title;
static char first_gameplay_level[32];

static const char *const campaign_order[] = {
 "wall","boat","venice","opera","rig","platform","unwater","keel",
 "living","deck","skidoo","monastry","catacomb","icecave","emprtomb",
 "floating","xian","house","assault"
};

static void copy_name(char dst[32],const char *src)
{
 size_t n=strlen(src);
 if(n>31)n=31;
 memcpy(dst,src,n);
 dst[n]=0;
}

static bool pack_has(const char *name)
{
 if(!name || !name[0])return false;
 if(!strcmp(name,"title"))return pack_has_title;
 for(unsigned i=0;i<pack_level_count;++i)
  if(!strcmp(pack_levels[i],name))return true;
 return false;
}

static void pack_scan_tr2(void)
{
 tr64_pack_t pack;
 uint32_t bytes=tr64_level_pack_bind_streams();
 tr64_load_scope_begin();
 tr64_pack_mount_catalog(&pack,bytes);
 while(!tr64_pack_poll(&pack)) {
  if(tr64_io_frame_bytes()>=TR64_IO_FRAME)tr64_io_frame();
  tr64_io_pump();
 }
 pack_level_count=0;
 pack_has_title=false;
 first_gameplay_level[0]=0;
 char raw_levels[PACK_MAX_LEVELS][32];
 unsigned raw_count=0;
 const char *id_names[PACK_MAX_LEVELS+1];
 unsigned id_count=0;
 for(uint32_t i=0;i<pack.levels;++i) {
  const uint8_t *e=pack.directory+i*48u;
  if(tr64_be32(e+44u)!=2u)continue;
  char name[32];
  memcpy(name,e,32u);
  name[31]=0;
  if(!strcmp(name,"title")) {
   pack_has_title=true;
  } else if(raw_count<PACK_MAX_LEVELS) {
   memcpy(raw_levels[raw_count],name,sizeof(name));
   ++raw_count;
  }
 }
 /* Order gameplay levels in campaign_order so Next/Previous chords follow the campaign. */
 bool placed[PACK_MAX_LEVELS]={0};
 for(unsigned c=0;c<sizeof(campaign_order)/sizeof(campaign_order[0]);++c) {
  for(unsigned i=0;i<raw_count;++i) {
   if(!placed[i] && !strcmp(raw_levels[i],campaign_order[c])) {
    memcpy(pack_levels[pack_level_count],raw_levels[i],32u);
    id_names[id_count++]=pack_levels[pack_level_count];
    ++pack_level_count;
    placed[i]=true;
    break;
   }
  }
 }
 for(unsigned i=0;i<raw_count;++i) {
  if(!placed[i] && pack_level_count<PACK_MAX_LEVELS) {
   memcpy(pack_levels[pack_level_count],raw_levels[i],32u);
   id_names[id_count++]=pack_levels[pack_level_count];
   ++pack_level_count;
  }
 }
 if(pack_has_title)id_names[id_count++]="title";
 tr64_pack_unmount(&pack);
 tr64_load_scope_end();

 if(pack_level_count>0)
  copy_name(first_gameplay_level,pack_levels[0]);
 if(!first_gameplay_level[0] && !pack_has_title)
  sys_error("Tomb Raider II: no TR2 level in cart pack");
 tr64_save_identity_bind(id_names,id_count);
 tr64_save_game_require_confirmation();
}

static void show_notice(const char *title,const char *body,uint16_t *previous)
{
 uint32_t start=sys_time_us();
 tr64_pace_t pace;
 tr64_pace_reset(&pace,start,1);
 if(!tr64_ui_message(title,body))return;
 while((uint32_t)(sys_time_us()-start)<2000000u) {
  tr64_io_pump();
  tr64_snd_pump();
  uint32_t due=tr64_pace_due(&pace,sys_time_us());
  if(due)tr64_io_frame();
  for(;due;--due) {
   tr64_input_t pad=tr64_input_read();
   *previous=pad.buttons;
  }
 }
}

static void draw(tr64_game_drawable_t *drawables,uint32_t ticks,uint32_t dropped)
{
 tr64_game_view_t view;
 if(!strcmp(level_name,"title")) {
  memset(&view,0,sizeof(view));
  view.camera_room=UINT32_MAX;
 } else {
  game_view_camera(&view);
 }
 tr64_play_message_ready(0);
 tr64_ui_game_frame(&view,drawables,TR64_GAME_DRAW_CHUNK);
 tr64_play_message_ready(1);
 if(ticks && TR64_PERF_ON())
  tr64_perf_end(&(tr64_perf_frame_t){ticks,dropped,view.camera_room,level_name});
}

static uint32_t actions(tr64_input_t pad,uint32_t *extra_out)
{
 uint16_t buttons=pad.buttons;
 const tr64_game_settings_t *cfg=game_settings_view();
 const bool tr3_moves=cfg && cfg->controls_abilities==TR64_LARA_ABILITIES_TR3;
 pad.buttons &= (uint16_t)~(TR64_PAD_START|TR64_PAD_CDOWN|TR64_PAD_CLEFT|TR64_PAD_CRIGHT);
 uint32_t mask=tr64_input_game(pad);
 uint32_t extra=0;
 if(tr3_moves) {
  if(buttons&TR64_PAD_L)extra|=TR64_GAME_EXTRA_SPRINT;
  if(buttons&TR64_PAD_CLEFT)extra|=TR64_GAME_EXTRA_CROUCH;
  if(buttons&TR64_PAD_CRIGHT)mask|=TR2_IN_FLARE;
  if(buttons&TR64_PAD_CDOWN)mask|=TR2_IN_ROLL;
 } else {
  if(buttons&TR64_PAD_L)mask|=TR2_IN_FLARE;
  if(buttons&TR64_PAD_CDOWN)mask|=TR2_IN_ROLL;
  if(buttons&TR64_PAD_CLEFT)mask|=TR2_IN_STEP_LEFT;
  if(buttons&TR64_PAD_CRIGHT)mask|=TR2_IN_STEP_RIGHT;
 }
 if(extra_out)*extra_out=extra;
 return mask;
}

typedef struct {
 tr64_game_drawable_t *drawables;
 uint16_t *previous;
 bool opened;
} ring_input_t;

static bool run_ring(ring_input_t *ring,bool key)
{
 const uint32_t began=sys_time_us();
 if(!game_tr2_ring_open(key))return false;
 ring->opened=true;
 tr64_perf_restart_window();
 tr64_pace_t menu_pace;
 tr64_pace_reset(&menu_pace,sys_time_us(),0);
 uint16_t held=*ring->previous&(TR64_PAD_START|TR64_PAD_B|TR64_PAD_A);
 draw(ring->drawables,0,0);
 tr64_mem_log();
 bool open=true;
 while(open) {
  tr64_io_pump();
  tr64_snd_pump();
  tr64_ui_game_poll();
  uint32_t due=tr64_pace_due(&menu_pace,sys_time_us());
  if(!due)continue;
  tr64_input_t pad=tr64_input_read();
  *ring->previous=pad.buttons;
  held&=pad.buttons;
  pad.buttons&=(uint16_t)~held;
  do {
   uint16_t menu_actions=tr64_ui_picker_filter(tr64_input_game(pad));
   open=game_tr2_ring_update(menu_actions);
   tr64_snd_tick();
   tr64_snd_pump();
  } while(open && --due);
  if(open)draw(ring->drawables,0,0);
 }
 const tr64_game_stop_t *stop=game_stop_info();
 if(stop && stop->kind!=TR64_GAME_STOP_NONE) {
  log_printf("play-tr2: ring transition kind=%lu next=%ld tr=%lu\n",
             (unsigned long)stop->kind,(long)stop->next_level_id,(unsigned long)stop->transition);
  return true;
 }
 const uint32_t elapsed=(uint32_t)(((uint64_t)(sys_time_us()-began)*TR64_TICK_HZ)/1000000u);
 const unsigned type=game_tr2_ring_choice();
 game_tr2_ring_commit(type,elapsed);
 log_printf("play-tr2: ring closed type=%u clock=%lu\n",type,(unsigned long)elapsed);
 return false;
}

static void choose_key(void *context) { (void)run_ring(context,true); }

static tr64_game_drawable_t *load_level(const char *name,const tr64_game_inventory_carry_t *carry,
                                        bool start_menu)
{
 mem_level_begin();
 code_loading_begin();
 code_set_frame_handler(NULL,NULL);
 log_printf("play-tr2: loading=%s start_menu=%u\n",name,(unsigned)start_menu);
 log_flush();
 tr64_render_scene_set_live(1);
 tr64_scene_load_held(name);
 tr64_level_validate_tr2_gameplay(tr64_level_current());
 tr64_load_scope_end();
 game_init();
 if(carry) {
  game_level_start_transition(tr64_level_current(),name,carry);
 } else if(start_menu) {
  tr64_game_inventory_carry_t title_carry={.transition=TR64_GAME_TRANSITION_TITLE};
  game_level_start_transition(tr64_level_current(),name,&title_carry);
 } else {
  game_tr2_start(tr64_level_current(),name,0u);
 }
 tr64_game_drawable_t *drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*drawables));
 tr64_render_game_scene_prepare();
 code_loading_end();
 tr64_snd_level_start(tr64_level_current());
 tr64_snd_pump();
 tr64_ui_game_start(tr64_level_current(),name);
 tr64_play_message_ready(1);
 return drawables;
}

static void set_title_target(char dst[32],bool *start_menu)
{
 if(pack_has_title) {
  copy_name(dst,"title");
  *start_menu=false;
 } else {
  copy_name(dst,first_gameplay_level);
  *start_menu=true;
 }
}

static bool select_pack_level(unsigned cmd,char dst[32])
{
 if(!pack_level_count)return false;
 unsigned cur=0;
 for(unsigned i=0;i<pack_level_count;++i) {
  if(!strcmp(pack_levels[i],dst)) { cur=i; break; }
 }
 switch(cmd) {
 case TR64_DEMO_RESTART:
  return true;
 case TR64_DEMO_PREVIOUS:
  copy_name(dst,pack_levels[cur ? cur-1u : pack_level_count-1u]);
  return true;
 case TR64_DEMO_NEXT:
  copy_name(dst,pack_levels[(cur+1u)%pack_level_count]);
  return true;
 case TR64_DEMO_FIRST:
  copy_name(dst,first_gameplay_level);
  return true;
 case TR64_DEMO_GYM:
  if(!pack_has("assault"))return false;
  copy_name(dst,"assault");
  return true;
 default:
  return false;
 }
}

static void apply_transition(const tr64_game_stop_t *stop,tr64_game_inventory_carry_t *carry,
                             int *carry_next,int *restore_next,bool *start_menu,uint16_t *previous)
{
 *carry_next=0;
 *restore_next=0;
 if(stop->transition==TR64_GAME_TRANSITION_LOAD) {
  tr64_save_info_t info;
  const int confirmed=tr64_save_game_take_load_confirmation();
  const char *target=NULL;
  if(confirmed && tr64_save_slot_status(tr64_save_game_selected(),&info)==TR64_SAVE_OK)
   target=game_level_name(info.level_id);
  if(!target || !pack_has(target)) {
   log_printf("play-tr2: selected SRAM slot refused confirmed=%d\n",confirmed);
   log_flush();
   show_notice("SAVE NOT USABLE","RETURNING TO TITLE",previous);
   if(game_inventory_capture(carry)) {
    carry->transition=TR64_GAME_TRANSITION_TITLE;
    *carry_next=1;
   }
   set_title_target(level_name,start_menu);
   return;
  }
  if(game_inventory_capture(carry)) {
   carry->transition=TR64_GAME_TRANSITION_LOAD;
   *carry_next=1;
  }
  *restore_next=1;
  *start_menu=false;
  copy_name(level_name,target);
  return;
 }

 if(stop->transition==TR64_GAME_TRANSITION_NEW_GAME) {
  if(game_inventory_capture(carry)) {
   carry->transition=TR64_GAME_TRANSITION_NEW_GAME;
   *carry_next=1;
  }
  copy_name(level_name,first_gameplay_level);
  *start_menu=false;
  return;
 }

 if(stop->transition==TR64_GAME_TRANSITION_RESTART) {
  if(game_inventory_capture(carry)) {
   carry->transition=TR64_GAME_TRANSITION_RESTART;
   *carry_next=1;
  }
  *start_menu=false;
  return;
 }

 if(stop->transition==TR64_GAME_TRANSITION_GYM && pack_has("assault")) {
  if(game_inventory_capture(carry)) {
   carry->transition=TR64_GAME_TRANSITION_GYM;
   *carry_next=1;
  }
  copy_name(level_name,"assault");
  *start_menu=false;
  return;
 }

 if(stop->transition==TR64_GAME_TRANSITION_CAMPAIGN) {
  const char *next=game_level_name(stop->next_level_id);
  if(next && pack_has(next)) {
   if(game_inventory_capture(carry)) {
    carry->transition=TR64_GAME_TRANSITION_CAMPAIGN;
    *carry_next=1;
   }
   copy_name(level_name,next);
   *start_menu=false;
   return;
  }
 }

 /* Title transition or single-level pack completion -> return to Title / Start Menu. */
 if(game_inventory_capture(carry)) {
  carry->transition=TR64_GAME_TRANSITION_TITLE;
  *carry_next=1;
 }
 set_title_target(level_name,start_menu);
}

void tr64_play_run(void)
{
 log_printf("play-tr2: build_id=%s mode=live\n",TR64_BUILD_ID);
 log_flush();
 pack_scan_tr2();

 bool start_menu=false;
 set_title_target(level_name,&start_menu);
 tr64_game_inventory_carry_t carry={0};
 int carry_next=0,restore_next=0;
 uint16_t previous=0;

 for(;;) {
  const int restore=restore_next;
  tr64_game_drawable_t *drawables=load_level(level_name,carry_next ? &carry : NULL,start_menu);
  carry_next=restore_next=0;

  ring_input_t ring={.drawables=drawables,.previous=&previous};
  const bool is_title=!strcmp(level_name,"title");
  if(!is_title)
   game_tr2_bind_key_ring(choose_key,&ring);

  int load_refused=0;
  if(restore && !tr64_save_game_load()) {
   log_printf("play-tr2: selected SRAM load refused status=%d\n",tr64_save_game_status());
   log_flush();
   show_notice("SAVE NOT USABLE","RETURNING TO TITLE",&previous);
   if(game_inventory_capture(&carry)) {
    carry.transition=TR64_GAME_TRANSITION_TITLE;
    carry_next=1;
   }
   set_title_target(level_name,&start_menu);
   load_refused=1;
  }

  if(!load_refused && (is_title || start_menu)) {
   /* Title screen or single-level Start Menu: run the shared Title ring until a
    * gameplay transition (New Game / Load Game / Lara's Home) is chosen. */
   for(;;) {
    tr64_io_pump();
    tr64_snd_pump();
    if(run_ring(&ring,false)) {
     const tr64_game_stop_t *stop=game_stop_info();
     if(stop && stop->kind==TR64_GAME_STOP_LEVEL_END) {
      apply_transition(stop,&carry,&carry_next,&restore_next,&start_menu,&previous);
      break;
     }
     if(stop && stop->kind!=TR64_GAME_STOP_NONE) {
      show_notice("NOTICE",stop->name[0] ? stop->name : "RETURNING TO TITLE",&previous);
      set_title_target(level_name,&start_menu);
      break;
     }
    } else {
     set_title_target(level_name,&start_menu);
     break;
    }
   }
  } else if(!load_refused) {
   tr64_pace_t pace;
   uint32_t last_mask=0,frame=0;
   draw(drawables,0,0);
   tr64_mem_log();
   log_printf("play-tr2: ready=%s hz=%u\n",level_name,TR64_TICK_HZ);
   log_flush();
   tr64_pace_reset(&pace,sys_time_us(),0);

   for(;;) {
    uint32_t io_ticks=0;
    if(TR64_PERF_ON()) {
     const uint32_t stamp=sys_profile_ticks();
     tr64_io_pump();
     io_ticks=sys_profile_ticks()-stamp;
    } else tr64_io_pump();
    tr64_snd_pump();
    tr64_ui_game_poll();
    uint32_t due=tr64_pace_due(&pace,sys_time_us());
    if(!due)continue;
    tr64_input_t pad=tr64_input_read();
    const unsigned demo_cmd=(pad.buttons&TR64_PAD_R) ? tr64_input_demo(pad,previous) : TR64_DEMO_NONE;
    bool start=(pad.buttons&TR64_PAD_START) && !(previous&TR64_PAD_START);
    const bool profile_start=start && (pad.buttons&TR64_PAD_R) && !(pad.buttons&TR64_PAD_L);
    previous=pad.buttons;
    if(demo_cmd) {
     char from[32];
     copy_name(from,level_name);
     if(select_pack_level(demo_cmd,level_name)) {
      log_printf("play-tr2: select from=%s to=%s command=%u\n",from,level_name,demo_cmd);
      log_flush();
      start_menu=false;
      break;
     }
    }
    if(start && !profile_start && !((pad.buttons&(TR64_PAD_L|TR64_PAD_R))==(TR64_PAD_L|TR64_PAD_R))) {
     if(run_ring(&ring,false)) {
      const tr64_game_stop_t *stop=game_stop_info();
      if(stop && stop->kind==TR64_GAME_STOP_LEVEL_END) {
       apply_transition(stop,&carry,&carry_next,&restore_next,&start_menu,&previous);
       break;
      }
     }
     draw(drawables,0,0);
     tr64_pace_restart(&pace,sys_time_us());
     continue;
    }
    if(profile_start)tr64_perf_toggle();
    if(tr64_perf_state) {
     tr64_perf_begin(due);
     if(TR64_PERF_ON())tr64_perf_io_ticks=io_ticks;
    }
    code_frame(frame++);
    tr64_io_frame();
    uint32_t extra_in=0;
    uint32_t mask=((pad.buttons&(TR64_PAD_L|TR64_PAD_R))==(TR64_PAD_L|TR64_PAD_R)) ? 0u : actions(pad,&extra_in);
    game_set_extra_input(extra_in);
    if(mask!=last_mask) {
     int32_t xyz[3];
     game_tr2_position(xyz);
     log_printf("play-tr2: input=%05lx Lara=%ld,%ld,%ld\n",(unsigned long)mask,
                (long)xyz[0],(long)xyz[1],(long)xyz[2]);
     last_mask=mask;
    }
    const uint32_t ticks=due;
    ring.opened=false;
    bool leave_level=false;
    do {
     const uint32_t sim=tr64_perf_stamp();
     game_tr2_tick(mask);
     TR64_PERF_ADD(tr64_perf_sim_ticks,sim);
     tr64_snd_tick();
     tr64_snd_pump();
     const tr64_game_stop_t *stop=game_stop_info();
     if(stop && stop->kind==TR64_GAME_STOP_DEATH) {
      if(game_level_id()==101) { /* TR2_LVL_HOME: return directly to Title */
       log_printf("play-tr2: home death -> returning to Title\n");
       log_flush();
       set_title_target(level_name,&start_menu);
       leave_level=true;
       break;
      }
      log_printf("play-tr2: death -> opening Death Passport\n");
      log_flush();
      for(;;) {
       tr64_io_pump();
       tr64_snd_pump();
       if(run_ring(&ring,false)) {
        stop=game_stop_info();
        if(stop && stop->kind==TR64_GAME_STOP_LEVEL_END) {
         apply_transition(stop,&carry,&carry_next,&restore_next,&start_menu,&previous);
         leave_level=true;
         break;
        }
        if(stop && stop->kind!=TR64_GAME_STOP_NONE && stop->kind!=TR64_GAME_STOP_DEATH) {
         set_title_target(level_name,&start_menu);
         leave_level=true;
         break;
        }
       } else {
        set_title_target(level_name,&start_menu);
        leave_level=true;
        break;
       }
      }
      break;
     }
     if(stop && stop->kind==TR64_GAME_STOP_LEVEL_END) {
      if(game_level_id()==101) { /* TR2_LVL_HOME: skip stats, return to Title */
       log_printf("play-tr2: home end -> returning to Title\n");
       log_flush();
       set_title_target(level_name,&start_menu);
       leave_level=true;
       break;
      }
      log_printf("play-tr2: level end -> showing Level Statistics\n");
      log_flush();
      (void)run_ring(&ring,false);
      stop=game_stop_info();
      if(stop && stop->kind==TR64_GAME_STOP_LEVEL_END)
       apply_transition(stop,&carry,&carry_next,&restore_next,&start_menu,&previous);
      else
       set_title_target(level_name,&start_menu);
      leave_level=true;
      break;
     }
     if(stop && stop->kind!=TR64_GAME_STOP_NONE) {
      log_printf("play-tr2: stop kind=%lu name=%s -> returning to title\n",
                 (unsigned long)stop->kind,stop->name);
      log_flush();
      show_notice("NOTICE",stop->name[0] ? stop->name : "RETURNING TO TITLE",&previous);
      set_title_target(level_name,&start_menu);
      leave_level=true;
      break;
     }
     tr64_ui_game_tick(NULL,drawables,TR64_GAME_DRAW_CHUNK);
     if(ring.opened) {
      tr64_pace_restart(&pace,sys_time_us());
      break;
     }
    } while(--due);
    if(leave_level)break;
    draw(drawables,ticks,pace.dropped);
   }
  }

  tr64_snd_level_stop();
  tr64_snd_report();
  tr64_ui_game_stop();
  game_level_stop();
  tr64_render_scene_release();
  drawables=NULL;
  eng_level_release();
 }
}
