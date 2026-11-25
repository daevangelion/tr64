/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "tr64/level.h"
#include "engine/core/sys.h"
static const char *const names[]={
#define NAME(name,kind,stride) #name,
 TR64_CORE_SCHEMA(NAME)
#undef NAME
};
const char *tr64_level_table_name(uint32_t kind)
{ return kind && kind<=TR64_CORE_TABLES ? names[kind-1] : "unknown"; }
const uint8_t *tr64_level_title_image(const tr64_level_t *l,uint32_t *bytes)
{
 int present=l && l->ready && l->title_image;
 if(bytes)*bytes=present ? l->title_image_bytes : 0;
 return present ? l->title_image : NULL;
}
const tr64_level_table_t *tr64_level_table(const tr64_level_t *l,uint32_t kind)
{
 if(!l->ready || !kind || kind>TR64_CORE_TABLES)
  sys_error("core %s: not ready/invalid table",tr64_level_table_name(kind));
 return l->tables+kind-1;
}
const void *tr64_level_at(const tr64_level_t *l,uint32_t kind,uint32_t index)
{
 const tr64_level_table_t *t=tr64_level_table(l,kind);
 if(index>=t->count)sys_error("core %s: index out of range",tr64_level_table_name(kind));
 return t->data+index*t->stride;
}
