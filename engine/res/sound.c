/* SPDX-License-Identifier: MIT */
/* SN64 owns sample identities; Core keeps the source map/details unchanged. */
#include "engine/res/load-internal.h"
#include "engine/snd/snd.h"
#include "tr64/res.h"

extern void *tr64_snd_metadata_buffer(const tr64_level_t *,uint32_t *) __attribute__((weak));
extern tr64_pack_stream_block_t *tr64_snd_index_buffer(uint32_t *) __attribute__((weak));
extern int tr64_snd_validate_metadata(const void *,uint32_t,uint32_t,const tr64_level_t *) __attribute__((weak));
extern void tr64_snd_bind(const void *,uint32_t,const tr64_pack_stream_block_t *,uint32_t,uint32_t) __attribute__((weak));

void load_sound_prepare(tr64_level_t *level,tr64_pack_t *pack)
{
 tr64_pack_stream_block_t *blocks; uint32_t capacity,started; int status;
 void *metadata; tr64_io_req_t request={0};
 if(!tr64_pack_has_kind(pack,TR64_PACK_SOUND))return;
 if(!tr64_snd_metadata_buffer || !tr64_snd_index_buffer ||
    !tr64_snd_validate_metadata || !tr64_snd_bind)
  sys_error("sound section: Audio loader unavailable");
 blocks=tr64_snd_index_buffer(&capacity);
 load_pack_stream_index(pack,TR64_PACK_SOUND,blocks,capacity,&level->sound_stream);
 for(uint32_t i=1;i<level->sound_stream.block_count;++i)
  if(blocks[i].raw_bytes>TR64_SND_BLOCK_BYTES)sys_error("audio block partition");
 metadata=tr64_snd_metadata_buffer(level,&capacity);
 if(!metadata || blocks[0].logical_offset || blocks[0].raw_bytes>capacity)
  sys_error("sound section: metadata prefix capacity/offset");
 started=sys_time_us();
 while((status=tr64_pack_stream_read(blocks,&request,metadata,capacity))==TR64_IO_SUBMIT_BUSY)
  load_io_pump();
 if(status)sys_error("sound section: metadata I/O request");
 while(request.state==TR64_IO_QUEUED || request.state==TR64_IO_ACTIVE)load_io_pump();
 pack->read_us+=(uint32_t)(sys_time_us()-started);pack->bytes_read+=request.size;
 started=sys_time_us();
 if(!tr64_pack_stream_poll(blocks,&request))sys_error("sound section: incomplete metadata");
 pack->hash_us+=sys_time_us()-started;
 level->sound_metadata=metadata;level->sound_metadata_bytes=blocks[0].raw_bytes;
}
static int unavailable(const uint8_t *metadata,uint32_t sound,uint32_t sample)
{
 uint32_t lo=0,hi=tr64_be32(metadata+28),offset=tr64_be32(metadata+32);
 while(lo<hi) {
  uint32_t mid=lo+(hi-lo)/2;
  const uint8_t *pair=metadata+offset+mid*8;
  uint32_t s=tr64_be32(pair),n=tr64_be32(pair+4);
  if(s<sound || (s==sound && n<sample))lo=mid+1;
  else hi=mid;
 }
 if(lo>=tr64_be32(metadata+28))return 0;
 return tr64_be32(metadata+offset+lo*8)==sound && tr64_be32(metadata+offset+lo*8+4)==sample;
}
void load_sound_validate(const tr64_level_t *level)
{
 const uint8_t *metadata=level->sound_metadata;uint32_t samples,missing,offset,i,id;
 if(!metadata)return;
 if(!tr64_snd_validate_metadata(metadata,level->sound_metadata_bytes,level->sound_stream.total_bytes,level))
  sys_error("sound section: invalid SN64 metadata");
 samples=tr64_be32(metadata+16);missing=tr64_be32(metadata+28);offset=tr64_be32(metadata+32);
 /* Every exception must identify an actual variant of its unchanged map row. */
 for(i=0;i<missing;++i) {
  uint32_t sound=tr64_be32(metadata+offset+i*8),sample=tr64_be32(metadata+offset+i*8+4);
  int32_t detail=tr64_i16(*tr64_level_sound_map(level,sound));
  const tr64_core_sound_details_t *row;uint32_t first,count;
  if(detail<0)sys_error("core sound_map: unavailable sound is unmapped");
  row=tr64_level_sound_details(level,(uint32_t)detail);
  first=tr64_u16(row->sample);count=(tr64_u16(row->flags)>>2)&15u;if(!count)count=1;
  if(sample<first || sample-first>=count)
   sys_error("core sound_details: unrelated unavailable sample");
 }
 for(i=0;i<tr64_level_table(level,TR64_CORE_sound_details)->count;++i) {
  const tr64_core_sound_details_t *row=tr64_level_sound_details(level,i);
  uint32_t first=tr64_u16(row->sample),count=(tr64_u16(row->flags)>>2)&15u,seen=0,j;
  if(!count)count=1; /* Fixed soundPlay always selects the base sample. */
  if(first<samples && count<=samples-first)continue;
  for(id=0;id<tr64_level_table(level,TR64_CORE_sound_map)->count;++id)if(tr64_i16(*tr64_level_sound_map(level,id))==(int32_t)i) {
   seen=1;
   for(j=0;j<count;++j)if(first+j>=samples && !unavailable(metadata,id,first+j))
    sys_error("core sound_details: unlisted missing sample sound=%lu sample=%lu",(unsigned long)id,(unsigned long)(first+j));
  }
  if(!seen)sys_error("core sound_details: unreferenced detail sample outside bank");
 }
}
void load_sound_bind(const tr64_level_t *level)
{
#if TR64_HAS_TR2 || TR64_GAME == 3
 if(level->sound_metadata)
#else
 if(level->sound_metadata && level->game!=2)
#endif
  tr64_snd_bind(level->sound_metadata,level->sound_metadata_bytes,level->sound_stream.blocks,
   level->sound_stream.block_count,level->sound_stream.total_bytes);
}
