/* SPDX-License-Identifier: MIT */
/* Original integer implementation from documented mathematical facts.
 * Pan/attenuation rule reference: OpenLara main src/sound.h:36-40,973-988;
 * listener axes: src/utils.h:470,653-655,776-784 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. No BSD source text copied. */
#include "engine/snd/spatial.h"
static uint32_t root(uint64_t value)
{
 uint64_t result=0,bit=UINT64_C(1)<<62;
 while(bit>value)bit>>=2;
 while(bit) {
  if(value>=result+bit) { value-=result+bit;result=(result>>1)+bit; }
  else result>>=1;
  bit>>=2;
 }
 return (uint32_t)result;
}
static int32_t unit_dot(const int32_t axis[3],const int32_t n[3])
{
 int64_t dot=((int64_t)axis[0]*n[0]+(int64_t)axis[1]*n[1]+(int64_t)axis[2]*n[2])/16384;
 return dot>16384 ? 16384 : dot< -16384 ? -16384 : (int32_t)dot;
}
void tr64_snd_spatial_gain(const tr64_game_view_t *view,const int32_t position[3],
                           uint32_t gain,int attenuate,uint16_t *left,uint16_t *right)
{
 int64_t d[3];int32_t n[3],scaled[3],pan,forward;
 uint64_t maximum=0;uint32_t i,shift=0,length,squared=0,l,r,facing,distance=16384;
 if(!view) { *left=*right=(uint16_t)gain;return; }
 for(i=0;i<3;++i) {
  uint64_t magnitude;
  d[i]=(int64_t)position[i]-view->camera_eye[i];
  magnitude=(uint64_t)(d[i]<0 ? -d[i] : d[i]);
  if(magnitude>maximum)maximum=magnitude;
 }
 if(attenuate && maximum>=8192) { *left=*right=0;return; }
 while((maximum>>shift)>32767)++shift;
 for(i=0;i<3;++i) {
  scaled[i]=(int32_t)(d[i]/((int64_t)1<<shift));
  squared+=(uint32_t)(scaled[i]*scaled[i]);
 }
 length=root((uint64_t)squared<<28);
 for(i=0;i<3;++i)n[i]=length ? (int32_t)((int64_t)scaled[i]*268435456/length) : 0;
 if(attenuate)distance=length>=8192u*16384u ? 0 : 16384-length/8192;
 pan=unit_dot(view->camera_basis,n);
 /* Main m.dir points from target to eye; fixed camera_basis row2 is forward. */
 forward=unit_dot(view->camera_basis+6,n);
 facing=(uint32_t)((17*16384+3*forward)/20);
 l=16384-(pan>0 ? (uint32_t)pan*7/10 : 0);
 r=16384-(pan<0 ? (uint32_t)-pan*7/10 : 0);
 gain=(uint32_t)((uint64_t)gain*distance/16384);
 *left=(uint16_t)((uint64_t)gain*l*facing/(16384u*16384u));
 *right=(uint16_t)((uint64_t)gain*r*facing/(16384u*16384u));
}
