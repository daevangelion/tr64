/* SPDX-License-Identifier: MIT */
/* Original formatting implementation; observable rules from OpenLara 2333adef
 * src/fixed/level_stats.h:41-88 and inventory.h:1853-1874. */
#include <stddef.h>
#include <stdio.h>
#include "engine/ui/format.h"
#include "engine/ui/ui.h"
#include "ui_string_ids.h"
static char *number(char *out,uint32_t value,unsigned zero)
{
 char *start=out;
 do { *out++=(char)(zero+value%10);value/=10; } while(value);
 char *a=start,*b=out-1;
 while(a<b) { char c=*a;*a++=*b;*b--=c; }
 return out;
}
void tr64_ui_count_text(char out[16],uint32_t value,uint8_t suffix)
{
 char *end=number(out,value,1);
 if(suffix) { *end++=' ';*end++='$';*end++=(char)suffix; }
 *end=0;
}
void tr64_ui_time_text(char out[16],uint32_t ticks)
{
 uint32_t seconds=ticks/30u,hours=seconds/3600u,minutes=seconds/60u%60u;
 char *end=number(out,hours?hours:minutes,'0');
 *end++=':';
 if(hours) {
  *end++=(char)('0'+minutes/10);*end++=(char)('0'+minutes%10);*end++=':';
 }
 seconds%=60u;
 *end++=(char)('0'+seconds/10);*end++=(char)('0'+seconds%10);*end=0;
}
static char *append(char *out,char *end,const char *value)
{
 while(*value) { if(out==end)return NULL;*out++=*value++; }
 return out;
}
int tr64_ui_stats_text(char out[192],const tr64_game_level_stats_t *stats)
{
 const char *format=tr64_ui_string(TR64_STR_LEVEL_STATS);
 const char *title=stats?tr64_ui_string(stats->titleID):NULL;
 char time[16],digits[11],*cursor=out,*end=out+191;
 unsigned argument=0;
 out[0]=0;
 if(!title || !format)return 0;
 unsigned secrets=stats->secrets;
 secrets-=secrets>>1&0x55u;
 secrets=(secrets&0x33u)+(secrets>>2&0x33u);
 secrets=(secrets+(secrets>>4))&15u;
#if TR64_HAS_TR2
 if(stats->titleID>=TR64_STR_TR2_WALL) {
  uint32_t hours=stats->time/108000u;
  uint32_t mins=(stats->time/1800u)%60u;
  uint32_t secs=(stats->time/30u)%60u;
  uint32_t meters=stats->distance/445u;
  char dist[24];
  if(meters<1000u)
   snprintf(dist,sizeof(dist),"%lum",(unsigned long)meters);
  else
   snprintf(dist,sizeof(dist),"%lu.%02lukm",(unsigned long)(meters/1000u),(unsigned long)(meters%100u));
  int size;
  if(stats->secretsMax)
   size=snprintf(out,192,"%s@@TIME TAKEN %02lu:%02lu:%02lu@SECRETS %u of %lu@KILLS %u@AMMO USED %lu@HITS %lu@HEALTH PACKS %u.%u@DISTANCE %s",
    title,(unsigned long)hours,(unsigned long)mins,(unsigned long)secs,
    secrets,(unsigned long)stats->secretsMax,(unsigned)stats->kills,
    (unsigned long)stats->shots,(unsigned long)stats->hits,
    (unsigned)(stats->mediUsed/2u),(unsigned)((stats->mediUsed&1u)*5u),dist);
  else
   size=snprintf(out,192,"%s@@TIME TAKEN %02lu:%02lu:%02lu@KILLS %u@AMMO USED %lu@HITS %lu@HEALTH PACKS %u.%u@DISTANCE %s",
    title,(unsigned long)hours,(unsigned long)mins,(unsigned long)secs,
    (unsigned)stats->kills,(unsigned long)stats->shots,(unsigned long)stats->hits,
    (unsigned)(stats->mediUsed/2u),(unsigned)((stats->mediUsed&1u)*5u),dist);
  return size>=0 && size<192;
 }
#endif
 uint32_t values[4]={stats->kills,stats->pickups,secrets,stats->secretsMax};
 tr64_ui_time_text(time,stats->time);
 while(*format) {
  if(*format!='%') {
   if(cursor==end)return 0;
   *cursor++=*format++;continue;
  }
  ++format;
  if(*format=='%') {
   if(cursor==end)return 0;
   *cursor++=*format++;continue;
  }
  if(argument==0 || argument==5) {
   if(*format!='s')return 0;
   cursor=append(cursor,end,argument?time:title);
  } else if(argument<5) {
   if(*format!='d')return 0;
   *number(digits,values[argument-1],'0')=0;
   cursor=append(cursor,end,digits);
  } else return 0;
  if(!cursor)return 0;
  ++argument;++format;
 }
 if(argument!=6)return 0;
 *cursor=0;return 1;
}
