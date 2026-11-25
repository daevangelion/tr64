/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "engine/core/fmt.h"
static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"fmt FAIL line%d: %s\n",__LINE__,#x);return 1; } } while(0)
static int compare(size_t cap,const char *fmt,...)
{
 unsigned char expected[256],actual[256];va_list ap,a,b;int x,y;
 memset(expected,0xa7,sizeof(expected));memset(actual,0xa7,sizeof(actual));
 va_start(ap,fmt);va_copy(a,ap);va_copy(b,ap);
 x=vsnprintf((char *)expected,cap,fmt,a);y=tr64_vsnprintf((char *)actual,cap,fmt,b);
 va_end(a);va_end(b);va_end(ap);
 CHECK(x>=0 && x==y);
 size_t n=cap;if(n>(size_t)x+1)n=(size_t)x+1;
 CHECK(!memcmp(actual,expected,n));
 for(size_t i=cap;i<sizeof(actual);++i)CHECK(actual[i]==0xa7);
 return 0;
}
/* Runtime-invalid inputs deliberately bypass compile-time format diagnosis;
 * exercise the same va_list API used by production logs/errors. */
static int runtime_format(char *dst,size_t cap,const char *fmt,...)
{ va_list ap;int n;va_start(ap,fmt);n=tr64_vsnprintf(dst,cap,fmt,ap);va_end(ap);return n; }
static int raw(char *dst,size_t cap,const char *fmt,...)
{ va_list ap;int n;va_start(ap,fmt);n=tr64_debug_vsnprintf(dst,cap,fmt,ap);va_end(ap);return n; }
typedef struct { char data[4096];uint32_t size;int fail; } sink_t;
static int output(void *opaque,const char *p,uint32_t n)
{
 sink_t *s=opaque;if(s->fail || n>sizeof(s->data)-s->size)return -1;
 memcpy(s->data+s->size,p,n);s->size+=n;return 0;
}
static int streamed(sink_t *sink,const char *fmt,...)
{ va_list ap;int n;va_start(ap,fmt);n=tr64_debug_vformat(output,sink,fmt,ap);va_end(ap);return n; }
int main(void)
{
 uint32_t random=1;char text[256];
 for(size_t cap=0;cap<=64;++cap) {
  CHECK(!compare(cap,"signed %d %+d % d %08d %-9d %.0d",INT_MIN,17,17,-23,17,0));
  CHECK(!compare(cap,"unsigned %#x %#X %#o %015u %.0u %.0x %#.0o",0x1234u,0x1234u,511u,UINT_MAX,0u,0u,0u));
  CHECK(!compare(cap,"length %hhd %hd %hhu %hu %ld %lld %zu %td %jd %ju",130,32769,257u,65537u,LONG_MIN,LLONG_MIN,(size_t)123,(ptrdiff_t)-4,(intmax_t)INTMAX_MIN,(uintmax_t)UINTMAX_MAX));
  CHECK(!compare(cap,"text [%10.4s][%-9s][%c][%c] %%","abcdefgh","xy",'Q',0));
  for(unsigned i=0;i<64;++i) {
   random=random*1664525u+1013904223u;uint64_t value=(uint64_t)random<<32;
   random=random*1664525u+1013904223u;value|=random;
   int width=(int)(random%25)-12,precision=(int)((random>>8)%14)-1;
   CHECK(!compare(cap,"[%*.*lld][%#0*.*llx]",width,precision,(long long)value,width,precision,(unsigned long long)value));
  }
 }
 CHECK(tr64_snprintf(NULL,0,"%2147483647d",1)==INT_MAX);
 CHECK(runtime_format(text,sizeof(text),"%2147483647dx",1)==-1);
 CHECK(runtime_format(text,sizeof(text),"%*d",INT_MIN,1)==-1);
 CHECK(tr64_snprintf(text,sizeof(text),"%p",(void *)(uintptr_t)0x1234)==6 && !strcmp(text,"0x1234"));
 int untouched=77;CHECK(tr64_snprintf(text,sizeof(text),"%n",&untouched)==-1 && untouched==77);
 CHECK(tr64_snprintf(text,sizeof(text),"%f",1.5)==-1);
 CHECK(tr64_snprintf(NULL,1,"x")==-1);
 CHECK(raw(text,sizeof(text),"%f",1.5)==23 && !strcmp(text,"FP64:0x3ff8000000000000"));
 uint64_t bits=UINT64_C(0x7ff8000000001234);double value;memcpy(&value,&bits,sizeof(value));
 CHECK(raw(text,sizeof(text),"%.12g",value)==23 && !strcmp(text,"FP64:0x7ff8000000001234"));
 bits=UINT64_C(0x8000000000000000);memcpy(&value,&bits,sizeof(value));
 CHECK(raw(text,sizeof(text),"%e",value)==23 && !strcmp(text,"FP64:0x8000000000000000"));
 CHECK(raw(text,sizeof(text),"%Lf",(long double)1.0)==-1);
 {
  char long_text[2049],expected[4096];sink_t sink={{0},0,0};memset(long_text,'q',sizeof(long_text)-1);long_text[2048]=0;
  int n=snprintf(expected,sizeof(expected),"stream %020llu %s",123ULL,long_text);
  CHECK(streamed(&sink,"stream %020llu %s",123ULL,long_text)==n && sink.size==(uint32_t)n && !memcmp(sink.data,expected,(size_t)n));
  sink.size=0;sink.fail=1;CHECK(streamed(&sink,"stream %u",7u)==-1);
 }
 printf("fmt PASS checks=%u: libc differential integers/flags/width/precision/lengths/truncation/NUL, bounded huge count, %%n rejected, exact FP64 bits, stream/failure\n",checks);
 return 0;
}
