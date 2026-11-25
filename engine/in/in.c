/* SPDX-License-Identifier: MIT */
/* Raw pad normalization, ported from MIT ac3n64 04134cc. */
#include "tr64/in.h"
#include "engine/core/sys.h"
#include "tr64/game.h"
#include "engine/in/input.h"
static tr64_input_t normalize(tr64_pad_t p)
{
 tr64_input_t out;
 out.buttons = p.buttons;
 out.x = p.x < -80 ? -80 : p.x > 80 ? 80 : p.x;
 out.y = p.y < -80 ? -80 : p.y > 80 ? 80 : p.y;
 return out;
}
tr64_input_t tr64_input_read(void)
{
 tr64_pad_t p;
 sys_pad_read(&p,0);
 return normalize(p);
}
void tr64_input_read_pair(tr64_input_t out[2])
{
 tr64_pad_t p[2];
 sys_pad_read(&p[0],&p[1]);
 out[0]=normalize(p[0]);
 out[1]=normalize(p[1]);
}
uint16_t tr64_input_game(tr64_input_t input)
{
 uint16_t out=0,p=input.buttons;
 /* 24 raw units leaves the stick's neutral noise below the digital threshold. */
 if(input.x < -24 || (p&TR64_PAD_LEFT))out|=TR64_GAME_LEFT;
 if(input.x > 24 || (p&TR64_PAD_RIGHT))out|=TR64_GAME_RIGHT;
 if(input.y > 24 || (p&TR64_PAD_UP))out|=TR64_GAME_FORWARD;
 if(input.y < -24 || (p&TR64_PAD_DOWN))out|=TR64_GAME_BACK;
 if(p&TR64_PAD_A)out|=TR64_GAME_JUMP;
 if(p&TR64_PAD_B)out|=TR64_GAME_ACTION;
 if(p&TR64_PAD_Z)out|=TR64_GAME_WEAPON;
 if(p&TR64_PAD_R)out|=TR64_GAME_WALK;
 if(p&TR64_PAD_START)out|=TR64_GAME_SELECT;
 /* Fixed Lara STATE_STOP/STATE_STEP_LEFT/RIGHT use walk+turn for sidesteps.
  * s_checkRoll uses simultaneous IN_UP|IN_DOWN; no separate roll action. */
 if(p&TR64_PAD_CLEFT)out|=TR64_GAME_WALK|TR64_GAME_LEFT;
 if(p&TR64_PAD_CRIGHT)out|=TR64_GAME_WALK|TR64_GAME_RIGHT;
 if(p&TR64_PAD_CUP)out|=TR64_GAME_LOOK;
 if(p&TR64_PAD_CDOWN)out|=TR64_GAME_FORWARD|TR64_GAME_BACK;
 return out;
}
unsigned tr64_input_demo(tr64_input_t input,uint16_t previous_buttons)
{
 uint16_t pressed=input.buttons&~previous_buttons;
 if(!(input.buttons&TR64_PAD_L))return TR64_DEMO_NONE;
 if(pressed&TR64_PAD_START)return TR64_DEMO_RESTART;
 if(pressed&TR64_PAD_CLEFT)return TR64_DEMO_PREVIOUS;
 if(pressed&TR64_PAD_CRIGHT)return TR64_DEMO_NEXT;
 if(pressed&TR64_PAD_CUP)return TR64_DEMO_FIRST;
 if(pressed&TR64_PAD_CDOWN)return TR64_DEMO_GYM;
 return TR64_DEMO_NONE;
}
unsigned tr64_input_perf(tr64_input_t input,uint16_t previous_buttons)
{
 const unsigned held=input.buttons;
 if((held!=(TR64_PAD_L|TR64_PAD_UP) && held!=(TR64_PAD_L|TR64_PAD_DOWN)) ||
    input.x < -24 || input.x > 24 || input.y < -24 || input.y > 24)return TR64_PERF_CHORD_NONE;
 const unsigned direction=held&~TR64_PAD_L;
 if(previous_buttons&direction)return TR64_PERF_CHORD_NONE;
 return direction==TR64_PAD_DOWN?TR64_PERF_CHORD_OVERLAY:TR64_PERF_CHORD_PREVIEW;
}
unsigned tr64_input_coop(tr64_input_t input,uint16_t previous,unsigned slot,
                         int paired,uint16_t primary_pressed)
{
 const uint16_t pressed=(uint16_t)(input.buttons&~previous);
 if(input.buttons&TR64_PAD_L) {
  if(slot && (pressed&TR64_PAD_START))return paired?TR64_COOP_LEAVE:TR64_COOP_NONE;
  if(paired && (pressed&TR64_PAD_A))return TR64_COOP_WARP;
 } else if(slot && !paired && (pressed&TR64_PAD_START) && !(primary_pressed&TR64_PAD_START)) {
  return TR64_COOP_JOIN;
 }
 return TR64_COOP_NONE;
}
