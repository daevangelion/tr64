/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "tr64/limits.h"
#include "engine/core/log.h"
static char output[32768];static uint32_t used;
void sys_log_write(const char *p,uint32_t n)
{
 if(n>sizeof(output)-used) { fputs("logger test sink overflow\n",stderr);exit(1); }
 memcpy(output+used,p,n);used+=n;
}
void sys_error(const char *fmt,...)
{
 va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);exit(1);
}
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"log burst FAIL line%d: %s\n",__LINE__,#x);return 1; } } while(0)
int main(void)
{
 char line[TR64_LOG_LINE_MAX];log_stats_t stats;
 memset(line,'q',sizeof(line)-1);line[sizeof(line)-1]=0;
 for(unsigned i=0;i<8;++i)log_printf("%s",line);
 log_stats(&stats);CHECK(stats.peak_pending==8u*(sizeof(line)-1) && !stats.dropped && !used);
 log_report();CHECK(used<112 && used && !memcmp(output,"log-ring:",9));
 used=0;log_flush();CHECK(used==8u*(sizeof(line)-1));
 for(uint32_t i=0;i<used;++i)CHECK(output[i]=='q');
 used=0;uint32_t count=TR64_LOG_RING_BYTES/(sizeof(line)-1)+2;
 for(uint32_t i=0;i<count;++i)log_printf("%s",line);
 uint32_t lost=count*(sizeof(line)-1)-TR64_LOG_RING_BYTES;
 log_stats(&stats);CHECK(stats.dropped==lost && stats.peak_pending>TR64_LOG_RING_BYTES);
 log_flush();CHECK(used>TR64_LOG_RING_BYTES);
 CHECK(!memcmp(output,"log: ",5));
 for(uint32_t i=used-TR64_LOG_RING_BYTES;i<used;++i)CHECK(output[i]=='q');
 printf("log burst PASS: near4KiB=%u loss0; report does not flush; intentionaloverflow detected lost=%u peak=%u ring=%u\n",
        8u*(unsigned)(sizeof(line)-1),lost,stats.peak_pending,TR64_LOG_RING_BYTES);
 return 0;
}
