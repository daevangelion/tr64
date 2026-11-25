/* SPDX-License-Identifier: MIT */
#include <libdragon.h>
#include <sram.h>
#include "engine/save/internal.h"
static int transfer_valid(uint32_t offset,const void *data,uint32_t bytes)
{
 return data && bytes && !(offset&1) && !(bytes&1) && !((uintptr_t)data&15) &&
        offset<=TR64_SAVE_BYTES && bytes<=TR64_SAVE_BYTES-offset;
}
int tr64_save_platform_init(void)
{
 /* ROM header declares the chip. Do not use sram_detect(): its destructive
  * probe briefly replaces slot0's commit word. Writes verify real readback. */
 sram_init();return TR64_SAVE_OK;
}
int tr64_save_platform_read(uint32_t offset,void *data,uint32_t bytes)
{
 if(!transfer_valid(offset,data,bytes))return TR64_SAVE_ARGUMENT;
 /* Finish any previous ROM DMA before taking the shared PI engine. A pending
  * engine/io request can publish its already-completed data afterwards. */
 dma_wait();
 return sram_read(data,offset,bytes)==(int)bytes?TR64_SAVE_OK:TR64_SAVE_IO;
}
int tr64_save_platform_write(uint32_t offset,const void *data,uint32_t bytes)
{
 if(!transfer_valid(offset,data,bytes))return TR64_SAVE_ARGUMENT;
 dma_wait();
 return sram_write(data,offset,bytes)==(int)bytes?TR64_SAVE_OK:TR64_SAVE_IO;
}
