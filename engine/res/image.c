/* SPDX-License-Identifier: MIT */
/* UI64 layout facts supplied by the title-image cook; source pixels stay external. */
#include <string.h>
#include "engine/res/load-internal.h"
#include "engine/core/log.h"
#include "tr64/mem.h"
#include "tr64/res.h"
void load_image_prepare(tr64_level_t *level,tr64_pack_t *pack)
{
 const uint8_t *raw;uint32_t bytes,format,palette;
 if(!tr64_pack_has_kind(pack,TR64_PACK_IMAGE))return;
 load_pack_kind(pack,TR64_PACK_IMAGE,TR64_MEM_level);
 while(!load_pack_poll(pack))load_io_pump();
 raw=load_pack_data(pack,&bytes);
 if(bytes<32 || memcmp(raw,"UI64",4) || tr64_be32(raw+4)!=1 ||
    tr64_be32(raw+8)!=bytes || tr64_be32(raw+12)!=320 ||
    tr64_be32(raw+16)!=240 || tr64_be32(raw+24)!=32)
  sys_error("image section: invalid UI64 header");
 format=tr64_be32(raw+20);palette=tr64_be32(raw+28);
 if(!((format==1 && bytes==77344 && palette==76832) ||
      (format==2 && bytes==153632 && palette==0)))
  sys_error("image section: invalid UI64 format/extent");
 level->title_image=raw;level->title_image_bytes=bytes;
 log_printf("level-image: bytes=%lu format=%lu width=320 height=240 hash=%08lx\n",
  (unsigned long)bytes,(unsigned long)format,(unsigned long)pack->kind_checksum);
}
