/* SPDX-License-Identifier: MIT */
/* Explicit test-source probe only. No renderer/decoder substitute: exercise the
 * real whole-arena borrow before the ordinary boot card claims that arena. */
#include "tr64/mem.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
__attribute__((constructor)) static void video_borrow_probe(void)
{
 uint32_t capacity;tr64_mem_stats_t p;uint8_t *first,*second;
 sys_memory_boot();first=mem_video_begin(&capacity);tr64_mem_stats(TR64_MEM_level,&p);
 if(capacity!=TR64_BYTES_level || first!=p.base || ((uintptr_t)first&15u) ||
    p.used!=capacity || p.peak!=capacity)sys_error("VIDEO probe carve mismatch");
 first[0]=0xa5;first[capacity-1]=0x5a;
 mem_video_end(first);tr64_mem_stats(TR64_MEM_level,&p);
 if(p.used || p.peak!=capacity)sys_error("VIDEO probe release mismatch");
 second=mem_video_begin(&capacity);
 if(second!=first)sys_error("VIDEO probe changed base");
 mem_video_end(second);
 /* Leave buffered until normal main initializes emulog and flushes boot logs. */
 log_printf("video-borrow: PASS base=%08lx capacity=%lu cycles=2 used=0 peak=%lu\n",
  (unsigned long)(uintptr_t)first,(unsigned long)capacity,(unsigned long)p.peak);
}
