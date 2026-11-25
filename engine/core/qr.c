/* SPDX-License-Identifier: MIT */
/* Written from ISO/IEC 18004:2015 (module placement, alphanumeric mode, error
 * correction block structure for version 13-M, format and version information
 * with their BCH codes, mask patterns) and RFC 9285 (base-45). No QR library
 * code is used.
 *
 * Version 13, level M: 69x69 modules, 532 codewords = 8 blocks of 37 data and
 * one of 38, 22 error-correction codewords each, no remainder bits, six
 * alignment patterns (centres 6, 34, 62 except the three that meet a finder),
 * two version information blocks. The framebuffer is the module matrix:
 * function patterns are computed by fixed(), data modules are painted in the
 * standard zigzag order straight from the interleaved codewords, so the only
 * working memory is the 532 codeword bytes. */
#include <string.h>
#include "engine/core/qr.h"
/* Diagnostic code that is resident for good: size over speed. */
#pragma GCC optimize ("Os")

#define N TR64_QR_MODULES
#define BLOCKS 9u
#define SHORT_DATA 37u
#define DATA (BLOCKS*SHORT_DATA+1u)
#define EC 22u
#define MASK 4u
/* Format information (level M, mask MASK): 5 data bits, BCH(15,5) with
 * generator 0x537, XOR 0x5412 (ISO/IEC 18004 7.9). */
static const uint16_t format_bits[8]={0x5412,0x5125,0x5e7c,0x5b4b,0x45f9,0x40ce,0x4f97,0x4aa0};
/* Version information, version 13: 6 bits and BCH(18,6) with generator 0x1F25 (7.10). */
#define VERSION_BITS 0x0d847u
static const char alnum[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
/* Bytes of record that fit: prefix "TR64.2." (7) and the closing '.' (1) take 8
 * characters; each byte pair takes 3, a last odd byte 2. */
#define PACK_BYTES (((TR64_QR_CHARS-8u)/3u)*2u+(((TR64_QR_CHARS-8u)%3u)==2u))
_Static_assert(PACK_BYTES==TR64_QR_FIXED+TR64_QR_ROOM,"record room");

/* ---- record packing: big-endian bytes, then base-45 (RFC 9285 section 4) ---- */
static uint8_t *be(uint8_t *p,uint64_t v,unsigned bytes)
{ while(bytes--)*p++=(uint8_t)(v>>(8u*bytes));return p; }
static int frame_code(uint32_t a,uint32_t *code)
{
 unsigned top=a>>24;
 if((a&3u) || (top!=0x80u && top-0xc0u>1u))return 0;
 *code=(top==0x80u ? 0u : top-0xbfu)<<22|(a&0xffffffu)>>2;return 1;
}
/* Upper-case hex digit: '0'-'9' are 0x30-0x39, 'A'-'F' 0x41-0x46. */
static unsigned hex(char c) { return ((unsigned)c&15u)+((unsigned)c>>6)*9u; }
static uint8_t *words(uint8_t *p,const uint32_t *w)
{ for(unsigned i=0;i<4;++i)p=be(p,w[i],4);return p; }
/* idx, hi low half, hi bits 63:62 and 39:32, EntryLo0, EntryLo1, mask >> 13. */
static uint8_t *entry(uint8_t *p,const tr64_qr_tlb_t *e)
{
 p=be(p,e->index,1);p=be(p,(uint32_t)e->hi,4);
 p=be(p,(uint32_t)((e->hi>>48)&0xc000u)|(uint32_t)((e->hi>>32)&0xffu),2);
 p=be(p,e->lo0,4);p=be(p,e->lo1,4);return be(p,(e->mask>>13)&0xffffu,2);
}
unsigned tr64_qr_pack(char *out,const tr64_qr_record_t *r)
{
 uint8_t b[PACK_BYTES],*p=b;uint32_t w;unsigned i,n=7,nf=0,room,reserve=0,len=0;
 const char *m=r->message;
 *p++=r->kind;p++; /* second byte: frame count and ExcCode, set below */
 for(i=0;i<12;i+=2)*p++=(uint8_t)(hex(r->build[i])<<4|hex(r->build[i+1]));
 p=be(p,r->pc,4);p=be(p,r->ra,4);p=be(p,r->badvaddr,8);p=be(p,r->sr,4);p=be(p,r->cause,4);
 p=be(p,r->epc,4);p=be(p,r->entryhi,8);p=be(p,r->context,8);
 p=be(p,(uint32_t)r->level,2);p=be(p,r->tick,4);p=be(p,r->units,4);
 p=be(p,r->flags,1);p=be(p,r->vec,1);p=be(p,r->count,1);p=be(p,r->tlb_valid,1);p=be(p,r->unit,1);
 p=be(p,r->gpr_wide,4);
 for(i=1;i<32;++i)if(i!=26 && i!=27)p=be(p,r->gpr[i],4);
 p=words(p,r->epc_words);p=words(p,r->entry_words);p=words(p,r->mapped_words);
 p=entry(p,&r->pc_entry);p=entry(p,&r->unit_entry);p=be(p,r->frame,4);
 if(p!=b+TR64_QR_FIXED)return 0;
 if(m)while(m[len] && len<TR64_QR_ROOM)++len;
 reserve=len<TR64_QR_MESSAGE_FIRST ? len : TR64_QR_MESSAGE_FIRST;
 room=PACK_BYTES-TR64_QR_FIXED-reserve;
 while(nf<r->nframes && nf<TR64_QR_FRAMES && 3u*(nf+1u)<=room && frame_code(r->frames[nf],&w)) { p=be(p,w,3);++nf; }
 b[1]=(uint8_t)(nf<<5|(r->code&31u));
 if(m)for(i=0;i<len && p<b+PACK_BYTES;++i)*p++=(uint8_t)m[i];
 memcpy(out,"TR64.2.",7);
 for(i=0;i<(unsigned)(p-b);i+=2) {
  unsigned v=b[i],last=i+1==(unsigned)(p-b);
  if(!last)v=v<<8|b[i+1];
  out[n++]=alnum[v%45u];v/=45u;out[n++]=alnum[v%45u];
  if(!last)out[n++]=alnum[v/45u];
 }
 out[n++]='.';out[n]=0;return n;
}

/* ---- Reed-Solomon over GF(2^8), polynomial x^8+x^4+x^3+x^2+1 (0x11d) ---- */
static uint8_t gmul(uint8_t a,uint8_t b)
{
 unsigned r=0,x=a;
 for(;b;b>>=1) { if(b&1u)r^=x;x<<=1;if(x&256u)x^=0x11du; }
 return (uint8_t)r;
}
/* g(x)=prod(x-a^i), i=0..21, a=2: the 22 non-leading coefficients, highest
 * degree first (ISO/IEC 18004 Annex A lists the same polynomial by exponent). */
static const uint8_t gen[EC]={89,179,131,176,182,244,19,189,69,40,28,137,29,123,67,253,86,218,230,26,145,245};
/* Remainder of data*x^22 modulo g(x) by the shift register division. */
static void ec_block(const uint8_t *data,unsigned length,uint8_t *rem)
{
 unsigned i,j;
 memset(rem,0,EC);
 for(i=0;i<length;++i) {
  uint8_t f=data[i]^rem[0];
  for(j=0;j<EC;++j)rem[j]=(uint8_t)((j+1<EC ? rem[j+1] : 0u)^gmul(gen[j],f));
 }
}

/* ---- symbol layout ---- */
/* Function module (0 light, 1 dark) at column x, row y, or -1 for a data
 * module: finder patterns with separators, timing patterns, the six
 * alignment patterns, format and version information and the always-dark
 * module. */
static unsigned ring(int dx,int dy)
{
 dx=dx<0 ? -dx : dx;dy=dy<0 ? -dy : dy;
 return (unsigned)(dx>dy ? dx : dy);
}
/* Alignment centre (6, 34 or 62) nearest to v. */
static unsigned centre(unsigned v) { return v<20u ? 6u : v<48u ? 34u : 62u; }
static int fixed(unsigned x,unsigned y)
{
 /* The three finders are mirror images: fold onto the nearest corner. */
 unsigned fx=x<N-1u-x ? x : N-1u-x,fy=y<N-1u-y ? y : N-1u-y,bit,ax,ay,r;
 if(fx<8 && fy<8 && (x<8 || y<8))return fx<7 && fy<7 && ring((int)fx-3,(int)fy-3)!=2;
 if(y<6 && x>=N-11u && x<=N-9u)return (VERSION_BITS>>(y*3u+x-(N-11u)))&1u;
 if(x<6 && y>=N-11u && y<=N-9u)return (VERSION_BITS>>(x*3u+y-(N-11u)))&1u;
 if(x==6)return !(y&1u);
 if(y==6)return !(x&1u);
 if(x==8 && y<=8)bit=y<6 ? y : y-1u;
 else if(y==8 && x<8)bit=x==7 ? 8u : 14u-x;
 else if(y==8 && x>=N-8)bit=N-1u-x;
 else if(x==8 && y>=N-8) {
  if(y==N-8)return 1;
  bit=y-(N-15u);
 }
 else {
  ax=centre(x);ay=centre(y);r=ring((int)x-(int)ax,(int)y-(int)ay);
  if(r>2 || (ax==6 && (ay==6 || ay==62)) || (ax==62 && ay==6))return -1;
  return r!=1;
 }
 return (format_bits[MASK]>>bit)&1;
}
static int mask_hit(unsigned x,unsigned y)
{
 /* ISO/IEC 18004 table 10 with i=row, j=column. */
 switch(MASK) {
 case 0:return !((x+y)&1u);
 case 1:return !(y&1u);
 case 2:return !(x%3u);
 case 3:return !((x+y)%3u);
 case 4:return !((y/2u+x/3u)&1u);
 case 5:return !((x*y)%2u+(x*y)%3u);
 case 6:return !(((x*y)%2u+(x*y)%3u)&1u);
 default:return !((((x+y)&1u)+(x*y)%3u)&1u);
 }
}

typedef struct { uint16_t *fb;unsigned stride,x,y,scale;uint16_t dark; } canvas_t;
static void dot(const canvas_t *c,unsigned mx,unsigned my)
{
 unsigned px=c->x+(mx+TR64_QR_QUIET)*c->scale,py=c->y+(my+TR64_QR_QUIET)*c->scale,i,j;
 for(j=0;j<c->scale;++j)for(i=0;i<c->scale;++i)c->fb[(py+j)*c->stride+px+i]=c->dark;
}

/* Build the 334 data codewords: mode 0010, 11 bit count, 11 bit pairs, a 6 bit
 * final character, terminator, byte padding, then 0xEC/0x11 pad codewords. */
static int data_words(uint8_t *d,const char *text,unsigned length)
{
 unsigned pos=0,item,i,k,v,bits;
 const char *p;
 memset(d,0,DATA);
 if(length>TR64_QR_CHARS)return 0;
 for(item=0;item<2u+(length+1u)/2u;++item) {
  if(item==0) { v=2;bits=4; }
  else if(item==1) { v=length;bits=11; }
  else {
   v=bits=0;
   for(k=0;k<2 && 2u*(item-2u)+k<length;++k) {
    p=memchr(alnum,(unsigned char)text[2u*(item-2u)+k],45);
    if(!p)return 0;
    v=v*45u+(unsigned)(p-alnum);bits+=k ? 5u : 6u;
   }
  }
  while(bits--) {
   if((v>>bits)&1u)d[pos>>3]|=(uint8_t)(0x80u>>(pos&7u));
   ++pos;
  }
 }
 pos+=DATA*8u-pos<4u ? DATA*8u-pos : 4u;
 for(i=(pos+7u)/8u,k=0;i<DATA;++i,++k)d[i]=k&1u ? 0x11u : 0xecu;
 return 1;
}

int tr64_qr_draw(uint16_t *fb,unsigned stride,unsigned x,unsigned y,unsigned scale,
                const char *text,unsigned length,uint16_t dark,uint16_t light)
{
 uint8_t data[DATA],ec[BLOCKS*EC];
 canvas_t c={fb,stride,x,y,scale,dark};
 unsigned i,j,k=0,edge=TR64_QR_EDGE*scale;
 int col;
 if(!data_words(data,text,length))return 0;
 for(i=0;i<BLOCKS;++i)ec_block(data+i*SHORT_DATA,i==BLOCKS-1u ? SHORT_DATA+1u : SHORT_DATA,ec+i*EC);
 for(j=0;j<edge;++j)for(i=0;i<edge;++i)fb[(y+j)*stride+x+i]=light;
 /* One walk over every module: column pairs from the lower right, upward
  * first, the vertical timing column 6 alone (it holds no data); a data
  * module takes the next bit of the codewords, interleaved block by block
  * (the 38th data byte only exists in the last block). */
 for(col=N-1;col>=0;col-=col==6 ? 1 : 2) {
  for(i=0;i<N;++i)for(j=0;j<(col==6 ? 1u : 2u);++j) {
   unsigned cx=(unsigned)col-j,cy=((col+1)&2) ? i : N-1u-i;
   int v=fixed(cx,cy);
   if(v<0) {
    unsigned w=k>>3,b=0;
    if(w<DATA+BLOCKS*EC) {
     if(w<BLOCKS*SHORT_DATA)b=data[(w%BLOCKS)*SHORT_DATA+w/BLOCKS];
     else if(w<DATA)b=data[w];
     else b=ec[((w-DATA)%BLOCKS)*EC+(w-DATA)/BLOCKS];
    }
    v=(int)(((b>>(7u-(k&7u)))&1u)^(unsigned)mask_hit(cx,cy));
    ++k;
   }
   if(v)dot(&c,cx,cy);
  }
 }
 return 1;
}
