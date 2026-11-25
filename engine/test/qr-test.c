/* SPDX-License-Identifier: MIT */
/* Host driver for test/run/qr-tests.py, never linked into the engine.
 *   qr-test draw  : each stdin line is a text; stdout gets one status byte
 *                   (1 painted, 0 rejected) and, if painted, the 231x231 pixel
 *                   symbol at scale 3 as bytes (0 dark, 255 light).
 *   qr-test pack  : each stdin line, all numbers hex except level (decimal):
 *                     kind code nframes flags vec count tlbvalid unit build
 *                     pc ra badhi badlo sr cause epc ehhi ehlo ctxhi ctxlo
 *                     level tick units frame gprwide g0..g31 e0..e3 n0..n3
 *                     m0..m3 pcentry unitentry f1..fn message
 *                   (entry = index hihi hilo lo0 lo1 mask; message hex or '-')
 *                   prints the packed text. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/core/qr.h"
#define SCALE 3u
#define EDGE (TR64_QR_EDGE*SCALE)
static uint16_t fb[EDGE*EDGE];
static int draw(void)
{
 static char line[4096];
 while(fgets(line,sizeof(line),stdin)) {
  size_t n=strcspn(line,"\r\n");static unsigned char out[EDGE*EDGE];int ok;
  memset(fb,0x55,sizeof(fb));
  ok=tr64_qr_draw(fb,EDGE,0,0,SCALE,line,(unsigned)n,0x0001u,0xffffu);
  putchar(ok);
  if(ok) {
   for(unsigned i=0;i<EDGE*EDGE;++i) {
    if(fb[i]!=0x0001u && fb[i]!=0xffffu) { fprintf(stderr,"stray pixel %04x at %u\n",fb[i],i);return 1; }
    out[i]=fb[i]==0x0001u ? 0 : 255;
   }
   fwrite(out,1,sizeof(out),stdout);
  } else {
   for(unsigned i=0;i<EDGE*EDGE;++i)if(fb[i]!=0x5555u) { fprintf(stderr,"rejected text painted pixels\n");return 1; }
  }
 }
 return 0;
}
static uint64_t hex(char **p) { return strtoull(*p,p,16); }
static void entry(char **p,tr64_qr_tlb_t *e)
{
 e->index=(uint8_t)hex(p);
 {uint64_t hi=hex(p);e->hi=hi<<32|hex(p);}
 e->lo0=(uint32_t)hex(p);e->lo1=(uint32_t)hex(p);e->mask=(uint32_t)hex(p);
}
static int pack(void)
{
 static char line[8192];
 while(fgets(line,sizeof(line),stdin)) {
  static tr64_qr_record_t r;char *p=line,*end,text[TR64_QR_CHARS+1],build[16],message[512];
  uint64_t hi;
  memset(&r,0,sizeof(r));
  r.kind=(uint8_t)hex(&p);r.code=(uint8_t)hex(&p);r.nframes=(uint8_t)hex(&p);r.flags=(uint8_t)hex(&p);
  r.vec=(uint8_t)hex(&p);r.count=(uint8_t)hex(&p);r.tlb_valid=(uint8_t)hex(&p);r.unit=(uint8_t)hex(&p);
  while(*p==' ')++p;
  memcpy(build,p,12);build[12]=0;p+=12;r.build=build;
  r.pc=(uint32_t)hex(&p);r.ra=(uint32_t)hex(&p);
  hi=hex(&p);r.badvaddr=hi<<32|hex(&p);
  r.sr=(uint32_t)hex(&p);r.cause=(uint32_t)hex(&p);r.epc=(uint32_t)hex(&p);
  hi=hex(&p);r.entryhi=hi<<32|hex(&p);hi=hex(&p);r.context=hi<<32|hex(&p);
  r.level=(int32_t)strtol(p,&p,10);
  r.tick=(uint32_t)hex(&p);r.units=(uint32_t)hex(&p);r.frame=(uint32_t)hex(&p);r.gpr_wide=(uint32_t)hex(&p);
  for(unsigned i=0;i<32;++i)r.gpr[i]=(uint32_t)hex(&p);
  for(unsigned i=0;i<4;++i)r.epc_words[i]=(uint32_t)hex(&p);
  for(unsigned i=0;i<4;++i)r.entry_words[i]=(uint32_t)hex(&p);
  for(unsigned i=0;i<4;++i)r.mapped_words[i]=(uint32_t)hex(&p);
  entry(&p,&r.pc_entry);entry(&p,&r.unit_entry);
  if(r.nframes>TR64_QR_FRAMES)r.nframes=TR64_QR_FRAMES;
  for(unsigned i=0;i<r.nframes;++i)r.frames[i]=(uint32_t)hex(&p);
  while(*p==' ')++p;
  end=p+strcspn(p,"\r\n");*end=0;
  if(strcmp(p,"-")) {
   size_t n=strlen(p)/2;
   for(size_t i=0;i<n;++i) { char h[3]={p[2*i],p[2*i+1],0};message[i]=(char)strtoul(h,NULL,16); }
   message[n]=0;r.message=message;
  }
  {
   unsigned n=tr64_qr_pack(text,&r);
   if(n>TR64_QR_CHARS || strlen(text)!=n) { fprintf(stderr,"pack length %u\n",n);return 1; }
   puts(text);
  }
 }
 return 0;
}
int main(int argc,char **argv)
{
 if(argc==2 && !strcmp(argv[1],"draw"))return draw();
 if(argc==2 && !strcmp(argv[1],"pack"))return pack();
 fprintf(stderr,"usage: qr-test draw|pack\n");return 2;
}
