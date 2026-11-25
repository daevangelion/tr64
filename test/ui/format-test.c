/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/ui/format.h"
#include "engine/ui/ui.h"
#include "ui_string_ids.h"
int tr64_ui_text(int x,int y,const char *text,unsigned align)
{ (void)x;(void)y;(void)text;(void)align;abort(); }
int main(void)
{
 static const uint32_t ticks[]={0,29,30,1799,1800,107999,108000,UINT32_MAX};
 static const uint32_t counts[]={0,1,10,32767,UINT32_MAX};
 for(unsigned i=0;i<sizeof(ticks)/sizeof(*ticks);++i) {
  char actual[16],expected[16];uint32_t seconds=ticks[i]/30;
  if(seconds>=3600)snprintf(expected,sizeof(expected),"%u:%02u:%02u",
   (unsigned)(seconds/3600),(unsigned)(seconds/60%60),(unsigned)(seconds%60));
  else snprintf(expected,sizeof(expected),"%u:%02u",(unsigned)(seconds/60),(unsigned)(seconds%60));
  tr64_ui_time_text(actual,ticks[i]);assert(!strcmp(actual,expected));
 }
 for(unsigned i=0;i<sizeof(counts)/sizeof(*counts);++i) {
  char actual[16],expected[16];
  snprintf(expected,sizeof(expected),"%u",(unsigned)counts[i]);
  unsigned n=(unsigned)strlen(expected);
  for(unsigned c=0;c<n;++c)expected[c]=(char)(expected[c]-47);
  expected[n]=' ';expected[n+1]='$';expected[n+2]=103;expected[n+3]=0;
  tr64_ui_count_text(actual,counts[i],103);assert(!strcmp(actual,expected));
 }
 for(unsigned mask=0;mask<256;++mask) {
  tr64_game_level_stats_t stats={.time=108000,.kills=65535,.pickups=255,
   .secrets=(uint8_t)mask,.secretsMax=3,.titleID=TR64_STR_TR1_LEVEL1};
  unsigned bits=0;for(unsigned bit=0;bit<8;++bit)bits+=(mask>>bit)&1;
  char actual[192],expected[192];
  snprintf(expected,sizeof(expected),tr64_ui_string(TR64_STR_LEVEL_STATS),
   tr64_ui_string(stats.titleID),65535,255,(int)bits,3,"1:00:00");
  assert(tr64_ui_stats_text(actual,&stats) && !strcmp(actual,expected));
 }
 tr64_game_level_stats_t invalid={.titleID=TR64_UI_STRING_COUNT};char out[192];
 assert(!tr64_ui_stats_text(out,&invalid));assert(!tr64_ui_stats_text(out,NULL));
 puts("UI cold formatting PASS: source time boundaries, numeric glyph bytes, 256 secret masks and rejected title IDs");
 return 0;
}
