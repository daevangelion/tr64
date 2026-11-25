/* SPDX-License-Identifier: MIT */
/* Exercises only production APIs, with real engine/io and no codec/backend mocks. */
#include <string.h>
#include "tr64/pack.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/code.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
static void service(void)
{
 if(tr64_io_frame_bytes()>=TR64_IO_FRAME)tr64_io_frame();
 tr64_io_pump();
}
void tr64_catalog_test(uint32_t bytes,const char *level,const char *mode)
{
 tr64_pack_t pack;tr64_pack_stream_source_t source,absent;
 tr64_pack_stream_query_t query={0};tr64_pack_stream_block_t blocks[1024];
 tr64_mem_stats_t before,after,io,boot;
 uint8_t scratch[4096] __attribute__((aligned(16)));
 uint8_t data[TR64_PACK_BLOCK] __attribute__((aligned(16)));
 tr64_io_req_t busy[TR64_IO_REQUESTS];
 uint8_t buffers[TR64_IO_REQUESTS][16] __attribute__((aligned(16)));
 uint32_t i,offset;
 int only=!strncmp(mode,"catalog-only",12);
 int busy_query=strstr(mode,"busy")!=NULL,drain=strstr(mode,"drain")!=NULL;
 tr64_mem_stats(TR64_MEM_level,&boot);
 if(!strcmp(mode,"catalog-only-no-scope"))tr64_pack_mount_catalog(&pack,bytes);
 tr64_load_scope_begin();
 if(only)tr64_pack_mount_catalog(&pack,bytes);
 else tr64_pack_mount(&pack,bytes,level);
 while(!tr64_pack_poll(&pack))service();
 if(only && (pack.entries || pack.staging || pack.count || pack.staging_peak ||
    pack.level_index!=UINT32_MAX || pack.bytes_read!=32u+pack.levels*48u ||
    pack.io_end!=pack.levels*48u))
  sys_error("catalog-test: bootstrap read more than header/directory");
 if(!strcmp(mode,"catalog-only-kind"))tr64_pack_begin_kind(&pack,TR64_PACK_VIDEO,TR64_MEM_level);
 if(!tr64_pack_stream_source(&pack,only ? "VIDEO_00" : "MUSIC_02",
     only ? TR64_PACK_VIDEO : TR64_PACK_MUSIC,&source) ||
    tr64_pack_stream_source(&pack,only ? "VIDEO_MISSING" : "MUSIC_58",
     only ? TR64_PACK_VIDEO : TR64_PACK_MUSIC,&absent) || absent.block_count)
  sys_error("catalog-test: named source lookup");
 tr64_pack_unmount(&pack);tr64_load_scope_end();
 if(code_is_loaded("load"))sys_error("catalog-test: load unit retained");
 tr64_mem_stats(TR64_MEM_io,&io);tr64_mem_stats(TR64_MEM_level,&before);
 if(io.used)sys_error("catalog-test: I/O lease retained");
 if(only && (boot.used || before.used || before.peak!=boot.peak))
  sys_error("catalog-test: bootstrap planned a level");
 if(busy_query)
  for(i=0;i<TR64_IO_REQUESTS;++i) {
   busy[i]=(tr64_io_req_t){buffers[i],0,16,0,TR64_IO_IDLE};
   if(tr64_io_submit(busy+i))sys_error("catalog-test: busy setup");
  }
 tr64_io_frame();
 tr64_pack_stream_query_begin(&query,&source,bytes,blocks,
  strstr(mode,"small") ? 1u : 1024u,scratch,
  strstr(mode,"workspace") ? 63u : sizeof(scratch));
 if(strstr(mode,"active"))
  tr64_pack_stream_query_begin(&query,&source,bytes,blocks,1024,scratch,sizeof(scratch));
 if(drain) {
  uint32_t expected=query.pending ? 0 : query.request.size;
  while(!tr64_pack_stream_query_drain(&query))service();
  if(query.result.blocks || query.result.block_count || tr64_io_frame_bytes()!=expected)
   sys_error("catalog-test: drain submitted another page");
  while(tr64_io_pending())service();
  tr64_pack_stream_query_begin(&query,&source,bytes,blocks,1024,scratch,sizeof(scratch));
 }
 while(!tr64_pack_stream_query_poll(&query)) {
  if(query.result.blocks || query.result.block_count)sys_error("catalog-test: unverified result published");
  service();
 }
 if(query.result.blocks!=blocks || query.result.block_count!=source.block_count || code_is_loaded("load"))
  sys_error("catalog-test: resident query result");
 offset=0;
 for(i=0;i<query.result.block_count;++i) {
  tr64_io_req_t request={0};
  if(blocks[i].logical_offset!=offset)sys_error("catalog-test: logical index gap");
  offset+=blocks[i].raw_bytes;
  if(tr64_pack_stream_read(blocks+i,&request,data,sizeof(data)))sys_error("catalog-test: payload submit");
  while(!tr64_pack_stream_poll(blocks+i,&request))service();
 }
 if(offset!=query.result.total_bytes)sys_error("catalog-test: total extent");
 tr64_mem_stats(TR64_MEM_level,&after);tr64_mem_stats(TR64_MEM_io,&io);
 if(before.used!=after.used || before.peak!=after.peak || io.used || tr64_io_pending())
  sys_error("catalog-test: playback allocation or pending I/O");
 log_printf("catalog: blocks=%lu bytes=%lu source_bytes=%lu query_bytes=%lu unit_loaded=0 io_used=0\n",
  (unsigned long)query.result.block_count,(unsigned long)query.result.total_bytes,
  (unsigned long)sizeof(source),(unsigned long)sizeof(query));
}

void tr64_catalog_bind_test(uint32_t expected_bytes,const char *mode)
{
 tr64_pack_t pack; tr64_pack_stream_source_t absent;
 tr64_mem_stats_t before,after,io;
 tr64_io_req_t request={0};
 uint8_t header[32] __attribute__((aligned(16)));
 uint8_t expected[32]; uint32_t bytes;
 tr64_mem_stats(TR64_MEM_level,&before);
 if(!strcmp(mode,"bind-busy")) {
  request=(tr64_io_req_t){header,0,32,0,TR64_IO_IDLE};
  if(tr64_io_submit(&request))sys_error("catalog-bind-test: busy setup");
 } else if(!strcmp(mode,"bind-lease")) {
  (void)tr64_mem_alloc(TR64_MEM_io,16);
 } else if(!strcmp(mode,"bind-scope"))tr64_load_scope_begin();
 bytes=tr64_level_pack_bind_streams();
 if(bytes!=expected_bytes)sys_error("catalog-bind-test: configured extent mismatch");
 tr64_load_scope_begin(); tr64_pack_mount_catalog(&pack,bytes);
 while(!tr64_pack_poll(&pack))service();
 if(pack.entries || pack.staging || pack.bytes_read!=32u+pack.levels*48u ||
    tr64_pack_stream_source(&pack,"NOT_A_PACK_ENTRY",TR64_PACK_VIDEO,&absent))
  sys_error("catalog-bind-test: bootstrap state");
 memcpy(expected,pack.header,32);
 tr64_pack_unmount(&pack); tr64_load_scope_end();
 tr64_level_release(); tr64_io_init(0);
 request=(tr64_io_req_t){header,0,32,0,TR64_IO_IDLE};
 if(tr64_io_submit(&request)!=TR64_IO_SUBMIT_INVALID)
  sys_error("catalog-bind-test: released binding still accepts reads");
 if(tr64_level_pack_bind_streams()!=bytes)
  sys_error("catalog-bind-test: restored extent mismatch");
 if(tr64_io_submit(&request))sys_error("catalog-bind-test: restored read rejected");
 while(request.state!=TR64_IO_DONE) {
  if(request.state==TR64_IO_FAILED)sys_error("catalog-bind-test: restored read failed");
  service();
 }
 if(memcmp(header,expected,32))sys_error("catalog-bind-test: rebound different bytes");
 tr64_mem_stats(TR64_MEM_level,&after); tr64_mem_stats(TR64_MEM_io,&io);
 if(before.used || after.used || before.peak!=after.peak || io.used ||
    tr64_io_pending() || code_is_loaded("load"))
  sys_error("catalog-bind-test: bootstrap retained state");
 log_printf("catalog-bind: bytes=%lu restored=1 directory_only=1 level_used=0 io_used=0 unit_loaded=0\n",
  (unsigned long)bytes);
}
