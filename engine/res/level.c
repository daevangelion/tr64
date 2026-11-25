/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "engine/res/load-internal.h"
#include "tr64/mem.h"
#include "tr64/res.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
/* Catalog copy only: resident callback cannot enqueue I/O or enter a unit. */
extern void tr64_music_catalog_bind(const tr64_pack_t *) __attribute__((weak));
static const uint8_t strides[]={
#define STRIDE(name,kind,stride) stride,
 TR64_CORE_SCHEMA(STRIDE)
#undef STRIDE
};
void load_core_begin(tr64_level_t *l,tr64_pack_t *p)
{
 memset(l,0,sizeof(*l)); l->pack=p; l->loading=1;
 l->game=(uint16_t)p->game;
 load_pack_kind(p,1,TR64_MEM_level);
}
int load_core_poll(tr64_level_t *l)
{
 tr64_level_table_t *t; uint32_t kind=l->loading,n,count,stride,flags,offset,start;
 const uint8_t *raw;
 if(l->core_loaded)return 1;
 if(!kind)sys_error("core: load not begun");
 if(!load_pack_poll(l->pack))return 0;
 start=sys_time_us(); raw=load_pack_data(l->pack,&n);
 if(n<32 || memcmp(raw,"TR6C",4) || tr64_be32(raw+4)!=kind || tr64_be32(raw+28)!=TR64_CORE_ABI)
  sys_error("core %s: table header",tr64_level_table_name(kind));
 count=tr64_be32(raw+8); stride=tr64_be32(raw+12); flags=tr64_be32(raw+16); offset=tr64_be32(raw+20);
 if(stride!=strides[kind-1] || flags>1 || offset!=32 || tr64_be32(raw+24)!=n ||
    count>(n-32)/strides[kind-1] || count*strides[kind-1]!=n-32)
  sys_error("core %s: table offset/count out of range",tr64_level_table_name(kind));
 if(!flags && (count || kind!=TR64_CORE_demo_data))sys_error("core %s: invalid absence",tr64_level_table_name(kind));
 t=l->tables+kind-1; *t=(tr64_level_table_t){raw,raw+offset,count,n,stride,flags};
 l->pack->validation_us+=sys_time_us()-start;
 log_printf("core-table: %s count=%lu bytes=%lu hash=%08lx\n",tr64_level_table_name(kind),
  (unsigned long)count,(unsigned long)n,(unsigned long)l->pack->kind_checksum);
 if(kind==TR64_CORE_TABLES) { l->loading=0; l->core_loaded=1; return 1; }
 ++l->loading; load_pack_kind(l->pack,l->loading,TR64_MEM_level); return 0;
}
void load_finish(tr64_level_t *l)
{
 tr64_pack_t *p=l->pack; uint32_t start,texture_us,core_us,sound_us;
 if(!l->core_loaded || !p || p->kind!=TR64_PACK_TEX)sys_error("level: core/texture load incomplete");
 l->textures=load_pack_data(p,&l->texture_bytes);
 load_sound_prepare(l,p);
 log_printf("level-textures: bytes=%lu hash=%08lx\n",(unsigned long)l->texture_bytes,(unsigned long)p->kind_checksum);
 load_image_prepare(l,p);
 if(tr64_music_catalog_bind)tr64_music_catalog_bind(p);
 l->stats.bytes_read=p->bytes_read; l->stats.decode_us=p->decode_us;
 l->stats.hash_us=p->hash_us; l->stats.validation_us=p->validation_us; l->stats.staging_peak=p->staging_peak;
 l->stats.bus_us=p->read_us;
 /* Retained stream metadata/index belong to Audio. Release the temporary
  * read lease before bounded validation scratch; no view retains that lease. */
 load_pack_release(p); start=sys_time_us();
 load_validate_texture(l);
 texture_us=sys_time_us()-start; start=sys_time_us();
 l->ready=1; /* Internal accessor permission; publication is this call's return. */
 load_validate_core(l);
 core_us=sys_time_us()-start;
 start=sys_time_us(); load_sound_validate(l); sound_us=sys_time_us()-start;
 l->stats.validation_us+=texture_us+core_us+sound_us;
 log_printf("level-validate: core_us=%lu texture_us=%lu sound_us=%lu sound_refs=%s\n",
  (unsigned long)core_us,(unsigned long)texture_us,(unsigned long)sound_us,
  l->sound_metadata ? "validated" : "unvalidated");
 tr64_mem_reset(TR64_MEM_io); load_sound_bind(l); l->pack=NULL;
 if(l->sound_metadata)log_printf("level-sound: metadata=%lu blocks=%lu total=%lu metadata_crc=%08lx samples=%lu unavailable=%lu refs=validated\n",
  (unsigned long)l->sound_metadata_bytes,(unsigned long)l->sound_stream.block_count,
  (unsigned long)l->sound_stream.total_bytes,(unsigned long)l->sound_stream.blocks[0].raw_crc32,
  (unsigned long)tr64_be32(l->sound_metadata+16),(unsigned long)tr64_be32(l->sound_metadata+28));
}
