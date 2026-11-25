/* SPDX-License-Identifier: MIT */
/* Caller-owned, codec-agnostic metadata transport; no heap or code-unit entry. */
#include <string.h>
#include "tr64/pack.h"
#include "tr64/res.h"
#include "engine/core/sys.h"

enum { QUERY_IDLE,QUERY_READ,QUERY_READY };
int tr64_pack_stream_source(const tr64_pack_t *pack,const char *name,uint32_t kind,
 tr64_pack_stream_source_t *source)
{
 uint32_t lo=0,hi;
 if(!pack || !pack->leased || pack->phase || !pack->directory || !name || !source ||
    kind<TR64_PACK_SOUND || kind==TR64_PACK_IMAGE)
  sys_error("pack catalog: invalid lookup");
 hi=pack->levels;
 while(lo<hi) {
  uint32_t mid=lo+(hi-lo)/2;
  int order=strcmp((const char *)pack->directory+mid*48,name);
  if(order<0)lo=mid+1;else hi=mid;
 }
 *source=(tr64_pack_stream_source_t){0};
 if(lo==pack->levels || strcmp((const char *)pack->directory+lo*48,name))return 0;
 const uint8_t *entry=pack->directory+lo*48;
 uint32_t offset=tr64_be32(entry+32),count=tr64_be32(entry+36);
 if(!count || (offset&15u) || count>pack->bytes/64 || offset>pack->bytes-count*64)
  sys_error("pack catalog: directory table bounds");
 *source=(tr64_pack_stream_source_t){offset,count,tr64_be32(entry+40),kind};
 return 1;
}
static void query_submit(tr64_pack_stream_query_t *query)
{
 int status=tr64_io_submit(&query->request);
 if(status==TR64_IO_SUBMIT_BUSY) { query->pending=1;return; }
 if(status)sys_error("pack stream table: I/O request");
 query->pending=0;
}
static void query_page(tr64_pack_stream_query_t *query)
{
 uint32_t bytes=query->source.block_count*64-query->wire_done;
 if(bytes>query->scratch_bytes)bytes=query->scratch_bytes;
 query->request=(tr64_io_req_t){query->scratch,query->source.table_offset+query->wire_done,bytes,0,TR64_IO_IDLE};
 query_submit(query);
}
void tr64_pack_stream_query_begin(tr64_pack_stream_query_t *query,
 const tr64_pack_stream_source_t *source,uint32_t pack_bytes,
 tr64_pack_stream_block_t *blocks,uint32_t capacity,void *scratch,uint32_t scratch_bytes)
{
 if(!query || !source || !blocks || !scratch || ((uintptr_t)scratch&15u) ||
    scratch_bytes<64 || scratch_bytes>TR64_PACK_BLOCK || (scratch_bytes&63u))
  sys_error("pack stream table: invalid workspace");
 if(query->state==QUERY_READ || query->request.state==TR64_IO_QUEUED || query->request.state==TR64_IO_ACTIVE)
  sys_error("pack stream table: query active");
 if(source->kind<TR64_PACK_SOUND || source->kind==TR64_PACK_IMAGE || !source->block_count ||
    (source->table_offset&15u) || source->block_count>pack_bytes/64 ||
    source->table_offset>pack_bytes-source->block_count*64)
  sys_error("pack stream table: source bounds");
 if(source->block_count>capacity)sys_error("pack stream table: index capacity");
 query->source=*source;
 query->result=(tr64_pack_stream_t){0};
 query->blocks=blocks;query->scratch=scratch;query->pack_bytes=pack_bytes;
 query->capacity=capacity;query->scratch_bytes=scratch_bytes;
 query->wire_done=0;query->total_bytes=0;query->crc=0;query->pending=0;
 query->state=QUERY_READ;query_page(query);
}
int tr64_pack_stream_query_poll(tr64_pack_stream_query_t *query)
{
 uint32_t i,first,n,remaining;
 if(!query || query->state==QUERY_IDLE)sys_error("pack stream table: query not begun");
 if(query->state==QUERY_READY)return 1;
 remaining=query->source.block_count*64-query->wire_done;
 n=remaining<query->scratch_bytes ? remaining : query->scratch_bytes;
 if(!remaining || query->request.dst!=query->scratch || query->request.size!=n ||
    query->request.offset!=query->source.table_offset+query->wire_done)
  sys_error("pack stream table: request mismatch");
 if(query->pending) { query_submit(query);return 0; }
 if(query->request.state==TR64_IO_QUEUED || query->request.state==TR64_IO_ACTIVE)return 0;
 if(query->request.state!=TR64_IO_DONE || query->request.done!=query->request.size)
  sys_error("pack stream table: truncated I/O");
 n=query->request.size;first=query->wire_done/64;
 query->crc=tr64_crc32(query->crc,query->scratch,n);
 for(i=0;i<n/64;++i) {
  const uint8_t *entry=query->scratch+i*64;
  uint32_t kind=tr64_be32(entry),destination=tr64_be32(entry+4),offset=tr64_be32(entry+8);
  uint32_t stored=tr64_be32(entry+12),raw=tr64_be32(entry+16),dma;
  if(kind!=query->source.kind)sys_error("pack stream table: kind mismatch");
  if(tr64_be32(entry+24) || stored!=raw)sys_error("pack stream table: compressed block");
  dma=(raw+1u)&~1u;
  if(!raw || raw>TR64_PACK_BLOCK || tr64_be32(entry+20)!=16 || (offset&15u) ||
     dma>query->pack_bytes || offset>query->pack_bytes-dma)
   sys_error("pack stream table: descriptor bounds");
  if(destination!=query->total_bytes || raw>UINT32_MAX-query->total_bytes)
   sys_error("pack stream table: logical destination bounds");
  query->blocks[first+i]=(tr64_pack_stream_block_t){destination,offset,raw,tr64_be32(entry+28)};
  query->total_bytes+=raw;
 }
 query->wire_done+=n;
 if(query->wire_done<query->source.block_count*64) { query_page(query);return 0; }
 if(query->crc!=query->source.table_crc32)sys_error("pack stream table: corrupt CRC");
 query->result=(tr64_pack_stream_t){query->blocks,query->source.block_count,query->total_bytes};
 query->state=QUERY_READY;return 1;
}
int tr64_pack_stream_query_drain(tr64_pack_stream_query_t *query)
{
 if(!query)sys_error("pack stream table: missing drain state");
 if(query->request.state==TR64_IO_QUEUED || query->request.state==TR64_IO_ACTIVE)return 0;
 if(query->request.state==TR64_IO_FAILED)sys_error("pack stream table: drain I/O failure");
 query->result=(tr64_pack_stream_t){0};query->state=QUERY_IDLE;query->pending=0;
 query->request=(tr64_io_req_t){0};return 1;
}
