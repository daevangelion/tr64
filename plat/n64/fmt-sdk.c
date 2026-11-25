/* SPDX-License-Identifier: MIT */
/* SDK stdio integration, test and shipping builds alike. Standard streams go straight to the
 * existing raw emulog sink, never a console/FILE buffer or allocation. Writable
 * non-console FILE streams and libc abort() route to sys_error so newlib stdio
 * and signal machinery stay out of the cart image.
 * FP diagnostic arguments are explicit IEEE bits, not decimal printf values. */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "build_id.h"
#include "engine/core/fmt.h"
#include "engine/core/sys.h"
static int output(void *opaque,const char *bytes,uint32_t count)
{
 FILE *stream=opaque;
 if(stream==stdout || stream==stderr) { sys_log_write(bytes,count);return 0; }
 sys_error("stdio: non-console stream write");
}
int __wrap_vfprintf(FILE *stream,const char *fmt,va_list ap)
{ return tr64_debug_vformat(output,stream,fmt,ap); }
int __wrap_fprintf(FILE *stream,const char *fmt,...)
{
 va_list ap;int n;va_start(ap,fmt);n=__wrap_vfprintf(stream,fmt,ap);va_end(ap);return n;
}
int __wrap_vprintf(const char *fmt,va_list ap)
{ return __wrap_vfprintf(stdout,fmt,ap); }
int __wrap_printf(const char *fmt,...)
{
 va_list ap;int n;va_start(ap,fmt);n=__wrap_vprintf(fmt,ap);va_end(ap);return n;
}
int __wrap_vsnprintf(char *dst,size_t capacity,const char *fmt,va_list ap)
{ return tr64_debug_vsnprintf(dst,capacity,fmt,ap); }
int __wrap_snprintf(char *dst,size_t capacity,const char *fmt,...)
{
 va_list ap;int n;va_start(ap,fmt);n=__wrap_vsnprintf(dst,capacity,fmt,ap);va_end(ap);return n;
}
int __wrap_vsprintf(char *dst,const char *fmt,va_list ap)
{ return tr64_debug_vsnprintf(dst,SIZE_MAX,fmt,ap); }
int __wrap_sprintf(char *dst,const char *fmt,...)
{
 va_list ap;int n;va_start(ap,fmt);n=__wrap_vsprintf(dst,fmt,ap);va_end(ap);return n;
}
size_t __wrap_fwrite(const void *ptr,size_t size,size_t count,FILE *stream)
{
 if(stream!=stdout && stream!=stderr)sys_error("stdio: non-console stream write");
 if(!size || !count || count>SIZE_MAX/size)return 0;
 size_t left=size*count;const char *p=ptr;
 while(left) {
  uint32_t take=left>(uint32_t)INT_MAX?(uint32_t)INT_MAX:(uint32_t)left;
  sys_log_write(p,take);p+=take;left-=take;
 }
 return count;
}
int __wrap_fputc(int value,FILE *stream)
{
 unsigned char c=(unsigned char)value;
 return output(stream,(const char *)&c,1)?EOF:(int)c;
}
int __wrap_putchar(int value) { return __wrap_fputc(value,stdout); }
int __wrap_fputs(const char *text,FILE *stream)
{
 size_t n=strlen(text);if(n>(uint32_t)INT_MAX)return EOF;
 return output(stream,text,(uint32_t)n)?EOF:0;
}
int __wrap_puts(const char *text)
{ if(__wrap_fputs(text,stdout)==EOF || __wrap_fputc('\n',stdout)==EOF)return EOF;return 0; }
int __wrap_setvbuf(FILE *stream,char *buf,int mode,size_t size)
{
 (void)buf;(void)mode;(void)size;
 if(stream==stdout || stream==stderr)return 0;
 sys_error("stdio: non-console setvbuf");
}
void __wrap_abort(void) __attribute__((noreturn));
void __wrap_abort(void) { sys_error("abort"); }
