/* SPDX-License-Identifier: MIT */
/* Native pack scene driver; simulation and canonical trace are in game/. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include "tr64/game.h"
#include "tr64/pack.h"
#include "game/tr2.h"
#if TR64_HAS_TR2
#include "game/internal.h"
#endif
#include "tr64/level.h"
#include "tr64/engine.h"
#include "tr64/code.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/ui/ui.h"
#include "engine/ui/frontend.h"
#include "engine/save/game.h"
#include "tr64/eng.h"
#include "engine/ui/perf.h"
#include "test/oracle/coop-input.h"
#include "build_id.h"
extern void tr64_ui_game_start(const tr64_level_t *,const char *) __attribute__((weak));
extern void tr64_ui_game_stop(void) __attribute__((weak));
extern int tr64_ui_game_frame(const tr64_game_view_t *,tr64_game_drawable_t *,uint32_t) __attribute__((weak));
extern void tr64_ui_game_tick(const tr64_game_view_t *,tr64_game_drawable_t *,uint32_t) __attribute__((weak));
extern void tr64_ui_coop_no_medipack(unsigned) __attribute__((weak));
extern void tr64_ui_coop_warped(unsigned) __attribute__((weak));
extern void tr64_ui_coop_leave(void) __attribute__((weak));
extern int tr64_save_host_bind(const char *) __attribute__((weak));
extern int tr64_save_game_load(void) __attribute__((weak));
extern unsigned tr64_save_game_selected(void) __attribute__((weak));
extern int tr64_save_cached(unsigned,tr64_save_info_t *) __attribute__((weak));
extern int tr64_ui_bind(const tr64_level_t *) __attribute__((weak));
static int ui_active;
static void ui_release(void)
{
 if(ui_active) { tr64_ui_game_stop();ui_active=0; }
}
extern void tr64_gpu_init(void) __attribute__((weak));
extern void tr64_render_game_scene_frame(const tr64_game_view_t *view,
 tr64_game_drawable_t *buffer,uint32_t capacity) __attribute__((weak));
extern void tr64_render_game_scene_coop_frame(const tr64_game_view_t *,const tr64_game_view_t *,
 tr64_game_drawable_t *,uint32_t) __attribute__((weak));
extern void tr64_render_game_scene_prepare(void) __attribute__((weak));
extern void tr64_render_scene_set_live(int live) __attribute__((weak));
extern void tr64_snd_boot(void) __attribute__((weak));
extern void tr64_snd_level_start(const tr64_level_t *) __attribute__((weak));
extern void tr64_snd_level_stop(void) __attribute__((weak));
extern void tr64_snd_tick(void) __attribute__((weak));
extern void tr64_snd_pump(void) __attribute__((weak));
extern void tr64_snd_report(void) __attribute__((weak));
/* Only the explicit save-I/O fixture provides these real RAM-slot adapters. */
extern int tr64_game_test_save(void) __attribute__((weak));
extern int tr64_game_test_load(void) __attribute__((weak));
extern int32_t tr64_game_test_saved_level(void) __attribute__((weak));
extern const uint8_t *tr64_game_test_saved_data(uint32_t *) __attribute__((weak));
/* Only test/game/directives.c provides these; never linked into shipping builds. */
extern int tr64_game_test_give(int32_t,int32_t) __attribute__((weak));
extern int tr64_game_test_item_health(int32_t,int32_t) __attribute__((weak));
extern int tr64_game_test_item_activate(int32_t,int32_t) __attribute__((weak));
extern int tr64_game_test_player_health(unsigned,int32_t) __attribute__((weak));
extern int tr64_game_test_weapon_hit(unsigned,int32_t,int32_t) __attribute__((weak));
extern int tr64_game_test_heavy(unsigned,int32_t,int32_t,int32_t) __attribute__((weak));
extern int tr64_game_test_flip(void) __attribute__((weak));
extern int tr64_game_test_pit(unsigned,int32_t,int32_t,int32_t) __attribute__((weak));
/* --game-perf: the profiler overlay on, as the cart's play loop runs it (one begin and one end around each
 * drawn frame); the clock skips a tick period per frame so the half-second windows close. Host times are
 * the host's own, not N64 evidence: this checks that the overlay draws. */
extern uint8_t tr64_perf_state __attribute__((weak));
extern void tr64_perf_begin(uint32_t) __attribute__((weak));
extern void tr64_perf_end(const tr64_perf_frame_t *) __attribute__((weak));
extern void sys_host_clock_skip(uint32_t);
static int perf_overlay,perf_open;
static void perf_open_frame(void)
{
 if(perf_overlay && !perf_open) { tr64_perf_begin(1);perf_open=1; }
}
static const char *perf_level;
static tr64_game_drawable_t *drawables;
static void require_running(void)
{
 const tr64_game_stop_t *stop=game_stop_info();
 if(!stop)sys_error("game stop query before level");
 if(stop->kind!=TR64_GAME_STOP_NONE)sys_error("%s",stop->name);
}
static void report_stop(int enabled)
{
 const tr64_game_stop_t *stop=game_stop_info();
 tr64_game_stop_t saved;
 tr64_mem_stats_t code;
 if(!enabled || !stop || stop->kind==TR64_GAME_STOP_NONE)return;
 saved=*stop;
 game_tick(0);
 if(tr64_snd_tick)tr64_snd_tick();
 if(tr64_snd_pump)tr64_snd_pump();
 if(!game_stop_info() || memcmp(&saved,game_stop_info(),sizeof(saved)))
  sys_error("game stopped tick changed its stop");
 if(tr64_snd_level_stop)tr64_snd_level_stop();
 ui_release();
 game_level_stop();
 if(game_stop_info())sys_error("game release retained state");
 tr64_mem_stats(TR64_MEM_code,&code);
 log_printf("game-stop: kind=%lu next_level_id=%ld death_ready=%lu name=%s\n",
  (unsigned long)saved.kind,(long)saved.next_level_id,
  (unsigned long)saved.death_ready,saved.name);
 log_printf("game-stop-release: code_bytes=%lu\n",(unsigned long)code.used);
 if(tr64_snd_report)tr64_snd_report();
 sys_memory_report();log_flush();sys_quit(0);
}
static uint32_t unsigned_number(const char *s)
{
 uint32_t n=0,base=10,digit;const char *p=s;
 if(p[0]=='0' && (p[1]=='x' || p[1]=='X')) { base=16;p+=2; }
 if(!*p)sys_error("game input: empty integer");
 for(;*p;++p) {
  if(*p>='0' && *p<='9')digit=*p-'0';
  else if(base==16 && *p>='a' && *p<='f')digit=*p-'a'+10;
  else if(base==16 && *p>='A' && *p<='F')digit=*p-'A'+10;
  else sys_error("game input: invalid unsigned integer %s",s);
  if(digit>=base || n>(UINT32_MAX-digit)/base)sys_error("game input: integer range");
  n=n*base+digit;
 }
 return n;
}
static int32_t signed_number(const char *s)
{
 char *end;long n;
 errno=0;n=strtol(s,&end,10);
 if(errno || !*s || *end || n<INT32_MIN || n>INT32_MAX)sys_error("game placement: signed integer");
 return (int32_t)n;
}
static void output(void *context,const char *bytes,uint32_t count)
{ if(fwrite(bytes,1,count,context)!=count)sys_error("game trace: write failed"); }
static void frame(uint32_t tick,const char *shot,uint32_t shot_tick,int coop_world)
{
 tr64_game_view_t view;
 game_view_camera(&view);
 perf_open_frame();
 if(!tr64_render_game_scene_frame)sys_error("game renderer not linked");
 if(ui_active) {
  if(!tr64_ui_game_frame(&view,drawables,TR64_GAME_DRAW_CHUNK))sys_error("game scene: UI frame unavailable");
 } else if(coop_world && (game_player_mask()&2u)) {
  tr64_game_view_t second;game_view_camera_player(1,&second);
  tr64_render_game_scene_coop_frame(&view,&second,drawables,TR64_GAME_DRAW_CHUNK);
 } else tr64_render_game_scene_frame(&view,drawables,TR64_GAME_DRAW_CHUNK);
 if(perf_overlay) {
  tr64_perf_end(&(tr64_perf_frame_t){1,0,view.camera_room,perf_level});
  sys_host_clock_skip(33333);perf_open=0;
 }
 if(shot && tick==shot_tick && sys_shot(shot)<0)sys_error("game shot: write failed");
}
static void transitionLevel(int32_t target,uint32_t reason,int traceOnly)
{
 const char *name=game_level_name(target);
 tr64_game_inventory_carry_t carry;
 int restoreUI=ui_active;
 if(!name)sys_error("game transition Core not ported: level=%ld",(long)target);
 if(!game_inventory_capture(&carry))sys_error("game transition carry failed");
 carry.transition=reason;
 ui_release();
 game_level_stop();eng_level_release();
 mem_level_begin();code_loading_begin();tr64_scene_load(name);code_loading_end();
 game_level_start_transition(tr64_level_current(),name,&carry);
 if(tr64_snd_level_start)tr64_snd_level_start(tr64_level_current());
 if(!traceOnly) {
  drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*drawables));
  if(tr64_render_game_scene_prepare)tr64_render_game_scene_prepare();
  if(restoreUI) {
   code_loading_begin();
   if(!tr64_ui_bind(tr64_level_current()))sys_error("game scene: transition UI font unavailable");
   tr64_ui_game_start(tr64_level_current(),name);ui_active=1;code_loading_end();
  }
 }
}
static void loadSaved(int traceOnly)
{
 if(ui_active) {
  tr64_save_info_t info;
  if(!tr64_save_game_selected || !tr64_save_cached || !tr64_save_game_load ||
     tr64_save_cached(tr64_save_game_selected(),&info)!=TR64_SAVE_OK)
   sys_error("game scene: selected SRAM slot unavailable");
  if(info.level_id!=game_level_id())transitionLevel(info.level_id,TR64_GAME_TRANSITION_LOAD,traceOnly);
  if(!tr64_save_game_load())sys_error("game scene: SRAM load failed");
  return;
 }
 if(!tr64_game_test_load || !tr64_game_test_saved_level)
  sys_error("game test save-I/O adapter not linked");
 int32_t target=tr64_game_test_saved_level();
 if(target!=game_level_id())transitionLevel(target,TR64_GAME_TRANSITION_LOAD,traceOnly);
 if(!tr64_game_test_load())sys_error("game test load failed");
}
static void sceneBoundary(uint32_t tick,uint32_t saveTick,uint32_t loadTick,int traceOnly)
{
 if(tick==saveTick) {
  int saved=ui_active ? eng_game_save && eng_game_save()==TR64_ENG_SAVE_OK :
   tr64_game_test_save && tr64_game_test_save();
  if(!saved)sys_error("game test save failed or save-I/O adapter not linked");
 }
 if(tick==loadTick)loadSaved(traceOnly);
}
static void transitions(const uint16_t input[TR64_GAME_PLAYERS],int traceOnly)
{
 const tr64_game_stop_t *stop;
 while((stop=game_stop_info()) && stop->kind==TR64_GAME_STOP_LEVEL_END) {
  if(stop->transition==TR64_GAME_TRANSITION_LOAD)loadSaved(traceOnly);
  else transitionLevel(stop->next_level_id,stop->transition,traceOnly);
  game_tick_players(input); /* Source startLevel/gameLoad recurses with the same input. */
 }
}
static void coop_commands(const CoopCommand *commands,unsigned count,unsigned *next,unsigned tick,int presentation)
{
 while(*next<count && commands[*next].tick==tick) {
  const CoopCommand *c=&commands[(*next)++];
  const int32_t *v=c->value;
  switch(c->kind) {
   case COOP_JOIN: if(game_player_join())sys_error("co-op join refused");break;
   case COOP_LEAVE:
   case COOP_LEAVE_RESULT: {
    const int result=game_player_leave();
    if(result!=(c->kind==COOP_LEAVE?TR64_GAME_LEAVE_OK:v[0]))
     sys_error("co-op leave returned an unexpected result");
    if(presentation && result==TR64_GAME_LEAVE_OK)tr64_ui_coop_leave();
    break;
   }
   case COOP_JOIN_REFUSED:
    if(game_player_join()==0)sys_error("co-op join unexpectedly accepted");
    break;
   case COOP_SAVE_BLOCKED:
    if(game_save_blocked()!=v[0] || (v[0] && game_save(NULL,0)))
     sys_error("co-op save preflight returned an unexpected result");
    break;
   case COOP_WARP_RESULT:
    if(game_player_warp(c->slot)!=v[0])sys_error("co-op warp returned an unexpected result");
    break;
   case COOP_FLIP:
    if(!tr64_game_test_flip || !tr64_game_test_flip())
     sys_error("co-op flip refused or test/game/directives.c not linked");
    break;
   case COOP_PIT:
    if(!tr64_game_test_pit || !tr64_game_test_pit((unsigned)v[0],v[1],v[2],v[3]))
     sys_error("co-op pit refused or test/game/directives.c not linked");
    break;
   case COOP_HEAVY:
    if(!tr64_game_test_heavy || !tr64_game_test_heavy((unsigned)v[0],v[1],v[2],v[3]))
     sys_error("co-op heavy refused or test/game/directives.c not linked");
    break;
   case COOP_WARP: {
    const int result=game_player_warp(c->slot);
    if(presentation && result==TR64_GAME_WARP_NO_MEDIPACK)tr64_ui_coop_no_medipack(c->slot);
    if(presentation && result==TR64_GAME_WARP_OK)tr64_ui_coop_warped(c->slot);
    break;
   }
   case COOP_PLACE:
    if(!game_place_player(c->slot,(uint32_t)v[0],v[1],v[2],v[3],(int16_t)v[4]))
     sys_error("co-op placement refused");
    break;
   case COOP_HEALTH:
    if(!tr64_game_test_player_health || !tr64_game_test_player_health(c->slot,v[0]))
     sys_error("co-op health refused or test/game/directives.c not linked");
    break;
   case COOP_HIT:
    if(!tr64_game_test_weapon_hit || !tr64_game_test_weapon_hit(c->slot,v[0],v[1]))
     sys_error("co-op hit refused or test/game/directives.c not linked");
    break;
  }
 }
}
static void write_saved_data(const char *path)
{
 const char *fd=!strncmp(path,"/proc/self/fd/",14) ? path+14 : NULL;
 if(!fd || !*fd || strspn(fd,"0123456789")!=strlen(fd))
  sys_error("game save output requires an inherited /proc/self/fd/N");
 uint32_t count=0;
 const uint8_t *bytes=tr64_game_test_saved_data ? tr64_game_test_saved_data(&count) : NULL;
 if(!bytes || !count)sys_error("game save output requires a saved test slot");
 FILE *out=fopen(path,"wb");
 if(!out)sys_error("game save output: open failed");
 size_t written=fwrite(bytes,1,count,out);
 if(fclose(out) || written!=count)sys_error("game save output: write failed");
}

/* Logic-only; use the same native level loader without binding a posed or
 * live renderer. This entry never calls the play, sound or save adapters. */
enum { TR2_FIXTURE_RING=2048 };
static int tr2_mask(FILE *file,uint32_t *mask,bool ring_ok)
{
 int c;
 do { c=fgetc(file); } while(c==' ' || c=='\t' || c=='\r' || c=='\n');
 if(c==EOF) {
  if(ferror(file))sys_error("Tomb Raider II: input read failed");
  return 0;
 }
 uint32_t value=0;
 if(c<'0' || c>'9')sys_error("Tomb Raider II: expected a decimal input mask");
 do {
  value=value*10+(uint32_t)(c-'0');
  if(value>TR2_INPUT_MASK)sys_error("Tomb Raider II: input mask range");
  c=fgetc(file);
 } while(c>='0' && c<='9');
 if((c!=EOF && c!=' ' && c!='\t' && c!='\r' && c!='\n') ||
    (value==TR2_FIXTURE_RING ? !ring_ok : (value & ~TR2_INPUT_MASK)))
  sys_error("Tomb Raider II: undefined control or mask separator");
 *mask=value;
 return 1;
}

static bool tr2_place_line(FILE *file,int32_t placement[5])
{
 int c=fgetc(file);
 if(c!='p') {
  if(c!=EOF)ungetc(c,file);
  return false;
 }
 char line[128];
 size_t n=0;
 do {
  c=fgetc(file);
  if(c==EOF || c==0 || n==sizeof(line)-1)
   sys_error("Tomb Raider II: invalid or overlong placement line");
  line[n++]=(char)c;
 } while(c!='\n');
 line[n]='\0';
 if(strncmp(line,"lace",4))sys_error("Tomb Raider II: expected place directive");
 char *p=line+4;
 for(unsigned i=0;i<5;++i) {
  if(*p!=' ' && *p!='\t')sys_error("Tomb Raider II: missing placement argument");
  do { ++p; } while(*p==' ' || *p=='\t');
  const char *digit=p+(*p=='-');
  if(*digit<'0' || *digit>'9')sys_error("Tomb Raider II: expected signed placement decimal");
  char *end;
  errno=0;
  long long value=strtoll(p,&end,10);
  if(errno || value<INT32_MIN || value>INT32_MAX)
   sys_error("Tomb Raider II: placement argument range");
  placement[i]=value;
  p=end;
 }
 while(*p==' ' || *p=='\t')++p;
 if(*p=='\r')++p;
 if(*p!='\n')sys_error("Tomb Raider II: trailing placement argument or byte");
 return true;
}

/* Host test hook. `pit TICK ROOM X Z STEPS` drops that sector before the
 * named tick, using the same directive as the co-op scene. */
static int tr2_pit_line(FILE *file,uint32_t *tick,int32_t at[4])
{
    int c;
    do { c=fgetc(file); } while(c==' ' || c=='\t' || c=='\r' || c=='\n');
    if(c==EOF)return 0;
    if(c!='p') { ungetc(c,file); return 0; }
    char line[128];
    size_t n=0;
    do {
        c=fgetc(file);
        if(c==EOF || c==0 || n==sizeof(line)-1)
            sys_error("Tomb Raider II: invalid pit line");
        line[n++]=(char)c;
    } while(c!='\n');
    line[n]='\0';
    if(strncmp(line,"it ",3))sys_error("Tomb Raider II: expected pit directive");
    char *p=line+2;
    long long values[5];
    for(unsigned i=0;i<5;++i) {
        while(*p==' ' || *p=='\t')++p;
        if(!*p)sys_error("Tomb Raider II: pit needs tick, room, x, z and steps");
        char *end;
        errno=0;
        values[i]=strtoll(p,&end,10);
        if(errno || end==p || values[i]<INT32_MIN || values[i]>INT32_MAX)
            sys_error("Tomb Raider II: pit argument range");
        p=end;
    }
    while(*p==' ' || *p=='\t' || *p=='\r')++p;
    if(*p && *p!='\n')sys_error("Tomb Raider II: trailing pit argument");
    if(values[0]<1 || values[1]<0 || values[4]<1 || values[4]>127)
        sys_error("Tomb Raider II: pit tick, room or steps out of range");
    *tick=(uint32_t)values[0];
    for(unsigned i=0;i<4;++i)at[i]=(int32_t)values[i+1];
    return 1;
}

typedef struct { FILE *file; uint32_t hash; } Tr2Trace;

static void tr2_output(void *context,const char *bytes,uint32_t count)
{
 Tr2Trace *stream=context;
 output(stream->file,bytes,count);
 for(uint32_t i=0;i<count;++i)
  stream->hash=(stream->hash^(uint8_t)bytes[i])*16777619u;
}

static void confirm_ammo_bundle(void)
{
 ItemObj item={0};
 int32 found=-1, i;
 extern void game_tr2_pickup_credit(const ItemObj *);
 extern unsigned game_tr2_ring_remove(unsigned);
 item.type=143;
 game_tr2_ring_remove(158);
 game_tr2_pickup_credit(&item);
 if(!game_tr2_ring_find(165))
  sys_error("Tomb Raider II: ammunition bundle was not carried");
 if(!game_tr2_ring_open(false))
  sys_error("Tomb Raider II: ammunition bundle ring did not open");
 for(i=0;i<32 && gs->inventory.state!=INV_STATE_READY;++i)
  game_tr2_ring_update(0);
 if(gs->inventory.state!=INV_STATE_READY)
  sys_error("Tomb Raider II: ammunition bundle ring did not reach ready");
 for(i=0;i<gs->inventory.itemsCount;++i)
  if(gs->inventory.itemsList[i]==165)found=i;
 if(found<0)
  sys_error("Tomb Raider II: ammunition bundle is not on the ring");
 gs->inventory.itemIndex=found;
 gs->inventory.nextIndex=found;
 game_tr2_ring_update(IN_ACTION);
 for(i=0;i<24 && gs->stop.kind==TR64_GAME_STOP_NONE &&
     gs->inventory.state!=INV_STATE_NONE;++i)
  game_tr2_ring_update(0);
 if(gs->stop.kind==TR64_GAME_STOP_NONE)
  sys_error("Tomb Raider II: ammunition bundle confirm ran inert");
}

static int tr2_scene(int argc,char **argv,unsigned schema)
{
 bool ammo_bundle=argc>5 && !strcmp(argv[argc-1],"--ammo-bundle-confirm");
 int ring_argc=ammo_bundle ? argc-1 : argc;
 if(ring_argc<5 || (ring_argc-5)%4)
     sys_error("usage: %s WALL.pack INPUT.txt TRACE.jsonl [--key-confirmation|--shotgun-selection|--medipack-selection TICK TYPE PAUSED_TICKS]... [--ammo-bundle-confirm]",argv[1]);
 bool shotgun=false, medipack=false;
 uint32_t shotTick=0;
 for(int i=5;i<ring_argc;i+=4) {
  uint32_t tick=unsigned_number(argv[i+1]),type=unsigned_number(argv[i+2]);
  if(!tick || (i>5 && tick<=unsigned_number(argv[i-3])))
   sys_error("Tomb Raider II: ring event ticks must strictly increase");
  if(!strcmp(argv[i],"--key-confirmation")) {
   if(type<197 || type>200)sys_error("Tomb Raider II: invalid key confirmation type");
  } else if(!strcmp(argv[i],"--shotgun-selection")) {
   if(shotgun)sys_error("Tomb Raider II: invalid shotgun selection fixture");
   shotgun=true;
   shotTick=tick;
   if(schema!=21 || tick<2 || type!=158 || unsigned_number(argv[i+3])!=87)
    sys_error("Tomb Raider II: invalid shotgun selection fixture");
  } else if(!strcmp(argv[i],"--medipack-selection")) {
   medipack=true;
   if(type!=171 && type!=172)sys_error("Tomb Raider II: invalid medipack selection type");
  } else sys_error("Tomb Raider II: unknown ring event");
 }
 const bool ring_ok=shotgun || medipack;
 tr64_level_t level;
 uint32_t mask;
 FILE *in=fopen(argv[3],"rb");
 if(!in)sys_error("Tomb Raider II: missing input");
 int32_t placement[5];
    bool placed=tr2_place_line(in,placement);
    uint32_t pitTick=0;
    int32_t pitAt[4]={0};
    int havePit=tr2_pit_line(in,&pitTick,pitAt);
    if(!tr2_mask(in,&mask,ring_ok))sys_error("Tomb Raider II: empty input");
 const char *level_name=schema==22 ? "boat" : "wall";
 tr64_eng_boot();code_boot();mem_level_begin();
 if(tr64_snd_boot)tr64_snd_boot(); /* Required to validate SN64; playback follows. */
 int bytes=sys_cart_open(argv[2]);
 if(bytes<=0)sys_error("Tomb Raider II: cannot open %s pack",schema==22 ? "Venice" : "Wall");
 tr64_io_init((uint32_t)bytes);tr64_io_frame();
 tr64_level_load(&level,(uint32_t)bytes,level_name);
 if(tr64_snd_level_start)tr64_snd_level_start(&level);
 tr64_io_frame();
 if(tr64_snd_pump)tr64_snd_pump();
 game_init();game_tr2_start(&level,level_name,schema);
 if(placed && !game_tr2_place(placement))sys_error("Tomb Raider II: placement outside dry live sector");
 FILE *out=fopen(argv[4],"wb");
 if(!out)sys_error("Tomb Raider II: cannot open trace output");
 Tr2Trace stream={out,2166136261u};
 game_trace_tr2(tr2_output,&stream);
 uint32_t tick=0;
 int event=5;
 bool inventory_marker=false;
 do {
        ++tick;
        if(havePit && tick==pitTick) {
            if(!tr64_game_test_pit || !tr64_game_test_pit((unsigned)pitAt[0],pitAt[1],pitAt[2],pitAt[3]))
                sys_error("Tomb Raider II: pit refused or test/game/directives.c not linked");
        }
        unsigned command=0;
        uint32_t pause=0;
        if(ammo_bundle && tick==1)confirm_ammo_bundle();
        if(event<ring_argc && tick==unsigned_number(argv[event+1])) {
            command=TR2_RING_EVENT|unsigned_number(argv[event+2]);
            if(!strcmp(argv[event],"--key-confirmation"))command|=TR2_RING_EVENT_KEY;
            pause=unsigned_number(argv[event+3]);
            if(!strcmp(argv[event],"--shotgun-selection")) {
                if(!inventory_marker)sys_error("Tomb Raider II: shotgun menu marker mismatch");
                /* The replay skips the native menu, not its measured clock and
                 * presentation advances. The live request does neither. */
                for(unsigned i=0;i<91;++i)(void)rand_draw();
            } else if(!strcmp(argv[event],"--key-confirmation")) {
                /* tr2-oracle-trace: this fixture's menu interval is the extra
                 * presentation advances, besides the update's own draws. */
                for(unsigned i=0;i<pause;++i)(void)rand_draw();
            } else if(!strcmp(argv[event],"--medipack-selection")) {
                /* Q-CARTUI-01: 88 then 82 advances across the two sample intervals. */
                unsigned draws=tick==169 && unsigned_number(argv[event+2])==171 ? 88u :
                               tick==191 && unsigned_number(argv[event+2])==172 ? 82u : 0u;
                if(!draws && (tick==169 || tick==191))
                    sys_error("Tomb Raider II: invalid medipack selection fixture");
                for(unsigned i=0;i<draws;++i)(void)rand_draw();
            }
            event+=4;
        }
        if(shotgun && (mask==TR2_FIXTURE_RING)!=(tick+1==shotTick))
            sys_error("Tomb Raider II: shotgun menu marker mismatch");
        if(mask==TR64_GAME_SELECT &&
           (command || event>=ring_argc || tick+1!=unsigned_number(argv[event+1]) ||
            !strcmp(argv[event],"--key-confirmation")))
            sys_error("Tomb Raider II: inventory marker requires next-tick main-ring selection");
        inventory_marker=mask==TR64_GAME_SELECT;
        game_tr2_route_update(mask,command,pause);
        tr64_io_frame();
        if(tr64_snd_tick)tr64_snd_tick();
        if(tr64_snd_pump)tr64_snd_pump();
        const tr64_game_stop_t *stop=game_stop_info();
        const bool finished=stop && stop->kind==TR64_GAME_STOP_LEVEL_END;
        if(!finished)require_running();
        game_trace_tr2(tr2_output,&stream);
        if(finished) {
            if(tr2_mask(in,&mask,ring_ok))sys_error("%s",stop->name);
            printf("tr2 boundary: %s\n",stop->name);
            break;
        }
    } while(tr2_mask(in,&mask,ring_ok));
    if(event<ring_argc)sys_error("Tomb Raider II: ring event lies beyond the last input");
    if(shotgun && tick<shotTick)sys_error("Tomb Raider II: shotgun menu did not resume");
 game_trace_tr2_end(output,out,stream.hash);
 if(fclose(in) || fclose(out))sys_error("Tomb Raider II: close failed");
 game_tr2_report();sys_memory_report();log_flush();
 sys_quit(0);
}

typedef struct { char text[320];uint32_t used; } PrefixCapture;
static void prefix_capture(void *context,const char *bytes,uint32_t count)
{
 PrefixCapture *c=context;
 const uint32_t room=(uint32_t)sizeof(c->text)-c->used;
 if(count>room)count=room;
 if(count) { memcpy(c->text+c->used,bytes,count);c->used+=count; }
}
static void check_trace_prefix(uint32_t tick,int negative)
{
 static const uint32_t caps[]={0,1,16,239,299};
 PrefixCapture full={{0},0};
 game_trace(prefix_capture,&full);
 char full_str[321];
 memcpy(full_str,full.text,full.used);full_str[full.used]=0;
 const int lara_null=strstr(full_str,",\"lara\":null")!=NULL;
 for(unsigned k=0;k<sizeof(caps)/sizeof(caps[0]);++k) {
  unsigned char buf[336];
  const uint32_t cap=caps[k];
  memset(buf,0xA5,sizeof(buf));
  const uint32_t n=game_trace_prefix((char *)buf,cap);
  if(n>cap)sys_error("trace-prefix overflow tick=%lu cap=%lu n=%lu",(unsigned long)tick,(unsigned long)cap,(unsigned long)n);
  for(uint32_t j=cap;j<sizeof(buf);++j)
   if(buf[j]!=0xA5)sys_error("trace-prefix sentinel overwritten tick=%lu cap=%lu at=%lu",(unsigned long)tick,(unsigned long)cap,(unsigned long)j);
  if(cap==0) {
   if(n!=0)sys_error("trace-prefix cap=0 wrote %lu",(unsigned long)n);
   continue;
  }
  if(negative && cap==239 && n>0)buf[n/2]^=1u;
  if(n>full.used || memcmp(buf,full.text,n)!=0)
   sys_error("trace-prefix mismatch tick=%lu cap=%lu n=%lu",(unsigned long)tick,(unsigned long)cap,(unsigned long)n);
  if(!lara_null) {
   if(cap<=239 && n!=cap)
    sys_error("trace-prefix short Lara prefix tick=%lu cap=%lu n=%lu",(unsigned long)tick,(unsigned long)cap,(unsigned long)n);
  } else if(cap==239) {
   if(n<12 || memcmp(buf+n-12,",\"lara\":null",12)!=0)
    sys_error("trace-prefix malformed lara:null tick=%lu n=%lu",(unsigned long)tick,(unsigned long)n);
  }
 }
}

int tr64_game_host_scene(int argc,char **argv)
{
 const char *input=NULL,*level=NULL,*trace=NULL,*shot=NULL,*save_device=NULL;
 const char *save_out=NULL;
 uint32_t ticks=UINT32_MAX,count=0,i,placement=0,shot_tick=UINT32_MAX;
 uint16_t masks[4096][TR64_GAME_PLAYERS];
 CoopCommand commands[COOP_COMMAND_CAPACITY];
 unsigned commandCount=0,nextCommand=0;
 int first=1,coop=0;
 uint32_t schema=2,saveTick=UINT32_MAX,loadTick=UINT32_MAX;
 int32_t start[5]={0},setup[64][3];uint32_t setups=0;char word[128];FILE *in,*out=NULL;
 tr64_mem_stats_t state;
 int trace_only=0,stop_report=0,show_ui=0,live_render=0,coop_world=0,check_prefix=0;
 if(argc>1 && !strcmp(argv[1],"--game-tr2"))return tr2_scene(argc,argv,20);
 if(argc>1 && !strcmp(argv[1],"--game-tr2-schema21"))return tr2_scene(argc,argv,21);
 if(argc>1 && !strcmp(argv[1],"--game-tr2-schema22"))return tr2_scene(argc,argv,22);
 for(i=1;i<(uint32_t)argc;++i)if(!strcmp(argv[i],"--game-input"))break;
 if(i==(uint32_t)argc)return 0;
 for(i=1;i<(uint32_t)argc;++i) {
  if(!strcmp(argv[i],"--game-input") && i+1<(uint32_t)argc)input=argv[++i];
  else if(!strcmp(argv[i],"--game-level") && i+1<(uint32_t)argc)level=argv[++i];
  else if(!strcmp(argv[i],"--game-trace") && i+1<(uint32_t)argc)trace=argv[++i];
  else if(!strcmp(argv[i],"--game-ticks") && i+1<(uint32_t)argc)ticks=unsigned_number(argv[++i]);
  else if(!strcmp(argv[i],"--game-shot") && i+1<(uint32_t)argc)shot=argv[++i];
  else if(!strcmp(argv[i],"--game-shot-tick") && i+1<(uint32_t)argc)shot_tick=unsigned_number(argv[++i]);
  else if(!strcmp(argv[i],"--game-ui"))show_ui=1;
  else if(TR64_TEST && !strcmp(argv[i],"--game-coop-world"))coop_world=1;
  else if(!strcmp(argv[i],"--game-save-device") && i+1<(uint32_t)argc)save_device=argv[++i];
  else if(!strcmp(argv[i],"--game-live"))live_render=1;
  else if(!strcmp(argv[i],"--game-perf") && tr64_perf_begin)perf_overlay=1;
  else if(!strcmp(argv[i],"--game-trace-only"))trace_only=1;
  else if(!strcmp(argv[i],"--game-check-prefix"))check_prefix=1;
  else if(!strcmp(argv[i],"--game-stop-report"))stop_report=1;
  else if(!strcmp(argv[i],"--game-trace-schema") && i+1<(uint32_t)argc)schema=unsigned_number(argv[++i]);
  else if(!strcmp(argv[i],"--game-save-tick") && i+1<(uint32_t)argc)saveTick=unsigned_number(argv[++i]);
  else if(!strcmp(argv[i],"--game-load-tick") && i+1<(uint32_t)argc)loadTick=unsigned_number(argv[++i]);
  else if(!strcmp(argv[i],"--game-save-out") && i+1<(uint32_t)argc)save_out=argv[++i];
  else sys_error("usage: --game-input masks --game-level name --game-ticks N [--game-trace path]");
 }
 perf_level=level;
 if(perf_overlay && (!show_ui || trace_only))sys_error("game scene: --game-perf needs --game-ui");
 if(perf_overlay)tr64_perf_state=2; /* on from the first frame, as after the chord */
 const int coop_replay=TR64_TEST && schema==5;
 if(coop_world && (!coop_replay || show_ui || trace_only || !tr64_render_game_scene_coop_frame))
  sys_error("game scene: --game-coop-world requires a rendered schema-5 scene without UI");
 if(coop_replay && show_ui && (!tr64_ui_game_tick || !tr64_ui_coop_leave || !tr64_ui_coop_no_medipack || !tr64_ui_coop_warped))
  sys_error("game scene: schema-5 UI hooks unavailable");
 if(!input || !level || ticks>(coop_replay || !strncmp(level,"cut",3) || !strcmp(level,"level3b") || !strcmp(level,"level7b") ||
    !strcmp(level,"level10a") || !strcmp(level,"level10b") || !strcmp(level,"level4") || !strcmp(level,"level8b") ? 4096u : 600u)) {
  if(coop_replay)sys_error("game scene: missing input/level/ticks or too many ticks (schema 5 limit 4096)");
  sys_error("game scene: missing input/level/ticks or too many ticks (600; a cutscene level, the level before one, LEVEL4 and LEVEL8B 4096)");
 }
 if(schema<2 || schema>5)sys_error("game trace schema must be 2, 3, 4 or 5");
 if((saveTick!=UINT32_MAX && saveTick>ticks) ||
    (loadTick!=UINT32_MAX && loadTick>ticks))sys_error("game save/load tick outside scene");
 if(shot_tick==UINT32_MAX)shot_tick=ticks;
 if(shot_tick>ticks)sys_error("game shot: tick outside scene");
 if(trace_only && shot)sys_error("game scene: trace-only cannot capture a picture");
 if(trace_only && show_ui)sys_error("game scene: trace-only cannot draw UI");
 if(save_device && (!show_ui || !tr64_save_host_bind || tr64_save_host_bind(save_device)!=TR64_SAVE_OK))
  sys_error("game scene: save device requires linked UI backend and --game-ui");
 in=fopen(input,"rb");if(!in)sys_error("game input: cannot open %s",input);
 while(fscanf(in,"%127s",word)==1) {
  if(first && !strcmp(word,"coop")) {
   if(fscanf(in,"%127s",word)!=1 || strcmp(word,"1"))sys_error("co-op input version must be 1");
   first=0;coop=1;continue;
  }
  first=0;
  if(coop) {
   CoopCommand command={0};
   int parsed=coop_read_command(in,word,&command,unsigned_number,signed_number);
   if(parsed) {
    if(parsed<0 || count || commandCount==COOP_COMMAND_CAPACITY || command.tick>ticks ||
       (commandCount && command.tick<commands[commandCount-1].tick))
     sys_error("co-op commands must be valid and ordered before all masks");
    commands[commandCount++]=command;continue;
   }
  }
  if(!strcmp(word,"start")) {
   if(placement || count)sys_error("game placement: directive must be first");
   for(i=0;i<5;++i) { if(fscanf(in,"%127s",word)!=1)sys_error("game placement: needs five fields");start[i]=signed_number(word); }
   if(start[0]<0 || start[4]<INT16_MIN || start[4]>INT16_MAX)sys_error("game placement: room/angle range");
   placement=1;
  } else if(!strcmp(word,"give") || !strcmp(word,"item") || !strcmp(word,"activate")) {
   /* setup[n]={kind(1 give, 2 item, 3 activate), a, b}, applied after placement, in file order. */
   int kind=word[0]=='g'?1:word[0]=='i'?2:3;
   if(count || setups>=64)sys_error("game setup: directive must precede all masks (at most 64)");
   setup[setups][0]=kind;
   for(i=1;i<3;++i) { if(fscanf(in,"%127s",word)!=1)sys_error("game setup: needs two fields");setup[setups][i]=signed_number(word); }
   ++setups;
  } else {
   uint32_t mask=unsigned_number(word);
   if(mask&~(2u|4u|8u|16u|32u|64u|128u|256u|512u|2048u))sys_error("game input: undefined bits or START");
   if(count>=ticks)sys_error("game input: too many masks");
   masks[count][0]=(uint16_t)mask;
   masks[count][1]=0;
   if(coop) {
    if(fscanf(in,"%127s",word)!=1)sys_error("co-op input missing second mask");
    mask=unsigned_number(word);
    if(mask&~0xbfeu)sys_error("co-op input: undefined bits or START");
    masks[count][1]=(uint16_t)mask;
   }
   ++count;
  }
 }
 if(ferror(in) || fclose(in))sys_error("game input: read failed");
 if(count!=ticks)sys_error("game input: expected %lu masks, got %lu",(unsigned long)ticks,(unsigned long)count);
 if(trace) { out=fopen(trace,"wb");if(!out)sys_error("game trace: cannot open %s",trace); }
 tr64_eng_boot();tr64_io_init(0);code_boot();
 if(live_render) {
  if(!tr64_render_scene_set_live)sys_error("game scene: live renderer unavailable");
  tr64_render_scene_set_live(1);
 }
 if(tr64_gpu_init)tr64_gpu_init();
 if(tr64_snd_boot)tr64_snd_boot();
 if(trace_only && tr64_render_scene_set_live)tr64_render_scene_set_live(1);
 mem_level_begin();code_loading_begin();tr64_scene_load(level);code_loading_end();
 game_init();game_level_start(tr64_level_current(),level);
 if(placement && !game_place((uint32_t)start[0],start[1],start[2],start[3],(int16_t)start[4]))sys_error("game placement: room/grid/floor mismatch");
 for(i=0;i<setups;++i) {
  int ok=setup[i][0]==1 ? tr64_game_test_give && tr64_game_test_give(setup[i][1],setup[i][2]) :
   setup[i][0]==2 ? tr64_game_test_item_health && tr64_game_test_item_health(setup[i][1],setup[i][2]) :
   tr64_game_test_item_activate && tr64_game_test_item_activate(setup[i][1],setup[i][2]);
  if(!ok)sys_error("game setup: give/item/activate refused or test/game/directives.c not linked");
 }
 require_running();
 if(tr64_snd_level_start)tr64_snd_level_start(tr64_level_current());
 if(!trace_only) {
  drawables=tr64_mem_alloc(TR64_MEM_state,TR64_GAME_DRAW_CHUNK*sizeof(*drawables));
  if(tr64_render_game_scene_prepare)tr64_render_game_scene_prepare();
  if(show_ui) {
   if(!tr64_ui_bind || !tr64_ui_game_start || !tr64_ui_game_stop || !tr64_ui_game_frame)
    sys_error("game scene: full UI unavailable");
   code_loading_begin();
   if(!tr64_ui_bind(tr64_level_current()))sys_error("game scene: UI font unavailable");
   tr64_ui_game_start(tr64_level_current(),level);ui_active=1;code_loading_end();
  }
 }
 coop_commands(commands,commandCount,&nextCommand,0,coop_replay && ui_active);
 sceneBoundary(0,saveTick,loadTick,trace_only);
 const int prefix_neg=check_prefix && getenv("TR64_TRACE_PREFIX_NEG") && !strcmp(getenv("TR64_TRACE_PREFIX_NEG"),"1");
 if(check_prefix)check_trace_prefix(0,prefix_neg);
 if(out)game_trace_schema(schema,output,out);
 log_printf("game-tick: tick=0 hash=%08lx tick_us=0\n",(unsigned long)game_state_hash());log_flush();
 if(!trace_only)frame(0,shot,shot_tick,coop_world);
 for(i=0;i<ticks;++i) {
  coop_commands(commands,commandCount,&nextCommand,i+1,coop_replay && ui_active);
  uint32_t begin=sys_time_us(),elapsed;
  perf_open_frame();
  game_tick_players(masks[i]);elapsed=sys_time_us()-begin;
  transitions(masks[i],trace_only);
  if(tr64_snd_tick)tr64_snd_tick();
  if(tr64_snd_pump)tr64_snd_pump();
  report_stop(stop_report);
  require_running();
  sceneBoundary(i+1,saveTick,loadTick,trace_only);
  if(coop_replay && ui_active) {
   tr64_game_view_t view;game_view_camera(&view);
   tr64_ui_game_tick(&view,drawables,TR64_GAME_DRAW_CHUNK);
  }
  if(check_prefix)check_trace_prefix(i+1,prefix_neg);
  if(out)game_trace_schema(schema,output,out);
  log_printf("game-tick: tick=%lu hash=%08lx tick_us=%lu\n",(unsigned long)(i+1),(unsigned long)game_state_hash(),(unsigned long)elapsed);log_flush();
  if(!trace_only)frame(i+1,shot,shot_tick,coop_world);
 }
 if(save_out)write_saved_data(save_out);
 if(out && fclose(out))sys_error("game trace: close failed");
 tr64_mem_stats(TR64_MEM_state,&state);
 log_printf("game: ticks=%lu state=%08lx state_pool_used=%lu state_pool_peak=%lu\n",(unsigned long)ticks,(unsigned long)game_state_hash(),(unsigned long)state.used,(unsigned long)state.peak);
 if(tr64_snd_report)tr64_snd_report();
 ui_release();
 sys_memory_report();log_flush();sys_quit(0);
}
