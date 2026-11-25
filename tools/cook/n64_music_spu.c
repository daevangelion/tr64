/* SPDX-License-Identifier: MIT */
/* Cook-only original encoder for the documented SPU recurrence.
 * Input: signed16 LE mono PCM on stdin. Output: raw16-byte SPU blocks.
 * Chooses predictor/shift by decoded squared error; no retail code used.
 * Decoder recurrence: psx-spx.consoledev.net/soundprocessingunitspu/ */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static const int filters[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
static int clamp(int value) { return value>32767?32767:value< -32768?-32768:value; }
int main(void)
{
    int history[2]={0,0};
    for(;;) {
        int samples[28],n=0,i,p,shift,best_h1=0,best_h2=0;
        uint8_t best[16]={0};
        uint64_t best_error=UINT64_MAX;
        for(i=0;i<28;++i) {
            int lo=getchar(),hi;
            if(lo==EOF)break;
            hi=getchar();if(hi==EOF){fputs("odd PCM input\n",stderr);return 1;}
            samples[n++]=(int16_t)((unsigned)lo|((unsigned)hi<<8));
        }
        if(!n)break;
        for(i=n;i<28;++i)samples[i]=0;
        for(p=0;p<5;++p)for(shift=0;shift<=12;++shift) {
            int h1=history[0],h2=history[1],step=4096>>shift;
            uint64_t error=0;
            uint8_t encoded[16]={0};
            encoded[0]=(uint8_t)(p*16+shift);
            for(i=0;i<28;++i) {
                int prediction=(h1*filters[p][0]+h2*filters[p][1]+32)>>6;
                int residual=samples[i]-prediction;
                int q=residual>=0?(residual+step/2)/step:-((-residual+step/2)/step);
                int decoded,d;
                if(q>7)q=7;else if(q< -8)q=-8;
                decoded=clamp(prediction+q*step);
                d=samples[i]-decoded;error+=(uint64_t)((int64_t)d*d);
                encoded[2+i/2]|=(uint8_t)((q&15)<<((i&1)*4));
                h2=h1;h1=decoded;
            }
            if(error<best_error) {
                best_error=error;memcpy(best,encoded,16);best_h1=h1;best_h2=h2;
            }
        }
        if(fwrite(best,1,16,stdout)!=16)return 1;
        history[0]=best_h1;history[1]=best_h2;
    }
    return ferror(stdin)||fflush(stdout)==EOF;
}
