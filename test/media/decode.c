/* SPDX-License-Identifier: MIT */
#include "engine/snd/music_decode.h"
#include "engine/snd/music_format.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void sys_error(const char *format,...)
{
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
    fputc('\n',stderr);exit(3);
}
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
int main(int argc,char **argv)
{
    uint8_t metadata[208],raw[512];tr64_music_format_t format;
    tr64_music_decoder_t decoder;uint32_t i,n,unit=0,done=0,total,prefix;
    FILE *input;
    if(argc!=2 || !(input=fopen(argv[1],"rb")))return 2;
    if(fread(metadata,1,64,input)!=64)return 2;
    total=be32(metadata+8);prefix=be32(metadata+36);
    if(prefix>sizeof(metadata) || (prefix!=0 && prefix<64))return 2;
    if(prefix>64 && fread(metadata+64,1,prefix-64,input)!=prefix-64)return 2;
    tr64_music_format(metadata,prefix?prefix:64,total,be32(metadata+16),&format);
    if(format.flags)return 0;
    tr64_music_decoder_init(&decoder,format.codec);
    while(done<format.frames) {
        if(fread(raw,1,format.unit_bytes,input)!=format.unit_bytes)return 2;
        n=format.frames-done;if(n>format.unit_frames)n=format.unit_frames;
        for(i=0;i<n;++i) {
            uint16_t value=(uint16_t)tr64_music_decoder_sample(&decoder,raw,unit,i);
            if(putchar(value&255u)==EOF || putchar(value>>8)==EOF)return 2;
        }
        done+=n;++unit;
    }
    if(fgetc(input)!=EOF)return 2;
    return fclose(input)!=0 || fflush(stdout)==EOF;
}
