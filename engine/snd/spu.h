/* SPDX-License-Identifier: MIT */
/* Original integer decoder from public SPU/XA recurrence, not a platform port.
 * https://psx-spx.consoledev.net/soundprocessingunitspu/#spu-adpcm-samples */
#ifndef TR64_SND_SPU_H
#define TR64_SND_SPU_H
#include <stdint.h>
#include "tr64/eng.h"
static inline void snd_spu_block(int16_t out[28],const uint8_t raw[16],int32_t history[2])
{
 static const int16_t filters[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
 uint32_t i,predictor=raw[0]>>4,shift=raw[0]&15u;int32_t h1=history[0],h2=history[1];
 int32_t a,b;
 if(predictor>4 || shift>12 || raw[1]>7)sys_error("audio invalid SPU block");
 a=filters[predictor][0];b=filters[predictor][1];
 for(i=0;i<28;++i) {
  uint32_t nibble=(raw[2+i/2]>>((i&1u)*4))&15u;
  int32_t signed_nibble=(int32_t)(nibble^8u)-8;
  int32_t value=((signed_nibble*4096)>>shift)+((h1*a+h2*b+32)>>6);
  if(value>32767)value=32767;else if(value< -32768)value= -32768;
  out[i]=(int16_t)value;h2=h1;h1=value;
 }
 history[0]=h1;history[1]=h2;
}
#endif
