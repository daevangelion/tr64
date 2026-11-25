/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "engine/save/internal.h"
#include "tr64/code.h"
tr64_save_cache_t tr64_save_cache;
static uint32_t refused; /* slots the game's own load check rejected; survives rescans */
#ifdef TR64_HOST
const char *tr64_save_host_path;
int tr64_save_host_bind(const char *path)
{
 if(tr64_save_cache.scanned || !path || !*path)return TR64_SAVE_ARGUMENT;
 tr64_save_host_path=path;return TR64_SAVE_OK;
}
#endif
void tr64_save_unit_load(void) { code_load("medium"); }
static int call(tr64_save_call_t *request)
{ return (int)code_call("medium",(uint32_t)(uintptr_t)request); }
int tr64_save_scan(void)
{ tr64_save_call_t c={.operation=SAVE_SCAN};return call(&c); }
int tr64_save_write(unsigned slot,const tr64_save_info_t *info,const void *data,uint32_t bytes)
{
 tr64_save_call_t c={.operation=SAVE_WRITE,.slot=slot,.bytes=bytes,
                    .source=data,.input_info=info};
 int status=call(&c);
 if(status==TR64_SAVE_OK && slot<TR64_SAVE_SLOTS)refused&=~(1u<<slot);
 return status;
}
int tr64_save_read(unsigned slot,void *data,uint32_t capacity,tr64_save_info_t *info)
{
 tr64_save_call_t c={.operation=SAVE_READ,.slot=slot,.capacity=capacity,
                    .destination=data,.output_info=info};return call(&c);
}
int tr64_save_settings_write(const void *data,uint32_t bytes)
{
 tr64_save_call_t c={.operation=SAVE_SETTINGS_WRITE,.bytes=bytes,.source=data};
 return call(&c);
}
int tr64_save_settings_read(void *data,uint32_t capacity,uint32_t *bytes)
{
 tr64_save_call_t c={.operation=SAVE_SETTINGS_READ,.capacity=capacity,
                    .destination=data,.output_bytes=bytes};return call(&c);
}
int tr64_save_any(void)
{ return tr64_save_cache.scanned && tr64_save_cache.valid_slots!=0; }
int tr64_save_cached(unsigned slot,tr64_save_info_t *info)
{
 if(slot>=TR64_SAVE_SLOTS || !info)return TR64_SAVE_ARGUMENT;
 if(!tr64_save_cache.scanned)return TR64_SAVE_UNAVAILABLE;
 if(tr64_save_cache.slot_status[slot]!=TR64_SAVE_OK)return tr64_save_cache.slot_status[slot];
 *info=tr64_save_cache.info[slot];return TR64_SAVE_OK;
}
void tr64_save_refuse(unsigned slot)
{ if(slot<TR64_SAVE_SLOTS)refused|=1u<<slot; }
int tr64_save_slot_status(unsigned slot,tr64_save_info_t *info)
{
 tr64_save_info_t local;
 int status=tr64_save_cached(slot,&local);
 if(status)return status;
 if((refused&(1u<<slot)) || !local.identity ||
    local.identity!=tr64_save_level_identity(local.level_id))return TR64_SAVE_FOREIGN;
 if(info)*info=local;
 return TR64_SAVE_OK;
}
int tr64_save_usable_any(void)
{
 for(unsigned i=0;i<TR64_SAVE_SLOTS;++i)if(tr64_save_slot_status(i,NULL)==TR64_SAVE_OK)return 1;
 return 0;
}
