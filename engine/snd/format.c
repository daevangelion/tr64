/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "engine/snd/snd.h"
#include "engine/snd/format.h"
int tr64_snd_validate_metadata(const void *data,uint32_t bytes,uint32_t total,
                              const tr64_level_t *level)
{
 const uint8_t *p=data;uint32_t count,missing,offset,cursor,i,j,last_sound=0,last_sample=0;
#if defined(TR64_HOST) || TR64_GAME >= 2
 unsigned tr2=level && (level->game==2 || level->game==3);
#else
 unsigned tr2=level && level->game==2;
#endif
 uint32_t capacity=TR64_LIMIT(sound_metadata,tr2);
 if(!p || bytes<64 || bytes>capacity || total<bytes || memcmp(p,"SN64",4) ||
    snd_be32(p+4)!=1 || snd_be32(p+8)!=total || snd_be32(p+12)!=bytes ||
    snd_be32(p+20)!=32 || snd_be32(p+24)!=64 || snd_be32(p+36)!=4096)return 0;
 count=snd_be32(p+16);missing=snd_be32(p+28);offset=snd_be32(p+32);
 if(count>TR64_LIMIT(sound_samples,tr2) || missing>TR64_LIMIT(sound_missing,tr2) || offset!=64+count*32 ||
    bytes!=((offset+missing*8+15)&~15u))return 0;
 for(i=40;i<64;++i)if(p[i])return 0;
 cursor=bytes;
 for(i=0;i<count;++i) {
  const uint8_t *s=snd_sample(p,i);uint32_t frames=snd_be32(s+4),rate=snd_be32(s+8);
  uint32_t encoding=snd_be32(s+12),size=snd_be32(s+20);
  if(snd_be32(s)!=cursor || !frames || (rate!=11025 && rate!=22050) ||
     encoding<1 || encoding>3 || snd_be32(s+16) || (snd_be32(s+24)&~1u) || snd_be32(s+28) ||
     cursor>total || size>total-cursor || total-cursor-size<((16u-size)&15u))return 0;
  if(encoding==3) {
   if(frames%28 || rate!=11025 || size!=frames/28*16)return 0;
  } else if(frames>UINT32_MAX/encoding || size!=frames*encoding)return 0;
  cursor+=(size+15)&~15u;
 }
 if(cursor!=total)return 0;
 for(i=0;i<missing;++i) {
  uint32_t sound=snd_be32(p+offset+i*8),sample=snd_be32(p+offset+i*8+4);
  if(sound>=256 || sample<count || sample>65535 ||
     (i && (sound<last_sound || (sound==last_sound && sample<=last_sample))))return 0;
  last_sound=sound;last_sample=sample;
 }
 for(j=offset+missing*8;j<bytes;++j)if(p[j])return 0;
 return 1;
}
