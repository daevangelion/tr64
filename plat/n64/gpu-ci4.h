/* SPDX-License-Identifier: MIT */
#ifndef TR64_GPU_CI4_H
#define TR64_GPU_CI4_H
#include <stdint.h>
typedef struct { uint32_t load_a, load_b, clamp_a, clamp_b; } tr64_ci4_rect_t;
/* Load through an I8 tile, in bytes. Set the draw tile's clamp in texels. */
static inline tr64_ci4_rect_t tr64_ci4_rect(unsigned x,unsigned y,unsigned width,unsigned height)
{
 unsigned left=x/2,right=left+(width+1)/2-1;
 return (tr64_ci4_rect_t){0xf4000000u|(left*4<<12)|(y*4),
                         (1u<<24)|(right*4<<12)|((y+height-1)*4),
                         0xf2000000u, ((width-1)*4<<12)|((height-1)*4)};
}
/* Contiguous whole-width 8-byte-aligned CI4 tiles can use LOAD_BLOCK as 16-bit
 * texels (2 bytes/texel) when the 1.11 DXT line increment stays exact for all
 * rows. Within 2 KiB TMEM (words=row/8 in 1..16), only words=11 (>20 rows) and
 * words=14 (>14 rows) drift before TMEM fills; those fall back to LOAD_TILE. */
static inline int tr64_ci4_can_load_block(unsigned x,unsigned row_bytes,unsigned row,unsigned height)
{
 if(x || row_bytes!=row || (row&7u) || !row || row>128 || !height || height>256 || height*row>2048)return 0;
 unsigned words=row>>3;
 if(words==11 && height>20)return 0;
 if(words==14 && height>14)return 0;
 return 1;
}
static inline tr64_ci4_rect_t tr64_ci4_block(unsigned width,unsigned height,unsigned row)
{
 static const uint16_t dxt[16]={
  2048,1024,683,512,410,342,293,256,
  228,205,187,171,158,147,137,128
 };
 unsigned texels16=(height*row)>>1;
 return (tr64_ci4_rect_t){0xf3000000u,
                         (1u<<24)|((texels16-1)<<12)|dxt[(row>>3)-1],
                         0xf2000000u, ((width-1)*4<<12)|((height-1)*4)};
}
#endif
