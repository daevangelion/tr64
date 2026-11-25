/* SPDX-License-Identifier: MIT */
#ifndef TR64_FMT_H
#define TR64_FMT_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
/* snprintf count/termination semantics; returns-1 for invalid format/count
 * overflow. No heap/locale/errno. Integer d/i/u/o/x/X, p,c,s,%%; flags,width,
 * precision and hh/h/l/ll/z/t/j. Floating conversions and %n are rejected. */
int tr64_vsnprintf(char *,size_t,const char *,va_list) __attribute__((format(printf,3,0)));
int tr64_snprintf(char *,size_t,const char *,...) __attribute__((format(printf,3,4)));
/* Shipping SDK diagnostics additionally serialize double arguments as explicit
 * FP64:0x<16 IEEE bits>, NOT decimal floating printf semantics. No FP math.
 * Long double/wide strings/%n are unsupported. Original integer semantics stay
 * identical. Writer returns0 on success,-1 on output failure. */
typedef int (*tr64_fmt_writer_t)(void *,const char *,uint32_t);
int tr64_debug_vsnprintf(char *,size_t,const char *,va_list) __attribute__((format(printf,3,0)));
int tr64_debug_vformat(tr64_fmt_writer_t,void *,const char *,va_list) __attribute__((format(printf,3,0)));
#endif
