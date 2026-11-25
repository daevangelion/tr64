/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/snd/snd.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/mem.h"
#include "engine/mem/pool.h"
static mem_slots_t slots;
static uint32_t slot_bits[(TR64_BYTES_audio/16u+31u)/32u];
static void *boot_allocate(uint32_t bytes)
{
 uint32_t payload=bytes ? bytes : 1,budget=mem_budget(TR64_MEM_audio),n;
 if(budget<16 || payload>budget-16)sys_error("pool audio overflow allocation");
 n=(payload+16u+15u)/16u;
 return (uint8_t *)mem_slots_take_offset(&slots,n,16,16)+16;
}
static FILE *wav;
static tr64_snd_frame_t *buffer;
static uint32_t frames_due,frames_written,mix_total_us;
static int active;
static void le16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void le32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24); }
static void header(void)
{
 uint8_t h[44]={0};
 if(!wav)return;
 memcpy(h,"RIFF",4);le32(h+4,36+frames_written*4);memcpy(h+8,"WAVEfmt ",8);
 le32(h+16,16);le16(h+20,1);le16(h+22,2);le32(h+24,22050);
 le32(h+28,88200);le16(h+32,4);le16(h+34,16);memcpy(h+36,"data",4);
 le32(h+40,frames_written*4);
 if(fseek(wav,0,SEEK_SET) || fwrite(h,1,sizeof(h),wav)!=sizeof(h) || fseek(wav,0,SEEK_END))
  sys_error("host audio WAV header write");
}
static void close_wave(void)
{
 if(wav) { header();if(fclose(wav))sys_error("host audio WAV close");wav=NULL; }
}
void *tr64_audio_boot(uint32_t bytes)
{
 const char *path=getenv("TR64_AUDIO_WAV");void *arena;
 sys_memory_boot();mem_library_claim(TR64_MEM_audio);
 mem_slots_init(&slots,TR64_MEM_audio,16,slot_bits,sizeof(slot_bits)/sizeof(slot_bits[0]));
 /* Measured cart order:8-byte AI callback,5632-byte ring (three448-frame
  * buffers plus DMA pad),arena,cached448-frame output;16-byte headers. */
 (void)boot_allocate(8);
 (void)boot_allocate(3u*448u*4u+32u*8u);
 arena=boot_allocate(bytes);
 buffer=boot_allocate(448u*8u);
 if(path && *path) {
  wav=fopen(path,"wb+");if(!wav)sys_error("cannot open host audio WAV %s",path);
  header();if(atexit(close_wave))sys_error("host audio exit registration");
 }
 return arena;
}
void tr64_audio_active(int enabled) { active=enabled;frames_due=0; }
void tr64_audio_tick(void) { if(active)frames_due+=735; }
void tr64_audio_pump(void)
{
 while(frames_due) {
  uint32_t n=frames_due>448 ? 448 : frames_due,i;
  uint8_t *out=(uint8_t *)buffer;
  {const uint32_t t=sys_profile_ticks();tr64_snd_mix(buffer,n,22050);mix_total_us+=sys_profile_us(sys_profile_ticks()-t);}
  if(wav) {
   /* Host may be big-endian; WAV is always little-endian. */
   for(i=0;i<n*2;++i)le16(out+i*2,(uint16_t)buffer[i/2].stereo[i&1u]);
   if(fwrite(out,4,n,wav)!=n)sys_error("host audio WAV data write");
  }
  frames_written+=n;frames_due-=n;
 }
}
void tr64_audio_perf(uint32_t *total_us,uint32_t *frames) { *total_us=mix_total_us;*frames=frames_written; }
void tr64_audio_report(void)
{
 header();if(wav && fflush(wav))sys_error("host audio WAV flush");
 log_printf("audio-output: host rate=22050 frames=%lu underruns=0\n",(unsigned long)frames_written);
}
