/* SPDX-License-Identifier: MIT */
/* Native pack implementation, no asset/DFS layer and no heap allocation. */
#include <string.h>
#include "engine/res/load-internal.h"
#include "tr64/res.h"
#include "tr64/mem.h"
#include "engine/core/sys.h"

static int length(const uint8_t *s,uint32_t size,uint32_t *pos,uint32_t *n)
{
 uint32_t b;
 if(*n!=15)return 0;
 do {
  if(*pos>=size)return -1;
  b=s[(*pos)++];
  if(*n>UINT32_MAX-b)return -1;
  *n+=b;
 } while(b==255);
 return 0;
}
int load_lz4(const uint8_t *s,uint32_t size,uint8_t *d,uint32_t raw)
{
 uint32_t ip=0,op=0;
 while(ip<size) {
  uint32_t token=s[ip++],n=token>>4,distance;
  if(length(s,size,&ip,&n) || n>size-ip || n>raw-op)return -1;
  memcpy(d+op,s+ip,n); ip+=n; op+=n;
  if(ip==size)break;
  if(size-ip<2)return -1;
  distance=s[ip] | (uint32_t)s[ip+1]<<8; ip+=2; n=token&15;
  if(length(s,size,&ip,&n) || n>UINT32_MAX-4)return -1;
  n+=4;
  if(!distance || distance>op || n>raw-op)return -1;
  while(n--) { d[op]=d[op-distance]; ++op; }
 }
 return op==raw ? 0 : -1;
}
static uint32_t word(const uint8_t *entry,unsigned index) { return tr64_be32(entry+index*4); }
static const uint8_t *entry(const tr64_pack_t *p,uint32_t index) { return p->entries+index*TR64_PACK_RETAINED_ENTRY_BYTES; }
static uint32_t even(uint32_t n) { return (n+1u)&~1u; }
static void io_room(const tr64_pack_t *p,uint32_t bytes)
{
 tr64_mem_stats_t s; uint32_t start;
 tr64_mem_stats(TR64_MEM_io,&s); start=(s.used+15u)&~15u;
 if(start>s.budget || bytes>s.budget-start)
  sys_error("pack %s section table: section too large for pool io",p->name);
}
static void submit(tr64_pack_t *p)
{
 int status=tr64_io_submit(&p->request);
 if(status && tr64_io_pending()==TR64_IO_REQUESTS) { p->pending=1; return; }
 if(status)sys_error("pack %s section %lu: invalid I/O request/binding",p->name,(unsigned long)p->kind);
 p->pending=0; p->read_started=sys_time_us(); p->bytes_read+=p->request.size;
}
static void read_begin(tr64_pack_t *p,uint8_t *dst,uint32_t offset,uint32_t bytes)
{
 if(bytes>p->bytes || offset>p->bytes-bytes)
  sys_error("pack %s section %lu: truncated section/read bounds",p->name,(unsigned long)p->kind);
 p->request=(tr64_io_req_t){dst,offset,bytes,0,TR64_IO_IDLE};
 submit(p);
}
static void mount(tr64_pack_t *p,uint32_t bytes,const char *name,uint32_t catalog_only)
{
 tr64_mem_stats_t stats; uint32_t n=0;
 if(!name)sys_error("pack directory: missing level name");
 while(n<32 && name[n])++n;
 if(!n || n==32)sys_error("pack directory: level name length");
 tr64_mem_stats(TR64_MEM_io,&stats);
 if(stats.used)sys_error("pack loading requires an exclusive empty io pool");
 memset(p,0,sizeof(*p)); memcpy(p->name,name,n);
 p->catalog_only=catalog_only;
 if(bytes<32 || bytes>64u*1024u*1024u)sys_error("pack %s header: truncated section",p->name);
 p->bytes=bytes; p->leased=1; p->level_index=UINT32_MAX; p->phase=1;
 read_begin(p,p->header,0,32);
}
void load_pack_mount(tr64_pack_t *p,uint32_t bytes,const char *name)
{ mount(p,bytes,name,0); }
void load_pack_mount_catalog(tr64_pack_t *p,uint32_t bytes)
{ mount(p,bytes,"<catalog>",1); }
static int resident_section(uint32_t kind)
{
 if(kind && kind<TR64_PACK_TEX)return TR64_LEVEL_core;
 switch(kind) {
 case TR64_PACK_TEX:case TR64_PACK_IMAGE:return TR64_LEVEL_tex;
 case TR64_PACK_ROOMS:return TR64_LEVEL_rooms;
 case TR64_PACK_MESHES:return TR64_LEVEL_meshes;
 default:return -1;
 }
}
static void section_bytes(const tr64_pack_t *p,uint32_t bytes[TR64_LEVEL_COUNT],uint32_t kind,uint32_t raw)
{
 int section=resident_section(kind);
 if(section<0)return; /* Nonresident kinds (including streamed sound) reserve no level bytes. */
 if(bytes[section]>UINT32_MAX-15u)sys_error("pack %s section %lu: level size overflow",p->name,(unsigned long)kind);
 bytes[section]=(bytes[section]+15u)&~15u;
 if(section==TR64_LEVEL_core) {
  if(raw>UINT32_MAX-15u)sys_error("pack %s section %lu: level size overflow",p->name,(unsigned long)kind);
  raw=(raw+15u)&~15u;
 }
 if(raw>UINT32_MAX-bytes[section])sys_error("pack %s section %lu: level size overflow",p->name,(unsigned long)kind);
 bytes[section]+=raw;
}
static void validate_entries(tr64_pack_t *p)
{
 uint32_t i,last=0,destination=0,bytes[TR64_LEVEL_COUNT]={0};
 for(i=0;i<p->count;++i) {
  const uint8_t *e=entry(p,i);
  uint32_t kind=word(e,0),off=word(e,2),stored=word(e,3),raw=word(e,4),codec=word(e,6);
  if(!kind || kind<last || word(e,5)!=16 || codec>1 ||
     !stored || stored>TR64_PACK_BLOCK || !raw || raw>TR64_PACK_BLOCK ||
     off%16 || even(stored)>p->bytes || off>p->bytes-even(stored) || (!codec && stored!=raw))
   sys_error("pack %s section %lu: truncated section/descriptor bounds",p->name,(unsigned long)kind);
  if(kind!=last) { section_bytes(p,bytes,last,destination); destination=0; }
  if(word(e,1)!=destination || raw>UINT32_MAX-destination)
   sys_error("pack %s section %lu: destination offset out of range",p->name,(unsigned long)kind);
  destination+=raw; last=kind;
 }
 section_bytes(p,bytes,last,destination);
 mem_level_plan(p->name,bytes);
}
static void part_begin(tr64_pack_t *p)
{
 const uint8_t *e=entry(p,p->next);
 uint32_t destination=word(e,1),stored=word(e,3);
 int staged=word(e,6)!=0 || (stored&1u) || (destination&15u);
 if(staged && even(stored)>p->staging_peak)p->staging_peak=even(stored);
 read_begin(p,staged ? p->staging : p->output+destination,word(e,2),even(stored));
}
int load_pack_poll(tr64_pack_t *p)
{
 uint32_t n,off,start; const uint8_t *e;
 if(!p->phase)return 1;
 if(p->pending) { submit(p); return 0; }
 if(p->request.state==TR64_IO_FAILED)sys_error("pack %s section %lu: truncated section/I/O failed",p->name,(unsigned long)p->kind);
 if(p->request.state!=TR64_IO_DONE)return 0;
 p->read_us+=(uint32_t)(sys_time_us()-p->read_started);
 if(p->phase==1) {
  start=sys_time_us();
  const uint8_t *h=p->header;
  if(memcmp(h,"TR6P",4) || word(h,1)!=2 || word(h,2)!=p->bytes || word(h,4)!=32 || word(h,5)!=48 || word(h,7))
   sys_error("pack header: truncated section/invalid header");
  p->levels=word(h,3);
  if(!p->levels || p->levels>(p->bytes-32)/48 || p->levels>TR64_BYTES_io/48)
   sys_error("pack directory: count out of range");
  n=p->levels*48;
  io_room(p,n);
  p->directory=tr64_mem_alloc(TR64_MEM_io,n);
  p->phase=2; p->validation_us+=sys_time_us()-start;
  read_begin(p,p->directory,32,n); return 0;
 }
 if(p->phase==2) {
  uint32_t i; tr64_mem_stats_t stats;
  start=sys_time_us();
  if(tr64_crc32(0,p->directory,p->levels*48)!=word(p->header,6))sys_error("pack directory: corrupt CRC");
  for(i=0;i<p->levels;++i) {
   e=p->directory+i*48;
   if(!memchr(e,0,32) || !e[0] || (i && strcmp((const char *)e-48,(const char *)e)>=0))
    sys_error("pack directory: invalid/unsorted level name");
   uint32_t game=word(e,11);
#if defined(TR64_HOST) || TR64_GAME == 3
   if(game!=0 && game!=2 && game!=3)
#else
   if(game!=0 && game!=2)
#endif
    sys_error("pack %s: unsupported game version %lu",(const char *)e,(unsigned long)game);
   if(!p->catalog_only && !strcmp((const char *)e,p->name))p->level_index=i;
  }
  if(p->catalog_only) {
   tr64_mem_stats(TR64_MEM_io,&stats); p->io_end=stats.used;
   p->phase=0; p->validation_us+=sys_time_us()-start; return 1;
  }
  if(p->level_index==UINT32_MAX)sys_error("pack %s: level not found",p->name);
  e=p->directory+p->level_index*48;
  memcpy(p->name,e,32); off=word(e,8); p->count=word(e,9);
  p->game=word(e,11);
#ifdef TR64_HOST
  p->directory=mem_host_level_game(p->game>=2 ? 2 : 1,p->directory,p->levels*48u);
#elif TR64_GAME == 2
  if(p->game!=2)sys_error("pack %s: Tomb Raider I entry on Tomb Raider II cart",p->name);
#elif TR64_GAME == 3
  if(p->game!=3)sys_error("pack %s: non-Tomb Raider III entry on Tomb Raider III cart",p->name);
#else
  if(p->game!=0)sys_error("pack %s: Tomb Raider II entry on Tomb Raider I cart",p->name);
#endif
  if(!p->count || off%16 || p->count>p->bytes/64 || off>p->bytes-p->count*64)
   sys_error("pack %s section table: offset/count out of range",p->name);
  n=p->count*TR64_PACK_RETAINED_ENTRY_BYTES; io_room(p,n+TR64_PACK_BLOCK);
  p->entries=tr64_mem_alloc(TR64_MEM_io,n);
  p->staging=tr64_mem_alloc(TR64_MEM_io,TR64_PACK_BLOCK);
  tr64_mem_stats(TR64_MEM_io,&stats); p->io_end=stats.used;
  p->table_offset=off; p->table_read=0; p->table_crc=0;
  p->phase=3; p->validation_us+=sys_time_us()-start;
  n=p->count*64; if(n>TR64_PACK_BLOCK)n=TR64_PACK_BLOCK;
  if(n>p->staging_peak)p->staging_peak=n;
  read_begin(p,p->staging,off,n); return 0;
 }
 if(p->phase==3) {
  uint32_t i,total=p->count*64;
  start=sys_time_us(); n=p->request.size;
  p->table_crc=tr64_crc32(p->table_crc,p->staging,n);
  for(i=0;i<n/64;++i)
   memcpy(p->entries+(p->table_read/64+i)*TR64_PACK_RETAINED_ENTRY_BYTES,
    p->staging+i*64,TR64_PACK_RETAINED_ENTRY_BYTES);
  p->table_read+=n;
  if(p->table_read<total) {
   n=total-p->table_read; if(n>TR64_PACK_BLOCK)n=TR64_PACK_BLOCK;
   if(n>p->staging_peak)p->staging_peak=n;
   p->validation_us+=sys_time_us()-start;
   read_begin(p,p->staging,p->table_offset+p->table_read,n); return 0;
  }
  e=p->directory+p->level_index*48;
  if(p->table_crc!=word(e,10))sys_error("pack %s section table: corrupt CRC",p->name);
  validate_entries(p); p->validation_us+=sys_time_us()-start; p->phase=0; return 1;
 }
 if(p->phase==4) {
  uint32_t checksum,raw,stored,destination,codec,start;
  e=entry(p,p->next); destination=word(e,1); stored=word(e,3); raw=word(e,4); codec=word(e,6);
  if(codec) {
   start=sys_time_us();
   if(load_lz4(p->staging,stored,p->output+destination,raw))
    sys_error("pack %s section %lu: LZ4 decode bounds",p->name,(unsigned long)p->kind);
   p->decode_us+=sys_time_us()-start;
  } else if((stored&1u) || (destination&15u))memcpy(p->output+destination,p->staging,raw);
  start=sys_time_us(); checksum=tr64_crc32(0,p->output+destination,raw);
  p->hash_us+=sys_time_us()-start;
  if(checksum!=word(e,7))sys_error("pack %s section %lu: corrupt checksum",p->name,(unsigned long)p->kind);
  p->kind_checksum=tr64_crc32(p->kind_checksum,e+28,4);
  if(++p->next==p->end) { p->phase=0; return 1; }
  part_begin(p); return 0;
 }
 sys_error("pack %s section %lu: invalid loader state",p->name,(unsigned long)p->kind);
}
void load_pack_kind(tr64_pack_t *p,uint32_t kind,int pool)
{
 uint32_t i,first,total=0; tr64_mem_stats_t stats;
 if(p->phase || !p->entries || !kind || pool==TR64_MEM_io)sys_error("pack section %lu: invalid load/pool",(unsigned long)kind);
 for(i=0;i<p->count && word(entry(p,i),0)<kind;++i) { }
 first=p->next=i;
 while(i<p->count && word(entry(p,i),0)==kind) { total+=word(entry(p,i),4); ++i; }
 if(i==first)sys_error("pack %s section %lu: kind missing",p->name,(unsigned long)kind);
 p->kind=kind; p->kind_checksum=0; p->end=i; p->output_bytes=total;
 if(pool==TR64_MEM_level) {
  int section=resident_section(kind);
  if(section<0)sys_error("pack %s section %lu: not a resident level kind",p->name,(unsigned long)kind);
  p->output=mem_level_alloc(section,total);
 } else {
  tr64_mem_stats(pool,&stats);
  if(stats.kind!=TR64_CARVE || ((stats.used+15u)&~15u)>stats.budget || total>stats.budget-((stats.used+15u)&~15u))
   sys_error("pack %s section %lu: section too large for pool %s",p->name,(unsigned long)kind,stats.name);
  p->output=tr64_mem_alloc(pool,total);
 }
 p->phase=4; part_begin(p);
}
void load_pack_stream_index(tr64_pack_t *p,uint32_t kind,
 tr64_pack_stream_block_t *blocks,uint32_t capacity,tr64_pack_stream_t *stream)
{
 uint32_t i,n=0,total=0;
 if(p->phase || !p->entries || !blocks || !stream || resident_section(kind)>=0)
  sys_error("pack section %lu: invalid stream export",(unsigned long)kind);
 for(i=0;i<p->count;++i) {
  const uint8_t *e=entry(p,i);
  if(word(e,0)!=kind)continue;
  if(n==capacity)sys_error("pack %s section %lu: stream index capacity",p->name,(unsigned long)kind);
  if(word(e,6) || word(e,3)!=word(e,4))
   sys_error("pack %s section %lu: stream block compressed",p->name,(unsigned long)kind);
  blocks[n++]=(tr64_pack_stream_block_t){word(e,1),word(e,2),word(e,4),word(e,7)};
  total=word(e,1)+word(e,4); /* Checked contiguous/overflow-free at mount. */
 }
 if(!n)sys_error("pack %s section %lu: stream kind missing",p->name,(unsigned long)kind);
 *stream=(tr64_pack_stream_t){blocks,n,total};
}
const void *load_pack_data(const tr64_pack_t *p,uint32_t *bytes)
{
 if(p->phase || !p->output)sys_error("pack section %lu: data not ready",(unsigned long)p->kind);
 if(bytes)*bytes=p->output_bytes;
 return p->output;
}
void load_pack_release(tr64_pack_t *p)
{
 tr64_mem_stats_t stats;
 if(!p->leased || p->phase || p->pending)sys_error("pack %s: unmount while pending/not mounted",p->name);
 tr64_mem_stats(TR64_MEM_io,&stats);
 if(stats.used!=p->io_end)sys_error("pack %s: exclusive io lease ownership changed",p->name);
 tr64_mem_reset(TR64_MEM_io);
 p->leased=0; p->directory=p->entries=p->staging=p->output=NULL; p->io_end=0;
}
