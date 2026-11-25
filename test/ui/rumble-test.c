/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine/core/sys.h"
#include "engine/ui/frontend.h"
#include "tr64/eng.h"

static tr64_game_settings_t settings={.controls_vibration=1};
static unsigned owner,admitted=3u,running,starts[2],stops[2];
static uint32_t now;
const tr64_game_settings_t *game_settings_view(void) { return &settings; }
unsigned game_menu_player(void) { return owner; }
uint32_t sys_time_us(void) { return now; }
void sys_error(const char *fmt,...) { (void)fmt;abort(); }
int tr64_ui_backend_rumble(unsigned slot,int active)
{
 assert(slot<TR64_GAME_PLAYERS);
 const unsigned bit=1u<<slot;
 if(active)++starts[slot];else ++stops[slot];
 if(!(admitted&bit))return 0;
 if(active)running|=bit;else running&=~bit;
 return 1;
}
int main(void)
{
 eng_game_vibrate();assert(running==1 && starts[0]==1 && !starts[1]);
 now=100000;owner=1;eng_game_vibrate();assert(running==3 && starts[1]==1);
 now=249999;tr64_ui_game_poll();assert(running==3);
 now=250000;tr64_ui_game_poll();assert(running==2 && stops[0]==1 && !stops[1]);
 now=350000;tr64_ui_game_poll();assert(!running && stops[1]==1);
 admitted=1;eng_game_vibrate();assert(!running && starts[1]==2);
 now=600000;tr64_ui_game_poll();assert(stops[1]==1); /* No absent Pak to stop. */
 admitted=3;eng_game_vibrate();owner=0;eng_game_vibrate();assert(running==3);
 tr64_ui_coop_leave();assert(running==1 && stops[1]==2);
 settings.controls_vibration=0;eng_game_vibrate();assert(!running && stops[0]==2);
 puts("UI rumble PASS: ring owner, independent expiry, absent Pak, leave and disable");
 return 0;
}
