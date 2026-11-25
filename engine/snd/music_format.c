/* SPDX-License-Identifier: MIT */
#include "engine/snd/music_format.h"
#include "engine/snd/music_decode.h"
#include "engine/core/sys.h"
#include <string.h>
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
void tr64_music_format(const void *header,uint32_t bytes,uint32_t total,
                       uint32_t expected_track,tr64_music_format_t *f)
{
    const uint8_t *p=header;uint32_t book_offset,book_bytes,units;
    if(bytes<64 || memcmp(p,"MU64",4) || be32(p+4)!=1 || be32(p+8)!=total ||
       be32(p+16)!=expected_track || expected_track<2 ||
       (expected_track>59 && expected_track!=TR64_MUSIC_FILM_TRACK) || be32(p+60))
        sys_error("music invalid native header");
    memset(f,0,sizeof(*f));f->total=total;f->flags=be32(p+12);f->track=expected_track;
    if(f->flags==TR64_MUSIC_UNAVAILABLE) {
        uint32_t i;
        if(bytes!=64 || total!=64)sys_error("music unavailable extent");
        for(i=20;i<64;i+=4)if(be32(p+i))sys_error("music unavailable payload");
        return;
    }
    if(f->flags)sys_error("music unknown flags");
    f->codec=be32(p+20);f->rate=be32(p+24);f->frames=be32(p+32);
    f->data_offset=be32(p+36);f->data_bytes=be32(p+40);
    f->unit_bytes=be32(p+44);f->unit_frames=be32(p+48);
    book_offset=be32(p+52);book_bytes=be32(p+56);
    if((f->rate!=16000 && f->rate!=22050) || be32(p+28)!=1 || !f->frames ||
       f->data_offset!=bytes || bytes>total || f->data_bytes!=total-bytes)
        sys_error("music invalid rate/extent");
    if(f->codec==TR64_MUSIC_SPU) {
        if(f->unit_bytes!=16 || f->unit_frames!=28)sys_error("music SPU framing");
    } else sys_error("music unknown codec");
    if(bytes!=64 || book_offset || book_bytes)sys_error("music unexpected codebook");
    units=(f->frames-1)/f->unit_frames+1;
    if(units>UINT32_MAX/f->unit_bytes || units*f->unit_bytes!=f->data_bytes)
        sys_error("music decoded length mismatch");
}
