/* SPDX-License-Identifier: MIT */
#ifndef TR64_SND_FORMAT_H
#define TR64_SND_FORMAT_H
#include <stdint.h>
static inline uint32_t snd_be32(const void *p)
{
 const uint8_t *b=p;
 return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | b[3];
}
static inline const uint8_t *snd_sample(const void *metadata,uint32_t index)
{ return (const uint8_t *)metadata+64u+index*32u; }
#endif
