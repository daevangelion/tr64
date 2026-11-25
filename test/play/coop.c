/* SPDX-License-Identifier: MIT */
/* Test-only observations from the live play loop, not a second simulation path. */
#include <string.h>
#include "build_id.h"
#if !TR64_TEST
#error co-op play probe requires TR64_TEST=1
#endif
#include "engine/core/log.h"
#include "engine/core/sys.h"
#include "engine/in/input.h"
#include "gate_place.h"
#include "tr64/level.h"
#include "tr64/render.h"
#include "tr64/game.h"
#include "game/internal.h"

static uint32_t epoch,tick;
static int active;
enum { WOLF_INDEX=26 };
static int wolf_fight,wolf_target,wolf_hit[2];
static int16_t wolf_health[2];

static void wolf_place(unsigned slot)
{
 const unsigned other=slot^1u;
 if(!game_place_player(slot,14,22016,6656,51200,-32768) ||
    !game_place_player(other,0,75264,3072,3584,0))
  sys_error("co-op wolf: directed placement refused");
 if(!wolf_fight) {
  /* Same authored wolf and explicit activation as test/game/coop.py. */
  if(gs->level.itemsCount<=WOLF_INDEX || gs->items[WOLF_INDEX].type!=ITEM_WOLF)
   sys_error("co-op wolf: unexpected authored item");
  ItemObj *wolf=gs->items+WOLF_INDEX;
  wolf->timer=0;wolf->flags|=ITEM_FLAG_MASK;
  if(!(wolf->flags&ITEM_FLAG_ACTIVE))worldTriggerActivate(wolf);
 }
 wolf_fight=1;
 for(unsigned i=0;i<2;++i) { wolf_health[i]=gs->players[i]->health;wolf_hit[i]=0; }
 log_printf("coop-wolf: near=%u\n",slot);
}
static void wolf_observe(void)
{
 if(!wolf_fight || game_player_mask()!=3)return;
 ItemObj *wolf=gs->items+WOLF_INDEX;
 const ItemObj *target=wolf->extraE?wolf->extraE->target:NULL;
 const int slot=target==gs->players[1]?1:target==gs->players[0]?0:-1;
 if(slot!=wolf_target) {
  wolf_target=slot;log_printf("coop-wolf: target=%d\n",slot);
 }
 for(unsigned i=0;i<2;++i) {
  const int health=gs->players[i]->health;
  if(!wolf_hit[i] && health<wolf_health[i]) {
   wolf_hit[i]=1;
   log_printf("coop-wolf: hit=%u health_before=%d health_after=%d\n",i,wolf_health[i],health);
  }
 }
}

/* Read the two short Lara prefixes out of the streamed public trace. The
 * long camera/nav records between them never need to fit in scratch. */
typedef struct { char text[2][192];unsigned slot,mark,used;int writing; } trace_head_t;
static void capture_head(void *context,const char *bytes,uint32_t count)
{
 static const char marker[]=",\"lara\":";
 static const char end[]=",\"anim\":";
 trace_head_t *head=context;
 for(uint32_t i=0;i<count && head->slot<2;++i) {
  char ch=bytes[i];
  if(!head->writing) {
   if(ch!=marker[head->mark])head->mark=0;
   if(ch==marker[head->mark] && !marker[++head->mark]) {
    head->mark=0;head->writing=1;
   }
   continue;
  }
  char *text=head->text[head->slot];
  if(head->used==sizeof(head->text[0]))sys_error("co-op test Lara trace prefix too long");
  text[head->used++]=ch;
  const unsigned suffix=sizeof(end)-1;
  if(head->used>=suffix && !memcmp(text+head->used-suffix,end,suffix)) {
   text[head->used-suffix]=0;++head->slot;head->used=0;head->writing=0;
  }
 }
}
static void start_poses(void)
{
 trace_head_t head={0};
 game_trace_schema(5,capture_head,&head);
 if(head.slot!=2)sys_error("co-op test missing start pose");
 for(unsigned slot=0;slot<2;++slot) {
  const char *p=strstr(head.text[slot],"\"pos\":[");
  if(!p)sys_error("co-op test incomplete start pose");
  log_printf("coop-start-pose: epoch=%lu slot=%u {%s}\n",(unsigned long)epoch,slot,p);
 }
}

static void record(const uint16_t actions[TR64_GAME_PLAYERS])
{
 const uint32_t present=game_player_mask(),living=game_player_alive_mask();
 tr64_game_view_t first,second;
 game_view_camera_player(0,&first);
 if(present&2u)game_view_camera_player(1,&second);
 log_printf("coop-tick: epoch=%lu tick=%lu input=%04x,%04x present=%lu alive=%lu room=%lu,%ld hash=%08lx\n",
  (unsigned long)epoch,(unsigned long)tick,actions[0],actions[1],(unsigned long)present,
  (unsigned long)living,(unsigned long)first.camera_room,present&2u?(long)second.camera_room:-1L,
  (unsigned long)game_state_hash_schema(5));
}
void tr64_play_probe_start(const char *level)
{
 active=!strcmp(level,"level1");
 if(!active)return;
 wolf_fight=0;wolf_target=-1;
 ++epoch;tick=0;
 record((const uint16_t[TR64_GAME_PLAYERS]){0,0});
 if(game_player_mask()==3)start_poses();
}
void tr64_play_probe_tick(const uint16_t actions[TR64_GAME_PLAYERS])
{
 /* State hash export requires a running game. A terminal transition is
  * reported by the play loop, not a simulation record in either replay. */
 if(active && game_stop_info()->kind==TR64_GAME_STOP_NONE) { ++tick;record(actions);wolf_observe(); }
}
void tr64_play_probe_frame(void)
{
 if(!active)return;
 const tr64_render_counts_t *counts=tr64_render_scene_counts();
 log_printf("coop-frame: epoch=%lu tick=%lu views=%u\n",(unsigned long)epoch,(unsigned long)tick,
            counts && counts->split.split?2u:1u);
}
void tr64_play_probe_pad(const tr64_input_t pads[2],const uint16_t previous[2])
{
 if(!active || !(game_player_mask()&2u))return;
 for(unsigned slot=0;slot<2;++slot) {
  uint16_t held=pads[slot].buttons,edge=held&~previous[slot];
  if(!(held&TR64_PAD_L))continue;
  uint32_t room;int32_t x,y,z;int16_t yaw=0;
  if(edge&TR64_PAD_B) { wolf_place(slot);continue; }
  if(edge&TR64_PAD_Z) {
   const int32_t pose[]={GATE_DEATH};
   room=(uint32_t)pose[0];x=pose[1];y=pose[2];z=pose[3];yaw=(int16_t)pose[4];
  } else if(edge&TR64_PAD_R) {
   /* LEVEL1 item 29 is the room-17 small medipack (PC data). */
   const tr64_core_items_t *item=tr64_level_items(tr64_level_current(),29);
   if(tr64_u16(item->type)!=93)sys_error("co-op test pickup is not a small medipack");
   room=tr64_u16(item->room);x=tr64_i32(item->x);y=tr64_i32(item->y);z=tr64_i32(item->z);
  } else continue;
  if(!game_place_player(slot,room,x,y,z,yaw))sys_error("co-op test placement refused");
  log_printf("coop-place: epoch=%lu tick=%lu slot=%u pose=%lu %ld %ld %ld %d\n",
   (unsigned long)epoch,(unsigned long)tick+1,slot,(unsigned long)room,(long)x,(long)y,(long)z,yaw);
 }
}
