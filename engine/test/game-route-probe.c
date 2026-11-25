/* SPDX-License-Identifier: MIT */
/* Asset-free lifecycle fixture, linked only by test/run/game-route-tests.py.
 * This verifies dispatch, not Tomb Raider simulation or oracle parity. */
#include <string.h>
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/game.h"
#include "tr64/route.h"
#include "tr64/code.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
struct tr64_level { uint32_t marker; };
static const struct tr64_level core={0x47414d45u};
static uint8_t *workspace;
static const char *loaded_name;
static uint32_t ticks,camera_calls,stop_queries,prepare_calls,sound_reads;
static uint32_t hashed_tick=UINT32_MAX,rendered_tick=UINT32_MAX;
extern const uint32_t tr64_scene_test_no_hash __attribute__((weak));
static int perf_only(void) { return &tr64_scene_test_no_hash && tr64_scene_test_no_hash; }
static uint32_t expected_hash(uint32_t tick) { return perf_only() ? UINT32_MAX : tick; }
#ifndef TR64_GAME_ROUTE_NO_WORK_STAMP
static uint32_t frame_completed,frame_pacing;
#endif
enum { NORMAL,EMPTY,BAD_CURSOR,BAD_CHUNK,STOP,NULL_STOP,AUDIO_BUMP,AUDIO_RESET,PACING,BAD_STAMP };
static int loaded,initialized,started,placed,mode;
#if defined(TR64_GAME_ROUTE_BOUNDARY)
static int reloaded;
#endif
extern uint32_t game_view_room(uint32_t,uint32_t *,tr64_game_drawable_t *,uint32_t) __attribute__((weak));
void tr64_scene_load(const char *name)
{
 if(loaded)sys_error("game route probe duplicate load");
 if(!strcmp(name,"level1"))mode=NORMAL;
 else if(!strcmp(name,"empty-view"))mode=EMPTY;
 else if(!strcmp(name,"bad-cursor"))mode=BAD_CURSOR;
 else if(!strcmp(name,"bad-chunk"))mode=BAD_CHUNK;
 else if(!strcmp(name,"stop-view"))mode=STOP;
 else if(!strcmp(name,"null-stop"))mode=NULL_STOP;
 else if(!strcmp(name,"audio-bump"))mode=AUDIO_BUMP;
 else if(!strcmp(name,"audio-reset"))mode=AUDIO_RESET;
 else if(!strcmp(name,"pacing-view"))mode=PACING;
 else if(!strcmp(name,"bad-stamp"))mode=BAD_STAMP;
 else sys_error("game route probe load identity");
 loaded_name=name;
#if defined(TR64_GAME_ROUTE_BOUNDARY)
 if(reloaded)(void)tr64_mem_alloc(TR64_MEM_state,16);
#endif
 workspace=tr64_mem_alloc(TR64_MEM_state,17);memset(workspace,0xa5,17);loaded=1;
}
const struct tr64_level *tr64_level_current(void) { return loaded ? &core : NULL; }
void game_init(void)
{
 if(!loaded || initialized)sys_error("game route probe init order/count");
 initialized=1;
}
void game_level_start(const struct tr64_level *level,const char *name)
{
 uint8_t *state;
 if(!initialized || started || level!=&core || strcmp(name,loaded_name))
  sys_error("game route probe start order/identity");
 state=eng_game_state_alloc(16);
 if(state!=workspace+32)sys_error("game route probe reset renderer workspace");
 memset(state,0x5a,16);started=1;
 log_printf("game route probe start\n");
}
int game_place(uint32_t room,int32_t x,int32_t y,int32_t z,int16_t angle)
{
 if(!started || ticks || placed || prepare_calls || camera_calls)sys_error("game route probe placement order/count");
 if(room!=7 || x!=INT32_MIN || y!=INT32_MAX || z!=-7 || angle!=INT16_MIN)return 0;
 placed=1;log_printf("game route probe placed\n");return 1;
}
void game_tick(uint16_t mask)
{
 if(!started || ticks>=2 || mask!=(ticks ? 8 : 65535))sys_error("game route probe raw action/tick mismatch");
 if(rendered_tick!=ticks)sys_error("game route probe missing preceding rendered view");
 ++ticks;
 log_printf("game route probe tick=%lu\n",(unsigned long)ticks);
}
const tr64_game_sound_event_t *game_sound_events(uint32_t *count)
{
 static tr64_game_sound_event_t event;
 if(!started || !ticks || sound_reads+1!=ticks || hashed_tick!=expected_hash(ticks-1) || rendered_tick!=ticks-1)
  sys_error("game route probe sound consumption order/count");
 sound_reads=ticks;
 event.sound=mode==AUDIO_BUMP ? INT16_MIN : mode==AUDIO_RESET ? INT16_MIN+1 : (int16_t)ticks;
 event.volume=123;*count=1;
 return &event;
}
#ifndef TR64_GAME_ROUTE_NO_STOP
const tr64_game_stop_t *game_stop_info(void)
{
 static tr64_game_stop_t stop;
 if(!started || !ticks || stop_queries+1!=ticks || hashed_tick!=expected_hash(ticks-1) || rendered_tick!=ticks-1)
  sys_error("game route probe stop query order/count");
 ++stop_queries;
 if(mode==NULL_STOP)return NULL;
 stop.kind=mode==STOP ? TR64_GAME_STOP_NOT_PORTED : TR64_GAME_STOP_NONE;
 stop.next_level_id=-1;
 if(mode==STOP)memcpy(stop.name,"game route probe named stop",sizeof("game route probe named stop"));
 return &stop;
}
#endif
uint32_t game_state_hash(void)
{
 uint32_t i;
 if(perf_only())sys_error("performance-only route unexpectedly called gameplay hash");
 if(!started || stop_queries!=ticks)sys_error("game route probe hash before stop query");
#ifndef TR64_GAME_ROUTE_NO_PREPARE
 if(prepare_calls!=1)sys_error("game route probe hash before renderer preparation");
#endif
 for(i=0;i<17;++i)if(workspace[i]!=0xa5)sys_error("game route probe lost renderer workspace");
 hashed_tick=ticks;
 return 0x70000000u+(placed ? 256u : 0u)+ticks;
}
#if defined(TR64_GAME_ROUTE_SCHEMA3)
uint32_t game_state_hash_schema(uint32_t schema)
{
 if(schema!=3)sys_error("game route probe wrong hash schema");
 return game_state_hash()+0x03000000u;
}
#endif
#ifndef TR64_GAME_ROUTE_NO_PREPARE
void tr64_render_game_scene_prepare(void)
{
 tr64_mem_stats_t state;
 if(!started || prepare_calls ||
#if defined(TR64_GAME_ROUTE_BOUNDARY)
    (reloaded ? camera_calls!=1 || hashed_tick!=0 :
#endif
    camera_calls || hashed_tick!=UINT32_MAX
#if defined(TR64_GAME_ROUTE_BOUNDARY)
    )
#endif
    )
  sys_error("game route probe preparation order/count");
 tr64_mem_stats(TR64_MEM_state,&state);
 if(state.used!=48+TR64_GAME_DRAW_CHUNK*sizeof(tr64_game_drawable_t)
#if defined(TR64_GAME_ROUTE_BOUNDARY)
    +(reloaded ? 16u : 0u)
#endif
    )
  sys_error("game route probe preparation before fixed chunk allocation");
 ++prepare_calls;log_printf("game route probe prepare\n");
}
#endif
#ifndef TR64_GAME_ROUTE_NO_CAMERA
void game_view_camera(tr64_game_view_t *out)
{
 if(!started || hashed_tick!=expected_hash(ticks) || rendered_tick==ticks || camera_calls!=ticks)
  sys_error("game route probe camera order/count");
 ++camera_calls;memset(out,0,sizeof(*out));
 out->camera_eye[0]=(int32_t)ticks;out->camera_room=5;out->texture_animation_step=17+ticks;
}
#endif
#ifndef TR64_GAME_ROUTE_NO_ROOM
uint32_t game_view_room(uint32_t room,uint32_t *cursor,tr64_game_drawable_t *buffer,uint32_t capacity)
{
 uint32_t start,n,total=mode==EMPTY ? 0 : 1+70*ticks;
 if(room!=5 || !cursor || !buffer || capacity!=TR64_GAME_DRAW_CHUNK || camera_calls!=ticks+1)
  sys_error("game route probe room pull arguments/order");
 start=*cursor;
 if(start==TR64_GAME_VIEW_END || start>total)sys_error("game route probe invalid cursor");
 if(mode==BAD_CHUNK)return capacity+1; /* Required whole item: no output or cursor write. */
 n=total-start;if(n>capacity)n=capacity;
 for(uint32_t i=0;i<n;++i) {
  buffer[i].instance_id=100+start+i;buffer[i].translation[0]=(int32_t)ticks;
 }
 if(mode!=BAD_CURSOR)*cursor=start+n==total ? TR64_GAME_VIEW_END : start+n;
 return n;
}
#endif
#ifndef TR64_GAME_ROUTE_NO_RENDER
void tr64_render_game_scene_frame(const tr64_game_view_t *view,tr64_game_drawable_t *buffer,uint32_t capacity)
{
 uint32_t cursor=0,total=0,pulls=0,expected=mode==EMPTY ? 0 : 1+70*ticks;
 if(view->camera_eye[0]!=(int32_t)ticks || view->camera_room!=5 || view->texture_animation_step!=17+ticks ||
    capacity!=TR64_GAME_DRAW_CHUNK || buffer!=(tr64_game_drawable_t *)(workspace+48))
  sys_error("game route probe renderer camera/fixed buffer mismatch");
#ifndef TR64_GAME_ROUTE_NO_WORK_STAMP
 frame_pacing=0;
 if(mode==PACING) {
  uint32_t wait_start=sys_time_us();
  while((uint32_t)(sys_time_us()-wait_start)<2000u) { }
  frame_pacing=sys_time_us()-wait_start;
 }
#endif
 while(cursor!=TR64_GAME_VIEW_END) {
  uint32_t before=cursor,n=game_view_room(view->camera_room,&cursor,buffer,capacity);++pulls;
  if(n>capacity)sys_error("game route probe whole item exceeds chunk");
  if(cursor==before)sys_error("game route probe cursor did not advance");
  for(uint32_t i=0;i<n;++i)if(buffer[i].instance_id!=100+total+i || buffer[i].translation[0]!=(int32_t)ticks)
   sys_error("game route probe room chunk data mismatch");
  total+=n;
 }
 if(total!=expected)sys_error("game route probe room chunks truncated");
#ifndef TR64_GAME_ROUTE_NO_WORK_STAMP
 frame_completed=sys_time_us();
#endif
 if(!ticks)log_printf("game route probe capacity=%lu bytes=%lu\n",(unsigned long)capacity,(unsigned long)(capacity*sizeof(*buffer)));
 rendered_tick=ticks;
 log_printf("game route probe render tick=%lu drawables=%lu pulls=%lu\n",(unsigned long)ticks,(unsigned long)total,(unsigned long)pulls);
}
#endif
#if defined(TR64_GAME_ROUTE_UI) && !defined(TR64_GAME_ROUTE_NO_RENDER)
int tr64_ui_game_frame(const tr64_game_view_t *view,tr64_game_drawable_t *buffer,uint32_t capacity)
{
 if(!view || view->camera_room!=5 || capacity!=TR64_GAME_DRAW_CHUNK ||
    buffer!=(tr64_game_drawable_t *)(workspace+48) || camera_calls!=ticks+1)
  sys_error("game route probe UI changed camera/chunk/query order");
 log_printf("game route probe UI bound=%u tick=%lu\n",TR64_GAME_ROUTE_UI,(unsigned long)ticks);
 if(!TR64_GAME_ROUTE_UI)return 0;
 tr64_render_game_scene_frame(view,buffer,capacity);return 1;
}
#endif
/* Asset-free lifecycle controls, not a packed-level or gameplay substitute. */
#if defined(TR64_GAME_ROUTE_BOUNDARY)
void game_level_stop(void) { started=0;ticks=0; }
void tr64_render_scene_release(void) { prepare_calls=0; }
void tr64_level_release(void) { loaded=0; }
int tr64_scene_game_boundary(const tr64_route_t *route,uint32_t tick,uint16_t input)
{
 if(!route->game || route->ticks!=2 || !route->level || !started ||
    (tick && (ticks!=tick || hashed_tick!=tick-1 || rendered_tick!=tick-1)) ||
    (!tick && (ticks || hashed_tick!=UINT32_MAX || camera_calls)))
  sys_error("game route probe boundary order/metadata");
 log_printf("game route probe boundary tick=%lu input=%u\n",(unsigned long)tick,input);
 if(TR64_GAME_ROUTE_BOUNDARY==2)return 2;
 if(TR64_GAME_ROUTE_BOUNDARY==3)return 1;
 if(TR64_GAME_ROUTE_BOUNDARY==4) { eng_level_release();return 0; }
 if(TR64_GAME_ROUTE_BOUNDARY==1 && tick==1 && !reloaded) {
  game_level_stop();tr64_render_scene_release();eng_level_release();reloaded=1;
  mem_level_begin();code_loading_begin();tr64_scene_load("level1");
  game_level_start(tr64_level_current(),"level1");code_loading_end();
  return 1;
 }
 return 0;
}
#endif
#ifndef TR64_GAME_ROUTE_NO_WORK_STAMP
int tr64_render_scene_work_stamp(uint32_t *completed,uint32_t *pacing)
{
 if(rendered_tick==UINT32_MAX)return 0;
 *completed=frame_completed;*pacing=mode==BAD_STAMP ? UINT32_MAX : frame_pacing;
 return 1;
}
#endif
