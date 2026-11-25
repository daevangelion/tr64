/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/res.h"
#include "tr64/code.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "engine/snd/snd.h"
#include "engine/mem/pool.h"
#include "engine/save/internal.h"

static uint32_t catalog_bytes;
uint32_t __wrap_tr64_level_pack_bind_streams(void) { return catalog_bytes; }
const char *game_level_name(int32_t id) { return id==0 ? "level1" : NULL; }
static void save_picker(uint32_t bytes,uint32_t identity,int legacy)
{
 const char *const names[]={"level1"};
 catalog_bytes=bytes;
 tr64_save_identity_bind(names,1);
 if(!identity)sys_error("save picker: expected nonzero fixture identity");
 tr64_save_cache.scanned=1;
 tr64_save_cache.slot_status[0]=TR64_SAVE_OK;
 tr64_save_cache.info[0]=(tr64_save_info_t){.level_id=0,.identity=identity};
 if(tr64_save_slot_status(0,NULL)!=(legacy ? TR64_SAVE_OK : TR64_SAVE_FOREIGN))
  sys_error("save picker: slot admission");
 log_printf("save picker: %s\n",legacy ? "TR1 offered" : "TR2 refused");
}
static void sound_lifecycle(uint32_t bytes)
{
 tr64_level_t level, sequel={.game=2};
 uint32_t capacity;
 uint8_t expected[32],*cache;
 tr64_level_load(&level,bytes,"legacy");
 tr64_snd_level_start(&level);tr64_snd_level_stop();
 cache=tr64_snd_metadata_buffer(&sequel,&capacity);
 if(capacity<sizeof(expected))sys_error("TR2 effects cache test capacity");
 memcpy(expected,cache,sizeof(expected));
 for(unsigned i=0;i<sizeof(expected);++i)
  if(expected[i]!=0x5a)sys_error("TR1 effects cache was not preloaded");
 mem_level_end();mem_level_begin();
 tr64_level_load(&level,bytes,"sequel");
 if(level.sound_metadata!=cache || memcmp(cache,"SN64",4) || !memcmp(cache,expected,sizeof(expected)))
  sys_error("TR2 metadata did not overwrite the idle effects cache");
 /* Real start/stop is forbidden for TR2; level release just stops/drains. */
 tr64_snd_level_stop();
 mem_level_end();mem_level_begin();
 tr64_level_load(&level,bytes,"legacy");
 tr64_snd_level_start(&level);tr64_snd_level_stop();
 if(memcmp(cache,expected,sizeof(expected)))sys_error("TR1 effects cache was not restored");
 log_printf("audio: TR1 -> TR2 idle cache borrow -> TR1 restored resident sample bytes\n");
}
void tr64_catalog_test(uint32_t,const char *,const char *);
void tr64_catalog_bind_test(uint32_t,const char *);
static void pump(void)
{ if(tr64_io_frame_bytes()>=TR64_IO_FRAME)tr64_io_frame(); tr64_io_pump(); }
int main(int argc,char **argv)
{
 tr64_level_t level; int bytes; const char *mode=argc>3 ? argv[3] : "retail";
 if(argc<3 || argc>4)sys_error("core-test: PACK LEVEL [values|small|opaque|repeat]");
 sys_memory_boot(); tr64_snd_boot(); code_boot();
 if(strncmp(mode,"catalog-only",12) && strncmp(mode,"bind",4))mem_level_begin();
 if(!strcmp(mode,"small"))mem_limit(TR64_MEM_level,64);
 bytes=sys_cart_open(argv[1]); if(bytes<0)sys_error("core-test: cart open");
 tr64_io_init((uint32_t)bytes); tr64_io_frame();
 if(!strcmp(mode,"save-picker") || !strcmp(mode,"save-picker-tr2"))
  save_picker((uint32_t)bytes,(uint32_t)strtoul(argv[2],NULL,16),!strcmp(mode,"save-picker"));
 else if(!strcmp(mode,"sound-lifecycle"))sound_lifecycle((uint32_t)bytes);
 else if(!strcmp(mode,"row-switch")) {
  const char *const names[]={"legacy","sequel","legacy"};
  for(unsigned i=0;i<3;++i) {
   tr64_pack_t pack;
   tr64_load_scope_begin();tr64_pack_mount(&pack,(uint32_t)bytes,names[i]);
   while(!tr64_pack_poll(&pack))pump();
   log_printf("level rows: %s level=%lu state=%lu\n",names[i],
     (unsigned long)mem_budget(TR64_MEM_level),(unsigned long)mem_budget(TR64_MEM_state));
   tr64_pack_unmount(&pack);tr64_load_scope_end();
   mem_level_end();
   if(i<2)mem_level_begin();
  }
 }
 else if(!strncmp(mode,"bind",4))tr64_catalog_bind_test((uint32_t)bytes,mode);
 else if(!strncmp(mode,"catalog",7))tr64_catalog_test((uint32_t)bytes,argv[2],mode);
 else if(!strncmp(mode,"stream",6)) {
  tr64_pack_t pack; tr64_pack_stream_block_t blocks[1024]; tr64_pack_stream_t stream;
  uint8_t buffer[TR64_PACK_BLOCK] __attribute__((aligned(16)));
  uint32_t i;
  tr64_load_scope_begin(); tr64_pack_mount(&pack,(uint32_t)bytes,argv[2]);
  while(!tr64_pack_poll(&pack))pump();
  if(!tr64_pack_has_kind(&pack,TR64_PACK_SOUND))sys_error("core-test: stream kind absent");
  tr64_pack_stream_index(&pack,TR64_PACK_SOUND,blocks,!strcmp(mode,"stream-small-index") ? 1u : 1024u,&stream);
  tr64_pack_unmount(&pack); tr64_load_scope_end();
  if(code_is_loaded("load"))sys_error("core-test: stream retained load unit");
  for(i=0;i<stream.block_count;++i) {
   tr64_io_req_t request={0};
   if(tr64_pack_stream_read(blocks+i,&request,buffer,0)!=TR64_IO_SUBMIT_INVALID || tr64_io_pending())
    sys_error("core-test: stream capacity was not enforced");
   if(tr64_pack_stream_read(blocks+i,&request,buffer,sizeof(buffer)))sys_error("core-test: stream submit");
   while(!tr64_pack_stream_poll(blocks+i,&request))pump();
  }
  log_printf("stream: blocks=%lu bytes=%lu unit_loaded=0\n",(unsigned long)stream.block_count,(unsigned long)stream.total_bytes);
 } else if(!strcmp(mode,"opaque") || !strcmp(mode,"repeat")) {
  tr64_pack_t pack; const void *data; uint32_t n;
  tr64_io_req_t queued[TR64_IO_REQUESTS]; uint32_t i;
  uint8_t buffers[TR64_IO_REQUESTS][16] __attribute__((aligned(16)));
  tr64_load_scope_begin();
  for(i=0;i<TR64_IO_REQUESTS;++i) {
   queued[i]=(tr64_io_req_t){buffers[i],0,16,0,TR64_IO_IDLE};
   if(tr64_io_submit(&queued[i]))sys_error("core-test: queue saturation setup");
  }
  tr64_pack_mount(&pack,(uint32_t)bytes,argv[2]);
  if(!pack.pending)sys_error("core-test: full queue did not defer mount");
  while(!tr64_pack_poll(&pack))pump();
  tr64_pack_begin_kind(&pack,TR64_PACK_TEX,TR64_MEM_level);
  while(!tr64_pack_poll(&pack))pump();
  data=tr64_pack_data(&pack,&n);
  log_printf("opaque: kind=65536 bytes=%lu crc=%08lx\n",(unsigned long)n,(unsigned long)tr64_crc32(0,data,n));
  if(!strcmp(mode,"repeat"))tr64_pack_begin_kind(&pack,TR64_PACK_TEX,TR64_MEM_level);
  tr64_pack_begin_kind(&pack,TR64_PACK_ROOMS,TR64_MEM_level);
  while(!tr64_pack_poll(&pack))pump();
  data=tr64_pack_data(&pack,&n);
  log_printf("opaque: kind=131072 bytes=%lu crc=%08lx\n",(unsigned long)n,(unsigned long)tr64_crc32(0,data,n));
  tr64_pack_unmount(&pack);
  tr64_load_scope_end();
 } else {
  tr64_level_load(&level,(uint32_t)bytes,argv[2]);
  if(!level.ready)sys_error("core-test: level not ready");
  if(!strcmp(mode,"image")) {
   uint32_t image_bytes=0;const uint8_t *image=tr64_level_title_image(&level,&image_bytes);
   if(!image || (image_bytes!=77344 && image_bytes!=153632) ||
      memcmp(image,"UI64",4) || memcmp(level.textures,"TX64",4) ||
      image==level.textures || code_is_loaded("load"))
    sys_error("core-test: image/TX span publication mismatch");
   level.ready=0;image_bytes=1;
   if(tr64_level_title_image(&level,&image_bytes) || image_bytes)
    sys_error("core-test: not-ready image was published");
  }
  if(!strcmp(mode,"values")) {
   const tr64_core_items_t *item=tr64_level_items(&level,0);
   const tr64_core_room_sprites_t *sprite=tr64_level_room_sprites(&level,0);
   const tr64_core_mesh_spheres_t *sphere=tr64_level_mesh_spheres(&level,0);
   const tr64_core_mesh_spheres_t *second=tr64_level_mesh_spheres(&level,1);
   if(tr64_level_table(&level,TR64_CORE_mesh_spheres)->count!=4 ||
      tr64_i16(sphere->x)!=-32768 || tr64_i16(sphere->y)!=123 ||
      tr64_i16(sphere->z)!=32767 || tr64_u16(sphere->radius)!=65535 ||
      tr64_i16(second->x)!=4 || tr64_i16(second->y)!=-5 ||
      tr64_i16(second->z)!=6 || tr64_u16(second->radius)!=0 ||
      memcmp(sphere,tr64_level_mesh_spheres(&level,2),sizeof(*sphere)) ||
      memcmp(second,tr64_level_mesh_spheres(&level,3),sizeof(*second)))
    sys_error("core-test: native mesh sphere fields/aliases mismatch");
   if(tr64_u32(tr64_level_rooms(&level,0)->sprite_start)!=0 ||
      tr64_u32(tr64_level_rooms(&level,0)->sprite_count)!=1 ||
      tr64_i16(sprite->x)!=128 || tr64_i16(sprite->y) || tr64_i16(sprite->z) ||
      (tr64_u16(sprite->gain)!=8090 && tr64_u16(sprite->gain)!=3232) ||
      tr64_u16(sprite->descriptor)!=2 || tr64_u16(sprite->reserved))
    sys_error("core-test: native room sprite fields mismatch");
   if(tr64_i32(item->y)!=-2 || tr64_i16(item->intensity)!=-1 || tr64_i16(item->angle)!=-8192 ||
      tr64_level_quadrant(item->angle)!=2 || tr64_i32(tr64_level_animations(&level,0)->speed)!=-65536 ||
      tr64_i16(tr64_level_rooms(&level,0)->alternate_room)!=-1 || tr64_level_sectors(&level,0)->floor!=-4)
    sys_error("core-test: typed fields/quadrant mismatch");
  }
 }
 tr64_mem_log(); log_flush(); sys_quit(0);
}
