/* SPDX-License-Identifier: MIT */
/* Runtime raw-block reads: no heap, decoder, filesystem or cold-code entry. */
#include <stdint.h>
#include "tr64/pack.h"
#include "tr64/res.h"
#include "engine/core/sys.h"

static uint32_t dma_bytes(const tr64_pack_stream_block_t *block)
{
 if(!block || !block->raw_bytes || block->raw_bytes>TR64_PACK_BLOCK || (block->rom_offset&15u))
  sys_error("pack stream: invalid block descriptor");
 return (block->raw_bytes+1u)&~1u;
}
int tr64_pack_stream_read(const tr64_pack_stream_block_t *block,tr64_io_req_t *request,
 void *buffer,uint32_t capacity)
{
 uint32_t bytes=dma_bytes(block);
 if(!request || !buffer || ((uintptr_t)buffer&15u) || capacity<bytes ||
    request->state==TR64_IO_QUEUED || request->state==TR64_IO_ACTIVE)
  return TR64_IO_SUBMIT_INVALID;
 *request=(tr64_io_req_t){buffer,block->rom_offset,bytes,0,TR64_IO_IDLE};
 return tr64_io_submit(request);
}
int tr64_pack_stream_poll(const tr64_pack_stream_block_t *block,const tr64_io_req_t *request)
{
 uint32_t bytes=dma_bytes(block);
 if(!request || !request->dst || ((uintptr_t)request->dst&15u) ||
    request->offset!=block->rom_offset || request->size!=bytes)
  sys_error("pack stream block %lu: request mismatch",(unsigned long)block->logical_offset);
 if(request->state==TR64_IO_QUEUED || request->state==TR64_IO_ACTIVE)return 0;
 if(request->state!=TR64_IO_DONE || request->done!=bytes)
  sys_error("pack stream block %lu: truncated I/O",(unsigned long)block->logical_offset);
 if(tr64_crc32(0,request->dst,block->raw_bytes)!=block->raw_crc32)
  sys_error("pack stream block %lu: corrupt checksum",(unsigned long)block->logical_offset);
 return 1;
}
