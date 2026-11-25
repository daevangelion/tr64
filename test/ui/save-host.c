/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine/save/save.h"
#include "tr64/code.h"
static uint8_t data[8192] __attribute__((aligned(16))),out[8192] __attribute__((aligned(16)));
static int loaded;
extern uint32_t tr64_unit_medium(uint32_t argument);
void code_load(const char *name) { assert(!strcmp(name,"medium"));loaded=1; }
uint32_t code_call(const char *name,uint32_t argument)
{ assert(loaded && !strcmp(name,"medium"));return tr64_unit_medium(argument); }
uint32_t tr64_save_level_identity(int32_t level_id) { return 0xa5000000u+(uint32_t)level_id; }
int main(int argc,char **argv)
{
 if(argc!=3) { fputs("usage: save-host-test write|read|io|corrupt SRAM-file\n",stderr);return 2; }
 assert(tr64_save_host_bind(argv[2])==TR64_SAVE_OK);tr64_save_unit_load();
 int status=tr64_save_scan();
 if(!strcmp(argv[1],"io")) { assert(status==TR64_SAVE_IO && !tr64_save_any());return 0; }
 if(!strcmp(argv[1],"corrupt")) { assert(status==TR64_SAVE_CORRUPT && !tr64_save_any());return 0; }
 int writing=!strcmp(argv[1],"write");assert(writing || !strcmp(argv[1],"read"));
 assert(status==(writing?TR64_SAVE_EMPTY:TR64_SAVE_OK));
 for(unsigned i=0;i<sizeof(data);++i)data[i]=(uint8_t)(i*17+(i>>8)*29);
 const uint32_t sizes[3]={6050,3903,381};
 for(unsigned slot=0;slot<3;++slot) {
  tr64_save_info_t info={.level_id=(int32_t)slot+2,.stats={.time=1805,.distance=0x12345678,
   .mediUsed=2,.ammoUsed=31,.kills=17,.pickups=9,.secrets=5,.titleID=151,.secretsMax=3},.identity=0xa5000002u+slot},actual;
  if(writing)assert(tr64_save_write(slot,&info,data,sizes[slot])==TR64_SAVE_OK);
  assert(tr64_save_read(slot,out,sizeof(out),&actual)==TR64_SAVE_OK);
  assert(actual.bytes==sizes[slot] && !memcmp(data,out,sizes[slot]) && actual.level_id==info.level_id);
  assert(actual.stats.time==info.stats.time && actual.stats.distance==info.stats.distance);
  assert(actual.stats.mediUsed==info.stats.mediUsed && actual.stats.ammoUsed==info.stats.ammoUsed);
  assert(actual.stats.kills==info.stats.kills && actual.stats.pickups==info.stats.pickups);
  assert(actual.stats.secrets==info.stats.secrets && actual.stats.titleID==info.stats.titleID);
  assert(actual.stats.secretsMax==info.stats.secretsMax && actual.identity==info.identity);
 }
 if(writing)assert(tr64_save_settings_write(data,32)==TR64_SAVE_OK);
 uint32_t bytes=0;assert(tr64_save_settings_read(out,sizeof(out),&bytes)==TR64_SAVE_OK);
 assert(bytes==32 && !memcmp(data,out,32));
 assert(tr64_save_any());printf("host SRAM %s PASS: 3 slots (6050/3903/381B) + settings\n",argv[1]);
 return 0;
}
