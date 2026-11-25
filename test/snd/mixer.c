/* SPDX-License-Identifier: MIT */
/* Synthetic device services only. Links real snd, stream, CRC and engine/io. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "engine/snd/snd.h"
#include "engine/snd/format.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/res.h"
#include "tr64/mem.h"
static uint8_t ram[4u*1024u*1024u] __attribute__((aligned(4096)));
static uint8_t cart[65536] __attribute__((aligned(16)));
static uint32_t cart_size,clock_us,read_calls;
static int pending;
static tr64_game_sound_event_t event;
static uint32_t event_count;
static uint32_t ambient_control,current_tick,pan_control;
static int reapproach_ok;
static tr64_game_settings_t preferences={.audio_sfx=1,.audio_music=1};
const tr64_game_settings_t *game_settings_view(void) { return &preferences; }
void sys_error(const char *fmt,...)
{
 va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);exit(1);
}
void sys_log_write(const char *p,uint32_t n) { uint32_t i; if(fwrite(p,1,n,stdout)!=n)exit(1); for(i=0;i+34<=n;++i) if(!memcmp(p+i,"audio-ambient: sources=1 started=2",34))reapproach_ok=1; }
void sys_memory_boot(void) { if(!mem_is_booted())mem_boot_carve(ram,sizeof(ram)); }
void sys_io_frame_begin(void) { }
int sys_read_start(void *dst,uint32_t offset,uint32_t bytes)
{
 if(pending || bytes>4096 || offset>cart_size || bytes>cart_size-offset)return -1;
 memcpy(dst,cart+offset,bytes);clock_us+=(bytes*195u+1023u)/1024u;++read_calls;pending=1;return 0;
}
int sys_read_poll(void) { int p=pending;pending=0;return p; }
uint32_t sys_time_us(void) { return clock_us; }
uint32_t sys_profile_ticks(void) { return clock_us; }
uint32_t sys_profile_us(uint32_t ticks) { return ticks; }
const tr64_game_sound_event_t *game_sound_events(uint32_t *count)
{ *count=event_count;return &event; }
void game_view_camera(tr64_game_view_t *view)
{
 memset(view,0,sizeof(*view));view->camera_basis[0]=view->camera_basis[4]=view->camera_basis[8]=16384;
 if(ambient_control==1) {
  view->flipped=current_tick>=3 && current_tick<6;
  if(current_tick>=9)view->camera_eye[2]=-16384;
 } else if(ambient_control==4) {
  if(current_tick>=3 && current_tick<9)view->camera_eye[2]=-16384;
 }
 }
uint32_t game_player_mask(void) { return 1; }
void game_view_camera_player(unsigned slot,tr64_game_view_t *view)
{
 if(slot)sys_error("mixer test requested absent camera");
 game_view_camera(view);
}
int main(int argc,char **argv)
{
 FILE *f;uint32_t bytes,capacity,count=0,offset,remaining,i;uint8_t *meta;
 uint32_t mode=0,ticks=30,reads_at_expiry=0,pitch=128;int mode_control=0,pitch_control=0;
 tr64_pack_stream_block_t *index;tr64_level_t level={0};
 tr64_core_sound_sources_t source={0};tr64_core_sound_details_t details={0};
 tr64_i16_be_t map[256];
 if(argc!=2 && argc!=3)sys_error("mixer-test requires synthetic SN64 [control]");
 if(argc==3 && !strncmp(argv[2],"--mode",6)) {
  if(argv[2][6]<'0' || argv[2][6]>'3' || argv[2][7])sys_error("invalid mode control");
  mode=(uint32_t)(argv[2][6]-'0');mode_control=1;ticks=90;
 }
 if(argc==3 && !strcmp(argv[2],"--pitch115")) { pitch=115;pitch_control=1; }
 if(argc==3 && !strcmp(argv[2],"--pitch140")) { pitch=140;pitch_control=1; }
 if(argc==3 && !strcmp(argv[2],"--ambient-loop"))ambient_control=1;
 if(argc==3 && !strcmp(argv[2],"--ambient-once"))ambient_control=2;
 if(argc==3 && !strcmp(argv[2],"--ambient-settings"))ambient_control=3;
 if(argc==3 && !strcmp(argv[2],"--ambient-reapproach"))ambient_control=4;
 if(argc==3 && !strcmp(argv[2],"--pan-right"))pan_control=1;
 if(argc==3 && !strcmp(argv[2],"--pan-left"))pan_control=2;
 if(argc==3 && !strcmp(argv[2],"--pan-front"))pan_control=3;
 if(argc==3 && !strcmp(argv[2],"--pan-back"))pan_control=4;
 f=fopen(argv[1],"rb");if(!f)sys_error("synthetic cart open");
 cart_size=(uint32_t)fread(cart,1,sizeof(cart),f);if(ferror(f) || !feof(f))sys_error("synthetic cart length");fclose(f);
 tr64_io_init(cart_size);tr64_snd_boot();
 if(argc==3 && !strcmp(argv[2],"--audio-bump"))(void)tr64_mem_alloc(TR64_MEM_audio,16);
 if(argc==3 && !strcmp(argv[2],"--audio-reset"))tr64_mem_reset(TR64_MEM_audio);
 if(argc==3 && !mode_control && !pitch_control && !ambient_control && !pan_control && strcmp(argv[2],"--corrupt"))sys_error("unknown audio control");
 meta=tr64_snd_metadata_buffer(NULL,&capacity);bytes=snd_be32(cart+12);
 if(bytes>capacity || bytes>cart_size)sys_error("synthetic metadata bounds");
 memcpy(meta,cart,bytes);index=tr64_snd_index_buffer(&capacity);
 offset=0;remaining=cart_size;
 while(remaining) {
  uint32_t n=offset ? remaining>4096 ? 4096 : remaining : bytes;
  if(count>=capacity)sys_error("synthetic index overflow");
  index[count++]=(tr64_pack_stream_block_t){offset,offset,n,tr64_crc32(0,cart+offset,n)};
  offset+=n;remaining-=n;
 }
 if(argc==3 && !strcmp(argv[2],"--corrupt"))cart[cart_size-1]^=1;
 if(!tr64_snd_validate_metadata(meta,bytes,cart_size,NULL))sys_error("synthetic metadata validation");
 tr64_snd_bind(meta,bytes,index,count,cart_size);level.ready=1;
 if(ambient_control) {
  for(i=0;i<256;++i)map[i].raw=tr64_core_be16(65535);
  map[0].raw=0;source.z.raw=tr64_core_be32(1024);source.flags.raw=tr64_core_be16(128);
  details.volume.raw=tr64_core_be16(32767);details.flags.raw=tr64_core_be16(ambient_control==2 ? 4 : ambient_control==4 ? 3 : 6);
  if(ambient_control==4)level.game=2;
  level.tables[TR64_CORE_sound_sources-1]=(tr64_level_table_t){.data=(const uint8_t *)&source,.count=1,.stride=16};
  level.tables[TR64_CORE_sound_map-1]=(tr64_level_table_t){.data=(const uint8_t *)map,.count=256,.stride=2};
  level.tables[TR64_CORE_sound_details-1]=(tr64_level_table_t){.data=(const uint8_t *)&details,.count=1,.stride=8};
 }
 tr64_snd_level_start(&level);
 for(i=0;i<ticks;++i) {
  tr64_io_frame();event_count=i<(mode_control ? 3u : 2u) ? 1 : 0;
  current_tick=i;if(ambient_control)event_count=0;
  if(ambient_control==3)preferences.audio_sfx=i<3 || (i>=6 && i<9);
  event=(tr64_game_sound_event_t){.sound=0,.sample=0,.volume=64,.pitch=(int16_t)pitch,.mode=(uint8_t)mode};
  if(pan_control) {
   event.positioned=1;
   event.position[pan_control<=2 ? 0 : 2]=(pan_control==2 || pan_control==4) ? -1024 : 1024;
  }
  tr64_snd_tick();tr64_snd_pump();
  if(tr64_io_frame_bytes()>32768)sys_error("audio exceeded frame read allowance");
  if(mode_control && mode==2 && i==3)reads_at_expiry=read_calls;
  if(mode_control && mode==2 && i>3 && (read_calls!=reads_at_expiry || tr64_io_pending()))
   sys_error("unrefreshed loop continued streaming");
 }
 tr64_snd_report();log_flush();tr64_snd_level_stop();tr64_snd_level_stop();
 if(ambient_control==4 && !reapproach_ok)sys_error("ambient reapproach was not heard twice");
 if(tr64_io_pending())sys_error("audio release retained I/O");
 tr64_mem_log();log_flush();return 0;
}
