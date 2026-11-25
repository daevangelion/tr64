/* SPDX-License-Identifier: MIT */
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "engine/core/fmt.h"
typedef struct {
 char *dst;size_t cap;uint32_t count;
 tr64_fmt_writer_t write;void *opaque;int failed;
} output_t;
static void bytes(output_t *o,const char *s,size_t n)
{
 if(o->failed)return;
 if(n>(uint32_t)INT_MAX-o->count) { o->failed=1;return; }
 if(o->write) {
  if(n && o->write(o->opaque,s,(uint32_t)n)) { o->failed=1;return; }
 } else if(o->cap && o->count<o->cap-1) {
  size_t take=o->cap-1-o->count;if(take>n)take=n;
  memcpy(o->dst+o->count,s,take);
 }
 o->count+=(uint32_t)n;
}
static void padding(output_t *o,char c,uint32_t n)
{
 static const char spaces[]="                                ";
 static const char zeroes[]="00000000000000000000000000000000";
 if(o->failed || !n)return;
 if(n>(uint32_t)INT_MAX-o->count) { o->failed=1;return; }
 if(o->write) {
  const char *block=c=='0'?zeroes:spaces;
  while(n && !o->failed) { uint32_t take=n>32u?32u:n;bytes(o,block,take);n-=take; }
 } else {
  if(o->cap && o->count<o->cap-1) {
   size_t take=o->cap-1-o->count;if(take>n)take=n;
   memset(o->dst+o->count,c,take);
  }
  o->count+=n;
 }
}
static int number(const char **p,uint32_t *value)
{
 uint32_t n=0;
 while(**p>='0' && **p<='9') {
  uint32_t digit=(unsigned char)*(*p)++-'0';
  if(n>((uint32_t)INT_MAX-digit)/10u)return 0;
  n=n*10u+digit;
 }
 *value=n;return 1;
}
enum length { NONE,HH,H,L,LL,Z,T,J,BIG_L };
static int format(output_t *o,const char *p,va_list ap,int raw_fp)
{
 va_list args;va_copy(args,ap);
 if(!p) { o->failed=1;goto done; }
 while(*p && !o->failed) {
  const char *literal=p;uint32_t width=0,precision=0;int have_precision=0;
  int left=0,zero=0,plus=0,space=0,alternate=0;enum length length=NONE;
  while(*p && *p!='%')++p;
  bytes(o,literal,(size_t)(p-literal));if(!*p)break;
  ++p;if(*p=='%') { bytes(o,p++,1);continue; }
  for(;;++p) {
   if(*p=='-')left=1;else if(*p=='0')zero=1;else if(*p=='+')plus=1;
   else if(*p==' ')space=1;else if(*p=='#')alternate=1;else break;
  }
  if(*p=='*') {
   int w=va_arg(args,int);++p;
   if(w==INT_MIN) { o->failed=1;break; }
   if(w<0) { left=1;w=-w; }width=(uint32_t)w;
  } else if(!number(&p,&width)) { o->failed=1;break; }
  if(*p=='.') {
   ++p;have_precision=1;
   if(*p=='*') {
    int v=va_arg(args,int);++p;
    if(v<0)have_precision=0;else precision=(uint32_t)v;
   } else if(!number(&p,&precision)) { o->failed=1;break; }
  }
  if(*p=='h') { ++p;length=H;if(*p=='h') { ++p;length=HH; } }
  else if(*p=='l') { ++p;length=L;if(*p=='l') { ++p;length=LL; } }
  else if(*p=='z') { ++p;length=Z; }
  else if(*p=='t') { ++p;length=T; }
  else if(*p=='j') { ++p;length=J; }
  else if(*p=='L') { ++p;length=BIG_L; }
  char conversion=*p;
  if(!conversion) { o->failed=1;break; }++p;
  if(conversion=='s' || conversion=='c') {
   const char *s;char c;size_t n=0,limit=have_precision?precision:SIZE_MAX;
   if(length!=NONE) { o->failed=1;break; }
   if(conversion=='c') { c=(char)va_arg(args,int);s=&c;n=1; }
   else {
    s=va_arg(args,const char *);if(!s)s="(null)";
    while(n<limit && s[n])++n;
   }
   uint32_t pad=width>n?width-(uint32_t)n:0;
   if(!left)padding(o,' ',pad);
   bytes(o,s,n);
   if(left)padding(o,' ',pad);
  } else if(conversion=='f' || conversion=='F' || conversion=='e' || conversion=='E' ||
            conversion=='g' || conversion=='G' || conversion=='a' || conversion=='A') {
   uint64_t bits;char token[23];double value;
   if(!raw_fp || (length!=NONE && length!=L)) { o->failed=1;break; }
   _Static_assert(sizeof(double)==sizeof(uint64_t),"SDK diagnostic IEEE double ABI");
   value=va_arg(args,double);memcpy(&bits,&value,sizeof(bits));memcpy(token,"FP64:0x",7);
   for(unsigned i=0;i<16;++i)token[7+i]="0123456789abcdef"[(bits>>(60-4*i))&15u];
   uint32_t pad=width>sizeof(token)?width-(uint32_t)sizeof(token):0;
   if(!left)padding(o,' ',pad);
   bytes(o,token,sizeof(token));
   if(left)padding(o,' ',pad);
  } else {
   uint64_t value=0;int negative=0,signed_value=conversion=='d' || conversion=='i';
   unsigned base=10;char storage[32],prefix[2],*end=storage+sizeof(storage),*digits=end;
   uint32_t prefix_size=0;
   if(length==BIG_L) { o->failed=1;break; }
   if(conversion=='p') {
    if(length!=NONE) { o->failed=1;break; }
    value=(uintptr_t)va_arg(args,void *);base=16;prefix[0]='0';prefix[1]='x';prefix_size=2;
   } else if(signed_value) {
    intmax_t v;
    switch(length) {
    case HH:v=(signed char)va_arg(args,int);break;case H:v=(short)va_arg(args,int);break;
    case L:v=va_arg(args,long);break;case LL:v=va_arg(args,long long);break;
    case Z:case T:v=va_arg(args,ptrdiff_t);break;case J:v=va_arg(args,intmax_t);break;
    default:v=va_arg(args,int);break;
    }
    negative=v<0;value=negative?UINT64_C(0)-(uint64_t)v:(uint64_t)v;
    if(negative || plus || space) { prefix[0]=negative?'-':plus?'+':' ';prefix_size=1; }
   } else if(conversion=='u' || conversion=='o' || conversion=='x' || conversion=='X') {
    switch(length) {
    case HH:value=(unsigned char)va_arg(args,unsigned);break;case H:value=(unsigned short)va_arg(args,unsigned);break;
    case L:value=va_arg(args,unsigned long);break;case LL:value=va_arg(args,unsigned long long);break;
    case Z:value=va_arg(args,size_t);break;case T:value=va_arg(args,uintptr_t);break;
    case J:value=va_arg(args,uintmax_t);break;default:value=va_arg(args,unsigned);break;
    }
    base=conversion=='o'?8u:conversion=='u'?10u:16u;
    if(alternate && base==16 && value) { prefix[0]='0';prefix[1]=conversion;prefix_size=2; }
   } else { o->failed=1;break; }
   if(value || !have_precision || precision || conversion=='p') {
    uint64_t v=value;const char *alphabet=conversion=='X'?"0123456789ABCDEF":"0123456789abcdef";
    do { *--digits=alphabet[v%base];v/=base; } while(v);
   }
   uint32_t n=(uint32_t)(end-digits),leading=have_precision && precision>n?precision-n:0;
   if(alternate && base==8 && !leading && (!n || *digits!='0')) { prefix[0]='0';prefix_size=1; }
   uint32_t total=prefix_size+leading+n,pad=width>total?width-total:0;
   if(!left && (!zero || have_precision))padding(o,' ',pad);
   bytes(o,prefix,prefix_size);
   if(!left && zero && !have_precision)padding(o,'0',pad);
   padding(o,'0',leading);bytes(o,digits,n);if(left)padding(o,' ',pad);
  }
 }
 done:
 va_end(args);
 if(!o->write && o->cap)o->dst[o->count<o->cap?o->count:o->cap-1]=0;
 return o->failed?-1:(int)o->count;
}
static int buffer(char *dst,size_t capacity,const char *fmt,va_list ap,int raw_fp)
{
 output_t o={dst,capacity,0,NULL,NULL,0};
 if(capacity && !dst)return -1;
 return format(&o,fmt,ap,raw_fp);
}
int tr64_vsnprintf(char *dst,size_t capacity,const char *fmt,va_list ap)
{ return buffer(dst,capacity,fmt,ap,0); }
int tr64_snprintf(char *dst,size_t capacity,const char *fmt,...)
{
 va_list ap;int result;va_start(ap,fmt);result=tr64_vsnprintf(dst,capacity,fmt,ap);va_end(ap);return result;
}
int tr64_debug_vsnprintf(char *dst,size_t capacity,const char *fmt,va_list ap)
{ return buffer(dst,capacity,fmt,ap,1); }
int tr64_debug_vformat(tr64_fmt_writer_t write,void *opaque,const char *fmt,va_list ap)
{
 output_t o={NULL,0,0,write,opaque,0};if(!write)return -1;
 return format(&o,fmt,ap,1);
}
