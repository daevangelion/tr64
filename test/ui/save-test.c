/* SPDX-License-Identifier: MIT */
/* External-device RAM is diagnostic host storage, not emulated RDRAM. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "engine/save/internal.h"
#include "tr64/res.h"
static uint8_t bank[TR64_SAVE_BYTES],snapshot[TR64_SAVE_BYTES];
static uint8_t first[8192] __attribute__((aligned(16))),second[8192] __attribute__((aligned(16)));
static uint8_t loaded[8192] __attribute__((aligned(16)));
static int unit_loaded,writes,fail_write=-1,partial;
static uint32_t pack_identity[8]; /* what this pack says each level is, by level id */
uint32_t tr64_save_level_identity(int32_t level_id)
{ return level_id>=0 && level_id<8 ? pack_identity[level_id] : 0; }
extern uint32_t tr64_unit_medium(uint32_t argument);
void code_load(const char *name) { assert(!strcmp(name,"medium"));unit_loaded=1; }
uint32_t code_call(const char *name,uint32_t argument)
{ assert(unit_loaded && !strcmp(name,"medium"));return tr64_unit_medium(argument); }
int tr64_save_platform_init(void) { return TR64_SAVE_OK; }
int tr64_save_platform_read(uint32_t offset,void *data,uint32_t bytes)
{
 assert(data && !((uintptr_t)data&15) && !(offset&1) && !(bytes&1));
 assert(offset<=sizeof(bank) && bytes<=sizeof(bank)-offset);
 memcpy(data,bank+offset,bytes);return TR64_SAVE_OK;
}
int tr64_save_platform_write(uint32_t offset,const void *data,uint32_t bytes)
{
 assert(data && !((uintptr_t)data&15) && !(offset&1) && !(bytes&1));
 assert(offset<=sizeof(bank) && bytes<=sizeof(bank)-offset);
 if(writes++==fail_write) {
  if(partial)memcpy(bank+offset,data,bytes/2);
  return TR64_SAVE_IO;
 }
 memcpy(bank+offset,data,bytes);return TR64_SAVE_OK;
}
static void header_crc(uint32_t offset,uint32_t size)
{
 uint8_t *h=bank+offset;h[16]=h[17]=h[18]=h[19]=0;
 uint32_t crc=tr64_crc32(0,h,size);
 h[16]=crc>>24;h[17]=crc>>16;h[18]=crc>>8;h[19]=crc;
}
static unsigned saved_writes(void) { return (unsigned)writes; }
/* A complete write's transfer count, so every tear point can be enumerated. */
static void write_slot(unsigned slot,const tr64_save_info_t *info,const uint8_t *data,uint32_t bytes)
{ assert(tr64_save_write(slot,info,data,bytes)==TR64_SAVE_OK); }
static void expect_slot(unsigned slot,const uint8_t *data,uint32_t bytes)
{
 tr64_save_info_t out;
 assert(tr64_save_read(slot,loaded,sizeof(loaded),&out)==TR64_SAVE_OK && out.bytes==bytes && !memcmp(loaded,data,bytes));
}
static void expect_two_others(unsigned slot,const uint32_t size[3])
{
 for(unsigned other=0;other<TR64_SAVE_SLOTS;++other)if(other!=slot)expect_slot(other,first,size[other]);
}
int main(void)
{
 memset(bank,0xff,sizeof(bank));
 for(unsigned i=0;i<sizeof(first);++i) { first[i]=(uint8_t)(i*17+(i>>8)*29);second[i]=(uint8_t)~first[i]; }
 tr64_save_info_t info={.level_id=2,.stats={.time=1805,.distance=0x12345678,
  .mediUsed=2,.ammoUsed=31,.kills=17,.pickups=9,.secrets=5,.titleID=151,.secretsMax=3}},out;
 const uint32_t size[3]={6050,3903,381};
 assert(!tr64_save_any());tr64_save_unit_load();assert(tr64_save_scan()==TR64_SAVE_EMPTY);
 /* A device that was never written is empty, and reading it does not write it. */
 assert(tr64_save_read(0,loaded,sizeof(loaded),&out)==TR64_SAVE_EMPTY && writes==0);
 for(unsigned slot=0;slot<TR64_SAVE_SLOTS;++slot) {
  info.level_id=(int32_t)slot+2;info.identity=0xa5000000u+(uint32_t)info.level_id;pack_identity[info.level_id]=info.identity;
  write_slot(slot,&info,first,size[slot]);
  assert(tr64_save_cached(slot,&out)==TR64_SAVE_OK && out.bytes==size[slot] && out.level_id==info.level_id);
  assert(out.crc==tr64_crc32(0,first,size[slot]));
  assert(tr64_save_read(slot,loaded,sizeof(loaded),&out)==TR64_SAVE_OK && !memcmp(loaded,first,size[slot]));
 }
 /* Layout: the first save of each slot lands in the region of its own number. */
 assert(tr64_save_cache.region[0]==0 && tr64_save_cache.region[1]==1 && tr64_save_cache.region[2]==2);
 for(unsigned region=0;region<3;++region)assert(!memcmp(bank+region*TR64_SAVE_REGION_BYTES,"SG64",4));
 assert(!memcmp(bank+3*TR64_SAVE_REGION_BYTES,"\xff\xff\xff\xff",4)); /* the spare region is untouched */
 assert(tr64_save_any());memcpy(snapshot,bank,sizeof(bank));
 assert(tr64_save_write(3,&info,first,6050)==TR64_SAVE_ARGUMENT);
 assert(tr64_save_write(0,&info,first,TR64_SAVE_PAYLOAD_BYTES+1)==TR64_SAVE_SPACE);
 assert(tr64_save_write(0,&info,first,TR64_SAVE_PAYLOAD_BYTES)==TR64_SAVE_OK); /* the largest payload fits a region */
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 assert(tr64_save_read(0,loaded,6049,&out)==TR64_SAVE_SPACE);
 /* ---- Atomic overwrite: power lost at any transfer, before it or halfway through it, leaves the old
  * save of that slot and the others. First a complete overwrite, to count its transfers. */
 writes=0;write_slot(0,&info,second,3903);
 const unsigned transfers=saved_writes();
 assert(transfers>=8 && transfers<=12);
 info.level_id=2;info.identity=pack_identity[2];
 {
  unsigned old_survived=0;
  for(int half=0;half<2;++half)for(unsigned failure=0;failure<transfers;++failure) {
   memcpy(bank,snapshot,sizeof(bank));fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_OK);
   writes=0;fail_write=(int)failure;partial=half;
   assert(tr64_save_write(0,&info,second,3903)==TR64_SAVE_IO);
   fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_OK);
   expect_slot(0,first,6050);expect_two_others(0,size);++old_survived;
   if(failure==transfers-1) {
    /* Only the directory's commit word is missing: the new save is complete in the spare region. */
    assert(!memcmp(bank+3*TR64_SAVE_REGION_BYTES,"SG64",4));
    assert(tr64_crc32(0,bank+3*TR64_SAVE_REGION_BYTES+64,3903)==tr64_crc32(0,second,3903));
   }
   /* The interrupted slot can be written again, and then holds the new save. */
   assert(tr64_save_write(0,&info,second,3903)==TR64_SAVE_OK);
   expect_slot(0,second,3903);expect_slot(1,first,3903);expect_slot(2,first,381);
  }
  assert(old_survived==2*transfers);
  printf("atomic overwrite: %u tear points (%u transfers, before and halfway): the old save survived every one\n",old_survived,transfers);
 }
 /* Control: the harness detects a loss. The previous in-place write (clear the commit word, then
  * payload, header, commit word, on the slot's own region) loses the old save at its second transfer. */
 memcpy(bank,snapshot,sizeof(bank));fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_OK);
 {
  static const uint8_t zero[16] __attribute__((aligned(16)))={0};
  writes=0;fail_write=1;partial=0;
  assert(tr64_save_platform_write(0,zero,4)==TR64_SAVE_OK);
  assert(tr64_save_platform_write(64,second,3902)==TR64_SAVE_IO);
  fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_OK);
  assert(tr64_save_read(0,loaded,sizeof(loaded),&out)!=TR64_SAVE_OK); /* the old save is gone: what the tear loop above would report */
  assert(tr64_save_cached(0,&out)!=TR64_SAVE_OK);
 }
 /* A completed write: the new save, the others unchanged, the slot's old region now spare. */
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 write_slot(0,&info,second,3903);
 assert(tr64_save_scan()==TR64_SAVE_OK);expect_slot(0,second,3903);expect_slot(1,first,3903);expect_slot(2,first,381);
 assert(tr64_save_cache.region[0]==3 && tr64_save_cache.region[1]==1 && tr64_save_cache.region[2]==2);
 write_slot(0,&info,first,6050); /* back to a region of the slot's own number: regions rotate */
 assert(tr64_save_cache.region[0]==0);expect_slot(0,first,6050);
 /* ---- The first save on a never-written device: every tear point leaves an empty or a usable device. */
 {
  memset(bank,0xff,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_EMPTY);
  writes=0;write_slot(0,&info,first,6050);const unsigned first_transfers=saved_writes();
  for(int half=0;half<2;++half)for(unsigned failure=0;failure<first_transfers;++failure) {
   memset(bank,0xff,sizeof(bank));fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_EMPTY);
   writes=0;fail_write=(int)failure;partial=half;
   assert(tr64_save_write(0,&info,first,6050)==TR64_SAVE_IO);
   fail_write=-1;int status=tr64_save_scan();
   assert(status==TR64_SAVE_EMPTY || status==TR64_SAVE_OK || status==TR64_SAVE_CORRUPT);
   assert(!tr64_save_any());
   assert(tr64_save_write(0,&info,first,6050)==TR64_SAVE_OK);expect_slot(0,first,6050);
  }
 }
 /* ---- Settings: two copies, newest wins; a tear leaves the old settings. */
 {
  static uint8_t settings_a[40] __attribute__((aligned(16))),settings_b[40] __attribute__((aligned(16)));
  memset(settings_a,0xa1,sizeof(settings_a));memset(settings_b,0xb2,sizeof(settings_b));
  uint32_t got=0;
  memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
  assert(tr64_save_settings_read(loaded,sizeof(loaded),&got)==TR64_SAVE_EMPTY);
  assert(tr64_save_settings_write(settings_a,31)==TR64_SAVE_OK);
  writes=0;
  assert(tr64_save_settings_write(settings_b,32)==TR64_SAVE_OK);
  const unsigned settings_transfers=saved_writes();
  assert(settings_transfers>=3);
  unsigned old_settings=0;
  for(int half=0;half<2;++half)for(unsigned failure=0;failure<settings_transfers;++failure) {
   memcpy(bank,snapshot,sizeof(bank));fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_OK);
   assert(tr64_save_settings_write(settings_a,31)==TR64_SAVE_OK);
   writes=0;fail_write=(int)failure;partial=half;
   assert(tr64_save_settings_write(settings_b,32)==TR64_SAVE_IO);
   fail_write=-1;assert(tr64_save_scan()==TR64_SAVE_OK);
   assert(tr64_save_settings_read(loaded,sizeof(loaded),&got)==TR64_SAVE_OK && got==31 && !memcmp(loaded,settings_a,31));
   ++old_settings;
   assert(tr64_save_settings_write(settings_b,32)==TR64_SAVE_OK);
   assert(tr64_save_settings_read(loaded,sizeof(loaded),&got)==TR64_SAVE_OK && got==32 && !memcmp(loaded,settings_b,32));
  }
  assert(old_settings==2*settings_transfers);
  printf("atomic settings: %u tear points, the old settings survived every one\n",old_settings);
  /* The settings and the slots do not disturb each other. */
  assert(tr64_save_scan()==TR64_SAVE_OK);expect_slot(1,first,3903);expect_slot(2,first,381);
 }
 /* ---- Integrity: corrupt payload, header, reserved field, directory copies. */
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 bank[64+123]^=1;assert(tr64_save_read(0,loaded,sizeof(loaded),&out)==TR64_SAVE_CORRUPT);
 assert(tr64_save_cached(0,&out)==TR64_SAVE_CORRUPT);
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 bank[4]^=1;assert(tr64_save_read(0,loaded,sizeof(loaded),&out)==TR64_SAVE_CORRUPT);
 assert(tr64_save_cached(0,&out)==TR64_SAVE_CORRUPT);
 memcpy(bank,snapshot,sizeof(bank));bank[52]=1;header_crc(0,64);
 assert(tr64_save_scan()==TR64_SAVE_OK && tr64_save_cached(0,&out)==TR64_SAVE_CORRUPT);
 /* The newest directory copy is damaged: the older copy is still a complete directory (the one
  * before the last save), so the device reads as it did before that save, never as corrupt. */
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 const unsigned newest_copy=tr64_save_cache.copy,newest_sequence=tr64_save_cache.sequence;
 bank[TR64_SAVE_DIRECTORY_OFFSET+TR64_SAVE_DIRECTORY_BYTES*newest_copy+16]^=1;
 assert(tr64_save_scan()==TR64_SAVE_OK && tr64_save_cache.copy!=newest_copy && tr64_save_cache.sequence==newest_sequence-1);
 /* Both copies damaged: no slot is readable; the next save makes a directory again. */
 bank[TR64_SAVE_DIRECTORY_OFFSET+TR64_SAVE_DIRECTORY_BYTES*tr64_save_cache.copy+16]^=1;
 assert(tr64_save_scan()==TR64_SAVE_CORRUPT && !tr64_save_any());
 assert(tr64_save_read(0,loaded,sizeof(loaded),&out)==TR64_SAVE_CORRUPT);
 info.level_id=2;info.identity=pack_identity[2];
 write_slot(1,&info,second,3903);expect_slot(1,second,3903);
 assert(tr64_save_scan()==TR64_SAVE_OK && tr64_save_cached(0,&out)==TR64_SAVE_EMPTY);
 /* ---- The level identity is metadata the picker judges against this pack. */
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 for(unsigned slot=0;slot<TR64_SAVE_SLOTS;++slot) {
  assert(tr64_save_cached(slot,&out)==TR64_SAVE_OK && out.identity==0xa5000002u+slot);
  assert(tr64_save_slot_status(slot,&out)==TR64_SAVE_OK && out.level_id==(int32_t)slot+2);
 }
 assert(tr64_save_usable_any());
 pack_identity[3]^=1; /* another cook of level 3: slot 1 is intact but foreign */
 assert(tr64_save_slot_status(1,&out)==TR64_SAVE_FOREIGN && tr64_save_cached(1,&out)==TR64_SAVE_OK);
 assert(tr64_save_slot_status(0,&out)==TR64_SAVE_OK);
 pack_identity[3]^=1;pack_identity[4]=0; /* a pack without level 4 */
 assert(tr64_save_slot_status(2,&out)==TR64_SAVE_FOREIGN);
 pack_identity[4]=0xa5000004u;assert(tr64_save_slot_status(2,&out)==TR64_SAVE_OK);
 tr64_save_refuse(0); /* the game rejected its contents: foreign until written again */
 assert(tr64_save_slot_status(0,&out)==TR64_SAVE_FOREIGN);
 assert(tr64_save_scan()==TR64_SAVE_OK && tr64_save_slot_status(0,&out)==TR64_SAVE_FOREIGN);
 info.level_id=2;info.identity=pack_identity[2];
 assert(tr64_save_write(0,&info,first,6050)==TR64_SAVE_OK && tr64_save_slot_status(0,&out)==TR64_SAVE_OK);
 /* A slot written without identity (zero) is never usable. */
 info.identity=0;assert(tr64_save_write(0,&info,first,6050)==TR64_SAVE_OK);
 assert(tr64_save_slot_status(0,&out)==TR64_SAVE_FOREIGN);
 /* A slot record of another layout version (intact, valid CRC) is reported, not read. */
 memcpy(bank,snapshot,sizeof(bank));bank[5]=2;header_crc(0,64);
 assert(tr64_save_scan()==TR64_SAVE_OK && tr64_save_cached(0,&out)==TR64_SAVE_VERSION);
 assert(tr64_save_slot_status(0,&out)==TR64_SAVE_VERSION && tr64_save_read(0,loaded,sizeof(loaded),&out)==TR64_SAVE_VERSION);
 assert(tr64_save_slot_status(1,&out)==TR64_SAVE_OK);
 tr64_save_info_t other={.level_id=2,.identity=pack_identity[2]};
 assert(tr64_save_write(0,&other,first,6050)==TR64_SAVE_OK && tr64_save_slot_status(0,&out)==TR64_SAVE_OK);
 /* ---- A device written by the previous layout (version 1 directory at 6000, three 8 KiB slots): every
  * slot is reported unusable, nothing is fatal, and the first save makes the new layout over it. */
 memset(bank,0xff,sizeof(bank));
 {
  uint8_t *old=bank+TR64_SAVE_DIRECTORY_OFFSET;
  memset(old,0,128);memcpy(old,"SD64",4);old[5]=1;old[7]=128;old[11]=3;old[22]=0x80;old[25]=0x20; /* 3 slots, 32768, 8192 */
  header_crc(TR64_SAVE_DIRECTORY_OFFSET,128);
  memcpy(bank,"SG64",4);bank[5]=2;bank[7]=64;header_crc(0,64);
 }
 assert(tr64_save_scan()==TR64_SAVE_VERSION && !tr64_save_any());
 for(unsigned slot=0;slot<TR64_SAVE_SLOTS;++slot)assert(tr64_save_cached(slot,&out)==TR64_SAVE_VERSION);
 assert(tr64_save_read(0,loaded,sizeof(loaded),&out)==TR64_SAVE_VERSION);
 info.level_id=2;info.identity=pack_identity[2];
 write_slot(1,&info,first,3903);expect_slot(1,first,3903);
 assert(tr64_save_scan()==TR64_SAVE_OK && tr64_save_cached(0,&out)==TR64_SAVE_EMPTY && tr64_save_cached(1,&out)==TR64_SAVE_OK);
 /* ---- A corrupt directory on a copy of a settled device. */
 memcpy(bank,snapshot,sizeof(bank));assert(tr64_save_scan()==TR64_SAVE_OK);
 assert(tr64_save_settings_write(first,31)==TR64_SAVE_OK);uint32_t size_out=0;
 assert(tr64_save_settings_read(loaded,sizeof(loaded),&size_out)==TR64_SAVE_OK && size_out==31 && !memcmp(loaded,first,31));
 /* Both settings copies damaged: unreadable, not fatal. */
 bank[TR64_SAVE_SETTINGS_OFFSET+32]^=1;bank[TR64_SAVE_SETTINGS_OFFSET+TR64_SAVE_SETTINGS_COPY_BYTES+32]^=1;
 assert(tr64_save_settings_read(loaded,sizeof(loaded),&size_out)==TR64_SAVE_CORRUPT);
 puts("save medium PASS: 3 slots/settings in 4 regions, odd payloads, cache, CRCs, atomic overwrite and first save at every transfer both before and halfway (old save always survives; completed write is the new one), two-copy directory and settings, old layouts unusable and replaced");
 return 0;
}
