/* SPDX-License-Identifier: MIT */
/* Synthetic test module, linked only by loader-tests.py. It exercises a real
 * 32-byte ROM-header range through engine/io; it is not a game-pack loader. */
#include <stdlib.h>
#include <string.h>
#include "tr64/route.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "tr64/code.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
void tr64_scene_load(const char *level)
{
 uint8_t *buffer; uint32_t start; uint64_t bus_before;tr64_mem_stats_t level_pool;tr64_io_req_t request={0};
 if(strcmp(level,"rom-bind-test"))sys_error("scene loader probe wrong identity");
 tr64_mem_stats(TR64_MEM_level,&level_pool);
 if(level_pool.used)sys_error("scene loader probe overlaps boot-card borrow");
 /* The dispatcher already owns an outer loading scope. Native APIs may
  * nest the same scope; dispatch still resolves by name on every call. */
 code_loading_begin();code_load("test");
 if(code_call("test",123)!=279u)sys_error("scene loader probe generic dispatch");
 code_unload("test");code_loading_end();
#ifdef TR64_HOST
 const char *path=getenv("TR64_TEST_CART");
 if(!path || sys_cart_open(path)!=32)sys_error("scene loader probe host file binding");
#else
 if(sys_cart_bind(TR64_CART_BYTES,1)!=-1 || sys_cart_bind(0,0)!=-1 ||
    sys_cart_bind(1,32)!=-1 || sys_cart_bind(0,TR64_CART_BYTES+1u)!=-1 ||
    sys_cart_bind(TR64_CART_BYTES-32u,32)!=-1)
  sys_error("scene loader probe accepted invalid ROM range");
 if(sys_cart_bind(0x20,32))sys_error("scene loader probe ROM binding");
#endif
 tr64_io_init(32); buffer=tr64_mem_alloc(TR64_MEM_io,32);
 memset(buffer,0xa5,32);bus_before=sys_io_total_us();
 request.dst=buffer;request.offset=30;request.size=4;
 if(tr64_io_submit(&request)!=-1)sys_error("scene loader probe accepted out-of-range read");
 request.offset=0;request.size=32;
 if(tr64_io_submit(&request))sys_error("scene loader probe submit");
#ifdef TR64_HOST
 if(sys_cart_open(path)!=-1)sys_error("scene loader probe accepted queued file rebind");
#else
 if(sys_cart_bind(0x20,32)!=-1)sys_error("scene loader probe accepted queued ROM rebind");
#endif
 tr64_io_frame();start=sys_time_us();
 while(request.state!=TR64_IO_DONE && request.state!=TR64_IO_FAILED) {
  tr64_io_pump();
  if((uint32_t)(sys_time_us()-start)>500000u)sys_error("scene loader probe DMA timeout");
 }
 if(request.state!=TR64_IO_DONE || request.done!=32 || memcmp(buffer,"tr64",4))
  sys_error("scene loader probe ROM-header bytes mismatch");
 if(tr64_io_pending())sys_error("scene loader probe retained completed IO");
 if(sys_io_total_us()<=bus_before)sys_error("scene loader probe missing cumulative IO time");
 log_printf("scene loader bus_us=%llu\n",(unsigned long long)(sys_io_total_us()-bus_before));
 log_printf("scene loader probe PASS level=%s bytes=%lu\n",level,(unsigned long)request.done);log_flush();
}
