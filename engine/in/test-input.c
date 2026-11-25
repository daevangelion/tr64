/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "engine/in/input.h"
#include "engine/core/sys.h"
#include "tr64/game.h"
#define CHECK(condition) do { if(!(condition)) { \
 fprintf(stderr,"input mapping FAIL line %d: %s\n",__LINE__,#condition);return 1; \
} } while(0)
static tr64_pad_t raw[2];
static unsigned polls;
void sys_pad_read(tr64_pad_t *pad,tr64_pad_t *second)
{
 ++polls;
 *pad=raw[0];
 if(second)*second=raw[1];
}
int main(void)
{
 const struct { uint16_t pad,game; } cases[]={
  {TR64_PAD_A,TR64_GAME_JUMP},{TR64_PAD_B,TR64_GAME_ACTION},
  {TR64_PAD_Z,TR64_GAME_WEAPON},{TR64_PAD_R,TR64_GAME_WALK},
  {TR64_PAD_UP,TR64_GAME_FORWARD},{TR64_PAD_DOWN,TR64_GAME_BACK},
  {TR64_PAD_LEFT,TR64_GAME_LEFT},{TR64_PAD_RIGHT,TR64_GAME_RIGHT},
  {TR64_PAD_CLEFT,TR64_GAME_WALK|TR64_GAME_LEFT},
  {TR64_PAD_CRIGHT,TR64_GAME_WALK|TR64_GAME_RIGHT},
  {TR64_PAD_CUP,TR64_GAME_LOOK},
  {TR64_PAD_CDOWN,TR64_GAME_FORWARD|TR64_GAME_BACK},
  {TR64_PAD_START,TR64_GAME_SELECT},
  {TR64_PAD_START|TR64_PAD_L,TR64_GAME_SELECT},{TR64_PAD_L,0}
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
  CHECK(tr64_input_game((tr64_input_t){cases[i].pad,0,0})==cases[i].game);
 CHECK(tr64_input_game((tr64_input_t){0,24,-24})==0);
 CHECK(tr64_input_game((tr64_input_t){0,-25,25})==(TR64_GAME_LEFT|TR64_GAME_FORWARD));
 CHECK(tr64_input_game((tr64_input_t){0,25,-25})==(TR64_GAME_RIGHT|TR64_GAME_BACK));
 CHECK(tr64_input_game((tr64_input_t){TR64_PAD_A|TR64_PAD_B|TR64_PAD_UP,-80,0})==
  (TR64_GAME_JUMP|TR64_GAME_ACTION|TR64_GAME_FORWARD|TR64_GAME_LEFT));
 raw[0]=(tr64_pad_t){TR64_PAD_Z,127,-128};
 tr64_input_t normalized=tr64_input_read();
 CHECK(normalized.buttons==TR64_PAD_Z && normalized.x==80 && normalized.y==-80);
 tr64_input_t pads[2];
 tr64_input_read_pair(pads);
 CHECK(polls==2 && pads[0].buttons==TR64_PAD_Z && !pads[1].buttons && !pads[1].x && !pads[1].y);
 raw[1]=(tr64_pad_t){TR64_PAD_A,-128,127};
 tr64_input_read_pair(pads);
 CHECK(polls==3 && pads[0].buttons==TR64_PAD_Z);
 CHECK(pads[1].buttons==TR64_PAD_A && pads[1].x==-80 && pads[1].y==80);
 CHECK(tr64_input_game(pads[1])==(TR64_GAME_JUMP|TR64_GAME_LEFT|TR64_GAME_FORWARD));
 const struct { uint16_t pad;unsigned command; } demo[]={
  {TR64_PAD_START,TR64_DEMO_RESTART},{TR64_PAD_CLEFT,TR64_DEMO_PREVIOUS},
  {TR64_PAD_CRIGHT,TR64_DEMO_NEXT},{TR64_PAD_CUP,TR64_DEMO_FIRST},
  {TR64_PAD_CDOWN,TR64_DEMO_GYM}
 };
 for(unsigned i=0;i<sizeof(demo)/sizeof(demo[0]);++i) {
  tr64_input_t command={TR64_PAD_L|demo[i].pad,0,0};
  CHECK(tr64_input_demo(command,TR64_PAD_L)==demo[i].command);
  CHECK(tr64_input_demo(command,command.buttons)==TR64_DEMO_NONE);
  CHECK(tr64_input_demo((tr64_input_t){demo[i].pad,0,0},0)==TR64_DEMO_NONE);
 }
 CHECK(tr64_input_demo((tr64_input_t){TR64_PAD_L|TR64_PAD_CRIGHT,0,0},
  TR64_PAD_CRIGHT)==TR64_DEMO_NONE);
 const uint16_t up=TR64_PAD_L|TR64_PAD_UP,down=TR64_PAD_L|TR64_PAD_DOWN;
 CHECK(tr64_input_perf((tr64_input_t){up,0,0},TR64_PAD_L)==TR64_PERF_CHORD_PREVIEW);
 CHECK(tr64_input_perf((tr64_input_t){down,24,-24},TR64_PAD_L)==TR64_PERF_CHORD_OVERLAY);
 CHECK(!tr64_input_perf((tr64_input_t){up,0,0},up));
 CHECK(!tr64_input_perf((tr64_input_t){down,0,0},TR64_PAD_DOWN));
 CHECK(!tr64_input_perf((tr64_input_t){TR64_PAD_UP,0,0},0));
 CHECK(!tr64_input_perf((tr64_input_t){up,25,0},TR64_PAD_L));
 CHECK(!tr64_input_perf((tr64_input_t){down,0,-25},TR64_PAD_L));
 /* Includes both directions, a held game action, demo chords and gate chords. */
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
  uint16_t other=cases[i].pad;
  if(other&~up)CHECK(!tr64_input_perf((tr64_input_t){up|other,0,0},other));
  if(other&~down)CHECK(!tr64_input_perf((tr64_input_t){down|other,0,0},other));
 }
 tr64_input_t start={TR64_PAD_START,0,0},warp={TR64_PAD_L|TR64_PAD_A,0,0};
 CHECK(tr64_input_coop(start,0,1,0,TR64_PAD_START)==TR64_COOP_NONE);
 CHECK(tr64_input_coop(start,0,1,0,0)==TR64_COOP_JOIN);
 CHECK(tr64_input_coop(start,TR64_PAD_START,1,0,0)==TR64_COOP_NONE);
 CHECK(tr64_input_coop(start,0,1,1,0)==TR64_COOP_NONE);
 CHECK(tr64_input_coop((tr64_input_t){0},0,1,0,0)==TR64_COOP_NONE);
 CHECK(tr64_input_coop(warp,0,0,0,0)==TR64_COOP_NONE);
 CHECK(tr64_input_coop(warp,0,0,1,0)==TR64_COOP_WARP);
 CHECK(tr64_input_coop(warp,TR64_PAD_A,1,1,0)==TR64_COOP_NONE);
 CHECK(tr64_input_coop((tr64_input_t){warp.buttons|TR64_PAD_START,0,0},0,1,1,0)==TR64_COOP_LEAVE);
 CHECK(tr64_input_perf((tr64_input_t){warp.buttons|TR64_PAD_DOWN,0,0},0)==TR64_PERF_CHORD_NONE);
 puts("input mapping PASS: both ports, chords and simultaneous/mirrored START");
 return 0;
}
