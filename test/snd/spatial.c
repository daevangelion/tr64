/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "engine/snd/spatial.h"
int main(void)
{
 tr64_game_view_t v;int32_t p[3];uint32_t turn,range,gain;uint16_t left,right;
 while(1) {
  memset(&v,0,sizeof(v));
  int fields=scanf("%"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %u %u %u",
   p,p+1,p+2,v.camera_eye,v.camera_eye+1,v.camera_eye+2,&turn,&range,&gain);
  if(fields==EOF)return 0;
  if(fields!=9 || turn>3 || range>1 || gain>4096)return 2;
  static const int32_t axis[4][2]={{16384,0},{0,16384},{-16384,0},{0,-16384}};
  v.camera_basis[0]=axis[turn][0];v.camera_basis[2]=-axis[turn][1];
  v.camera_basis[4]=16384;v.camera_basis[6]=axis[turn][1];v.camera_basis[8]=axis[turn][0];
  tr64_snd_spatial_gain(&v,p,gain,(int)range,&left,&right);
  printf("%u %u\n",left,right);
 }
}
