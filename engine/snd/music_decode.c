/* SPDX-License-Identifier: MIT */
#include "engine/snd/music_decode.h"
#include "engine/snd/spu.h"
#include "engine/core/sys.h"
#include <string.h>
void tr64_music_decoder_init(tr64_music_decoder_t *state,uint32_t codec)
{
    if(codec!=TR64_MUSIC_SPU)sys_error("music invalid codec %lu",(unsigned long)codec);
    memset(state,0,sizeof(*state));state->unit=UINT32_MAX;
}
int16_t tr64_music_decoder_sample(tr64_music_decoder_t *s,const uint8_t *raw,
                                  uint32_t unit,uint32_t sample)
{
    if(sample>=28)sys_error("music SPU sample bound");
    if(unit!=s->unit) {
        if((s->unit==UINT32_MAX && unit!=0) ||
           (s->unit!=UINT32_MAX && unit!=s->unit+1))
            sys_error("music nonsequential decoder unit");
        if(raw[1])sys_error("music SPU block flags");
        snd_spu_block(s->decoded,raw,s->history);s->unit=unit;
    }
    return s->decoded[sample];
}
