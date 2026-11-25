/* SPDX-License-Identifier: MIT */
/* The SRAM medium. A save is atomic: it is written whole into a region no slot
 * points to, verified, and only then does a new directory copy make it the slot's
 * save, so power lost at any point leaves the old save (docs/formats/n64-ui.md,
 * "SRAM medium, version 3"). Directory and settings are kept in two copies with a
 * sequence number; the newest valid copy wins and a write goes to the other one. */
#include <limits.h>
#include <string.h>
#include "engine/save/internal.h"
#include "tr64/res.h"
#define SLOT_MAGIC 0x53473634u
#define SLOT_VERSION 3u /* 2: three 8 KiB slots at fixed offsets; 1: no level identity at offset 48 */
#define DIRECTORY_MAGIC 0x53443634u
#define DIRECTORY_VERSION 2u /* 1: one copy, fixed slot offsets */
#define SETTINGS_MAGIC 0x53543634u
#define SETTINGS_VERSION 2u /* 1: one copy */
#define NONE 0xffu /* directory: a slot without a save */
static uint16_t get16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static uint32_t get32(const uint8_t *p) { return tr64_be32(p); }
static void put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
static void put32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n; }
static uint32_t header_crc(uint8_t *header,uint32_t bytes)
{
 uint32_t saved=get32(header+16);put32(header+16,0);
 uint32_t crc=tr64_crc32(0,header,bytes);put32(header+16,saved);return crc;
}
/* An intact header of another layout version is reported as such, never read with this field map. */
static int header_valid(uint8_t *header,uint32_t bytes,uint32_t magic,unsigned version)
{
 if(get32(header)!=magic)return TR64_SAVE_EMPTY;
 if(get16(header+6)!=bytes || get32(header+16)!=header_crc(header,bytes))return TR64_SAVE_CORRUPT;
 return get16(header+4)==version?TR64_SAVE_OK:TR64_SAVE_VERSION;
}
static int zeroes(const uint8_t *p,uint32_t bytes)
{ for(uint32_t i=0;i<bytes;++i)if(p[i])return 0;return 1; }
static int read_bytes(uint32_t offset,void *data,uint32_t bytes)
{ return tr64_save_platform_read(offset,data,bytes)?TR64_SAVE_IO:TR64_SAVE_OK; }
static int write_bytes(uint32_t offset,const void *data,uint32_t bytes)
{ return tr64_save_platform_write(offset,data,bytes)?TR64_SAVE_IO:TR64_SAVE_OK; }
static int payload_crc(uint32_t offset,uint32_t bytes,uint32_t expected)
{
 uint8_t block[256] __attribute__((aligned(16)));
 uint32_t crc=0;
 while(bytes) {
  uint32_t count=bytes>sizeof(block)?sizeof(block):bytes;
  int status=read_bytes(offset,block,(count+1)&~1u);if(status)return status;
  crc=tr64_crc32(crc,block,count);offset+=count;bytes-=count;
 }
 return crc==expected?TR64_SAVE_OK:TR64_SAVE_CORRUPT;
}
/* Commit word is invalidated BEFORE any payload write. All header fields and
 * their CRC are written while magic remains zero; the valid word is LAST.
 * A torn write can expose an invalid record, never a partially updated record
 * accepted by magic+CRC. The target is always a region or copy that holds no
 * save the player still has. */
static int write_record(uint32_t offset,uint8_t *header,uint32_t header_bytes,
                        const uint8_t *data,uint32_t bytes)
{
 uint32_t magic=get32(header);put32(header+16,header_crc(header,header_bytes));
 put32(header,0);
 int status=write_bytes(offset,header,4);if(status)return status;
 uint32_t even=bytes&~1u;
 if(even) { status=write_bytes(offset+header_bytes,data,even);if(status)return status; }
 if(bytes&1) {
  uint8_t tail[16] __attribute__((aligned(16)))={0};tail[0]=data[bytes-1];
  status=write_bytes(offset+header_bytes+even,tail,2);if(status)return status;
 }
 status=write_bytes(offset,header,header_bytes);if(status)return status;
 put32(header,magic);return write_bytes(offset,header,4);
}
static void base_header(uint8_t *header,uint32_t bytes,uint32_t magic,unsigned version)
{
 memset(header,0,bytes);put32(header,magic);put16(header+4,(uint16_t)version);put16(header+6,(uint16_t)bytes);
}
/* Two copies of a record, newest valid first: sets *copy and returns OK, else the
 * status of the pair (an old layout outranks a corrupt copy, which outranks empty). */
static int newest(const int status[2],const uint32_t sequence[2],int *copy)
{
 if(!status[0] && !status[1]) { *copy=(int32_t)(sequence[1]-sequence[0])>0;return TR64_SAVE_OK; }
 if(!status[0] || !status[1]) { *copy=status[0]?1:0;return TR64_SAVE_OK; }
 if(status[0]==TR64_SAVE_IO || status[1]==TR64_SAVE_IO)return TR64_SAVE_IO;
 if(status[0]==TR64_SAVE_VERSION || status[1]==TR64_SAVE_VERSION)return TR64_SAVE_VERSION;
 return status[0]==TR64_SAVE_CORRUPT || status[1]==TR64_SAVE_CORRUPT?TR64_SAVE_CORRUPT:TR64_SAVE_EMPTY;
}
/* ---- directory: sequence and the region of each slot ---- */
static int directory_copy(unsigned copy,uint8_t region[TR64_SAVE_SLOTS],uint32_t *sequence)
{
 uint8_t h[TR64_SAVE_DIRECTORY_BYTES] __attribute__((aligned(16)));
 int status=read_bytes(TR64_SAVE_DIRECTORY_OFFSET+copy*TR64_SAVE_DIRECTORY_BYTES,h,sizeof(h));
 if(!status)status=header_valid(h,sizeof(h),DIRECTORY_MAGIC,DIRECTORY_VERSION);
 if(status)return status;
 if(get32(h+8)!=TR64_SAVE_SLOTS || get32(h+20)!=TR64_SAVE_BYTES || get32(h+24)!=TR64_SAVE_REGION_BYTES ||
    get32(h+28)!=TR64_SAVE_REGIONS || !zeroes(h+44,84))return TR64_SAVE_CORRUPT;
 uint32_t used=0;
 for(unsigned i=0;i<TR64_SAVE_SLOTS;++i) {
  uint32_t r=get32(h+32+4*i);
  if(r==UINT32_MAX) { region[i]=NONE;continue; }
  if(r>=TR64_SAVE_REGIONS || (used>>r&1))return TR64_SAVE_CORRUPT;
  used|=1u<<r;region[i]=(uint8_t)r;
 }
 *sequence=get32(h+12);return TR64_SAVE_OK;
}
static int directory_read(void)
{
 uint8_t region[2][TR64_SAVE_SLOTS];uint32_t sequence[2]={0,0};int status[2],copy=0;
 for(unsigned i=0;i<2;++i)status[i]=directory_copy(i,region[i],&sequence[i]);
 int result=newest(status,sequence,&copy);
 if(!result) {
  memcpy(tr64_save_cache.region,region[copy],sizeof(tr64_save_cache.region));
  tr64_save_cache.sequence=sequence[copy];tr64_save_cache.copy=(unsigned)copy;
 }
 return result;
}
/* The flip: the new directory goes to the copy that is not current, and is the save's
 * commit. `fresh`: no valid directory exists, start at sequence 1 in copy 0. */
static int directory_commit(const uint8_t region[TR64_SAVE_SLOTS],int fresh)
{
 uint8_t h[TR64_SAVE_DIRECTORY_BYTES] __attribute__((aligned(16)));
 const uint32_t sequence=fresh?1:tr64_save_cache.sequence+1;
 const unsigned copy=fresh?0:!tr64_save_cache.copy;
 base_header(h,sizeof(h),DIRECTORY_MAGIC,DIRECTORY_VERSION);put32(h+8,TR64_SAVE_SLOTS);put32(h+12,sequence);
 put32(h+20,TR64_SAVE_BYTES);put32(h+24,TR64_SAVE_REGION_BYTES);put32(h+28,TR64_SAVE_REGIONS);
 for(unsigned i=0;i<TR64_SAVE_SLOTS;++i)put32(h+32+4*i,region[i]==NONE?UINT32_MAX:region[i]);
 int status=write_record(TR64_SAVE_DIRECTORY_OFFSET+copy*TR64_SAVE_DIRECTORY_BYTES,h,sizeof(h),NULL,0);
 if(!status)status=directory_read();
 if(!status && (tr64_save_cache.sequence!=sequence || tr64_save_cache.copy!=copy))status=TR64_SAVE_CORRUPT;
 return status;
}
/* ---- slots ---- */
static uint32_t region_offset(unsigned region) { return region*TR64_SAVE_REGION_BYTES; }
static int region_header(unsigned region,uint8_t h[64],tr64_save_info_t *info)
{
 int status=read_bytes(region_offset(region),h,64);if(status)return status;
 status=header_valid(h,64,SLOT_MAGIC,SLOT_VERSION);if(status)return status;
 uint32_t bytes=get32(h+8);
 if(!bytes || bytes>TR64_SAVE_PAYLOAD_BYTES || !zeroes(h+52,12))return TR64_SAVE_CORRUPT;
 memset(info,0,sizeof(*info));info->bytes=bytes;info->crc=get32(h+12);info->identity=get32(h+48);
 uint32_t level=get32(h+20);
 info->level_id=level<=INT32_MAX?(int32_t)level:-(int32_t)(~level)-1;
 info->stats.time=get32(h+24);info->stats.distance=get32(h+28);
 info->stats.mediUsed=get16(h+32);info->stats.ammoUsed=get16(h+34);info->stats.kills=get16(h+36);
 info->stats.pickups=h[38];info->stats.secrets=h[39];
 info->stats.titleID=get32(h+40);info->stats.secretsMax=get32(h+44);
 return TR64_SAVE_OK;
}
static int cache_slot(unsigned slot,int status,const tr64_save_info_t *info)
{
 tr64_save_cache.valid_slots&=~(1u<<slot);tr64_save_cache.slot_status[slot]=status;
 if(!status) { tr64_save_cache.info[slot]=*info;tr64_save_cache.valid_slots|=1u<<slot; }
 return status;
}
static int region_check(unsigned region,tr64_save_info_t *info)
{
 uint8_t h[64] __attribute__((aligned(16)));
 int status=region_header(region,h,info);
 return status?status:payload_crc(region_offset(region)+64,info->bytes,info->crc);
}
static int slot_check(unsigned slot)
{
 tr64_save_info_t info;
 if(tr64_save_cache.region[slot]==NONE)return cache_slot(slot,TR64_SAVE_EMPTY,NULL);
 int status=region_check(tr64_save_cache.region[slot],&info);
 return cache_slot(slot,status,&info);
}
static void check_slots(void)
{ for(unsigned i=0;i<TR64_SAVE_SLOTS;++i)(void)slot_check(i); }
static int scan(void)
{
 memset(&tr64_save_cache,0,sizeof(tr64_save_cache));tr64_save_cache.scanned=1;
 int status=tr64_save_platform_init();
 if(!status)status=directory_read();
 tr64_save_cache.directory_status=status;
 for(unsigned i=0;i<TR64_SAVE_SLOTS;++i)tr64_save_cache.slot_status[i]=status?status:TR64_SAVE_EMPTY;
 if(!status)check_slots();
 return status;
}
/* Writing makes a directory when there is none usable (empty, corrupt or another layout:
 * saves of another layout are unreadable by this build anyway). */
static int directory_ready(int writing)
{
 if(!tr64_save_cache.scanned)(void)scan();
 int status=tr64_save_cache.directory_status;
 if(writing && (status==TR64_SAVE_EMPTY || status==TR64_SAVE_CORRUPT || status==TR64_SAVE_VERSION)) {
  const uint8_t none[TR64_SAVE_SLOTS]={NONE,NONE,NONE};
  status=directory_commit(none,1);tr64_save_cache.directory_status=status;
  if(!status)check_slots();
 }
 return status;
}
/* After a failed write: what the device holds now, as a rescan would see it. */
static int resync(int status)
{
 int again=directory_read();
 tr64_save_cache.directory_status=again;
 if(!again)check_slots();
 return status;
}
static int slot_write(const tr64_save_call_t *call)
{
 if(call->slot>=TR64_SAVE_SLOTS || !call->input_info || !call->source ||
    !call->bytes || ((uintptr_t)call->source&15))return TR64_SAVE_ARGUMENT;
 if(call->bytes>TR64_SAVE_PAYLOAD_BYTES)return TR64_SAVE_SPACE;
 int status=directory_ready(1);if(status)return status;
 /* A region no slot points to: the slot's own region stays whole until the flip. */
 unsigned used=0,spare=0;uint8_t region[TR64_SAVE_SLOTS];
 for(unsigned i=0;i<TR64_SAVE_SLOTS;++i) {
  region[i]=tr64_save_cache.region[i];
  if(region[i]!=NONE)used|=1u<<region[i];
 }
 while(used>>spare&1)++spare;
 uint8_t h[64] __attribute__((aligned(16)));const tr64_save_info_t *info=call->input_info;
 base_header(h,sizeof(h),SLOT_MAGIC,SLOT_VERSION);put32(h+8,call->bytes);put32(h+48,info->identity);
 put32(h+12,tr64_crc32(0,call->source,call->bytes));put32(h+20,(uint32_t)info->level_id);
 put32(h+24,info->stats.time);put32(h+28,info->stats.distance);
 put16(h+32,info->stats.mediUsed);put16(h+34,info->stats.ammoUsed);put16(h+36,info->stats.kills);
 h[38]=info->stats.pickups;h[39]=info->stats.secrets;
 put32(h+40,info->stats.titleID);put32(h+44,info->stats.secretsMax);
 status=write_record(region_offset(spare),h,sizeof(h),call->source,call->bytes);
 tr64_save_info_t written;
 if(!status)status=region_check(spare,&written); /* readback before the flip */
 if(!status) { region[call->slot]=(uint8_t)spare;status=directory_commit(region,0); }
 if(status)return resync(status);
 /* The region was verified before the flip and nothing has written it since: no second payload read. */
 return cache_slot(call->slot,TR64_SAVE_OK,&written);
}
static int slot_read(const tr64_save_call_t *call)
{
 if(call->slot>=TR64_SAVE_SLOTS || !call->destination || !call->output_info ||
    ((uintptr_t)call->destination&15))return TR64_SAVE_ARGUMENT;
 int status=directory_ready(0);if(status)return status;
 if(tr64_save_cache.region[call->slot]==NONE)return cache_slot(call->slot,TR64_SAVE_EMPTY,NULL);
 const unsigned region=tr64_save_cache.region[call->slot];
 uint8_t h[64] __attribute__((aligned(16)));tr64_save_info_t info;
 status=region_header(region,h,&info);
 if(status)return cache_slot(call->slot,status,NULL);
 if(call->capacity<((info.bytes+15)&~15u))return TR64_SAVE_SPACE;
 status=read_bytes(region_offset(region)+64,call->destination,(info.bytes+1)&~1u);
 if(!status && tr64_crc32(0,call->destination,info.bytes)!=info.crc)status=TR64_SAVE_CORRUPT;
 if(status)return cache_slot(call->slot,status,NULL);
 *call->output_info=info;return cache_slot(call->slot,TR64_SAVE_OK,&info);
}
/* ---- settings: two copies, header 32 B: payload bytes @8, payload CRC @12, sequence @20 ---- */
static uint32_t settings_offset(unsigned copy) { return TR64_SAVE_SETTINGS_OFFSET+copy*TR64_SAVE_SETTINGS_COPY_BYTES; }
static int settings_copy(unsigned copy,uint8_t h[32],uint32_t *sequence)
{
 int status=read_bytes(settings_offset(copy),h,32);
 if(!status)status=header_valid(h,32,SETTINGS_MAGIC,SETTINGS_VERSION);
 if(status)return status;
 uint32_t bytes=get32(h+8);
 if(!bytes || bytes>TR64_SAVE_SETTINGS_BYTES || !zeroes(h+24,8))return TR64_SAVE_CORRUPT;
 *sequence=get32(h+20);return TR64_SAVE_OK;
}
static int settings(const tr64_save_call_t *call,int writing)
{
 if(writing) {
  if(!call->source || !call->bytes || ((uintptr_t)call->source&15))return TR64_SAVE_ARGUMENT;
  if(call->bytes>TR64_SAVE_SETTINGS_BYTES)return TR64_SAVE_SPACE;
 } else if(!call->destination || !call->output_bytes || ((uintptr_t)call->destination&15))return TR64_SAVE_ARGUMENT;
 int status=directory_ready(writing);if(status)return status;
 uint8_t h[2][32] __attribute__((aligned(16)));uint32_t sequence[2]={0,0};int state[2],copy=0;
 for(unsigned i=0;i<2;++i)state[i]=settings_copy(i,h[i],&sequence[i]);
 status=newest(state,sequence,&copy);
 if(writing) {
  if(status==TR64_SAVE_IO)return status;
  const unsigned target=status?0:!copy;
  const uint32_t next=status?1:sequence[copy]+1,crc=tr64_crc32(0,call->source,call->bytes);
  uint8_t n[32] __attribute__((aligned(16)));
  base_header(n,sizeof(n),SETTINGS_MAGIC,SETTINGS_VERSION);put32(n+8,call->bytes);put32(n+12,crc);put32(n+20,next);
  status=write_record(settings_offset(target),n,sizeof(n),call->source,call->bytes);
  if(status)return status;
  uint32_t seen=0;
  status=settings_copy(target,n,&seen);
  if(!status && seen!=next)status=TR64_SAVE_CORRUPT;
  return status?status:payload_crc(settings_offset(target)+32,call->bytes,crc);
 }
 if(status)return status;
 uint32_t bytes=get32(h[copy]+8);
 if(call->capacity<((bytes+15)&~15u))return TR64_SAVE_SPACE;
 status=read_bytes(settings_offset((unsigned)copy)+32,call->destination,(bytes+1)&~1u);
 if(status)return status;
 if(tr64_crc32(0,call->destination,bytes)!=get32(h[copy]+12))return TR64_SAVE_CORRUPT;
 *call->output_bytes=bytes;return TR64_SAVE_OK;
}
uint32_t tr64_unit_medium(uint32_t argument)
{
 const tr64_save_call_t *call=(const tr64_save_call_t *)(uintptr_t)argument;
 if(!call)return TR64_SAVE_ARGUMENT;
 switch(call->operation) {
 case SAVE_SCAN:return (uint32_t)scan();
 case SAVE_WRITE:return (uint32_t)slot_write(call);
 case SAVE_READ:return (uint32_t)slot_read(call);
 case SAVE_SETTINGS_WRITE:return (uint32_t)settings(call,1);
 case SAVE_SETTINGS_READ:return (uint32_t)settings(call,0);
 default:return TR64_SAVE_ARGUMENT;
 }
}
