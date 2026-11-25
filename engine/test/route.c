/* SPDX-License-Identifier: MIT */
/* Integer tick-window route VM, ported from MIT ac3n64 04134cc.
 * Version 3: control header, level identity, then dedicated game/placement
 * words. Version 4 widens sequel action masks in each event.
 * CRC covers all identity/scene fields and input events. */
#include <string.h>
#include "tr64/route.h"
#include "tr64/res.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "tr64/limits.h"
#include "tr64/code.h"
#include "tr64/game.h"
#include "tr64/eng.h"
#if TR64_HAS_TR2
#include "engine/in/wall.h"
extern tr64_game_drawable_t *tr64_wall_load(const char *,const int32_t *,unsigned) __attribute__((weak));
extern void game_tr2_route_update(uint32_t,unsigned,uint32_t) __attribute__((weak));
extern uint32_t game_tr2_state_hash(void) __attribute__((weak));
#endif
#include "engine/mem/pool.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
extern void tr64_scene_load(const char *level) __attribute__((weak));
/* Optional game and render modules share the public, schema-independent DTO.
 * Requested hooks must all be present; ordinary scenes do not invoke them. */
extern const struct tr64_level *tr64_level_current(void) __attribute__((weak));
extern void tr64_level_release(void) __attribute__((weak));
extern void tr64_level_reload_probe(const char *) __attribute__((weak));
extern void tr64_video_probe(const char *name,uint32_t max_ticks) __attribute__((weak));
extern void game_init(void) __attribute__((weak));
extern void game_level_start(const struct tr64_level *,const char *) __attribute__((weak));
extern void game_tick(uint16_t) __attribute__((weak));
extern int game_place(uint32_t,int32_t,int32_t,int32_t,int16_t) __attribute__((weak));
extern uint32_t game_state_hash(void) __attribute__((weak));
extern uint32_t game_state_hash_schema(uint32_t) __attribute__((weak));
extern void game_view_camera(tr64_game_view_t *) __attribute__((weak));
extern uint32_t game_view_room(uint32_t,uint32_t *,tr64_game_drawable_t *,uint32_t) __attribute__((weak));
extern const tr64_game_stop_t *game_stop_info(void) __attribute__((weak));
extern void tr64_render_game_scene_frame(const tr64_game_view_t *,tr64_game_drawable_t *,uint32_t) __attribute__((weak));
extern int tr64_ui_game_frame(const tr64_game_view_t *,tr64_game_drawable_t *,uint32_t) __attribute__((weak));
extern void tr64_render_game_scene_prepare(void) __attribute__((weak));
extern int tr64_render_scene_work_stamp(uint32_t *,uint32_t *) __attribute__((weak));
extern void tr64_snd_level_start(const struct tr64_level *) __attribute__((weak));
extern void tr64_snd_level_stop(void) __attribute__((weak));
extern void tr64_snd_tick(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));
extern int tr64_scene_game_boundary(const tr64_route_t *,uint32_t,uint16_t) __attribute__((weak));
/* Explicit performance probes cannot also be used as gameplay hash evidence. */
extern const uint32_t tr64_scene_test_no_hash __attribute__((weak));
static int perf_only;
static uint32_t game_ticks,perf_room;
#if TR64_CART_TR2
static int game_active;
#else
static int game_initialized,game_active;
#endif
static tr64_game_drawable_t *game_drawables;
static const tr64_route_t *game_route;
static uint32_t clock_wrap_start;
static int clock_wrap_active;
static int32_t signed_word(const uint8_t *p)
{
 uint32_t n=tr64_be32(p);
 return n<=INT32_MAX ? (int32_t)n : -(int32_t)(~n)-1;
}
#if TR64_HAS_TR2
static int sequel_schema(uint32_t schema) { return schema==20 || schema==21 || schema==22; }
#endif
int tr64_route_open(tr64_route_t *r,const uint8_t *p,uint32_t n)
{
 uint32_t count,ticks,i,last=0,length=0;
#if TR64_HAS_TR2
 int tr2=p && n>=TR64_ROUTE_HEADER && sequel_schema(tr64_be32(p+92)) && tr64_be32(p+64);
#endif
 memset(r,0,sizeof(*r));
 if(!p || n<TR64_ROUTE_HEADER || memcmp(p,"TR6R",4) ||
#if TR64_HAS_TR2
    tr64_be32(p+4)!=(tr2 ? TR64_ROUTE_VERSION_TR2 : TR64_ROUTE_VERSION) ||
#else
    tr64_be32(p+4)!=TR64_ROUTE_VERSION ||
#endif
    tr64_be32(p+8)!=n || tr64_be32(p+28)>TR64_PROBE_VIDEO ||
    tr64_be32(p+64)>1 || tr64_be32(p+68)>1 ||
    (tr64_be32(p+92) &&
#if TR64_HAS_TR2
     !tr2 &&
#endif
     (tr64_be32(p+92)!=3 || !tr64_be32(p+64))))return -1;
 ticks=tr64_be32(p+12); count=tr64_be32(p+20);
 if(!ticks || ticks>
#if TR64_HAS_TR2
    (tr2 ? 900u : TR64_SCENE_TICKS_MAX)
#else
    TR64_SCENE_TICKS_MAX
#endif
    || count>TR64_ROUTE_OPS ||
    n!=TR64_ROUTE_HEADER+12*count || tr64_crc32(0,p+32,n-32)!=tr64_be32(p+24))return -1;
 while(length<32 && p[32+length]) {
  uint8_t c=p[32+length];
  if(!((c>='a' && c<='z') || (length && ((c>='0' && c<='9') || c=='_' || c=='-'))))return -1;
  ++length;
 }
 if(length==32)return -1;
 for(i=length;i<32;++i)if(p[32+i])return -1;
 if((tr64_be32(p+64) && !length) || (tr64_be32(p+68) && !tr64_be32(p+64)))return -1;
 if(tr64_be32(p+28)>=TR64_PROBE_RELOAD && (!length || tr64_be32(p+64)))return -1;
 if(!tr64_be32(p+68)) { for(i=72;i<92;++i)if(p[i])return -1; }
 else if(signed_word(p+88)<INT16_MIN || signed_word(p+88)>INT16_MAX)return -1;
 for(i=0;i<count;++i) {
  const uint8_t *e=p+TR64_ROUTE_HEADER+12*i;
  uint32_t t=tr64_be32(e);
#if TR64_HAS_TR2
  if(tr2) {
   uint32_t mask=tr64_be32(e+4),ring=tr64_be32(e+8);
   if(t>=ticks || (i && t<=last) || ((mask & ~TR2_INPUT_MASK) && mask!=TR64_GAME_SELECT))
    return -1;
   if(mask==TR64_GAME_SELECT) {
    if(ring || i+1==count || tr64_be32(e+12)!=t+1u ||
       tr64_be32(e+16)==TR64_GAME_SELECT ||
       (tr64_be32(e+20)&(TR2_RING_EVENT|TR2_RING_EVENT_KEY))!=TR2_RING_EVENT)
     return -1;
   }
   if(ring) {
    unsigned type=ring&(TR2_RING_EVENT-1u);
    if(!(ring&TR2_RING_EVENT) || type>=TR2_PUBLIC_TYPES ||
       ((ring&TR2_RING_EVENT_KEY) &&
        (!(mask&TR64_GAME_ACTION) || (type && (type<197 || type>200)))))
     return -1;
   }
   last=t;
   continue;
  }
#endif
  int x=e[6]<128 ? e[6] : (int)e[6]-256;
  int y=e[7]<128 ? e[7] : (int)e[7]-256;
  if(t>=ticks || (i && t<=last) || x < -80 || x>80 || y < -80 || y>80 || tr64_be32(e+8))return -1;
  if(tr64_be32(p+64) && (x || y))return -1;
  last=t;
 }
 r->data=p; r->level=length ? (const char *)p+32 : NULL;
 r->ticks=ticks; r->seed=tr64_be32(p+16); r->count=count; r->probe=tr64_be32(p+28);
 r->game=(uint8_t)tr64_be32(p+64);r->place=(uint8_t)tr64_be32(p+68);
 r->place_room=tr64_be32(p+72);r->place_x=signed_word(p+76);
 r->place_y=signed_word(p+80);r->place_z=signed_word(p+84);
 r->place_angle=(int16_t)signed_word(p+88);
 r->game_hash_schema=tr64_be32(p+92)==3 ? 3u : 2u;
#if TR64_HAS_TR2
 if(tr2)r->game_hash_schema=tr64_be32(p+92);
#endif
 return 0;
}
tr64_input_t tr64_route_tick(tr64_route_t *r,uint32_t tick)
{
#if TR64_HAS_TR2
 r->ring_event=0;
#endif
 if(r->next<r->count) {
  const uint8_t *p=r->data+TR64_ROUTE_HEADER+12*r->next;
  if(tr64_be32(p)==tick) {
#if TR64_HAS_TR2
   if(sequel_schema(r->game_hash_schema)) {
    r->input.buttons=tr64_be32(p+4);
    r->ring_event=tr64_be32(p+8);
    ++r->next;
    return r->input; /* zero axes validated at open */
   }
#endif
   r->input.buttons=(uint16_t)((uint16_t)p[4]<<8 | p[5]);
   r->input.x=(int8_t)(p[6]<128 ? (int)p[6] : (int)p[6]-256);
   r->input.y=(int8_t)(p[7]<128 ? (int)p[7] : (int)p[7]-256);
   ++r->next;
  }
 }
 return r->input;
}
/* M0 policy belongs to its boot scene, not the generic residency engine. */
static void boot_unit_frame(uint32_t tick,void *context)
{
 uint32_t value;
 (void)context;
 if(tick==0 || tick==4)code_load("test");
 else if(tick==2 || tick==6)code_unload("test");
 if(code_is_loaded("test")) {
  value=code_call("test",tick);
  if(value!=156u+tick)sys_error("code test rodata mismatch");
  log_printf("code: call test tick=%lu value=%lu\n",(unsigned long)tick,(unsigned long)value);
 }
 if(tick<=6) { code_log();log_flush(); }
}
static uint32_t game_scene_hash(void)
{
#if !defined(TR64_HOST) && TR64_GAME==2
 return game_tr2_state_hash();
#else
#if TR64_HAS_TR2
 if(sequel_schema(game_route->game_hash_schema))return game_tr2_state_hash();
#endif
 return game_route->game_hash_schema==3 ? game_state_hash_schema(3) : game_state_hash();
#endif
}
#if TR64_HAS_TR2
static void game_log_tick(uint32_t tick,uint32_t hash,uint32_t elapsed)
{
 if(game_route->game_hash_schema==3)
  log_printf("game-tick: tick=%lu hash=%08lx tick_us=%lu hash_schema=3\n",
   (unsigned long)tick,(unsigned long)hash,(unsigned long)elapsed);
 else if(sequel_schema(game_route->game_hash_schema))
  log_printf("game-tick: tick=%lu hash=%08lx tick_us=%lu hash_schema=%lu\n",
   (unsigned long)tick,(unsigned long)hash,(unsigned long)elapsed,
   (unsigned long)game_route->game_hash_schema);
 else
  log_printf("game-tick: tick=%lu hash=%08lx tick_us=%lu\n",
   (unsigned long)tick,(unsigned long)hash,(unsigned long)elapsed);
}
#endif
#if defined(TR64_HOST) || TR64_GAME!=2
static int game_boundary(uint32_t tick,uint16_t input)
{
 uint32_t before=game_ticks;int result;
 if(!tr64_scene_game_boundary)return 0;
 result=tr64_scene_game_boundary(game_route,tick,input);
 if(result==0) {
  if(!game_active || !game_drawables)sys_error("scene boundary released Core without replacement");
  return 0;
 }
 if(result!=1 || game_active || game_drawables || !tr64_level_current())
  sys_error("scene boundary reload requires released and replaced Core");
 game_drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*game_drawables));
 code_loading_begin();
 if(tr64_render_game_scene_prepare)tr64_render_game_scene_prepare();
 code_loading_end();
 game_ticks=before;game_active=1;
 return 1;
}
#endif
#if !defined(TR64_HOST) && TR64_GAME==2
/* Keep the independently checked render stack path visible to the linker. */
__attribute__((noinline))
#endif
static void game_scene_render(void)
{
 tr64_game_view_t view;
 game_view_camera(&view);
 if(perf_only)perf_room=view.camera_room;
#if defined(TR64_HOST) || TR64_GAME!=2
#if TR64_HAS_TR2
 if(!sequel_schema(game_route->game_hash_schema))
#endif
 if(tr64_ui_game_frame && tr64_ui_game_frame(&view,game_drawables,TR64_GAME_DRAW_CHUNK))return;
#endif
 tr64_render_game_scene_frame(&view,game_drawables,TR64_GAME_DRAW_CHUNK);
}
static void clock_wrap_begin(void)
{
 sys_test_clock_rebase(UINT32_MAX-250000u);
 clock_wrap_start=sys_time_us();
 if((uint32_t)(clock_wrap_start-(UINT32_MAX-250000u))>=100000u)
  sys_error("clock-wrap test rebase did not activate before wrap");
 clock_wrap_active=1;
}
void tr64_scene_clock_report(void)
{
 uint32_t now,elapsed,crossed;
 if(!clock_wrap_active)return;
 now=sys_time_us();elapsed=now-clock_wrap_start;
 crossed=(uint32_t)(((uint64_t)clock_wrap_start+elapsed)>>32);
 if(!crossed)sys_error("clock-wrap test did not cross the 32-bit clock boundary");
 log_printf("clock-wrap: start=%08lx end=%08lx elapsed_us=%lu crossed=%lu\n",
  (unsigned long)clock_wrap_start,(unsigned long)now,(unsigned long)elapsed,(unsigned long)crossed);
 clock_wrap_active=0;
}
void eng_level_release(void)
{
 tr64_mem_stats_t io;
 code_level_release_begin();
 if(tr64_snd_level_stop)tr64_snd_level_stop();
 if(tr64_io_pending())sys_error("level release: I/O pending");
 tr64_mem_stats(TR64_MEM_io,&io);
 if(io.used)sys_error("level release: I/O scratch retained");
 if(!tr64_level_release)sys_error("level release: loader not linked");
 /* Nested resource/code leases fail before any reset; no planner can retain
  * level or state pointers once unit mappings are released. */
 code_level_reset();
 tr64_level_release();
 mem_level_end();
 game_active=0;game_ticks=0;game_drawables=NULL;
 clock_wrap_active=0;
 tr64_io_init(0);
 code_loading_end();
}
void tr64_scene_begin(const tr64_route_t *route)
{
#if defined(TR64_HOST) || TR64_GAME!=2
 const struct tr64_level *core;
#endif
#if TR64_HAS_TR2
 const int tr2=sequel_schema(route->game_hash_schema);
#endif
 game_active=0;game_ticks=0;
 game_drawables=NULL;
 game_route=route;
 clock_wrap_active=0;
 perf_only=&tr64_scene_test_no_hash && tr64_scene_test_no_hash;
#if TR64_HAS_TR2
 if(tr2 && (!tr64_wall_load || !game_tr2_route_update || !game_tr2_state_hash || !game_stop_info))
  sys_error("scene Tomb Raider II game hooks not linked");
#endif
#if !defined(TR64_HOST) && TR64_GAME==2
 if(route->game && !tr2)sys_error("scene %s: Tomb Raider II gameplay requires hash-schema 20, 21 or 22",route->level);
#else
#if TR64_HAS_TR2
 if(!tr2)
#endif
 if(route->game && (!game_init || !game_level_start || !game_tick || !game_state_hash ||
    !game_stop_info || !tr64_level_current || (route->place && !game_place)))
  sys_error("scene game hooks not linked");
 if(route->game && route->game_hash_schema==3 && !game_state_hash_schema)
  sys_error("scene game hash schema3 query not linked");
#endif
 if(route->game && (!game_view_camera || !game_view_room || !tr64_render_game_scene_frame))
  sys_error("scene game view hooks not linked");
 if(route->probe==TR64_PROBE_RELOAD) {
  if(!route->level || route->game)sys_error("scene reload probe requires a non-game level");
  if(!tr64_scene_load)sys_error("scene level loader not linked: %s",route->level);
  if(!tr64_level_reload_probe)sys_error("scene reload probe not linked");
  tr64_level_reload_probe(route->level);
  return;
 }
 if(route->probe==TR64_PROBE_VIDEO) {
#if !defined(TR64_HOST) && TR64_GAME==2
  sys_error("Tomb Raider II: cut scenes and video playback unavailable");
#else
  if(!route->level || route->game)sys_error("scene video probe requires a non-game name");
  if(!tr64_video_probe)sys_error("scene video probe not linked");
  tr64_video_probe(route->level,route->seed);
  return;
#endif
 }
 if(!route->level) {
  uint32_t pages;
  (void)sys_code_find("test",&pages);
  if(pages<4)sys_error("code test unit requires two page pairs");
  code_loading_begin();code_set_frame_handler(boot_unit_frame,NULL);code_loading_end();
  if(route->probe==TR64_PROBE_CLOCK_WRAP)clock_wrap_begin();
  return;
 }
 if(!tr64_scene_load)sys_error("scene level loader not linked: %s",route->level);
#if TR64_HAS_TR2
 if(tr2) {
  if(route->place && route->place_room>INT32_MAX)sys_error("Tomb Raider II: placement room range");
  const int32_t placement[]={route->place_x,route->place_y,route->place_z,route->place_angle,
                            (int32_t)route->place_room};
  game_drawables=tr64_wall_load(route->level,route->place ? placement : NULL,route->game_hash_schema);
  if(tr64_snd_level_start)tr64_snd_level_start(tr64_level_current());
  if(tr64_snd_pump)tr64_snd_pump();
  game_active=1;
 } else
#endif
 {
 mem_level_begin();
 code_loading_begin();code_set_frame_handler(NULL,NULL);
 log_printf("scene: load level %s\n",route->level); log_flush();
 tr64_scene_load(route->level);
 if(route->game) {
#if defined(TR64_HOST) || TR64_GAME!=2
  core=tr64_level_current();
  if(!core)sys_error("scene game core not ready");
  if(!game_initialized) { game_init();game_initialized=1; }
  game_level_start(core,route->level);
  if(route->place && !game_place(route->place_room,route->place_x,route->place_y,
     route->place_z,route->place_angle))sys_error("game placement: room/grid/floor mismatch");
  if(tr64_snd_level_start)tr64_snd_level_start(core);
  game_drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*game_drawables));
  if(tr64_render_game_scene_prepare)tr64_render_game_scene_prepare();
  game_active=1;
#endif
 }
 code_loading_end();
 }
 if(route->game) {
#if defined(TR64_HOST) || TR64_GAME!=2
  (void)game_boundary(0,0);
#endif
  if(perf_only)log_printf("game-tick: tick=0 hash=omitted/perf-only tick_us=0\n");
#if TR64_HAS_TR2
  else game_log_tick(0,game_scene_hash(),0);
#else
  else if(route->game_hash_schema==3)log_printf("game-tick: tick=0 hash=%08lx tick_us=0 hash_schema=3\n",(unsigned long)game_scene_hash());
  else log_printf("game-tick: tick=0 hash=%08lx tick_us=0\n",(unsigned long)game_scene_hash());
#endif
  tr64_mem_log();log_flush();
  game_scene_render();
 }
 if(route->probe==TR64_PROBE_CLOCK_WRAP)clock_wrap_begin();
}
void tr64_scene_tick(tr64_input_t input)
{
 uint32_t start,game_done,elapsed,hash,audio_done=0,diag_begin=0,diag_end=0;
 const tr64_game_stop_t *stop;
 if(!game_active) { if(tr64_snd_pump)tr64_snd_pump();return; }
 if(input.x || input.y)sys_error("game scene requires raw action masks and zero axes");
 start=sys_time_us();
#if !defined(TR64_HOST) && TR64_GAME==2
 game_tr2_route_update(input.buttons,game_route->ring_event&2047u,game_route->ring_event>>11);
 game_done=sys_time_us();elapsed=game_done-start;
 if(tr64_snd_tick)tr64_snd_tick();
 if(tr64_snd_pump)tr64_snd_pump();
#else
#if TR64_HAS_TR2
 if(sequel_schema(game_route->game_hash_schema)) {
  game_tr2_route_update(input.buttons,game_route->ring_event&2047u,game_route->ring_event>>11);
  game_done=sys_time_us();elapsed=game_done-start;
  if(tr64_snd_tick)tr64_snd_tick();
  if(tr64_snd_pump)tr64_snd_pump();
 } else
#endif
 {
  game_tick(input.buttons);
  if(game_boundary(game_ticks+1,input.buttons))game_tick(input.buttons);
  game_done=sys_time_us();elapsed=game_done-start;
  if(tr64_snd_tick)tr64_snd_tick();
  if(tr64_snd_pump)tr64_snd_pump();
 }
#endif
 if(tr64_render_scene_work_stamp)audio_done=sys_time_us();
 stop=game_stop_info();
 if(!stop)sys_error("game stop query lost active level");
 if(stop->kind!=TR64_GAME_STOP_NONE)sys_error("%s",stop->name);
 if(tr64_render_scene_work_stamp)diag_begin=sys_time_us();
 if(perf_only) {
  ++game_ticks;
  log_printf("game-tick: tick=%lu hash=omitted/perf-only tick_us=%lu\n",
   (unsigned long)game_ticks,(unsigned long)elapsed);
 } else {
  hash=game_scene_hash();++game_ticks;
#if TR64_HAS_TR2
  game_log_tick(game_ticks,hash,elapsed);
#else
  if(game_route->game_hash_schema==3)
   log_printf("game-tick: tick=%lu hash=%08lx tick_us=%lu hash_schema=3\n",(unsigned long)game_ticks,(unsigned long)hash,(unsigned long)elapsed);
  else log_printf("game-tick: tick=%lu hash=%08lx tick_us=%lu\n",(unsigned long)game_ticks,(unsigned long)hash,(unsigned long)elapsed);
#endif
 }
 log_flush();
 if(tr64_render_scene_work_stamp)diag_end=sys_time_us();
 game_scene_render();
 if(tr64_render_scene_work_stamp) {
  uint32_t completed,pacing,wall,diagnostics;
  if(!tr64_render_scene_work_stamp(&completed,&pacing))sys_error("scene-work missing completed frame");
  wall=completed-start;diagnostics=diag_end-diag_begin;
  if(pacing>wall || diagnostics>wall-pacing)sys_error("scene-work invalid completion span");
  /* Direct tick-to-GPU-completion interval, excluding actual framebuffer
   * pacing and the measured pre-render hash/log span, never a sum of phases.
   * The renderer's completion stamp precedes presentation and its logging.
   * Net pacing subtracts acquisition audio, so that audio remains in total_us.
   * audio_us covers only immediate post-tick hooks, not all mixer CPU. */
  if(perf_only)
   log_printf("scene-work: tick=%lu total_us=%lu wall_us=%lu diagnostics_us=%lu pacing_us=%lu game_us=%lu audio_us=%lu room=%lu\n",
    (unsigned long)game_ticks,(unsigned long)(wall-pacing-diagnostics),(unsigned long)wall,
    (unsigned long)diagnostics,(unsigned long)pacing,(unsigned long)elapsed,(unsigned long)(audio_done-game_done),(unsigned long)perf_room);
  else
   log_printf("scene-work: tick=%lu total_us=%lu wall_us=%lu diagnostics_us=%lu pacing_us=%lu game_us=%lu audio_us=%lu\n",
    (unsigned long)game_ticks,(unsigned long)(wall-pacing-diagnostics),(unsigned long)wall,
    (unsigned long)diagnostics,(unsigned long)pacing,(unsigned long)elapsed,(unsigned long)(audio_done-game_done));
  log_flush();
 }
}
